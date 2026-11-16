# Virtual interop lab (Containerlab)

A virtual copy of the physical C-Rust lab in `interop/README.md`, run on one machine with Containerlab and Docker.

```
hostA 192.168.44.2 --eth1-- vtep1 (C) --eth2 == MTU 1280 == eth2-- vtep2 (Rust) --eth1-- hostB 192.168.44.3
                                  192.168.11.2/24            192.168.11.1/24
```

Access links have MTU 1500, the underlay link 1280. On vtep1 the daemon `schcvxd` creates the TAP `schc0`, and the entrypoint bridges it with `eth1` in `br0` (no IP). The Rust `vtep` captures and injects frames on `eth1` itself, so it creates no TAP and no bridge is used on vtep2 (see "Rust configuration" below).

## Files

| File | Purpose |
| --- | --- |
| `schcvx.clab.yml` | Containerlab topology (4 `linux` nodes, 3 links with MTU, `RECEIVER` from the environment). |
| `images/c/` | Multi-stage image: builds `impl-c` with meson/ninja/gcc, runs `meson test` (the build fails if a test fails), slim runtime with iproute2, ethtool, tcpdump, procps. |
| `images/rust/` | Multi-stage `cargo build --release` of `interop/impl-rust`, same runtime tools. |
| `images/host/` | iperf3, ping, tcpdump, jq. |
| `run-matrix.sh` | Builds missing images, deploys, runs the test matrix for both receiver modes, writes `out/<timestamp>/`. |

The images are named `schcvx-lab-c`, `schcvx-lab-rust`, `schcvx-lab-host`. Each VTEP image follows one contract: the environment variables `RECEIVER`, `LOCAL_IP`, `PEER_IP` go in; the entrypoint renders that implementation's own configuration file, sets the underlay address, disables TX checksum offload on `eth2` (veth partial checksums are rejected by the C daemon's software UDP checksum check), starts the daemon and stays up while it runs.

## Containerlab on macOS

Containerlab needs a Linux kernel (network namespaces, veth), so it cannot run natively on macOS. Containerlab documents Docker Desktop on macOS as supported through its container image. This lab uses exactly that, with no install on the Mac:

```
docker run --rm --privileged --network host --pid host \
    -v /var/run/docker.sock:/var/run/docker.sock \
    -v /var/run/netns:/var/run/netns \
    -v /var/lib/docker/containers:/var/lib/docker/containers \
    -v /etc/hosts:/etc/hosts \
    -v <topology dir>:<topology dir> -w <topology dir> \
    ghcr.io/srl-labs/clab containerlab <deploy|destroy> -t schcvx.clab.yml
```

`run-matrix.sh` wraps this in its `clab` function (the image is pulled on first use; verified here with containerlab 0.83.0, Docker Desktop 29.8.2, linux/arm64). `--privileged --network host --pid host` makes the clab container act inside the Docker Desktop Linux VM, where it creates the veth pairs and moves them into the node containers. `/var/lib/docker/containers` and `/etc/hosts` let it handle node hostnames. Nothing else is installed.

The topology file declares its own management network (`schcvx-mgmt`, 172.30.250.0/24) because the Containerlab default 172.20.20.0/24 often overlaps existing Docker networks (it did on the development machine). Change it in `schcvx.clab.yml` if it clashes with yours.

## Running

```
interop/vlab/run-matrix.sh                 # both receiver modes, about 3 minutes plus image builds
interop/vlab/run-matrix.sh --force-build   # rebuild all images (re-runs meson test)
interop/vlab/run-matrix.sh --modes tolerant --iperf-secs 5
```

The Rust sources are not on this branch: the script takes them from `origin/main:interop/impl-rust` with `git archive` (override with `RUST_REF`). Build contexts and the staged topology are created under `interop/vlab/out/.stage/` and removed at exit. On exit, also on error or Ctrl-C, a trap destroys the lab and removes the management network.

Per mode the script deploys the lab with `RECEIVER=<mode>`, waits until vtep1 has `schc0` in `br0` and the Rust daemon runs, and then runs:

