#include "schcvx/rx_ordered.h"
#include "schcvx/rx_tolerant.h"
#include "schcvx/tx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { printf("failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)

/* Performance regression: 200,000 two-fragment packets at 100,000 packets/s (10 us apart), R = 100 ms
 * so about 10,000 identifiers are retired at any moment. With linear scans over the retirement table
 * this takes minutes; with the hashed/FIFO structures it takes well under a second. No timing is asserted. */
#define PACKETS 200000u
static uint8_t g_rule=1; static uint32_t g_vni=100;
static const uint64_t STEP=10000, R=100000000, I=10000000, DELTA=20000000;

static int make(struct schcvx_assoc_wire *wire, uint64_t dtag, uint8_t dg[2][700], size_t sz[2]) {
    uint8_t prefix[9], frame[600]; struct schcvx_tx_plan plan={false,2,80,10}; size_t length=80+10;
    for (size_t i=0;i<sizeof(frame);i++) frame[i]=(uint8_t)(i*7+3);
    CHECK(!schcvx_packet_prefix(0,g_vni,prefix));
    for (unsigned j=0;j<2;j++) CHECK(!schcvx_tx_fragment(&plan,wire,1,dtag,prefix,frame,length-9,j,dg[j],700,&sz[j]));
    return 0;
}
static int run_ordered(void) {
    struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&g_rule,1,0}; struct schcvx_counters c; memset(&c,0,sizeof c);
    struct schcvx_rx_ordered_cfg cfg={.wire=wire,.vnis=&g_vni,.vni_count=1,.max_entries=1024,.retirement_capacity=65536,.max_bytes=4194304,.max_frame_len=1518,.inactivity_ns=I,.retention_ns=R};
    size_t bytes=schcvx_rx_ordered_bytes(&cfg); void *mem=malloc(bytes); struct schcvx_rx_ordered rx; struct schcvx_delivery out;
    CHECK(bytes && mem && !schcvx_rx_ordered_init(&rx,&cfg,mem,bytes,&c));
    uint8_t dg[2][700]; size_t sz[2]; unsigned delivered=0;
    for (unsigned p=0;p<PACKETS;p++) {
        CHECK(!make(&wire,p,dg,sz)); uint64_t now=(uint64_t)p*STEP;
        CHECK(!schcvx_rx_ordered_receive(&rx,dg[0],sz[0],now,&out));
        delivered+=(unsigned)schcvx_rx_ordered_receive(&rx,dg[1],sz[1],now+STEP/2,&out);
    }
    CHECK(delivered==PACKETS && c.packets_delivered==PACKETS && c.table_exhaustion==0 && c.late==0 && c.expired==0);
    free(mem); return 0;
}
static int run_tolerant(void) {
    struct schcvx_assoc_wire wire={SCHCVX_COMPACT,&g_rule,1,0}; struct schcvx_counters c; memset(&c,0,sizeof c);
    struct schcvx_rx_tolerant_pool pool={4096,16777216,1024,0,0,0};
    struct schcvx_rx_tolerant_cfg cfg={.wire=wire,.vnis=&g_vni,.vni_count=1,.max_entries=1024,.retirement_capacity=65536,.max_bytes=4194304,.max_frame_len=1518,.max_tile_len=1518,.orphan_budget=256,.deadline_ns=DELTA,.inactivity_ns=I,.retention_ns=R,.pool=&pool};
    size_t bytes=schcvx_rx_tolerant_bytes(&cfg); void *mem=malloc(bytes); struct schcvx_rx_tolerant rx; struct schcvx_delivery out;
    CHECK(bytes && mem && !schcvx_rx_tolerant_init(&rx,&cfg,mem,bytes,&c));
    uint8_t dg[2][700]; size_t sz[2]; unsigned delivered=0;
    for (unsigned p=0;p<PACKETS;p++) {
        CHECK(!make(&wire,p,dg,sz)); uint64_t now=(uint64_t)p*STEP;
        CHECK(!schcvx_rx_tolerant_receive(&rx,dg[0],sz[0],now,&out));
        delivered+=(unsigned)schcvx_rx_tolerant_receive(&rx,dg[1],sz[1],now+STEP/2,&out);
    }
    CHECK(delivered==PACKETS && c.packets_delivered==PACKETS && c.table_exhaustion==0 && c.late==0 && c.expired==0 && !pool.entries && !pool.bytes);
    free(mem); return 0;
}
int main(void) { return run_ordered() || run_tolerant(); }
