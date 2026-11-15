# SCHC VXLAN Fragmentation — DPDK Implementation Specification

Status: proposed input for a new pipeline generation (use as the frozen design of the implementation repository or subtree).
Pinned to: draft `draft-vladyka-schc-vxlan-fragmentation-00` at repository commit `8485aac`, and the Python reference model in `src/schcvx/` at the same commit.

## 0. Authority and rules

- **Normative sources, in order:** (1) the draft sections in `draft/sections/` (2) `docs/design/DESIGN.md` (3) the reference model and its tests in `src/schcvx/`, `tests/`, `draft/generated/examples.md`. If this document and the draft disagree, the draft wins. Report the disagreement; do not resolve it silently.
- **Open items stay open.** Every "Open Issue" in the draft (deadline equality rule, Frame ID width, restart mechanism, header versioning, shared SCHC UDP port, numeric defaults for Δ and L) is a configuration value or an isolated function in the implementation. Never hard-code an answer; document the provisional choice in `docs/impl/DECISIONS.md`.
- **Unspecified behavior is not permission to invent.** If the draft and the model are silent, stop the affected work package and record a question.
- **No throughput claims** anywhere until measured on hardware (section 11).
- Wire format changes are expected (the human is considering reworking the draft toward an RFC 8724 No-ACK Profile). Therefore the wire codec (`schcvx_wire.*`) is the only module allowed to know field offsets and sizes. Nothing else may parse or build fragment headers by hand.

## 1. Goal and scope

Implement the draft's sender, receiver and VXLAN integration in C on DPDK, runnable and testable in Docker, with a minimal open-source-compatible data path.

In scope, version 1:

- IPv4 and IPv6 outer headers without extension headers (IPv6 is milestone M3).
- Statically provisioned unicast VTEP peers; BUM traffic by ingress replication to those peers.
- Standard RFC 7348 VXLAN for frames that fit; the fragment format for frames that do not.
- Per-peer, per-direction configured PMTU. Configured maximum fragment count (default 2).
- Bounded receiver state, fixed first-arrival deadline Δ, retirement records for L, all counters of the draft.
- Docker-based functional and interoperability tests; a Linux hardware benchmark procedure (run later, results pending measurement).

Non-goals, version 1: dynamic PMTU discovery (RFC 8899), restart-safe Frame ID mechanism, encryption or authentication, multicast VXLAN, any inter-frame ordering guarantee, header compression, kernel or VPP integration (see section 12, deferred).

## 2. Decisions (FIXED for this generation)

| ID | Decision |
| --- | --- |
| D1 | Language C11. Build system Meson + Ninja. DPDK is a pkg-config dependency (`libdpdk`), pinned to one LTS release in the Dockerfile. |
| D2 | Two layers: a **portable core library** (`impl/core/`, C11, libc only, no DPDK) and a **DPDK application** (`impl/app/`). All protocol logic lives in the core. The app only moves mbufs. |
| D3 | The core is verified natively and against the Python reference model with golden vectors (differential testing). The app is verified in Docker. |
| D4 | The forwarder is a small purpose-built DPDK VTEP (bridge + VXLAN + extension). Using VPP or OVS-DPDK is deferred (section 12). |
| D5 | One owner per reassembly entry: each lcore owns private tables; fragments of one frame reach the same lcore because they share the outer five-tuple (the draft requires the same source UDP port) and RSS hashes IP + UDP ports. No locks on the data path. |
| D6 | Reassembly copies each received slice into a dedicated reassembly buffer pool and frees the RX mbuf immediately. This keeps RX mempool sizing independent of the receiver caps. Zero-copy reassembly is a later optimisation. |
| D7 | Time is `uint64` ticks from a monotonic clock (`rte_rdtsc()` in the app, an injected counter in tests). The core never reads a clock itself. |
| D8 | Expiry and retirement are FIFO queues, not heaps: Δ and L are constants per receiver, so both queues are ordered by insertion time. A sweep pops from the head while `now >= expiry`. |
| D9 | The deadline-equality rule is a build-time constant `SCHCVX_EXPIRE_AT_EQUALITY = 1` (expired when `t >= expiry`), matching the reference model, and is covered by a test. |
| D10 | Frame IDs: one `uint64` counter per (peer association, VNI), incremented once per fragmented frame. Initial value is configurable; the default is the current wall-clock time in nanoseconds. This is a provisional choice, **not** a restart-safety mechanism (draft Open Issue). |

