# SCHC VXLAN fragmentation: userspace C implementation specification

Status: build specification for the C implementation (owner Alexander Pelov, developed with Codex). DPDK is a later step and is out of scope here.
Pinned to: `draft/draft-vladyka-schc-vxlan-fragmentation-00.md` (moved there from `draft/draft-vladyka-schc-vxlan-fragmentation-00.md` in `cbc9eaf`) at commit `3e72a86`, and the interop plan in `interop/README.md`.

## 0. Authority and rules

- **The draft is the only protocol authority.** Section names below refer to the revised draft. If this document and the draft disagree, the draft wins: stop, record the disagreement in `impl-c/QUESTIONS.md`, and do not resolve it in code.
- **Independence.** Do not read, port or ask about the Rust implementation in `impl-rust/`. Shared material is only the draft, `draft/revised/wire-vectors.json`, `draft/revised/check_vectors.py` and `interop/`.
- **Unspecified behaviour is not permission to invent.** Every choice the draft leaves open is a named configuration value or a named constant, listed in `impl-c/DECISIONS.md` with its source.
- **No throughput claims.** This is a functional and interoperability implementation.
- **The wire codec is the only module that knows field widths and bit positions.** Nothing else builds or parses headers by hand, so a format change stays local.

## 1. Scope

In scope, version 1:

- The revised binding: SCHC Packet = 8-bit NoCompress C/D RuleID + 8-byte VXLAN header + Ethernet frame (no FCS).
- **Unfragmented sending** when the SCHC Packet fits, and **fragmentation** into K = 2..8 fragments otherwise (section "Sender Procedure").
- Both wire variants (compact and extended); the lab uses compact.
- Both receivers: the **ordered RFC 8724 receiver** (default) and the **reordering-tolerant receiver** (configuration switch).
- Retirement records, DTag allocation with startup quarantine Q, resource caps, all counters.
- IPv4 underlay without options. IPv6 underlay is milestone M3.
- Linux userspace data path, packaged for NixOS as a systemd service.

