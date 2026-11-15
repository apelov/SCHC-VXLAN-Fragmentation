#include "schcvx/rx_tolerant.h"
#include "schcvx/crc32c.h"
#include <string.h>
#include <stdint.h>
static uint64_t add(uint64_t a,uint64_t b) { return UINT64_MAX-a<b ? UINT64_MAX : a+b; }
size_t schcvx_rx_tolerant_bytes(const struct schcvx_rx_tolerant_cfg *c) {
    if (!c || schcvx_wire_config_check(&c->wire) || !c->max_entries || !c->retirement_capacity || !c->max_bytes || c->max_frame_len<14 || c->max_frame_len>SIZE_MAX-9 || !c->max_tile_len || !c->deadline_ns || !c->retention_ns || (c->vni_count&&!c->vnis) || !c->pool) return 0;
    size_t s=c->max_frame_len+9;
    if (c->max_entries>SIZE_MAX/sizeof(struct schcvx_rx_tolerant_entry) || c->retirement_capacity>SIZE_MAX/sizeof(struct schcvx_retire_record) || c->max_entries>SIZE_MAX/s) return 0;
    size_t a=c->max_entries*sizeof(struct schcvx_rx_tolerant_entry), b=c->retirement_capacity*sizeof(struct schcvx_retire_record), d=c->max_entries*s;
    if (a>SIZE_MAX-b || a+b>SIZE_MAX-d || a+b+d>SIZE_MAX-s) return 0;
    return a+b+d+s;
}
int schcvx_rx_tolerant_init(struct schcvx_rx_tolerant *r,const struct schcvx_rx_tolerant_cfg *c,void *buf,size_t bytes,struct schcvx_counters *counts) {
    size_t n=schcvx_rx_tolerant_bytes(c);
    if (!r || !buf || !counts || !n || bytes<n || (uintptr_t)buf%_Alignof(max_align_t)) return -1;
    r->cfg=*c; r->counters=counts; r->retained=0; r->orphans=0; r->entries=buf;
    memset(r->entries,0,c->max_entries*sizeof(*r->entries));
    struct schcvx_retire_record *records=(void *)((uint8_t *)buf+c->max_entries*sizeof(*r->entries));
    schcvx_retire_init(&r->retire,records,c->retirement_capacity,c->retention_ns);
    r->packets=(uint8_t *)(records+c->retirement_capacity); r->assembly=r->packets+c->max_entries*(c->max_frame_len+9); return 0;
}
static uint64_t deadline(const struct schcvx_rx_tolerant *r,const struct schcvx_rx_tolerant_entry *e) { return r->cfg.inactivity_ns && e->inactivity<e->deadline ? e->inactivity : e->deadline; }
static void finish(struct schcvx_rx_tolerant *r,size_t i,uint64_t now) {
    struct schcvx_rx_tolerant_entry *e=&r->entries[i]; struct schcvx_rx_tolerant_pool *p=r->cfg.pool;
    r->retained-=e->len; p->bytes-=e->len; p->entries--; if(e->orphan) { r->orphans--; p->orphans--; }
    (void)schcvx_retire_insert(&r->retire,e->rule_id,e->dtag,now); e->used=false;
}
void schcvx_rx_tolerant_sweep(struct schcvx_rx_tolerant *r,uint64_t now) {
    schcvx_retire_expire(&r->retire,now);
    for(size_t i=0;i<r->cfg.max_entries;i++) if(r->entries[i].used && now>=deadline(r,&r->entries[i])) { uint64_t t=deadline(r,&r->entries[i]); r->counters->expired++; finish(r,i,t); }
    schcvx_retire_expire(&r->retire,now);
}
bool schcvx_rx_tolerant_next_deadline(const struct schcvx_rx_tolerant *r,uint64_t *out) {
    bool found=schcvx_retire_next_deadline(&r->retire,out);
    for(size_t i=0;i<r->cfg.max_entries;i++) if(r->entries[i].used && (!found || deadline(r,&r->entries[i])<*out)) { *out=deadline(r,&r->entries[i]); found=true; }
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
    size_t i; for(i=0;i<r->cfg.max_entries;i++) if(r->entries[i].used && r->entries[i].rule_id==m.rule_id && r->entries[i].dtag==m.dtag) break;
    if(m.kind==SCHCVX_ABORT) { r->counters->sender_aborts_received++; if(i<r->cfg.max_entries) finish(r,i,now); return 0; }
    size_t stride=r->cfg.max_frame_len+9; struct schcvx_rx_tolerant_pool *pool=r->cfg.pool;
    if(m.payload_len>r->cfg.max_tile_len || m.payload_len>stride) { if(i<r->cfg.max_entries) { r->counters->integrity_failures++; finish(r,i,now); } else r->counters->malformed++; return 0; }
    bool fresh=i==r->cfg.max_entries;
    if(fresh) {
        bool orphan=!m.first;
        for(i=0;i<r->cfg.max_entries && r->entries[i].used;i++) {}
        if(i==r->cfg.max_entries || pool->entries>=pool->max_entries || (orphan && (r->orphans>=r->cfg.orphan_budget || pool->orphans>=pool->max_orphans)) || m.payload_len>r->cfg.max_bytes-r->retained || pool->bytes>pool->max_bytes || m.payload_len>pool->max_bytes-pool->bytes || !schcvx_retire_reserve(&r->retire)) { r->counters->table_exhaustion++; return 0; }
        r->entries[i]=(struct schcvx_rx_tolerant_entry){.dtag=m.dtag,.deadline=add(now,r->cfg.deadline_ns),.inactivity=add(now,r->cfg.inactivity_ns),.rule_id=m.rule_id,.used=true,.orphan=orphan};
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
    if(m.payload_len>r->cfg.max_bytes-r->retained || pool->bytes>pool->max_bytes || m.payload_len>pool->max_bytes-pool->bytes) { r->counters->table_exhaustion++; if(fresh) { pool->entries--; if(e->orphan) { r->orphans--; pool->orphans--; } schcvx_retire_release(&r->retire); e->used=false; } return 0; }
    piece->offset=e->len; piece->len=m.payload_len; piece->fcn=m.fcn; piece->present=true; memcpy(base+e->len,m.payload,m.payload_len); e->len+=m.payload_len; r->retained+=m.payload_len; pool->bytes+=m.payload_len;
    if(m.kind==SCHCVX_ALL1) e->rcs=m.rcs; else { e->tile_len=m.payload_len; if(r->cfg.inactivity_ns) e->inactivity=add(now,r->cfg.inactivity_ns); }
    if(m.first) { e->first=true; e->n=m.countdown; if(e->orphan) { e->orphan=false; r->orphans--; pool->orphans--; } }
    if(!e->first || !e->pieces[7].present) return 0;
    for(unsigned j=0;j<=e->n;j++) if(!e->pieces[j].present) return 0;
    size_t off=0; for(int j=e->n;j>=0;j--) { memcpy(r->assembly+off,base+e->pieces[j].offset,e->pieces[j].len); off+=e->pieces[j].len; }
    memcpy(r->assembly+off,base+e->pieces[7].offset,e->pieces[7].len); off+=e->pieces[7].len;
    int result=0; if(schcvx_crc32c(r->assembly,off)==e->rcs) result=deliver(r,r->assembly,off,out); else r->counters->integrity_failures++;
    finish(r,i,now); return result;
}
