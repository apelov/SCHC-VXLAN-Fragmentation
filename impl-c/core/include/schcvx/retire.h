#ifndef SCHCVX_RETIRE_H
#define SCHCVX_RETIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "schcvx/hidx.h"
struct schcvx_retire_record { uint64_t dtag, expiry; uint8_t rule_id; bool used; };
struct schcvx_retire {
    struct schcvx_retire_record *records; size_t capacity, active, retired; uint64_t retention_ns;
    size_t head; struct schcvx_hslot *index; size_t index_slots;
};
/* Memory is caller-supplied: capacity records (FIFO ring, oldest expiry first) followed by
 * schcvx_retire_index_slots(capacity) index slots. schcvx_retire_bytes() gives the total
 * (0 when capacity is zero or too large). Records must be inserted with non-decreasing
 * `now` (an expiry earlier than the newest record's is raised to it). */
size_t schcvx_retire_index_slots(size_t capacity);
size_t schcvx_retire_bytes(size_t capacity);
void schcvx_retire_init(struct schcvx_retire *t, struct schcvx_retire_record *records, size_t capacity, struct schcvx_hslot *index, size_t index_slots, uint64_t retention_ns);
void schcvx_retire_expire(struct schcvx_retire *t, uint64_t now);
bool schcvx_retire_contains(const struct schcvx_retire *t, uint8_t rule, uint64_t dtag);
bool schcvx_retire_reserve(struct schcvx_retire *t);
void schcvx_retire_release(struct schcvx_retire *t);
bool schcvx_retire_insert(struct schcvx_retire *t, uint8_t rule, uint64_t dtag, uint64_t now);
bool schcvx_retire_next_deadline(const struct schcvx_retire *t, uint64_t *deadline);
#endif
