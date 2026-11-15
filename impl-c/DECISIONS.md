# C implementation decisions

## WP01: build skeleton

Use explicit source lists in Meson, extending them as packages are added. Each
listed test source produces its own executable and a test named by removing
`test_` and `.c`. This is a build-maintenance choice; WP01 dictates the targets.

## WP02: portable CRC-32C

Use a checked-in, static const 256-entry table (one entry per octet). Generate
each entry by starting with its index and applying eight iterations of
`r = (r >> 1) ^ ((r & 1) ? 0x82f63b78 : 0)`. This avoids runtime initialization,
allocation, and build-time generator dependencies. The polynomial and register
parameters come from SPEC section 4.1 and the draft's Reassembly Check Sequence.

The incremental state is the unfinalized register. Finalization returns a value
without modifying that state. Zero-length updates leave the state unchanged
and permit a null data pointer; nonempty input must address the requested bytes.
These are API conventions for the supplied signatures, not wire-format choices.

## Questions

None for WP01 and WP02.

## WP03: wire codec

Associations borrow a caller-owned RuleID array. Duplicate IDs are accepted;
empty arrays and invalid variants are rejected. Configuration is checked on
each classify/encode call. This allocation-free representation supports multiple
configured rules without imposing a new protocol limit.

Classification returns negative named errors and leaves output unchanged on
error. Payload views borrow the input: the whole SCHC Packet for unfragmented,
a tile for data fragments, and an empty view for Abort. Unused fields are zero.
E_MALFORMED covers short ordinary messages, E_INVALID_FCN invalid FCNs,
E_SHORT_ALL1 truncated All-1 data, and E_FIRST bad first-fragment envelopes.
Header-only regular fragments return E_MALFORMED before envelope checking,
following SPEC 4.2's order.

The size_t encoder returns zero on error without writing. It checks configured
RuleID membership, field bounds and kind/FCN agreement; derived first/countdown
fields are ignored. It writes fragment headers only; unfragmented packets need
no fragment header. Callers supply space for the All-1 header. Tile validation
belongs to classification. Extended RuleID overflow is excluded by uint8_t;
compact RuleID, DTag and FCN limits are explicitly checked.

FCN helpers use zero-based sender position and K in 2..8; invalid inputs yield
negative errors. Countdown is defined only for valid regular FCNs. Header size
helpers return zero on invalid variants. These are API conventions; the draft
dictates wire widths and protocol behavior.

## WP04: shared vectors

The standard-library generator takes input JSON and output header paths and
emits byte arrays and metadata without protocol encoding. Meson tracks both
script and JSON and generates the header in the build directory. The C test
reconstructs packets independently and uses only CRC and wire APIs for fragments.
No protocol disagreements were found for WP03 or WP04.

## WP05: sender and DTag allocation

The configured PMTU is the minimum supported PMTU. Frame limits include the
14-byte Ethernet header. An unfragmented plan reports K=1, C=0 and packet
length as last_len. Fragment construction borrows prefix and frame buffers;
the output must not overlap them. Emission stops at equality with S.

The caller owns one DTag state per association and fragmentation rule, provides
its start value (zero is the default), monotonic nanosecond timestamps, and a
positive integer token rate and burst. The bucket starts full and retains
fractional tokens. Initialization checks burst plus rate times quarantine
against the identifier space. Backward timestamps have a distinct error. The
caller must account for the emission interval in its quarantine rate margin.

## Questions (WP05)

SPEC 4.3 says stop when the emission interval would exceed S, whereas the draft
requires stopping when S is reached. The implementation follows the draft.

## WP06: ordered receiver and shared state

One receiver instance represents one peer association; entries and retirement records are keyed by fragmentation RuleID and DTag. The caller validates peer association and UDP checksum before invoking the receiver, supplies monotonically nondecreasing receive timestamps, and owns configuration arrays, counters and aligned storage. Delivery views borrow the datagram or receiver storage and are valid until the next receiver operation. Deadlines saturate at UINT64_MAX on arithmetic overflow. Expired entries retire at their deadline, even if a sweep runs later. The retirement table reserves a slot for every active entry.

Packet storage uses a fixed maximum-sized slot per configured entry; max_bytes limits actual retained bytes. If a fragment cannot fit the retained-byte budget, it is dropped and table exhaustion is counted; an existing entry remains until completion or timeout. A Sender-Abort without an active entry is counted but does not reserve a retirement record. Counter names are snake_case derived from SPEC 4.6. Formatting returns the required byte count excluding the NUL.

