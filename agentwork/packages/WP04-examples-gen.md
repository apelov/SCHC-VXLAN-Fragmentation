---
id: WP04
name: examples-gen
kind: implementation
tier: code
rung: 2
depends: [WP02, WP03]
inputs:
  - docs/design/DESIGN.md
  - src/schcvx/sender.py
  - src/schcvx/receiver.py
outputs:
  - tools/gen_examples.py
  - tests/test_examples.py
  - draft/generated/examples.md
owns:
  - tools/gen_examples.py
  - tests/test_examples.py
  - draft/generated/examples.md
locked_tests: []
done_when:
  - python3 -m pytest tests/test_examples.py -q && ruff check tools tests
review_required: false
---
# WP04 — Worked examples from the reference model

Tier: code
Verify: python3 -m pytest tests/test_examples.py -q && ruff check tools tests

Produces the two examples DESIGN requires: a last-fragment-first completion and a deadline expiry, computed (not hand-written).

## Deliverables

1. `tools/gen_examples.py` prints and writes `draft/generated/examples.md` (fixed, deterministic content): Example A: 1514-byte frame over IPv4, PMTU 1500, two fragments (slices 1452 and 62, outer IP lengths 1496 and 110) delivered last-fragment-first with a stated clock, showing the bitmap after each datagram and the final CRC-32C; Example B: same frame, only the final fragment arrives, entry expires at first_rx+delta, then the missing fragment arrives late and is counted as `late`. Use a fixed illustrative frame (deterministic bytes), VNI, Frame ID, peer name, delta and L; show header fields in hex.
2. Test that regenerating output matches the committed file byte for byte, and that the counters match the narrative.

## Owns (touch only these)

- `tools/gen_examples.py`
- `tests/test_examples.py`
- `draft/generated/examples.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp04.md` summarizing changes, verification output, and any non-obvious choices.
