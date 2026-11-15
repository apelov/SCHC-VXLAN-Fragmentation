#!/usr/bin/env bash
# Builds the draft from draft/AlternativeDesign/draft-vladyka-schc-vxlan-fragmentation-00.md into draft/AlternativeDesign/out/.
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")"

name="draft-vladyka-schc-vxlan-fragmentation-00"

mkdir -p out

kramdown-rfc "${name}.md" > "out/${name}.xml"
xml2rfc --text --html "out/${name}.xml"
