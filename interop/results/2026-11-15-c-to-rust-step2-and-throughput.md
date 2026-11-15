# Interop run 2026-11-15 (afternoon): computer to computer, throughput, reordering

- Draft: `draft/draft-vladyka-schc-vxlan-fragmentation-00.md` (wire format unchanged since `3e72a86`).
- C implementation: branch `impl-c-dev`, from `9abaaf4` to `4ced57d` during the session; `schcvxd` built with GCC on server 1, receiver mode `ordered`, lab profile from `interop/README.md`.
- Rust implementation: Andrii's, on server 2 (192.168.11.1); receiver mode not recorded.
- Follows `2026-11-15-c-to-rust-step1.md`.

## Step 2: bridging the access port on server 1

Server 1 was bridged at runtime (nothing in `/etc/nixos`): NetworkManager set to leave `enp0s20f0u2` unmanaged; bridge `br-schc` with the MAC of `enp0s20f0u2`; `enp0s20f0u2` and the daemon's TAP `schc0` as ports; 192.168.44.1/24 moved from `enp0s20f0u2` to `br-schc`. A 3-minute revert timer guarded the change and was cancelled after SSH was confirmed. Undo with `/tmp/schc-bridge-revert.sh` on server 1, or reboot.

The bridge learned the user's computer (192.168.44.2, `00:e0:4c:68:00:2f`) on `enp0s20f0u2` and Andrii's computer (192.168.44.3, `8c:ae:4c:dd:33:a5`) on `schc0`.

Pings from the user's computer to Andrii's computer, `-D` (DF), 10 each:

| ICMP payload | Path over the underlay | Result | RTT avg |
| --- | --- | --- | --- |
| 56 | unfragmented | 10/10 | 3.9 ms |
| 1201 (1243-byte frame) | unfragmented, 1252-byte datagram (IP 1280) | 10/10 | 4.1 ms |
| 1202 | 2 fragments each way | 10/10 | 4.5 ms |
| 1472 (1514-byte frame) | 2 fragments each way | 10/10 | 4.2 ms |

The fragmented path that failed in the morning run worked from this point on, in both directions; no change was made on the C side, so the Rust side changed in between (not recorded here).

## Throughput (iperf3 TCP between the two computers)

| Stage | Reverse (Rust sends, C reassembles) | Forward (C sends, Rust reassembles) | Finding |
| --- | --- | --- | --- |
| First runs | ~0–35 Mbit/s, collapsing | ~650–760 Mbit/s | — |
| Measured on server 1 | 29 Mbit/s; in 6 s: +5,323 `table_exhaustion`, +4,454 raw-socket drops (`/proc/net/raw`), 6,199 TCP retransmits | — | C receiver: retirement records reserved full entry slots (capacity 1024, R = 100 ms); raw socket had the 208 KB kernel default buffer |
| Bigger tables + 16 MB buffer (`9abaaf4`) | 37–50 Mbit/s, zero drops, zero retransmits | ~415–445 Mbit/s | `schcvxd` at ~100% of one core: linear scans over all entry and retirement slots on every datagram |
| O(1) tables, separate `max_retired` (`9a4600c`) | **532 Mbit/s**, CPU ~65% of one core | **732 Mbit/s**, CPU ~75% | zero `table_exhaustion`, raw drops, integrity failures, expiries over 285,000 delivered packets |

Correction: an intermediate note in this session attributed the slow reverse direction to the Rust sender (no retransmissions, no drops). That was wrong: the C receiver was CPU-bound and the 16 MB buffer turned loss into queueing delay. Lesson: record the daemon's CPU time with every throughput run.

Also fixed (`4ced57d`): after an ICMP port-unreachable from a peer that was not listening yet, the next `send()` on the connected UDP socket fails with ECONNREFUSED and the frame was lost; the daemon now retries once and rate-limits the warning.

## Reordering with netem (user's run)

Setup: netem on **server 1's underlay `enp3s0`** (egress, so it reorders C → Rust datagrams), 50% of packets delayed by 5 ms; one TCP iperf3 stream for 722 s.

| Metric | Result |
| --- | --- |
| Average bitrate | 343.7 Mbit/s (clean baseline ~670) |
| Maximum / minimum (1 s intervals) | 410 / 0 Mbit/s |
| Retransmissions | 212,686 |
| Largest events | 35,808 retransmits around 260–262 s with ~1 s stall; 34,688 around 628–630 s |
| Stability | the single TCP session survived the whole run |

Analysis (not yet confirmed by counters; the raw log is on the machine that ran iperf3, `/tmp/iperf_netem.log`):

1. Delaying half of the datagrams by 5 ms regularly delays a packet's first fragment while its All-1 goes straight through. An **ordered** receiver (RFC 8724 No-ACK) concludes on the early All-1 and drops the packet; only the tolerant receiver waits for the missing fragment. The Rust receiver's mode during the run determines how much of the loss is this effect.
2. TCP itself reacts to reordering: 5 ms of reordering at these rates produces duplicate ACKs and spurious fast retransmits even when nothing is lost, which reduces the window.
3. netem's default queue `limit` is 1000 packets; at ~60,000 packets per second a 5 ms delay holds ~300 packets on average, but bursts can exceed the limit and drop.

Recommended rerun matrix (read the C counters `integrity_failures`, `expired`, `late` and the Rust counters after each run):

| Run | Purpose |
| --- | --- |
| tolerant receivers on both sides, same netem | removes effect 1 |
| same, `netem ... limit 100000` | removes effect 3 |
| `iperf3 -M 1100` (frames stay under the 1243-byte unfragmented limit) with the same netem | control: TCP's own reorder sensitivity without fragmentation (effect 2) |
| ordered receivers, same netem | expected worst case for the RFC 8724 profile |

## State left on server 1

- `schcvx-lab` (transient systemd unit) running `~/schcvx-git/build-it/linux/schcvxd -c ~/schcvx-lab.conf` (`receiver = ordered`, `max_entries = 1024`, `max_retired = 65536`).
- `br-schc` with `enp0s20f0u2` and `schc0`, 192.168.44.1/24 on the bridge; NetworkManager does not manage `enp0s20f0u2`.
- Kernel socket-buffer defaults restored; no netem qdisc on `enp3s0`.
- None of this survives a reboot.
