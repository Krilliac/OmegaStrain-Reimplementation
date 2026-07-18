from __future__ import annotations

import io
import json
import subprocess
import sys
import tempfile
import time
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from typing import Any
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import probe_native_levels as probe  # noqa: E402


PRIVATE_EXECUTABLE = "PRIVATE-EXECUTABLE-PATH"
PRIVATE_ROOT = "PRIVATE-DATA-ROOT-PATH"


def completed(
    returncode: int = 0, stdout: str = "", stderr: str = ""
) -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess([], returncode, stdout, stderr)


def summary(level: str, terrain: int, spatial: int | None = None) -> str:
    if spatial is None:
        spatial = terrain
    return (
        f"OpenOmega level: code={level.upper()} "
        f"terrain_cells={terrain} spatial_meshes={spatial}\n"
    )


def category_counts(**overrides: int) -> dict[str, int]:
    counts = {category: 0 for category in probe.ERROR_CATEGORIES}
    counts.update(overrides)
    return counts


def aggregate_expected(
    *,
    levels: int,
    valid: int = 0,
    terrain_cells: int = 0,
    spatial_meshes: int = 0,
    **categories: int,
) -> dict[str, object]:
    counts = category_counts(**categories)
    return {
        "levels": levels,
        "valid": valid,
        "terrain_cells": terrain_cells,
        "spatial_meshes": spatial_meshes,
        "errors": sum(counts.values()),
        "error_categories": counts,
    }


def compact_line(document: dict[str, object]) -> str:
    return json.dumps(document, separators=(",", ":"), sort_keys=True) + "\n"


class ScriptedRunner:
    def __init__(self, actions: dict[str, Any]) -> None:
        self.actions = actions
        self.calls: list[tuple[list[str], dict[str, object]]] = []

    def __call__(self, command: list[str], **kwargs: object) -> Any:
        self.calls.append((command, kwargs))
        level_argument = next(
            argument for argument in command if argument.startswith("--level=")
        )
        level = level_argument.removeprefix("--level=")
        action = self.actions[level]
        if isinstance(action, BaseException):
            raise action
        return action


