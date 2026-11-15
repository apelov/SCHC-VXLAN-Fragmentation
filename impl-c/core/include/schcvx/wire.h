#ifndef SCHCVX_WIRE_H
#define SCHCVX_WIRE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum schcvx_variant { SCHCVX_COMPACT, SCHCVX_EXTENDED };
enum schcvx_kind { SCHCVX_UNFRAGMENTED, SCHCVX_REGULAR, SCHCVX_ALL1, SCHCVX_ABORT };
enum schcvx_wire_error {
    SCHCVX_E_MALFORMED = 1, SCHCVX_E_UNKNOWN_RULE, SCHCVX_E_INVALID_FCN,
    SCHCVX_E_SHORT_ALL1, SCHCVX_E_FIRST, SCHCVX_E_CONFIG
};
struct schcvx_assoc_wire {
    enum schcvx_variant variant;
    const uint8_t *fr_rule_ids;
    size_t fr_rule_count;
    uint8_t cd_rule_id;
};
struct schcvx_msg {
    enum schcvx_kind kind;
    uint8_t rule_id;
    uint64_t dtag;
    uint8_t fcn;
    bool first;
    uint8_t countdown;
    uint32_t rcs;
    const uint8_t *payload;
    size_t payload_len;
};
/* Association and rule array remain caller-owned. Zero means valid. */
int schcvx_wire_config_check(const struct schcvx_assoc_wire *a);
/* Invalid variants return zero. */
size_t schcvx_header_len(enum schcvx_variant variant);
size_t schcvx_all1_header_len(enum schcvx_variant variant);
bool schcvx_fcn_is_first(uint8_t fcn);
/* Returns -SCHCVX_E_INVALID_FCN unless fcn is regular. */
int schcvx_fcn_countdown(uint8_t fcn);
/* Zero-based position, K in 2..8; returns FCN or negative error. */
int schcvx_fcn_value(unsigned k, unsigned position);
/* Returns zero on success, negative error otherwise; out unchanged on error.
 * Unfragmented payload includes the C/D RuleID. Other payloads are tiles.
 * NULL data or len == 0 is rejected as malformed. */
int schcvx_classify(const struct schcvx_assoc_wire *a, const uint8_t *data,
                    size_t len, struct schcvx_msg *out);
/* Writes only a fragment header, never a tile. Caller supplies space for the
 * variant's All-1 header. Returns bytes written, or zero without writing on
 * invalid fields/configuration/kind. kind and fcn must agree; first/countdown
 * are derived on decoding and ignored here. UNFRAGMENTED is not supported. */
size_t schcvx_encode_header(const struct schcvx_assoc_wire *a,
                            const struct schcvx_msg *m, uint8_t *out);
#endif
