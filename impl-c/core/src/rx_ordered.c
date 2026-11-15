#include "schcvx/rx_ordered.h"
#include "schcvx/crc32c.h"
#include <stdint.h>
#include <string.h>
static uint64_t add_time(uint64_t a, uint64_t b) { return UINT64_MAX-a<b ? UINT64_MAX : a+b; }
/* Memory: entries | retire records | retire index | entry index | free stack | packet buffers. */
struct layout { size_t records, rindex, hindex, stack, packets, total; size_t hslots; };
static int layout(const struct schcvx_rx_ordered_cfg *c, struct layout *l) {
    if (!c || schcvx_wire_config_check(&c->wire) || !c->max_entries || !c->retirement_capacity || !c->max_bytes || c->max_frame_len<14 || !c->inactivity_ns || !c->retention_ns || (c->vni_count && !c->vnis) || c->max_frame_len>SIZE_MAX-9) return -1;
    size_t rs=schcvx_hidx_slots(c->retirement_capacity); l->hslots=schcvx_hidx_slots(c->max_entries);
    if (!rs || !l->hslots || c->max_entries>=UINT32_MAX/4) return -1;
    size_t t=0, stride=c->max_frame_len+9;
    if (!schcvx_size_add(&t,c->max_entries,sizeof(struct schcvx_rx_ordered_entry))) return -1;
    l->records=t; if (!schcvx_size_add(&t,c->retirement_capacity,sizeof(struct schcvx_retire_record))) return -1;
    l->rindex=t; if (!schcvx_size_add(&t,rs,sizeof(struct schcvx_hslot))) return -1;
    l->hindex=t; if (!schcvx_size_add(&t,l->hslots,sizeof(struct schcvx_hslot))) return -1;
    l->stack=t; if (!schcvx_size_add(&t,c->max_entries,sizeof(uint32_t))) return -1;
    l->packets=t; if (!schcvx_size_add(&t,c->max_entries,stride)) return -1;
    l->total=t; return 0;
}
size_t schcvx_rx_ordered_bytes(const struct schcvx_rx_ordered_cfg *c) {
    struct layout l; return layout(c,&l) ? 0 : l.total;
}
int schcvx_rx_ordered_init(struct schcvx_rx_ordered *r, const struct schcvx_rx_ordered_cfg *c, void *buf, size_t bytes, struct schcvx_counters *counts) {
    struct layout l; size_t n=layout(c,&l) ? 0 : l.total; uint8_t *b=buf;
    if (!r || !buf || !counts || !n || bytes<n || (uintptr_t)buf%_Alignof(max_align_t)) return -1;
    r->cfg=*c; r->counters=counts; r->retained=0; r->entries=buf;
    memset(r->entries,0,c->max_entries*sizeof(*r->entries));
    schcvx_retire_init(&r->retire,(void *)(b+l.records),c->retirement_capacity,(void *)(b+l.rindex),schcvx_hidx_slots(c->retirement_capacity),c->retention_ns);
    r->hash=(void *)(b+l.hindex); r->hash_slots=l.hslots; memset(r->hash,0,l.hslots*sizeof(*r->hash));
    r->free_stack=(void *)(b+l.stack); r->free_top=c->max_entries;
    for (size_t i=0;i<c->max_entries;i++) r->free_stack[i]=(uint32_t)(c->max_entries-1-i);
    r->head=r->tail=SCHCVX_NIL;
    r->packets=b+l.packets; return 0;
}
static bool match(const void *ctx, uint32_t item, uint8_t rule, uint64_t dtag) {
    const struct schcvx_rx_ordered *r=ctx; return r->entries[item].rule_id==rule && r->entries[item].dtag==dtag;
}
static void unlink_entry(struct schcvx_rx_ordered *r, uint32_t i) {
    struct schcvx_rx_ordered_entry *e=&r->entries[i];
    if (e->prev==SCHCVX_NIL) r->head=e->next; else r->entries[e->prev].next=e->next;
    if (e->next==SCHCVX_NIL) r->tail=e->prev; else r->entries[e->next].prev=e->prev;
}
static void append(struct schcvx_rx_ordered *r, uint32_t i) {
    struct schcvx_rx_ordered_entry *e=&r->entries[i];
    e->prev=r->tail; e->next=SCHCVX_NIL;
    if (r->tail==SCHCVX_NIL) r->head=i; else r->entries[r->tail].next=i;
    r->tail=i;
}
/* Removes an active entry from every structure (list, index, in-use flag, free stack). */
static void drop(struct schcvx_rx_ordered *r, uint32_t i) {
    struct schcvx_rx_ordered_entry *e=&r->entries[i];
    size_t pos=schcvx_hidx_find(r->hash,r->hash_slots,r,match,e->rule_id,e->dtag);
    if (pos!=SCHCVX_HIDX_NONE) schcvx_hidx_erase_at(r->hash,r->hash_slots,pos);
    unlink_entry(r,i); e->used=false; r->free_stack[r->free_top++]=i;
}
static void finish(struct schcvx_rx_ordered *r, size_t i, uint64_t now) {
    struct schcvx_rx_ordered_entry *e=&r->entries[i];
    r->retained-=e->len; (void)schcvx_retire_insert(&r->retire,e->rule_id,e->dtag,now); drop(r,(uint32_t)i);
}
/* Active entries sit in a list ordered by deadline (deadline = receive time + I, every refresh
 * moves the entry to the tail), so the earliest deadline is the head and a sweep costs O(1)
 * plus O(1) per expired entry. */
