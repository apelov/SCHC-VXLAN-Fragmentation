# FROZEN DESIGN — SCHC VXLAN Fragmentation Internet-Draft

- Deliverable: individual Internet-Draft `draft-vladyka-schc-vxlan-fragmentation-00`, for presentation to the IETF SCHC WG.
- Authors: Andrii Vladyka (andrii.vladyka@gmail.com), Alexander Pelov (alexander.pelov@imt-atlantique.fr).
- Companion deliverable: a small Python reference model (`src/schcvx/`) whose tests pin every number and example used in the draft. No throughput claims anywhere.
- Authority rule: everything below the line is the authoritative technical input. Wire values are provisional prototype choices. The "Items to settle" section lists OPEN decisions: the draft must mark them explicitly as open and must not resolve them. Unspecified behavior is not permission to invent.

---


## Revision notes (human decisions, 2026-11-14)

- Keep CRC-32C RCS: it is the SCHC integrity check (RFC 8724 §8.2.3, §8.4.1) and guards against splicing slices of different frames, for example after a restart.
- Drop the original-frame-length field. The receiver relies on the fragment count, equal-length nonfinal slices, the configured maximum frame length and the RCS. The header is 14 bytes.
- RuleID stays one byte (value 0) to preserve alignment; a zero-bit RuleID is not used. RFC 8724 §6 leaves RuleID size to the Profile.
- PMTU is a per-peer, per-direction configuration, not tied to 1500.
- Frame ID stays 8 bytes; its width is an open item. The draft is to be short.

---
# Deadline-bounded Ethernet fragmentation for VXLAN deployments

## Drafting brief

Use this Markdown as the technical input for an individual Internet-Draft. Preserve the design below, verify RFC references, and mark open protocol decisions explicitly. The first version targets statically provisioned unicast VTEP pairs in a managed fabric. Wire values below are provisional prototype choices. Use BCP 14 terminology in the draft where interoperability requires it.

## Problem and requirements

Bridge Ethernet frames whose encapsulated size exceeds the underlay IP path MTU (PMTU). Preserve the full input Ethernet frame as presented to the tunnel, including any tags carried by the tunnel, and excluding the Ethernet FCS. Keep standard VXLAN for frames that fit. Deliver a fragmented frame only after successful reassembly, with bounded waiting time and bounded receiver state. Support fragment reordering. Use no acknowledgments or retransmissions in the data path.

Line rate is a performance target subject to the specified traffic mix, link rate, and packet expansion. The base design delivers frames as they complete and provides no additional inter-frame ordering guarantee across the ordinary and fragmented paths.

## Placement and interoperability

Transmit placement is after bridge forwarding and before tunnel encapsulation. Receive placement is after tunnel classification and before bridge ingress. Both paths feed the same VNI-to-bridge mapping. Learn source MAC addresses only from complete frames.

* If the frame fits, emit RFC 7348 VXLAN.
* If it exceeds the VXLAN payload budget and both peers are configured for the extension, encapsulate each slice in a separate UDP datagram on a distinct, configured destination port.
* If the extension is unavailable or the configured limits cannot carry the frame, drop the oversized frame and increment a local counter.
* Begin with unicast transmission between VTEPs. BUM traffic is in scope only through ingress replication to individually provisioned unicast peers. Each copy uses that peer's capabilities and PMTU.

The extension carries the semantics of the Ethernet overlay VNI but uses its own UDP payload format. It does not fragment an already encapsulated VXLAN packet at IP. Use the same source UDP port and outer five-tuple for every fragment of a frame to preserve ordinary flow-hash affinity. The receiver identifies a configured peer independently of the source UDP port. The protocol tolerates reorder even when path affinity fails.

## Relationship to SCHC

RFC 8724 Section 8.4.1 No-ACK assumes in-order fragment delivery and concludes reassembly on All-1. The indexed receiver below can accept the final fragment first and wait for missing indices. Treat this as a **candidate new SCHC fragmentation mode** with a UDP/Ethernet binding, pending working-group agreement on how it integrates with the SCHC framework. The trailer below plays the role of a reassembly check sequence (RCS). The prototype wire format is provisional and has no RFC 8724 wire compatibility. A deployment with a genuine in-order guarantee could instead specify a separate RFC 8724 No-ACK profile. Header compression is outside this draft's scope.

## Prototype wire format

The configured UDP destination port selects this format. All multibyte integers use network byte order.

