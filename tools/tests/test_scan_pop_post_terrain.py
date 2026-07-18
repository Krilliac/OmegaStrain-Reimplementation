from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import scan_pop_post_terrain as scanner  # noqa: E402


def _append_record(
    data: bytearray,
    kind: int,
    index: int,
    name: bytes,
    alignment_fill: int = 0,
) -> None:
    data.extend(struct.pack("<II", kind, index))
    data.extend(name)
    data.append(0)
    while len(data) % 4:
        data.append(alignment_fill)


def _append_aligned_marker(data: bytearray, tag: bytes, fill: int = 0xCC) -> None:
    while len(data) % 4:
        data.append(fill)
    data.extend(tag)


def _make_pop(
    records: tuple[tuple[int, int, bytes, int], ...] = ((12, 3, b"CELL_A.VUM", 0),),
    post_gob: bytes = b"",
) -> bytes:
    data = bytearray(struct.pack("<I4sI", 70, b"TER:", len(records)))
    for kind, index, name, alignment_fill in records:
        _append_record(data, kind, index, name, alignment_fill)
    data.extend(b"GOB:")
    data.extend(post_gob)
    return bytes(data)


class PopPostTerrainStreamTests(unittest.TestCase):
    def test_exact_gob_boundary_and_aligned_literal_order_are_preserved(self) -> None:
        post_gob = bytearray(b"\x01\x02\x03\x04")
        _append_aligned_marker(post_gob, b"SND:")
        post_gob.extend(b"\x10\x11\x12\x13")
        _append_aligned_marker(post_gob, b"ANPC:")
        post_gob.extend(b"\xFF\xFF\xFF")
        _append_aligned_marker(post_gob, b"CAM:")
        data = _make_pop(
            records=((12, 3, b"CELL_A.VUM", 0x7F), (13, 9, b"B", 0)),
            post_gob=bytes(post_gob),
        )

        result = scanner.scan_pop_stream(io.BytesIO(data), len(data))

        self.assertEqual(result.terrain_records, 2)
        self.assertEqual(result.nonzero_alignment_records, 1)
        self.assertEqual(result.gob_offset % 4, 0)
        self.assertEqual([tag for _, tag in result.markers], ["GOB:", "SND:", "ANPC:", "CAM:"])
        self.assertTrue(all(offset % 4 == 0 for offset, _ in result.markers))

    def test_unaligned_and_unpublished_marker_like_bytes_are_ignored(self) -> None:
        data = _make_pop(post_gob=b"XSND:" + b"ZZZ:" + b"\0\0")

        result = scanner.scan_pop_stream(io.BytesIO(data), len(data))

        self.assertEqual(result.markers, ((result.gob_offset, "GOB:"),))

    def test_repeated_aligned_literals_are_inventory_hits_not_sections(self) -> None:
        post_gob = bytearray()
        _append_aligned_marker(post_gob, b"SND:")
        _append_aligned_marker(post_gob, b"SND:")
        data = _make_pop(post_gob=bytes(post_gob))

        result = scanner.scan_pop_stream(io.BytesIO(data), len(data))

        self.assertEqual([tag for _, tag in result.markers], ["GOB:", "SND:", "SND:"])

    def test_five_byte_literal_crossing_a_stream_window_is_found(self) -> None:
        post_gob = bytearray(b"\xCC" * (scanner._SCAN_CHUNK_BYTES - 4))
        post_gob.extend(b"ANPC:")
        data = _make_pop(post_gob=bytes(post_gob))

        result = scanner.scan_pop_stream(io.BytesIO(data), len(data))

        self.assertEqual([tag for _, tag in result.markers], ["GOB:", "ANPC:"])

    def test_malformed_or_truncated_prefixes_fail_closed(self) -> None:
        valid = bytearray(_make_pop())
        cases: list[tuple[str, bytes]] = []
        cases.append(("short", bytes(valid[:15])))
        bad_word = bytearray(valid)
        bad_word[0] = 69
        cases.append(("header", bytes(bad_word)))
        bad_tag = bytearray(valid)
        bad_tag[4:8] = b"BAD:"
        cases.append(("tag", bytes(bad_tag)))
        bad_count = bytearray(valid)
        bad_count[8:12] = struct.pack("<I", 100)
        cases.append(("count extent", bytes(bad_count)))
        empty_name = bytearray(valid)
        empty_name[20] = 0
        cases.append(("empty name", bytes(empty_name)))
        non_ascii = bytearray(valid)
        non_ascii[20] = 0x1F
        cases.append(("non-ascii name", bytes(non_ascii)))
        bad_gob = bytearray(valid)
        bad_gob[-4:] = b"NOPE"
        cases.append(("missing GOB", bytes(bad_gob)))

        for label, data in cases:
            with self.subTest(label=label):
                with self.assertRaises(scanner.ScanFailure):
                    scanner.scan_pop_stream(io.BytesIO(data), len(data))

    def test_limits_apply_before_unbounded_scanning(self) -> None:
        data = _make_pop()
        with self.assertRaisesRegex(scanner.ScanFailure, "limit_exceeded"):
            scanner.scan_pop_stream(
                io.BytesIO(data),
                len(data),
                scanner.ScanLimits(maximum_file_bytes=len(data) - 1),
            )
        with self.assertRaisesRegex(scanner.ScanFailure, "limit_exceeded"):
            scanner.scan_pop_stream(
                io.BytesIO(data),
                len(data),
                scanner.ScanLimits(maximum_terrain_records=0),
            )
        with self.assertRaisesRegex(scanner.ScanFailure, "limit_exceeded"):
            scanner.scan_pop_stream(
                io.BytesIO(data),
                len(data),
                scanner.ScanLimits(maximum_name_bytes=2),
            )
        with self.assertRaisesRegex(scanner.ScanFailure, "limit_exceeded"):
            scanner.scan_pop_stream(
                io.BytesIO(data),
                len(data),
                scanner.ScanLimits(maximum_marker_hits_per_file=0),
            )

        marker_heavy = _make_pop(post_gob=b"SND:" * 3)
        with self.assertRaisesRegex(scanner.ScanFailure, "limit_exceeded"):
            scanner.scan_pop_stream(
                io.BytesIO(marker_heavy),
                len(marker_heavy),
                scanner.ScanLimits(maximum_marker_hits_per_file=2),
            )


