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
        self.assertTrue(any("keys must be exactly" in error for error in errors))

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

    def test_empty_ledger_is_flagged(self) -> None:
        self.assertEqual(gate.check_text(""), ["ledger is empty"])

    def test_crlf_and_missing_final_lf_are_flagged(self) -> None:
        crlf_errors = gate.check_text(line() + "\r\n")
        self.assertTrue(any("line endings must be LF" in error for error in crlf_errors))
        no_final_lf_errors = gate.check_text(line())
        self.assertTrue(any("final LF" in error for error in no_final_lf_errors))

    def test_blank_line_is_flagged(self) -> None:
        errors = gate.check_text(line(id="E-0001") + "\n\n" + line(id="E-0002") + "\n")
        self.assertTrue(any("blank lines" in error for error in errors))

    def test_nonfinite_numbers_are_rejected(self) -> None:
        for token in ("NaN", "Infinity", "-Infinity"):
            with self.subTest(token=token):
                raw = line().replace('"claim":"c"', f'"claim":{token}') + "\n"
                self.assertTrue(any("invalid JSON" in error for error in gate.check_text(raw)))

    def test_keys_must_be_exact_and_canonical(self) -> None:
        reordered = '{"state":"confirmed","id":"E-0001","claim":"c","evidence":["e"],"check":"x"}\n'
        extra = line(extra="value") + "\n"
        for raw in (reordered, extra):
            with self.subTest(raw=raw):
                self.assertTrue(any("keys must be exactly" in error for error in gate.check_text(raw)))

    def test_required_field_types_are_checked(self) -> None:
        invalid_records = (
            line(state=1),
            line(claim=[]),
            line(evidence="e"),
            line(evidence=["e", 2]),
            line(check=None),
        )
        for raw in invalid_records:
            with self.subTest(raw=raw):
                self.assertNotEqual(gate.check_text(raw + "\n"), [])


class LedgerFormatRealFileTests(unittest.TestCase):
    def test_tracked_ledger_conforms(self) -> None:
        ledger = REPO_ROOT / "analysis" / "evidence" / "ledger.jsonl"
        self.assertEqual(gate.check_text(ledger.read_bytes().decode("utf-8")), [])


if __name__ == "__main__":
    unittest.main()
