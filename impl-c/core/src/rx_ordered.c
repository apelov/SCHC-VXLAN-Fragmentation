#include "schcvx/rx_ordered.h"
#include "schcvx/crc32c.h"
#include <stdint.h>
#include <string.h>
static uint64_t add_time(uint64_t a, uint64_t b) { return UINT64_MAX-a<b ? UINT64_MAX : a+b; }
size_t schcvx_rx_ordered_bytes(const struct schcvx_rx_ordered_cfg *c) {
    if (!c || schcvx_wire_config_check(&c->wire) || !c->max_entries || !c->retirement_capacity || !c->max_bytes || c->max_frame_len<14 || !c->inactivity_ns || !c->retention_ns || (c->vni_count && !c->vnis) || c->max_frame_len>SIZE_MAX-9) return 0;
    size_t stride=c->max_frame_len+9;
    if (c->max_entries>SIZE_MAX/sizeof(struct schcvx_rx_ordered_entry) || c->retirement_capacity>SIZE_MAX/sizeof(struct schcvx_retire_record) || c->max_entries>SIZE_MAX/stride) return 0;
    size_t a=c->max_entries*sizeof(struct schcvx_rx_ordered_entry), b=c->retirement_capacity*sizeof(struct schcvx_retire_record), d=c->max_entries*stride;
    if (a>SIZE_MAX-b || a+b>SIZE_MAX-d) return 0;
    return a+b+d;
}
int schcvx_rx_ordered_init(struct schcvx_rx_ordered *r, const struct schcvx_rx_ordered_cfg *c, void *buf, size_t bytes, struct schcvx_counters *counts) {
    size_t n=schcvx_rx_ordered_bytes(c);
    if (!r || !buf || !counts || !n || bytes<n || (uintptr_t)buf%_Alignof(max_align_t)) return -1;
    r->cfg=*c; r->counters=counts; r->retained=0; r->entries=buf;
    memset(r->entries,0,c->max_entries*sizeof(*r->entries));
    struct schcvx_retire_record *records=(void *)((uint8_t *)buf+c->max_entries*sizeof(*r->entries));
    schcvx_retire_init(&r->retire,records,c->retirement_capacity,c->retention_ns);
    r->packets=(uint8_t *)(records+c->retirement_capacity); return 0;
}
static void finish(struct schcvx_rx_ordered *r, size_t i, uint64_t now) {
    struct schcvx_rx_ordered_entry *e=&r->entries[i];
    r->retained-=e->len; (void)schcvx_retire_insert(&r->retire,e->rule_id,e->dtag,now); e->used=false;
}
void schcvx_rx_ordered_sweep(struct schcvx_rx_ordered *r, uint64_t now) {
    schcvx_retire_expire(&r->retire,now);
    for (size_t i=0;i<r->cfg.max_entries;i++) if (r->entries[i].used && now>=r->entries[i].deadline) { uint64_t deadline=r->entries[i].deadline; r->counters->expired++; finish(r,i,deadline); }
    schcvx_retire_expire(&r->retire,now);
}
bool schcvx_rx_ordered_next_deadline(const struct schcvx_rx_ordered *r, uint64_t *deadline) {
    bool found=schcvx_retire_next_deadline(&r->retire,deadline);
    for (size_t i=0;i<r->cfg.max_entries;i++) if (r->entries[i].used && (!found || r->entries[i].deadline<*deadline)) { *deadline=r->entries[i].deadline; found=true; }
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
    size_t i; for (i=0;i<r->cfg.max_entries;i++) if (r->entries[i].used && r->entries[i].rule_id==m.rule_id && r->entries[i].dtag==m.dtag) break;
    if (m.kind==SCHCVX_ABORT) { r->counters->sender_aborts_received++; if (i<r->cfg.max_entries) finish(r,i,now); return 0; }
    if (i==r->cfg.max_entries) {
        for (i=0;i<r->cfg.max_entries && r->entries[i].used;i++) {}
        if (i==r->cfg.max_entries || !schcvx_retire_reserve(&r->retire)) { r->counters->table_exhaustion++; return 0; }
        r->entries[i]=(struct schcvx_rx_ordered_entry){m.dtag,add_time(now,r->cfg.inactivity_ns),0,m.rule_id,true};
    }
    struct schcvx_rx_ordered_entry *e=&r->entries[i]; size_t stride=r->cfg.max_frame_len+9;
    if (m.payload_len>stride-e->len) { r->counters->integrity_failures++; finish(r,i,now); return 0; }
    if (m.payload_len>r->cfg.max_bytes-r->retained) { r->counters->table_exhaustion++; if (!e->len) { schcvx_retire_release(&r->retire); e->used=false; } return 0; }
    uint8_t *packet=r->packets+i*stride; memcpy(packet+e->len,m.payload,m.payload_len); e->len+=m.payload_len; r->retained+=m.payload_len;
    if (m.kind==SCHCVX_REGULAR) { e->deadline=add_time(now,r->cfg.inactivity_ns); return 0; }
    int delivered=0; if (schcvx_crc32c(packet,e->len)==m.rcs) delivered=deliver(r,packet,e->len,out); else r->counters->integrity_failures++;
    finish(r,i,now); return delivered;
}
