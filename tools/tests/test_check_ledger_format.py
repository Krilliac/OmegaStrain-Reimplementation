from __future__ import annotations

import json
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO_ROOT))
from tools import check_ledger_format as gate  # noqa: E402


def line(**fields: object) -> str:
    record = {
        "id": fields.pop("id", "E-0001"),
        "state": fields.pop("state", "confirmed"),
        "claim": fields.pop("claim", "c"),
        "evidence": fields.pop("evidence", []),
        "check": fields.pop("check", "x"),
    }
    record.update(fields)
    return gate.compact(record)


class LedgerFormatCheckTextTests(unittest.TestCase):
    def test_conforming_lines_pass(self) -> None:
        text = "\n".join([line(id="E-0001"), line(id="E-0002")]) + "\n"
        self.assertEqual(gate.check_text(text), [])

    def test_noncompact_line_is_flagged(self) -> None:
        record = json.loads(line(id="E-0003"))
        spaced = json.dumps(record, separators=(", ", ": "))
        errors = gate.check_text(spaced + "\n")
        self.assertTrue(any("not compact" in error for error in errors))

    def test_invalid_json_is_flagged(self) -> None:
        self.assertTrue(any("invalid JSON" in error for error in gate.check_text("{not json\n")))

    def test_missing_required_key_is_flagged(self) -> None:
        record = json.loads(line(id="E-0004"))
        del record["check"]
        errors = gate.check_text(gate.compact(record) + "\n")
        self.assertTrue(any("missing required key" in error for error in errors))

    def test_bad_id_format_is_flagged(self) -> None:
        self.assertTrue(any("id must match" in error for error in gate.check_text(line(id="E-12") + "\n")))

    def test_reserved_non_e_prefix_is_allowed(self) -> None:
        # The ledger already carries an ``R-####`` record alongside ``E-####``.
        self.assertEqual(gate.check_text(line(id="R-0001") + "\n"), [])

    def test_duplicate_id_is_flagged(self) -> None:
        text = line(id="E-0005") + "\n" + line(id="E-0005") + "\n"
        self.assertTrue(any("duplicate id" in error for error in gate.check_text(text)))

    def test_historical_id_gaps_are_allowed(self) -> None:
        text = "\n".join([line(id="E-0011"), line(id="E-0013")]) + "\n"
        self.assertEqual(gate.check_text(text), [])


class LedgerFormatRealFileTests(unittest.TestCase):
    def test_tracked_ledger_conforms(self) -> None:
        ledger = REPO_ROOT / "analysis" / "evidence" / "ledger.jsonl"
        self.assertEqual(gate.check_text(ledger.read_text(encoding="utf-8")), [])


if __name__ == "__main__":
    unittest.main()
