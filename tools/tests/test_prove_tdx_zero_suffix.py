from __future__ import annotations

import copy
import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import prove_tdx_zero_suffix as proof  # noqa: E402


class TdxZeroSuffixProofTests(unittest.TestCase):
    def test_exact_expected_baseline_passes(self) -> None:
        self.assertTrue(proof.matches_expected_baseline(copy.deepcopy(proof.EXPECTED_PROOF)))

    def test_any_aggregate_mismatch_fails(self) -> None:
        for key in proof.EXPECTED_PROOF:
            with self.subTest(key=key):
                result = copy.deepcopy(proof.EXPECTED_PROOF)
                if key == "missing_bytes":
                    result[key]["16"] += 1
                else:
                    result[key] += 1
                self.assertFalse(proof.matches_expected_baseline(result))


if __name__ == "__main__":
    unittest.main()