1. `ping -M do -c 10 -s N` from hostA to hostB for N = 56, 1201, 1202, 1472 (0% loss expected; 1201 gives a 1243-byte frame, unfragmented; 1202 and 1472 are fragmented).
2. `iperf3` TCP for 10 s, forward (hostA to hostB, server on hostB) and reverse (`-R`).
3. netem on vtep1 `eth2` (egress, so the C-to-Rust direction), `iperf3` 10 s forward: (a) `delay 5ms reorder 50%`, (b) the same with `limit 100000`, (c) `iperf3 -M 1100` under (b), so frames stay below 1244 bytes and are not fragmented. The qdisc is removed after each step.

Ping steps give PASS or FAIL. iperf3 steps FAIL only if iperf3 does not complete. The netem steps are INFO: the ordered receiver is expected to drop reordered fragments, so low throughput there is the draft's behaviour, not a lab fault.

## Reading the results

`out/<timestamp>/summary.tsv` is the table printed at the end. Columns: step, receiver mode, iperf3 throughput forward and reverse (Mbit/s, received side), TCP retransmits (forward/reverse), C `integrity_failures` and `expired` (cumulative within the deployment of that mode), `/proc/net/raw` drops summed over both VTEPs, and daemon CPU seconds consumed during the step (utime + stime from `/proc/<pid>/stat`).

Per step, `out/<timestamp>/<mode>/<step>/` contains:

| File | Content |
| --- | --- |
| `vtep1-eth2.pcap` | `tcpdump -c 3000` on vtep1 `eth2` (UDP port 60789 only), taken during the step. |
| `c-counters.txt` | C counters file, written after SIGUSR1. |
| `rust-log-tail.txt` | Last 40 lines of the Rust log (it prints a periodic `counters:` line). |
| `vtep{1,2}-proc-net-raw.txt`, `vtep2-proc-net-udp.txt` | Socket tables with drop counts. |
| `iperf-*.json`, `ping.txt` | Raw tool output. |

Open a capture:

```
tshark -2 -X lua_script:interop/wireshark/schcvx.lua -r <capture.pcap> -Y 'schcvx.rcs.status == "good"'
wireshark -X lua_script:interop/wireshark/schcvx.lua <capture.pcap>
```

If `schcvx.lua` is also installed in your personal Wireshark plugins folder, Wireshark refuses to load it twice ("there cannot be two protocols with the same description"). Either drop `-X lua_script:...` or run with an empty plugin folder, for example `HOME=$(mktemp -d) tshark -2 -X lua_script:...`.

## Rust configuration

`vtep.conf.example` of `interop/impl-rust` (commit aa84d75) offers a subset of the lab profile. Mapped: `local`/`peer` (192.168.11.x), `port = 60789` (one socket for both directions, so source port equals destination port), `pmtu = 1280`, `h-ip = 20`, `variant = compact`, `rule = 1`, `vni = 100`, `receiver`, `inactivity-ms = 10`, `deadline-ms = 20` (Delta), `retirement-ms = 100` (R), `max-entries = 1024`, `trace = false`, `lan-if = eth1`, and `allow = 192.168.44.0/24`, which Rust needs to forward anything (its forwarding filter is on the inner IPv4 destination; this is a Rust-only extra).

Not expressible in that configuration (left at whatever the binary does): C/D RuleID 0, `max_frame_len` 1518, max fragments 8, S, B, Q, `dtag_start`, `dtag_rate`, `dtag_burst`, per-association `max_bytes` (33554432), `orphan_budget` (2048), `max_retired` (65536), the global `max_entries` 4096, `max_bytes` 67108864 and `orphan_budget` 4096, a separate source port, and a counters file or SIGUSR1 (counters appear as a periodic line in the log, collected as is). Rust has no TAP: it works on `eth1` directly.

## Limits

- Everything runs in the Docker Desktop VM on shared CPUs: both daemons, iperf3 and the veth links compete for the same cores. Throughput, retransmits and CPU times are relative numbers for comparing receiver modes and steps. They are not hardware performance and not comparable to the physical lab.
- The underlay is a veth pair, not a wire: netem delay and reordering are the only impairments, and TX checksum offload is disabled to match what the C daemon needs.
- The Docker Desktop VM disk must have free space for the image builds (a few GB); `docker builder prune` helps.
- Base images use Debian trixie. The `bookworm` images available at the time of writing failed apt signature checks.
