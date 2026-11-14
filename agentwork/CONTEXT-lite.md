# SCHC-VXLAN-Fragmentation — Shared Agent Brief (lite)

You are a coding agent working on **SCHC-VXLAN-Fragmentation**, a Python reference model for an
IETF draft on Ethernet fragmentation over VXLAN. Do the work package that follows.

## The vocabulary

- `docs/design/DESIGN.md` — frozen design; read the wire format and algorithms first.
- `src/schcvx/wire.py` — header codec and constants (once it exists).

## Where your work goes

- `src/schcvx/`, `tests/`, `tools/`, `draft/`.

## Rules

- Touch **only** the files your package lists under "Owns".
- Do not change shared types or public interfaces. Report problems instead.
- Python 3 stdlib only. Deterministic: no wall-clock, no randomness (inject clocks).

## How to verify

```sh
python3 -m pytest -q
ruff check src tests tools
```

Both must pass before you are done.

## Workspace discipline (fixed — do not change)

You are on a dedicated git branch/worktree. Commit locally as you finish
coherent units (`feat(...)`, `fix(...)`). **Never push.** Touch only the paths
your package declares. Finish by writing `agentwork/reports/<wp-id>.md`: what
you changed, how you verified it (paste test / check output), and anything the
reviewer must confirm live.
