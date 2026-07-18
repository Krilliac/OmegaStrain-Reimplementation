from __future__ import annotations

import sys
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import check_public_tree as gate  # noqa: E402


class PublicTreeGateTests(unittest.TestCase):
    def errors(self, path: str, data: bytes = b"safe text\n", mode: str = "100644") -> list[str]:
        blob = gate.TrackedBlob(mode=mode, object_id="synthetic", path=Path(path))
        with mock.patch.object(gate, "read_blob", return_value=data):
            return gate.check_blob(blob)

    def test_safe_text_file_passes(self) -> None:
        self.assertEqual(self.errors("native/src/example.cpp"), [])

    def test_private_and_generated_roots_are_blocked(self) -> None:
        self.assertTrue(any("blocked root" in error for error in self.errors("private/input.txt")))
        self.assertTrue(
            any("blocked root" in error for error in self.errors("analysis/output/result.json"))
        )

    def test_payload_extensions_and_boot_names_are_blocked(self) -> None:
        self.assertTrue(any("payload extension" in error for error in self.errors("fixture.iso")))
        for extension in (".ska", ".skas"):
            with self.subTest(extension=extension):
                self.assertTrue(
                    any(
                        "payload extension" in error
                        for error in self.errors(f"fixture{extension}")
                    )
                )
        self.assertTrue(
            any("retail executable name" in error for error in self.errors("SCUS_972.64"))
        )

    def test_sensitive_names_and_git_modes_are_blocked(self) -> None:
        self.assertTrue(any("sensitive filename" in error for error in self.errors(".env.local")))
        self.assertTrue(
            any("unsupported Git mode" in error for error in self.errors("link", mode="120000"))
        )

    def test_binary_and_secret_content_are_blocked(self) -> None:
        self.assertTrue(any("binary/NUL" in error for error in self.errors("data.txt", b"a\0b")))
        self.assertTrue(
            any("non-UTF-8" in error for error in self.errors("data.txt", b"\xff\xfe"))
        )
        self.assertTrue(
            any(
                "Git LFS pointer" in error
                for error in self.errors(
                    "opaque.dat", b"version https://git-lfs.github.com/spec/v1\n"
                )
            )
        )
        synthetic_token = b"gh" + b"p_" + (b"A" * 24)
        self.assertTrue(
            any("GitHub token" in error for error in self.errors("notes.txt", synthetic_token))
        )


if __name__ == "__main__":
    unittest.main()
