# Session

## Goal
Produce `draft-vladyka-schc-vxlan-fragmentation-00` (Standards Track, SCHC WG) via the autonomous pipeline, from `docs/design/DESIGN.md`.

## Status
- All 12 workpackages integrated; final gate passes (`pipeline.py drive` exit 0, manifest generation 7, `test_command` is `pytest -q`).
- Draft builds to `draft/out/` (XML, text, HTML). Final edits applied in `f8ec476`.
- A Fable reviewer was started on 2026-11-14; its findings had not arrived when this was written.

## Watchouts
- The authors have not read the draft yet. The statement about draft-ietf-schc-protocol-numbers (no assigned port, no UDP payload layout) came from a summary; check it. No RFC 3720 reference for CRC-32C (unverified). Acknowledgments are generic. idnits not run.
- Claude workers were set with `ANTHROPIC_MODEL` on the `drive` command; `launch.sh` has no `--model` flag. `launch.sh` carries local patches (selftest `pwd -P`, plus pipeline fixes from the other machine).
- pytest was installed into Homebrew Python 3.14's user site and then removed.
- Design open items: deadline-equality rule, Frame ID width, restart mechanism, port sharing, sliding-window retirement idea (not adopted).

## Next
Relay the reviewer's findings, then apply any agreed fixes to the draft sections.
