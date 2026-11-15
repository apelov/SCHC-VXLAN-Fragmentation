# Revised option C: VXLAN payload fragmentation

Status: review proposal, 2026-11-14. This document incorporates the interactive discussion and corrects identified design defects. It does not replace the frozen DESIGN.md, change the implemented wire format, or reopen sealed workpackages. New engineering choices below are proposals pending human review. No claim of exhaustive or independent validation is made.

## Recommendation

Carry the complete VXLAN UDP payload through SCHC fragmentation, with NoCompress mandatory and default. Keep ordinary VXLAN for frames that fit. Use an explicit first-fragment marker and countdown, no acknowledgments, and two receiver behaviors: ordered RFC 8724 No-ACK processing and an explicitly specified tolerant extension. Configure behavior per association; do not infer support from successful delivery.

## Findings and repairs

| Problem in earlier discussion or design | Revised treatment |
| --- | --- |
| Two-fragment limit presented as inherent | Remove it; enforce a profile capacity bound instead. |
| `yyyxxx` described as four bits | It is six bits; replace it with four-bit first/countdown encoding. |
| First flag plus remaining-count loses eight-fragment capacity to All-1 | Encode remaining regular fragments, excluding All-1. |
| Arbitrary FCN meanings treated as permission to change receiver behavior | Encoding flexibility and receiver extension are separate standards questions. |
| CRC trials used to infer completion | Require first marker, all countdown positions, and All-1; check integrity once. |
| VNI claimed unavailable until complete reassembly | With NoCompress, inspect it when the first fragment arrives; earlier orphan state remains unattributed. |
| Restart shorter than timeout considered safe | Drain delayed traffic and receiver state before identifier reuse. |
| Deadline alone treated as sufficient quarantine | Include maximum residual transport lifetime and retirement retention. |
| Four-bit FCN inserted into a byte-aligned layout implicitly | Pack it with a 60-bit DTag; do not insert unspecified padding. |
| Compression treated as a free wire-size saving | Account for compression RuleID and provisioned context; leave compression disabled. |

## Scope and preserved requirements

Managed, statically provisioned unicast VTEP associations are the base deployment. Ingress replication applies the policy separately to each destination. Preserve the Ethernet frame including carried VLAN tags, excluding FCS. Use the same source port and flow affinity for all fragments of one packet. No inter-frame ordering guarantee is added. Unsupported oversized frames are dropped and counted. CRC-32C and nonzero validated UDP checksums are retained from the frozen design; neither authenticates a peer.

The fragmented unit in the base binding is exactly eight VXLAN header bytes followed by the Ethernet frame. The binding bypasses compression; “NoCompress” here does not silently introduce a nested compression RuleID. If the WG instead requires an explicit uncompressed SCHC C/D envelope, its RuleID must be included in the wire definition, integrity coverage, and size calculations before implementation.

## Proposed four-bit FCN

Use `Fccc` on regular fragments. `F=1` marks the original first fragment. `c` is the number of **regular fragments remaining after this fragment**, excluding the final All-1. Reserve `1111` for All-1. Support two through eight fragments per packet, without counter wrap.

For total fragment count K:

- First fragment: `F=1`, `c=K-2`, with c in 0..6.
- Subsequent regular fragments: `F=0`, c descends from K-3 to 0.
- Final fragment: `1111`, carrying the final payload and RCS.
- `0110` and `0111` are invalid in this profile. All-1 is parsed before interpreting the flag.

| K | FCNs in transmission order |
| --- | --- |
| 2 | `1000`, `1111` |
| 3 | `1001`, `0000`, `1111` |
| 4 | `1010`, `0001`, `0000`, `1111` |
| 8 | `1110`, `0101`, `0100`, `0011`, `0010`, `0001`, `0000`, `1111` |

Once the first marker supplies n=K-2, the required regular positions are n down to 0; only n has F=1. All-1 occupies a separate final slot. Before the first marker arrives, positions remain provisional. Reject any stored position incompatible with its eventual n. Never infer completeness from All-1 alone, or from a successful CRC on a partial candidate in the tolerant receiver.

A first marker lost in transit prevents completion, as it should. A missing middle or final fragment also prevents completion. Variable tile lengths can be ordered using the countdown without byte offsets; this proposal uses equal regular lengths for simple validation.

## Proposed wire layout

An explicit local UDP port selects this binding. No IANA assignment is assumed.

