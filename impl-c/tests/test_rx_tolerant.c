#include "schcvx/rx_tolerant.h"
#include "schcvx/tx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("failed line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static uint8_t rule=1; static uint32_t vni=100;
static struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&rule,1,0};
static uint8_t fragments[8][256]; static size_t lengths[8]; static unsigned k;
static void make(unsigned count) {
    uint8_t prefix[9], frame[200]; memset(frame,0x5a,sizeof(frame)); CHECK(!schcvx_packet_prefix(0,vni,prefix));
    struct schcvx_tx_plan plan={false,(uint8_t)count,23,(uint32_t)(count==2?23:1)};
    size_t frame_len=(count-1)*23+plan.last_len-9; CHECK(frame_len<=sizeof(frame));
    for(unsigned i=0;i<count;i++) CHECK(!schcvx_tx_fragment(&plan,&wire,rule,42,prefix,frame,frame_len,i,fragments[i],sizeof(fragments[i]),&lengths[i])); k=count;
}
static void init(struct schcvx_rx_tolerant *r,struct schcvx_rx_tolerant_pool *pool,struct schcvx_counters *c,void **mem,uint64_t inactivity) {
    *pool=(struct schcvx_rx_tolerant_pool){10,2000,10,0,0,0}; memset(c,0,sizeof(*c));
    struct schcvx_rx_tolerant_cfg cfg={.wire=wire,.vnis=&vni,.vni_count=1,.max_entries=4,.retirement_capacity=8,.max_bytes=1000,.max_frame_len=200,.max_tile_len=200,.orphan_budget=4,.deadline_ns=100,.inactivity_ns=inactivity,.retention_ns=50,.pool=pool};
    size_t n=schcvx_rx_tolerant_bytes(&cfg); CHECK(n); *mem=malloc(n); CHECK(*mem); CHECK(!schcvx_rx_tolerant_init(r,&cfg,*mem,n,c));
}
static void permute(unsigned *a,unsigned at) {
    if(at==k) { struct schcvx_rx_tolerant r; struct schcvx_rx_tolerant_pool p; struct schcvx_counters c; void *mem; struct schcvx_delivery d; init(&r,&p,&c,&mem,0); int delivered=0;
        for(unsigned i=0;i<k;i++) delivered+=schcvx_rx_tolerant_receive(&r,fragments[a[i]],lengths[a[i]],i,&d);
        CHECK(delivered==1 && c.packets_delivered==1 && !p.entries && !p.bytes); free(mem); return; }
    for(unsigned i=at;i<k;i++) { unsigned t=a[at]; a[at]=a[i]; a[i]=t; permute(a,at+1); t=a[at]; a[at]=a[i]; a[i]=t; }
}
int main(void) {
    unsigned a[8]={0,1,2,3,4,5,6,7};
    for(unsigned n=2;n<=8;n++) { make(n); permute(a,0);
        for(unsigned lost=0;lost<n;lost++) { struct schcvx_rx_tolerant r; struct schcvx_rx_tolerant_pool p; struct schcvx_counters c; void *mem; struct schcvx_delivery d; init(&r,&p,&c,&mem,0);
            for(unsigned i=0;i<n;i++) if(i!=lost) { CHECK(!schcvx_rx_tolerant_receive(&r,fragments[i],lengths[i],0,&d)); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[i],lengths[i],1,&d)); }
            CHECK(c.duplicates==n-1); schcvx_rx_tolerant_sweep(&r,100); CHECK(c.expired==1 && !p.entries); free(mem); }
    }
    make(2); struct schcvx_rx_tolerant r; struct schcvx_rx_tolerant_pool p; struct schcvx_counters c; void *mem; struct schcvx_delivery d;
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],0,&d)); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],100,&d)); CHECK(c.expired==1 && c.late==1); free(mem);
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],0,&d)); CHECK(schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],99,&d)==1); free(mem);
    init(&r,&p,&c,&mem,10); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],0,&d)); schcvx_rx_tolerant_sweep(&r,10); CHECK(c.expired==1); free(mem);
    make(3);
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],0,&d)); fragments[1][lengths[1]-1]^=1; CHECK(!schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],1,&d)); CHECK(c.integrity_failures==1 && !p.entries); fragments[1][lengths[1]-1]^=1; free(mem);
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[2],lengths[2],0,&d)); fragments[2][5]^=1; CHECK(!schcvx_rx_tolerant_receive(&r,fragments[2],lengths[2],1,&d)); CHECK(c.integrity_failures==1); fragments[2][5]^=1; free(mem);
    init(&r,&p,&c,&mem,0); fragments[2][5]^=1; for(unsigned i=0;i<3;i++) CHECK(!schcvx_rx_tolerant_receive(&r,fragments[i],lengths[i],i,&d)); CHECK(c.integrity_failures==1 && !p.entries); fragments[2][5]^=1; free(mem);
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],0,&d)); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0]-1,1,&d)); CHECK(c.integrity_failures==1); free(mem);
    init(&r,&p,&c,&mem,0); r.cfg.orphan_budget=0; CHECK(!schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],0,&d)); CHECK(c.table_exhaustion==1 && !p.entries); free(mem);
    init(&r,&p,&c,&mem,0); p.max_entries=0; CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],0,&d)); CHECK(c.table_exhaustion==1); free(mem);
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[1],lengths[1],0,&d)); r.cfg.vni_count=0; CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],1,&d)); CHECK(c.vni_admission_failures==1 && !p.entries); free(mem);
    init(&r,&p,&c,&mem,0); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],0,&d)); schcvx_rx_tolerant_sweep(&r,100); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],101,&d)); CHECK(c.late==1); schcvx_rx_tolerant_sweep(&r,150); CHECK(!schcvx_rx_tolerant_receive(&r,fragments[0],lengths[0],150,&d)); CHECK(p.entries==1); free(mem);
    return 0;
}
