#include "config.h"
#include "schcvx/dtag.h"
#include "schcvx/wire.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

static char *trim(char *s) { char *e; while (isspace((unsigned char)*s)) s++; e=s+strlen(s); while(e>s && isspace((unsigned char)e[-1])) *--e=0; return s; }
static int number(const char *s, uint64_t *v) { char *e; unsigned long long n; if (!isdigit((unsigned char)*s)) return -1; errno=0; n=strtoull(s,&e,10); if(errno || *trim(e)) return -1; *v=n; return 0; }
static int ip(const char *s) { unsigned a,b,c,d; char x; return sscanf(s,"%u.%u.%u.%u%c",&a,&b,&c,&d,&x)==4 && a<256 && b<256 && c<256 && d<256; }
static int copy(char *dst,size_t n,const char *s) { if (!*s || strlen(s)>=n) return -1; memcpy(dst,s,strlen(s)+1); return 0; }
static int fail(char *e,size_t n,const char *section,const char *key,const char *why) { if(n) snprintf(e,n,"%s: %s: %s",section,key,why); return -1; }
static const char *const keys[]={"local_ip","peer_ip","udp_port","src_port","variant","fr_rule_id","cd_rule_id","vnis","tap","pmtu","h_ip","max_frame_len","max_fragments","receiver","S","B","I","Delta","R","Q","max_entries","max_bytes","orphan_budget","dtag_start","counters_file","counters_interval","dtag_rate","dtag_burst"};
static const char *const globals[]={"max_entries","max_bytes","orphan_budget"};
static int find(const char *s,const char *const *list,size_t n) { size_t i; for(i=0;i<n;i++) if(!strcmp(s,list[i])) return (int)i; return -1; }
static int value(struct schcvx_config_assoc *a,int k,char *v) {
 uint64_t x=0; char *p,*next; size_t count=0;
 if(k==0) return ip(v)?copy(a->local_ip,sizeof a->local_ip,v):-1;
 if(k==1) return ip(v)?copy(a->peer_ip,sizeof a->peer_ip,v):-1;
 if(k==4) { if(!strcmp(v,"compact")) a->tx.variant=SCHCVX_COMPACT; else if(!strcmp(v,"extended")) a->tx.variant=SCHCVX_EXTENDED; else return -1; return 0; }
 if(k==7) { p=v; while(p && *p) { next=strchr(p,','); if(next) *next++=0; if(count==SCHCVX_MAX_VNIS || number(trim(p),&x) || x>0xffffff) return -1; a->vnis[count++]=(uint32_t)x; p=next; } a->vni_count=count; return count?0:-1; }
 if(k==8) return copy(a->tap,sizeof a->tap,v);
 if(k==13) { if(!strcmp(v,"ordered")) a->tolerant=false; else if(!strcmp(v,"tolerant")) a->tolerant=true; else return -1; return 0; }
 if(k==24) return copy(a->counters_file,sizeof a->counters_file,v);
 if(number(v,&x)) return -1;
 switch(k) {
 case 2: case 3: if(!x || x>65535) return -1; if(k==2) a->udp_port=(uint16_t)x; else a->src_port=(uint16_t)x; break;
 case 5: case 6: if(x>255) return -1; if(k==5) a->fr_rule_id=(uint8_t)x; else a->cd_rule_id=(uint8_t)x; break;
 case 9: case 10: case 11: if(x>UINT32_MAX) return -1; if(k==9) a->tx.pmtu=(uint32_t)x; else if(k==10) a->tx.ip_header_len=(uint32_t)x; else a->tx.max_frame_len=(uint32_t)x; break;
 case 12: if(x>255) return -1; a->tx.max_fragments=(uint8_t)x; break;
 case 14: a->S=x; break; case 15: a->B=x; break; case 16: a->I=x; break; case 17: a->Delta=x; break; case 18: a->R=x; break; case 19: a->Q=x; break;
 case 20: case 21: case 22: if(x>SIZE_MAX) return -1; if(k==20) a->max_entries=(size_t)x; else if(k==21) a->max_bytes=(size_t)x; else a->orphan_budget=(size_t)x; break;
 case 23: a->dtag_start=x; break; case 25: a->counters_interval=x; break;
 case 26: case 27: if(!x || x>UINT32_MAX) return -1; if(k==26) a->dtag_rate=(uint32_t)x; else a->dtag_burst=(uint32_t)x; break;
 default: return -1; } return 0;
}
int schcvx_config_load(const char *path,struct schcvx_config *out,char *error,size_t error_size) {
 FILE *f; char line[1024], section[80]="file"; struct schcvx_config c={0}; uint64_t masks[SCHCVX_MAX_ASSOC]={0}, gm=0, x; int current=-2, k; size_t i; unsigned lineno=0;
 f=fopen(path,"r"); if(!f) return fail(error,error_size,"file",path,"cannot open");
 while(fgets(line,sizeof line,f)) { char *s,*eq,*end; lineno++; if(!strchr(line,'\n') && !feof(f)) { fclose(f); return fail(error,error_size,section,"line","too long"); } s=trim(line); if(!*s || *s=='#' || *s==';') continue;
 if(*s=='[') { end=strchr(s,']'); if(!end || *trim(end+1)) goto badsection; *end=0; if(!strcmp(s+1,"global")) { if(current!=-2 || gm) goto badsection; current=-1; strcpy(section,"global"); } else if(!strncmp(s+1,"association ",12) && c.count<SCHCVX_MAX_ASSOC) { char *name=trim(s+13); if(!*name) goto badsection; for(i=0;i<c.count;i++) if(!strcmp(c.assoc[i].name,name)) goto badsection; current=(int)c.count++; if(copy(c.assoc[current].name,sizeof c.assoc[current].name,name)) goto badsection; snprintf(section,sizeof section,"association %s",name); c.assoc[current].tx.min_frame_len=14; } else goto badsection; continue; }
 eq=strchr(s,'='); if(!eq || current==-2) goto badsection; *eq++=0; s=trim(s); eq=trim(eq);
 if(current==-1) { k=find(s,globals,3); if(k<0 || (gm & (UINT64_C(1)<<k)) || number(eq,&x) || x>SIZE_MAX || !x) { fclose(f); return fail(error,error_size,section,s,"unknown, duplicate or invalid key"); } gm|=UINT64_C(1)<<k; if(k==0)c.max_entries=(size_t)x; else if(k==1)c.max_bytes=(size_t)x; else c.orphan_budget=(size_t)x; }
 else { k=find(s,keys,sizeof keys/sizeof keys[0]); if(k<0 || (masks[current]&(UINT64_C(1)<<k)) || value(&c.assoc[current],k,eq)) { fclose(f); return fail(error,error_size,section,s,"unknown, duplicate or invalid key"); } masks[current]|=UINT64_C(1)<<k; }
 continue;
 badsection: fclose(f); if(error_size) snprintf(error,error_size,"%s: line %u: invalid section or syntax",section,lineno); return -1;
 }
 if(ferror(f)) { fclose(f); return fail(error,error_size,"file",path,"read error"); } fclose(f);
 for(i=0;i<3;i++) if(!(gm&(UINT64_C(1)<<i))) return fail(error,error_size,"global",globals[i],"missing key");
 if(!c.count) return fail(error,error_size,"file","association","missing section");
 for(i=0;i<c.count;i++) { struct schcvx_config_assoc *a=&c.assoc[i]; struct schcvx_assoc_wire w={a->tx.variant,&a->fr_rule_id,1,a->cd_rule_id}; struct schcvx_dtag d; uint64_t W,D; size_t j; snprintf(section,sizeof section,"association %s",a->name);
 for(j=0;j<sizeof keys/sizeof keys[0];j++) if(!(masks[i]&(UINT64_C(1)<<j))) return fail(error,error_size,section,keys[j],"missing key");
 if(schcvx_wire_config_check(&w)) return fail(error,error_size,section,"fr_rule_id","not prefix-free");
 if(schcvx_tx_config_check(&a->tx)) return fail(error,error_size,section,"pmtu/max_frame_len/max_fragments","invalid transmit configuration");
 if(!a->S || !a->I || !a->R || !a->Q || !a->counters_interval || !a->max_entries || !a->max_bytes) return fail(error,error_size,section,"timers/limits","must be positive");
 if(UINT64_MAX-a->S<a->B) return fail(error,error_size,section,"S/B","overflow");
 W=a->S+a->B;
 if(a->R<W) return fail(error,error_size,section,"R","must be >= S + B");
 if(a->tolerant && a->Delta<W) return fail(error,error_size,section,"Delta","must be >= S + B");
 if(UINT64_MAX-W<a->I) return fail(error,error_size,section,"I","overflow");
 D=W+a->I;
 if(a->Delta>D) D=a->Delta;
 if(UINT64_MAX-a->B<D || UINT64_MAX-(a->B+D)<a->R || a->Q<=a->B+D+a->R) return fail(error,error_size,section,"Q","must exceed B + D + R");
 if(schcvx_dtag_init(&d,a->tx.variant==SCHCVX_COMPACT?32:60,a->dtag_start,0,a->Q,a->dtag_rate,a->dtag_burst)) return fail(error,error_size,section,"dtag_rate/dtag_burst/dtag_start","invalid allocator configuration");
 }
 *out=c; return 0;
}
