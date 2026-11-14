---
id: WP09
name: sec-receiver
kind: implementation
tier: plan
rung: 3
depends: [WP03, WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
  - src/schcvx/receiver.py
outputs:
  - draft/sections/05-receiver.md
owns:
  - draft/sections/05-receiver.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/05-receiver.md --must "retire" --must "bitmap" --must "expir" --must "Frame ID" --must "late" --must "Open Issue"
review_required: true
---
# WP09 — Draft section 5: Receiver procedure

Tier: plan
Verify: python3 tools/check_section.py draft/sections/05-receiver.md --must "retire" --must "bitmap" --must "expir" --must "Frame ID" --must "late" --must "Open Issue"

Specify the receiver precisely enough to interoperate and to be testable.

## Deliverables

1. `05-receiver.md`: `# Receiver Procedure` following DESIGN steps 1-5 with BCP 14 keywords where interoperability needs them: entry key, validation before allocation, caps, admission, expiry rules (the deadline-equality rule used by the reference receiver is stated as a recommendation pending WG input, as an Open Issue), duplicates, bitmap/length/RCS completion, retirement records and the reserve-a-slot rule, counters (completed, expiration, late fragments, duplicates, integrity failure, malformed, peer admission failure, table exhaustion). Explain why starting from any index supports reordering.

## Owns (touch only these)

- `draft/sections/05-receiver.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp09.md` summarizing changes, verification output, and any non-obvious choices.
