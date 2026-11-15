# Revised C proposal work record

Authorization: user requested analysis, corrections, and a Markdown revised solution. Scope is a review proposal, not implementation or replacement of frozen DESIGN.md.

Plan: inspect existing design and wire options; verify RFC constraints; write self-contained proposal; check encoding and capacity arithmetic; update session; checkpoint only owned changes.

## Design tree and authority

- A1 [F] AXIOM HIGH: option C with default NoCompress; source: current conversation.
- A2 [F] AXIOM HIGH: distinguish pure and tolerant receivers; source: user acceptance of two possibilities.
- C1 [~] CHOICE HIGH: countdown excludes All-1, supporting eight fragments; origin: agent correction; impact: wire semantics; parent A2.
- C2 [~] CHOICE HIGH: 60-bit DTag packed with four-bit FCN; origin: agent alignment proposal; impact: wire layout; parent A1.
- C3 [~] CHOICE HIGH: direct payload binding without nested C/D RuleID; origin: agent proposal; impact: SCHC mapping; parent A1.
- A3 [F] AXIOM HIGH: preserve CRC-32C pending explicit revision; source: frozen DESIGN.md.
- Q1 [?] QUESTION HIGH: standards vehicle for tolerant behavior; origin: RFC comparison; parent A2.
- S1 [~] ASSUMPTION HIGH: bounded residual packet lifetime B; origin: restart analysis; impact: reuse/replay guarantees; parent A2.

No new protocol behavior is approved for implementation. Independent semantic validation remains a human/WG review gap. Verification covers proposal arithmetic and exhaustive encoding/permutation consistency only, not implementation tests.
