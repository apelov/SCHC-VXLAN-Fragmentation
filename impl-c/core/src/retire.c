#include "schcvx/retire.h"
#include <string.h>
#include <stdint.h>
void schcvx_retire_init(struct schcvx_retire *t, struct schcvx_retire_record *r, size_t n, uint64_t retention) {
    t->records=r; t->capacity=n; t->active=0; t->retired=0; t->retention_ns=retention;
    if (n) memset(r, 0, n*sizeof(*r));
}
void schcvx_retire_expire(struct schcvx_retire *t, uint64_t now) {
    for (size_t i=0;i<t->capacity;i++) if (t->records[i].used && now>=t->records[i].expiry) { t->records[i].used=false; t->retired--; }
}
bool schcvx_retire_contains(const struct schcvx_retire *t, uint8_t rule, uint64_t dtag) {
    for (size_t i=0;i<t->capacity;i++) if (t->records[i].used && t->records[i].rule_id==rule && t->records[i].dtag==dtag) return true;
    return false;
}
bool schcvx_retire_reserve(struct schcvx_retire *t) { if (t->active>=t->capacity-t->retired) return false; t->active++; return true; }
void schcvx_retire_release(struct schcvx_retire *t) { if (t->active) t->active--; }
bool schcvx_retire_insert(struct schcvx_retire *t, uint8_t rule, uint64_t dtag, uint64_t now) {
    if (!t->active || schcvx_retire_contains(t,rule,dtag)) return false;
    for (size_t i=0;i<t->capacity;i++) if (!t->records[i].used) {
        t->records[i]=(struct schcvx_retire_record){dtag, UINT64_MAX-now<t->retention_ns ? UINT64_MAX : now+t->retention_ns, rule, true};
        t->active--; t->retired++; return true;
    }
    return false;
}
bool schcvx_retire_next_deadline(const struct schcvx_retire *t, uint64_t *deadline) {
    bool found=false; for (size_t i=0;i<t->capacity;i++) if (t->records[i].used && (!found || t->records[i].expiry<*deadline)) { *deadline=t->records[i].expiry; found=true; } return found;
}
