#include "schcvx/rx_ordered.h"
#include "schcvx/tx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)

/* Scripted scenarios with explicit timestamps. Inactivity I=10, retention 100. */
static uint8_t g_rule=1; static uint32_t g_vni=100;
struct fx { struct schcvx_rx_ordered rx; struct schcvx_counters c; void *mem; struct schcvx_delivery out; };
static int fx_open(struct fx *f, size_t max_entries, size_t max_bytes) {
 struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&g_rule,1,0};
 struct schcvx_rx_ordered_cfg cfg={wire,&g_vni,1,max_entries,8,max_bytes,600,10,100};
 size_t bytes=schcvx_rx_ordered_bytes(&cfg); memset(&f->c,0,sizeof(f->c)); f->mem=malloc(bytes); CHECK(f->mem);
 CHECK(!schcvx_rx_ordered_init(&f->rx,&cfg,f->mem,bytes,&f->c)); return 0;
}
/* Builds k fragments (80-byte tiles, 10-byte last) for the DTag with the given VNI. */
static int build(unsigned k, uint64_t dtag, uint32_t vni, uint8_t dg[8][700], size_t sizes[8]) {
 struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&g_rule,1,0}; uint8_t prefix[9], frame[600];
 struct schcvx_tx_plan plan={false,(uint8_t)k,80,10}; size_t length=80*(k-1)+10;
 for (size_t i=0;i<sizeof(frame);i++) frame[i]=(uint8_t)(i*7+3);
 CHECK(!schcvx_packet_prefix(0,vni,prefix));
 for (unsigned j=0;j<k;j++) CHECK(!schcvx_tx_fragment(&plan,&wire,1,dtag,prefix,frame,length-9,j,dg[j],700,&sizes[j]));
 return 0;
}
static int scen_all1_loss(void) {
 struct fx f; uint8_t dg[8][700]; size_t sz[8]; CHECK(!fx_open(&f,4,4096)); CHECK(!build(3,5,100,dg,sz));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[0],sz[0],100,&f.out)); CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[1],sz[1],105,&f.out));
 schcvx_rx_ordered_sweep(&f.rx,114); CHECK(f.c.expired==0);   /* 1 ns before last regular + I */
 schcvx_rx_ordered_sweep(&f.rx,115); CHECK(f.c.expired==1 && !f.c.packets_delivered);
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[2],sz[2],116,&f.out)); CHECK(f.c.late==1 && !f.c.packets_delivered);
 free(f.mem); return 0;
}
static int scen_integrity(void) {
 struct fx f; uint8_t dg[8][700]; size_t sz[8];
 /* Lost regular fragment: All-1 finds a CRC mismatch. */
 CHECK(!fx_open(&f,4,4096)); CHECK(!build(4,5,100,dg,sz));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[0],sz[0],0,&f.out)); CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[2],sz[2],1,&f.out));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[3],sz[3],2,&f.out)); CHECK(f.c.integrity_failures==1 && !f.c.packets_delivered); free(f.mem);
 /* Duplicate regular fragment. */
 CHECK(!fx_open(&f,4,4096));
 for (unsigned j=0;j<3;j++) CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[j],sz[j],j,&f.out));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[1],sz[1],3,&f.out)); CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[3],sz[3],4,&f.out));
 CHECK(f.c.integrity_failures==1 && !f.c.packets_delivered); free(f.mem);
 /* Bad RCS. */
 CHECK(!fx_open(&f,4,4096)); dg[3][5]^=1;
 for (unsigned j=0;j<4;j++) CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[j],sz[j],j,&f.out));
 CHECK(f.c.integrity_failures==1 && !f.c.packets_delivered); dg[3][5]^=1; free(f.mem);
 /* Reordering: swap two regular fragments (expected loss of the ordered receiver). */
 CHECK(!fx_open(&f,4,4096)); unsigned order[4]={0,2,1,3};
 for (unsigned j=0;j<4;j++) CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[order[j]],sz[order[j]],j,&f.out));
 CHECK(f.c.integrity_failures==1 && !f.c.packets_delivered); free(f.mem);
 return 0;
}
static int scen_abort_admission(void) {
 struct fx f; uint8_t dg[8][700], ab[16]; size_t sz[8];
 struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&g_rule,1,0}; struct schcvx_msg am={.kind=SCHCVX_ABORT,.rule_id=1,.dtag=5,.fcn=15};
 size_t an=schcvx_encode_header(&wire,&am,ab); CHECK(an);
 /* Sender-Abort retires the active entry. */
 CHECK(!fx_open(&f,4,4096)); CHECK(!build(3,5,100,dg,sz));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[0],sz[0],0,&f.out)); CHECK(!schcvx_rx_ordered_receive(&f.rx,ab,an,1,&f.out));
 CHECK(f.c.sender_aborts_received==1); CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[1],sz[1],2,&f.out)); CHECK(f.c.late==1); free(f.mem);
 /* Unauthorised VNI: the completed packet is not delivered. */
 CHECK(!fx_open(&f,4,4096)); CHECK(!build(3,5,999,dg,sz));
 for (unsigned j=0;j<3;j++) CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[j],sz[j],j,&f.out));
 CHECK(f.c.vni_admission_failures==1 && !f.c.packets_delivered); free(f.mem);
 /* Unknown fragmentation RuleID. */
 CHECK(!fx_open(&f,4,4096)); CHECK(!build(3,5,100,dg,sz)); dg[0][0]=(uint8_t)(0x30|(dg[0][0]&0x0f));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[0],sz[0],0,&f.out)); CHECK(f.c.unknown_rule==1 && !f.c.malformed); free(f.mem);
 return 0;
}
static int scen_exhaustion(void) {
 struct fx f; uint8_t dg[8][700]; size_t sz[8];
 /* Entry cap: a third DTag does not fit in a two-entry table. */
 CHECK(!fx_open(&f,2,4096));
 for (unsigned d=1;d<=3;d++) { CHECK(!build(3,d,100,dg,sz)); CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[0],sz[0],d,&f.out)); }
 CHECK(f.c.table_exhaustion==1); free(f.mem);
 /* Byte cap: 100 bytes hold one 80-byte tile but not two. */
 CHECK(!fx_open(&f,4,100)); CHECK(!build(3,5,100,dg,sz));
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[0],sz[0],0,&f.out)); CHECK(f.c.table_exhaustion==0);
 CHECK(!schcvx_rx_ordered_receive(&f.rx,dg[1],sz[1],1,&f.out)); CHECK(f.c.table_exhaustion==1); free(f.mem);
 return 0;
}
int main(void) {
 uint8_t rule=1, prefix[9], frame[600], dg[8][700]; size_t sizes[8]; uint32_t vni=100;
 struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&rule,1,0};
 struct schcvx_rx_ordered_cfg cfg={wire,&vni,1,4,8,4096,600,10,100};
 size_t bytes=schcvx_rx_ordered_bytes(&cfg); void *mem=malloc(bytes); CHECK(mem);
 struct schcvx_rx_ordered rx; struct schcvx_counters c={0}; struct schcvx_delivery out;
 CHECK(!schcvx_rx_ordered_init(&rx,&cfg,mem,bytes,&c));
 for (size_t i=0;i<sizeof(frame);i++) frame[i]=(uint8_t)i;
 CHECK(!schcvx_packet_prefix(0,100,prefix));
 for (unsigned k=2;k<=8;k++) {
  struct schcvx_tx_plan plan={false,(uint8_t)k,80,10}; size_t length=80*(k-1)+10;
  for (unsigned j=0;j<k;j++) CHECK(!schcvx_tx_fragment(&plan,&wire,1,k,prefix,frame,length-9,j,dg[j],sizeof(dg[j]),&sizes[j]));
  for (unsigned j=0;j<k;j++) CHECK(schcvx_rx_ordered_receive(&rx,dg[j],sizes[j],k*20+j,&out)==(j==k-1));
  CHECK(out.vni==100 && out.frame_len==length-9 && !memcmp(out.frame,frame,length-9));
  CHECK(!schcvx_rx_ordered_receive(&rx,dg[0],sizes[0],k*20+k,&out)); CHECK(c.late==k-1);
 }
 uint8_t packet[23]; memcpy(packet,prefix,9); memcpy(packet+9,frame,14);
 CHECK(schcvx_rx_ordered_receive(&rx,packet,sizeof(packet),300,&out)); packet[1]=0; CHECK(!schcvx_rx_ordered_receive(&rx,packet,sizeof(packet),301,&out));
 free(mem);
 if (scen_all1_loss()||scen_integrity()||scen_abort_admission()||scen_exhaustion()) return 1;
 return 0;
}