| Field | Size | Notes |
| --- | --- | --- |
| Fragmentation RuleID | 8 bits | Configured; identifies the full profile |
| DTag | 60 bits | Monotonic allocation per sending association and RuleID |
| FCN | 4 bits | Adjacent to DTag; low nibble of their eight-byte network-order word |
| RCS | 32 bits, All-1 only | Before final payload |
| Payload | Whole bytes | Nonempty |

The regular header is 9 bytes; the All-1 header is 13 bytes. No W field or padding is needed with eight-bit words and byte-sized payloads. Do not send Sender-Abort in this base proposal. A receiver must distinguish its RFC format from a valid All-1 and must never interpret an empty All-1 as a complete data fragment.

CRC-32C covers the complete fragmented unit in byte order. Preserve reflected Castagnoli parameters, initial register and final XOR 0xffffffff, check value 0xe3069283 for `123456789`, and network-order RCS serialization. CRC-32C is an intentional inherited choice; it is not the Ethernet CRC recommended by RFC 8724. Changing algorithms requires a distinct configured profile.

## Sender and capacity

Let P be the configured IP PMTU, H the complete outer IP header cost, and C=P-H-8-13. Account separately for any additional encapsulation consuming this budget. Require C>=8 so the complete uncompressed VXLAN header fits in the first fragment.

For Ethernet length E, ordinary VXLAN fits when E+8<=P-H-8. Otherwise fragment U=E+8 bytes into K=ceil(U/C) tiles, with all regular tiles C bytes and the last 1..C bytes. Require 2<=K<=8. Snapshot PMTU and profile for the packet. Never use IP fragmentation. Reject configurations whose maximum frame cannot fit within eight tiles at the minimum configured PMTU.

| IP PMTU | IP header | C | K for E=9014 | Maximum E in eight tiles |
| --- | --- | --- | --- | --- |
| 1500 | IPv4, 20 | 1459 | 7 | 11664 |
| 1500 | IPv6, 40 | 1439 | 7 | 11504 |
| 1280 | IPv6, 40 | 1219 | 8 | 9744 |

These are calculated examples, with no extra encapsulation. E=9014 means a 9000-byte inner payload plus an untagged Ethernet header. Tags consume additional space. For E=1514 over IPv4/1500, U=1522, tiles are 1459 and 63, and outer IP lengths are 1496 and 104. Fragmentation headers total 22 bytes, in addition to the retained eight-byte VXLAN header.

## Receiver behavior

### Ordered receiver

Follow RFC 8724 No-ACK arrival-order assembly and All-1 processing. This is an implementation of the configured profile, not compatibility with an arbitrary SCHC device. Do not claim reordered delivery is supported. Additional gap rejection, duplicate filtering, fixed deadlines, and retirement policies must not be silently attributed to the RFC state machine.

### Tolerant receiver extension

1. Validate configured peer, port, RuleID, UDP checksum, message length, FCN, and local size bounds before allocating. Key state by association, direction/local endpoint, RuleID and DTag, not VNI or source UDP port.
2. Any admissible fragment can open an entry. Allocate bounded orphan state if the first marker is absent. Reserve retirement capacity at admission.
3. Set a fixed deadline d=t0+Delta from the first admitted receive timestamp. Later arrivals and duplicates never extend d. A fragment with timestamp t>=d is late. Use one monotonic timestamp domain and serialized state ownership.
4. Store one payload per countdown and a separate All-1 payload/RCS. Identical duplicates are ignored; conflicting duplicates or incompatible first markers abort. Validate regular tile lengths are equal and final length does not exceed them once comparison is possible. Bound accumulated bytes before copying.
5. On the first marker, parse the full VXLAN header, validate the VNI and association authorization, and transfer accounting from orphan to tenant state. Reject unauthorized VNIs; no MAC learning occurs yet.
6. Completion requires the first marker, exactly its required regular positions, and All-1. Verify frame size and CRC once, then validate the reconstructed VXLAN unit and deliver once to the existing decapsulation path. Integrity failure aborts immediately; waiting cannot repair a complete corrupt packet.
7. Missing fragments may be awaited until d, subject to an earlier inactivity timeout or resource failure. Reset the inactivity timer on regular fragments as configured; total deadline remains an additional hard cap. Retire on completion, abort, or expiry.

Arrival timestamps determine eligibility, not timer-sweep timing. Processing of already timestamped packets must respect that rule; do not discard an eligible queued fragment merely because a wall-clock sweep ran first. The deadline bounds admission of pieces, not end-to-end latency or assembly CPU time.

