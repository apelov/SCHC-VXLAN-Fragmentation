# Handoff: SCHC VXLAN fragmentation (state at the end of the 2026-11-15 hackathon)

Read this first, then `AGENTS.md` (operating rules), `.ai/session.md` (running notes), and the files named below. Treat every statement here as "true when written"; verify paths and live state before acting.

## 1. Goal and status

- **Goal:** an IETF Internet-Draft for the SCHC WG, `draft-vladyka-schc-vxlan-fragmentation-00` (authors Andrii Vladyka, Alexander Pelov; Standards Track), plus two independent interoperating implementations.
- **Protocol (current design, "option C"):** a SCHC No-ACK profile over the whole VXLAN payload. Every frame on a configured peer association is carried as a SCHC Packet = 8-bit NoCompress C/D RuleID + 8-byte VXLAN header + Ethernet frame. Sent unfragmented if `E + 9 <= P - H - 8`, otherwise as K = 2..8 fragments (4-bit FCN `Fccc`: first marker + countdown, All-1 last with a CRC-32C RCS). Compact variant: 4-bit RuleID, 32-bit DTag, 4-bit FCN; extended: 8/60/4. Ordered (RFC 8724) receiver is the main profile; a reordering-tolerant receiver is an optional extension.
- **Status:** the draft builds; the C implementation (userspace, Linux) and Andrii's Rust implementation interoperate in the lab, computer to computer over a 1280-byte underlay, fragmented and unfragmented, ~530–730 Mbit/s TCP. See `interop/results/`.

## 2. Repository map

| Path | What |
| --- | --- |
| `draft/draft-vladyka-schc-vxlan-fragmentation-00.md` | **The current draft** (kramdown-rfc). Build: `bash draft/build.sh` (outputs in `draft/out/`, git-ignored). |
| `draft/revised/wire-vectors.json`, `draft/revised/check_vectors.py` | Shared wire vectors and the executable checker: the interop contract. `python3 draft/revised/check_vectors.py` must print PASS. |
| `draft/AlternativeDesign/` | The earlier indexed "Out-of-Order No-ACK" design (14-byte header), kept for reference; Andrii edited it (count 1) and the old Python model in `src/schcvx` was not updated. |
| `docs/design/` | `DESIGN.md` (frozen input of the original pipeline, old design), `REVISED-C-PROPOSAL.md`, `WIRE-OPTIONS.md` (A/B/C analysis). Historical. |
| `docs/impl/DPDK-IMPLEMENTATION-SPEC.md` | DPDK spec for the **old** format; marked superseded; rewrite before any DPDK work. |
| `impl-c/` | The C implementation: `SPEC.md` (build spec), `DECISIONS.md` (every non-specified choice), `README.md` (operator guide, config reference, counters), `core/` (portable C11 library), `linux/` (daemon `schcvxd`), `tests/` (unit, vectors, scale, fuzz smoke, namespace integration), `nix/` + `flake.nix`. |
| `interop/README.md` | Interop plan: roles, independence rules, lab profile, topology, test matrix. |
| `interop/results/` | Run reports (`2026-11-15-c-to-rust-step1.md` + `.pcap`, `2026-11-15-c-to-rust-step2-and-throughput.md`). `captures/` is untracked. |
| `interop/wireshark/schcvx.lua` | Wireshark dissector written from the draft only (reassembly, RCS check, VXLAN hand-off). |
| `agentwork/`, `.ai/`, `AGENTS.md` | Multi-agent pipeline template and project notes; the pipeline was used for the first draft only (see analysis). |
| `src/schcvx/`, `tests/`, `tools/` | Python reference model of the **old** indexed design; 120 tests; not the current protocol. |

Branches: `main` is at `80bcb0e` (draft + plans); **`impl-c-dev`** carries everything since (C implementation, fixes, dissector, results, the draft promotion to `draft/` by the user). Merging into `main` is the authors' decision.

## 3. Roles and independence

- Rust implementation: Andrii Vladyka (developed with Claude), on lab server 2. Never read, port or ask about `impl-rust/` from the C side, and vice versa.
- C implementation: developed by delegated coding agents (Codex for WP01–WP09b, then Claude Sonnet sub-agents); the orchestrating agent writes specs/prompts and validates, but does not write implementation code itself. The user validates with Mistral Vibe.
- Shared material only: the draft, the wire vectors and checker, `interop/`.
- Every change of protocol behaviour goes into the draft first.

## 4. Lab

```
computer A 192.168.44.2 ─ server 1 (C) ══ 1280-byte IPv4 underlay ══ server 2 (Rust) ─ computer B 192.168.44.3
                                 192.168.11.2              192.168.11.1
```

