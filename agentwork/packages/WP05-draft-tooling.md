---
id: WP05
name: draft-tooling
kind: implementation
tier: code
rung: 1
depends: []
inputs:
  - docs/design/DESIGN.md
outputs:
  - tools/check_section.py
  - tests/test_check_section.py
  - draft/build.sh
owns:
  - tools/check_section.py
  - tests/test_check_section.py
  - draft/build.sh
locked_tests: []
done_when:
  - python3 -m pytest tests/test_check_section.py -q && ruff check tools tests && bash -n draft/build.sh
review_required: false
---
# WP05 — Draft tooling: section lint and build script

Tier: code
Verify: python3 -m pytest tests/test_check_section.py -q && ruff check tools tests && bash -n draft/build.sh

Tools used by the draft WPs to verify prose deterministically.

## Deliverables

1. `tools/check_section.py FILE [--must TEXT]...`: exit 0 only if FILE exists, is nonempty, contains every `--must` string (case-sensitive), contains none of the four unresolved-marker words (the uppercase words spelled T,B,D; T,O,D,O; F,I,X,M,E; X,X,X — join the letters in code, and match them as whole words), no throughput claim words ("Gbps", "line rate achieved", "Mpps") outside a sentence containing "pending measurement", has balanced code fences, and its first non-blank line starts with `#`. Print one line per failure.
2. `draft/build.sh`: from `draft/`, run `kramdown-rfc draft-vladyka-schc-vxlan-fragmentation-00.md > out/<name>.xml`, then `xml2rfc --text --html out/<name>.xml`; `set -euo pipefail`; create `out/`. Do not run it in your tests (the main file does not exist yet); `bash -n` only.
3. Unit tests for each check_section rule (pass and fail).

## Owns (touch only these)

- `tools/check_section.py`
- `tests/test_check_section.py`
- `draft/build.sh`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp05.md` summarizing changes, verification output, and any non-obvious choices.
