# Interop run 2026-11-15: C (server 1) and Rust (server 2), first contact

- Draft: `draft/revised/draft-vladyka-schc-vxlan-fragmentation-00-revised.md` at `3e72a86`.
- C implementation: branch `impl-c-dev` at `1a55761`, `schcvxd` built with GCC on server 1, receiver mode `ordered`, lab profile from `interop/README.md` (compact, fragmentation RuleID 1, C/D RuleID 0, UDP 60789, VNI 100, PMTU 1280, H 20, DTag start 0).
- Rust implementation: running on server 2 (192.168.11.1); version and configuration not recorded here.
- Capture: `2026-11-15-c-to-rust-step1.pcap` (underlay `enp3s0` on server 1, `udp port 60789`).

## Setup on server 1

`schc0` has 10.100.0.1/24. Server 2 is already bridged to Andrii's computer (192.168.44.3), so there is no 10.100.0.2. For this run server 1 also had 192.168.44.10/32 on `schc0` and a host route to 192.168.44.3 via `schc0`; `enp0s20f0u2` was not changed.

## Results (pings from server 1 to 192.168.44.3, `-M do`)

| ICMP payload | Ethernet frame | Path | Result |
| --- | --- | --- | --- |
| 56 | 98 | unfragmented (107-byte datagram) | 5/5 replies |
| 1201 | 1243 | unfragmented (1252-byte datagram, IP 1280) | 5/5 replies |
| 1202 | 1244 | 2 fragments: 1248 + 19 bytes | 0/5 replies |
| 1472 | 1514 | 2 fragments: 1248 + 289 bytes | 0/5 replies |

- Unfragmented SCHC Packets interoperate in both directions: server 2's datagrams (C/D RuleID 0, VXLAN header `08 00 00 00 00 00 64 00`) are accepted and delivered by the C daemon, and server 2's side answers ARP and pings sent by server 1.
- Server 2 sent no fragmented datagram during the run, so either the Rust receiver did not accept the C fragments, or the Rust sender did not fragment its replies (1244- and 1514-byte frames); the capture on server 1 cannot tell which.

## The C fragments, decoded (first fragmented ping)

| Datagram | Bytes after the UDP header | Meaning |
| --- | --- | --- |
| 1248 bytes | `10 00 00 00 08` `00` `08 00 00 00 00 00 64 00` `8c ae 4c dd 33 a5 ...` | RuleID 1, DTag 0, FCN 1000 (first, K = 2); tile starts with C/D RuleID 0, VXLAN header (I flag, VNI 100), Ethernet frame |
| 19 bytes | `10 00 00 00 0f` `ff ae a2 46` `a8 a9 ... b1` | RuleID 1, DTag 0, FCN 1111 (All-1); RCS 0xffaea246; 10-byte last tile |

The next packet uses DTag 1 (`10 00 00 00 18`, `10 00 00 00 1f`). The C encoder reproduces `draft/revised/wire-vectors.json` byte for byte, and C-to-C runs pass in the namespace test.

## Questions for the Rust side

1. Did the Rust receiver see the 1248- and 19-byte datagrams, and what did it do with them (counters, logs)?
2. Does the Rust sender fragment frames larger than 1243 bytes at PMTU 1280 (startup quarantine Q, DTag rate limit, configured maximum frame length)?
3. Is the Rust side configured with the compact variant, fragmentation RuleID 1 and C/D RuleID 0?
