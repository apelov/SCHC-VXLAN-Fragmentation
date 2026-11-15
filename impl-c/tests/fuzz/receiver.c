#include "schcvx/rx_tolerant.h"
#include "schcvx/tx.h"
#include "schcvx/crc32c.h"
#include <stdlib.h>
#include <string.h>
#define LIMIT 128
struct observed { uint64_t tag; uint8_t prefix[9]; uint32_t crc; bool first, all1; };
static uint8_t next(const uint8_t *data, size_t size, size_t *p) { return *p < size ? data[(*p)++] : 0; }
static void check(bool ok) { if (!ok) abort(); }
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    uint8_t rule=1, prefix[9], frame[151], packet[256]; uint32_t vni=100;
    size_t pos=0, seen=0; struct observed obs[LIMIT]={{0}}; struct schcvx_counters counters={0};
    struct schcvx_assoc_wire wire={(enum schcvx_variant)(next(data,size,&pos)&1),&rule,1,0};
#ifdef FUZZ_TOLERANT
    struct schcvx_rx_tolerant rx; struct schcvx_rx_tolerant_pool pool={4,512,4,0,0,0};
    struct schcvx_rx_tolerant_cfg cfg={.wire=wire,.vnis=&vni,.vni_count=1,.max_entries=4,.retirement_capacity=6,.max_bytes=512,.max_frame_len=200,.max_tile_len=200,.orphan_budget=4,.deadline_ns=50,.inactivity_ns=20,.retention_ns=30,.pool=&pool};
    size_t bytes=schcvx_rx_tolerant_bytes(&cfg);
#else
    struct schcvx_rx_ordered rx;
    struct schcvx_rx_ordered_cfg cfg={.wire=wire,.vnis=&vni,.vni_count=1,.max_entries=4,.retirement_capacity=6,.max_bytes=512,.max_frame_len=200,.inactivity_ns=20,.retention_ns=30};
    size_t bytes=schcvx_rx_ordered_bytes(&cfg);
#endif
    void *mem=malloc(bytes); check(bytes && mem);
#ifdef FUZZ_TOLERANT
    check(!schcvx_rx_tolerant_init(&rx,&cfg,mem,bytes,&counters));
#else
    check(!schcvx_rx_ordered_init(&rx,&cfg,mem,bytes,&counters));
#endif
    check(!schcvx_packet_prefix(0,vni,prefix)); uint64_t now=0;
    for(size_t event=0;event<LIMIT && pos<size;event++) {
        uint8_t flags=next(data,size,&pos); now+=next(data,size,&pos)%16;
        if(flags&1) {
#ifdef FUZZ_TOLERANT
            schcvx_rx_tolerant_sweep(&rx,now);
#else
            schcvx_rx_ordered_sweep(&rx,now);
#endif
        }
        size_t len=0;
        if(flags&2) {
            unsigned k=2+next(data,size,&pos)%7, index=next(data,size,&pos)%k;
            uint64_t tag=next(data,size,&pos)%8; uint8_t seed=next(data,size,&pos);
            for(size_t i=0;i<sizeof(frame);i++) frame[i]=(uint8_t)(seed+i);
            struct schcvx_tx_plan plan={false,(uint8_t)k,20,20};
            check(!schcvx_tx_fragment(&plan,&wire,rule,tag,prefix,frame,k*20-9,index,packet,sizeof(packet),&len));
            if(flags&4) packet[len-1]^=1;
        } else {
            len=next(data,size,&pos)%80; if(len>size-pos) len=size-pos;
            memcpy(packet,data+pos,len); pos+=len;
        }
        struct schcvx_msg msg; int classified=schcvx_classify(&wire,packet,len,&msg); bool retired=false;
        if(!classified && msg.kind!=SCHCVX_UNFRAGMENTED) {
            for(size_t i=0;i<rx.retire.capacity;i++) {
                struct schcvx_retire_record *r=&rx.retire.records[i];
                if(r->used && r->expiry>now && r->rule_id==msg.rule_id && r->dtag==msg.dtag) retired=true;
            }
            if(seen<LIMIT && ((msg.kind==SCHCVX_REGULAR && msg.first && msg.payload_len>=9) || msg.kind==SCHCVX_ALL1)) {
                struct observed *o=&obs[seen++]; o->tag=msg.dtag;
                if(msg.kind==SCHCVX_ALL1) { o->all1=true; o->crc=msg.rcs; }
                else { o->first=true; memcpy(o->prefix,msg.payload,9); }
            }
        }
        struct schcvx_delivery out;
#ifdef FUZZ_TOLERANT
        int delivered=schcvx_rx_tolerant_receive(&rx,packet,len,now,&out);
#else
        int delivered=schcvx_rx_ordered_receive(&rx,packet,len,now,&out);
#endif
        check(rx.retained<=cfg.max_bytes && rx.retire.active+rx.retire.retired<=cfg.retirement_capacity);
        size_t entries=0; for(size_t i=0;i<cfg.max_entries;i++) entries+=rx.entries[i].used;
        check(entries<=cfg.max_entries && entries==rx.retire.active);
#ifdef FUZZ_TOLERANT
        check(pool.entries<=pool.max_entries && pool.bytes<=pool.max_bytes && pool.orphans<=pool.max_orphans && rx.orphans<=cfg.orphan_budget);
#endif
        if(delivered && !classified && msg.kind!=SCHCVX_UNFRAGMENTED) {
            check(!retired); bool matched=false;
            for(size_t i=0;i<seen;i++) if(obs[i].first && obs[i].tag==msg.dtag) {
                uint32_t state; schcvx_crc32c_init(&state); schcvx_crc32c_update(&state,obs[i].prefix,9); schcvx_crc32c_update(&state,out.frame,out.frame_len);
                uint32_t crc=schcvx_crc32c_final(state);
                for(size_t j=0;j<seen;j++) if(obs[j].all1 && obs[j].tag==msg.dtag && obs[j].crc==crc) matched=true;
            }
            check(matched);
        }
    }
    free(mem); return 0;
}