| Byte offset | Size | Field |
| --- | --- | --- |
| 0 | 3 | VNI, with the same meaning as in the associated VXLAN deployment |
| 3 | 1 | RuleID, value 0 for the sole prototype rule |
| 4 | 8 | Frame ID, unique across active and recently retired frames within the configured peer association and VNI |
| 12 | 1 | Fragment index, zero-based |
| 13 | 1 | Fragment count, 2 through the configured maximum |
| 14 | variable | Nonempty slice of the original frame |
| End of final fragment | 4 | RCS, present only if index equals count minus one |

Use CRC-32C over the original frame bytes in wire order. For interoperability, specify the Castagnoli polynomial, reflected input/output, initial register and final XOR of 0xffffffff, the check value 0xe3069283 for ASCII `123456789`, and big-endian trailer serialization. Generate and verify the UDP checksum on all extension datagrams. The CRC checks reassembly integrity.

## MTU and sender algorithm

Use the underlay **IP** PMTU, excluding outer Ethernet overhead. Let `H_IP` include the actual outer IP header and any options or extension headers. Account for additional encapsulation if it consumes the same PMTU budget.

* Ordinary VXLAN frame budget is `PMTU - H_IP - 8 (UDP) - 8 (VXLAN)`.
* For the prototype use the same slice budget for every fragment, `C = PMTU - H_IP - 8 (UDP) - 14 (fragment header) - 4 (RCS allowance)`. This leaves four bytes unused in nonfinal datagrams and removes a sizing special case.
* Compute `N = ceil(frame_length / C)` after validating `C > 0`. Reject frames outside the configured frame-length range or with `N` outside the configured range.
* PMTU is a sender-side configuration value per peer and direction, with no default in this document; 1500 appears only in examples. The receiver does not need the sender's PMTU, so it must not assume a slice length. Reject a configuration in which the minimum PMTU cannot carry the maximum frame in the maximum number of fragments.
* Snapshot the budget once per frame. Send contiguous slices in ascending index order, all `C` bytes except the final slice, which carries the remainder and RCS. Every outer IP datagram fits the budget. Use DF for IPv4 and omit IPv6 source fragmentation.
* Start with a conservative static PMTU per peer/path. If dynamic discovery is specified, describe how the extension probes its own path, including ECMP and path changes. See RFC 8899.

With a 1500-byte IP PMTU and no outer IP options or extensions, standard VXLAN carries a complete Ethernet frame of up to 1464 bytes over IPv4 or 1444 over IPv6. The corresponding untagged inner Ethernet payload limits are 1450 and 1430 bytes. The prototype slice budgets are 1454 and 1434 bytes, respectively. Over IPv4, a 1514-byte frame produces slices of 1454 and 60 bytes and outer IP lengths of 1496 and 106 bytes.

## Receiver algorithm

Key entries by configured peer association (including the local tunnel endpoint), VNI, RuleID, and Frame ID. A single worker or equivalent synchronization owns each entry. Use a local monotonic clock for admission, expiry, and delivery decisions.

1. Before allocating, validate peer and VNI authorization, UDP checksum, RuleID, count and index bounds, that the slice length does not exceed the configured maximum slice length, and that a nonempty slice remains after parsing the header and any required trailer. Cap incomplete entries and retained bytes per peer and globally.
2. Drop fragments for retired IDs. The first admissible fragment, of any index, opens an entry with `expiry = first_rx + Δ`. Use arrival timestamps from a consistent receive stage and enforce expiry before publishing a completed frame. Timer processing can be batched. Expired entries are ineligible for completion even if a timer sweep has yet to run.
3. Require count to match across the entry, every nonfinal slice to have the same length, and the final slice to be no longer than a nonfinal slice. Store one slice per index and update a bitmap and running byte count. Ignore identical duplicate fragments without extending expiry. A conflicting duplicate, inconsistent metadata, or a byte count exceeding the configured maximum frame length aborts and retires the entry.
4. When the bitmap is complete, require the sum of slice lengths to lie within the configured frame-length range. Assemble slices in index order, verify the RCS, and deliver once if verification finishes before expiry. A full bitmask with invalid length or RCS aborts the entry.
5. On completion, abort, or timeout, retire the ID and release the buffers. A final fragment with holes stays pending until the holes fill or the deadline passes. Keep a bounded retirement record to suppress late fragments and duplicate delivery.

Starting an entry from any index supports reordering. Consequently, never evict a retirement record while the deployment still allows fragments for that ID to arrive. For the first prototype, configure `L` as an upper bound on the arrival spread for all copies of fragments of a frame, including duplicates. Keep retirement records for `L` after retirement. Provision capacity for active and retired IDs at the admitted rate, or reserve a retirement slot at admission and stop admitting new IDs within an exhausted scope until space returns. Drops before admission create no deliverable state. Without a defensible bound, suppression via finite retention is best effort.

