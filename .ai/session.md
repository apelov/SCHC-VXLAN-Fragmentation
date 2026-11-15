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

## Revised C review proposal (2026-11-14)
- Added docs/design/REVISED-C-PROPOSAL.md; authority/plan in .ai/plans/revised-c-design.md. Frozen design and implementation unchanged.
- Proposed four-bit first/countdown encoding excludes All-1 from countdown, supports 2..8 fragments; 60-bit DTag packing and direct NoCompress binding remain provisional.
- Corrected standards claims, restart/drain/retirement timing, early VNI admission and capacity arithmetic. Preserved inherited CRC-32C decision.
- Exhaustively checked encoding/reconstruction across all permutations for K=2..8 and checked PMTU examples. Independent semantic review remains outstanding.
- Existing untracked WIRE-OPTIONS.md was preserved and excluded from checkpoint.

- User clarified configurable DTag: proposed compact 4-bit RuleID, 32-bit DTag, 4-bit FCN; 60-bit DTag is an extended variant. Updated size examples.
- Extended variant clarified: 8-bit RuleID, 60-bit DTag, 4-bit FCN; variant must be known before parsing.

## Revised draft variant
- Added standalone draft/revised/draft-vladyka-schc-vxlan-fragmentation-00-revised.md and build script; original technical sections preserved in appendix. Original draft and reference model remain unchanged.
- Main profile describes RFC 8724 ordered receiver; reordering-tolerant receiver is explicitly an extension with standards status open. Direct NoCompress mapping and RFC 7348 interpretation remain open.
- Built XML, text and HTML using kramdown-rfc and xml2rfc. idnits unavailable; semantic review is not independent, and old model applies to appendix only.

## Revised draft review repairs
- User authorized all review fixes. Added explicit 8-bit NoCompress C/D RuleID; retained compact/extended F/R layouts. Updated CRC coverage, MTU examples and first-fragment parsing.
- Defined emission bound S, delivery bound B, arrival span W=S+B, separate ordered/tolerant lifetime bounds, retirement and restart drain procedures. Clarified validation, association lookup and informative appendix.
- Added standard-library wire checker and JSON vectors; all permutations K=2..8, loss, duplicates, corrupt and malformed cases pass. XML/text/HTML build passes. Existing pytest suite unavailable (pytest not installed); no production timed receiver or independent semantic validation claimed.
