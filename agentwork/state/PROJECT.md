# SCHC-VXLAN-Fragmentation — Project Orientation

## What this project does
Produces the individual Internet-Draft `draft-vladyka-schc-vxlan-fragmentation-00` (authors Andrii Vladyka, Alexander Pelov) for the IETF SCHC WG: deadline-bounded Ethernet frame fragmentation for VXLAN deployments, framed as a candidate new SCHC fragmentation mode with a UDP/Ethernet binding. A small Python reference model backs every number and example in the draft.

## Authoritative Documents
- Design: `docs/design/DESIGN.md`
- Decisions: `agentwork/state/DECISIONS.md`
- Worker Contract: `AGENTS.md`

## Architecture Summary
- `src/schcvx/` — reference model: `wire.py` (header codec, constants), `crc32c.py`, `sender.py` (budget, slicing), `receiver.py` (reassembly state machine, injected clock).
- `tests/` — pytest suites pinning the design's acceptance checks.
- `tools/` — `check_section.py` (draft section lint), `gen_examples.py` (worked examples from the reference model).
- `draft/sections/*.md` — kramdown-rfc section files; `draft/draft-vladyka-schc-vxlan-fragmentation-00.md` — main file including them; `draft/build.sh` builds XML/txt.

## Canonical Commands
- Build: `bash draft/build.sh`
- Test: `python3 -m pytest -q`
- Lint: `ruff check src tests tools`

## Invariants & Rules
- Do not modify files outside task ownership (`owns:`).
- Run checks before declaring tasks complete.
- Follow the project's active privacy profile in `agentwork/config/project.yaml`.
- Open items in DESIGN.md "Items to settle" stay marked open in the draft; never resolve them.
- Every number in the draft comes from DESIGN.md or the reference model's output. No throughput claims.
- RFC citations are limited to those in DESIGN.md (RFC 7348 §4.3/§5, RFC 8724 §8.2.3/§8.4.1, RFC 8899) plus BCP 14 (RFC 2119, RFC 8174). CRC-32C is defined inline in the draft. The Driver verifies references after the build.
