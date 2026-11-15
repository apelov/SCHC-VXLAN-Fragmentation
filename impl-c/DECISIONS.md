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
