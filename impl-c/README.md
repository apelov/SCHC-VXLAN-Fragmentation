# schcvxd: SCHC VXLAN fragmentation in C

A userspace C implementation of the revised draft
`draft/revised/draft-vladyka-schc-vxlan-fragmentation-00-revised.md`, written for functional and
interoperability testing against the Rust implementation. It is not a production data plane:
one thread, kernel TAP and sockets, no DPDK.

The architecture and the work packages are in `SPEC.md`; design choices are in `DECISIONS.md`.

## Layout

```
core/        portable C11 library: CRC-32C, wire codec, sender, DTag allocator, retirement,
             ordered and reordering-tolerant receivers, counters (no sockets, no clock, no allocation after init)
linux/       daemon: configuration parser, portable daemon layer, TAP/raw-socket/epoll I/O, main (schcvxd)
tests/       unit tests, wire-vector conformance, receiver scenarios, fuzz targets, namespace integration test
tools/       vectors_to_c.py (turns draft/revised/wire-vectors.json into a C header)
nix/         package.nix and module.nix; flake.nix at this level
```

## Build and test

Meson, Ninja and Python 3 are required. The daemon `schcvxd` is built only on Linux; the core and
the portable tests build on macOS too. From the repository root:

    meson setup build-c impl-c
    meson compile -C build-c
    meson test -C build-c

The tests read `draft/revised/wire-vectors.json`, so build from a checkout of the whole repository.

Sanitizer build:

    meson setup build-asan impl-c -Db_sanitize=address,undefined
    meson test -C build-asan

Fuzz smoke tests (1000 generated inputs per target, named `fuzz-smoke`) run as part of `meson test`.
When the compiler supports libFuzzer (clang), the targets `fuzz_classify`, `fuzz_ordered` and
`fuzz_tolerant` are also built and can be run directly with libFuzzer arguments.

The `docs` test checks that every configuration key accepted by `linux/config.c` appears in this file.

## Running

    schcvxd -c /path/to/schcvx.conf

The daemon needs root, or `CAP_NET_ADMIN` (create and configure the TAP device) and `CAP_NET_RAW`
(raw receive socket), plus access to `/dev/net/tun` and a writable directory for the counters file.
It creates one non-persistent TAP device per association; the device disappears when the process exits.
It does not assign IP addresses and does not create or modify bridges. SIGUSR1 rewrites the counters
files immediately; SIGINT and SIGTERM stop it.

## Configuration reference

INI-style text: a `[global]` section and one `[association NAME]` section per peer; `#` and `;`
start comment lines. Every key is required, in both sections, and none may be repeated. There are no
built-in defaults. Values are decimal numbers or the strings shown. **All durations are nanoseconds.**
`interop/README.md` gives the lab values; `linux/schcvx.conf.example` is a complete file.

### `[global]`

| Key | Meaning |
| --- | --- |
| `max_entries` | Global limit on receiver entries (positive integer). |
| `max_bytes` | Global limit on retained fragment bytes (positive integer). |
| `orphan_budget` | Global limit on orphan entries, tolerant receiver (positive integer). |

### `[association NAME]`

