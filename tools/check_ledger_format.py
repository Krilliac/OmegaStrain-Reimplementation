#!/usr/bin/env python3
"""Enforce a stable line format for the evidence ledger.

The evidence ledger (`analysis/evidence/ledger.jsonl`) is authoritative and is
read mechanically. This gate keeps it byte-consistent so a reader or tool never
has to normalise it: every non-empty line must be valid JSON, serialised in
compact form (no incidental whitespace), carry the required record keys, and use
a unique, well-formed identifier. It enforces *format only* and never inspects
claim content; historical identifier gaps (e.g. a retracted entry) are allowed.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


DEFAULT_LEDGER = Path("analysis/evidence/ledger.jsonl")
REQUIRED_KEYS = {"id", "state", "claim", "evidence", "check"}
# Evidence entries use ``E-####``; other single-letter prefixes are reserved for
# adjacent record kinds already present in the ledger (e.g. ``R-####``).
ID_PATTERN = re.compile(r"^[A-Z]-\d{4}$")


def compact(record: object) -> str:
    return json.dumps(record, separators=(",", ":"), ensure_ascii=False)


def check_text(text: str) -> list[str]:
    errors: list[str] = []
    seen_ids: set[str] = set()
    for number, raw in enumerate(text.split("\n"), start=1):
        if raw == "":
            continue
        try:
            record = json.loads(raw)
        except json.JSONDecodeError as error:
            errors.append(f"line {number}: invalid JSON ({error})")
            continue
        if not isinstance(record, dict):
            errors.append(f"line {number}: record is not a JSON object")
            continue
        if raw != compact(record):
            errors.append(f"line {number}: not compact single-line JSON (re-serialise with separators=(',',':'))")
        missing = REQUIRED_KEYS - record.keys()
        if missing:
            errors.append(f"line {number}: missing required key(s) {sorted(missing)}")
        identifier = record.get("id")
        if not isinstance(identifier, str) or not ID_PATTERN.fullmatch(identifier):
            errors.append(f"line {number}: id must match [A-Z]-#### (got {identifier!r})")
        elif identifier in seen_ids:
            errors.append(f"line {number}: duplicate id {identifier}")
        else:
            seen_ids.add(identifier)
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--path", type=Path, default=DEFAULT_LEDGER)
    args = parser.parse_args()
    if not args.path.is_file():
        print(f"ledger-format gate: FAILED (no such file: {args.path})")
        return 1
    text = args.path.read_text(encoding="utf-8")
    errors = check_text(text)
    if errors:
        print(f"ledger-format gate: FAILED ({len(errors)} issue(s))")
        for error in errors:
            print(f"- {error}")
        return 1
    records = sum(1 for line in text.split("\n") if line)
    print(f"ledger-format gate: OK ({records} records checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
