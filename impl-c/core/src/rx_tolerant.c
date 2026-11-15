#include "schcvx/rx_tolerant.h"
#include "schcvx/crc32c.h"
#include <string.h>
#include <stdint.h>
static uint64_t add(uint64_t a,uint64_t b) { return UINT64_MAX-a<b ? UINT64_MAX : a+b; }
/* Memory: entries | retire records | retire index | entry index | free stack | packet buffers | assembly. */
struct layout { size_t records, rindex, hindex, stack, packets, assembly, total, hslots; };
static int layout(const struct schcvx_rx_tolerant_cfg *c, struct layout *l) {
    if (!c || schcvx_wire_config_check(&c->wire) || !c->max_entries || !c->retirement_capacity || !c->max_bytes || c->max_frame_len<14 || c->max_frame_len>SIZE_MAX-9 || !c->max_tile_len || !c->deadline_ns || !c->retention_ns || (c->vni_count&&!c->vnis) || !c->pool) return -1;
    size_t rs=schcvx_hidx_slots(c->retirement_capacity); l->hslots=schcvx_hidx_slots(c->max_entries);
    if (!rs || !l->hslots || c->max_entries>=UINT32_MAX/4) return -1;
    size_t s=c->max_frame_len+9, t=0;
    if (!schcvx_size_add(&t,c->max_entries,sizeof(struct schcvx_rx_tolerant_entry))) return -1;
    l->records=t; if (!schcvx_size_add(&t,c->retirement_capacity,sizeof(struct schcvx_retire_record))) return -1;
    l->rindex=t; if (!schcvx_size_add(&t,rs,sizeof(struct schcvx_hslot))) return -1;
    l->hindex=t; if (!schcvx_size_add(&t,l->hslots,sizeof(struct schcvx_hslot))) return -1;
    l->stack=t; if (!schcvx_size_add(&t,c->max_entries,sizeof(uint32_t))) return -1;
    l->packets=t; if (!schcvx_size_add(&t,c->max_entries,s)) return -1;
    l->assembly=t; if (!schcvx_size_add(&t,1,s)) return -1;
    l->total=t; return 0;
}
size_t schcvx_rx_tolerant_bytes(const struct schcvx_rx_tolerant_cfg *c) {
    struct layout l; return layout(c,&l) ? 0 : l.total;
}
int schcvx_rx_tolerant_init(struct schcvx_rx_tolerant *r,const struct schcvx_rx_tolerant_cfg *c,void *buf,size_t bytes,struct schcvx_counters *counts) {
    struct layout l; size_t n=layout(c,&l) ? 0 : l.total; uint8_t *b=buf;
    if (!r || !buf || !counts || !n || bytes<n || (uintptr_t)buf%_Alignof(max_align_t)) return -1;
    r->cfg=*c; r->counters=counts; r->retained=0; r->orphans=0; r->entries=buf;
    memset(r->entries,0,c->max_entries*sizeof(*r->entries));
    schcvx_retire_init(&r->retire,(void *)(b+l.records),c->retirement_capacity,(void *)(b+l.rindex),schcvx_hidx_slots(c->retirement_capacity),c->retention_ns);
    r->hash=(void *)(b+l.hindex); r->hash_slots=l.hslots; memset(r->hash,0,l.hslots*sizeof(*r->hash));
    r->free_stack=(void *)(b+l.stack); r->free_top=c->max_entries;
    for (size_t i=0;i<c->max_entries;i++) r->free_stack[i]=(uint32_t)(c->max_entries-1-i);
    r->dhead=r->dtail=r->ihead=r->itail=SCHCVX_NIL;
    r->packets=b+l.packets; r->assembly=b+l.assembly; return 0;
}
static bool match(const void *ctx, uint32_t item, uint8_t rule, uint64_t dtag) {
    const struct schcvx_rx_tolerant *r=ctx; return r->entries[item].rule_id==rule && r->entries[item].dtag==dtag;
}
/* List D orders entries by e->deadline, list I by e->inactivity (only kept when I != 0). */
static void d_unlink(struct schcvx_rx_tolerant *r, uint32_t i) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i];
    if (e->dprev==SCHCVX_NIL) r->dhead=e->dnext; else r->entries[e->dprev].dnext=e->dnext;
    if (e->dnext==SCHCVX_NIL) r->dtail=e->dprev; else r->entries[e->dnext].dprev=e->dprev;
}
static void d_append(struct schcvx_rx_tolerant *r, uint32_t i) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i];
    e->dprev=r->dtail; e->dnext=SCHCVX_NIL;
    if (r->dtail==SCHCVX_NIL) r->dhead=i; else r->entries[r->dtail].dnext=i;
    r->dtail=i;
}
static void i_unlink(struct schcvx_rx_tolerant *r, uint32_t i) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i];
    if (e->iprev==SCHCVX_NIL) r->ihead=e->inext; else r->entries[e->iprev].inext=e->inext;
    if (e->inext==SCHCVX_NIL) r->itail=e->iprev; else r->entries[e->inext].iprev=e->iprev;
}
static void i_append(struct schcvx_rx_tolerant *r, uint32_t i) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i];
    e->iprev=r->itail; e->inext=SCHCVX_NIL;
    if (r->itail==SCHCVX_NIL) r->ihead=i; else r->entries[r->itail].inext=i;
    r->itail=i;
}
/* Removes an active entry from every structure (lists, index, in-use flag, free stack). */
static void drop(struct schcvx_rx_tolerant *r, uint32_t i) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i];
    size_t pos=schcvx_hidx_find(r->hash,r->hash_slots,r,match,e->rule_id,e->dtag);
    if (pos!=SCHCVX_HIDX_NONE) schcvx_hidx_erase_at(r->hash,r->hash_slots,pos);
    d_unlink(r,i); if (r->cfg.inactivity_ns) i_unlink(r,i);
    e->used=false; r->free_stack[r->free_top++]=i;
}
static void finish(struct schcvx_rx_tolerant *r,size_t i,uint64_t now) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i]; struct schcvx_rx_tolerant_pool *p=r->cfg.pool;
    r->retained-=e->len; p->bytes-=e->len; p->entries--; if(e->orphan) { r->orphans--; p->orphans--; }
    (void)schcvx_retire_insert(&r->retire,e->rule_id,e->dtag,now); drop(r,(uint32_t)i);
}
/* An entry expires at min(deadline, inactivity) (inactivity only when I != 0). Both keys only grow
 * and each list is sorted, so the earliest expiry over all entries is the smaller of the two list
 * heads, and entries are retired in expiry order (which keeps the retirement ring sorted). A sweep
 * costs O(1) plus O(1) per expired entry. */
