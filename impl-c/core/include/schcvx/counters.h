#ifndef SCHCVX_COUNTERS_H
#define SCHCVX_COUNTERS_H
#include <stddef.h>
#include <stdint.h>
#define SCHCVX_COUNTER_FIELDS(X) \
 X(packets_sent_unfragmented) X(packets_sent_fragmented) X(sender_drop_length) X(sender_drop_k_range) X(sender_drop_s_exceeded) X(sender_drop_dtag_admission_stopped) X(sender_drop_quarantine) X(packets_delivered) X(expired) X(late) X(duplicates) X(integrity_failures) X(malformed) X(unknown_rule) X(peer_admission_failures) X(vni_admission_failures) X(table_exhaustion) X(sender_aborts_received)
struct schcvx_counters {
#define SCHCVX_COUNTER_MEMBER(n) uint64_t n;
 SCHCVX_COUNTER_FIELDS(SCHCVX_COUNTER_MEMBER)
#undef SCHCVX_COUNTER_MEMBER
};
/* Returns required bytes excluding NUL; output is NUL-terminated when capacity permits. */
size_t schcvx_counters_format(const struct schcvx_counters *c, char *out, size_t capacity);
#endif
