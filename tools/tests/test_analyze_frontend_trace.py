from __future__ import annotations

import copy
import io
import json
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

from tools import analyze_frontend_trace as analyzer
from tools import validate_frontend_trace as validator


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
FIXTURE_ROOT = REPOSITORY_ROOT / "analysis" / "fixtures" / "frontend_trace_v1"


def valid_document() -> dict[str, object]:
    return json.loads((FIXTURE_ROOT / "valid.json").read_bytes())


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


def expected_summary() -> dict[str, object]:
    zero_events = {
        "control": 0,
        "transition": 0,
        "render": 0,
        "resource": 0,
        "other": 0,
    }
    zero_resources = {"texture": 0, "font": 0, "audio": 0, "other": 0}
    return {
        "schema": "omega-frontend-trace-summary-v1",
        "input_relative_frame": 8,
        "actions": [
            {
                "scenario": "previous",
                "event_class_deltas": {**zero_events, "transition": 1},
                "resource_class_deltas": zero_resources.copy(),
                "transition_row_count_delta": 1,
                "transition_shape_matches_idle": False,
                "first_transition_divergence_frame": 8,
            },
            {
                "scenario": "next",
                "event_class_deltas": {**zero_events, "transition": 2},
                "resource_class_deltas": zero_resources.copy(),
                "transition_row_count_delta": 2,
                "transition_shape_matches_idle": False,
                "first_transition_divergence_frame": 8,
            },
            {
                "scenario": "confirm",
                "event_class_deltas": {**zero_events, "transition": 2},
                "resource_class_deltas": zero_resources.copy(),
                "transition_row_count_delta": 2,
                "transition_shape_matches_idle": False,
                "first_transition_divergence_frame": 8,
            },
            {
                "scenario": "back",
                "event_class_deltas": {**zero_events, "transition": 1},
                "resource_class_deltas": zero_resources.copy(),
                "transition_row_count_delta": 1,
                "transition_shape_matches_idle": False,
                "first_transition_divergence_frame": 9,
            },
        ],
    }


def scenario_reports(document: dict[str, object], scenario: str) -> list[dict[str, object]]:
    reports = document["reports"]
    assert isinstance(reports, list)
    return [
        report
        for report in reports
        if isinstance(report, dict) and report.get("scenario") == scenario
    ]


def run_main(args: list[str]) -> tuple[int, str, str]:
    output = io.StringIO()
    errors = io.StringIO()
    with redirect_stdout(output), redirect_stderr(errors):
        code = analyzer.main(args)
    return code, output.getvalue(), errors.getvalue()


