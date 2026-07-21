#!/usr/bin/env python3
"""Validate the public clean-room evidence ledger without opening cited evidence."""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path


REQUIRED_FIELDS = {"id", "state", "claim", "evidence", "check"}
ID_PATTERN = re.compile(r"(?P<prefix>[ER])-(?P<number>[0-9]{4})")
EXPECTED_STATE = {"E": "confirmed", "R": "rejected"}
DEFAULT_LEDGER = Path(__file__).resolve().parents[1] / "analysis" / "evidence" / "ledger.jsonl"


def validate_ledger(path: Path) -> list[str]:
    """Return categorical contract violations; cited paths are never opened."""
    issues: list[str] = []
    seen_ids: set[str] = set()
    record_count = 0

    try:
        stream = path.open("r", encoding="utf-8")
    except (OSError, UnicodeError):
        return ["evidence ledger could not be read as UTF-8"]

    with stream:
        try:
            for line_number, line in enumerate(stream, start=1):
                if not line.strip():
                    issues.append(f"line {line_number}: blank ledger records are forbidden")
                    continue
                try:
                    record = json.loads(line)
                except json.JSONDecodeError:
                    issues.append(f"line {line_number}: record is not valid JSON")
                    continue
                if not isinstance(record, dict):
                    issues.append(f"line {line_number}: record must be a JSON object")
                    continue
                if set(record) != REQUIRED_FIELDS:
                    issues.append(f"line {line_number}: record fields do not match the ledger schema")
                    continue

                record_count += 1
                identifier = record["id"]
                if not isinstance(identifier, str) or ID_PATTERN.fullmatch(identifier) is None:
                    issues.append(f"line {line_number}: record ID is invalid")
                    continue
                match = ID_PATTERN.fullmatch(identifier)
                assert match is not None
                prefix = match.group("prefix")
                number = int(match.group("number"))
                if number == 0:
                    issues.append(f"line {line_number}: record ID ordinal must be nonzero")
                if identifier in seen_ids:
                    issues.append(f"line {line_number}: duplicate record ID {identifier}")
                else:
                    seen_ids.add(identifier)

                state = record["state"]
                if state != EXPECTED_STATE[prefix]:
                    issues.append(f"line {line_number}: record state does not match its ID class")
                if not isinstance(record["claim"], str) or not record["claim"].strip():
                    issues.append(f"line {line_number}: claim must be a non-empty string")
                evidence = record["evidence"]
                if (
                    not isinstance(evidence, list)
                    or not evidence
                    or any(not isinstance(item, str) or not item.strip() for item in evidence)
                ):
                    issues.append(
                        f"line {line_number}: evidence must be a non-empty string array"
                    )
                if not isinstance(record["check"], str) or not record["check"].strip():
                    issues.append(f"line {line_number}: check must be a non-empty string")
        except UnicodeError:
            issues.append("evidence ledger could not be read as UTF-8")

    if record_count == 0:
        issues.append("evidence ledger contains no valid records")
    return issues


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("path", nargs="?", type=Path, default=DEFAULT_LEDGER)
    args = parser.parse_args()

    issues = validate_ledger(args.path)
    if issues:
        print(f"evidence-ledger gate: FAILED ({len(issues)} issue(s))")
        for issue in issues:
            print(f"- {issue}")
        return 1
    print("evidence-ledger gate: OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
