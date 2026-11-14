# Commands

## Install
None (Python 3 stdlib; pytest and ruff must be on PATH).

## Run
`python3 tools/gen_examples.py` — print worked examples from the reference model.

## Test
`python3 -m pytest -q`

## Lint
`ruff check src tests tools`

## Typecheck

## Build
`bash draft/build.sh` — kramdown-rfc -> XML, xml2rfc -> txt/html under `draft/out/`.
