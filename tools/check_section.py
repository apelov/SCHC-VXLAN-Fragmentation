"""Deterministic lint for a single draft section file.

Usage:

    python3 tools/check_section.py FILE [--must TEXT]...

Exits 0 only if FILE exists, is nonempty, contains every ``--must`` string,
contains none of the unresolved-marker words, makes no throughput claim
outside a sentence that also says "pending measurement", has balanced code
fences, and its first non-blank line starts with ``#``. Prints one line per
failing check; prints nothing and exits 0 if every check passes.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import sys

_MARKER_RE = re.compile(r"\b(TBD|TODO|FIXME|XXX)\b")

_THROUGHPUT_PATTERNS = ("Gbps", "Mpps", "line rate achieved")
_PENDING_MEASUREMENT = "pending measurement"

# A sentence is a run of non-terminator characters ending in . ! or ?, or the
# final such run at end of text with no terminator.
_SENTENCE_RE = re.compile(r"[^.!?]*[.!?]|[^.!?]+$")


def check_nonempty(text: str) -> list[str]:
    if text == "":
        return ["file is empty"]
    return []


def check_must(text: str, musts: list[str]) -> list[str]:
    return [f"missing required text: {must!r}" for must in musts if must not in text]


def check_unresolved_markers(text: str) -> list[str]:
    found = sorted(set(_MARKER_RE.findall(text)))
    return [f"unresolved marker: {marker}" for marker in found]


def _sentences(text: str) -> list[str]:
    flat = " ".join(text.split())
    return [s.strip() for s in _SENTENCE_RE.findall(flat) if s.strip()]


def check_throughput_claims(text: str) -> list[str]:
    failures = []
    for sentence in _sentences(text):
        if _PENDING_MEASUREMENT in sentence:
            continue
        for pattern in _THROUGHPUT_PATTERNS:
            if pattern in sentence:
                failures.append(
                    f"throughput claim {pattern!r} outside a 'pending measurement' "
                    f"sentence: {sentence!r}"
                )
    return failures


def check_balanced_fences(text: str) -> list[str]:
    fence_lines = [line for line in text.splitlines() if line.strip().startswith("```")]
    if len(fence_lines) % 2 != 0:
        return ["unbalanced code fences: odd number of ``` lines"]
    return []


def check_first_line_heading(text: str) -> list[str]:
    for line in text.splitlines():
        if line.strip():
            if line.lstrip().startswith("#"):
                return []
            return ["first non-blank line does not start with '#'"]
    return ["file has no non-blank line"]


def check_file(path: pathlib.Path, musts: list[str]) -> list[str]:
    if not path.is_file():
        return [f"file does not exist: {path}"]

    text = path.read_text()
    failures: list[str] = []
    failures.extend(check_nonempty(text))
    failures.extend(check_must(text, musts))
    failures.extend(check_unresolved_markers(text))
    failures.extend(check_throughput_claims(text))
    failures.extend(check_balanced_fences(text))
    failures.extend(check_first_line_heading(text))
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="check_section.py")
    parser.add_argument("file", type=pathlib.Path)
    parser.add_argument("--must", action="append", default=[], dest="musts")
    args = parser.parse_args(argv)

    failures = check_file(args.file, args.musts)
    for failure in failures:
        print(failure)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
