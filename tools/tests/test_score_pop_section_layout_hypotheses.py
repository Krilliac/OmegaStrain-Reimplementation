from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import score_pop_section_layout_hypotheses as scorer  # noqa: E402


def _append_record(data: bytearray, name: bytes) -> None:
    data.extend(struct.pack("<II", 12, 3))
    data.extend(name)
    data.append(0)
    while len(data) % 4:
        data.append(0x7F)


def _make_pop(post_gob: bytes, name: bytes = b"CELL_A.VUM") -> bytes:
    data = bytearray(struct.pack("<I4sI", 70, b"TER:", 1))
    _append_record(data, name)
    data.extend(b"GOB:")
    data.extend(post_gob)
    return bytes(data)


def _exact_span(
    count: int,
    stride: int,
    next_marker: bytes,
    opaque_header_words: int = 0,
) -> bytes:
    data = bytearray(b"HEAD" * opaque_header_words)
    data.extend(struct.pack("<I", count))
    data.extend(bytes(count * stride))
    data.extend(next_marker)
    return bytes(data)


def _scan_files(
    files: dict[str, bytes], limits: scorer.ScanLimits | None = None
) -> dict[str, object]:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        for relative, payload in files.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(payload)
        return scorer.scan_root(root, limits or scorer.ScanLimits())


