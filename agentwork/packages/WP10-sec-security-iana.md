---
id: WP10
name: sec-security-iana
kind: implementation
tier: plan
rung: 3
depends: [WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
outputs:
  - draft/sections/05-security-iana.md
owns:
  - draft/sections/05-security-iana.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/05-security-iana.md --must "Security Considerations" --must "IANA Considerations" --must "spoofing" --must "replay" --must "exhaustion" --must "Manageability" --must "authenticated"
review_required: true
---
# WP10 — Draft section 7: Security, IANA, Manageability

Tier: plan
Verify: python3 tools/check_section.py draft/sections/05-security-iana.md --must "Security Considerations" --must "IANA Considerations" --must "spoofing" --must "replay" --must "exhaustion" --must "Manageability" --must "authenticated"

Write the closing normative-support sections.

## Deliverables

1. `05-security-iana.md`: `# Manageability Considerations` (counters, configuration of peers/VNIs/ports/PMTU/limits), `# Security Considerations` (peer and VNI admission, spoofing, state exhaustion, replay, authenticated underlay where required; CRC and UDP checksum detect corruption only, no authentication), `# IANA Considerations` (no request in this version; locally configured port; cite draft-ietf-schc-protocol-numbers informatively as the candidate SCHC UDP port; note the eventual UDP port and any SCHC registry request as Open Issue).

## Owns (touch only these)

- `draft/sections/05-security-iana.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp10.md` summarizing changes, verification output, and any non-obvious choices.
