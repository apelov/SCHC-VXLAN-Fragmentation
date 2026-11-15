#include "daemon.h"
#include <stdio.h>
#include <string.h>
struct link { struct schcvx_daemon *peer; const char *local,*source; uint64_t now; const uint8_t *expected; size_t len,delivered; };
static int send_packet(void *ctx,size_t i,const uint8_t *p,size_t n) { struct link *l=ctx; (void)i; return schcvx_process_datagram(l->peer,l->local,60789,l->source,p,n,l->now); }
static int write_frame(void *ctx,size_t i,const uint8_t *p,size_t n) { struct link *l=ctx; (void)i; if(n!=l->len || memcmp(p,l->expected,n)) return -1; l->delivered++; return 0; }
static uint64_t now(void *ctx) { return ((struct link *)ctx)->now; }
int main(int argc,char **argv) { struct schcvx_config a,b; struct schcvx_daemon *da,*db; struct link la={0},lb={0}; struct schcvx_io ia={&la,send_packet,write_frame,now},ib={&lb,send_packet,write_frame,now}; char e[256]; uint8_t frame[9014]; size_t sizes[]={60,1243,1244,1514,9014},i,j; int mode;
 if(argc!=2 || schcvx_config_load(argv[1],&a,e,sizeof e)) return 1; for(i=0;i<sizeof frame;i++) frame[i]=(uint8_t)(i*37); b=a; strcpy(b.assoc[0].local_ip,"192.0.2.2"); strcpy(b.assoc[0].peer_ip,"192.0.2.1");
 for(mode=0;mode<2;mode++) { a.assoc[0].tolerant=mode; b.assoc[0].tolerant=mode; la=(struct link){0}; lb=(struct link){0}; la.local="192.0.2.2"; la.source="192.0.2.1"; lb.local="192.0.2.1"; lb.source="192.0.2.2"; la.now=lb.now=300000000; da=schcvx_daemon_create(&a,&ia,0); db=schcvx_daemon_create(&b,&ib,0); if(!da || !db) { fprintf(stderr,"create failed\n"); return 1; } la.peer=db; lb.peer=da; for(j=0;j<5;j++) { la.expected=lb.expected=frame; la.len=lb.len=sizes[j]; if(schcvx_process_frame(da,0,frame,sizes[j],la.now) || schcvx_process_frame(db,0,frame,sizes[j],lb.now)) { fprintf(stderr,"transfer failed mode %d length %zu\n",mode,sizes[j]); return 1; } } if(la.delivered!=5 || lb.delivered!=5) return 1; schcvx_daemon_destroy(da); schcvx_daemon_destroy(db); } return 0; }
