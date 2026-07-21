#!/usr/bin/env python3
"""Enforce a stable line format for the evidence ledger.

The evidence ledger (`analysis/evidence/ledger.jsonl`) is authoritative and is
read mechanically. This gate keeps it byte-consistent so a reader or tool never
has to normalise it: the non-empty file must be UTF-8 with LF endings and a final
newline, and every line must be strict JSON in the canonical key order and
compact form. Required fields have stable types and identifiers are unique and
well formed. It enforces structure only and never evaluates claim content;
historical identifier gaps (e.g. a retracted entry) are allowed.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


DEFAULT_LEDGER = Path("analysis/evidence/ledger.jsonl")
REQUIRED_KEY_ORDER = ("id", "state", "claim", "evidence", "check")
ALLOWED_STATES = {"confirmed", "rejected"}
# Evidence entries use ``E-####``; other single-letter prefixes are reserved for
# adjacent record kinds already present in the ledger (e.g. ``R-####``).
ID_PATTERN = re.compile(r"^[A-Z]-\d{4}$")


def compact(record: object) -> str:
    return json.dumps(record, separators=(",", ":"), ensure_ascii=False)


def reject_nonfinite_json(token: str) -> None:
    raise ValueError(f"non-finite number {token} is not valid JSON")


def check_text(text: str) -> list[str]:
    errors: list[str] = []
    if not text:
        return ["ledger is empty"]
    if text.startswith("\ufeff"):
        errors.append("UTF-8 BOM is not allowed")
    if "\r" in text:
        errors.append("line endings must be LF, not CR or CRLF")
    if not text.endswith("\n"):
        errors.append("ledger must end with a final LF newline")

    seen_ids: set[str] = set()
    lines = text.split("\n")
    if lines[-1] == "":
        lines.pop()
    for number, raw in enumerate(lines, start=1):
        if not raw:
            errors.append(f"line {number}: blank lines are not allowed")
            continue
        try:
            record = json.loads(raw, parse_constant=reject_nonfinite_json)
        except (json.JSONDecodeError, ValueError) as error:
            errors.append(f"line {number}: invalid JSON ({error})")
            continue
        if not isinstance(record, dict):
            errors.append(f"line {number}: record is not a JSON object")
            continue
        if raw != compact(record):
            errors.append(f"line {number}: not compact single-line JSON (re-serialise with separators=(',',':'))")
        if tuple(record) != REQUIRED_KEY_ORDER:
            errors.append(
                f"line {number}: keys must be exactly {list(REQUIRED_KEY_ORDER)} in that order"
            )
        identifier = record.get("id")
        if not isinstance(identifier, str) or not ID_PATTERN.fullmatch(identifier):
            errors.append(f"line {number}: id must match [A-Z]-#### (got {identifier!r})")
        elif identifier in seen_ids:
            errors.append(f"line {number}: duplicate id {identifier}")
        else:
            seen_ids.add(identifier)
        state = record.get("state")
        if not isinstance(state, str) or state not in ALLOWED_STATES:
            errors.append(
                f"line {number}: state must be one of {sorted(ALLOWED_STATES)} (got {state!r})"
            )
        claim = record.get("claim")
        if not isinstance(claim, str) or not claim:
            errors.append(f"line {number}: claim must be a non-empty string")
        evidence = record.get("evidence")
        if (
            not isinstance(evidence, list)
            or any(not isinstance(item, str) or not item for item in evidence)
        ):
            errors.append(f"line {number}: evidence must be a list of non-empty strings")
        check = record.get("check")
        if not isinstance(check, str) or not check:
            errors.append(f"line {number}: check must be a non-empty string")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--path", type=Path, default=DEFAULT_LEDGER)
    args = parser.parse_args()
    if not args.path.is_file():
        print(f"ledger-format gate: FAILED (no such file: {args.path})")
        return 1
    try:
        text = args.path.read_bytes().decode("utf-8")
    except UnicodeDecodeError as error:
        print(f"ledger-format gate: FAILED (ledger is not strict UTF-8: {error})")
        return 1
    errors = check_text(text)
    if errors:
        print(f"ledger-format gate: FAILED ({len(errors)} issue(s))")
        for error in errors:
            print(f"- {error}")
        return 1
    records = text.count("\n")
    print(f"ledger-format gate: OK ({records} records checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
