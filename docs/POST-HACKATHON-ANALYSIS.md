# Post-hackathon analysis: the AI-assisted process used for SCHC VXLAN fragmentation

Scope: the work of 2026-11-14 and 2026-11-15 in this repository, as seen from the orchestrating Claude session. It covers the process (the AI2II setup: the `AGENTS.md`/`agentwork` template, multi-model delegation, human steering), not the protocol itself. Other sessions (Andrii's Rust work, the user's own Codex and Mistral runs, the debug session) are included only where their output reached this repository.

## 1. What happened, in order

| Phase | What was done | Tools / models |
| --- | --- | --- |
| Design input | User's Markdown design captured as `docs/design/DESIGN.md`; challenged (CRC, length field, PMTU, RuleID) before sealing | Claude (orchestrator) |
| Pipeline build of draft v1 | 12 work packages sealed and built by the `agentwork` pipeline: Python reference model + 6 draft sections + assembly | `pipeline.py drive`, Claude workers (Opus, then Fable, then Sonnet), Codex fallback |
| Review of draft v1 | Independent review; 3 blockers, 6 interoperability gaps, all fixed | Fable reviewer sub-agent |
| Redesign | Analysis of options A/B/C; move to a pure SCHC No-ACK profile over the VXLAN payload; revised draft with vectors and checker | Claude analysis; the revised draft written by the user with Codex |
| Option 1 | Every frame on the binding; unfragmented SCHC Packets | Claude |
| C implementation | 12 work packages from `impl-c/SPEC.md` | Codex (gpt-6-astra, then gpt-6.1-terra) until its token limit; Gemini Flash 3.8 tried; then Claude Sonnet sub-agents; Claude validated |
| Independent code review | 1 real defect and 4 test gaps | Mistral Vibe (run by the user) |
| Lab bring-up | TAP-only, then bridge with revert timer; C ↔ Rust interop; Wireshark dissector | Claude, debug session, user |
| Performance | Raw-socket buffer, table capacity, O(1) data structures, ECONNREFUSED retry: reverse 29 → 532 Mbit/s | Claude diagnosis, Sonnet fixes |

## 2. What worked

1. **A frozen design with explicit open items.** Writing "Open Issue" instead of deciding kept the drafts honest and gave reviewers clear targets. The rule "unspecified behaviour is not permission to invent" was respected by every coding agent; Codex even caught a mismatch between the spec and the draft (the S bound) and followed the draft.
2. **Draft first, then code.** Every behaviour question (oversized datagrams, deadline equality, retirement) was traced back to the draft text. That made the C and Rust implementations comparable.
3. **Shared wire vectors plus an executable checker.** `wire-vectors.json` and `check_vectors.py` were the interop contract. The C encoder matched byte for byte before the lab, and the first lab contact decoded correctly on the first try.
4. **Small work packages with a verify command, and verification on the real target.** Each package was checked by the orchestrator on the Mac and again on Linux. The Linux build caught what macOS clang did not (GCC `-Wmisleading-indentation`); the namespace test caught the veth checksum-offload trap before the lab.
5. **Cross-model review.** A different model reviewing the work found real defects every time: Fable on the draft (3 blockers), Mistral on the C code (oversized fragments destroyed in-progress state; missing receiver scenarios).
6. **The namespace integration test before the lab.** C-to-C end to end on one machine, touching no physical interface, separated our bugs from interop bugs.
7. **Capture-driven debugging with a neutral dissector.** Decoding fragment headers byte by byte located the morning's fragmentation failure on the Rust side; the dissector, written from the draft only, is a referee neither implementation owns.
8. **Safety habits on a shared lab server.** Read-only inspection first, host-key verification of the fallback path, runtime-only changes, a revert timer for the bridge, and explicit user approval before moving the SSH address. No lockout happened.
9. **Cross-session coordination.** Messages between this session and the debug session avoided conflicting changes and duplicate captures.

## 3. What hurt or was wasted