Non-goals, version 1: plain RFC 7348 VXLAN for unconfigured associations (the Linux kernel's own VXLAN covers that), SCHC compression rules, PMTU discovery, DPDK, multiple threads, authentication or encryption, and any throughput target.

## 2. Lab target

```
computer A ──1500── server 1 (VTEP) ══1280 IPv4══ server 2 (VTEP) ──1500── computer B
```

- Each server has an access port to its computer and an underlay port to the other server. A third interface or console for management is recommended.
- Lab profile from `interop/README.md`: compact variant, fragmentation RuleID 1, C/D RuleID 0, UDP destination port 60789, VNI 100, PMTU 1280, H = 20, max fragments 8.
- Numbers (draft "Examples"): an Ethernet frame of up to 1243 bytes is sent unfragmented; a 1514-byte frame becomes a 1523-byte SCHC Packet split into 1243 + 280.

## 3. Architecture

Two layers, as in the later DPDK design, so the core is reused unchanged.

```
impl-c/
  core/        portable C11, libc only: no sockets, no clock reads, no allocation after init
    include/schcvx/  crc32c.h  wire.h  tx.h  rx_ordered.h  rx_tolerant.h  retire.h  dtag.h  counters.h
    src/
  linux/       Linux daemon: TAP, sockets, epoll, config, counters output
  tests/       unit, vector conformance, receiver scenarios, fuzz, netns integration
  tools/       vectors_to_c.py (reads the shared JSON, emits a C header)
  nix/         package and NixOS module
  SPEC.md  DECISIONS.md  QUESTIONS.md  README.md
```

### 3.1 Data path (one process, one thread)

- **Access side:** one TAP device per peer association, enslaved to a Linux bridge together with the access port. The kernel bridge does MAC learning and flooding; flooding to several TAP devices is the ingress replication of the draft. Because frames are written to the TAP only after successful reassembly and authorization, the bridge learns only from complete frames, as the draft requires.
- **Underlay transmit:** one UDP socket per association, bound to the local address and a fixed source port, connected to the peer address and port 60789. All datagrams of a packet therefore share the five-tuple. Set DF with `IP_MTU_DISCOVER` = `IP_PMTUDISC_PROBE` (DF set, kernel PMTU cache ignored, the configured PMTU governs). The kernel computes the UDP checksum.
- **Underlay receive:** a raw socket (`AF_INET`, `SOCK_RAW`, `IPPROTO_UDP`) with a classic BPF filter for the local address and destination port 60789. The daemon parses the IP and UDP headers itself and **verifies the UDP checksum in software, rejecting zero**, because a normal UDP socket accepts a zero IPv4 checksum and hides the field. The transmit UDP socket gets a drop-all socket filter so its receive queue stays empty while its binding still prevents ICMP port-unreachable replies.
- **Event loop:** `epoll` over the TAP devices and the raw socket, `recvmmsg` and `sendmmsg` for batching, and an `epoll_wait` timeout equal to the next receiver or retirement deadline.
- **Time:** one `CLOCK_MONOTONIC` read per received batch, used as the arrival time of every datagram in that batch (draft: decisions follow receive timestamps even if processed later).

Verify `IP_PMTUDISC_PROBE` and the socket-filter behaviour on the NixOS kernel in milestone M2 before relying on them; record the result in `DECISIONS.md`.

## 4. Core library

All functions are deterministic and never block. Errors are negative `SCHCVX_E_*` values. Time is `uint64_t` nanoseconds of a monotonic clock passed in by the caller.

### 4.1 CRC-32C (`crc32c.h`)

- Parameters (draft "Reassembly Check Sequence"): polynomial 0x1edc6f41 (reflected 0x82f63b78), reflected input and output, initial value and final XOR 0xffffffff, check value 0xe3069283 for ASCII `123456789`, serialized most significant byte first.
- Coverage: the entire SCHC Packet, including the C/D RuleID and the VXLAN header.
- Incremental API (`init`, `update`, `final`) and a one-shot function. Portable table implementation is mandatory; a hardware path (SSE4.2 or ARMv8 CRC) is optional and must match the portable one in tests.

### 4.2 Wire codec (`wire.h`)

```c
enum schcvx_variant { SCHCVX_COMPACT, SCHCVX_EXTENDED };
/* compact:  RuleID 4 bits | DTag 32 bits | FCN 4 bits  -> h = 5 bytes, All-1 header 9 bytes
   extended: RuleID 8 bits | DTag 60 bits | FCN 4 bits  -> h = 9 bytes, All-1 header 13 bytes
   All fields most significant bit first; the RCS follows the FCN in All-1, before the payload. */

enum schcvx_kind { SCHCVX_UNFRAGMENTED, SCHCVX_REGULAR, SCHCVX_ALL1, SCHCVX_ABORT };

struct schcvx_msg {
    enum schcvx_kind kind;
    uint8_t  rule_id;
    uint64_t dtag;
    uint8_t  fcn;            /* 4 bits; 15 = All-1 */
    bool     first;          /* F bit set on a regular fragment */
    uint8_t  countdown;      /* c, 3 bits, regular fragments */
    uint32_t rcs;            /* All-1 only */
    const uint8_t *payload;  size_t payload_len;
};

int schcvx_classify(const struct schcvx_assoc_wire *a, const uint8_t *udp_payload, size_t len,
                    struct schcvx_msg *out);
size_t schcvx_encode_header(const struct schcvx_assoc_wire *a, const struct schcvx_msg *m, uint8_t *out);
```

Classification (draft "Binding and Profile", "Structural Validation and State Admission"), after the association is already selected:

1. First byte equals the configured C/D RuleID → `UNFRAGMENTED`; shorter than 23 bytes → malformed.
2. Otherwise decode with the association's variant. Shorter than h → malformed. RuleID not configured → unknown rule.
3. FCN 15 with exactly h bytes → `ABORT` (Sender-Abort). FCN 15 with h+1 to h+4 bytes → malformed. FCN 15 with at least h+5 bytes → `ALL1`.
4. FCN 6 or 7 → malformed. Regular fragment with fewer than h+1 bytes → malformed.
5. First-marked regular fragment (F = 1, FCN 8 to 14) with fewer than 9 payload bytes, or whose payload does not start with the C/D RuleID → malformed.

Configuration validation: compact fragmentation RuleIDs must not be the high nibble of the C/D RuleID; extended fragmentation RuleIDs must differ from it (draft "Fragment Format").

### 4.3 Sender (`tx.h`)

```c
struct schcvx_tx_plan { bool unfragmented; uint8_t k; uint32_t c; uint32_t last_len; };
int schcvx_tx_plan(const struct schcvx_tx_cfg *cfg, size_t frame_len, struct schcvx_tx_plan *out);
```

Draft "Sender Procedure", with P = PMTU, H = outer IP header length, E = frame length, A = All-1 header length (9 or 13):

- If E + 9 ≤ P − H − 8: send the SCHC Packet unfragmented.
- Otherwise C = P − H − 8 − A; require C ≥ 9; K = ceil((E + 9) / C); reject the frame if it is outside the configured length range or K is outside 2..8.
- Tiles are contiguous, each C bytes except the last (at most C). FCNs: first = `1` followed by K − 2, then `0` followed by K − 3 down to 0, then `1111` with the RCS.
- One DTag per packet; the same RuleID and DTag on every fragment; fragments sent in order. No Sender-Abort is sent.
- Bound S: when the time since the first emission of a packet reaches S, stop emitting it and count the drop (draft "DTag Reuse and Restart": the sender MUST stop emitting the packet when S is reached).
- Startup validation: the minimum configured PMTU must carry the maximum frame length in at most 8 fragments.

### 4.4 DTag allocation (`dtag.h`)

Draft "DTag Reuse and Restart":

- One counter per sending association and fragmentation RuleID, shared by all VNIs. Start value configurable (default 0).
- After process start the sender does not send fragmented packets until Q has elapsed (the last-emission time before restart is unknown). Unfragmented packets need no DTag and are not delayed.
- Wrap must not occur within Q: at startup, check `rate_limit × Q < 2^bits`; if the limit is exceeded at run time, stop admission and count.

### 4.5 Receivers

Both receivers share: association and checksum validation before any state access; resource caps (entries, retained bytes per peer and globally, orphan budget, maximum retained packet bytes = max frame length + 9); a reservation of a retirement slot at admission; retirement for R after completion, abort or timeout; and a retired identifier check before admission. Capacity is never recovered by evicting a protected retirement record.

**Ordered receiver (`rx_ordered.h`)**, draft "Ordered Receiver": RFC 8724 No-ACK behaviour. Entry keyed by (association, RuleID, DTag). Append regular payloads in reception order; reset the inactivity timer I on each regular fragment; on All-1 append and check the RCS, deliver or drop, and conclude; on timer expiry drop. No reordering and no duplicate suppression: a duplicate regular fragment leads to an integrity failure.

**Tolerant receiver (`rx_tolerant.h`)**, draft "Optional Reordering-Tolerant Receiver": any valid fragment opens an entry (an orphan until the first fragment arrives); store regular fragments by countdown and All-1 separately; identical duplicates ignored (identity = full FCN and payload, plus RCS for All-1); conflicting duplicates, a second first marker with another countdown, a first marker colliding with a stored position, or positions outside n..0 abort the entry; equal regular tile lengths and a final tile no longer; completion needs the first fragment, all positions and All-1; assemble in countdown order, check the RCS once, deliver once; a complete packet with a bad RCS is dropped at once. Fixed deadline Delta from the first admitted fragment; arrivals at or after it are late; duplicates do not extend it; an optional inactivity timer may expire sooner.

**Both receivers before delivery:** validate the VXLAN header (I flag set; reserved bits ignored as in RFC 7348), check the VNI against the association, then hand the Ethernet frame to the TAP device. Unfragmented SCHC Packets skip reassembly and retirement but get the same checks. Sender-Abort discards and retires a matching active entry without a reply.

### 4.6 Counters (`counters.h`)

Per association: packets sent unfragmented, packets sent fragmented, sender drops by reason (length, K range, S exceeded, DTag admission stopped, quarantine), packets delivered, expired, late, duplicates, integrity (RCS) failures, malformed (including zero or bad UDP checksum), unknown rule, peer or VNI admission failures, table exhaustion, Sender-Aborts received.

## 5. Configuration

Plain-text `key = value` file, one `[association NAME]` section each, parsed without external libraries. The daemon refuses to start if any required value is missing; there are no built-in defaults for timers (the draft gives none).

| Key | Lab value | Notes |
| --- | --- | --- |
| `local_ip`, `peer_ip` | per server | IPv4 in version 1 |
| `udp_port` | 60789 | destination port, both directions |
| `src_port` | 60789 | fixed source port of the transmit socket |
| `variant` | compact | compact or extended |
| `fr_rule_id`, `cd_rule_id` | 1, 0 | validated prefix-free |
| `vnis` | 100 | admitted VNIs |
| `tap` | schc0 | TAP device name |
| `pmtu`, `h_ip` | 1280, 20 | |
| `max_frame_len` | 1518 | allows one VLAN tag |
| `max_fragments` | 8 | |
| `receiver` | ordered | ordered or tolerant |
| `S`, `B`, `I`, `Delta`, `R`, `Q` | 1 ms, 10 ms, 10 ms, 20 ms, 100 ms, 200 ms | see `interop/README.md`; startup checks R ≥ S + B and Q > B + D + R |
| `max_entries`, `max_bytes`, `orphan_budget` | 1024, 4 MiB, 256 | per association; global limits in `[global]` |
| `dtag_start` | 0 | |
| `counters_file`, `counters_interval` | /run/schcvx/counters, 1 s | also written on SIGUSR1 |

## 6. NixOS packaging

- `impl-c/nix/package.nix` builds the daemon with Meson and Ninja.
- `impl-c/nix/module.nix` provides `services.schcvx`: writes the configuration file, creates the bridge from the access port and the TAP device, and runs the daemon as a systemd service with `CAP_NET_ADMIN` and `CAP_NET_RAW` only (no root shell).
- A `flake.nix` at `impl-c/` exposes the package, the module and a development shell.

## 7. Verification

1. **Unit tests (native, also on macOS):** CRC-32C check value; codec round trips for both variants; every malformed case of section 4.2; sender boundaries for (P, H) = (1500, 20), (1500, 40) and (1280, 20): largest unfragmented frame fills P exactly, one byte more gives two fragments, every datagram fits P.
2. **Shared vector conformance (mandatory):** `tools/vectors_to_c.py` turns `draft/revised/wire-vectors.json` into a C header; the encoder reproduces every `udp_payload_hex` byte for byte and the decoder accepts them. `check_vectors.py` must still pass.
3. **Receiver scenarios:** for each receiver, scripted event sequences with explicit timestamps: in order, all permutations for K = 2..8 (tolerant), every single loss, identical and conflicting duplicates, RCS-only conflict, bad RCS, deadline and inactivity expiry including equality, late arrivals after retirement, Sender-Abort, unauthorized VNI, unknown rule, cap exhaustion, and the ordered receiver's expected loss on reordering.
4. **Fuzzing:** libFuzzer targets for `schcvx_classify` and for both receivers fed with random event streams, under AddressSanitizer and UBSan.
5. **Integration (Linux network namespaces, no hardware):** two namespaces as VTEPs joined by a veth pair with MTU 1280, one host namespace behind each; then the interop test matrix of `interop/README.md` (ping sizes 1201, 1202 and 1472, iperf3, netem reorder, loss and duplicate, restart).
6. **Lab:** the same matrix on the two NixOS servers, then against the Rust implementation in both directions; results in `interop/results/`.

## 8. Workpackages

Verify commands assume Meson with a `build/` directory. Core packages run natively on the macOS host; Linux packages need a Linux machine or VM (see Q2).

| WP | Name | Depends | Owns | Verify |
| --- | --- | --- | --- | --- |
| WP01 | skeleton | — | `impl-c/meson.build`, `impl-c/core/meson.build`, `impl-c/tests/meson.build` | `meson setup build impl-c && meson test -C build` |
| WP02 | crc32c | WP01 | `impl-c/core/**/crc32c*`, `impl-c/tests/test_crc32c.c` | `meson test -C build crc32c` |
| WP03 | wire-codec | WP02 | `impl-c/core/**/wire*`, `impl-c/tests/test_wire.c` | `meson test -C build wire` |
| WP04 | vectors | WP03 | `impl-c/tools/vectors_to_c.py`, `impl-c/tests/test_vectors.c` | `meson test -C build vectors` |
| WP05 | tx-dtag | WP03 | `impl-c/core/**/tx*`, `impl-c/core/**/dtag*`, `impl-c/tests/test_tx.c` | `meson test -C build tx` |
| WP06 | rx-ordered | WP03 | `impl-c/core/**/rx_ordered*`, `impl-c/core/**/retire*`, `impl-c/tests/test_rx_ordered.c` | `meson test -C build rx_ordered` |
| WP07 | rx-tolerant | WP06 | `impl-c/core/**/rx_tolerant*`, `impl-c/tests/test_rx_tolerant.c` | `meson test -C build rx_tolerant` |
| WP08 | fuzz | WP07 | `impl-c/tests/fuzz/**` | `meson test -C build fuzz-smoke` |
| WP09 | linux-daemon | WP05, WP07 | `impl-c/linux/**` | `meson test -C build daemon` (Linux) |
| WP10 | netns-integration | WP09 | `impl-c/tests/integration/**` | `sudo impl-c/tests/integration/run.sh` (Linux) |
| WP11 | nixos | WP09 | `impl-c/nix/**`, `impl-c/flake.nix` | `nix build ./impl-c` (Linux) |
| WP12 | docs | WP09 | `impl-c/README.md` | `bash impl-c/tests/check_docs.sh` |

WP05 and WP06 are independent and may run in parallel. WP06, WP07 and WP09 are rung 3 and `review_required`. Pre-seed the WP06 and WP07 scenario tests as locked tests so they cannot be weakened.

Milestones: **M1** WP01–WP08 (core, native). **M2** WP09–WP12 (daemon, namespaces, NixOS, IPv4). **M3** IPv6 underlay. **M4** lab interop with Rust.

## 9. Questions before sealing

- **Q1. Receiver default.** The ordered receiver is the draft's main profile, but it loses frames on any reordering. Default: ordered, with the tolerant receiver enabled in the lab for tests 5 and 6.
- **Q2. Linux gate.** WP09–WP11 need Linux. Options: a NixOS VM or one of the servers as the build and test host. Default: one server, reached over the management interface.
- **Q3. Timer values.** S, B, I, Delta, R and Q above are lab choices, not draft values. Confirm them with Andrii.
- **Q4. Ordered receiver retirement.** The draft subjects terminal identifiers of the ordered receiver to retirement admission; RFC 8724 itself does not. Default: apply retirement to both receivers, as the draft says.
- **Q5. Single source port.** One fixed source port per association gives the five-tuple stability the draft asks for but no ECMP entropy. Default: acceptable for the two-server lab.
