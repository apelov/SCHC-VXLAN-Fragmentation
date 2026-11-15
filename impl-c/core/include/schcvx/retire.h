#ifndef SCHCVX_RETIRE_H
#define SCHCVX_RETIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct schcvx_retire_record { uint64_t dtag, expiry; uint8_t rule_id; bool used; };
struct schcvx_retire { struct schcvx_retire_record *records; size_t capacity, active, retired; uint64_t retention_ns; };
void schcvx_retire_init(struct schcvx_retire *t, struct schcvx_retire_record *records, size_t capacity, uint64_t retention_ns);
void schcvx_retire_expire(struct schcvx_retire *t, uint64_t now);
bool schcvx_retire_contains(const struct schcvx_retire *t, uint8_t rule, uint64_t dtag);
bool schcvx_retire_reserve(struct schcvx_retire *t);
void schcvx_retire_release(struct schcvx_retire *t);
bool schcvx_retire_insert(struct schcvx_retire *t, uint8_t rule, uint64_t dtag, uint64_t now);
bool schcvx_retire_next_deadline(const struct schcvx_retire *t, uint64_t *deadline);
#endif
