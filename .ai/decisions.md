# Decisions

Record durable project decisions here. One entry per decision, newest at the top.

## 2026-11-14: Claude and Codex only; selftest path fix

**Context:** Human asked to use Claude and Codex. `doctor` failed because `launch.sh --selftest` used a `/var/...` temp path that seatbelt does not match (real path is `/private/var/...`).
**Decision:** Keep `routing_profile: sensitive`; in `agents.yaml` restrict `sensitive` to codex/claude and block agy, gemini, pi-code, claude-local. Patched `launch.sh` selftest to resolve `TEST_DIR` with `pwd -P`.
**Consequences:** `launch.sh` differs from the installed template (reapply on reinstall). No `approved_destinations` configured; pipeline.py has no handling for it.

## YYYY-MM-DD: Title

**Context:** What problem or choice prompted this.
**Decision:** What was chosen.
**Consequences:** Trade-offs, follow-ups, things to revisit.