## 3. Repository layout (implementation)

```
impl/
  meson.build
  core/                     portable library, no DPDK
    include/schcvx/*.h      public headers
    src/*.c
  app/                      DPDK application (Linux only)
    src/*.c
  tests/
    core/                   native unit and differential tests
    vectors/                golden vectors (generated, committed)
    fuzz/                   fuzz targets
    integration/            docker compose + scripts
  docker/
    Dockerfile
    compose.yaml
tools/
  gen_vectors.py            emits golden vectors from src/schcvx (Python oracle)
docs/impl/
  DPDK-IMPLEMENTATION-SPEC.md   this file
  DECISIONS.md                  provisional choices made while implementing
```

## 4. Core library specification

All functions are deterministic, never allocate after `init`, and never block. Error returns are negative `SCHCVX_E_*` codes; successful functions return 0 unless noted.

### 4.1 `schcvx_crc32c`

```c
void     schcvx_crc32c_init(uint32_t *state);                       /* state = 0xffffffff */
void     schcvx_crc32c_update(uint32_t *state, const void *p, size_t n);
uint32_t schcvx_crc32c_final(uint32_t state);                       /* state ^ 0xffffffff */
uint32_t schcvx_crc32c(const void *p, size_t n);                    /* one-shot */
```

- Parameters per the draft: Castagnoli polynomial 0x1edc6f41 (reflected 0x82f63b78), reflected input and output, initial value and final XOR 0xffffffff, check value 0xe3069283 for ASCII `123456789`.
- Incremental form is required so the app can checksum a chained mbuf without copying.
- Implementations: portable slice-by-8 (mandatory, used by the tests) and an optional hardware path (x86 SSE4.2 `crc32` instruction, ARMv8 CRC32C). A DPDK hash-CRC helper may be used only after a test proves it equals the portable result including init and final XOR handling; do not assume.
- Serialisation of the trailer is big-endian. A helper `schcvx_put_be32` belongs in the wire module.

### 4.2 `schcvx_wire` — the only module that knows the header layout

```c
#define SCHCVX_HDR_LEN 14
#define SCHCVX_RCS_LEN 4

struct schcvx_frag {
    uint8_t  rule_id;
    uint32_t vni;            /* 24 bits */
    uint64_t frame_id;
    uint8_t  index;
    uint8_t  count;
    const uint8_t *slice;    /* points into the caller's datagram */
    uint16_t slice_len;      /* excludes the RCS */
    bool     has_rcs;        /* true iff index == count - 1 */
    uint32_t rcs;
};

int    schcvx_decode(const uint8_t *udp_payload, size_t len, struct schcvx_frag *out);
size_t schcvx_encode_header(const struct schcvx_frag *f, uint8_t out[SCHCVX_HDR_LEN]);
```

Layout (provisional, from the draft): byte 0 RuleID, bytes 1–3 VNI, bytes 4–11 Frame ID, byte 12 index, byte 13 count, then the slice; the final fragment (index == count − 1) ends with a 4-byte RCS. Network byte order. `decode` returns `SCHCVX_E_MALFORMED` for: payload shorter than the header, count < 2, index ≥ count, empty slice, final fragment too short to hold at least one slice byte plus the RCS. It does not apply policy (maximum count, maximum slice length); that is the receiver's job.

### 4.3 `schcvx_tx` — sender planning

```c
struct schcvx_tx_cfg {
    uint32_t pmtu;               /* underlay IP PMTU for this peer and direction */
    uint32_t h_ip;               /* actual outer IP header length incl. options/extensions */
    uint32_t min_frame_len, max_frame_len;
    uint8_t  max_fragments;      /* >= 2 */
};
struct schcvx_tx_plan {
    enum { SCHCVX_TX_FITS, SCHCVX_TX_FRAGMENT, SCHCVX_TX_DROP } verdict;
    enum schcvx_drop_reason reason;   /* valid when DROP */
    uint16_t n;                       /* fragments */
    uint32_t c;                       /* slice budget */
    uint32_t last_slice_len;
};
int schcvx_tx_plan(const struct schcvx_tx_cfg *cfg, uint32_t frame_len, struct schcvx_tx_plan *out);
int schcvx_tx_validate_cfg(const struct schcvx_tx_cfg *cfg);   /* min PMTU carries max frame in max fragments */
```