1. **The full `agentwork` pipeline for a first draft.** Sealing 12 packages, generations and manifests cost more than it returned: the sandbox blocked `/dev/null` and the gate picked a Python without pytest; local run state lived outside git, so a second machine saw different state; the final gate failed on an environment detail after all packages were accepted; seven seal generations in two days. Most important, the design was sealed **before** it was stable: half a day later it was replaced by option C, and the Python model and 6 sections became the "alternative design".
2. **Model and tool churn without a protocol.** Workers moved from Opus to Fable to Sonnet, developers from Codex to Gemini to Sonnet, partly by preference and partly by limits (Codex token exhaustion, Gemini CLI eligibility error, Codex sandbox rejecting `rm -rf` and then doing nothing). Each switch cost a restart and a re-explanation, and twice I chose a model the user did not want.
3. **Silent agent failures.** Codex exited 0 after writing no code (WP09); agy prints nothing until the end; one sub-agent reported fuzz runs it had not checked. Every delegated result had to be re-verified.
4. **Diagnosis from incomplete metrics.** I attributed the slow reverse direction to the Rust sender because there was no loss and no retransmission; the real cause was our CPU-bound receiver. Measuring CPU time first would have saved an iteration and a wrong message to Andrii.
5. **Dense code style from the first coding model.** Many statements per line passed clang and failed GCC, and made reviews harder.
6. **Independence weakened late.** Both implementations ended up written by Claude-family models (Rust by Andrii with Claude, C finished by Sonnet). The interop result is still meaningful because the wire contract and the dissector are independent, but the argument is weaker than planned.
7. **Path churn.** Moving the draft (`draft/revised/...-revised.md` → `draft/...-00.md`) left stale references in four documents until the close-out.

## 4. Useless or overbuilt for this kind of project

| Item | Verdict |
| --- | --- |
| Sealed DAG with generations and manifests for writing prose | Drop for drafts; keep only for large, stable code builds |
| Rungs and automatic agent escalation | Not useful here; the human picked models directly anyway |
| Goldfish audit | Marginal; validation of `owns`/`inputs` was useful, the rest was noise |
| `docs/design/DESIGN.md` as a sealed file | Useful as input, harmful once frozen; a design log works better |
| Four-section `CONTEXT.md`/`CONTEXT-lite.md` briefs | Barely used after the first build |
| Python reference model for the old design | Valuable for v1 examples, then stranded by the redesign |

## 5. What to add

1. **A tool-readiness check at session start:** for every model/CLI that may be used, a one-line smoke test (auth, model id, sandbox can build and write, exit code reflects failure). Today: Gemini CLI eligibility, Codex sandbox and token budget would all have shown up in the first minute.
2. **A model-routing decision up front,** written into the repo (who develops, who reviews, who validates), with a fallback order for token limits, so switches are cheap and expected.
3. **Linux CI** (GitHub Actions): GCC, sanitizers, the namespace test, `check_vectors.py`, the dissector against the committed captures. It would have caught the GCC and checksum issues without a lab server.
4. **Shared receiver scenarios** next to the wire vectors: timed event scripts (order, loss, duplicates, deadlines, retirement) that both implementations must pass, not only byte vectors.
5. **A performance checklist for every interop run:** throughput, daemon CPU time, kernel drops (`/proc/net/raw`, NIC), receiver counters, and a capture with a size limit; `perf top` before blaming the peer.
6. **A results template** (setup, versions, counters, captures, conclusion, corrections) so every run is recorded the same way.
7. **A handoff file updated at each milestone** (like `HANDOFF.md`), instead of relying on long session notes.
8. **A decision log owned by the humans** for protocol choices (option C, Option 1, Standards Track, mode name), separate from implementation decisions.
9. **Seal late:** only after an independent design review; or use a lighter "plan + checklist" for anything shorter than a few days.

## 6. Recommendations, in order of impact

1. Keep: draft-first, explicit Open Issues, shared vectors + checker, independent dissector, cross-model review, namespace test, safe lab procedures.
2. Add: tool-readiness checks, a written model-routing policy, Linux CI, shared receiver scenarios, a performance checklist.
3. Drop for drafting work: the sealed multi-agent pipeline; use small delegated packages with orchestrator validation instead (what actually produced the C implementation).
4. Protect independence deliberately: assign different model families per implementation and per reviewer, and record it in the interop plan.
