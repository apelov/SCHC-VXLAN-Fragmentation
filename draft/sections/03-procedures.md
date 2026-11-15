# Sender Procedure

The PMTU used here is the underlay IP path MTU, excluding outer Ethernet overhead. `H_IP` is the length of the actual outer IP header, including any options or extension headers, and any further encapsulation that consumes the same PMTU budget.

PMTU is a sender configuration value per peer and per direction. This document defines no default PMTU; the value 1500 appears only in examples. The receiver does not need the sender's PMTU and MUST NOT assume a slice length.

The ordinary VXLAN frame budget is:

~~~
PMTU - H_IP - 8 (UDP) - 8 (VXLAN)
~~~

The slice budget `C` is the same for every fragment of a frame:

~~~
C = PMTU - H_IP - 8 (UDP) - 14 (fragment header) - 4 (RCS allowance)
~~~

Nonfinal datagrams therefore leave four bytes unused, which removes a sizing special case for the final fragment.

For each Ethernet frame (as presented to the tunnel, without FCS) the sender proceeds as follows.

1. If the frame fits the ordinary VXLAN frame budget, it emits RFC 7348 VXLAN.
2. Otherwise, if the peer is not configured for this extension, it drops the frame and increments a local counter.
3. It validates `C > 0`, computes `N = ceil(frame_length / C)`, and drops the frame if the length is outside the configured frame-length range or `N` is outside the configured fragment-count range (2 through the configured maximum).
4. It takes a snapshot of the budget once per frame, so a PMTU change affects only later frames.
5. It sends the slices in ascending index order. Every slice has `C` bytes except the final one, which carries the remainder followed by the 4-byte RCS. Every outer IP datagram fits the PMTU.

A configuration MUST be rejected if the minimum configured PMTU cannot carry the maximum frame length in the maximum number of fragments.

All fragments of a frame use the same outer five-tuple, in particular the same source UDP port. The sender derives that port in an implementation-specific way, for example from the inner frame as it does for ordinary VXLAN ({{RFC7348}}, Section 5), to preserve flow-hash affinity. The receiver identifies the peer independently of the source UDP port, and the protocol tolerates reordering even when path affinity fails.

For IPv4 the sender sets DF. For IPv6 the sender does not use source fragmentation.

Worked numbers with PMTU 1500, no outer IP options or extension headers: standard VXLAN carries complete Ethernet frames of up to 1464 bytes over IPv4 (`H_IP` = 20) and 1444 bytes over IPv6 (`H_IP` = 40). The slice budgets are 1454 and 1434 bytes. Over IPv4, a 1514-byte frame produces slices of 1454 and 60 bytes; the outer IP lengths are 1496 and 106 bytes (the final datagram carries 60 + 14 + 4 + 8 + 20).

The first version uses a conservative static PMTU per peer and path. Open Issue: dynamic discovery is not specified. If it is specified later, it has to describe how the extension probes its own path, including ECMP and path changes; see RFC 8899 {{RFC8899}}.

# Receiver Procedure

The wire format carries no original-length field. The receiver relies on the fragment count, on equal-length nonfinal slices, on the configured maximum frame length and on the RCS. It does not assume a slice length.

An entry is keyed by the configured peer association (including the local tunnel endpoint), VNI, RuleID and Frame ID. Because the RuleID of an association is configured and a mismatch is dropped, it does not distinguish entries within an association. A single worker or equivalent synchronization owns each entry. The receiver uses a local monotonic clock for admission, expiry and delivery decisions.

1. **Validation before allocation.** Before allocating state the receiver MUST check that: the peer and VNI are authorized; the UDP checksum is nonzero and correct; the RuleID equals the value configured for the peer association (a mismatch is counted as a peer admission failure); the count is at least 2 and at most the configured maximum; the index is less than the count; the slice length does not exceed the receiver's configured maximum slice length; and a nonempty slice remains after the header and any required trailer, so that a final fragment carries at least one slice byte and the 4-byte RCS. The maximum slice length is a local bound, at most the configured maximum frame length. A datagram that fails the checksum or bounds checks is dropped and counted as malformed. Caps on incomplete entries and retained bytes apply per peer and globally. If admitting a fragment would exceed a cap, the fragment is dropped and counted as table exhaustion; an existing entry stays pending until its deadline.
2. **Admission and expiry.** A fragment for a retired ID, whether the frame completed, aborted or expired, is dropped and counted as late. The first admissible fragment, of any index, opens an entry with `expiry = first_rx + Δ`; the deadline is never extended. Expiry is decided by the arrival timestamp `t` alone, taken at a consistent receive stage. Expired entries are ineligible for completion even if no timer sweep has run: a fragment that finds an expired entry retires it as expired (counted once) and is itself counted as late. Timer processing can be batched.
3. **Consistency and duplicates.** The count MUST match across the entry, every nonfinal slice MUST have the same length, and the final slice MUST be no longer than a nonfinal slice. The receiver stores one slice per index and updates a bitmap and a running byte count. An identical duplicate, meaning a fragment with the same index and the same slice and, for the final fragment, the same RCS, is ignored and does not extend expiry. A fragment with the same index but a different slice or, for the final fragment, a different RCS is a conflicting duplicate. A conflicting duplicate, inconsistent metadata, or a byte count above the maximum frame length aborts and retires the entry.
4. **Completion.** When the bitmap is complete, the sum of the slice lengths MUST lie within the configured frame-length range. The receiver assembles the slices in index order, verifies the RCS and delivers the frame once. The frame is eligible if the arrival timestamp of the completing fragment is before expiry; assembly and verification time after that arrival is budgeted in `Δ`. A full bitmap with an invalid length or RCS aborts the entry.
5. **Retirement.** On completion, abort or timeout the receiver retires the ID and releases the buffers. A final fragment with holes stays pending until the holes fill or the deadline passes.

Open Issue: the deadline equality rule. The reference receiver treats `t >= expiry` as expired, so a fragment arriving exactly at the deadline is late. This is a recommendation pending working group input.

Because an entry may start from any index, a fragment of a retired frame can arrive after retirement and would otherwise open a new entry. A bounded retirement record suppresses such late fragments and duplicate delivery. A record MUST NOT be evicted while the deployment still allows fragments for that ID to arrive. For the first version, `L` is configured as an upper bound on the arrival spread of all copies of the fragments of a frame, including duplicates, and a record is kept for `L` after retirement. Capacity for active and retired IDs is either provisioned for the admitted rate, or a retirement slot is reserved at admission, as the reference receiver does; new IDs are then not admitted within an exhausted scope until space returns. Drops before admission create no deliverable state. Without a defensible bound on `L`, suppression through finite retention is best effort.

Starting from any index supports reordering because every fragment carries its own index and the count, so the first fragment to arrive, including the final one, is enough to open an entry and size its bitmap, and missing indices simply stay pending.

The receiver maintains counters for: completed frames, expiration, late fragments, duplicates, integrity failure (inconsistent metadata, conflicting duplicates, bad length or RCS), malformed input, peer admission failure, and table exhaustion. Expiration and lateness are reported separately: expiration records incompleteness observed at the cutoff, while a late fragment, meaning any fragment of a retired Frame ID, is positive evidence of lateness. A duplicate is an identical fragment received while its entry is still active; an identical fragment arriving after retirement is late, not a duplicate.
