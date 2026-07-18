from __future__ import annotations

import io
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import profile_pop_candidate_record_shapes as profiler  # noqa: E402


def _append_terrain_record(data: bytearray, name: bytes) -> None:
    data.extend(struct.pack("<II", 12, 3))
    data.extend(name)
    data.append(0)
    while len(data) % 4:
        data.append(0x7F)


def _make_candidate_pop(
    records_by_tag: dict[str, list[list[int]]] | None = None,
    *,
    extra_after_tag: dict[str, bytes] | None = None,
    omitted_tags: set[str] | None = None,
    duplicate_tag: str | None = None,
    include_non_target_marker: bool = False,
    terrain_name: bytes = b"CELL_A.VUM",
) -> bytes:
    records_by_tag = records_by_tag or {}
    extra_after_tag = extra_after_tag or {}
    omitted_tags = omitted_tags or set()
    data = bytearray(struct.pack("<I4sI", 70, b"TER:", 1))
    _append_terrain_record(data, terrain_name)
    data.extend(b"GOB:")
    if include_non_target_marker:
        data.extend(b"SND:")

    for formula in profiler.CANDIDATE_FORMULAS:
        if formula.literal_tag in omitted_tags:
            continue
        repetitions = 2 if formula.literal_tag == duplicate_tag else 1
        for _ in range(repetitions):
            records = records_by_tag.get(formula.literal_tag, [])
            data.extend(formula.literal_tag.encode("ascii"))
            data.extend(struct.pack("<I", len(records)))
            for record in records:
                if len(record) != formula.column_count:
                    raise AssertionError("synthetic record has the wrong column count")
                data.extend(struct.pack(f"<{formula.column_count}I", *record))
            data.extend(extra_after_tag.get(formula.literal_tag, b""))
    return bytes(data)


def _write_candidate(root: Path, data: bytes, name: str = "candidate.POP") -> Path:
    path = root / name
    path.write_bytes(data)
    return path


def _shape(result: dict[str, object], literal_tag: str) -> dict[str, object]:
    shapes = result["candidate_record_shapes"]
    assert isinstance(shapes, list)
    for shape in shapes:
        assert isinstance(shape, dict)
        if shape["literal_tag"] == literal_tag:
            return shape
    raise AssertionError(f"missing synthetic shape {literal_tag}")


