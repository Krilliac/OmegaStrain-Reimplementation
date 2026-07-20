from __future__ import annotations

import copy
import io
import json
import sys
import tempfile
import unittest
from collections import OrderedDict
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import Any


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT))
from tools import validate_frontend_trace as validator  # noqa: E402


FIXTURE_ROOT = REPOSITORY_ROOT / "analysis" / "fixtures" / "frontend_trace_v1"


def _transition_rows(signature: tuple[tuple[int, int], ...]) -> list[dict[str, int]]:
    return [
        {"frame_delta": frame_delta, "ordinal": ordinal}
        for frame_delta, ordinal in signature
    ]


def _report(
    scenario: str, trial: int, signature: tuple[tuple[int, int], ...]
) -> dict[str, Any]:
    transition_count = len(signature)
    return {
        "scenario": scenario,
        "trial": trial,
        "frame_count": 16,
        "event_class_counts": {
            "control": 1,
            "transition": transition_count,
            "render": 2,
            "resource": 4,
            "other": 1,
        },
        "site_events": [
            {"frame_delta": 0, "site": 0, "event_class": "control", "count": 1},
            {
                "frame_delta": 0,
                "site": 1,
                "event_class": "transition",
                "count": transition_count,
            },
            {"frame_delta": 1, "site": 2, "event_class": "render", "count": 2},
            {"frame_delta": 1, "site": 3, "event_class": "resource", "count": 4},
            {"frame_delta": 1, "site": 4, "event_class": "other", "count": 1},
        ],
        "transition_ordinals": _transition_rows(signature),
        "resource_class_totals": {
            "texture": 2,
            "font": 1,
            "audio": 0,
            "other": 1,
        },
    }


def valid_document() -> dict[str, Any]:
    signatures = {
        "idle": ((0, 0),),
        "previous": ((0, 0), (8, 1)),
        "next": ((0, 0), (8, 1), (2, 0)),
        "confirm": ((0, 0), (8, 1), (2, 2)),
        "back": ((0, 0), (9, 1)),
    }
    reports = [
        _report(scenario, trial, signatures[scenario])
        for scenario in validator.SCENARIOS
        for trial in validator.TRIALS
    ]
    return {
        "schema": "omega-frontend-trace-v1",
        "input_relative_frame": 8,
        "reports": reports,
    }


def canonical_bytes(document: object) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=True,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=False,
        ).encode("ascii")
        + b"\n"
    )


