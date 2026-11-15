# Examples

Both examples use the following parameters, produced by the reference model described in Implementation Status.

* RuleID 0 (prototype value); VNI 12345 (0x003039); Frame ID 1 (0x0000000000000001); peer vtep-2.
* PMTU 1500 bytes, IPv4, H_IP = 20 bytes (no outer IP options).
* Illustrative Ethernet frame of 1514 bytes, with byte\[i\] = i mod 256.
* Deadline Δ = 5.0 ms; retirement record lifetime L = 50.0 ms.

The sender produces two fragments. CRC-32C over the 1514-byte frame is 0x487a7b87.

| Index | Count | Slice length | Outer IP length | Header fields (hex) | RCS (hex) |
| --- | --- | --- | --- | --- | --- |
| 0 | 2 | 1454 | 1496 | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x00 Count=0x02 | — |
| 1 | 2 | 60 | 106 | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x01 Count=0x02 | 487a7b87 |

## Example A: Last-Fragment-First Completion

The last fragment (index 1, carrying the RCS) arrives before the nonfinal fragment (index 0). The receiver opens the entry on the last fragment's arrival and delivers once the bitmap is complete and the RCS verifies.

| t | Event | Header fields (hex) | Result | Bitmap after datagram |
| --- | --- | --- | --- | --- |
| 0.0 ms | final fragment (index 1) arrives first | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x01 Count=0x02 | PENDING | 01 |
| 1.0 ms | nonfinal fragment (index 0) arrives second, before expiry at 5.0 ms | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x00 Count=0x02 | DELIVERED | 11 |

Delivered frame length: 1514 bytes; CRC-32C: 0x487a7b87.
Counters after Example A: completed=1, expired=0, late=0, duplicate=0, integrity_failure=0, malformed=0, peer_admission_failure=0, table_exhaustion=0.

## Example B: Deadline Expiry

Only the last fragment (index 1) arrives. The entry opens with expiry = first_rx + Δ = 0.0 + 5.0 = 5.0 ms. No sweep runs. The missing nonfinal fragment (index 0) then arrives after expiry: the entry is ineligible for completion even though no sweep has retired it yet, so it is retired as expired on this arrival and the arriving fragment is itself counted as late.

| t | Event | Header fields (hex) | Result | Bitmap after datagram |
| --- | --- | --- | --- | --- |
| 0.0 ms | final fragment (index 1) arrives alone; entry opens, expiry = 0.0 + Δ = 5.0 ms | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x01 Count=0x02 | PENDING | 01 |
| 7.0 ms | missing nonfinal fragment (index 0) arrives after expiry; the entry is retired as expired and the fragment is counted as late | RuleID=0x00 VNI=0x003039 FrameID=0x0000000000000001 Index=0x00 Count=0x02 | LATE | 01 |

Counters after Example B: completed=0, expired=1, late=1, duplicate=0, integrity_failure=0, malformed=0, peer_admission_failure=0, table_exhaustion=0.

# Test and Benchmark Considerations

An implementation can start with a maximum of two fragments and a two-bit receive mask, and should benchmark the integrity check and the fragmented forwarding path together. Throughput results are pending measurement; this document makes no throughput claim. Acceptance checks include:

* Exact-fit and one-byte-over frame sizes for IPv4 and IPv6, including tagged Ethernet, verifying that every emitted IP packet fits the PMTU.
* Both arrival orders, every single-fragment loss, identical and conflicting duplicates, malformed or inconsistent headers, bad RCS, deadline equality, late arrival after retirement, and Frame ID uniqueness across restart.
* Unsupported peers, ingress replication, table exhaustion, and a change of configured PMTU.
* Benchmarks of a single-fragment/multi-fragment mix, of frames just over the single-fragment threshold `C`, and of the maximum frame size, reporting frame and packet rates, wire bit rate, latency percentiles and drops by cause, and stating whether offered load is ingress frame rate or egress wire capacity.
* Counters for completed frames, expiration, late fragments, duplicates, integrity failure, malformed input, peer admission failure, and table exhaustion.
