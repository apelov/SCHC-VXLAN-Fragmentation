#ifndef SCHCVX_RX_ORDERED_H
#define SCHCVX_RX_ORDERED_H
#include "schcvx/wire.h"
#include "schcvx/retire.h"
#include "schcvx/rx_common.h"
#include "schcvx/counters.h"
struct schcvx_rx_ordered_cfg {
    struct schcvx_assoc_wire wire;
    const uint32_t *vnis; size_t vni_count;
    size_t max_entries, retirement_capacity, max_bytes, max_frame_len;
    uint64_t inactivity_ns, retention_ns;
};
struct schcvx_rx_ordered_entry { uint64_t dtag, deadline; size_t len; uint32_t prev, next; uint8_t rule_id; bool used; };
struct schcvx_rx_ordered {
    struct schcvx_rx_ordered_cfg cfg; struct schcvx_retire retire;
    struct schcvx_rx_ordered_entry *entries; uint8_t *packets;
    size_t retained; struct schcvx_counters *counters;
    /* Lookup index, free-slot stack and the active list (sorted by deadline, oldest first). */
    struct schcvx_hslot *hash; size_t hash_slots; uint32_t *free_stack; size_t free_top; uint32_t head, tail;
};
/* Buffer must be aligned for max_align_t. Zero indicates invalid/overflowing config. */
size_t schcvx_rx_ordered_bytes(const struct schcvx_rx_ordered_cfg *cfg);
int schcvx_rx_ordered_init(struct schcvx_rx_ordered *rx, const struct schcvx_rx_ordered_cfg *cfg, void *buffer, size_t bytes, struct schcvx_counters *counters);
/* Caller validates association and UDP checksum. Delivery view remains valid until
 * the next receiver operation; unfragmented views borrow the datagram. Returns 1
 * on delivery, 0 on drop or incomplete input. Timestamps must be monotonic. */
int schcvx_rx_ordered_receive(struct schcvx_rx_ordered *rx, const uint8_t *data, size_t len, uint64_t now, struct schcvx_delivery *out);
void schcvx_rx_ordered_sweep(struct schcvx_rx_ordered *rx, uint64_t now);
bool schcvx_rx_ordered_next_deadline(const struct schcvx_rx_ordered *rx, uint64_t *deadline);
#endif
