# Virtual lab run 2026-11-16: C ↔ Rust in Containerlab on Docker Desktop

- Lab: `interop/vlab/` (Containerlab 0.83.0 via the `ghcr.io/srl-labs/clab` container, Docker Desktop 29.8.2, linux/arm64, on the user's Mac). Same topology and addresses as the physical lab; underlay MTU 1280.
- C: `impl-c/` from branch `impl-c-dev` (image `schcvx-lab-c`, Meson tests run in the build stage). Rust: `interop/impl-rust/` from `origin/main` `aa84d75` (image `schcvx-lab-rust`), built and configured from its build and run surface only (`Cargo.toml`, `vtep.conf.example`).
- Two independent runs (the developer agent's `out/20261116-105804/` and the validation run `out/20261116-110324/`) gave the same picture; the numbers below are from the validation run. Raw output stays in `interop/vlab/out/` (git-ignored).
- netem is applied on vtep1 `eth2` egress, so it reorders the C → Rust datagrams: the **Rust** receiver is the one under test in the netem steps. The C counters in those steps only see TCP ACKs.

## Results

| Step | Receiver mode (both VTEPs) | Forward Mbit/s (C → Rust) | Reverse Mbit/s | TCP retransmits | Rust integrity_failure / expired (increase) |
| --- | --- | --- | --- | --- | --- |
| Pings 56, 1201, 1202, 1472 (DF) | ordered and tolerant | — | — | — | 0 loss in all eight cases |
| iperf3 10 s, no netem | ordered | 893 | 923 | 863 / 0 | 4 / 4 (cumulative) |
| iperf3 10 s, no netem | tolerant | 940 | 924 | 755 / 0 | 0 |
| netem `delay 5ms reorder 50%` | ordered | 0.52 | — | 229 | ~+155 / ~+154 |
| same | tolerant | 29.4 | — | 1025 | 0 |
| same + `limit 100000` | ordered | 0.31 | — | 148 | (lagging counters) |
| same + `limit 100000` | tolerant | 23.7 | — | 1249 | 0 |
| same + `iperf3 -M 1100` (control: no fragmentation; 3000/3000 captured datagrams unfragmented) | ordered | 8.9 | — | 1385 | n/a (no fragments) |
| same control | tolerant | 8.2 | — | 1349 | n/a |

C counters: zero integrity failures, malformed and expired in every step. Raw-socket drops: 16 (ordered run) and 53 (tolerant run) during the plain iperf3 steps, not attributed per VTEP. Daemon CPU per 10 s iperf3 step: about 18 s for C and 21 s for Rust across both directions inside the VM.

The fragmented 1472-byte pings reassemble with a good RCS in the captures (`tshark -2 -X lua_script:interop/wireshark/schcvx.lua ... -Y 'schcvx.rcs.status == "good"'`).

## Findings

1. **The two implementations interoperate in the virtual lab** exactly as in the physical one: unfragmented and fragmented, both receiver modes, both directions.
2. **Ordered vs tolerant under reordering:** with the same netem profile, the ordered (RFC 8724) receiver gets 0.3–0.5 Mbit/s and the tolerant receiver 24–29 Mbit/s, about 60 times more. The Rust counters confirm the mechanism: the ordered receiver counts one integrity failure and one expiry per reordered packet; the tolerant receiver has none. This is the behaviour the draft describes, and the strongest argument so far for specifying the tolerant receiver.
3. **This netem profile also collapses plain TCP in the Docker VM** (8–9 Mbit/s without any fragmentation), so the absolute netem numbers here describe netem in the VM, not SCHC. They cannot be compared with the physical run (344 Mbit/s under the user's netem on server 1); the exact netem command of the physical run was not recorded, which is the first thing to align.
4. The Rust receiver reports counters only as a periodic log line, so per-step attribution of its counters lags; a SIGUSR1-style dump or a counters file (as in the C daemon) would make runs exact.
5. Without impairment the virtual lab runs at ~900 Mbit/s each way, limited by the VM and single-threaded daemons.

## Next steps

- Record the exact netem command used in the physical run and use the same one in `run-matrix.sh`; add a no-reorder delay-only step to separate delay from reordering.
- Attribute raw-socket drops per VTEP.
- Ask Andrii whether the Rust daemon can expose counters on demand.
- Run the same lab in GitHub Actions (Linux runner) as an interop regression test.
