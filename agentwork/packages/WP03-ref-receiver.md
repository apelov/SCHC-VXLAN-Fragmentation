---
id: WP03
name: ref-receiver
kind: implementation
tier: code
rung: 3
depends: [WP01]
inputs:
  - docs/design/DESIGN.md
  - src/schcvx/wire.py
  - src/schcvx/crc32c.py
outputs:
  - src/schcvx/receiver.py
  - tests/test_receiver.py
owns:
  - src/schcvx/receiver.py
  - tests/test_receiver.py
locked_tests: []
done_when:
  - python3 -m pytest tests/test_receiver.py -q && ruff check src tests
review_required: true
---
# WP03 — Reference receiver: reassembly state machine

Tier: code
Verify: python3 -m pytest tests/test_receiver.py -q && ruff check src tests

Implements DESIGN "Receiver algorithm" with an injected monotonic clock; deterministic and single-threaded.

## Deliverables

1. `receiver.py`: class `Receiver(config, clock)` with `config` = frozen dataclass (delta, retire_l, max_entries_per_peer, max_bytes_per_peer, max_entries_global, max_bytes_global, max_count, max_frame_len, max_slice_len, authorized {(peer, vni)}). Method `receive(peer, datagram, udp_checksum_ok=True) -> Result` where Result is an enum-like outcome (`DELIVERED(frame)`, `PENDING`, `DUPLICATE_IGNORED`, `RETIRED_DROP`, `LATE`, `ABORTED(reason)`, `MALFORMED`, `PEER_REJECTED`, `TABLE_FULL`). Method `sweep()` expires entries. Counters dict with: completed, expired, late, duplicate, integrity_failure, malformed, peer_admission_failure, table_exhaustion.
2. Behavior exactly per DESIGN steps 1-5 as revised (there is NO original-length field): any index opens an entry with expiry first_rx+delta; expiry enforced before delivery even without a sweep (a fragment arriving at exactly the deadline is expired: use "expired when now >= expiry" and flag it in the report as an item for the draft); identical duplicates ignored without extending expiry; a conflicting duplicate, inconsistent count, nonfinal slices of unequal length, a final slice longer than a nonfinal one, or a retained byte count above max_frame_len aborts and retires; at completion the total length must lie within the configured frame-length range and the RCS must verify, else abort; the receiver never assumes a slice length (peers may use different PMTUs); retirement records kept for retire_l after retirement; a fragment after expiry counts as `late`, distinct from `expired`; reserve a retirement slot at admission and refuse admission when exhausted.
3. Tests for: two peers with different slice lengths; : both arrival orders (last fragment first), every single-fragment loss with N=2 and N=3, identical and conflicting duplicates, inconsistent headers, bad RCS, deadline equality, late arrival after retirement, table exhaustion, unauthorized peer/VNI, retirement-record expiry after L, and no delivery after expiry.

## Owns (touch only these)

- `src/schcvx/receiver.py`
- `tests/test_receiver.py`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp03.md` summarizing changes, verification output, and any non-obvious choices.
