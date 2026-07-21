from __future__ import annotations

import sys
import unittest
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import generate_manifest as manifest  # noqa: E402


class ManifestSortKeyTests(unittest.TestCase):
    def test_key_is_case_insensitive_first(self) -> None:
        self.assertLess(manifest.manifest_sort_key("Apple"), manifest.manifest_sort_key("banana"))
        self.assertLess(manifest.manifest_sort_key("apple"), manifest.manifest_sort_key("Banana"))

    def test_case_only_collision_is_broken_deterministically(self) -> None:
        # Paths differing only in case must be ordered by the exact original,
        # never left to filesystem-walk order (the reproducibility bug).
        self.assertNotEqual(
            manifest.manifest_sort_key("a/File.txt"),
            manifest.manifest_sort_key("a/file.txt"),
        )
        # Total order: uppercase 'F' (0x46) sorts before lowercase 'f' (0x66).
        self.assertLess(
            manifest.manifest_sort_key("a/File.txt"),
            manifest.manifest_sort_key("a/file.txt"),
        )

    def test_sorted_order_is_stable_regardless_of_input_order(self) -> None:
        members = ["b.txt", "A.txt", "a.txt", "B.txt", "dir/z.txt", "dir/Z.txt"]
        forward = sorted(members, key=manifest.manifest_sort_key)
        reversed_in = sorted(reversed(members), key=manifest.manifest_sort_key)
        self.assertEqual(forward, reversed_in)
        # Every distinct member has a distinct key => a genuine total order.
        keys = [manifest.manifest_sort_key(m) for m in members]
        self.assertEqual(len(set(keys)), len(members))


class ManifestEndToEndTests(unittest.TestCase):
    def test_manifest_line_order_is_deterministic_and_total(self) -> None:
        import tempfile

        # Distinct names even under case-folding, so this runs identically on
        # case-sensitive and case-insensitive filesystems. Case-only collisions
        # are pinned filesystem-independently by manifest_sort_key tests above.
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "root"
            (root / "dir").mkdir(parents=True)
            for name in ("Banana.txt", "apple.txt", "b.txt", "A.txt"):
                (root / name).write_text("x", encoding="utf-8")
            (root / "dir" / "z.txt").write_text("y", encoding="utf-8")

            paths = [p for p in root.rglob("*") if p.is_file()]
            order = sorted(
                paths,
                key=lambda p: manifest.manifest_sort_key(p.relative_to(root).as_posix()),
            )
            relatives = [p.relative_to(root).as_posix() for p in order]

            self.assertEqual(
                relatives,
                ["A.txt", "apple.txt", "b.txt", "Banana.txt", "dir/z.txt"],
            )


if __name__ == "__main__":
    unittest.main()
