#include "schcvx/retire.h"
#include <string.h>
#include <stdint.h>
/* Records form a FIFO ring ordered by expiry (expiry = now + R, R constant, now monotonic).
 * The hash index maps (rule, dtag) to the ring's physical position. */
size_t schcvx_retire_index_slots(size_t capacity) { return schcvx_hidx_slots(capacity); }
size_t schcvx_retire_bytes(size_t capacity) {
    size_t slots=schcvx_hidx_slots(capacity), total=0;
    if (!slots || !schcvx_size_add(&total,capacity,sizeof(struct schcvx_retire_record)) || !schcvx_size_add(&total,slots,sizeof(struct schcvx_hslot))) return 0;
    return total;
}
void schcvx_retire_init(struct schcvx_retire *t, struct schcvx_retire_record *r, size_t n, struct schcvx_hslot *index, size_t index_slots, uint64_t retention) {
    t->records=r; t->capacity=n; t->active=0; t->retired=0; t->retention_ns=retention;
    t->head=0; t->index=index; t->index_slots=index_slots;
    if (n) memset(r, 0, n*sizeof(*r));
    if (index_slots) memset(index, 0, index_slots*sizeof(*index));
}
static bool match(const void *ctx, uint32_t item, uint8_t rule, uint64_t dtag) {
    const struct schcvx_retire *t=ctx; return t->records[item].rule_id==rule && t->records[item].dtag==dtag;
}
void schcvx_retire_expire(struct schcvx_retire *t, uint64_t now) {
    while (t->retired && now>=t->records[t->head].expiry) {
        struct schcvx_retire_record *r=&t->records[t->head];
        size_t pos=schcvx_hidx_find(t->index,t->index_slots,t,match,r->rule_id,r->dtag);
        if (pos!=SCHCVX_HIDX_NONE) schcvx_hidx_erase_at(t->index,t->index_slots,pos);
        r->used=false; t->head++; if (t->head==t->capacity) t->head=0; t->retired--;
    }
}
bool schcvx_retire_contains(const struct schcvx_retire *t, uint8_t rule, uint64_t dtag) {
    return t->retired && schcvx_hidx_find(t->index,t->index_slots,t,match,rule,dtag)!=SCHCVX_HIDX_NONE;
}
bool schcvx_retire_reserve(struct schcvx_retire *t) { if (t->active>=t->capacity-t->retired) return false; t->active++; return true; }
void schcvx_retire_release(struct schcvx_retire *t) { if (t->active) t->active--; }
bool schcvx_retire_insert(struct schcvx_retire *t, uint8_t rule, uint64_t dtag, uint64_t now) {
    if (!t->active || t->retired>=t->capacity || schcvx_retire_contains(t,rule,dtag)) return false;
    uint64_t expiry=UINT64_MAX-now<t->retention_ns ? UINT64_MAX : now+t->retention_ns;
    size_t pos=t->head+t->retired; if (pos>=t->capacity) pos-=t->capacity;
    if (t->retired) { size_t last=pos?pos-1:t->capacity-1; if (expiry<t->records[last].expiry) expiry=t->records[last].expiry; }
    t->records[pos]=(struct schcvx_retire_record){dtag, expiry, rule, true};
    schcvx_hidx_insert(t->index,t->index_slots,rule,dtag,(uint32_t)pos);
    t->active--; t->retired++; return true;
}
bool schcvx_retire_next_deadline(const struct schcvx_retire *t, uint64_t *deadline) {
    if (!t->retired) return false;
    *deadline=t->records[t->head].expiry; return true;
}
