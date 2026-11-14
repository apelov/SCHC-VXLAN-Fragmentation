---
id: WP01
name: ref-core
kind: implementation
tier: code
rung: 2
depends: []
inputs:
  - docs/design/DESIGN.md
outputs:
  - src/schcvx/__init__.py
  - src/schcvx/wire.py
  - src/schcvx/crc32c.py
  - tests/test_wire.py
  - tests/test_crc32c.py
owns:
  - src/schcvx/__init__.py
  - src/schcvx/wire.py
  - src/schcvx/crc32c.py
  - tests/test_wire.py
  - tests/test_crc32c.py
locked_tests: []
done_when:
  - python3 -m pytest tests/test_wire.py tests/test_crc32c.py -q && ruff check src tests
review_required: false
---
# WP01 — Reference core: wire codec and CRC-32C

Tier: code
Verify: python3 -m pytest tests/test_wire.py tests/test_crc32c.py -q && ruff check src tests

Shared types for the reference model. Everything else imports from here.

## Deliverables

1. `crc32c.py`: `crc32c(data: bytes) -> int` per the Castagnoli spec in DESIGN.md "Prototype wire format" (reflected, init/xorout 0xffffffff); test the check value 0xe3069283 for b"123456789". Stdlib only.
2. `wire.py`: constants (HEADER_LEN=16, RCS_LEN=4, UDP_LEN=8, VXLAN_HDR_LEN=8, RULE_ID_0=0); frozen dataclass `Fragment(vni, rule_id, frame_id, index, count, orig_len, payload, rcs: int|None)`; `encode(fragment) -> bytes` (RCS appended only when index == count-1; require rcs set then, unset otherwise); `decode(datagram: bytes) -> Fragment` raising `MalformedFragment(reason)` for short header, count<2, index>=count, orig_len==0, empty slice, and a final fragment too short to hold slice plus RCS. Network byte order. VNI is 24 bits, frame_id 64 bits, validate ranges on encode.
3. Tests covering round trips, the byte offsets in the DESIGN table, final-fragment RCS handling and every malformed case.

## Owns (touch only these)

- `src/schcvx/__init__.py`
- `src/schcvx/wire.py`
- `src/schcvx/crc32c.py`
- `tests/test_wire.py`
- `tests/test_crc32c.py`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp01.md` summarizing changes, verification output, and any non-obvious choices.
