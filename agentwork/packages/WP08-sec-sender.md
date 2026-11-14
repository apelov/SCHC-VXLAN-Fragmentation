---
id: WP08
name: sec-sender
kind: implementation
tier: plan
rung: 2
depends: [WP02, WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
  - src/schcvx/sender.py
outputs:
  - draft/sections/04-sender.md
owns:
  - draft/sections/04-sender.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/04-sender.md --must "H_IP" --must "1452" --must "1432" --must "1496" --must "110" --must "RFC 8899" --must "Open Issue"
review_required: false
---
# WP08 — Draft section 4: MTU and sender procedure

Tier: plan
Verify: python3 tools/check_section.py draft/sections/04-sender.md --must "H_IP" --must "1452" --must "1432" --must "1496" --must "110" --must "RFC 8899" --must "Open Issue"

Specify the sender.

## Deliverables

1. `04-sender.md`: `# MTU and Sender Procedure`: underlay IP PMTU definition, formulas for the VXLAN frame budget and slice budget C, N computation and rejection rules, per-frame budget snapshot, ordering of emission, DF/IPv6 rules, static per-peer PMTU start and the RFC 8899 pointer for dynamic discovery (Open Issue), and the 1500-byte worked numbers (1464/1444, 1450/1430, 1452/1432, 1514-byte frame -> 1452+62, outer IP lengths 1496 and 110) verified against `src/schcvx/sender.py`.

## Owns (touch only these)

- `draft/sections/04-sender.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp08.md` summarizing changes, verification output, and any non-obvious choices.
