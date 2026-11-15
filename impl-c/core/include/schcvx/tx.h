#ifndef SCHCVX_TX_H
#define SCHCVX_TX_H
#include "schcvx/wire.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct schcvx_tx_cfg { uint32_t pmtu, ip_header_len; enum schcvx_variant variant; uint32_t min_frame_len, max_frame_len; uint8_t max_fragments; };
struct schcvx_tx_plan { bool unfragmented; uint8_t k; uint32_t c; uint32_t last_len; };
enum schcvx_tx_error { SCHCVX_E_TX_LENGTH = 100, SCHCVX_E_TX_FRAGMENTS, SCHCVX_E_TX_BUFFER };
int schcvx_tx_config_check(const struct schcvx_tx_cfg *cfg);
int schcvx_tx_plan(const struct schcvx_tx_cfg *cfg, size_t frame_len, struct schcvx_tx_plan *out);
int schcvx_vxlan_header(uint32_t vni, uint8_t out[8]);
int schcvx_packet_prefix(uint8_t cd_rule_id, uint32_t vni, uint8_t out[9]);
/* Output must not overlap prefix or frame. */
int schcvx_tx_fragment(const struct schcvx_tx_plan *plan, const struct schcvx_assoc_wire *assoc, uint8_t rule_id, uint64_t dtag, const uint8_t prefix[9], const uint8_t *frame, size_t frame_len, unsigned position, uint8_t *out, size_t capacity, size_t *written);
bool schcvx_tx_emission_stopped(uint64_t first_ns, uint64_t now_ns, uint64_t s_ns);
#endif