class FrontendTraceValidatorTests(unittest.TestCase):
    def assert_invalid(self, document: object) -> None:
        with self.assertRaises(validator.FrontendTraceValidationError):
            validator.validate_trace_document(document)

    def test_generated_document_round_trips_canonically(self) -> None:
        document = valid_document()
        raw = validator.encode_trace_document(document)
        self.assertEqual(raw, canonical_bytes(document))
        self.assertEqual(validator.parse_and_validate_trace(raw), document)

    def test_tracked_valid_fixture_is_exactly_the_generated_contract(self) -> None:
        fixture = (FIXTURE_ROOT / "valid.json").read_bytes()
        self.assertEqual(fixture, canonical_bytes(valid_document()))
        validator.parse_and_validate_trace(fixture)

    def test_tracked_invalid_fixtures_fail_closed(self) -> None:
        for path in sorted(FIXTURE_ROOT.glob("invalid-*.json")):
            with self.subTest(path=path.name):
                with self.assertRaises(validator.FrontendTraceValidationError):
                    validator.parse_and_validate_trace(path.read_bytes())

    def test_exact_scenario_and_trial_matrix_is_required(self) -> None:
        missing = valid_document()
        missing["reports"].pop()
        self.assert_invalid(missing)

        extra = valid_document()
        extra["reports"].append(copy.deepcopy(extra["reports"][-1]))
        self.assert_invalid(extra)

        wrong_scenario = valid_document()
        wrong_scenario["reports"][2]["scenario"] = "next"
        self.assert_invalid(wrong_scenario)

        wrong_trial = valid_document()
        wrong_trial["reports"][0]["trial"] = 1
        self.assert_invalid(wrong_trial)

        swapped = valid_document()
        swapped["reports"][0], swapped["reports"][1] = (
            swapped["reports"][1],
            swapped["reports"][0],
        )
        self.assert_invalid(swapped)

    def test_repeat_pairs_must_have_identical_normalized_bytes(self) -> None:
        mutators = (
            lambda repeat: repeat.__setitem__("frame_count", 17),
            lambda repeat: repeat["event_class_counts"].__setitem__("render", 3),
            lambda repeat: repeat["site_events"][2].__setitem__("count", 3),
            lambda repeat: repeat["transition_ordinals"][1].__setitem__(
                "frame_delta", 9
            ),
            lambda repeat: repeat["resource_class_totals"].update(
                texture=1, font=2
            ),
        )
        for mutate in mutators:
            with self.subTest(mutate=mutate):
                document = valid_document()
                repeat = document["reports"][3]
                mutate(repeat)
                self.assert_invalid(document)

    def test_at_least_one_action_transition_signature_must_differ_from_idle(self) -> None:
        document = valid_document()
        idle_rows = copy.deepcopy(document["reports"][0]["transition_ordinals"])
        for report in document["reports"]:
            report["transition_ordinals"] = copy.deepcopy(idle_rows)
            report["event_class_counts"]["transition"] = 1
            report["site_events"][1]["count"] = 1
        self.assert_invalid(document)

    def test_input_and_frame_counts_are_bounded_and_consistent(self) -> None:
        for value in (0, 121, True, 1.0):
            with self.subTest(input_frame=value):
                document = valid_document()
                document["input_relative_frame"] = value
                self.assert_invalid(document)

        for value in (0, 601, True, 16.0):
            with self.subTest(frame_count=value):
                document = valid_document()
                document["reports"][0]["frame_count"] = value
                self.assert_invalid(document)

        input_outside = valid_document()
        input_outside["reports"][0]["frame_count"] = 8
        self.assert_invalid(input_outside)

    def test_site_ids_are_first_seen_sequentially_and_keep_their_class(self) -> None:
        skipped = valid_document()
        skipped["reports"][0]["site_events"][2]["site"] = 3
        self.assert_invalid(skipped)

        changed_class = valid_document()
        changed_class["reports"][0]["site_events"].append(
            {"frame_delta": 1, "site": 0, "event_class": "other", "count": 1}
        )
        changed_class["reports"][0]["event_class_counts"]["other"] += 1
        self.assert_invalid(changed_class)

    def test_site_event_order_deltas_classes_and_totals_are_strict(self) -> None:
        unordered = valid_document()
        unordered["reports"][0]["site_events"][0], unordered["reports"][0][
            "site_events"
        ][1] = (
            unordered["reports"][0]["site_events"][1],
            unordered["reports"][0]["site_events"][0],
        )
        self.assert_invalid(unordered)

        outside = valid_document()
        outside["reports"][0]["site_events"][-1]["frame_delta"] = 15
        self.assert_invalid(outside)

        unknown_class = valid_document()
        unknown_class["reports"][0]["site_events"][0]["event_class"] = "unknown"
        self.assert_invalid(unknown_class)

        mismatch = valid_document()
        mismatch["reports"][0]["event_class_counts"]["render"] += 1
        self.assert_invalid(mismatch)

        zero_count = valid_document()
        zero_count["reports"][0]["site_events"][0]["count"] = 0
        self.assert_invalid(zero_count)

        empty = valid_document()
        empty["reports"][0]["site_events"] = []
        self.assert_invalid(empty)

    def test_transition_ordinals_are_first_seen_normalized_and_changing(self) -> None:
        skipped = valid_document()
        skipped["reports"][2]["transition_ordinals"][1]["ordinal"] = 2
        self.assert_invalid(skipped)

        repeated = valid_document()
        repeated["reports"][2]["transition_ordinals"][1]["ordinal"] = 0
        self.assert_invalid(repeated)

        missing_zero = valid_document()
        missing_zero["reports"][0]["transition_ordinals"][0]["ordinal"] = 1
        self.assert_invalid(missing_zero)

        outside = valid_document()
        outside["reports"][2]["transition_ordinals"][1]["frame_delta"] = 16
        self.assert_invalid(outside)

        count_mismatch = valid_document()
        count_mismatch["reports"][2]["event_class_counts"]["transition"] += 1
        count_mismatch["reports"][2]["site_events"][1]["count"] += 1
        self.assert_invalid(count_mismatch)

    def test_resource_classes_are_fixed_bounded_and_cross_counted(self) -> None:
        extra = valid_document()
        extra["reports"][0]["resource_class_totals"]["video"] = 0
        self.assert_invalid(extra)

        mismatch = valid_document()
        mismatch["reports"][0]["resource_class_totals"]["texture"] += 1
        self.assert_invalid(mismatch)

        over_limit = valid_document()
        over_limit["reports"][0]["resource_class_totals"]["texture"] = 1_000_001
        self.assert_invalid(over_limit)

    def test_unknown_and_privacy_bearing_fields_are_rejected_recursively(self) -> None:
        forbidden_fields = (
            "guest_pc",
            "absolute_address",
            "registers",
            "raw_state",
            "relative_offset",
            "payload_bytes",
            "asset_path",
            "source_name",
            "content_hash",
            "disc_crc",
            "decoded_data",
            "instruction",
            "opcode",
        )
        for field in forbidden_fields:
            with self.subTest(field=field):
                document = valid_document()
                document["reports"][0][field] = 1
                self.assert_invalid(document)

        unknown = valid_document()
        unknown["reports"][0]["unexpected"] = 1
        self.assert_invalid(unknown)

    def test_exact_object_key_order_is_required_at_every_level(self) -> None:
        root = valid_document()
        root_reordered = OrderedDict(
            (
                ("reports", root["reports"]),
                ("schema", root["schema"]),
                ("input_relative_frame", root["input_relative_frame"]),
            )
        )
        self.assert_invalid(root_reordered)

        document = valid_document()
        counts = document["reports"][0]["event_class_counts"]
        document["reports"][0]["event_class_counts"] = OrderedDict(
            reversed(tuple(counts.items()))
        )
        self.assert_invalid(document)

    def test_parser_requires_the_exact_canonical_byte_form(self) -> None:
        raw = canonical_bytes(valid_document())
        invalid_forms = (
            raw[:-1],
            raw + b"\n",
            b" " + raw,
            json.dumps(valid_document(), indent=2).encode("ascii") + b"\n",
        )
        for invalid in invalid_forms:
            with self.subTest(length=len(invalid)):
                with self.assertRaises(validator.FrontendTraceValidationError):
                    validator.parse_and_validate_trace(invalid)

    def test_parser_rejects_duplicates_nonfinite_invalid_utf8_and_oversize(self) -> None:
        raw = canonical_bytes(valid_document())
        duplicate = b'{"schema":"omega-frontend-trace-v1",' + raw[1:]
        with self.assertRaises(validator.FrontendTraceValidationError):
            validator.parse_and_validate_trace(duplicate)

        nonfinite = raw.replace(b'"frame_count":16', b'"frame_count":NaN', 1)
        with self.assertRaises(validator.FrontendTraceValidationError):
            validator.parse_and_validate_trace(nonfinite)

        with self.assertRaises(validator.FrontendTraceValidationError):
            validator.parse_and_validate_trace(b"\xff")
        with self.assertRaises(validator.FrontendTraceValidationError):
            validator.parse_and_validate_trace(b" " * ((4 * 1024 * 1024) + 1))
        with self.assertRaises(validator.FrontendTraceValidationError):
            validator.parse_and_validate_trace("not bytes")  # type: ignore[arg-type]

    def _run_cli(self, raw: bytes) -> tuple[int, str, str]:
        with tempfile.TemporaryDirectory() as temporary_directory:
            report = Path(temporary_directory) / "PRIVATE-REPORT-NAME.json"
            report.write_bytes(raw)
            stdout = io.StringIO()
            stderr = io.StringIO()
            with redirect_stdout(stdout), redirect_stderr(stderr):
                result = validator.main([str(report)])
            combined = stdout.getvalue() + stderr.getvalue()
            self.assertNotIn(str(report), combined)
            self.assertNotIn(report.name, combined)
            return result, stdout.getvalue(), stderr.getvalue()

    def test_cli_success_failure_help_and_usage_are_fixed_and_sanitized(self) -> None:
        result, stdout, stderr = self._run_cli(canonical_bytes(valid_document()))
        self.assertEqual(result, 0)
        self.assertEqual(
            stdout,
            "Front-end trace report is canonical, sanitized, and repeat-stable.\n",
        )
        self.assertEqual(stderr, "")

        private_value = "PRIVATE-PC-VALUE-7f00dead"
        result, stdout, stderr = self._run_cli(private_value.encode("ascii"))
        self.assertEqual(result, 1)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "Front-end trace report validation failed.\n")
        self.assertNotIn(private_value, stderr)

        help_stdout = io.StringIO()
        with redirect_stdout(help_stdout):
            self.assertEqual(validator.main(["--help"]), 0)
        self.assertEqual(help_stdout.getvalue(), "usage: validate_frontend_trace.py REPORT\n")

        usage_stderr = io.StringIO()
        with redirect_stderr(usage_stderr):
            self.assertEqual(validator.main([]), 1)
        self.assertEqual(usage_stderr.getvalue(), "Front-end trace report validation failed.\n")


if __name__ == "__main__":
    unittest.main()