- Ordinary VXLAN frame budget `pmtu - h_ip - 8 - 8`; frame fits if `frame_len <=` budget → `FITS`.
- Slice budget `C = pmtu - h_ip - 8 - 14 - 4`. `C <= 0` → `DROP`. `N = ceil(frame_len / C)`. Frame length outside the configured range or `N` outside 2..max_fragments → `DROP`.
- Slices are contiguous, ascending index, each `C` bytes except the last (remainder). The plan carries no frame data; the caller slices.
- Per-frame budget snapshot: the caller copies `cfg` once per frame; the function reads only its argument.
- `schcvx_tx_validate_cfg` rejects configurations where `max_frame_len > max_fragments * C`.

### 4.4 `schcvx_rx` — receiver state machine

```c
struct schcvx_rx_cfg {
    uint64_t ticks_per_sec;
    uint64_t delta_ticks;               /* Δ */
    uint64_t retire_ticks;              /* L */
    uint32_t max_frame_len;
    uint16_t max_slice_len;             /* local bound, <= max_frame_len */
    uint8_t  max_count;                 /* >= 2 */
    uint32_t max_entries_per_peer, max_entries_global;
    uint64_t max_bytes_per_peer, max_bytes_global;
    uint32_t n_assoc;                   /* peer associations */
    const struct schcvx_assoc *assoc;   /* id, expected rule_id, admitted VNIs */
};
struct schcvx_mem { void *(*alloc)(size_t); void (*free)(void *); };

int  schcvx_rx_init(struct schcvx_rx **rx, const struct schcvx_rx_cfg *cfg, const struct schcvx_mem *mem);
enum schcvx_rx_status schcvx_rx_input(struct schcvx_rx *rx, uint32_t assoc_id,
        const struct schcvx_frag *f, bool udp_cksum_ok, uint64_t now, struct schcvx_rx_out *out);
void schcvx_rx_sweep(struct schcvx_rx *rx, uint64_t now);          /* bounded work per call */
void schcvx_rx_release(struct schcvx_rx *rx, struct schcvx_rx_out *out);
void schcvx_rx_counters(const struct schcvx_rx *rx, struct schcvx_counters *out);
```

Status values: `PENDING`, `DELIVERED` (frame pointer and length in `out`, valid until `release`), `DUPLICATE_IGNORED`, `RETIRED_DROP`, `ABORTED` (reason), `MALFORMED`, `PEER_REJECTED`, `TABLE_FULL`.

Counters (names fixed, identical to the draft and the model): `completed`, `expired`, `late`, `duplicate`, `integrity_failure`, `malformed`, `peer_admission_failure`, `table_exhaustion`.

Behaviour, normative (draft section "Receiver Procedure" and `src/schcvx/receiver.py`):

1. **Validation before allocation:** `udp_cksum_ok` false → `MALFORMED`; association and VNI authorised and `rule_id` equal to the association's value, else `PEER_REJECTED` (counted as `peer_admission_failure`); count ≤ `max_count`; `slice_len ≤ max_slice_len`, else `MALFORMED`.
2. **Key** = (association, VNI, RuleID, Frame ID). The RuleID is fixed per association; it does not distinguish entries within one.
3. **Retired ID** → `RETIRED_DROP`, counter `late`.
4. **Expiry decided by arrival tick `now` alone.** An existing entry with `now >= expiry` is retired as expired (`expired` +1, once) and the arriving fragment is counted `late`. A sweep does the same for entries not touched again.
5. **Admission:** the first admissible fragment of any index opens an entry with `expiry = now + delta_ticks`, never extended. Reserve a retirement slot at admission. If a cap on entries or retained bytes would be exceeded → `TABLE_FULL`, counter `table_exhaustion`; an existing entry stays pending until its deadline.
6. **Consistency:** count equal across the entry; nonfinal slices equal length; final slice no longer than a nonfinal one; retained bytes ≤ `max_frame_len`. Violation → abort, retire, `integrity_failure`.
7. **Duplicates:** identical = same index, same slice bytes and, for the final fragment, the same RCS → `DUPLICATE_IGNORED` (counter `duplicate`, expiry unchanged). Same index but different slice or RCS → conflicting duplicate: abort, retire, `integrity_failure`.
8. **Completion** when the bitmap is full: total length within the configured range; assemble in index order; CRC-32C over the assembled frame equals the RCS; deliver once. Eligibility is decided by the arrival tick of the completing fragment (`now < expiry`). Bad length or RCS → abort, retire, `integrity_failure`.
9. **Retirement** on completion, abort or timeout: free buffers, keep the ID in the retirement FIFO until `now >= retire_time + retire_ticks`. A record is never evicted early; capacity is reserved at admission.

