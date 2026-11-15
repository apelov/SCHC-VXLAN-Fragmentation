#ifndef SCHCVX_CONFIG_H
#define SCHCVX_CONFIG_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "schcvx/tx.h"
#define SCHCVX_MAX_ASSOC 32
#define SCHCVX_MAX_VNIS 32
struct schcvx_config_assoc {
 char name[64], local_ip[16], peer_ip[16], tap[64], counters_file[256];
 uint16_t udp_port, src_port; uint8_t fr_rule_id, cd_rule_id;
 uint32_t vnis[SCHCVX_MAX_VNIS]; size_t vni_count;
 struct schcvx_tx_cfg tx; bool tolerant;
 uint64_t S, B, I, Delta, R, Q, dtag_start, counters_interval;
 size_t max_entries, max_bytes, orphan_budget;
 uint32_t dtag_rate, dtag_burst;
};
struct schcvx_config { size_t max_entries, max_bytes, orphan_budget, count; struct schcvx_config_assoc assoc[SCHCVX_MAX_ASSOC]; };
int schcvx_config_load(const char *path, struct schcvx_config *out, char *error, size_t error_size);
#endif