class ProbeNativeLevelsTests(unittest.TestCase):
    def run_cli(
        self,
        argv: list[str],
        *,
        actions: dict[str, Any] | None = None,
        levels: list[str] | None = None,
        discoverer: Any | None = None,
        path_resolver: Any | None = None,
    ) -> tuple[int, str, str, ScriptedRunner]:
        runner = ScriptedRunner(actions or {})
        if discoverer is None:
            discovered = list(levels or [])
            discoverer = lambda _root: discovered
        if path_resolver is None:
            path_resolver = lambda path: path
        stdout = io.StringIO()
        stderr = io.StringIO()
        with redirect_stdout(stdout), redirect_stderr(stderr):
            result = probe.main(
                argv,
                runner=runner,
                discoverer=discoverer,
                path_resolver=path_resolver,
            )
        return result, stdout.getvalue(), stderr.getvalue(), runner

    def test_aggregate_all_success_has_exact_fixed_json_surface(self) -> None:
        levels = ["ALPHA01", "BETA02"]
        actions = {
            "ALPHA01": completed(stdout=summary("ALPHA01", 2)),
            "BETA02": completed(stdout=summary("BETA02", 3)),
        }
        result, stdout, stderr, runner = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only", "--timeout", "7"],
            actions=actions,
            levels=levels,
        )

        expected = aggregate_expected(
            levels=2, valid=2, terrain_cells=5, spatial_meshes=5
        )
        self.assertEqual(result, 0)
        self.assertEqual(stdout, compact_line(expected))
        self.assertEqual(stderr, "")
        self.assertEqual(set(json.loads(stdout)), set(expected))
        self.assertEqual(
            set(json.loads(stdout)["error_categories"]), set(probe.ERROR_CATEGORIES)
        )
        self.assertEqual(len(runner.calls), 2)
        for level, (command, kwargs) in zip(levels, runner.calls, strict=True):
            self.assertEqual(
                command,
                [
                    PRIVATE_EXECUTABLE,
                    f"--data-root={PRIVATE_ROOT}",
                    f"--level={level}",
                    "--probe-only",
                ],
            )
            self.assertEqual(
                kwargs,
                {
                    "check": False,
                    "capture_output": True,
                    "text": True,
                    "timeout": 7.0,
                },
            )

    def test_aggregate_classifies_every_per_level_failure_without_leaks(self) -> None:
        levels = ["GOOD01", "TIME02", "EXIT03", "MISS04", "IDENT05", "CARD06"]
        private_stderr = "PRIVATE-RAW-STDERR-MEMBER-NAME-9F00"
        private_stdout = "PRIVATE-RAW-STDOUT-PAYLOAD-HASH-7E00"
        actions = {
            "GOOD01": completed(stdout=summary("GOOD01", 4)),
            "TIME02": subprocess.TimeoutExpired(
                cmd="PRIVATE-TIMEOUT-COMMAND", timeout=3, output=private_stdout
            ),
            "EXIT03": completed(
                returncode=7, stdout=summary("EXIT03", 8), stderr=private_stderr
            ),
            "MISS04": completed(stdout=private_stdout, stderr=private_stderr),
            "IDENT05": completed(stdout=summary("OTHER05", 2)),
            "CARD06": completed(stdout=summary("CARD06", 3, 2)),
        }
        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"],
            actions=actions,
            levels=levels,
        )

        expected = aggregate_expected(
            levels=6,
            valid=1,
            terrain_cells=4,
            spatial_meshes=4,
            timeout=1,
            process_exit=1,
            missing_summary=1,
            summary_identity_mismatch=1,
            cardinality_mismatch=1,
        )
        self.assertEqual(result, 1)
        self.assertEqual(stdout, compact_line(expected))
        self.assertEqual(stderr, "")
        combined = stdout + stderr
        for private_value in (
            *levels,
            "OTHER05",
            private_stderr,
            private_stdout,
            PRIVATE_EXECUTABLE,
            PRIVATE_ROOT,
            "PRIVATE-TIMEOUT-COMMAND",
        ):
            self.assertNotIn(private_value, combined)

    def test_aggregate_sanitizes_process_launch_exceptions(self) -> None:
        private_error = "PRIVATE-LAUNCH-PATH-AND-PAYLOAD"
        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"],
            actions={"LAUNCH01": OSError(private_error)},
            levels=["LAUNCH01"],
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            compact_line(aggregate_expected(levels=1, process_exit=1)),
        )
        self.assertEqual(stderr, "")
        self.assertNotIn(private_error, stdout)
        self.assertNotIn("LAUNCH01", stdout)

        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"],
            actions={"LIMIT02": probe.ProbeOutputLimitExceeded()},
            levels=["LIMIT02"],
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            compact_line(aggregate_expected(levels=1, process_exit=1)),
        )
        self.assertEqual(stderr, "")
        self.assertNotIn("LIMIT02", stdout)

    def test_aggregate_rejects_unbounded_numeric_summary_without_traceback(self) -> None:
        private_count = "9" * 5000
        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"],
            actions={
                "HUGE01": completed(
                    stdout=(
                        "OpenOmega level: code=HUGE01 "
                        f"terrain_cells={private_count} spatial_meshes={private_count}\n"
                    )
                )
            },
            levels=["HUGE01"],
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            compact_line(aggregate_expected(levels=1, invalid_summary=1)),
        )
        self.assertEqual(stderr, "")
        self.assertNotIn(private_count, stdout)
        self.assertNotIn("Traceback", stdout + stderr)

    def test_detailed_default_output_remains_compatible(self) -> None:
        levels = ["GOOD01", "TIME02", "EXIT03", "MISS04", "IDENT05", "CARD06"]
        private_stderr = "PRIVATE-DEFAULT-STDERR"
        actions = {
            "GOOD01": completed(stdout=summary("GOOD01", 4)),
            "TIME02": subprocess.TimeoutExpired(cmd="ignored", timeout=60),
            "EXIT03": completed(returncode=7, stderr=private_stderr),
            "MISS04": completed(stderr=private_stderr),
            "IDENT05": completed(stdout=summary("OTHER05", 2)),
            "CARD06": completed(stdout=summary("CARD06", 3, 2)),
        }
        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT], actions=actions, levels=levels
        )

        expected = {
            "levels": 6,
            "valid": 1,
            "terrain_cells": 4,
            "spatial_meshes": 4,
            "errors": [
                {"level": "TIME02", "error": "probe timed out"},
                {"level": "EXIT03", "exit": 7, "error": private_stderr},
                {"level": "MISS04", "exit": 0, "error": private_stderr},
                {
                    "level": "IDENT05",
                    "exit": 0,
                    "error": "missing native level summary",
                },
                {
                    "level": "CARD06",
                    "exit": 0,
                    "error": "missing native level summary",
                },
            ],
        }
        self.assertEqual(result, 1)
        self.assertEqual(stdout, compact_line(expected))
        self.assertEqual(stderr, "")
        self.assertIn(private_stderr, stdout)
        self.assertNotIn("error_categories", stdout)

    def test_aggregate_config_and_discovery_failures_are_fixed_and_sanitized(self) -> None:
        private_config_error = "PRIVATE-CONFIG-PATH"

        def fail_resolution(_path: Path) -> Path:
            raise FileNotFoundError(private_config_error)

        result, stdout, stderr, runner = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"],
            path_resolver=fail_resolution,
        )
        self.assertEqual(result, 1)
        self.assertEqual(stdout, compact_line(aggregate_expected(levels=0, config=1)))
        self.assertEqual(stderr, "")
        self.assertNotIn(private_config_error, stdout)
        self.assertFalse(runner.calls)

        for non_finite_timeout in ("nan", "inf", "-inf"):
            with self.subTest(non_finite_timeout=non_finite_timeout):
                result, stdout, stderr, runner = self.run_cli(
                    [
                        PRIVATE_EXECUTABLE,
                        PRIVATE_ROOT,
                        "--aggregate-only",
                        "--timeout",
                        non_finite_timeout,
                    ],
                    levels=["UNREACHED02"],
                )
                self.assertEqual(result, 1)
                self.assertEqual(
                    stdout,
                    compact_line(aggregate_expected(levels=0, config=1)),
                )
                self.assertEqual(stderr, "")
                self.assertFalse(runner.calls)

        private_discovery_error = "PRIVATE-DISCOVERY-MEMBER"

        def fail_discovery(_root: Path) -> list[str]:
            raise ValueError(private_discovery_error)

        result, stdout, stderr, runner = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"],
            discoverer=fail_discovery,
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout, compact_line(aggregate_expected(levels=0, discovery=1))
        )
        self.assertEqual(stderr, "")
        self.assertNotIn(private_discovery_error, stdout)
        self.assertFalse(runner.calls)

        result, stdout, stderr, runner = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT, "--aggregate-only"], levels=[]
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout, compact_line(aggregate_expected(levels=0, discovery=1))
        )
        self.assertEqual(stderr, "")
        self.assertFalse(runner.calls)

        result, stdout, stderr, runner = self.run_cli(
            [
                PRIVATE_EXECUTABLE,
                PRIVATE_ROOT,
                "--aggregate-only",
                "--timeout",
                "0",
            ],
            levels=["UNREACHED01"],
        )
        self.assertEqual(result, 1)
        self.assertEqual(stdout, compact_line(aggregate_expected(levels=0, config=1)))
        self.assertEqual(stderr, "")
        self.assertFalse(runner.calls)

        for invalid_arguments, private_value in (
            (
                [
                    PRIVATE_EXECUTABLE,
                    PRIVATE_ROOT,
                    "--aggregate-only",
                    "--timeout",
                    "PRIVATE-TIMEOUT-VALUE",
                ],
                "PRIVATE-TIMEOUT-VALUE",
            ),
            (
                [
                    PRIVATE_EXECUTABLE,
                    PRIVATE_ROOT,
                    "--aggregate-only",
                    "--PRIVATE-UNKNOWN-OPTION",
                ],
                "PRIVATE-UNKNOWN-OPTION",
            ),
        ):
            with self.subTest(invalid_argument=private_value):
                result, stdout, stderr, runner = self.run_cli(invalid_arguments)
                self.assertEqual(result, 1)
                self.assertEqual(
                    stdout, compact_line(aggregate_expected(levels=0, config=1))
                )
                self.assertEqual(stderr, "")
                self.assertNotIn(private_value, stdout)
                self.assertFalse(runner.calls)

    def test_default_discovery_failure_and_empty_messages_remain_compatible(self) -> None:
        private_error = "PRIVATE-DETAILED-DISCOVERY-ERROR"

        def fail_discovery(_root: Path) -> list[str]:
            raise ValueError(private_error)

        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT], discoverer=fail_discovery
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            json.dumps(
                {"levels": 0, "valid": 0, "errors": [private_error]},
                sort_keys=True,
            )
            + "\n",
        )
        self.assertEqual(stderr, "")

        result, stdout, stderr, _ = self.run_cli(
            [PRIVATE_EXECUTABLE, PRIVATE_ROOT], levels=[]
        )
        self.assertEqual(result, 1)
        self.assertEqual(
            stdout,
            json.dumps(
                {
                    "levels": 0,
                    "valid": 0,
                    "errors": ["no level POP files found"],
                },
                sort_keys=True,
            )
            + "\n",
        )
        self.assertEqual(stderr, "")

    def test_discovery_sorts_and_rejects_or_skips_unsafe_entries(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            gamedata = root / "GAMEDATA"
            gamedata.mkdir()
            alpha = gamedata / "Alpha01"
            zulu = gamedata / "zulu02"
            skipped = gamedata / "Skipped03"
            incomplete = gamedata / "Incomplete04"
            for directory in (alpha, zulu, skipped, incomplete):
                directory.mkdir()
            for directory in (alpha, zulu, skipped):
                (directory / "DATA.POP").write_bytes(b"")
            (gamedata / "ordinary-file").write_bytes(b"")

            original_is_symlink = Path.is_symlink

            def candidate_symlink(path: Path) -> bool:
                return path == skipped or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", candidate_symlink):
                self.assertEqual(probe.discover_levels(root), ["Alpha01", "zulu02"])

            zulu_pop = zulu / "DATA.POP"

            def pop_symlink(path: Path) -> bool:
                return path == zulu_pop or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", pop_symlink):
                self.assertEqual(
                    probe.discover_levels(root), ["Alpha01", "Skipped03"]
                )

            def gamedata_symlink(path: Path) -> bool:
                return path == gamedata or original_is_symlink(path)

            with mock.patch.object(Path, "is_symlink", gamedata_symlink):
                with self.assertRaises(ValueError):
                    probe.discover_levels(root)

            with self.assertRaises(ValueError):
                probe.discover_levels(root, maximum_level_directories=1)

    def test_default_runner_bounds_combined_child_output(self) -> None:
        completed_process = probe.bounded_run(
            [
                sys.executable,
                "-c",
                "import sys; sys.stdout.write('A' * 100); sys.stderr.write('B' * 100)",
            ],
            check=False,
            capture_output=True,
            text=True,
            timeout=10.0,
        )
        self.assertEqual(completed_process.returncode, 0)
        self.assertEqual(completed_process.stdout, "A" * 100)
        self.assertEqual(completed_process.stderr, "B" * 100)

        with self.assertRaises(probe.ProbeOutputLimitExceeded):
            probe.bounded_run(
                [
                    sys.executable,
                    "-c",
                    (
                        "import sys; sys.stdout.buffer.write("
                        f"b'X' * {probe.MAX_PROBE_CAPTURE_BYTES + 1})"
                    ),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=10.0,
            )

        with self.assertRaises(probe.ProbeOutputLimitExceeded):
            probe.bounded_run(
                [
                    sys.executable,
                    "-c",
                    (
                        "import sys; "
                        "sys.stdout.buffer.write(b'Y' * 40000); sys.stdout.flush(); "
                        "sys.stderr.buffer.write(b'Z' * 40000); sys.stderr.flush()"
                    ),
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=10.0,
            )

        with self.assertRaises(subprocess.TimeoutExpired):
            probe.bounded_run(
                [sys.executable, "-c", "import time; time.sleep(30)"],
                check=False,
                capture_output=True,
                text=True,
                timeout=0.2,
            )

    def test_default_runner_terminates_inherited_pipe_descendants(self) -> None:
        descendant = "import time; time.sleep(30)"
        overflow_parent = (
            "import subprocess,sys,time; "
            f"subprocess.Popen([sys.executable,'-c',{descendant!r}],"
            "stdout=sys.stdout,stderr=sys.stderr); "
            f"sys.stdout.buffer.write(b'Q' * {probe.MAX_PROBE_CAPTURE_BYTES + 1}); "
            "sys.stdout.flush(); time.sleep(30)"
        )
        started = time.monotonic()
        with self.assertRaises(probe.ProbeOutputLimitExceeded):
            probe.bounded_run(
                [sys.executable, "-c", overflow_parent],
                check=False,
                capture_output=True,
                text=True,
                timeout=10.0,
            )
        self.assertLess(time.monotonic() - started, 5.0)

        timeout_parent = (
            "import subprocess,sys,time; "
            f"subprocess.Popen([sys.executable,'-c',{descendant!r}],"
            "stdout=sys.stdout,stderr=sys.stderr); time.sleep(30)"
        )
        started = time.monotonic()
        with self.assertRaises(subprocess.TimeoutExpired):
            probe.bounded_run(
                [sys.executable, "-c", timeout_parent],
                check=False,
                capture_output=True,
                text=True,
                timeout=0.2,
            )
        self.assertLess(time.monotonic() - started, 5.0)


if __name__ == "__main__":
    unittest.main()
