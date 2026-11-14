---
id: WP07
name: sec-wire
kind: implementation
tier: plan
rung: 2
depends: [WP01, WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
  - src/schcvx/wire.py
  - src/schcvx/crc32c.py
outputs:
  - draft/sections/02-wire-format.md
owns:
  - draft/sections/02-wire-format.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/02-wire-format.md --must "0xe3069283" --must "Castagnoli" --must "RuleID" --must "Frame ID" --must "Open Issue"
review_required: false
---
# WP07 — Draft section 3: Wire format

Tier: plan
Verify: python3 tools/check_section.py draft/sections/02-wire-format.md --must "0xe3069283" --must "Castagnoli" --must "RuleID" --must "Frame ID" --must "Open Issue"

Specify the fragment format precisely.

## Deliverables

1. `02-wire-format.md`: `# Fragment Format` with an ASCII-art header diagram, a field table (offset, size, meaning) matching DESIGN (14-byte header: VNI 3, RuleID 1, Frame ID 8, index 1, count 1; no original-length field; RuleID kept at one byte, value 0, for alignment, with a note that RFC 8724 §6 leaves the RuleID size to the Profile), byte order, the final-fragment RCS, the CRC-32C definition (polynomial, reflection, init, final XOR, check value 0xe3069283, big-endian trailer), UDP checksum generate/verify requirement, UDP port selection by configuration, and the provisional status. Include an Open Issue for header versioning and the eventual port/IANA request. Cross-check offsets and sizes against `src/schcvx/wire.py`.

## Owns (touch only these)

- `draft/sections/02-wire-format.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp07.md` summarizing changes, verification output, and any non-obvious choices.
