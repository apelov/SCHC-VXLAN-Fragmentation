#!/usr/bin/env bash
# Usage: check_docs.sh <config.c> <README.md>
# Fails if a configuration key from keys[] or globals[] in config.c is not in the README.
set -u
src=${1:?config.c path}
doc=${2:?README.md path}
keys=$(grep -E 'static const char \*const (keys|globals)\[\]' "$src" | grep -o '"[^"]*"' | tr -d '"' | sort -u)
if [ -z "$keys" ]; then echo "no keys found in $src" >&2; exit 1; fi
missing=
for k in $keys; do
  grep -q -F -- "\`$k\`" "$doc" || missing="$missing $k"
done
if [ -n "$missing" ]; then
  echo "keys missing from $doc (expected in backticks):$missing" >&2
  exit 1
fi
echo "all $(echo "$keys" | wc -l | tr -d ' ') keys documented"
