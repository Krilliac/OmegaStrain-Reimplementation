from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import check_evidence_ledger as gate  # noqa: E402


REPOSITORY_LEDGER = (
    Path(__file__).resolve().parents[2] / "analysis" / "evidence" / "ledger.jsonl"
)


def record(identifier: str = "E-0001", state: str = "confirmed") -> dict[str, object]:
    return {
        "id": identifier,
        "state": state,
        "claim": "Synthetic public claim.",
        "evidence": ["tracked/generated-fixture.txt"],
        "check": "synthetic check",
    }


class EvidenceLedgerGateTests(unittest.TestCase):
    def validate_lines(self, lines: list[str]) -> list[str]:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "ledger.jsonl"
            path.write_text("\n".join(lines) + "\n", encoding="utf-8")
            return gate.validate_ledger(path)

    def validate_records(self, records: list[dict[str, object]]) -> list[str]:
        return self.validate_lines([json.dumps(item) for item in records])

    def test_repository_ledger_satisfies_the_public_contract(self) -> None:
        self.assertEqual(gate.validate_ledger(REPOSITORY_LEDGER), [])

    def test_generated_confirmed_and_rejected_sequences_pass(self) -> None:
        self.assertEqual(
            self.validate_records(
                [
                    record("E-0001", "confirmed"),
                    record("R-0001", "rejected"),
                    record("E-0002", "confirmed"),
                ]
            ),
            [],
        )

    def test_malformed_blank_and_non_object_records_fail_categorically(self) -> None:
        issues = self.validate_lines(["{", "", "[]"])
        self.assertTrue(any("not valid JSON" in issue for issue in issues))
        self.assertTrue(any("blank ledger records" in issue for issue in issues))
        self.assertTrue(any("must be a JSON object" in issue for issue in issues))

    def test_schema_and_field_types_are_closed(self) -> None:
        missing = record()
        del missing["check"]
        extra = record("E-0002")
        extra["unexpected"] = True
        invalid_values = record("E-0003")
        invalid_values.update({"claim": "", "evidence": [], "check": 7})
        issues = self.validate_records([missing, extra, invalid_values])
        self.assertEqual(
            sum("fields do not match" in issue for issue in issues),
            2,
        )
        self.assertTrue(any("claim must be" in issue for issue in issues))
        self.assertTrue(any("evidence must be" in issue for issue in issues))
        self.assertTrue(any("check must be" in issue for issue in issues))

    def test_ids_are_unique_nonzero_and_state_coupled(self) -> None:
        issues = self.validate_records(
            [
                record("E-0001", "rejected"),
                record("E-0001", "confirmed"),
                record("E-0003", "confirmed"),
                record("R-0000", "rejected"),
                record("X-0001", "confirmed"),
            ]
        )
        self.assertTrue(any("state does not match" in issue for issue in issues))
        self.assertTrue(any("duplicate record ID" in issue for issue in issues))
        self.assertTrue(any("ordinal must be nonzero" in issue for issue in issues))
        self.assertTrue(any("record ID is invalid" in issue for issue in issues))


if __name__ == "__main__":
    unittest.main()