| Key | Meaning and allowed values |
| --- | --- |
| `local_ip` | Local IPv4 address of the underlay, dotted quad. |
| `peer_ip` | Peer IPv4 address, dotted quad. |
| `udp_port` | UDP destination port, both directions, 1 to 65535 (lab 60789). |
| `src_port` | Fixed UDP source port of the transmit socket, 1 to 65535. |
| `variant` | `compact` (32-bit DTag) or `extended` (60-bit DTag). |
| `fr_rule_id`, `cd_rule_id` | Fragmentation and C/D RuleIDs, 0 to 255; must be prefix-free together. |
| `vnis` | Comma-separated VNIs, each 0 to 16777215, at most the compiled maximum. All are admitted on receive; the first is used for transmission. |
| `tap` | TAP device name (unique per association; the kernel limits interface names to 15 characters). |
| `pmtu` | Underlay path MTU in bytes (lab 1280). |
| `h_ip` | IP header length in bytes (lab 20). |
| `max_frame_len` | Maximum Ethernet frame accepted from the TAP, bytes (1518 allows one VLAN tag). The TAP MTU is set to this minus 14. |
| `max_fragments` | Maximum fragments per SCHC Packet, 2 to 8. |
| `receiver` | `ordered` or `tolerant`. |
| `S` | Timer S, nanoseconds. |
| `B` | Timer B, nanoseconds (may be 0). |
| `I` | Inactivity timer I, nanoseconds. |
| `Delta` | Fixed reassembly deadline Delta, nanoseconds. |
| `R` | Retirement time R, nanoseconds. |
| `Q` | Startup quarantine Q for DTag allocation, nanoseconds. |
| `max_entries` | Receiver entries for this association (positive). It also bounds retired identifiers, so set it to at least (expected packet rate x R) plus the active entries. Example: about 63,000 full-size packets per second at 760 Mbit/s with R = 100 ms needs about 6,300; the shipped default is 16384. When it is too small the receiver refuses packets (`table_exhaustion`). |
| `max_bytes` | Retained bytes for this association (positive). |
| `orphan_budget` | Orphan entries for this association. |
| `dtag_start` | First DTag value; below 2^32 (compact) or 2^60 (extended). |
| `dtag_rate` | DTag allocations per second (positive). Must exceed the expected fragmented frame rate (the default of 1,000,000 per second never throttles); when exhausted, fragmented sends are dropped and counted. Parser check: `dtag_rate` x `Q` + `dtag_burst` < 2^32 for compact. |
| `dtag_burst` | DTag token bucket depth (positive). Default 10000. |
| `counters_file` | Path of the counters file. Associations sharing a path share one file. |
| `counters_interval` | Counters write interval, nanoseconds (positive). |

### Startup constraints

The daemon refuses to start, naming the section and key, when any of these fails:

- a key is unknown, duplicated, missing, or has an invalid value; the file has no association;
- `fr_rule_id` and `cd_rule_id` are not prefix-free;
- the transmit configuration is invalid: `pmtu` must be at least `h_ip` + 8 + All-1 header + 9, `max_fragments`
  must be 2 to 8, and a maximum-length frame (+ 9 bytes of SCHC prefix) must fit in `max_fragments` fragments;
- `S`, `I`, `R`, `Q`, `counters_interval`, `max_entries` and `max_bytes` are positive;
- with W = S + B: R >= W; for `receiver = tolerant`, Delta >= W;
- with D = max(W + I, Delta): Q > B + D + R;
- the DTag allocator accepts `dtag_start`, `dtag_rate`, `dtag_burst`: the identifier space (2^32 or 2^60) must hold
  burst + rate x Q without wrapping inside the quarantine.

### Raw-socket receive buffer

The daemon requests a 16 MiB receive buffer on its raw socket (`SO_RCVBUFFORCE`, falling back to `SO_RCVBUF`), so bursts are not dropped at the kernel default (`net.core.rmem_default`). Startup continues if both requests fail. Confirm it in the daemon log: an `INFO: raw socket receive buffer is N bytes` line (the kernel reports about twice the request) or a `WARNING` line. Kernel drops show in the drops column of `/proc/net/raw`.

## Counters

The counters file has one `[association NAME]` header per association followed by `name value` lines:

`packets_sent_unfragmented`, `packets_sent_fragmented`, `sender_drop_length`, `sender_drop_k_range`,
`sender_drop_s_exceeded`, `sender_drop_dtag_admission_stopped`, `sender_drop_quarantine`, `packets_delivered`,
`expired`, `late`, `duplicates`, `integrity_failures`, `malformed` (includes zero or wrong UDP checksum),
`unknown_rule`, `peer_admission_failures`, `vni_admission_failures`, `table_exhaustion`, `sender_aborts_received`.

