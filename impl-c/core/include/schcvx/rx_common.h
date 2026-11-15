#ifndef SCHCVX_RX_COMMON_H
#define SCHCVX_RX_COMMON_H
#include <stddef.h>
#include <stdint.h>
struct schcvx_delivery { const uint8_t *frame; size_t frame_len; uint32_t vni; };
enum schcvx_rx_validation { SCHCVX_RX_VALID=0, SCHCVX_RX_MALFORMED=-1, SCHCVX_RX_UNAUTHORISED=-2 };
int schcvx_rx_validate(uint8_t cd_rule_id, const uint32_t *vnis, size_t vni_count, const uint8_t *packet, size_t len, struct schcvx_delivery *out);
#endif
