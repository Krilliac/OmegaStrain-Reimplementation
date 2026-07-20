from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import measure_member_structural_fingerprint as fp  # noqa: E402
from tools.measure_frontend_hog_topology import HARD_LIMITS, ScanFailure, ScanLimits  # noqa: E402


def make_hog(entries: list[tuple[str, bytes]], trailing_pad: int = 0) -> bytes:
    names = b"".join(name.encode("ascii") + b"\0" for name, _payload in entries)
    offsets_offset = 0x14
    names_offset = offsets_offset + 4 * (len(entries) + 1)
    data_offset = names_offset + len(names)
    offsets = [0]
    for _name, payload in entries:
        offsets.append(offsets[-1] + len(payload))
    data = bytearray(data_offset + offsets[-1] + trailing_pad)
    struct.pack_into(
        "<5I", data, 0, 0x12345678, len(entries), offsets_offset, names_offset, data_offset
    )
    struct.pack_into(f"<{len(offsets)}I", data, offsets_offset, *offsets)
    data[names_offset:data_offset] = names
    cursor = data_offset
    for _name, payload in entries:
        data[cursor : cursor + len(payload)] = payload
        cursor += len(payload)
    return bytes(data)


def write_root(directory: Path, blob: bytes, name: str = "ROOT.HOG") -> Path:
    path = directory / name
    path.write_bytes(blob)
    return path