Data structure guidance (not normative): open-addressing hash on the key; entries in a preallocated array; slice storage as fixed `max_slice_len` buckets from one preallocated block; two intrusive FIFOs (entries by `first_rx`, retirement records by retire time). For `max_count = 2` the bitmap is a two-bit mask; use `uint64_t` bitmaps up to 64 fragments and a byte array above that.

## 5. DPDK application specification

### 5.1 Roles and ports

- **Access port(s):** carry plain Ethernet frames of the bridged VNIs (a VLAN or an untagged port mapped to a VNI by configuration).
- **Underlay port:** carries VXLAN and extension datagrams to and from peers.
- One RX/TX queue pair per lcore on every port. RSS on the underlay port hashes outer IP addresses and UDP ports (`RTE_ETH_RSS_IP | RTE_ETH_RSS_UDP`). Startup fails with a clear error if the PMD cannot provide this; do not silently degrade.

### 5.2 Pipeline (run to completion, per lcore)

Transmit (access port → underlay):

1. Receive a burst; read the arrival tick once per burst.
2. Bridge lookup keyed by (VNI, destination MAC); unknown or broadcast destination → ingress replication to every provisioned peer of that VNI (and, for frames from the underlay, to local ports as usual). Each copy uses that peer's own configuration.
3. For each peer: call `schcvx_tx_plan` with that peer's PMTU, `h_ip` and limits.
   - `FITS` → standard RFC 7348 VXLAN: outer Ethernet, IP, UDP (destination 4789 or the configured VXLAN port), VXLAN header with the VNI; keep the existing checksum and TSO offload behaviour of the NIC where supported.
   - `FRAGMENT` → build N packets (section 5.3).
   - `DROP` → free, count by reason. Peers without the extension enabled are treated as `DROP` for oversized frames.
4. Send with `rte_eth_tx_burst`; free unsent mbufs and count them.

Receive (underlay → access port):

1. Parse outer Ethernet, IP, UDP with `rte_net_get_ptype` or equivalent. Ignore anything that is not UDP to a configured local address.
2. Identify the peer association from the outer source address and the local endpoint, independently of the UDP source port. Unknown source → drop and count `peer_admission_failure`.
3. UDP destination port selects the format: the VXLAN port → decapsulate; the extension port → `schcvx_decode` then `schcvx_rx_input`.
4. UDP checksum: read the RX offload flags (`RTE_MBUF_F_RX_L4_CKSUM_*`); if the NIC did not validate, validate in software. Extension datagrams with a zero or incorrect checksum are dropped (draft requirement, over IPv4 as well as IPv6); pass `udp_cksum_ok = false`.
5. On `DELIVERED`: learn the source MAC **only now** (never from a fragment), then bridge-forward the frame to the access port or onward.
6. Call `schcvx_rx_sweep` once per loop iteration with the burst's tick; the sweep is bounded.

### 5.3 Fragment construction (sender)

For fragment `i` of `N` build a chain of mbufs: `[outer Ethernet+IP+UDP+fragment header (new direct mbuf)] → [indirect mbuf referencing the slice of the original frame]`, and for the final fragment `→ [4-byte RCS trailer mbuf]`.

