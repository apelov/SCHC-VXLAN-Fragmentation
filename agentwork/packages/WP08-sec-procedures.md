---
id: WP08
name: sec-procedures
kind: implementation
tier: plan
rung: 3
depends: [WP02, WP03, WP05]
inputs:
  - tools/check_section.py
  - docs/design/DESIGN.md
  - src/schcvx/sender.py
  - src/schcvx/receiver.py
outputs:
  - draft/sections/03-procedures.md
owns:
  - draft/sections/03-procedures.md
locked_tests: []
done_when:
  - python3 tools/check_section.py draft/sections/03-procedures.md --must "H_IP" --must "1454" --must "1434" --must "1496" --must "106" --must "RFC 8899" --must "retire" --must "bitmap" --must "late" --must "Open Issue"
review_required: true
---
# WP08 — Draft section 3: Sender and receiver procedures

Tier: plan
Verify: python3 tools/check_section.py draft/sections/03-procedures.md --must "H_IP" --must "1454" --must "1434" --must "1496" --must "106" --must "RFC 8899" --must "retire" --must "bitmap" --must "late" --must "Open Issue"

Specify the sender and receiver procedures in one short section.

## Deliverables

1. `03-procedures.md`: `# Sender Procedure`: underlay IP PMTU definition; PMTU is a per-peer, per-direction sender configuration with no default in the document (1500 appears only in examples); formulas for the ordinary VXLAN frame budget and the slice budget C (`PMTU - H_IP - 8 - 14 - 4`); N computation, rejection rules and the requirement that the minimum PMTU carry the maximum frame in the maximum number of fragments; per-frame budget snapshot; ascending-index emission; same source UDP port for all fragments of a frame, derived from the inner frame as for VXLAN (RFC 7348 §5); DF/IPv6 rules; static PMTU first and an RFC 8899 pointer for dynamic discovery (Open Issue). Worked numbers, verified against `src/schcvx/sender.py`: with PMTU 1500, standard VXLAN carries frames up to 1464 (IPv4) and 1444 (IPv6); slice budgets 1454 and 1434; a 1514-byte frame over IPv4 gives slices 1454 and 60 and outer IP lengths 1496 and 106.
2. In the same file, `# Receiver Procedure` following DESIGN steps 1-5 as revised: there is no original-length field; the receiver uses the count, equal-length nonfinal slices, the configured maximum frame length and the RCS, and does not assume a slice length. Cover entry key, validation before allocation (including the drop of zero or incorrect UDP checksums), caps, admission, expiry (state the deadline-equality rule used by the reference receiver as a recommendation pending WG input, as an Open Issue), duplicates, bitmap/RCS completion, retirement records with the reserve-a-slot rule, and the counters (completed, expiration, late fragments, duplicates, integrity failure, malformed, peer admission failure, table exhaustion). Explain why starting from any index supports reordering. Keep it short.

## Owns (touch only these)

- `draft/sections/03-procedures.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp08.md` summarizing changes, verification output, and any non-obvious choices.