A strict peer and tolerant peer can consume the same transmitted encoding, but they offer different loss behavior on reordered paths. Provision the receiver behavior explicitly. This proposal does not prescribe pretending that optional waiting is already allowed by RFC 8724.

## DTag reuse, restart, and retirement

Starting at zero is acceptable only under a defined reuse policy. Uptime alone proves nothing about old packets or receiver state.

Define B as a configured upper bound from the **last emission of any copy** to its final possible arrival, including queues and replication. Define DeltaMax as the largest receiver entry lifetime accepted for this association. Keep retirement records for at least R>=B after retirement; since the proposal keys by packet identity, this suppresses delayed copies even after successful delivery. Provision slots for the maximum admitted rate and retention; never evict a protected record to admit another packet.

A conservative same-association reuse interval, measured from the previous last emission, is Q>B+DeltaMax+R, including timing margin. By then old fragments, active entries, and retirement records have drained. Record the previous last emission or wait Q after startup if it is unknown. A fast restart is not safe without this wait. A 60-bit space reduces wrap frequency but does not solve a reset-to-zero collision.

For normal operation, allocate unique DTags across all transmit workers and VNIs sharing the association/RuleID. Do not wrap faster than Q permits; block or reconfigure before reuse. Use monotonic elapsed time, not wall clock. Persisting counter reservations or establishing a receiver-visible new association generation can avoid the startup delay, but neither is implicitly provided by this wire format.

After receiver restart, discarded replay history cannot protect against old complete packet copies. Drain for B before reopening the unchanged association, or establish a new generation that rejects old traffic. Coordinate sender admission during the drain. If B cannot be defensibly bounded, these guarantees are best effort; an authenticated generation mechanism is a separate design requirement. Checksums do not prevent hostile replay.

## Compression and tenant isolation

NoCompress requires no compression-context bootstrap, but the fragmentation association still needs configuration. Optional compression needs a separately specified C/D envelope, rules, version/lifetime policy, and per-peer capability agreement. Do not change contexts while fragments using them remain live. Unknown rules are dropped, never guessed.

Initially retain VNI explicitly when compression is eventually defined. Eliding only constant VXLAN bytes saves five raw header bytes before rule overhead; the gain is small for jumbo frames. Compression of the entire inner packet could have different economics and is outside this base proposal. First-fragment VXLAN parsing applies only to NoCompress; compressed data requires its own admission procedure.

Apply orphan caps per peer and globally, then tenant caps when VNI becomes available. Sender policing complements receiver limits; trusted senders are not a substitute for validation. Authenticate the underlay where spoofing or replay is in scope.

## Standards boundaries and decisions still requiring review

The following source facts constrain the proposal: RFC 8724 permits profile-defined non-All-1 FCN meanings; its No-ACK receiver concludes on All-1; the RCS precedes final payload; DTag distinguishes overlapping packets. See [RFC 8724 §§8.2–8.4](https://www.rfc-editor.org/rfc/rfc8724.html#section-8.2). Its compression and fragmentation functions are separable; the precise direct UDP-payload binding here remains a proposal.

RFC 7348 forbids VTEP fragmentation of VXLAN packets. This design uses another UDP binding and reconstructs a VXLAN payload, rather than applying IP fragmentation. That distinction is an argument to review, not proof that no standards clarification is needed. See [RFC 7348 §4.3](https://www.rfc-editor.org/rfc/rfc7348.html#section-4.3).

Before freezing: agree the tolerant extension's standards vehicle; approve 60-bit DTag/four-bit FCN and direct NoCompress binding; define port/RuleID provisioning; configure B, Delta, inactivity timeout and memory limits; confirm retirement affordability at the intended rate. Retain CRC-32C unless the human explicitly changes the inherited decision. No current protocol-number draft status is asserted here.

## Validation and migration

Before implementation, independently review the state machine and encoding. This revision received author self-review only. Implement as a new configuration generation; do not mutate sealed workpackages or treat old tests as validation of this proposal.

Required checks: all K=2..8 FCN sequences; all permutations for small K and sampled larger permutations; every missing position including first and All-1; duplicates and conflicts before/after first; all invalid FCNs; checksum and RCS failure; exact-fit/one-byte-over PMTU cases; tagged and jumbo frames; deadline equality and delayed timer sweeps; orphan/tenant/retirement exhaustion; wrap, concurrent allocation, sender/receiver restart, and late complete replay. Verify strict and tolerant receivers separately. Benchmark memory for retirement records as well as payload buffers; no throughput claims follow from this document.