- Indirect mbufs are attached to the original frame's mbuf (`rte_pktmbuf_attach` or `rte_pktmbuf_clone`, then `rte_pktmbuf_adj`/`rte_pktmbuf_trim` to the slice range). The original frame is freed by reference counting when the last fragment is sent. No slice is copied.
- The RCS is computed once per frame over the original frame with the incremental CRC API (it may span segments) and written big-endian.
- Outer IPv4: set DF; IPv6: no source fragmentation. Every outer IP datagram length must be ≤ the configured PMTU (assert in debug builds).
- UDP checksum is mandatory on every extension datagram: request NIC offload (`RTE_MBUF_F_TX_UDP_CKSUM`, `RTE_MBUF_F_TX_IPV4`/`IPV6`, `l2_len`/`l3_len`) and write the pseudo-header checksum into the UDP field as the offload requires. If a port lacks offload or lacks multi-segment support, linearise and compute the checksum in software. Do not fall back silently: log once and count.
- All fragments of a frame use the same outer five-tuple; the UDP source port is derived from a hash of the inner Ethernet headers, as for ordinary VXLAN (RFC 7348 §5), computed once per frame.
- Emit fragments in ascending index order, back to back, into the same TX burst when possible.
- Frame ID: take the next value of the (peer, VNI) counter once per fragmented frame (D10).

### 5.4 Memory

- Mempools: RX mbuf pool; direct header pool; indirect pool; reassembly buffer pool (D6). Sizes are computed at startup from configuration by documented formulas, and startup fails if a pool cannot be created.
- Reassembly bytes are bounded by `max_bytes_per_peer` and `max_bytes_global`. Provide a worked sizing formula in the operator README: with link rate `R` and deadline Δ, worst-case held bytes ≈ `R × Δ / 8` (for example 1.25 MB at 10 Gbit/s and Δ = 1 ms). Retirement capacity equals entry capacity because a slot is reserved per entry.

### 5.5 Configuration

A plain text configuration file plus EAL options. No external parsing library. One block per peer association with: local endpoint address, peer address, RuleID (default 0), admitted VNIs, extension UDP destination port, outer IP version, `pmtu`, `h_ip` (derived, overridable), `max_fragments` (default 2), frame length range. Global receiver values: Δ, L, per-peer and global caps, `max_slice_len`. **No numeric defaults for Δ and L**; the application refuses to start without them (the draft leaves them open).

Startup validation: `schcvx_tx_validate_cfg` for every peer and direction; fail with a message naming the peer.

### 5.6 Counters and observability

Per association and VNI where practical: frames fragmented, oversized frames dropped (by reason), frames completed, expired, late, duplicates, integrity failures, malformed (including zero UDP checksum), peer admission failures, table exhaustion. Expose them through a periodic dump to a file or a Unix socket command (`show counters`); also expose active entries and bytes in use. Expiry and late are reported separately.

## 6. Docker environment

- `docker/Dockerfile`: pinned base image and DPDK LTS release built or installed reproducibly; build the app and run the native tests during `docker build --target test`.
- Functional runs use virtual devices (`net_af_packet`, `net_tap` or veth pairs) with hugepages mounted and `--cap-add IPC_LOCK --cap-add NET_ADMIN --cap-add SYS_NICE`; avoid `--privileged` except for NIC passthrough.
- `compose.yaml`: two VTEP containers (A, B) joined by an underlay network, and one host container behind each (access side). Inside the underlay path use `tc netem` to impose delay, reorder and loss so the integration tests can force: last-fragment-first arrival, single-fragment loss, duplicates and late fragments.
- Virtual-device runs prove function only. They produce **no** performance conclusions.

## 7. Verification plan

