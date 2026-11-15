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
