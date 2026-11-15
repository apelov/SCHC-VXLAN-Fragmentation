#ifndef SCHCVX_RX_TOLERANT_H
#define SCHCVX_RX_TOLERANT_H
#include "schcvx/rx_ordered.h"
/* Shared by all association receivers in the same global resource pool. */
struct schcvx_rx_tolerant_pool { size_t max_entries, max_bytes, max_orphans; size_t entries, bytes, orphans; };
struct schcvx_rx_tolerant_cfg {
    struct schcvx_assoc_wire wire;
    const uint32_t *vnis; size_t vni_count;
    size_t max_entries, retirement_capacity, max_bytes, max_frame_len, max_tile_len, orphan_budget;
    uint64_t deadline_ns, inactivity_ns, retention_ns;
    struct schcvx_rx_tolerant_pool *pool;
};
struct schcvx_rx_tolerant_piece { size_t offset, len; uint8_t fcn; bool present; };
struct schcvx_rx_tolerant_entry {
    uint64_t dtag, deadline, inactivity; size_t len, tile_len;
    uint32_t rcs; uint8_t rule_id, n; bool used, first, orphan;
    struct schcvx_rx_tolerant_piece pieces[8];
};
struct schcvx_rx_tolerant {
    struct schcvx_rx_tolerant_cfg cfg; struct schcvx_retire retire;
    struct schcvx_rx_tolerant_entry *entries; uint8_t *packets, *assembly;
    size_t retained, orphans; struct schcvx_counters *counters;
};
/* Buffer aligned for max_align_t; zero means invalid configuration or overflow. */
size_t schcvx_rx_tolerant_bytes(const struct schcvx_rx_tolerant_cfg *cfg);
int schcvx_rx_tolerant_init(struct schcvx_rx_tolerant *rx, const struct schcvx_rx_tolerant_cfg *cfg, void *buffer, size_t bytes, struct schcvx_counters *counters);
/* Caller validates association and checksum and supplies monotonic timestamps.
 * Delivery remains valid until the next receiver operation. */
int schcvx_rx_tolerant_receive(struct schcvx_rx_tolerant *rx, const uint8_t *data, size_t len, uint64_t now, struct schcvx_delivery *out);
void schcvx_rx_tolerant_sweep(struct schcvx_rx_tolerant *rx, uint64_t now);
bool schcvx_rx_tolerant_next_deadline(const struct schcvx_rx_tolerant *rx, uint64_t *deadline);
#endif