1. **Core unit tests (native, no DPDK):** port every test in `tests/test_wire.py`, `test_crc32c.py`, `test_sender.py`, `test_receiver.py` of the Python model, plus the new regression test for a final duplicate with a different RCS.
2. **Differential tests (mandatory acceptance gate):** `tools/gen_vectors.py` drives `src/schcvx` and writes scenario files to `impl/tests/vectors/`. Line format (plain text, no JSON dependency):
   - `CFG key=value ...`
   - `EVENT <tick> <assoc> <udp_ok 0|1> <datagram hex>`
   - `EXPECT <status> [reason] [delivered frame hex]`
   - `COUNTERS completed=.. expired=.. late=.. duplicate=.. integrity_failure=.. malformed=.. peer_admission_failure=.. table_exhaustion=..`
   The C harness replays each scenario and must match every `EXPECT` and `COUNTERS` line exactly. Vectors cover: both arrival orders for N = 2, N = 3 with all permutations, every single-fragment loss, identical and conflicting duplicates (including RCS-only conflict), inconsistent count or slice lengths, bad RCS, deadline equality, late arrival after retirement, retirement expiry after L, unauthorised peer or VNI, wrong RuleID, zero UDP checksum, cap exhaustion, and the two worked examples of the draft (CRC 0x487a7b87 for the 1514-byte frame with byte i = i mod 256; slices 1454 and 60; outer IP lengths 1496 and 106).
   Sender vectors cover PMTU 1280, 1500 and 9000, IPv4 and IPv6, exact fit and one byte over, tagged frames, and every emitted IP length ≤ PMTU.
3. **Fuzzing:** targets for `schcvx_decode` and for `schcvx_rx_input` fed with random event streams; run under AddressSanitizer and UBSan. No crash, no leak, invariants hold (bytes in use ≤ caps, entries ≤ caps).
4. **Integration (Docker):** a 1514-byte ping and a large UDP flow across the two VTEPs show: frames that fit use plain VXLAN; frames that do not are delivered via the extension; with netem reorder the frame is still delivered; with netem loss of one fragment the frame is dropped at Δ and the `expired` counter increments; MAC learning happens only from delivered frames. Compare the counters with the expectations of the scenario.
5. **Interoperability with the model:** feed the extension datagrams captured from the C sender into the Python receiver, and datagrams built by the Python sender into the C receiver; both must deliver identical frames.

## 8. Acceptance criteria (version 1 complete)

- All core unit and differential tests pass; fuzz targets run clean for a fixed budget (for example 10 minutes each in CI).
- The Docker integration suite passes with IPv4; with IPv6 at milestone M3.
- Every counter of the draft is exposed and verified by at least one scenario.
- No hard-coded answers to draft Open Issues; each is a configuration value or isolated constant listed in `docs/impl/DECISIONS.md`.
- The Docker image builds from scratch with pinned versions.
- Hardware benchmarks are explicitly out of the version 1 acceptance (section 11).

## 9. Milestones and workpackages (for the pipeline)

The pipeline gate command must be runnable where the workers run. Native packages (core, tests, generator) can be verified on the macOS host; packages that need DPDK, Linux or Docker can only be verified in a Linux environment. **Phase 1 must decide where the final gate runs** (see section 13, Q3) before sealing.

| WP | Name | Depends | Owns | Verify (example) |
| --- | --- | --- | --- | --- |
| WP01 | build-skeleton | — | `impl/meson.build`, `impl/core/meson.build`, test runner scaffolding | `meson setup build impl && meson test -C build` |
| WP02 | crc32c-wire | WP01 | `impl/core/**/crc32c*`, `**/wire*`, `impl/tests/core/test_wire.c` | `meson test -C build wire crc32c` |
| WP03 | vector-generator | — | `tools/gen_vectors.py`, `impl/tests/vectors/**`, `tests/test_gen_vectors.py` | `pytest tests/test_gen_vectors.py -q` |
| WP04 | tx-plan | WP02 | `impl/core/**/tx*`, tests | `meson test -C build tx` |
| WP05 | rx-core | WP02 | `impl/core/**/rx*`, tests | `meson test -C build rx` |
| WP06 | differential-harness | WP03, WP04, WP05 | `impl/tests/core/diff_runner.c` | `meson test -C build differential` |
| WP07 | dpdk-glue | WP04, WP05 | `impl/app/src/mbuf_*.c`, `impl/app/src/net_*.c` | `docker build --target test` |
| WP08 | vtep-app | WP07 | `impl/app/src/main.c`, bridge, config, counters | `docker build --target test` |
| WP09 | docker-integration | WP08 | `impl/docker/**`, `impl/tests/integration/**` | `docker compose run tests` |
| WP10 | fuzz | WP05 | `impl/tests/fuzz/**` | `meson test -C build fuzz-smoke` |
| WP11 | ipv6 (M3) | WP08, WP09 | app changes for IPv6, tests | `docker compose run tests-v6` |
| WP12 | operator-docs | WP08 | `impl/README.md` (config, sizing formula, counters) | `bash impl/tests/check_docs.sh` |
| WP13 | bench-plan | WP09 | `impl/bench/**` (procedure, harness, report template) | `bash impl/bench/check.sh` |