class FrontendTraceAnalyzerTests(unittest.TestCase):
    def test_valid_fixture_produces_the_exact_summary_contract(self) -> None:
        document = valid_document()
        expected = expected_summary()

        self.assertEqual(analyzer.summarize_trace_document(document), expected)
        self.assertEqual(analyzer.encode_summary_document(document), canonical_bytes(expected))
        self.assertEqual(
            analyzer.parse_and_analyze_trace((FIXTURE_ROOT / "valid.json").read_bytes()),
            canonical_bytes(expected),
        )

        self.assertEqual(tuple(expected), ("schema", "input_relative_frame", "actions"))
        for action in expected["actions"]:
            self.assertEqual(
                tuple(action),
                (
                    "scenario",
                    "event_class_deltas",
                    "resource_class_deltas",
                    "transition_row_count_delta",
                    "transition_shape_matches_idle",
                    "first_transition_divergence_frame",
                ),
            )
            self.assertEqual(tuple(action["event_class_deltas"]), analyzer.EVENT_CLASSES)
            self.assertEqual(tuple(action["resource_class_deltas"]), analyzer.RESOURCE_CLASSES)

    def test_output_is_deterministic_ascii_bounded_and_independent(self) -> None:
        source = valid_document()
        first_document = analyzer.summarize_trace_document(source)
        first = analyzer.encode_summary_document(source)
        second = analyzer.encode_summary_document(copy.deepcopy(source))

        self.assertEqual(first, second)
        self.assertLessEqual(len(first), analyzer._MAXIMUM_SUMMARY_BYTES)
        self.assertEqual(first[-1:], b"\n")
        self.assertNotEqual(first[-2:-1], b"\n")
        self.assertEqual(first.decode("ascii").encode("ascii"), first)

        source["input_relative_frame"] = 9
        source_reports = source["reports"]
        assert isinstance(source_reports, list)
        source_reports.clear()
        self.assertEqual(first_document, expected_summary())

    def test_every_tracked_invalid_source_fixture_fails_closed(self) -> None:
        invalid = sorted(FIXTURE_ROOT.glob("invalid-*.json"))
        self.assertGreater(len(invalid), 0)
        for path in invalid:
            with self.subTest(fixture=path.name):
                with self.assertRaises(validator.FrontendTraceValidationError):
                    analyzer.parse_and_analyze_trace(path.read_bytes())

    def test_signed_event_and_resource_deltas_are_exact(self) -> None:
        document = valid_document()
        for report in scenario_reports(document, "previous"):
            event_counts = report["event_class_counts"]
            site_events = report["site_events"]
            resource_counts = report["resource_class_totals"]
            assert isinstance(event_counts, dict)
            assert isinstance(site_events, list)
            assert isinstance(resource_counts, dict)

            event_counts["control"] = 3
            event_counts["render"] = 1
            event_counts["resource"] = 6
            site_events[0]["count"] = 3
            site_events[2]["count"] = 1
            site_events[3]["count"] = 6
            resource_counts.update(texture=1, font=0, audio=4, other=1)

        summary = analyzer.summarize_trace_document(document)
        previous = summary["actions"][0]
        self.assertEqual(
            previous["event_class_deltas"],
            {
                "control": 2,
                "transition": 1,
                "render": -1,
                "resource": 2,
                "other": 0,
            },
        )
        self.assertEqual(
            previous["resource_class_deltas"],
            {"texture": -1, "font": -1, "audio": 4, "other": 0},
        )

    def test_identical_prefix_and_boundary_transition_shapes_are_distinguished(self) -> None:
        identical = valid_document()
        for report in scenario_reports(identical, "previous"):
            report["event_class_counts"]["transition"] = 1
            report["site_events"][1]["count"] = 1
            report["transition_ordinals"] = [{"frame_delta": 0, "ordinal": 0}]

        identical_previous = analyzer.summarize_trace_document(identical)["actions"][0]
        self.assertEqual(identical_previous["transition_row_count_delta"], 0)
        self.assertTrue(identical_previous["transition_shape_matches_idle"])
        self.assertIsNone(identical_previous["first_transition_divergence_frame"])

        boundary = valid_document()
        for report in scenario_reports(boundary, "previous"):
            report["transition_ordinals"][1]["frame_delta"] = 15
        boundary_previous = analyzer.summarize_trace_document(boundary)["actions"][0]
        self.assertEqual(boundary_previous["first_transition_divergence_frame"], 15)

        earlier_action = ((0, 0), (8, 1))
        later_idle = ((0, 0), (10, 1))
        self.assertEqual(
            analyzer._first_transition_divergence_frame(later_idle, earlier_action), 8
        )

    def test_different_scenario_frame_spans_fail_closed(self) -> None:
        document = valid_document()
        for report in scenario_reports(document, "previous"):
            report["frame_count"] = 17

        with self.assertRaisesRegex(
            analyzer.FrontendTraceAnalysisError, "scenario frame spans differ"
        ):
            analyzer.summarize_trace_document(document)

    def test_source_schema_drift_and_output_overflow_fail_closed(self) -> None:
        drift_cases = (
            ("SCHEMA", "omega-frontend-trace-v2"),
            ("SCENARIOS", ("idle", "future")),
            ("TRIALS", (0, 1, 2)),
            ("EVENT_CLASSES", ("future",)),
            ("RESOURCE_CLASSES", ("future",)),
        )
        for attribute, value in drift_cases:
            with self.subTest(attribute=attribute), mock.patch.object(
                validator, attribute, value
            ):
                with self.assertRaises(analyzer.FrontendTraceAnalysisError):
                    analyzer.summarize_trace_document(valid_document())

        with mock.patch.object(analyzer, "_MAXIMUM_SUMMARY_BYTES", 1):
            with self.assertRaises(analyzer.FrontendTraceAnalysisError):
                analyzer.encode_summary_document(valid_document())

        with self.assertRaises(validator.FrontendTraceValidationError):
            analyzer.parse_and_analyze_trace(
                b" " * (analyzer._MAXIMUM_INPUT_BYTES + 1)
            )

    def test_cli_success_help_argument_and_privacy_contracts(self) -> None:
        valid_path = FIXTURE_ROOT / "valid.json"
        code, output, errors = run_main([str(valid_path)])
        self.assertEqual(code, 0)
        self.assertEqual(output.encode("ascii"), canonical_bytes(expected_summary()))
        self.assertEqual(errors, "")

        for help_arg in ("-h", "--help"):
            with self.subTest(help_arg=help_arg):
                code, output, errors = run_main([help_arg])
                self.assertEqual(code, 0)
                self.assertEqual(output, "usage: analyze_frontend_trace.py REPORT\n")
                self.assertEqual(errors, "")

        for args in ([], ["one", "two"]):
            with self.subTest(args=args):
                code, output, errors = run_main(args)
                self.assertEqual(code, 1)
                self.assertEqual(output, "")
                self.assertEqual(errors, "Front-end trace analysis failed.\n")

        secret = "ZXQ_PRIVATE_TRACE_PATH_492"
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            path = Path(directory) / f"{secret}.json"
            path.write_bytes(b"not-json\n")
            code, output, errors = run_main([str(path)])
        self.assertEqual(code, 1)
        self.assertEqual(output, "")
        self.assertEqual(errors, "Front-end trace analysis failed.\n")
        self.assertNotIn(secret, output + errors)

        with mock.patch.object(
            analyzer,
            "parse_and_analyze_trace",
            side_effect=RuntimeError(f"{secret}: raw private detail"),
        ):
            code, output, errors = run_main([str(valid_path)])
        self.assertEqual(code, 1)
        self.assertEqual(output, "")
        self.assertEqual(errors, "Front-end trace analysis failed.\n")
        self.assertNotIn(secret, output + errors)

    def test_real_process_emits_exact_canonical_bytes_without_cr_translation(self) -> None:
        valid_path = FIXTURE_ROOT / "valid.json"
        completed = subprocess.run(
            [
                sys.executable,
                "-B",
                str(REPOSITORY_ROOT / "tools" / "analyze_frontend_trace.py"),
                str(valid_path),
            ],
            cwd=REPOSITORY_ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
            timeout=10,
        )

        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stdout, canonical_bytes(expected_summary()))
        self.assertEqual(completed.stdout.count(b"\n"), 1)
        self.assertNotIn(b"\r", completed.stdout)
        self.assertEqual(completed.stderr, b"")

    def test_isolated_mode_help_succeeds_by_absolute_path_from_unrelated_directory(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as unrelated_directory:
            completed = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    "-E",
                    "-s",
                    "-S",
                    "-B",
                    str(REPOSITORY_ROOT / "tools" / "analyze_frontend_trace.py"),
                    "--help",
                ],
                cwd=unrelated_directory,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )

        self.assertEqual(completed.returncode, 0)
        self.assertEqual(
            completed.stdout.decode("ascii").strip(),
            "usage: analyze_frontend_trace.py REPORT",
        )
        self.assertEqual(completed.stderr, b"")

    def test_isolated_mode_analyzes_fixture_by_absolute_path_from_unrelated_directory(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as unrelated_directory:
            completed = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    "-E",
                    "-s",
                    "-S",
                    "-B",
                    str(REPOSITORY_ROOT / "tools" / "analyze_frontend_trace.py"),
                    str(FIXTURE_ROOT / "valid.json"),
                ],
                cwd=unrelated_directory,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )

        self.assertEqual(completed.returncode, 0)
        self.assertEqual(completed.stdout, canonical_bytes(expected_summary()))
        self.assertEqual(completed.stderr, b"")

    def test_direct_execution_does_not_mask_validator_internal_import_error(
        self,
    ) -> None:
        # When the sibling validator IS importable but raises ModuleNotFoundError
        # for one of ITS OWN dependencies, that error must surface. The path-based
        # fallback is reserved for the sibling itself being absent from sys.path.
        analyzer_source = (
            REPOSITORY_ROOT / "tools" / "analyze_frontend_trace.py"
        ).read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            sandbox = Path(directory)
            (sandbox / "analyze_frontend_trace.py").write_bytes(analyzer_source)
            (sandbox / "validate_frontend_trace.py").write_text(
                "import phantom_dependency_xyz\n", encoding="ascii"
            )
            completed = subprocess.run(
                [
                    sys.executable,
                    "-B",
                    str(sandbox / "analyze_frontend_trace.py"),
                    "--help",
                ],
                cwd=directory,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(b"phantom_dependency_xyz", completed.stderr)
        self.assertEqual(completed.stdout, b"")

    def test_isolated_fallback_does_not_replace_unrelated_package_module(
        self,
    ) -> None:
        analyzer_path = REPOSITORY_ROOT / "tools" / "analyze_frontend_trace.py"
        with tempfile.TemporaryDirectory() as unrelated_directory:
            unrelated_path = Path(unrelated_directory) / "unrelated_validator.py"
            wrapper = "\n".join(
                (
                    "import runpy, sys, types",
                    "occupied = types.ModuleType('tools.validate_frontend_trace')",
                    f"occupied.__file__ = {str(unrelated_path)!r}",
                    "sys.modules['tools.validate_frontend_trace'] = occupied",
                    f"runpy.run_path({str(analyzer_path)!r}, run_name='__main__')",
                )
            )
            completed = subprocess.run(
                [sys.executable, "-I", "-E", "-s", "-S", "-B", "-c", wrapper],
                cwd=unrelated_directory,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )

        self.assertNotEqual(completed.returncode, 0)
        self.assertIn(b"tools.validate_frontend_trace", completed.stderr)
        self.assertIn(b"already occupied", completed.stderr)
        self.assertEqual(completed.stdout, b"")

    def test_success_summary_excludes_source_identity_surfaces(self) -> None:
        rendered = analyzer.encode_summary_document(valid_document()).decode("ascii").lower()
        for forbidden in (
            "site_events",
            '"site"',
            "transition_ordinals",
            '"trial"',
            '"path"',
            '"name"',
            '"hash"',
            '"address"',
            '"payload"',
            '"raw"',
        ):
            self.assertNotIn(forbidden, rendered)


if __name__ == "__main__":
    unittest.main()
