---
id: WP02
name: ref-sender
kind: implementation
tier: code
rung: 2
depends: [WP01]
inputs:
  - docs/design/DESIGN.md
  - src/schcvx/wire.py
  - src/schcvx/crc32c.py
outputs:
  - src/schcvx/sender.py
  - tests/test_sender.py
owns:
  - src/schcvx/sender.py
  - tests/test_sender.py
locked_tests: []
done_when:
  - python3 -m pytest tests/test_sender.py -q && ruff check src tests
review_required: false
---
# WP02 — Reference sender: budget and slicing

Tier: code
Verify: python3 -m pytest tests/test_sender.py -q && ruff check src tests

Implements DESIGN "MTU and sender algorithm" over the WP01 codec.

## Deliverables

1. `sender.py`: `vxlan_frame_budget(pmtu, h_ip)`, `slice_budget(pmtu, h_ip)` (C), `fragment_frame(frame, *, vni, frame_id, pmtu, h_ip, max_frame_len, max_fragments) -> list[bytes] | Drop` where the outcome is either the list of encoded datagram payloads in ascending index order or a `Drop(reason)` for: fits in plain VXLAN (return a distinct `Fits` result, not a drop), C<=0, frame length outside range, N outside 2..max_fragments. Slices are all C bytes except the last (remainder + RCS = CRC-32C of the original frame).
2. Tests pin the DESIGN numbers: PMTU 1500 no options: standard VXLAN frame limits 1464 (IPv4, H_IP=20) and 1444 (IPv6, H_IP=40); slice budgets 1452 and 1432; a 1514-byte frame over IPv4 yields slices 1452 and 62 and outer IP lengths 1496 and 110. Also exact-fit and one-byte-over for IPv4/IPv6, tagged (1518-byte) frames, a change of PMTU between calls, and every emitted IP packet <= PMTU.

## Owns (touch only these)

- `src/schcvx/sender.py`
- `tests/test_sender.py`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp02.md` summarizing changes, verification output, and any non-obvious choices.