Rungs: WP05, WP07 and WP08 are rung 3 (stateful logic and DPDK specifics); WP02, WP04, WP06, WP09 rung 2; the rest rung 1 to 2. Mark WP05, WP08 and WP09 `review_required: true`. Pre-seed locked tests for WP05 from the differential vectors so workers cannot weaken them. Parallel packages must keep disjoint `owns:` sets (WP03 and WP02 are independent; WP04 and WP05 are independent).

Milestones: **M1** WP01–WP06 (core + differential, native). **M2** WP07–WP09 + WP12 (app in Docker, IPv4). **M3** WP10–WP11 (fuzz, IPv6). **M4** WP13 (hardware benchmark procedure; results later).

## 10. Risks and mitigations

| Risk | Mitigation |
| --- | --- |
| The draft's wire format changes (No-ACK Profile rework) | Only `schcvx_wire` knows the layout; regenerate vectors from the updated Python model; the differential gate catches drift |
| NIC lacks UDP checksum offload or multi-segment TX | Explicit software fallback with a one-time log and a counter; test with a vdev that lacks offload |
| Fragments of one frame land on different lcores | D5 relies on the shared five-tuple; test RSS hashing in Docker; add a counter for entries that expire with a single fragment, so a mismatch is visible; ring hand-off is a later feature |
| Retaining RX mbufs starves the RX pool | D6 copies slices to a separate pool |
| Deadline-equality and other open rules change | Constants and configuration, covered by tests that name the rule |
| Gate cannot run Docker or Linux in the pipeline sandbox | Decide in Phase 1 (Q3); keep the core and differential gate native so M1 is always verifiable |

## 11. Benchmark procedure (hardware, later; results pending measurement)

Run on Linux with real NICs (bare metal or SR-IOV VFs), not on virtual devices. Mixes from the draft: a normal/fragmented mix; all frames just over the VXLAN threshold; the maximum frame size. Report original frames per second, outer packets per second, wire bit rate, delivered payload rate, latency percentiles, CPU use, and drops by cause. State whether offered load is the ingress frame rate or the egress wire capacity, since fragmentation adds headers and packets. Report the integrity check and the fragmented forwarding path together. No throughput claim is made before these runs.

## 12. Deferred, not forgotten

- **VPP plugin:** port the core library into a graph node placed before VXLAN encapsulation and after tunnel classification. The core is already DPDK-free, which makes this a thin adaptation. Estimated at several weeks; revisit after M2.
- **OVS-DPDK:** not planned; reassembly state does not fit its flow-action model well.
- Zero-copy reassembly, ring hand-off across lcores, sliding-window retirement for sequential Frame IDs (an idea recorded for the draft, not adopted), dynamic PMTU discovery, restart-safe Frame IDs.

## 13. Questions for the human before sealing

- **Q1.** Confirm D4 (custom minimal VTEP first, VPP deferred). Default: confirmed.
- **Q2.** Which DPDK LTS release and which base OS image to pin? Default: the latest LTS available at project start, on a current Ubuntu LTS image.
- **Q3.** Where does the pipeline's final gate run? Options: (a) the macOS host runs only the native core gate and Docker packages are verified by a separate Linux step; (b) a Linux host or VM with Docker runs the whole gate. Default: (a) for M1, then (b).
- **Q4.** Is IPv6 required for the first usable release, or can it wait for M3? Default: M3.
- **Q5.** Licence for the implementation code (DPDK applications are commonly BSD-3-Clause). Default: BSD-3-Clause.
- **Q6.** Should this implementation follow the current wire format or wait for the No-ACK Profile decision in the draft? Default: implement the current format behind `schcvx_wire` and regenerate vectors if the draft changes.