void schcvx_rx_ordered_sweep(struct schcvx_rx_ordered *r, uint64_t now) {
    schcvx_retire_expire(&r->retire,now);
    while (r->head!=SCHCVX_NIL && now>=r->entries[r->head].deadline) { uint32_t i=r->head; uint64_t deadline=r->entries[i].deadline; r->counters->expired++; finish(r,i,deadline); }
    schcvx_retire_expire(&r->retire,now);
}
bool schcvx_rx_ordered_next_deadline(const struct schcvx_rx_ordered *r, uint64_t *deadline) {
    bool found=schcvx_retire_next_deadline(&r->retire,deadline);
    if (r->head!=SCHCVX_NIL && (!found || r->entries[r->head].deadline<*deadline)) { *deadline=r->entries[r->head].deadline; found=true; }
    return found;
}
static int deliver(struct schcvx_rx_ordered *r, const uint8_t *p, size_t n, struct schcvx_delivery *out) {
    int result=schcvx_rx_validate(r->cfg.wire.cd_rule_id,r->cfg.vnis,r->cfg.vni_count,p,n,out);
    if (result==SCHCVX_RX_VALID) { r->counters->packets_delivered++; return 1; }
    if (result==SCHCVX_RX_UNAUTHORISED) r->counters->vni_admission_failures++; else r->counters->malformed++;
    return 0;
}
int schcvx_rx_ordered_receive(struct schcvx_rx_ordered *r, const uint8_t *data, size_t len, uint64_t now, struct schcvx_delivery *out) {
    struct schcvx_msg m; int rc=schcvx_classify(&r->cfg.wire,data,len,&m);
    if (rc) { if (rc==-SCHCVX_E_UNKNOWN_RULE) r->counters->unknown_rule++; else r->counters->malformed++; return 0; }
    schcvx_rx_ordered_sweep(r,now);
    if (m.kind==SCHCVX_UNFRAGMENTED) return deliver(r,m.payload,m.payload_len,out);
    if (schcvx_retire_contains(&r->retire,m.rule_id,m.dtag)) { r->counters->late++; return 0; }
    size_t hp=schcvx_hidx_find(r->hash,r->hash_slots,r,match,m.rule_id,m.dtag);
    size_t i=hp==SCHCVX_HIDX_NONE ? SIZE_MAX : r->hash[hp].item1-1;
    if (m.kind==SCHCVX_ABORT) { r->counters->sender_aborts_received++; if (i!=SIZE_MAX) finish(r,i,now); return 0; }
    if (i==SIZE_MAX) {
        if (!r->free_top || !schcvx_retire_reserve(&r->retire)) { r->counters->table_exhaustion++; return 0; }
        i=r->free_stack[--r->free_top];
        r->entries[i]=(struct schcvx_rx_ordered_entry){.dtag=m.dtag,.deadline=add_time(now,r->cfg.inactivity_ns),.rule_id=m.rule_id,.used=true};
        schcvx_hidx_insert(r->hash,r->hash_slots,m.rule_id,m.dtag,(uint32_t)i); append(r,(uint32_t)i);
    }
    struct schcvx_rx_ordered_entry *e=&r->entries[i]; size_t stride=r->cfg.max_frame_len+9;
    if (m.payload_len>stride-e->len) { r->counters->integrity_failures++; finish(r,i,now); return 0; }
    if (m.payload_len>r->cfg.max_bytes-r->retained) { r->counters->table_exhaustion++; if (!e->len) { schcvx_retire_release(&r->retire); drop(r,(uint32_t)i); } return 0; }
    uint8_t *packet=r->packets+i*stride; memcpy(packet+e->len,m.payload,m.payload_len); e->len+=m.payload_len; r->retained+=m.payload_len;
    if (m.kind==SCHCVX_REGULAR) { e->deadline=add_time(now,r->cfg.inactivity_ns); unlink_entry(r,(uint32_t)i); append(r,(uint32_t)i); return 0; }
    int delivered=0; if (schcvx_crc32c(packet,e->len)==m.rcs) delivered=deliver(r,packet,e->len,out); else r->counters->integrity_failures++;
    finish(r,i,now); return delivered;
}