void schcvx_rx_tolerant_sweep(struct schcvx_rx_tolerant *r,uint64_t now) {
    schcvx_retire_expire(&r->retire,now);
    for(;;) {
        uint32_t d=r->dhead, in=r->cfg.inactivity_ns ? r->ihead : SCHCVX_NIL, i; uint64_t t;
        if (d==SCHCVX_NIL) break;
        if (in!=SCHCVX_NIL && r->entries[in].inactivity<r->entries[d].deadline) { i=in; t=r->entries[in].inactivity; } else { i=d; t=r->entries[d].deadline; }
        if (now<t) break;
        r->counters->expired++; finish(r,i,t);
    }
    schcvx_retire_expire(&r->retire,now);
}
bool schcvx_rx_tolerant_next_deadline(const struct schcvx_rx_tolerant *r,uint64_t *out) {
    bool found=schcvx_retire_next_deadline(&r->retire,out);
    if (r->dhead!=SCHCVX_NIL && (!found || r->entries[r->dhead].deadline<*out)) { *out=r->entries[r->dhead].deadline; found=true; }
    if (r->cfg.inactivity_ns && r->ihead!=SCHCVX_NIL && (!found || r->entries[r->ihead].inactivity<*out)) { *out=r->entries[r->ihead].inactivity; found=true; }
    return found;
}
static int deliver(struct schcvx_rx_tolerant *r,const uint8_t *p,size_t n,struct schcvx_delivery *out) {
    int v=schcvx_rx_validate(r->cfg.wire.cd_rule_id,r->cfg.vnis,r->cfg.vni_count,p,n,out);
    if(v==SCHCVX_RX_VALID) { r->counters->packets_delivered++; return 1; }
    if(v==SCHCVX_RX_UNAUTHORISED) r->counters->vni_admission_failures++; else r->counters->malformed++; return 0;
}
int schcvx_rx_tolerant_receive(struct schcvx_rx_tolerant *r,const uint8_t *data,size_t len,uint64_t now,struct schcvx_delivery *out) {
    struct schcvx_msg m; int rc=schcvx_classify(&r->cfg.wire,data,len,&m);
    if(rc) { if(rc==-SCHCVX_E_UNKNOWN_RULE) r->counters->unknown_rule++; else r->counters->malformed++; return 0; }
    schcvx_rx_tolerant_sweep(r,now);
    if(m.kind==SCHCVX_UNFRAGMENTED) return deliver(r,m.payload,m.payload_len,out);
    if(schcvx_retire_contains(&r->retire,m.rule_id,m.dtag)) { r->counters->late++; return 0; }
    size_t hp=schcvx_hidx_find(r->hash,r->hash_slots,r,match,m.rule_id,m.dtag);
    size_t i=hp==SCHCVX_HIDX_NONE ? SIZE_MAX : r->hash[hp].item1-1;
    if(m.kind==SCHCVX_ABORT) { r->counters->sender_aborts_received++; if(i!=SIZE_MAX) finish(r,i,now); return 0; }
    size_t stride=r->cfg.max_frame_len+9; struct schcvx_rx_tolerant_pool *pool=r->cfg.pool;
    if(m.payload_len>r->cfg.max_tile_len || m.payload_len>stride) { r->counters->malformed++; return 0; }
    bool fresh=i==SIZE_MAX;
    if(fresh) {
        bool orphan=!m.first;
        if(!r->free_top || pool->entries>=pool->max_entries || (orphan && (r->orphans>=r->cfg.orphan_budget || pool->orphans>=pool->max_orphans)) || m.payload_len>r->cfg.max_bytes-r->retained || pool->bytes>pool->max_bytes || m.payload_len>pool->max_bytes-pool->bytes || !schcvx_retire_reserve(&r->retire)) { r->counters->table_exhaustion++; return 0; }
        i=r->free_stack[--r->free_top];
        r->entries[i]=(struct schcvx_rx_tolerant_entry){.dtag=m.dtag,.deadline=add(now,r->cfg.deadline_ns),.inactivity=add(now,r->cfg.inactivity_ns),.rule_id=m.rule_id,.used=true,.orphan=orphan};
        schcvx_hidx_insert(r->hash,r->hash_slots,m.rule_id,m.dtag,(uint32_t)i); d_append(r,(uint32_t)i); if(r->cfg.inactivity_ns) i_append(r,(uint32_t)i);
        pool->entries++; if(orphan) { r->orphans++; pool->orphans++; }
    }
    struct schcvx_rx_tolerant_entry *e=&r->entries[i]; unsigned pos=m.kind==SCHCVX_ALL1 ? 7 : m.countdown; struct schcvx_rx_tolerant_piece *piece=&e->pieces[pos]; uint8_t *base=r->packets+i*stride;
    if(piece->present) {
        if(piece->fcn==m.fcn && piece->len==m.payload_len && !memcmp(base+piece->offset,m.payload,m.payload_len) && (m.kind!=SCHCVX_ALL1 || e->rcs==m.rcs)) { r->counters->duplicates++; return 0; }
        r->counters->integrity_failures++; finish(r,i,now); return 0;
    }
    bool conflict=(m.first && e->first) || (e->first && m.kind==SCHCVX_REGULAR && m.countdown>e->n);
    if(m.first) for(unsigned j=m.countdown+1;j<7;j++) if(e->pieces[j].present) conflict=true;
    if(m.kind==SCHCVX_REGULAR && e->tile_len && e->tile_len!=m.payload_len) conflict=true;
    if(m.kind==SCHCVX_ALL1 && e->tile_len && m.payload_len>e->tile_len) conflict=true;
    if(m.kind==SCHCVX_REGULAR && e->pieces[7].present && e->pieces[7].len>m.payload_len) conflict=true;
    if(m.payload_len>stride-e->len) conflict=true;
    if(conflict) { r->counters->integrity_failures++; finish(r,i,now); return 0; }
    if(m.first) {
        /* rx_common validates a complete packet; pad only the Ethernet portion. */
        uint8_t prefix[23]={0}; memcpy(prefix,m.payload,9); struct schcvx_delivery tmp;
        int v=schcvx_rx_validate(r->cfg.wire.cd_rule_id,r->cfg.vnis,r->cfg.vni_count,prefix,sizeof(prefix),&tmp);
        if(v!=SCHCVX_RX_VALID) { if(v==SCHCVX_RX_UNAUTHORISED) r->counters->vni_admission_failures++; else r->counters->malformed++; finish(r,i,now); return 0; }
    }
    if(m.payload_len>r->cfg.max_bytes-r->retained || pool->bytes>pool->max_bytes || m.payload_len>pool->max_bytes-pool->bytes) { r->counters->table_exhaustion++; if(fresh) { pool->entries--; if(e->orphan) { r->orphans--; pool->orphans--; } schcvx_retire_release(&r->retire); drop(r,(uint32_t)i); } return 0; }
    piece->offset=e->len; piece->len=m.payload_len; piece->fcn=m.fcn; piece->present=true; memcpy(base+e->len,m.payload,m.payload_len); e->len+=m.payload_len; r->retained+=m.payload_len; pool->bytes+=m.payload_len;
    if(m.kind==SCHCVX_ALL1) e->rcs=m.rcs; else { e->tile_len=m.payload_len; if(r->cfg.inactivity_ns) { e->inactivity=add(now,r->cfg.inactivity_ns); i_unlink(r,(uint32_t)i); i_append(r,(uint32_t)i); } }
    if(m.first) { e->first=true; e->n=m.countdown; if(e->orphan) { e->orphan=false; r->orphans--; pool->orphans--; } }
    if(!e->first || !e->pieces[7].present) return 0;
    for(unsigned j=0;j<=e->n;j++) if(!e->pieces[j].present) return 0;
    size_t off=0; for(int j=e->n;j>=0;j--) { memcpy(r->assembly+off,base+e->pieces[j].offset,e->pieces[j].len); off+=e->pieces[j].len; }
    memcpy(r->assembly+off,base+e->pieces[7].offset,e->pieces[7].len); off+=e->pieces[7].len;
    int result=0; if(schcvx_crc32c(r->assembly,off)==e->rcs) result=deliver(r,r->assembly,off,out); else r->counters->integrity_failures++;
    finish(r,i,now); return result;
}
