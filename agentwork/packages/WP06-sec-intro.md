---
id: WP06
name: sec-intro
kind: implementation
tier: plan
rung: 2
depends: [WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
outputs:
  - draft/sections/01-introduction.md
owns:
  - draft/sections/01-introduction.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/01-introduction.md --must "Terminology" --must "BCP 14" --must "VXLAN" --must "Open Issue" --must "No-ACK" --must "RCS" --must "8.4.1"
review_required: false
---
# WP06 — Draft section 1: Introduction, Terminology, Relationship to SCHC

Tier: plan
Verify: python3 tools/check_section.py draft/sections/01-introduction.md --must "Terminology" --must "BCP 14" --must "VXLAN" --must "Open Issue" --must "No-ACK" --must "RCS" --must "8.4.1"

Write the framing section in kramdown-rfc markdown. Keep it short: the draft is a short first version.

## Deliverables

1. `01-introduction.md`: `# Introduction` (problem, requirements, non-goals incl. no ACK/retransmit, no inter-frame ordering across ordinary/fragmented paths), `# Terminology` (BCP 14 boilerplate text and a definitions list: VTEP, PMTU, slice, Frame ID, RCS, Δ, L, peer association), and `# Placement and Interoperability` (DESIGN "Placement and interoperability", incl. unicast scope, ingress replication, drop-and-count behavior, port-selection of the format, same source port for all fragments, and the short NAT note from DESIGN). Mark the provisional-wire-value status.
2. In the same file, `# Relationship to SCHC` covering the No-ACK in-order assumption of RFC 8724 §8.4.1, the candidate new mode, the mapping of RuleID/Frame ID/index/RCS to SCHC terms (as a table, marking each mapping that DESIGN lists under "Items to settle" as Open Issue), no RFC 8724 wire compatibility, and header compression out of scope.

## Owns (touch only these)

- `draft/sections/01-introduction.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp06.md` summarizing changes, verification output, and any non-obvious choices.