Use a monotonic 64-bit counter per sending association and VNI. Preserve uniqueness across sender restarts by persisting counter allocation or establishing a new association generation that the receiver can distinguish and enforcing rejection of the old generation. A receiver restart needs a fresh association or a drain interval covering the deployment's residual packet lifetime before reopening the old association if duplicate suppression must survive restart. The prototype does not put an epoch on the wire. Specify the restart and generation mechanism before claiming restart-safe interoperability.

## Choosing the discard deadline

A receiver can observe incompleteness at the cutoff. It cannot tell whether a missing fragment was lost or will arrive later. A fragment received after expiry is positive evidence of lateness. Report the two events separately.

For a deterministic path, budget `Δ` from the maximum sender emission span, maximum differential path delay, and maximum RX scheduling, processing, and clock-resolution margin. An absolute maximum one-way latency can inform the differential bound, but it is the spread between pieces of the same frame that drives this timer. For a best-effort path, measure the spread under realistic contention and choose the cutoff against the service's latency and discard budgets. Define the observation stage when benchmarking.

The first-arrival deadline bounds receiver-held reassembly time. An end-to-end frame age bound requires a separate age mechanism, such as sender timestamps and bounded clock error. Leave this outside the base format.

## Fast path and acceptance checks

Start with a configured maximum of two fragments, valid where the minimum configured PMTU carries the maximum allowed frame in two slices (for example, an Ethernet-1500 service over a 1500-byte PMTU). Validate that the minimum underlay PMTU can carry the maximum allowed frame in two pieces. Use a two-bit receive mask, bounded per-worker state, batched timer processing, and scatter/gather where the forwarding interface supports it. Keep ordinary VXLAN on its existing offload path where supported. Benchmark the integrity check and the fragmented forwarding path together.

* Check exact-fit and one-byte-over frame sizes for IPv4 and IPv6, including tagged Ethernet, and verify every emitted IP packet fits the PMTU.
* Check both arrival orders, every single-fragment loss, identical and conflicting duplicates, malformed or inconsistent headers, bad RCS, deadline equality, late arrival after retirement, and ID uniqueness across restart.
* Check normal VXLAN forwarding, unsupported peers, ingress replication, table exhaustion, and the change from one configured PMTU to another.
* Benchmark a normal/fragmented mix, all frames just over the VXLAN threshold, and the maximum frame size. Report original frames per second, outer packets per second, wire bit rate, delivered payload rate, latency percentiles, CPU or hardware resources, and drops by cause. State whether offered load refers to ingress frame rate or egress wire capacity. Fragmentation adds headers and packet work, so an ingress stream that fills an equal-rate link can exceed the available egress capacity after encapsulation.
* Count completed frames, expiration, late fragments, duplicates, integrity failure, malformed input, peer admission failure, and table exhaustion.

## Items to settle in the Internet-Draft

1. Confirm the SCHC mode extension and identify how RuleID, frame identification, fragment indexing, and RCS map to SCHC terminology and architecture. Keep the tunnel binding explicit even if the generic mode and binding appear in the same document.
2. Specify final header versioning, UDP demultiplexing and any eventual IANA request. Use a locally configured port in the prototype. Identify the need for peer capability provisioning before sending oversized frames.
3. Specify the default operational profile, the Frame ID width (8 bytes is a prototype choice kept for alignment), the bounds on configured frame sizes and fragment counts, the meanings and configuration of Δ and L, and the restart procedure.
4. Include Security Considerations for peer and VNI admission, spoofing, state exhaustion, replay, and the use of an authenticated underlay if required by the deployment. CRC and UDP checksums detect corruption. They provide no peer authentication.
5. Include IANA Considerations, manageability, and examples showing a last-fragment-first completion and a deadline expiry. Leave throughput claims pending measurement.

## References

- [RFC 7348, Virtual eXtensible Local Area Network (VXLAN)](https://www.rfc-editor.org/rfc/rfc7348.html), especially Sections 4.3 and 5.
- [RFC 8724, SCHC Generic Framework](https://www.rfc-editor.org/rfc/rfc8724.html), especially Sections 8.2.3 and 8.4.1.
- [RFC 8899, Datagram Packetization Layer Path MTU Discovery](https://www.rfc-editor.org/rfc/rfc8899.html).
