# SCHC VXLAN fragmentation: interop plan

Two independent implementations of the revised draft are built and tested against each other in the lab:

| Implementation | Language | Owner | Tooling | Directory |
| --- | --- | --- | --- | --- |
| Userspace | Rust | Andrii Vladyka | Claude | `impl-rust/` |
| Userspace (DPDK later) | C | Alexander Pelov | Codex | `impl-c/` (spec: `impl-c/SPEC.md`) |

## 1. Authority

- The only specification is `draft/revised/draft-vladyka-schc-vxlan-fragmentation-00-revised.md` at commit `3e72a86` (Option 1: every frame on a configured association uses the binding; a SCHC Packet that fits is sent unfragmented).
- If the draft is unclear or wrong, fix the draft first, then both implementations. Do not settle a question in code only.

## 2. Independence

- Do not read, port or ask an assistant to look at the other implementation's code.
- Each implementation directory is touched only by its owner.
- Shared material is limited to: the draft, `draft/revised/wire-vectors.json`, `draft/revised/check_vectors.py`, and this `interop/` directory (scenarios and results).

## 3. Common lab profile (first interop round)

The port, VNI and timer values are lab choices, not values from the draft. They satisfy the draft's constraints: R >= W = S + B, Delta >= W, and Q > B + D + R with D = max(W + I, Delta) = 21 ms.

| Parameter | Value |
| --- | --- |
| Underlay | IPv4, no options, PMTU 1280 (H = 20) |
| Access links | MTU 1500 |
| Variant | Compact: 4-bit fragmentation RuleID = 1, 32-bit DTag, 4-bit FCN |
| C/D RuleID | NoCompress = 0 (8 bits) |
| UDP destination port | 60789 (locally configured) |
| VNI | 100 |
| Max fragments | 8 |
| UDP checksum | Always generated; a zero or wrong checksum is dropped, IPv4 included |
| Source port | Same for all fragments of a packet |
| Receiver | Ordered RFC 8724 receiver by default; tolerant receiver behind a configuration switch |
| Bounds (lab only) | S = 1 ms, B = 10 ms (so W = 11 ms); inactivity I = 10 ms; tolerant deadline Delta = 20 ms; retirement R = 100 ms; quarantine Q = 200 ms |
| DTag | One counter per association and fragmentation RuleID, start value 0; after start, no fragmented packet is sent until Q has elapsed (draft "DTag Reuse and Restart") |

## 3a. Lab topology

```
computer A ── server 1 (C) ══ 1280-byte IPv4 underlay ══ server 2 (Rust) ── computer B
```

| | Server 1 | Server 2 |
| --- | --- | --- |
| Administered by | Alexander Pelov | Andrii Vladyka |
| Implementation | C (`impl-c/`) | Rust (`impl-rust/`) |
| Underlay interface | `enp3s0` | chosen by Andrii |
| Underlay address | 192.168.11.2/24 | 192.168.11.1/24 |
| TAP test address (step 1) | 10.100.0.1/24 | 10.100.0.2/24 |
| Access port to the computer (step 2) | `enp0s20f0u2` | chosen by Andrii |

Bring-up order:

1. **TAP only.** Each daemon creates its TAP device and the test address is set on it; no physical access port is bridged. Ping 10.100.0.1 to 10.100.0.2 with `ping -M do -s 1472` (full-size frames, two fragments each way).
2. **Bridge the access port.** Bridge the TAP device with the access port so the computers join the overlay. On server 1 the access port also carries the management SSH session, so the change is applied with an automatic rollback.

Because each server runs a different implementation, every lab run is a mixed C–Rust run. Each side first passes its own same-implementation tests outside the lab (for C: two Linux network namespaces on server 1).

During tests, both sides capture on their underlay interface (`tcpdump -i <underlay> -w <file> udp port 60789`).

## 4. Conformance before interop

- The encoder must reproduce every `udp_payload_hex` in `draft/revised/wire-vectors.json` byte for byte (frame byte i = i mod 256, DTag 1, fragmentation RuleID 1).
- The decoder must accept those vectors and reject the malformed cases that `check_vectors.py` rejects.
- Boundaries at PMTU 1280 over IPv4: an Ethernet frame of 1243 bytes is sent unfragmented; 1244 bytes gives two fragments; 1514 bytes gives fragments of 1243 and 280 bytes.

## 5. Test matrix

In the lab, run each test in both directions (C sending to Rust and Rust sending to C). Same-implementation runs happen outside the lab, before the lab runs.

| # | Test | Command on the end hosts | Pass if |
| --- | --- | --- | --- |
| 1 | Small frames | `ping -c 100 <peer>` | 0% loss; unfragmented SCHC Packets in the capture |
| 2 | Boundary | `ping -M do -s 1201 <peer>` and `ping -M do -s 1202 <peer>` | The 1243-byte frame is unfragmented; the 1244-byte frame is two fragments |
| 3 | Full-size frames | `ping -M do -s 1472 <peer>` | 1514-byte frames delivered as 1243 + 280 |
| 4 | Throughput | `iperf3` TCP and UDP, both directions | Completes; counters show no integrity failures |
| 5 | Reorder (tolerant receiver) | `tc netem reorder` on the underlay | Frames delivered; the ordered receiver drops them, as the draft says |
| 6 | Loss and duplicates | `tc netem loss` and `tc netem duplicate` on the underlay | Incomplete packets expire; duplicates are handled as the draft says |
| 7 | Restart | Restart the sender mid-stream | No mixed or corrupt frames delivered |

Tests 5 and 6 need the underlay interface to stay under kernel control on at least one server (netem does not apply to a DPDK-owned port).

## 6. Recording results

- For each run, keep a pcap of the underlay, the counters of both sides, and the commits of both implementations and of the draft.
- Write results to `interop/results/<date>-<test>.md`.
- Report any disagreement as a GitHub issue labelled `interop`, quoting the draft section involved.
