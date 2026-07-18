from __future__ import annotations

import copy
import io
import json
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import Any


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import validate_vum_read_trace as validator  # noqa: E402


EXPECTED_SIZE = 256
MAX_REPORT_BYTES = 16 * 1024 * 1024
U32_OVERFLOW = 1 << 32
U64_OVERFLOW = 1 << 64


def complete_document() -> dict[str, Any]:
    """Return one complete report covering every supported EE load width."""
    return {
        "schema": "omega-vum-read-trace-v1",
        "status": "complete",
        "stop_reason": "quiet_frames",
        "selected_copy_count": 1,
        "frame_count": 1,
        "matching_event_observed": True,
        "ee_reads": [
            {"relative_offset": 0, "width": 1, "execution_count": 2},
            {"relative_offset": 1, "width": 1, "execution_count": 1},
            {"relative_offset": 2, "width": 2, "execution_count": 1},
            {"relative_offset": 4, "width": 4, "execution_count": 1},
            {"relative_offset": 8, "width": 8, "execution_count": 1},
            {"relative_offset": 16, "width": 16, "execution_count": 1},
        ],
        "anonymous_sites": [
            {
                "anonymous_site": 0,
                "width": 1,
                "execution_count": 3,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 1,
                "loop_candidate_heuristic": True,
            },
            {
                "anonymous_site": 1,
                "width": 2,
                "execution_count": 1,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 2,
                "maximum_relative_offset": 2,
                "loop_candidate_heuristic": False,
            },
            {
                "anonymous_site": 2,
                "width": 4,
                "execution_count": 1,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 4,
                "maximum_relative_offset": 4,
                "loop_candidate_heuristic": False,
            },
            {
                "anonymous_site": 3,
                "width": 8,
                "execution_count": 1,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 8,
                "maximum_relative_offset": 8,
                "loop_candidate_heuristic": False,
            },
            {
                "anonymous_site": 4,
                "width": 16,
                "execution_count": 1,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 16,
                "maximum_relative_offset": 16,
                "loop_candidate_heuristic": False,
            },
        ],
        "vif1_unpack_chunks": [
            {
                "source_relative_offset": 64,
                "source_width": 4,
                "source_word_count": 2,
                "remaining_output_element_count_before_chunk": 4,
                "event_count": 2,
            }
        ],
    }


def empty_document() -> dict[str, Any]:
    document = complete_document()
    document["matching_event_observed"] = False
    document["ee_reads"] = []
    document["anonymous_sites"] = []
    document["vif1_unpack_chunks"] = []
    return document


def document_with_read(offset: int, width: int, count: int = 1) -> dict[str, Any]:
    document = complete_document()
    document["ee_reads"] = [
        {"relative_offset": offset, "width": width, "execution_count": count}
    ]
    document["anonymous_sites"] = [
        {
            "anonymous_site": 0,
            "width": width,
            "execution_count": count,
            "distinct_relative_offset_count": 1,
            "minimum_relative_offset": offset,
            "maximum_relative_offset": offset,
            "loop_candidate_heuristic": False,
        }
    ]
    document["vif1_unpack_chunks"] = []
    return document


def compact_json(document: dict[str, Any]) -> bytes:
    return json.dumps(document, separators=(",", ":"), sort_keys=False).encode("utf-8")


def assign(document: Any, path: tuple[Any, ...], value: Any) -> None:
    target = document
    for component in path[:-1]:
        target = target[component]
    target[path[-1]] = value


