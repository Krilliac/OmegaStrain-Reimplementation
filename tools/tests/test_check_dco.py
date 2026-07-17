from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import check_dco as dco  # noqa: E402


class DcoGateTests(unittest.TestCase):
    def test_matching_author_signoff_passes(self) -> None:
        def fake_git(*arguments: str) -> str:
            if arguments[0] == "show":
                return "Example Author\0author@example.invalid\0Subject\n\nSigned-off-by: Example Author <author@example.invalid>\n"
            if arguments[0] == "rev-parse":
                return "123456789abc\n"
            self.fail(f"unexpected Git call: {arguments}")

        with mock.patch.object(dco, "git", side_effect=fake_git):
            self.assertIsNone(dco.check_commit("synthetic"))

    def test_mismatched_signoff_fails(self) -> None:
        def fake_git(*arguments: str) -> str:
            if arguments[0] == "show":
                return "Example Author\0author@example.invalid\0Subject\n\nSigned-off-by: Other Person <other@example.invalid>\n"
            if arguments[0] == "rev-parse":
                return "123456789abc\n"
            self.fail(f"unexpected Git call: {arguments}")

        with mock.patch.object(dco, "git", side_effect=fake_git):
            error = dco.check_commit("synthetic")
        self.assertIsNotNone(error)
        self.assertIn("expected 'Signed-off-by: Example Author", error or "")

    def test_revision_range_excludes_merge_commits(self) -> None:
        with mock.patch.object(dco, "git", return_value="one\ntwo\n") as git:
            self.assertEqual(dco.commits("base..head"), ["one", "two"])
        git.assert_called_once_with(
            "rev-list", "--reverse", "--no-merges", "base..head", "--"
        )


if __name__ == "__main__":
    unittest.main()
