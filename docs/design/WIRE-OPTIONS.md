# Wire design options for the SCHC VXLAN fragmentation draft

Status: decision aid, not part of the frozen design. It does not change `DESIGN.md`, the draft, the reference model or the sealed work packages. Based on the draft at commit `8485aac`.

Verified against RFC 8724 text: DTag semantics and size (§8.2.4), that a Profile may define FCN values other than All-1 (§8.4.1), that the recommended No-ACK FCN size is 1 bit (§8.4.1), the Inactivity Timer reset on each Regular fragment (§8.4.1.2), and that integrity checking is required (§8.2.3, §8.4.1). **Not verified:** the exact layout of the No-ACK All-1 message (I assume the RCS sits in its header, after the FCN and before the payload), and the DTag reuse text for No-ACK. All sizes and byte counts below are my arithmetic from the layouts stated here.

## The three options

All three keep: Ethernet frame without FCS as the payload, ordinary VXLAN for frames that fit, a distinct UDP port for the extension, one source port per frame, CRC-32C as the reassembly check, no ACKs, fixed-or-resettable receiver deadline, retirement records.

### A. Current draft (new mode, "Out-of-Order No-ACK")

Fragment header (14 bytes): RuleID 1, VNI 3, Frame ID 8, index 1, count 1; slice; the final fragment ends with a 4-byte RCS trailer. Fragmented unit: the Ethernet frame.

### B. Pure No-ACK Profile, VNI selected by RuleID

SCHC message: RuleID 1, DTag (Profile-sized, 4 bytes assumed here), FCN 1 byte (Profile-defined countdown, All-1 on the last fragment), payload; the All-1 fragment also carries the 4-byte RCS in its header. Fragmented unit: the Ethernet frame. The VNI is implied by the RuleID: one RuleID per VNI, so at most 256 VNIs per peer association with a 1-byte RuleID (wider RuleIDs are a Profile choice).

### C. Pure No-ACK Profile over the whole VXLAN payload

Same SCHC message as B. Fragmented unit: the 8-byte VXLAN header followed by the Ethernet frame, that is, exactly the UDP payload of an ordinary VXLAN packet. The VNI travels inside the first slice. The receiver reassembles, then hands the unit unchanged to the existing VXLAN decapsulation and bridge path. Optionally a SCHC compression rule shrinks the constant VXLAN header (compress first, then fragment); that is a later extension, out of scope here.

## Numbers for the worked case

PMTU 1500, IPv4 with no options (`H_IP` = 20), a 1514-byte Ethernet frame, uniform slice budget that keeps the 4-byte RCS allowance.

| | A (current) | B | C |
| --- | --- | --- | --- |
| Header per regular fragment | 14 | 6 | 6 |
| Header of the final fragment | 14 (+4 trailer) | 10 (RCS in header) | 10 |
| Fragmented unit | 1514 | 1514 | 1522 |
| Slice budget `C` | 1454 | 1462 | 1462 |
| Slices | 1454 + 60 | 1462 + 52 | 1462 + 60 |
| Outer IP lengths | 1496, 106 | 1496, 90 | 1496, 98 |
| Fragmentation header bytes per frame | 32 | 16 | 24 |
| Largest frame in two fragments | 2908 | 2924 | 2916 |

## Behaviour comparison

| Aspect | A | B | C |
| --- | --- | --- | --- |
| SCHC conformance | New mode; no RFC 8724 wire compatibility | No-ACK Profile plus receiver behaviour | Same as B |
| Where the VNI is | Every fragment | RuleID | First slice (inside the unit) |
| Early VNI admission before allocating state | Yes | Yes (RuleID is in every fragment) | **No**, only after reassembly |
| Per-VNI caps and counters before completion | Yes | Yes | **No** |
| Number of VNIs per association | 16 million | 256 with a 1-byte RuleID | 16 million |
| Reuse of the existing VXLAN decap path | Frame handed to the bridge | Frame handed to the bridge | **Unit handed to unchanged VXLAN decap** |
| Fragment order | Explicit ascending index | Profile-defined FCN countdown | Same as B |
| Completeness detection | Explicit bitmap sized by the count | Not explicit: needs a Profile-fixed fragment count, or a CRC test on each arrival once the All-1 fragment is held | Same as B |
| Reordering | Native, any first fragment | Trivial for two fragments (All-1 is last); beyond two it needs receiver behaviour outside the RFC text | Same as B |
| Receiver timer | Fixed first-arrival deadline | RFC Inactivity Timer (resets per fragment), or a fixed deadline as a deviation | Same as B |
| Frame ID / DTag scope | Per association and VNI | Per RuleID (so per VNI) | Per association (one RuleID) |
| Retirement records | Required | Required (RFC suggests a recent-DTag check) | Required |
| Error accounting | Integrity failure vs expiry separate | A failed CRC test is not an error; it waits and then expires | Same as B |
| Extra bytes per frame vs the bare frame | 32 | 16 | 24 |
| RFC 7348 §4.3 argument ("VTEPs MUST NOT fragment VXLAN packets") | Not at issue: a different format on a different port | Same as A | Needs an explicit argument: the unit is a VXLAN payload even though no VXLAN packet is sent |

## Effects on the existing work

- **Draft:** B and C remove the "new mode" argument and the count and index validation text; sections 1, 2 and 3 change substantially; the Open Issues for the SCHC mapping shrink. A needs none of this.
- **Reference model and tests:** B and C change `wire.py`, `sender.py` and `receiver.py` (no count or index, FCN semantics, a CRC test for completion, DTag keying) and the generated examples; most receiver tests stay in spirit (order, loss, duplicates, deadline, retirement).
- **DPDK implementation spec:** only `schcvx_wire`, the receiver completion logic and the vector generator change. The rest of the application is the same, which is why the spec isolates the wire codec.
- **Sealed pipeline:** any of these changes means a new configuration generation.

## When each option fits

- **A** if early VNI admission, per-VNI caps and an explicit bitmap matter more than staying close to RFC 8724. Nothing to redo.
- **B** if the number of VNIs per peer is small (well under 256), early VNI admission is wanted, and the goal is a pure No-ACK Profile.
- **C** if peers are trusted to send only admitted VNIs (a managed fabric, as in the draft's target), you want the smallest conceptual change to VXLAN, and you can accept state being held for a frame before its VNI is known.

## Open points whichever option is chosen

- Restart-safe DTag or Frame ID and the retirement-record sizing.
- DTag width (32 bits wraps in about 3.8 hours at 312,000 frames per second; 16 bits wraps in about 0.2 s, too tight against a deadline plus retirement time of tens of milliseconds).
- Deadline-equality rule.
- For more than two fragments in B and C: whether to fix the fragment count per Profile, or define the CRC completion test and its cost (at most N−1 extra CRC passes per frame, usually one).
- Shared SCHC UDP port with `draft-ietf-schc-protocol-numbers`, and header versioning.
- Whether any of this is to be specified as a SCHC Profile or as a new mode.