class PopCandidateRecordShapeTests(unittest.TestCase):
    def assert_failure_suppressed(
        self, result: dict[str, object], category: str | None = None
    ) -> None:
        self.assertFalse(result["complete"])
        if category is not None:
            self.assertEqual(result["errors"], {category: 1})
        for forbidden in (
            "totals",
            "classification_parameters",
            "candidate_record_shapes",
            "total_records",
            "columns",
        ):
            self.assertNotIn(forbidden, result)

    def test_formula_set_is_exact_and_fixed(self) -> None:
        self.assertEqual(
            [
                (
                    formula.literal_tag,
                    formula.count_word_delta_bytes,
                    formula.fixed_record_stride_bytes,
                )
                for formula in profiler.CANDIDATE_FORMULAS
            ],
            [
                ("INL:", 4, 36),
                ("PNT:", 4, 88),
                ("DIR:", 4, 44),
                ("ENV:", 4, 76),
                ("INV:", 4, 84),
            ],
        )

    def test_zero_counts_require_and_accept_exactly_empty_extents(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop())
            result = profiler.scan_root(root)

        self.assertTrue(result["complete"])
        self.assertEqual(result["totals"]["candidate_formula_occurrences"], 5)
        self.assertEqual(result["totals"]["total_records"], 0)
        self.assertEqual(
            [shape["literal_tag"] for shape in result["candidate_record_shapes"]],
            ["INL:", "PNT:", "DIR:", "ENV:", "INV:"],
        )
        for formula in profiler.CANDIDATE_FORMULAS:
            shape = _shape(result, formula.literal_tag)
            self.assertEqual(shape["total_records"], 0)
            self.assertEqual(shape["column_count"], formula.column_count)
            self.assertTrue(
                all(column["zero_bit_patterns"] == 0 for column in shape["columns"])
            )

    def test_zero_count_with_nonempty_extent_fails_formula_guard(self) -> None:
        malformed = _make_candidate_pop(extra_after_tag={"INL:": b"\0\0\0\0"})
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, malformed)
            result = profiler.scan_root(root)
        self.assert_failure_suppressed(result, "formula_mismatch")

    def test_each_fixed_stride_profiles_one_complete_synthetic_record(self) -> None:
        records = {
            formula.literal_tag: [list(range(1, formula.column_count + 1))]
            for formula in profiler.CANDIDATE_FORMULAS
        }
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop(records))
            result = profiler.scan_root(root)

        self.assertTrue(result["complete"])
        self.assertEqual(result["totals"]["total_records"], 5)
        self.assertEqual(result["totals"]["column_observations"], 82)
        for formula in profiler.CANDIDATE_FORMULAS:
            shape = _shape(result, formula.literal_tag)
            self.assertEqual(shape["total_records"], 1)
            self.assertEqual(len(shape["columns"]), formula.column_count)

    def test_nonzero_formula_mismatch_fails_closed(self) -> None:
        record = [0] * 9
        malformed = _make_candidate_pop(
            {"INL:": [record]}, extra_after_tag={"INL:": b"\x11\x22\x33\x44"}
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, malformed)
            result = profiler.scan_root(root)
        self.assert_failure_suppressed(result, "formula_mismatch")

    def test_missing_or_duplicate_target_formula_fails_closed(self) -> None:
        cases = (
            _make_candidate_pop(omitted_tags={"ENV:"}),
            _make_candidate_pop(duplicate_tag="DIR:"),
        )
        for data in cases:
            with self.subTest(size=len(data)), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                _write_candidate(root, data)
                result = profiler.scan_root(root)
            self.assert_failure_suppressed(result, "formula_mismatch")

    def test_per_column_classifications_are_aggregate_bit_pattern_counts(self) -> None:
        records = [
            [0, 0x7F800000, 1, 5000, 9, 9, 9, 9, 9],
            [7, 0x7FC01234, 2, 3, 9, 9, 9, 9, 9],
        ]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop({"INL:": records}))
            result = profiler.scan_root(root)

        self.assertTrue(result["complete"])
        inl = _shape(result, "INL:")
        self.assertEqual(inl["total_records"], 2)
        first = inl["columns"][0]
        self.assertEqual(first["zero_bit_patterns"], 1)
        self.assertEqual(first["nonzero_bit_patterns"], 1)
        self.assertEqual(first["finite_ieee754_bit_patterns"], 2)
        self.assertEqual(first["nonfinite_ieee754_bit_patterns"], 0)
        self.assertEqual(first["small_unsigned_bit_patterns_below_threshold"], 2)
        self.assertEqual(first["distinct_bit_pattern_cardinality"], 2)
        second = inl["columns"][1]
        self.assertEqual(second["finite_ieee754_bit_patterns"], 0)
        self.assertEqual(second["nonfinite_ieee754_bit_patterns"], 2)
        self.assertEqual(second["small_unsigned_bit_patterns_below_threshold"], 0)
        fourth = inl["columns"][3]
        self.assertEqual(fourth["small_unsigned_bit_patterns_below_threshold"], 1)
        self.assertIn("do not assign", result["interpretation"])
        self.assertEqual(
            result["classification_parameters"]["small_unsigned_exclusive_threshold"],
            profiler.SMALL_UNSIGNED_EXCLUSIVE_THRESHOLD,
        )

    def test_nan_and_infinity_patterns_are_counted_without_value_leakage(self) -> None:
        sentinels = (0x7F800000, 0xFF800000, 0x7FC01234)
        records = [[sentinel] + [0] * 8 for sentinel in sentinels]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop({"INL:": records}))
            result = profiler.scan_root(root)

        column = _shape(result, "INL:")["columns"][0]
        self.assertEqual(column["finite_ieee754_bit_patterns"], 0)
        self.assertEqual(column["nonfinite_ieee754_bit_patterns"], 3)
        self.assertEqual(column["distinct_bit_pattern_cardinality"], 3)
        serialized = json.dumps(result, sort_keys=True)
        for sentinel in sentinels:
            self.assertNotIn(str(sentinel), serialized)
            self.assertNotIn(f"{sentinel:08x}", serialized.casefold())

    def test_non_target_marker_is_not_profiled_as_an_extra_formula(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop(include_non_target_marker=True))
            result = profiler.scan_root(root)
        self.assertTrue(result["complete"])
        self.assertEqual(
            {shape["literal_tag"] for shape in result["candidate_record_shapes"]},
            {"INL:", "PNT:", "DIR:", "ENV:", "INV:"},
        )

    def test_distinct_cardinality_is_capped_without_retaining_overflow(self) -> None:
        records = [[1] + [0] * 8, [2] + [0] * 8, [3] + [0] * 8]
        limits = replace(
            profiler.ScanLimits(), maximum_distinct_bit_patterns_per_column=1
        )
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop({"INL:": records}))
            result = profiler.scan_root(root, limits)

        self.assertTrue(result["complete"])
        column = _shape(result, "INL:")["columns"][0]
        self.assertEqual(column["distinct_bit_pattern_cardinality"], 1)
        self.assertTrue(column["distinct_bit_pattern_cardinality_is_capped"])
        self.assertEqual(
            result["classification_parameters"][
                "distinct_bit_pattern_cardinality_cap_per_column"
            ],
            1,
        )

    def test_record_column_distinct_read_metadata_and_output_caps_fail_closed(self) -> None:
        records = [[1] + [0] * 8, [2] + [0] * 8]
        data = _make_candidate_pop({"INL:": records})
        cases = {
            "records_per_formula": replace(
                profiler.ScanLimits(), maximum_records_per_formula=1
            ),
            "total_records": replace(profiler.ScanLimits(), maximum_total_records=1),
            "formula_occurrences": replace(
                profiler.ScanLimits(), maximum_formula_occurrences=4
            ),
            "columns_per_formula": replace(
                profiler.ScanLimits(), maximum_columns_per_formula=21
            ),
            "output_columns": replace(
                profiler.ScanLimits(), maximum_total_output_columns=81
            ),
            "column_observations": replace(
                profiler.ScanLimits(), maximum_total_column_observations=17
            ),
            "distinct_total": replace(
                profiler.ScanLimits(), maximum_total_distinct_bit_patterns=1
            ),
            "read_bytes": replace(
                profiler.ScanLimits(), maximum_total_read_bytes=16
            ),
            "metadata": replace(profiler.ScanLimits(), maximum_metadata_bytes=1),
            "profile_chunk": replace(
                profiler.ScanLimits(), maximum_profile_read_chunk_bytes=87
            ),
            "output_bytes": replace(
                profiler.ScanLimits(), maximum_output_bytes=512
            ),
        }
        for label, limits in cases.items():
            with self.subTest(label=label), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                _write_candidate(root, data)
                result = profiler.scan_root(root, limits)
            self.assert_failure_suppressed(result, "limit_exceeded")

    def test_traversal_file_size_and_marker_caps_fail_closed(self) -> None:
        data = _make_candidate_pop()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, data)
            (root / "extra.txt").write_text("opaque", encoding="utf-8")
            cases = {
                "walk": replace(profiler.ScanLimits(), maximum_walk_entries=1),
                "file": replace(
                    profiler.ScanLimits(), maximum_file_bytes=len(data) - 1
                ),
                "total_file": replace(
                    profiler.ScanLimits(), maximum_total_file_bytes=len(data) - 1
                ),
                "terrain": replace(
                    profiler.ScanLimits(), maximum_terrain_records=0
                ),
                "markers": replace(
                    profiler.ScanLimits(), maximum_marker_hits_per_file=5
                ),
            }
            for label, limits in cases.items():
                with self.subTest(label=label):
                    result = profiler.scan_root(root, limits)
                    self.assert_failure_suppressed(result, "limit_exceeded")

    def test_file_count_depth_and_cumulative_terrain_caps_fail_closed(self) -> None:
        data = _make_candidate_pop()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, data, "first.POP")
            _write_candidate(root, data, "second.POP")
            result = profiler.scan_root(
                root, replace(profiler.ScanLimits(), maximum_files=1)
            )
            self.assert_failure_suppressed(result, "limit_exceeded")
            result = profiler.scan_root(
                root,
                replace(profiler.ScanLimits(), maximum_total_terrain_records=1),
            )
            self.assert_failure_suppressed(result, "limit_exceeded")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            nested = root / "nested"
            nested.mkdir()
            _write_candidate(nested, data)
            result = profiler.scan_root(
                root, replace(profiler.ScanLimits(), maximum_directory_depth=0)
            )
        self.assert_failure_suppressed(result, "limit_exceeded")

    def test_link_like_and_special_entries_fail_closed(self) -> None:
        data = _make_candidate_pop()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = _write_candidate(root, data)
            original_is_symlink = Path.is_symlink

            def emulate_symlink(path: Path) -> bool:
                return path == candidate or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", emulate_symlink):
                result = profiler.scan_root(root)
            self.assert_failure_suppressed(result, "unsafe_input")

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            special = root / "special.bin"
            special.write_bytes(b"ordinary fixture")
            original_stat = profiler.os.stat
            observed = original_stat(special, follow_symlinks=False)
            special_stat = mock.Mock(
                st_mode=stat.S_IFIFO,
                st_dev=observed.st_dev,
                st_ino=observed.st_ino,
                st_size=observed.st_size,
                st_mtime_ns=observed.st_mtime_ns,
                st_file_attributes=0,
            )

            def emulate_special(path: object, *args: object, **kwargs: object) -> object:
                if Path(path) == special:
                    return special_stat
                return original_stat(path, *args, **kwargs)

            with mock.patch.object(profiler.os, "stat", side_effect=emulate_special):
                result = profiler.scan_root(root)
            self.assert_failure_suppressed(result, "unsafe_input")

    def test_reparse_attribute_and_open_file_race_fail_closed(self) -> None:
        self.assertTrue(
            profiler._stat_is_reparse(mock.Mock(st_file_attributes=0x400))
        )
        data = _make_candidate_pop()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            candidate = _write_candidate(root, data)
            with candidate.open("rb") as stream:
                initial = os.fstat(stream.fileno())
            changed = mock.Mock(
                st_mode=initial.st_mode,
                st_dev=initial.st_dev,
                st_ino=initial.st_ino,
                st_size=initial.st_size,
                st_mtime_ns=initial.st_mtime_ns + 1,
                st_file_attributes=getattr(initial, "st_file_attributes", 0),
            )
            with mock.patch.object(
                profiler.os, "fstat", side_effect=(initial, changed)
            ):
                result = profiler.scan_root(root)
        self.assert_failure_suppressed(result, "unsafe_input")

    def test_any_candidate_error_suppresses_previous_structural_aggregates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop(), "valid.POP")
            _write_candidate(root, b"not a POP", "invalid.POP")
            result = profiler.scan_root(root)
        self.assert_failure_suppressed(result)

    def test_unexpected_adapter_error_is_sanitized_and_suppresses_aggregates(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(root, _make_candidate_pop())
            with mock.patch.object(
                profiler,
                "_profile_formula_span",
                side_effect=RuntimeError("SECRET_INTERNAL_DETAIL"),
            ):
                result = profiler.scan_root(root)
        self.assert_failure_suppressed(result, "io_error")
        self.assertNotIn("SECRET", json.dumps(result, sort_keys=True))

    def test_success_output_is_deterministic_and_contains_no_private_identity_or_values(self) -> None:
        records = [[0xDEADBEEF, 0xCAFEBABE] + [17] * 7]
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            _write_candidate(
                root,
                _make_candidate_pop(
                    {"INL:": records}, terrain_name=b"SECRET_CELL_ALPHA.VUM"
                ),
                "SECRET_LEVEL_ALPHA.POP",
            )
            first = profiler.scan_root(root)
            second = profiler.scan_root(root)

        self.assertEqual(first, second)
        self.assertTrue(first["complete"])
        serialized = json.dumps(first, sort_keys=True)
        for forbidden in (
            "SECRET",
            "3735928559",
            "3405691582",
            "deadbeef",
            "cafebabe",
            "file_bytes",
            "file_records",
            "source_locator",
        ):
            self.assertNotIn(forbidden.casefold(), serialized.casefold())

        forbidden_keys = {
            "path",
            "name",
            "hash",
            "raw_word",
            "value",
            "minimum",
            "maximum",
            "payload",
        }

        def visit(value: object) -> None:
            if isinstance(value, dict):
                self.assertTrue(forbidden_keys.isdisjoint(value))
                for child in value.values():
                    visit(child)
            elif isinstance(value, list):
                for child in value:
                    visit(child)

        visit(first)

    def test_empty_root_and_cli_failure_are_sanitized(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = profiler.scan_root(Path(temporary))
        self.assert_failure_suppressed(result, "no_candidates")

        private_path = "C:/owned/private/SECRET_DISC_ROOT"
        output = io.StringIO()
        with mock.patch.object(
            sys,
            "argv",
            ["profile_pop_candidate_record_shapes.py", private_path],
        ):
            with redirect_stdout(output):
                self.assertEqual(profiler.main(), 2)
        parsed = json.loads(output.getvalue())
        self.assert_failure_suppressed(parsed)
        self.assertNotIn("SECRET", output.getvalue())


if __name__ == "__main__":
    unittest.main()
