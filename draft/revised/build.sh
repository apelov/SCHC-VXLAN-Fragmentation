#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
name=draft-vladyka-schc-vxlan-fragmentation-00-revised
kramdown-rfc "$name.md" > "$name.xml"
xml2rfc --text --html "$name.xml"