class PopPostTerrainAggregateTests(unittest.TestCase):
    def test_aggregate_is_deterministic_and_contains_no_per_file_identity(self) -> None:
        first_tail = bytearray()
        _append_aligned_marker(first_tail, b"SND:")
        _append_aligned_marker(first_tail, b"ACL:")
        second_tail = bytearray()
        _append_aligned_marker(second_tail, b"SND:")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "SECRET_LEVEL_ALPHA.POP").write_bytes(
                _make_pop(records=((12, 1, b"SECRET_CELL_ALPHA.VUM", 0),), post_gob=bytes(first_tail))
            )
            nested = root / "nested"
            nested.mkdir()
            (nested / "SECRET_LEVEL_BETA.POP").write_bytes(
                _make_pop(records=((13, 2, b"SECRET_CELL_BETA.VUM", 0),), post_gob=bytes(second_tail))
            )

            first = scanner.scan_root(root)
            second = scanner.scan_root(root)

        self.assertEqual(first, second)
        self.assertTrue(first["complete"])
        self.assertEqual(first["files"], 2)
        self.assertEqual(first["literal_marker_occurrences"], {"ACL:": 1, "GOB:": 2, "SND:": 2})
        self.assertEqual(first["distinct_ordered_literal_sequences"], 2)
        self.assertEqual(
            first["ordered_literal_marker_ordinals"][0]["literal_tag_counts"],
            {"GOB:": 2},
        )
        serialized = json.dumps(first, sort_keys=True)
        for forbidden in ("SECRET_LEVEL", "SECRET_CELL"):
            self.assertNotIn(forbidden, serialized)
        self.assertNotIn("source_locator", serialized)
        self.assertNotIn("file_records", serialized)

    def test_any_invalid_candidate_suppresses_structural_aggregates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "valid.POP").write_bytes(_make_pop())
            (root / "invalid.POP").write_bytes(b"not a POP")

            result = scanner.scan_root(root)

        self.assertFalse(result["complete"])
        self.assertEqual(result["files_discovered"], 2)
        self.assertEqual(result["files_valid"], 1)
        self.assertTrue(result["errors"])
        self.assertNotIn("literal_marker_occurrences", result)
        self.assertNotIn("candidate_marker_transitions", result)

    def test_walk_entry_budget_prevents_unbounded_non_candidate_traversal(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "first.txt").write_text("not a candidate", encoding="utf-8")
            (root / "second.txt").write_text("not a candidate", encoding="utf-8")

            result = scanner.scan_root(
                root,
                scanner.ScanLimits(maximum_walk_entries=1),
            )

        self.assertFalse(result["complete"])
        self.assertEqual(result["errors"], {"limit_exceeded": 1})
        self.assertNotIn("literal_marker_occurrences", result)

    def test_link_like_subtree_fails_closed_instead_of_escaping_root(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "valid.POP").write_bytes(_make_pop())
            linked = root / "linked"
            linked.mkdir()
            (linked / "outside.POP").write_bytes(_make_pop(post_gob=b"SND:"))

            original_is_junction = Path.is_junction

            def emulate_junction(path: Path) -> bool:
                return path == linked or original_is_junction(path)

            with mock.patch.object(Path, "is_junction", emulate_junction):
                result = scanner.scan_root(root)

        self.assertFalse(result["complete"])
        self.assertEqual(result["errors"], {"unsafe_input": 1})
        self.assertNotIn("literal_marker_occurrences", result)

    def test_link_like_candidate_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            linked = root / "linked.POP"
            linked.write_bytes(_make_pop(post_gob=b"SND:"))

            original_is_symlink = Path.is_symlink

            def emulate_symlink(path: Path) -> bool:
                return path == linked or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", emulate_symlink):
                result = scanner.scan_root(root)

        self.assertFalse(result["complete"])
        self.assertEqual(result["errors"], {"unsafe_input": 1})
        self.assertNotIn("literal_marker_occurrences", result)

    def test_empty_root_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = scanner.scan_root(Path(temporary))
        self.assertEqual(result["errors"], {"no_candidates": 1})
        self.assertFalse(result["complete"])


if __name__ == "__main__":
    unittest.main()