class VumReadTraceValidatorTests(unittest.TestCase):
    def assert_invalid(self, document: dict[str, Any]) -> None:
        with self.assertRaises(validator.TraceValidationError):
            validator.validate_trace_document(document, EXPECTED_SIZE)

    def test_valid_lifecycle_status_forms(self) -> None:
        cases: list[tuple[str, str, dict[str, Any]]] = []
        for stop_reason in ("quiet_frames", "maximum_frames"):
            document = complete_document()
            document["stop_reason"] = stop_reason
            cases.append(("complete", stop_reason, document))

        maximum_limit = complete_document()
        maximum_limit.update(stop_reason="maximum_frames", frame_count=10_000)
        cases.append(("complete", "maximum_frames at limit", maximum_limit))

        partial = complete_document()
        partial["status"] = "partial"
        partial["stop_reason"] = "vm_shutdown"
        cases.append(("partial", "vm_shutdown", partial))

        for stop_reason in ("vm_shutdown", "maximum_frames"):
            inconclusive = empty_document()
            inconclusive["status"] = "inconclusive"
            inconclusive["stop_reason"] = stop_reason
            cases.append(("inconclusive", stop_reason, inconclusive))

        failed = empty_document()
        failed.update(
            status="failed",
            stop_reason="failure",
            failure_reason="invalid_environment",
            selected_copy_count=0,
            frame_count=0,
        )
        cases.append(("failed", "failure", failed))

        failed_after_event = copy.deepcopy(failed)
        failed_after_event["matching_event_observed"] = True
        cases.append(("failed after event", "failure", failed_after_event))

        for status, stop_reason, document in cases:
            with self.subTest(status=status, stop_reason=stop_reason):
                validator.validate_trace_document(document, EXPECTED_SIZE)

    def test_all_load_widths_and_exact_end_bounds_are_valid(self) -> None:
        validator.validate_trace_document(complete_document(), EXPECTED_SIZE)
        for width in (1, 2, 4, 8, 16):
            with self.subTest(width=width):
                validator.validate_trace_document(
                    document_with_read(EXPECTED_SIZE - width, width), EXPECTED_SIZE
                )

    def test_malformed_lifecycle_combinations_are_rejected(self) -> None:
        cases: list[tuple[str, dict[str, Any]]] = []
        for status, stop_reason in (
            ("unknown", "quiet_frames"),
            ("complete", "vm_shutdown"),
            ("partial", "maximum_frames"),
            ("inconclusive", "quiet_frames"),
            ("failed", "quiet_frames"),
        ):
            document = complete_document()
            document["status"] = status
            document["stop_reason"] = stop_reason
            if status == "failed":
                document["failure_reason"] = "invalid_environment"
            cases.append((f"{status}/{stop_reason}", document))

        false_with_events = complete_document()
        false_with_events["matching_event_observed"] = False
        cases.append(("false matching flag with events", false_with_events))

        zero_frame_complete = complete_document()
        zero_frame_complete["frame_count"] = 0
        cases.append(("complete before first frame", zero_frame_complete))

        zero_frame_maximum = empty_document()
        zero_frame_maximum.update(
            status="inconclusive", stop_reason="maximum_frames", frame_count=0
        )
        cases.append(("maximum-frame stop before first frame", zero_frame_maximum))

        for status, matching in (("partial", True), ("inconclusive", False)):
            maximum_preempts_shutdown = complete_document() if matching else empty_document()
            maximum_preempts_shutdown.update(
                status=status, stop_reason="vm_shutdown", frame_count=10_000
            )
            cases.append((f"{status} shutdown at frame limit", maximum_preempts_shutdown))

        quiet_at_limit = complete_document()
        quiet_at_limit.update(stop_reason="quiet_frames", frame_count=10_000)
        cases.append(("quiet stop at frame limit", quiet_at_limit))

        true_without_events = empty_document()
        true_without_events["matching_event_observed"] = True
        cases.append(("true matching flag without events", true_without_events))

        reason_on_success = complete_document()
        reason_on_success["failure_reason"] = "invalid_environment"
        cases.append(("failure reason on success", reason_on_success))

        missing_reason = empty_document()
        missing_reason.update(status="failed", stop_reason="failure", selected_copy_count=0)
        cases.append(("missing failure reason", missing_reason))

        unknown_reason = copy.deepcopy(missing_reason)
        unknown_reason["failure_reason"] = "private_unclassified_failure"
        cases.append(("unknown failure reason", unknown_reason))

        failed_with_detail = complete_document()
        failed_with_detail.update(
            status="failed",
            stop_reason="failure",
            failure_reason="counter_overflow",
        )
        cases.append(("failed report with aggregates", failed_with_detail))

        for label, document in cases:
            with self.subTest(label=label):
                self.assert_invalid(document)

    def test_boolean_and_integer_types_are_not_interchangeable(self) -> None:
        numeric_paths = (
            ("selected_copy_count",),
            ("frame_count",),
            ("ee_reads", 0, "relative_offset"),
            ("ee_reads", 0, "width"),
            ("ee_reads", 0, "execution_count"),
            ("anonymous_sites", 0, "anonymous_site"),
            ("anonymous_sites", 0, "width"),
            ("anonymous_sites", 0, "execution_count"),
            ("anonymous_sites", 0, "distinct_relative_offset_count"),
            ("anonymous_sites", 0, "minimum_relative_offset"),
            ("anonymous_sites", 0, "maximum_relative_offset"),
            ("vif1_unpack_chunks", 0, "source_relative_offset"),
            ("vif1_unpack_chunks", 0, "source_width"),
            ("vif1_unpack_chunks", 0, "source_word_count"),
            (
                "vif1_unpack_chunks",
                0,
                "remaining_output_element_count_before_chunk",
            ),
            ("vif1_unpack_chunks", 0, "event_count"),
        )
        for path in numeric_paths:
            with self.subTest(path=path):
                document = complete_document()
                assign(document, path, True)
                self.assert_invalid(document)

        for path in (
            ("matching_event_observed",),
            ("anonymous_sites", 0, "loop_candidate_heuristic"),
        ):
            with self.subTest(path=path):
                document = complete_document()
                assign(document, path, 1)
                self.assert_invalid(document)

    def test_invalid_count_ranges_are_rejected(self) -> None:
        cases = (
            (("selected_copy_count",), 0),
            (("selected_copy_count",), -1),
            (("selected_copy_count",), 65),
            (("frame_count",), -1),
            (("frame_count",), 10001),
            (("ee_reads", 0, "execution_count"), 0),
            (("ee_reads", 0, "execution_count"), -1),
            (("ee_reads", 0, "execution_count"), U64_OVERFLOW),
            (("anonymous_sites", 0, "execution_count"), 0),
            (("anonymous_sites", 0, "distinct_relative_offset_count"), 0),
            (
                ("anonymous_sites", 0, "distinct_relative_offset_count"),
                U32_OVERFLOW,
            ),
            (("vif1_unpack_chunks", 0, "source_word_count"), 0),
            (("vif1_unpack_chunks", 0, "source_word_count"), -1),
            (("vif1_unpack_chunks", 0, "source_word_count"), U32_OVERFLOW),
            (
                (
                    "vif1_unpack_chunks",
                    0,
                    "remaining_output_element_count_before_chunk",
                ),
                -1,
            ),
            (
                (
                    "vif1_unpack_chunks",
                    0,
                    "remaining_output_element_count_before_chunk",
                ),
                U32_OVERFLOW,
            ),
            (("vif1_unpack_chunks", 0, "event_count"), 0),
            (("vif1_unpack_chunks", 0, "event_count"), -1),
            (("vif1_unpack_chunks", 0, "event_count"), U64_OVERFLOW),
        )
        for path, value in cases:
            with self.subTest(path=path, value=value):
                document = complete_document()
                assign(document, path, value)
                self.assert_invalid(document)

        zero_is_valid = complete_document()
        zero_is_valid.update(status="partial", stop_reason="vm_shutdown", frame_count=0)
        zero_is_valid["vif1_unpack_chunks"][0][
            "remaining_output_element_count_before_chunk"
        ] = 0
        validator.validate_trace_document(zero_is_valid, EXPECTED_SIZE)

    def test_anonymous_offset_memberships_respect_tracer_capacity(self) -> None:
        expected_size = 1024
        distinct_count = expected_size
        site_count = 1025
        document = complete_document()
        document["ee_reads"] = [
            {
                "relative_offset": offset,
                "width": 1,
                "execution_count": site_count,
            }
            for offset in range(distinct_count)
        ]
        document["anonymous_sites"] = [
            {
                "anonymous_site": ordinal,
                "width": 1,
                "execution_count": distinct_count,
                "distinct_relative_offset_count": distinct_count,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": distinct_count - 1,
                "loop_candidate_heuristic": True,
            }
            for ordinal in range(site_count)
        ]
        with self.assertRaises(validator.TraceValidationError):
            validator.validate_trace_document(document, expected_size)

    def test_ee_order_duplicates_alignment_and_bounds_are_rejected(self) -> None:
        out_of_order = complete_document()
        out_of_order["ee_reads"][0], out_of_order["ee_reads"][1] = (
            out_of_order["ee_reads"][1],
            out_of_order["ee_reads"][0],
        )

        duplicate = complete_document()
        duplicate["ee_reads"].insert(1, copy.deepcopy(duplicate["ee_reads"][0]))

        cases = {
            "out of order": out_of_order,
            "duplicate": duplicate,
            "unaligned": document_with_read(3, 2),
            "overrun": document_with_read(EXPECTED_SIZE - 8, 16),
            "negative offset": document_with_read(-1, 1),
            "unsupported width": document_with_read(0, 3),
        }
        for label, document in cases.items():
            with self.subTest(label=label):
                self.assert_invalid(document)

    def test_anonymous_ordinals_order_and_equal_tuples(self) -> None:
        equal = document_with_read(0, 1, count=2)
        equal["anonymous_sites"] = [
            {
                "anonymous_site": ordinal,
                "width": 1,
                "execution_count": 1,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 0,
                "loop_candidate_heuristic": False,
            }
            for ordinal in (0, 1)
        ]
        validator.validate_trace_document(equal, EXPECTED_SIZE)

        wrong_ordinal = complete_document()
        wrong_ordinal["anonymous_sites"][0]["anonymous_site"] = 1
        self.assert_invalid(wrong_ordinal)

        out_of_order = complete_document()
        out_of_order["anonymous_sites"][0], out_of_order["anonymous_sites"][1] = (
            out_of_order["anonymous_sites"][1],
            out_of_order["anonymous_sites"][0],
        )
        for ordinal, site in enumerate(out_of_order["anonymous_sites"]):
            site["anonymous_site"] = ordinal
        self.assert_invalid(out_of_order)

    def test_anonymous_range_distinct_heuristic_and_cross_counts(self) -> None:
        cases: list[tuple[str, dict[str, Any]]] = []

        inverted = complete_document()
        inverted["anonymous_sites"][0]["minimum_relative_offset"] = 2
        cases.append(("inverted min/max", inverted))

        too_many_distinct = complete_document()
        too_many_distinct["anonymous_sites"][0]["distinct_relative_offset_count"] = 3
        cases.append(("distinct exceeds observed offsets", too_many_distinct))

        wrong_heuristic = complete_document()
        wrong_heuristic["anonymous_sites"][0]["loop_candidate_heuristic"] = False
        cases.append(("wrong heuristic", wrong_heuristic))

        wrong_total = complete_document()
        wrong_total["anonymous_sites"][0]["execution_count"] += 1
        cases.append(("execution total mismatch", wrong_total))

        wrong_width_totals = complete_document()
        wrong_width_totals["anonymous_sites"][0]["execution_count"] = 2
        wrong_width_totals["anonymous_sites"][1]["execution_count"] = 2
        cases.append(("per-width execution mismatch", wrong_width_totals))

        max_not_observed = complete_document()
        max_not_observed["anonymous_sites"][0]["maximum_relative_offset"] = 2
        cases.append(("maximum not observed for width", max_not_observed))

        membership = document_with_read(0, 1)
        membership["ee_reads"].append(
            {"relative_offset": 2, "width": 1, "execution_count": 1}
        )
        membership["anonymous_sites"][0].update(
            execution_count=2,
            distinct_relative_offset_count=2,
            maximum_relative_offset=2,
            loop_candidate_heuristic=True,
        )
        validator.validate_trace_document(membership, EXPECTED_SIZE)

        min_not_observed = copy.deepcopy(membership)
        min_not_observed["anonymous_sites"][0]["minimum_relative_offset"] = 1
        cases.append(("minimum not observed", min_not_observed))

        max_not_observed = copy.deepcopy(membership)
        max_not_observed["anonymous_sites"][0]["maximum_relative_offset"] = 1
        cases.append(("maximum not observed", max_not_observed))

        missing_distinct_offsets = copy.deepcopy(membership)
        missing_distinct_offsets["ee_reads"][0]["execution_count"] = 2
        missing_distinct_offsets["anonymous_sites"][0].update(
            execution_count=3,
            distinct_relative_offset_count=3,
        )
        cases.append(("distinct count exceeds observed offsets", missing_distinct_offsets))

        observed_outside_range = document_with_read(0, 1, count=3)
        observed_outside_range["ee_reads"] = [
            {"relative_offset": offset, "width": 1, "execution_count": 1}
            for offset in (0, 2, 10)
        ]
        observed_outside_range["anonymous_sites"][0].update(
            execution_count=3,
            distinct_relative_offset_count=3,
            maximum_relative_offset=2,
            loop_candidate_heuristic=True,
        )
        cases.append(("distinct offset only outside site range", observed_outside_range))

        insufficient_extrema_executions = document_with_read(0, 1)
        insufficient_extrema_executions["ee_reads"] = [
            {"relative_offset": 0, "width": 1, "execution_count": 1},
            {"relative_offset": 2, "width": 1, "execution_count": 3},
        ]
        insufficient_extrema_executions["anonymous_sites"] = [
            {
                "anonymous_site": ordinal,
                "width": 1,
                "execution_count": 2,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 2,
                "loop_candidate_heuristic": True,
            }
            for ordinal in (0, 1)
        ]
        cases.append(("extrema execution deficit", insufficient_extrema_executions))

        reversed_singletons = document_with_read(0, 1)
        reversed_singletons["ee_reads"] = [
            {"relative_offset": 0, "width": 1, "execution_count": 100},
            {"relative_offset": 1, "width": 1, "execution_count": 1},
        ]
        reversed_singletons["anonymous_sites"] = [
            {
                "anonymous_site": 0,
                "width": 1,
                "execution_count": 1,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 0,
                "loop_candidate_heuristic": False,
            },
            {
                "anonymous_site": 1,
                "width": 1,
                "execution_count": 100,
                "distinct_relative_offset_count": 1,
                "minimum_relative_offset": 1,
                "maximum_relative_offset": 1,
                "loop_candidate_heuristic": False,
            },
        ]
        cases.append(("singleton execution deficit", reversed_singletons))

        saturated_range_deficit = document_with_read(0, 1)
        saturated_range_deficit["ee_reads"] = [
            {"relative_offset": 0, "width": 1, "execution_count": 3},
            {"relative_offset": 1, "width": 1, "execution_count": 1},
            {"relative_offset": 2, "width": 1, "execution_count": 2},
        ]
        saturated_range_deficit["anonymous_sites"] = [
            {
                "anonymous_site": ordinal,
                "width": 1,
                "execution_count": 3,
                "distinct_relative_offset_count": 3,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 2,
                "loop_candidate_heuristic": True,
            }
            for ordinal in (0, 1)
        ]
        cases.append(("saturated range execution deficit", saturated_range_deficit))

        disjoint_range_execution_deficit = document_with_read(0, 1)
        disjoint_range_execution_deficit["ee_reads"] = [
            {"relative_offset": 0, "width": 1, "execution_count": 1},
            {"relative_offset": 2, "width": 1, "execution_count": 1},
            {"relative_offset": 10, "width": 1, "execution_count": 99},
            {"relative_offset": 12, "width": 1, "execution_count": 1},
        ]
        disjoint_range_execution_deficit["anonymous_sites"] = [
            {
                "anonymous_site": 0,
                "width": 1,
                "execution_count": 100,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 2,
                "loop_candidate_heuristic": True,
            },
            {
                "anonymous_site": 1,
                "width": 1,
                "execution_count": 2,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 10,
                "maximum_relative_offset": 12,
                "loop_candidate_heuristic": True,
            },
        ]
        cases.append(
            ("disjoint range execution deficit", disjoint_range_execution_deficit)
        )

        for label, document in cases:
            with self.subTest(label=label):
                self.assert_invalid(document)

    def test_overlapping_anonymous_ranges_cannot_overbook_shared_ee_capacity(self) -> None:
        document = complete_document()
        document["ee_reads"] = [
            {"relative_offset": offset, "width": 1, "execution_count": 2}
            for offset in range(5)
        ]
        document["anonymous_sites"] = [
            {
                "anonymous_site": 0,
                "width": 1,
                "execution_count": 3,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 2,
                "loop_candidate_heuristic": True,
            },
            {
                "anonymous_site": 1,
                "width": 1,
                "execution_count": 3,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": 2,
                "loop_candidate_heuristic": True,
            },
            {
                "anonymous_site": 2,
                "width": 1,
                "execution_count": 4,
                "distinct_relative_offset_count": 2,
                "minimum_relative_offset": 3,
                "maximum_relative_offset": 4,
                "loop_candidate_heuristic": True,
            },
        ]
        document["vif1_unpack_chunks"] = []
        validator.validate_trace_document(document, EXPECTED_SIZE)

        impossible = copy.deepcopy(document)
        impossible["anonymous_sites"][0]["execution_count"] = 4
        impossible["anonymous_sites"][1]["execution_count"] = 4
        impossible["anonymous_sites"][2]["execution_count"] = 2
        self.assert_invalid(impossible)

    def test_vif_width_order_duplicates_alignment_bounds_and_counts(self) -> None:
        valid = complete_document()
        valid["vif1_unpack_chunks"] = [
            {
                "source_relative_offset": 64,
                "source_width": 4,
                "source_word_count": 1,
                "remaining_output_element_count_before_chunk": 8,
                "event_count": 1,
            },
            {
                "source_relative_offset": 64,
                "source_width": 4,
                "source_word_count": 2,
                "remaining_output_element_count_before_chunk": 4,
                "event_count": 1,
            },
            {
                "source_relative_offset": 80,
                "source_width": 4,
                "source_word_count": 1,
                "remaining_output_element_count_before_chunk": 0,
                "event_count": 1,
            },
        ]
        validator.validate_trace_document(valid, EXPECTED_SIZE)

        out_of_order = copy.deepcopy(valid)
        out_of_order["vif1_unpack_chunks"][0], out_of_order["vif1_unpack_chunks"][1] = (
            out_of_order["vif1_unpack_chunks"][1],
            out_of_order["vif1_unpack_chunks"][0],
        )

        duplicate = copy.deepcopy(valid)
        repeated = copy.deepcopy(duplicate["vif1_unpack_chunks"][0])
        repeated["event_count"] = 99
        duplicate["vif1_unpack_chunks"].insert(1, repeated)

        cases = {
            "source width": copy.deepcopy(valid),
            "out of order": out_of_order,
            "duplicate key": duplicate,
            "unaligned": copy.deepcopy(valid),
            "overrun": copy.deepcopy(valid),
        }
        cases["source width"]["vif1_unpack_chunks"][0]["source_width"] = 8
        cases["unaligned"]["vif1_unpack_chunks"][0]["source_relative_offset"] = 65
        cases["overrun"]["vif1_unpack_chunks"] = [
            {
                "source_relative_offset": EXPECTED_SIZE - 4,
                "source_width": 4,
                "source_word_count": 2,
                "remaining_output_element_count_before_chunk": 0,
                "event_count": 1,
            }
        ]
        for label, document in cases.items():
            with self.subTest(label=label):
                self.assert_invalid(document)

        exact_end = complete_document()
        exact_end["vif1_unpack_chunks"] = [
            {
                "source_relative_offset": EXPECTED_SIZE - 4,
                "source_width": 4,
                "source_word_count": 1,
                "remaining_output_element_count_before_chunk": 0,
                "event_count": 1,
            }
        ]
        validator.validate_trace_document(exact_end, EXPECTED_SIZE)

    def test_unknown_and_recursively_forbidden_fields_are_rejected(self) -> None:
        unknown_top = complete_document()
        unknown_top["extra"] = 1
        self.assert_invalid(unknown_top)

        unknown_nested = complete_document()
        unknown_nested["ee_reads"][0]["extra"] = 1
        self.assert_invalid(unknown_nested)

        forbidden_fields = (
            "guest_pc",
            "absolute_address",
            "ram_address",
            "crc32",
            "payload_hash",
            "source_path",
            "asset_name",
            "payload_bytes",
            "decoded_data",
            "instruction",
            "opcode",
            "register",
        )
        for field in forbidden_fields:
            with self.subTest(field=field):
                document = complete_document()
                document["unexpected"] = {"nested": [{field: "PRIVATE_VALUE"}]}
                self.assert_invalid(document)

    def test_raw_parser_rejects_duplicate_nonfinite_and_oversize_json(self) -> None:
        raw = compact_json(complete_document())
        duplicate = b'{"schema":"omega-vum-read-trace-v1",' + raw[1:]
        with self.assertRaises(validator.TraceValidationError):
            validator.parse_and_validate_trace(duplicate, EXPECTED_SIZE)

        for token in (b"NaN", b"Infinity", b"-Infinity"):
            with self.subTest(token=token):
                nonfinite = raw.replace(b'"frame_count":1', b'"frame_count":' + token)
                with self.assertRaises(validator.TraceValidationError):
                    validator.parse_and_validate_trace(nonfinite, EXPECTED_SIZE)

        oversize = b" " * (MAX_REPORT_BYTES + 1)
        with self.assertRaises(validator.TraceValidationError):
            validator.parse_and_validate_trace(oversize, EXPECTED_SIZE)

    def test_valid_producer_report_below_byte_cap_is_not_rejected_by_node_cap(self) -> None:
        expected_size = 131_072
        document = complete_document()
        document["ee_reads"] = [
            {"relative_offset": offset, "width": 1, "execution_count": 1}
            for offset in range(expected_size)
        ]
        document["anonymous_sites"] = [
            {
                "anonymous_site": 0,
                "width": 1,
                "execution_count": expected_size,
                "distinct_relative_offset_count": expected_size,
                "minimum_relative_offset": 0,
                "maximum_relative_offset": expected_size - 1,
                "loop_candidate_heuristic": True,
            }
        ]
        raw = compact_json(document)
        self.assertLess(len(raw), MAX_REPORT_BYTES)
        validator.parse_and_validate_trace(raw, expected_size)

    def test_repeat_requires_byte_identity(self) -> None:
        raw = compact_json(complete_document())
        validator.validate_repeat(raw, raw, EXPECTED_SIZE)
        with self.assertRaises(validator.TraceValidationError):
            validator.validate_repeat(raw, raw + b"\n", EXPECTED_SIZE)

    def run_cli(self, raw: bytes) -> tuple[int, str, str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            report = Path(temporary_directory) / "PRIVATE-SOURCE-NAME.json"
            report.write_bytes(raw)
            stdout = io.StringIO()
            stderr = io.StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = validator.main(
                    [str(report), "--expected-size", str(EXPECTED_SIZE)]
                )
            combined = stdout.getvalue() + stderr.getvalue()
            self.assertNotIn(str(report), combined)
            self.assertNotIn(report.name, combined)
            return result, stdout.getvalue(), stderr.getvalue()

    def test_cli_complete_noncomplete_and_malformed_behavior_is_sanitized(self) -> None:
        result, _, _ = self.run_cli(compact_json(complete_document()))
        self.assertEqual(result, 0)

        partial = complete_document()
        partial.update(status="partial", stop_reason="vm_shutdown")
        result, _, _ = self.run_cli(compact_json(partial))
        self.assertEqual(result, 2)

        private_value = "PRIVATE-PC-VALUE-7f00dead"
        malformed = (
            b'{"schema":"omega-vum-read-trace-v1","guest_pc":"'
            + private_value.encode("ascii")
            + b'"'
        )
        result, stdout, stderr = self.run_cli(malformed)
        self.assertEqual(result, 1)
        self.assertNotIn(private_value, stdout + stderr)


if __name__ == "__main__":
    unittest.main()