The file is replaced atomically every `counters_interval` and on SIGUSR1.

## NixOS

`flake.nix` in this directory provides `packages.default` (the daemon; its check phase runs the Meson tests),
`nixosModules.default`, `devShells.default` and `checks`. Build from the repository (the flake needs the
repository root, because the tests read `draft/revised/wire-vectors.json`):

    nix build ./impl-c
    nix develop ./impl-c

The flake must be referenced as a git flake that includes the repository root, because the package build reads
`draft/revised/wire-vectors.json` from outside `impl-c`: use `nix build ./impl-c` inside a git checkout, or
`git+file:///path/to/repo?dir=impl-c` or `github:apelov/SCHC-VXLAN-Fragmentation?dir=impl-c` as a flake input.
A plain `path:` reference to the `impl-c` directory fails, since the repository root is then not part of the source.

The module `services.schcvx` writes the configuration into the Nix store and runs `schcvxd -c <file>` as a
systemd service (dynamic user; capabilities `CAP_NET_ADMIN` and `CAP_NET_RAW` only; `/dev/net/tun` allowed; runtime
directory `/run/schcvx`). Association options use the daemon key names; most default to the lab profile, while
`local_ip`, `peer_ip`, `vnis` and `tap` are required. Extra per-association options: `bridge` and `accessInterface`.
With `bridge = null` (default) no network interface is touched. With `bridge` set, the module creates the bridge
if missing, enslaves the TAP device and `accessInterface` (when given), and sets them up. The module never adds,
removes or moves IP addresses. Two associations with the same `tap` fail evaluation.

Example for lab server 1, step 1 (TAP only, no bridge):

```nix
{
  imports = [ schcvx.nixosModules.default ];   # input: git+file:///path/to/repo?dir=impl-c (not a plain path: reference)
  services.schcvx = {
    enable = true;
    associations.lab = {
      local_ip = "192.168.11.2";   # enp3s0
      peer_ip = "192.168.11.1";
      udp_port = 60789;
      vnis = [ 100 ];
      pmtu = 1280;
      variant = "compact";
      tap = "schc0";
    };
  };
}
```

The operator then assigns the test address, which the module does not do:

    ip addr add 10.100.0.1/24 dev schc0

Test with `ping -M do -s 1472 10.100.0.2`.

### WARNING: bridging the access port on lab server 1

In step 2 (`bridge = "br0"; accessInterface = "enp0s20f0u2";`) the access port `enp0s20f0u2` also carries the
management SSH session (192.168.44.1). Enslaving it to a bridge removes its address from the port's point of view: the
session is lost until the operator moves 192.168.44.1 to the bridge (`br0`), which the module deliberately does not do.
Do this only with a rollback plan: console or out-of-band access, or an automatic rollback (for example
`nixos-rebuild test` followed by a timed `systemctl` or reboot that restores the previous generation, or a scheduled
`ip link set enp0s20f0u2 nomaster`). Prefer using a different management path.

## Namespace integration test

`tests/integration/run.sh` (root, Linux) runs two daemons in network namespaces over a veth underlay, for both
receivers, with pings, counters, optional iperf3, netem reorder/loss/duplication and a restart. See
`tests/integration/README.md`:

    sudo bash impl-c/tests/integration/run.sh build-c/linux/schcvxd

## Known limitations

- IPv4 underlay only.
- Single thread, kernel TAP and sockets; no DPDK.
- `dtag_rate` must be set high enough for the expected fragmented frame rate; otherwise sends are dropped
  (`sender_drop_dtag_admission_stopped`).
- The UDP checksum is verified in software on a raw socket; hosts whose NIC offloads transmit checksums on a
  virtual interface (veth) must disable it, as the integration test does.
- The ordered receiver drops reordered fragments, as the draft specifies.
