#ifndef SCHCVX_HIDX_H
#define SCHCVX_HIDX_H
/* Internal helpers: open-addressing hash index over caller-supplied memory
 * (linear probing, backward-shift deletion), keyed by (rule_id, dtag). A slot
 * holds the 1-based position of the indexed item (0 = empty) and its hash. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct schcvx_hslot { uint32_t item1, hash; };
#define SCHCVX_NIL UINT32_MAX
#define SCHCVX_HIDX_NONE SIZE_MAX
typedef bool (*schcvx_hidx_eq)(const void *ctx, uint32_t item, uint8_t rule, uint64_t dtag);
static inline uint32_t schcvx_hidx_hash(uint8_t rule, uint64_t dtag) {
    uint64_t x=(dtag^((uint64_t)rule<<56))*UINT64_C(0x9E3779B97F4A7C15);
    x^=x>>32; x*=UINT64_C(0xD6E8FEB86659FD93); x^=x>>29;
    return (uint32_t)x;
}
/* Power-of-two slot count of at least 2 x items; 0 if it cannot be represented. */
static inline size_t schcvx_hidx_slots(size_t items) {
    if (!items || items>=UINT32_MAX/4) return 0;
    size_t n=4; while (n<items*2) n<<=1;
    return n;
}
static inline size_t schcvx_hidx_find(const struct schcvx_hslot *t, size_t slots, const void *ctx, schcvx_hidx_eq eq, uint8_t rule, uint64_t dtag) {
    uint32_t h=schcvx_hidx_hash(rule,dtag); size_t mask=slots-1, i=h&mask;
    while (t[i].item1) { if (t[i].hash==h && eq(ctx,t[i].item1-1,rule,dtag)) return i; i=(i+1)&mask; }
    return SCHCVX_HIDX_NONE;
}
static inline void schcvx_hidx_insert(struct schcvx_hslot *t, size_t slots, uint8_t rule, uint64_t dtag, uint32_t item) {
    uint32_t h=schcvx_hidx_hash(rule,dtag); size_t mask=slots-1, i=h&mask;
    while (t[i].item1) i=(i+1)&mask;
    t[i].item1=item+1; t[i].hash=h;
}
static inline void schcvx_hidx_erase_at(struct schcvx_hslot *t, size_t slots, size_t pos) {
    size_t mask=slots-1, i=pos, j=pos;
    for (;;) {
        j=(j+1)&mask; if (!t[j].item1) break;
        size_t k=t[j].hash&mask;
        if (i<=j ? (i<k && k<=j) : (i<k || k<=j)) continue;
        t[i]=t[j]; i=j;
    }
    t[i].item1=0; t[i].hash=0;
}
/* Checked size accumulation: returns false on overflow. */
static inline bool schcvx_size_add(size_t *acc, size_t count, size_t each) {
    if (each && count>(SIZE_MAX-*acc)/each) return false;
    *acc+=count*each; return true;
}
#endif
