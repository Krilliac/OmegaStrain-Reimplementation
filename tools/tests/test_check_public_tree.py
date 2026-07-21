from __future__ import annotations

import contextlib
import io
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import check_public_tree as gate  # noqa: E402


class PublicTreeGateTests(unittest.TestCase):
    @staticmethod
    def invoke_main() -> tuple[int, str, str]:
        stdout = io.StringIO()
        stderr = io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            result = gate.main()
        return result, stdout.getvalue(), stderr.getvalue()

    def errors(self, path: str, data: bytes = b"safe text\n", mode: str = "100644") -> list[str]:
        blob = gate.TrackedBlob(mode=mode, object_id="synthetic", path=Path(path))
        with mock.patch.object(gate, "read_blob", return_value=data):
            return gate.check_blob(blob)

    def test_main_reports_success_for_one_safe_blob(self) -> None:
        object_id = "a" * 40
        inventory = subprocess.CompletedProcess(
            args=[],
            returncode=0,
            stdout=f"100644 {object_id} 0\tnative/src/example.cpp\0".encode(),
        )
        contents = subprocess.CompletedProcess(
            args=[], returncode=0, stdout=b"safe text\n"
        )
        with mock.patch.object(gate.subprocess, "run", side_effect=[inventory, contents]):
            result, stdout, stderr = self.invoke_main()

        self.assertEqual(result, 0)
        self.assertEqual(stdout, "public-tree gate: OK (1 indexed text blobs checked)\n")
        self.assertEqual(stderr, "")

    def test_main_classifies_missing_git_without_leaking_exception_text(self) -> None:
        private_detail = r"C:\private\owner-disc.iso"
        with mock.patch.object(
            gate.subprocess, "run", side_effect=FileNotFoundError(private_detail)
        ):
            result, stdout, stderr = self.invoke_main()

        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            "public-tree gate: FAILED (1 issue(s))\n"
            "- Git repository could not be inspected safely\n",
        )
        self.assertEqual(stderr, "")
        self.assertNotIn(private_detail, stdout)

    def test_main_classifies_blob_read_failure_without_leaking_git_stderr(self) -> None:
        object_id = "b" * 40
        inventory = subprocess.CompletedProcess(
            args=[],
            returncode=0,
            stdout=f"100644 {object_id} 0\tnative/src/example.cpp\0".encode(),
        )
        private_detail = b"fatal: private owner path C:/private/owner-disc.iso"
        failure = subprocess.CalledProcessError(
            returncode=128,
            cmd=["git", "cat-file"],
            stderr=private_detail,
        )
        with mock.patch.object(gate.subprocess, "run", side_effect=[inventory, failure]):
            result, stdout, stderr = self.invoke_main()

        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            "public-tree gate: FAILED (1 issue(s))\n"
            "- Git repository could not be inspected safely\n",
        )
        self.assertEqual(stderr, "")
        self.assertNotIn(private_detail.decode(), stdout)

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

    def test_retail_executable_name_is_blocked_even_with_extra_suffix(self) -> None:
        # A suffixed disc-executable name (e.g. re-encoded as text) previously
        # slipped past the anchored fullmatch; a substring search catches it.
        for name in ("SCUS_972.64", "SLUS_200.73.txt", "dump-SCES_502.24.json"):
            with self.subTest(name=name):
                self.assertTrue(
                    any("retail executable name" in error for error in self.errors(name))
                )

    def test_absolute_owner_home_paths_are_blocked(self) -> None:
        # Assemble each leak at runtime so THIS test file (itself scanned by the
        # gate) contains no contiguous literal owner path. Splitting immediately
        # after the drive prefix / before the name segment keeps the file bytes
        # clean while the concatenated runtime value still trips the scanner.
        drive = b"C:"
        leaks = [
            b"see " + drive + rb"\Users\alice\project\file.txt",
            b"path is " + drive + b"/Users" + b"/bob/checkout",
            b"log at /home" + b"/carol/build.log",
            b"macos /Users" + b"/dave/dev",
        ]
        for leak in leaks:
            with self.subTest(leak=leak):
                self.assertTrue(
                    any("owner-home path" in error for error in self.errors("notes.md", leak))
                )

    def test_owner_home_paths_are_blocked_case_insensitively(self) -> None:
        # Windows paths are case-insensitive, so lowercase/uppercase variants
        # must still be caught.
        drive = b"C:"
        for leak in (drive + rb"\users\eve\x", drive + rb"\USERS\eve\x", b"at /HOME" + b"/frank/y"):
            with self.subTest(leak=leak):
                self.assertTrue(
                    any("owner-home path" in error for error in self.errors("notes.md", leak))
                )

    def test_escaped_and_unicode_owner_home_paths_are_blocked(self) -> None:
        drive = b"C:"
        escaped = b'{"path":"' + drive + rb"\\Users\\alice\\repo" + b'"}'
        unicode_unix = ("at /home" + "/élise/repo").encode("utf-8")
        unicode_windows = drive + (r"\Users\élise\repo").encode("utf-8")
        embedded_windows = b"prefix" + drive + rb"\Users\alice\repo"
        unix_home = b"/home" + b"/alice/repo"
        macos_home = b"/Users" + b"/alice/repo"
        file_uri_unix = b"file://" + unix_home
        file_uri_macos = b"file://" + macos_home
        short_file_uri = b"file:" + unix_home
        for leak in (
            escaped,
            unicode_unix,
            unicode_windows,
            embedded_windows,
            file_uri_unix,
            file_uri_macos,
            short_file_uri,
        ):
            with self.subTest(leak=leak):
                self.assertTrue(
                    any("owner-home path" in error for error in self.errors("notes.md", leak))
                )

    def test_urls_and_dot_segments_are_not_owner_home_paths(self) -> None:
        drive = b"C:"
        safe = (
            b"https://example.test/home/docs",
            b"https://example.test/Users/docs",
            drive + rb"\Users\..\Windows",
            drive + rb"\Users\.\Windows",
            b"relative/home/alice",
        )
        for value in safe:
            with self.subTest(value=value):
                self.assertEqual(
                    [e for e in self.errors("docs/note.md", value) if "owner-home path" in e],
                    [],
                )

    def test_earlier_url_does_not_mask_later_owner_home_path(self) -> None:
        owner_home = b"/home" + b"/alice/repo"
        compact_values = (
            b'{"url":"https://example.test","path":"' + owner_home + b'"}',
            b"url=https://example.test&path=" + owner_home,
            b"url=https://example.test/path=" + owner_home,
            b"cwd:" + owner_home,
            b"path:" + owner_home,
        )
        for compact in compact_values:
            with self.subTest(compact=compact):
                self.assertTrue(
                    any("owner-home path" in error for error in self.errors("record.jsonl", compact))
                )

    def test_generic_home_placeholders_are_not_blocked(self) -> None:
        for safe in (
            b"documented under <user>/OpenOmega",
            b"resolved from $HOME/.config",
            b"%LOCALAPPDATA%/OpenOmega/native-save",
            b"C:/Users/<user>/OpenOmega",
            b"C:/Users/%USERNAME%/OpenOmega",
            b"documented as `/home/<name>`",
            b"the private/ and runtime/ roots stay ignored",
        ):
            with self.subTest(safe=safe):
                self.assertEqual(
                    [e for e in self.errors("docs/note.md", safe) if "owner-home path" in e],
                    [],
                )


if __name__ == "__main__":
    unittest.main()
