---
id: WP12
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
  - draft/sections/08-examples.md
owns:
  - draft/sections/08-examples.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/08-examples.md --must "last fragment" --must "expir" --must "late" --must "pending measurement"
review_required: false
---
# WP12 — Draft section 8: Examples and acceptance checks

Tier: plan
Verify: python3 tools/check_section.py draft/sections/08-examples.md --must "last fragment" --must "expir" --must "late" --must "pending measurement"

Turn the generated examples into draft text and list acceptance checks.

## Deliverables

1. `08-examples.md`: `# Examples` with the two examples taken verbatim from `draft/generated/examples.md` (numbers must match exactly; do not invent others), and `# Test and Benchmark Considerations` condensing DESIGN "Fast path and acceptance checks" (test matrix, benchmark mixes, metrics to report, offered-load definition). State that throughput results are pending measurement.

## Owns (touch only these)

- `draft/sections/08-examples.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp12.md` summarizing changes, verification output, and any non-obvious choices.