class PopSectionLayoutHypothesisTests(unittest.TestCase):
    def test_exact_count_field_and_fixed_stride_are_aggregate_hypotheses(self) -> None:
        result = _scan_files({
            "SECRET_LEVEL.POP": _make_pop(_exact_span(2, 8, b"SND:"))
        })

        self.assertTrue(result["complete"])
        hypothesis = next(
            item
            for item in result["candidate_layout_hypotheses"]
            if item["literal_marker"] == "GOB:"
            and item["candidate_count_field_delta_bytes"] == 4
            and item["candidate_fixed_record_stride_bytes"] == 8
        )
        self.assertEqual(hypothesis["candidate_header_extent_bytes"], 8)
        self.assertEqual(hypothesis["candidate_extent_exact_matches"], 1)
        self.assertEqual(hypothesis["candidate_extent_mismatches"], 0)

    def test_support_score_includes_mismatching_bounded_occurrences(self) -> None:
        exact = _make_pop(_exact_span(2, 8, b"SND:"))
        mismatch = _make_pop(_exact_span(2, 6, b"SND:"))
        result = _scan_files({"one.POP": exact, "two.POP": mismatch})

        hypothesis = next(
            item
            for item in result["candidate_layout_hypotheses"]
            if item["literal_marker"] == "GOB:"
            and item["candidate_count_field_delta_bytes"] == 4
            and item["candidate_fixed_record_stride_bytes"] == 8
        )
        self.assertEqual(hypothesis["bounded_nonzero_count_occurrences_tested"], 2)
        self.assertEqual(hypothesis["candidate_extent_exact_matches"], 1)
        self.assertEqual(hypothesis["candidate_extent_mismatches"], 1)

    def test_field_slots_are_marker_relative_and_five_byte_tags_align_forward(self) -> None:
        post_gob = bytearray(struct.pack("<I", 0))
        post_gob.extend(b"ANPC:")
        post_gob.extend(b"PAD")
        post_gob.extend(_exact_span(2, 8, b"SND:"))
        result = _scan_files({"level.POP": _make_pop(bytes(post_gob))})

        hypothesis = next(
            item
            for item in result["candidate_layout_hypotheses"]
            if item["literal_marker"] == "ANPC:"
        )
        self.assertEqual(hypothesis["candidate_count_field_delta_bytes"], 8)
        self.assertEqual(hypothesis["candidate_fixed_record_stride_bytes"], 8)

    def test_zero_candidate_count_does_not_create_degenerate_stride_evidence(self) -> None:
        result = _scan_files({
            "level.POP": _make_pop(struct.pack("<I", 0) + b"SND:")
        })

        self.assertTrue(result["complete"])
        self.assertEqual(result["candidate_layout_hypotheses"], [])
        self.assertEqual(result["totals"]["bounded_nonzero_count_probes"], 0)

    def test_opaque_words_above_candidate_count_cap_are_not_decoded(self) -> None:
        result = _scan_files({
            "level.POP": _make_pop(struct.pack("<I", 0xFFFFFFFF) + b"SND:")
        })

        self.assertTrue(result["complete"])
        self.assertEqual(result["candidate_layout_hypotheses"], [])
        self.assertEqual(result["totals"]["bounded_nonzero_count_probes"], 0)

    def test_prefix_marker_bytes_and_private_identifiers_are_never_reported(self) -> None:
        result = _scan_files({
            "SECRET_LEVEL_ALPHA.POP": _make_pop(
                _exact_span(2, 8, b"SND:"), b"SND:SECRET_CELL_ALPHA.VUM"
            )
        })

        encoded = json.dumps(result, sort_keys=True)
        self.assertTrue(result["complete"])
        self.assertEqual(result["totals"]["candidate_marker_spans"], 2)
        for forbidden in ("SECRET_LEVEL", "SECRET_CELL", "placement", "visibility"):
            self.assertNotIn(forbidden, encoded)

        def assert_private_keys_absent(value: object) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    self.assertNotIn(
                        key,
                        {"path", "name", "hash", "raw_word", "payload", "file_records"},
                    )
                    assert_private_keys_absent(child)
            elif isinstance(value, list):
                for child in value:
                    assert_private_keys_absent(child)

        assert_private_keys_absent(result)

    def test_any_malformed_candidate_suppresses_all_structural_aggregates(self) -> None:
        result = _scan_files({
            "valid.POP": _make_pop(_exact_span(2, 8, b"SND:")),
            "malformed.POP": b"not a bounded POP",
        })

        self.assertFalse(result["complete"])
        self.assertEqual(result["files_discovered"], 2)
        self.assertEqual(result["files_valid"], 1)
        self.assertTrue(result["errors"])
        self.assertNotIn("candidate_layout_hypotheses", result)
        self.assertNotIn("totals", result)

    def test_file_total_read_metadata_probe_and_output_budgets_fail_closed(self) -> None:
        payload = _make_pop(_exact_span(2, 8, b"SND:"))
        cases = (
            replace(scorer.ScanLimits(), maximum_file_bytes=len(payload) - 1),
            replace(scorer.ScanLimits(), maximum_total_file_bytes=len(payload) - 1),
            replace(scorer.ScanLimits(), maximum_total_read_bytes=16),
            replace(scorer.ScanLimits(), maximum_metadata_bytes=1),
            replace(scorer.ScanLimits(), maximum_candidate_spans=1),
            replace(scorer.ScanLimits(), maximum_field_probes=1),
            replace(scorer.ScanLimits(), maximum_output_bytes=512),
        )
        for limits in cases:
            with self.subTest(limits=limits):
                result = _scan_files({"level.POP": payload}, limits)
                self.assertFalse(result["complete"])
                self.assertEqual(result["errors"], {"limit_exceeded": 1})
                self.assertNotIn("candidate_layout_hypotheses", result)

    def test_output_hypothesis_count_is_bounded_before_publication(self) -> None:
        post_gob = bytearray(_exact_span(2, 8, b"SND:"))
        post_gob.extend(_exact_span(1, 4, b"ACL:"))
        result = _scan_files(
            {"level.POP": _make_pop(bytes(post_gob))},
            replace(scorer.ScanLimits(), maximum_output_hypotheses=1),
        )

        self.assertFalse(result["complete"])
        self.assertEqual(result["errors"], {"limit_exceeded": 1})
        self.assertNotIn("candidate_layout_hypotheses", result)

    def test_cumulative_terrain_record_budget_is_enforced(self) -> None:
        payload = _make_pop(_exact_span(2, 8, b"SND:"))
        result = _scan_files(
            {"one.POP": payload, "two.POP": payload},
            replace(scorer.ScanLimits(), maximum_total_terrain_records=1),
        )

        self.assertFalse(result["complete"])
        self.assertEqual(result["errors"], {"limit_exceeded": 1})
        self.assertNotIn("candidate_layout_hypotheses", result)

    def test_boolean_and_excessive_slot_limits_are_rejected(self) -> None:
        for limits in (
            replace(scorer.ScanLimits(), maximum_files=True),
            replace(scorer.ScanLimits(), maximum_count_field_slots=65),
        ):
            with self.subTest(limits=limits):
                result = _scan_files({"level.POP": _make_pop(b"")}, limits)
                self.assertFalse(result["complete"])
                self.assertEqual(result["errors"], {"limit_exceeded": 1})

    def test_reparse_root_and_traversal_entry_are_rejected(self) -> None:
        payload = _make_pop(_exact_span(2, 8, b"SND:"))
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "level.POP").write_bytes(payload)
            with mock.patch.object(scorer, "_stat_is_reparse", return_value=True):
                root_result = scorer.scan_root(root)
        self.assertFalse(root_result["complete"])
        self.assertEqual(root_result["errors"], {"unsafe_input": 1})
        self.assertNotIn("candidate_layout_hypotheses", root_result)

        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "level.POP").write_bytes(payload)
            with mock.patch.object(
                scorer, "_stat_is_reparse", side_effect=(False, True)
            ):
                entry_result = scorer.scan_root(root)
        self.assertFalse(entry_result["complete"])
        self.assertEqual(entry_result["errors"], {"unsafe_input": 1})
        self.assertNotIn("candidate_layout_hypotheses", entry_result)

    def test_directory_depth_and_walk_entry_budgets_reject_before_file_reads(self) -> None:
        payload = _make_pop(_exact_span(2, 8, b"SND:"))
        for limits in (
            replace(scorer.ScanLimits(), maximum_directory_depth=0),
            replace(scorer.ScanLimits(), maximum_walk_entries=1),
        ):
            with self.subTest(limits=limits):
                result = _scan_files({"nested/level.POP": payload}, limits)
                self.assertFalse(result["complete"])
                self.assertIn("limit_exceeded", result["errors"])
                self.assertNotIn("candidate_layout_hypotheses", result)

    def test_cli_error_is_sanitized(self) -> None:
        private_path = "C:/owned/private/SECRET_DISC_ROOT"
        output = io.StringIO()
        with mock.patch.object(
            sys, "argv", ["score_pop_section_layout_hypotheses.py", private_path]
        ):
            with redirect_stdout(output):
                self.assertEqual(scorer.main(), 2)
        result = json.loads(output.getvalue())
        self.assertFalse(result["complete"])
        self.assertNotIn("SECRET", output.getvalue())
        self.assertNotIn("candidate_layout_hypotheses", result)

    def test_empty_root_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            result = scorer.scan_root(Path(temporary))
        self.assertFalse(result["complete"])
        self.assertEqual(result["errors"], {"no_candidates": 1})


if __name__ == "__main__":
    unittest.main()
