# SCHC-VXLAN-Fragmentation — Shared Agent Brief

You are a coding/writing agent working on **SCHC-VXLAN-Fragmentation**, an IETF Internet-Draft
(`draft-vladyka-schc-vxlan-fragmentation-00`) plus a Python reference model. This brief is
prepended to every work-package prompt. Read it, then do the work package that follows.

## What this project is (and its north star)

A draft for the SCHC WG specifying deadline-bounded Ethernet fragmentation for VXLAN, with no
ACK/retransmission, bounded receiver state and reorder tolerance.

1. **Fidelity to `docs/design/DESIGN.md`** — wire format, algorithms and numbers are as written there.
2. **Honest openness** — items listed under "Items to settle" are marked open, not decided.
3. **Checkable examples** — all numbers are reproducible by the reference model (`src/schcvx/`).

## Layout

- `docs/design/DESIGN.md` — frozen design (read first; protected).
- `src/schcvx/` — reference model (Python 3, stdlib only, no third-party runtime deps).
- `tests/` — pytest tests. `tools/` — helper scripts.
- `draft/sections/NN-*.md` — draft sections in kramdown-rfc markdown (no front matter, no `#` title line other than section headings starting at `#`).
- `draft/` — main draft file, `build.sh`, `generated/`.

## How to run + verify

```sh
python3 -m pytest -q
ruff check src tests tools
bash draft/build.sh        # only once the main draft exists
python3 tools/check_section.py <section.md> --must "<text>"   # draft section lint
```

**Sandbox note:** if you cannot reach the network, do what you can offline and say so in your
report. Do not cite RFC text you cannot verify; cite section numbers given in DESIGN.md.

## Hard invariants

- Wire format: 14-byte header (VNI 3, RuleID 1, Frame ID 8, index 1, count 1), then slice; 4-byte CRC-32C RCS (big-endian) on the final fragment only. Network byte order.
- Slice budget `C = PMTU - H_IP - 8 - 14 - 4`; same C for every fragment; `N = ceil(len / C)`.
- Receiver never delivers a frame after expiry; expired entries are ineligible even before a sweep.
- Draft prose uses BCP 14 keywords only where interoperability requires; never claims throughput.
- Draft section files contain no `TBD`/`TODO`; open items are written as explicit "Open Issue" text.
- The project's checks stay green; run every check your change could affect.
- Unspecified behavior is not permission to invent. Report blocking unresolved semantics; see
  `.ai/workflows/design-exploration.md`.

## Workspace discipline (fixed — do not change)

You are on a dedicated git branch/worktree. Commit locally as you finish
coherent units (`feat(...)`, `fix(...)`). **Never push.** Touch only the paths
your package declares. Finish by writing `agentwork/reports/<wp-id>.md`: what
you changed, how you verified it (paste test / check output), and anything the
reviewer must confirm live.
