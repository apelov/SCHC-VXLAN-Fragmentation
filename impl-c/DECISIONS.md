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
