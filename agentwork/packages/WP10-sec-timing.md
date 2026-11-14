---
id: WP10
name: sec-timing
kind: implementation
tier: plan
rung: 3
depends: [WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
outputs:
  - draft/sections/06-timing-and-profile.md
owns:
  - draft/sections/06-timing-and-profile.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/06-timing-and-profile.md --must "Frame ID" --must "restart" --must "generation" --must "Open Issue" --must "L" --must "first-arrival"
review_required: true
---
# WP10 — Draft section 6: Deadline, Frame ID lifetime, restart, profile

Tier: plan
Verify: python3 tools/check_section.py draft/sections/06-timing-and-profile.md --must "Frame ID" --must "restart" --must "generation" --must "Open Issue" --must "L" --must "first-arrival"

Cover choosing Δ, retirement bound L, Frame ID uniqueness, restart and the default operational profile.

## Deliverables

1. `06-timing-and-profile.md`: `# Discard Deadline` (lost vs late observability; Δ budget for deterministic and best-effort paths; first-arrival deadline bounds receiver-held time only, end-to-end age out of scope), `# Frame Identification and Restart` (64-bit counter per association and VNI, persistence or association generation, receiver restart drain interval, no epoch on the wire in the prototype, restart-safe interoperability not claimed until specified = Open Issue), and `# Default Operational Profile` (max 2 fragments for Ethernet-1500, minimum-PMTU validation, configured bounds, meanings and configuration of Δ and L; leave numeric defaults as explicit Open Issues unless DESIGN gives the value).

## Owns (touch only these)

- `draft/sections/06-timing-and-profile.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp10.md` summarizing changes, verification output, and any non-obvious choices.
