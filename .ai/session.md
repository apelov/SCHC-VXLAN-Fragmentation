# Session

## Goal
Produce `draft-vladyka-schc-vxlan-fragmentation-00` (Standards Track, SCHC WG) via the autonomous pipeline, from `docs/design/DESIGN.md`.

## Status
- Design and 12 workpackages sealed (generation 1) on 2026-11-14.
- Build started with `ANTHROPIC_MODEL=claude-opus-5-5 python3 agentwork/pipeline.py drive`.

## Watchouts
- Claude workers use Opus 5.5 only through the `ANTHROPIC_MODEL` environment variable on the `drive` command; `launch.sh` has no `--model` flag. Rerun `drive` with the same variable.
- Codex workers run with a fresh `CODEX_HOME`, so they use the Codex built-in default model, not `~/.codex/config.toml`.
- `launch.sh` selftest carries a local patch (`pwd -P`); see `.ai/decisions.md`.
- After the build: verify the draft's RFC references and review `draft/out/`.

## Next
Inspect the `drive` exit code and report (Phase 4).
