---
id: WP13
name: draft-assemble
kind: implementation
tier: plan
rung: 3
depends: [WP06, WP07, WP08, WP09, WP10, WP11, WP12]
inputs:
  - draft/build.sh
  - docs/design/DESIGN.md
  - draft/sections/01-introduction.md
  - draft/sections/02-schc-relationship.md
  - draft/sections/03-wire-format.md
  - draft/sections/04-sender.md
  - draft/sections/05-receiver.md
  - draft/sections/06-timing-and-profile.md
  - draft/sections/07-security-iana.md
  - draft/sections/08-examples.md
outputs:
  - draft/draft-vladyka-schc-vxlan-fragmentation-00.md
owns:
  - draft/draft-vladyka-schc-vxlan-fragmentation-00.md
locked_tests: []
done_when:
  - bash draft/build.sh && grep -q "draft-vladyka-schc-vxlan-fragmentation-00" draft/out/draft-vladyka-schc-vxlan-fragmentation-00.xml
review_required: true
---
# WP13 — Assemble and build the Internet-Draft

Tier: plan
Verify: bash draft/build.sh && grep -q "draft-vladyka-schc-vxlan-fragmentation-00" draft/out/draft-vladyka-schc-vxlan-fragmentation-00.xml

Main kramdown-rfc file: front matter, abstract, includes, references; the build must succeed.

## Deliverables

1. Main file with kramdown-rfc YAML header: `title`, `docname: draft-vladyka-schc-vxlan-fragmentation-00`, `category: info`, `submissiontype: IETF`, `ipr: trust200902`, `area: Internet`, `workgroup: SCHC`, `keyword`, `stand_alone: yes`, `pi` options, authors Andrii Vladyka (email andrii.vladyka@gmail.com) and Alexander Pelov (email alexander.pelov@imt-atlantique.fr, organization IMT Atlantique), normative refs RFC2119, RFC8174, RFC7348, RFC8724, RFC8899 as kramdown-rfc `normative:`/`informative:` entries (use the bare-RFC shorthand the tool resolves, e.g. `RFC7348:`), a `--- abstract` and the eight section files via `{::include sections/NN-*.md}` in order. Add a short "Open Issues" summary section collecting every Open Issue from the included sections, and a `# Acknowledgments` placeholder-free sentence. Note in your report that `category: info` is a provisional choice for the authors.
2. The build (`draft/build.sh`) succeeds offline if kramdown-rfc can resolve references from its cache; if it cannot, say so in the report and include the failing output. Review required.

## Owns (touch only these)

- `draft/draft-vladyka-schc-vxlan-fragmentation-00.md`

Do **not** modify files outside `owns:`.

## Constraints / invariants

- Honor the hard invariants in `agentwork/CONTEXT.md` and `agentwork/state/PROJECT.md`; the design authority is `docs/design/DESIGN.md`.
- Commit locally on your worktree branch; **never push**.
- Write `agentwork/reports/wp13.md` summarizing changes, verification output, and any non-obvious choices.