## WP07: reordering-tolerant receiver

Association receivers share a caller-owned global pool, whose entry, byte and orphan limits and initial zero usage are supplied by the caller. The local orphan budget counts entries. The configured maximum tile length is enforced independently of the packet bound. Storage has one packet-sized slot per entry and one assembly buffer per receiver; actual retained payload bytes are charged to the limits. Conflicts and incompatible tile lengths count as integrity failures, while admission failures count as table exhaustion. A fragment rejected solely for byte capacity leaves an existing entry intact. The caller supplies monotonically nondecreasing receive timestamps and must process queued arrivals before sweeping past their timestamps, as in WP06. Inactivity starts on admission and resets on newly admitted regular fragments; duplicates do not reset it. Early VNI validation uses the shared packet validator with a padded Ethernet portion.

## WP08: fuzzing

The event stream selects a variant, advances monotonic time, optionally sweeps,
 and supplies arbitrary datagrams or sender-built fragments. Generated packets
use a small DTag space to exercise retirement; input controls fragment count,
position, contents, and optional corruption. The CRC oracle compares delivered
frames with observed first-fragment prefixes and All-1 RCS values for the same
DTag. Unfragmented packets have no RCS. The standalone driver uses a fixed
xorshift seed and 1,000 inputs per target for smoke tests.

Run with sanitizers: `meson setup build-asan impl-c -Db_sanitize=address,undefined && meson test -C build-asan fuzz-smoke`.
When libFuzzer is supported, run `build-asan/tests/fuzz_classify`,
`build-asan/tests/fuzz_ordered`, or `build-asan/tests/fuzz_tolerant` directly
with libFuzzer arguments and an optional corpus directory.

## WP09: portable daemon layer

Configuration numeric time values are nanoseconds; DTag rate is identifiers per
second and burst is a separate required key. The allocator requires both values
and the specification does not define them. The first configured VNI is used for
transmission, while all configured VNIs are admitted on receive. The global
limits are required keys `max_entries`, `max_bytes`, and `orphan_budget`.
Per-association retirement capacity equals max_entries. The portable layer owns
receiver storage and invokes synchronous callbacks; the I/O implementation must
copy datagrams if it queues them, and must provide monotonic time between sends.

## WP09b: Linux I/O

The raw socket filter accepts the configured destination ports; the portable layer
performs address and association admission. One monotonic timestamp is used for
each recvmmsg batch. Counters files contain named association sections, including
all associations sharing a path, and are atomically replaced; periodic scheduling
is measured from the last attempted write. TAP MTU setup is best effort, while
failure to bring an interface up is fatal. Socket and TAP I/O errors are logged;
failed individual writes are dropped. Verify on the Linux server that
IP_PMTUDISC_PROBE sets DF while ignoring cached PMTU, that the transmit socket's
drop-all filter still prevents ICMP port-unreachable responses, that raw-socket
copies arrive as expected alongside bound UDP sockets, and that the TAP MTU
(max_frame_len minus 14) supports the intended tagged and untagged frames.

## WP10: namespace integration test

`tests/integration/run.sh` builds four namespaces (prefix `schcvx<pid>-`), runs
each receiver mode with freshly generated configs, and sums the counters of both
daemons (the counters file has a `[association lab]` header followed by
`name value` lines; the script reads lines by name). Ping loss is derived from
the transmitted/received counts. Ordered-mode reorder loss and loss-step loss are
reported, not judged. The temporary directory is kept for inspection. The server
run must confirm: the daemon creates a non-persistent `schc0` (it disappears when
the process exits, and the script waits for that before restarting); SIGUSR1
rewrites the counters file within 0.5 s; the daemon does not require `schc0` to
be pre-configured up or bridged; the TAP MTU from max_frame_len 1518 carries
1514-byte frames; DF pings of size 1472 fit the 1500 access MTU.

Finding from the first root run: every datagram was counted malformed. veth has
TX checksum offload on by default, so locally generated UDP packets cross the
underlay with a partial checksum (CHECKSUM_PARTIAL); the kernel UDP stack accepts
them, but the daemon receives on a raw socket and verifies the checksum in
software (zero or wrong is dropped, as the draft requires). Fix: `run.sh` runs
`ethtool -K tx off` on u1 and u2 and fails early if ethtool is missing; physical
NICs fill the checksum in hardware, so only the veth test is affected.