class StructuralFingerprintTests(unittest.TestCase):
    def scan_blob(self, blob: bytes, **limit_kwargs) -> dict:
        with tempfile.TemporaryDirectory() as raw:
            root = write_root(Path(raw), blob)
            limits = ScanLimits(**limit_kwargs) if limit_kwargs else ScanLimits()
            return fp.scan_input(root, limits)

    def test_counts_and_size_structure(self):
        blob = make_hog(
            [
                ("MENU.GUI", b"\x00" * 16),
                ("HUD.GUI", b"\x00" * 32),
                ("MAP.GUI", b"\x00" * 48),
                ("BODY.FNT", b"\x00" * 100),
                ("A.IE", b"\x00" * 7),
                ("B.IE", b"\x00" * 7),
                ("SKIN.TDX", b"\x00" * 5),
                ("NOTES.TXT", b"\x00" * 3),
            ]
        )
        doc = self.scan_blob(blob)
        prints = doc["suffix_size_fingerprints"]
        self.assertEqual(
            prints[".gui"],
            {"count": 3, "size_min": 16, "size_max": 48, "size_distinct": 3, "size_gcd": 16},
        )
        self.assertEqual(
            prints[".fnt"],
            {"count": 1, "size_min": 100, "size_max": 100, "size_distinct": 1, "size_gcd": 100},
        )
        self.assertEqual(
            prints[".ie"],
            {"count": 2, "size_min": 7, "size_max": 7, "size_distinct": 1, "size_gcd": 7},
        )
        self.assertEqual(doc["totals"]["matched_member_occurrences"], 6)
        self.assertEqual(doc["totals"]["archive_occurrences"], 1)
        self.assertEqual(doc["totals"]["root_archives_scanned"], 1)
        self.assertEqual(doc["totals"]["errors"], 0)

    def test_recursion_into_nested_hog(self):
        inner = make_hog([("DEEP.GUI", b"\x00" * 20)])
        outer = make_hog([("SUB.HOG", inner), ("TOP.GUI", b"\x00" * 10)])
        doc = self.scan_blob(outer)
        self.assertEqual(doc["suffix_size_fingerprints"][".gui"]["count"], 2)
        self.assertEqual(doc["suffix_size_fingerprints"][".gui"]["size_min"], 10)
        self.assertEqual(doc["suffix_size_fingerprints"][".gui"]["size_max"], 20)
        # Outer + inner archive both counted; the .hog member itself is not a match.
        self.assertEqual(doc["totals"]["archive_occurrences"], 2)
        self.assertEqual(doc["totals"]["matched_member_occurrences"], 2)

    def test_gcd_of_mixed_sizes(self):
        blob = make_hog([("X.GUI", b"\x00" * 12), ("Y.GUI", b"\x00" * 18)])
        prints = self.scan_blob(blob)["suffix_size_fingerprints"]
        self.assertEqual(prints[".gui"]["size_gcd"], 6)
        self.assertEqual(prints[".gui"]["size_distinct"], 2)

    def test_zero_size_member_is_counted(self):
        blob = make_hog([("EMPTY.GUI", b""), ("FULL.GUI", b"\x00" * 24)])
        prints = self.scan_blob(blob)["suffix_size_fingerprints"]
        self.assertEqual(prints[".gui"]["count"], 2)
        self.assertEqual(prints[".gui"]["size_min"], 0)
        self.assertEqual(prints[".gui"]["size_max"], 24)
        # gcd(0, 24) == 24; a zero size contributes the identity.
        self.assertEqual(prints[".gui"]["size_gcd"], 24)

    def test_no_matching_members_is_all_zero_and_succeeds(self):
        blob = make_hog([("A.TDX", b"\x00" * 8), ("B.TXT", b"\x00" * 4)])
        doc = self.scan_blob(blob)
        for suffix in fp.FROZEN_SUFFIXES:
            self.assertEqual(
                doc["suffix_size_fingerprints"][suffix],
                {"count": 0, "size_min": 0, "size_max": 0, "size_distinct": 0, "size_gcd": 0},
            )
        self.assertEqual(doc["totals"]["matched_member_occurrences"], 0)
        self.assertEqual(doc["totals"]["errors"], 0)

    def test_hog_members_are_not_matched(self):
        blob = make_hog([("NESTED.HOG", make_hog([("Z.FNT", b"\x00" * 4)]))])
        doc = self.scan_blob(blob)
        # .hog is not in the frozen suffix set; only the inner .fnt matches.
        self.assertEqual(doc["suffix_size_fingerprints"][".fnt"]["count"], 1)
        self.assertEqual(doc["totals"]["matched_member_occurrences"], 1)

    def test_deterministic_and_json_stable(self):
        blob = make_hog([("A.GUI", b"\x00" * 16), ("B.IE", b"\x00" * 8)])
        first = self.scan_blob(blob)
        second = self.scan_blob(blob)
        self.assertEqual(first, second)
        text = json.dumps(first, separators=(",", ":"), sort_keys=True)
        self.assertEqual(json.loads(text), first)

    def test_schema_version_and_scope_present(self):
        doc = self.scan_blob(make_hog([("A.GUI", b"\x00" * 4)]))
        self.assertEqual(doc["schema_version"], fp.SCHEMA_VERSION)
        self.assertIn("NOT a payload-address alignment", doc["scope"])

    def test_frozen_suffixes_are_pinned(self):
        # Change-detector: promoting a suffix must update fixtures and this pin together.
        self.assertEqual(fp.FROZEN_SUFFIXES, (".fnt", ".gui", ".ie"))
        doc = self.scan_blob(make_hog([("A.GUI", b"\x00" * 4)]))
        self.assertEqual(list(doc["suffix_size_fingerprints"]), list(fp.FROZEN_SUFFIXES))
        self.assertEqual(doc["frozen_suffixes"], list(fp.FROZEN_SUFFIXES))

    def test_uppercase_and_mixed_case_suffix_matches(self):
        blob = make_hog([("MENU.GUI", b"\x00" * 4), ("hud.Gui", b"\x00" * 8)])
        # Both normalize to the .gui extension regardless of source case.
        self.assertEqual(self.scan_blob(blob)["suffix_size_fingerprints"][".gui"]["count"], 2)

    def test_malformed_root_hog_fails_closed(self):
        with tempfile.TemporaryDirectory() as raw:
            root = write_root(Path(raw), b"\x00" * 10)  # too small for a HOG directory
            with redirect_stdout(io.StringIO()) as out:
                code = fp.main([str(root)])
        self.assertEqual(code, 1)
        doc = json.loads(out.getvalue())
        self.assertEqual(doc["totals"]["errors"], 1)
        self.assertEqual(doc["error_categories"]["root_hog_malformed"], 1)

    def test_caller_limit_cannot_raise_a_hard_ceiling(self):
        blob = make_hog([("A.GUI", b"\x00" * 4)])
        over = HARD_LIMITS["maximum_root_archives"] + 1
        with self.assertRaises(ScanFailure) as ctx:
            self.scan_blob(blob, maximum_root_archives=over)
        self.assertEqual(ctx.exception.category, "config")
        # Exactly at the hard ceiling is permitted (the boundary is inclusive).
        doc = self.scan_blob(blob, maximum_root_archives=HARD_LIMITS["maximum_root_archives"])
        self.assertEqual(doc["totals"]["errors"], 0)

    def test_a_tighter_caller_limit_is_honored(self):
        # A single valid root archive with a root-archive cap of 1 still succeeds;
        # the tighter limit does not manufacture an error on a conforming input.
        doc = self.scan_blob(make_hog([("A.GUI", b"\x00" * 4)]), maximum_root_archives=1)
        self.assertEqual(doc["totals"]["root_archives_scanned"], 1)

    def test_bad_argv_is_a_config_failure(self):
        with redirect_stdout(io.StringIO()) as out:
            code = fp.main([])  # missing required input argument
        self.assertEqual(code, 1)
        doc = json.loads(out.getvalue())
        self.assertEqual(doc["error_categories"]["config"], 1)

    def test_diagnostics_never_disclose_member_names_or_paths(self):
        blob = make_hog(
            [("TOPSECRETMEMBERNAME.GUI", b"\x00" * 16), ("C__USERS_PRIVATE.IE", b"\x00" * 8)]
        )
        with tempfile.TemporaryDirectory() as raw:
            root = write_root(Path(raw), blob, name="SECRETARCHIVE.HOG")
            with redirect_stdout(io.StringIO()) as out:
                fp.main([str(root)])
        rendered = out.getvalue()
        self.assertNotIn("TOPSECRET", rendered)
        self.assertNotIn("SECRETARCHIVE", rendered)
        self.assertNotIn("PRIVATE", rendered)
        self.assertNotIn(raw, rendered)
        # It is still a valid aggregate document.
        doc = json.loads(rendered)
        self.assertEqual(doc["suffix_size_fingerprints"][".gui"]["count"], 1)
        self.assertEqual(doc["suffix_size_fingerprints"][".ie"]["count"], 1)

    def test_failure_document_shape_matches_success_document(self):
        success_keys = set(self.scan_blob(make_hog([("A.GUI", b"\x00" * 4)])))
        failure_keys = set(fp.failure_document("io"))
        self.assertEqual(success_keys, failure_keys)


if __name__ == "__main__":
    unittest.main()