- **Server 1** (NixOS 25.11, sudo for `apelov`): `ssh apelov@192.168.44.1` (via the USB link to the user's computer) or `ssh apelov@31.130.239.187` (public, over `enp1s0`; ED25519 host key `SHA256:0j2kCR59WZMTl/3E7VZAEn4tMbjUICzaBPEm2QomUks`). Server 2 is Andrii's; no access from here.
- Server 1 interfaces: `enp0s20f0u2` access port (carries the management SSH session!), `enp3s0` underlay (MTU 1280), `enp1s0` uplink (public IP, default route; never bridge it).
- **Live runtime state on server 1** (none of it survives a reboot): transient unit `schcvx-lab` running `~/schcvx-git/build-it/linux/schcvxd -c ~/schcvx-lab.conf` (`receiver = ordered`); bridge `br-schc` = `enp0s20f0u2` + `schc0`, holding 192.168.44.1/24; NetworkManager does not manage `enp0s20f0u2`. Undo: `/tmp/schc-bridge-revert.sh`.
- Restarting the daemon removes `schc0` from the bridge (the TAP is recreated). After `sudo systemctl stop schcvx-lab` and `sudo systemd-run --unit schcvx-lab -p RuntimeDirectory=schcvx-lab --collect <binary> -c ~/schcvx-lab.conf`, wait for `schc0` and run `sudo ip link set schc0 master br-schc && sudo ip link set schc0 up`.
- Counters: `sudo kill -USR1 $(systemctl show -p MainPID --value schcvx-lab); sudo cat /run/schcvx-lab/counters`. Logs: `journalctl -u schcvx-lab`.
- Toolchain on server 1: `nix shell github:NixOS/nixpkgs/8fd9daa3db09ced9700431c5b7ad0e8ba199b575#{gcc,meson,ninja,pkg-config,python3}` (same pin as the system). The working copy there is `~/schcvx-git` (synced with rsync, includes `.git`).
- Any change that moves 192.168.44.1 or touches `enp0s20f0u2` needs the user's explicit approval and a revert timer.

## 5. How to verify

| Check | Command | Where |
| --- | --- | --- |
| Wire vectors | `python3 draft/revised/check_vectors.py` | anywhere |
| Unit + vectors + scale + fuzz smoke + docs | `meson setup --wipe build-c impl-c && meson test -C build-c` (15 tests) | Mac and Linux |
| Sanitizers | `meson setup build-asan impl-c -Db_sanitize=address,undefined && meson test -C build-asan` | Mac |
| GCC + Linux daemon | same Meson commands with the Nix toolchain; `ninja -C build-c -k 0 2>&1 \| grep -c -E 'error\|warning:'` must print 0 | server 1 |
| Namespace end-to-end (C to C) | `sudo -E env PATH=$PATH bash impl-c/tests/integration/run.sh build-c/linux/schcvxd` inside `nix shell ...#{iproute2,iputils,iperf3,procps,ethtool}` | server 1 (root) |
| Nix package / module | `nix build ./impl-c` inside a git checkout (must be a git flake: the build reads `draft/revised/wire-vectors.json`) | server 1 |
| Captures | `tshark -2 -X lua_script:interop/wireshark/schcvx.lua -r file.pcap -Y schcvx` (the dissector is also installed in the user's Wireshark personal plugins) | Mac |

## 6. Gotchas learned the hard way

- veth TX checksum offload leaves partial UDP checksums; the daemon verifies checksums in software on a raw socket and drops them. The namespace test therefore runs `ethtool -K <veth> tx off`. Physical NICs are fine.
- GCC 14 `-Wmisleading-indentation` rejects several statements on one line after an unbraced `if`/`for`; Apple clang does not. Always build on Linux.
- Sandboxed coding agents reject `rm -rf`; use `meson setup --wipe`. The `gemini` CLI fails on this account (eligibility); `agy --model gemini-3.8-flash` works.
- `dtag_rate` must exceed the fragmented packet rate (lab: 1,000,000/s, burst 10,000); `max_retired` must be at least packet rate x R (lab: 65,536 for R = 100 ms).
- The raw receive socket asks for 16 MiB (`SO_RCVBUFFORCE`); the log shows the doubled value 33554432.
- A connected UDP socket fails the next send after ICMP port-unreachable (peer not listening yet); the daemon retries once.
- Always record daemon CPU time with throughput runs: a CPU-bound receiver with a large socket buffer shows as low throughput with zero loss.
- Agent pipeline (`agentwork/`): local run state lives outside git (`~/.local/state/agentwork/`), so a second machine sees different state.

## 7. Open items

1. **Draft sentence awaiting the authors** ("Structural Validation and State Admission"): "A datagram whose payload alone exceeds the maximum tile bytes is malformed: it is discarded without modifying, terminating or retiring admitted state. A fragment that is individually valid but would make its entry exceed the maximum retained packet bytes conflicts with that entry." The C code already behaves this way.
2. **Reordering performance:** rerun the netem matrix in `interop/results/2026-11-15-c-to-rust-step2-and-throughput.md` with tolerant receivers on both sides and read the counters.
3. Retirement for the ordered receiver: the draft requires it, RFC 8724 does not; decide in the draft.
4. Persistent deployment on server 1 (NixOS module `services.schcvx`, bridge) instead of the runtime setup; the module renders correctly but has never been activated (DynamicUser with only CAP_NET_ADMIN/CAP_NET_RAW is untested).
5. IPv6 underlay (milestone M3), DPDK (rewrite `docs/impl/DPDK-IMPLEMENTATION-SPEC.md` for the current format; RTL8125 DPDK support unverified).
6. Draft hygiene: run idnits; verify the statements about draft-ietf-schc-protocol-numbers (taken from a summary); RFC 3720 reference for CRC-32C not added (unverified).
7. Old Python model (`src/schcvx`) still rejects count 1 after Andrii's edit to the alternative design; only relevant if that design is revived.
8. Lab profile: add `dtag_rate`/`dtag_burst` guidance to `interop/README.md` for both implementations.