Server run (2026-11-15, server 1): all required steps passed for both receivers, confirming the assumptions above. Ordered receiver lost 7 of 20 reordered pings, as expected; the tolerant receiver lost none, and counted 12 duplicates under netem duplicate 50%.

## WP11/WP12: NixOS packaging and documentation

Package source is the repository root restricted with `lib.fileset` to `impl-c` plus
`draft/revised/wire-vectors.json` (the Meson tests read that file at `../../draft` from
`impl-c/tests`), with `sourceRoot = source/impl-c`; the build passes `-Dwerror=false` so a newer
compiler cannot fail the package on warnings, and `doCheck = true` runs `meson test`.

Service user: the daemon needs only `CAP_NET_ADMIN` (TUNSETIFF, MTU, link up) and `CAP_NET_RAW` (raw
socket); it opens `/dev/net/tun` read-write and writes counters with mkstemp and rename in the counters
directory. The module therefore uses `DynamicUser = true` with those two ambient capabilities,
`DevicePolicy = closed` plus `DeviceAllow = /dev/net/tun rw`, and `RuntimeDirectory = schcvx` (owned by the
dynamic user, default counters path `/run/schcvx/<name>.counters`). This is untested; if the server
shows a permission failure, the fallback is `User = root` with the same capability bounding set.

Bridge setup is an `ExecStartPost` with the `+` prefix (full privileges, since the daemon user has no
rights over bridges) that waits up to 10 s for each TAP device, creates the bridge if missing, enslaves
the TAP device and the access interface, and sets them up. Only associations with `bridge` set are
touched. IP addresses are never changed. Association option names equal the daemon keys; durations are
nanoseconds. Lab-profile defaults are provided for everything except `local_ip`, `peer_ip`, `vnis` and `tap`.
`tests/check_docs.sh` ensures every key of `keys[]` and `globals[]` appears in backticks in the README.

## Review fixes

1. `rx_tolerant.c`: a fragment whose payload exceeds `max_tile_len` or the per-packet stride is dropped and counted as malformed whether or not an entry exists; an existing entry is left untouched (draft: bounds are checked before buffering; a malformed individual datagram does not terminate admitted state). The later check that terminates an entry when the accumulated packet would exceed its maximum length is unchanged (conflict with stored state). Test added to `test_rx_tolerant.c`.
2. Test gaps against SPEC.md section 7 item 3: scripted scenarios with explicit timestamps added to `test_rx_ordered.c` (All-1 loss with expiry at exactly last regular + I and none 1 ns earlier, lost regular, duplicate, bad RCS, reordering, Sender-Abort, unauthorised VNI, unknown rule, entry and byte caps) and to `test_rx_tolerant.c` (Sender-Abort, unknown rule, byte cap, conflicting second first-marked fragment, first-marked collision with stored regular positions).
3. `wire.h`: comment now says NULL data or len == 0 is rejected as malformed, matching `schcvx_classify`.
4. `test_wire.c`: sentinel assertions that the classify output struct and the encoder buffer are unchanged on error.
5. `test_tx.c`: the "every datagram fits P" check also runs for every fragment of the K=2..8 tiling loop.
6. `nix/module.nix` and `linux/schcvx.conf.example`: `dtag_rate` default 1000000 and `dtag_burst` 10000 (the old 1000/100 throttled traffic above about 1000 fragmented frames per second); README explains that `dtag_rate` must exceed the fragmented frame rate.
7. README: the flake must be referenced as a git flake including the repository root; a plain `path:` reference to `impl-c` fails because the build reads `draft/revised/wire-vectors.json`.

## Performance fixes

1. The raw receive socket requests a 16 MiB buffer (`SO_RCVBUFFORCE`, then `SO_RCVBUF`) and logs the effective size. At about 21,000 two-fragment packets in 6 s the kernel default (212992 bytes) dropped 4,454 datagrams; with 16 MiB there were none. Failure to set it only warns.
2. Retirement capacity equals `max_entries` (WP09), so about rate x R identifiers are retired at any moment. With 1024 entries and R = 100 ms the receiver refused 5,323 packets in 6 s; with 16384 none. Lab and NixOS defaults are now per association 16384 / 33554432 / 2048 and global 32768 / 67108864 / 4096 (max_entries / max_bytes / orphan_budget). The config parser has no constraint between global and per-association values.
