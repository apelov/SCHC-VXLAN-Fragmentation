---
id: WP11
name: sec-examples
kind: implementation
tier: plan
rung: 2
depends: [WP04, WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
  - draft/generated/examples.md
outputs:
  - draft/sections/06-examples.md
owns:
  - draft/sections/06-examples.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/06-examples.md --must "last fragment" --must "expir" --must "late" --must "pending measurement"
review_required: false
---
# WP11 — Draft section 8: Examples and acceptance checks

Tier: plan
Verify: python3 tools/check_section.py draft/sections/06-examples.md --must "last fragment" --must "expir" --must "late" --must "pending measurement"

Turn the generated examples into draft text and list acceptance checks.

## Deliverables

1. `06-examples.md`: `# Examples` with the two examples taken verbatim from `draft/generated/examples.md` (numbers must match exactly; do not invent others), and a brief `# Test and Benchmark Considerations` (at most one short paragraph and one list) condensing DESIGN "Fast path and acceptance checks". State that throughput results are pending measurement.

## Owns (touch only these)

- `draft/sections/06-examples.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp11.md` summarizing changes, verification output, and any non-obvious choices.
