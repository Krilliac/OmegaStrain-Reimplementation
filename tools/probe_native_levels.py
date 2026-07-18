#!/usr/bin/env python3
"""Run OpenOmega's headless native startup against every discovered level."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


MAX_LEVEL_DIRECTORIES = 4096
LEVEL_SUMMARY = re.compile(
    r"OpenOmega level: code=([A-Z0-9]+) terrain_cells=(\d+) spatial_meshes=(\d+)\b"
)
ERROR_CATEGORIES = (
    "timeout",
    "process_exit",
    "missing_summary",
    "summary_identity_mismatch",
    "cardinality_mismatch",
    "discovery",
    "config",
)


@dataclass
class ProbeResults:
    levels: int
    valid: int = 0
    terrain_cells: int = 0
    spatial_meshes: int = 0
    errors: list[dict[str, object]] = field(default_factory=list)
    error_categories: dict[str, int] = field(
        default_factory=lambda: {category: 0 for category in ERROR_CATEGORIES}
    )

    @property
    def error_count(self) -> int:
        return sum(self.error_categories.values())

    def record_error(
        self, category: str, detail: dict[str, object] | None = None
    ) -> None:
        self.error_categories[category] += 1
        if detail is not None:
            self.errors.append(detail)


Runner = Callable[..., Any]
Discoverer = Callable[[Path], list[str]]
PathResolver = Callable[[Path], Path]


class AggregateArgumentError(Exception):
    pass


class ProbeArgumentParser(argparse.ArgumentParser):
    def __init__(self, *, aggregate_only: bool) -> None:
        super().__init__()
        self.aggregate_only = aggregate_only

    def error(self, message: str) -> None:
        if self.aggregate_only:
            raise AggregateArgumentError from None
        super().error(message)


def discover_levels(
    root: Path, *, maximum_level_directories: int = MAX_LEVEL_DIRECTORIES
) -> list[str]:
    gamedata = root / "GAMEDATA"
    if not gamedata.is_dir() or gamedata.is_symlink():
        raise ValueError("data root has no safe GAMEDATA directory")

    levels: list[str] = []
    visited = 0
    for candidate in gamedata.iterdir():
        visited += 1
        if visited > maximum_level_directories:
            raise ValueError("GAMEDATA directory count exceeds safety limit")
        if candidate.is_symlink() or not candidate.is_dir():
            continue
        pop = candidate / "DATA.POP"
        if pop.is_file() and not pop.is_symlink():
            levels.append(candidate.name)
    return sorted(levels, key=str.upper)


def resolve_existing(path: Path) -> Path:
    return path.resolve(strict=True)


def _detailed_error(level: str, completed: Any) -> dict[str, object]:
    return {
        "level": level,
        "exit": completed.returncode,
        "error": completed.stderr.strip() or "missing native level summary",
    }


def probe_levels(
    executable: Path,
    root: Path,
    levels: Sequence[str],
    timeout: float,
    *,
    runner: Runner = subprocess.run,
    aggregate_only: bool = False,
) -> ProbeResults:
    results = ProbeResults(levels=len(levels))
    for level in levels:
        command = [
            str(executable),
            f"--data-root={root}",
            f"--level={level}",
            "--probe-only",
        ]
        try:
            completed = runner(
                command,
                check=False,
                capture_output=True,
                text=True,
                timeout=timeout,
            )
        except subprocess.TimeoutExpired:
            results.record_error(
                "timeout",
                None
                if aggregate_only
                else {"level": level, "error": "probe timed out"},
            )
            continue
        except Exception:
            if not aggregate_only:
                raise
            results.record_error("process_exit")
            continue

        try:
            match = LEVEL_SUMMARY.search(completed.stdout)
            if completed.returncode != 0:
                category = "process_exit"
            elif match is None:
                category = "missing_summary"
            elif match.group(1) != level.upper():
                category = "summary_identity_mismatch"
            elif match.group(2) != match.group(3):
                category = "cardinality_mismatch"
            else:
                category = None
        except Exception:
            if not aggregate_only:
                raise
            results.record_error("process_exit")
            continue

        if category is not None:
            results.record_error(
                category,
                None if aggregate_only else _detailed_error(level, completed),
            )
            continue
        results.valid += 1
        results.terrain_cells += int(match.group(2))
        results.spatial_meshes += int(match.group(3))
    return results


def detailed_document(results: ProbeResults) -> dict[str, object]:
    return {
        "levels": results.levels,
        "valid": results.valid,
        "terrain_cells": results.terrain_cells,
        "spatial_meshes": results.spatial_meshes,
        "errors": results.errors,
    }


def aggregate_document(results: ProbeResults) -> dict[str, object]:
    return {
        "levels": results.levels,
        "valid": results.valid,
        "terrain_cells": results.terrain_cells,
        "spatial_meshes": results.spatial_meshes,
        "errors": results.error_count,
        "error_categories": {
            category: results.error_categories[category]
            for category in ERROR_CATEGORIES
        },
    }


def aggregate_failure_document(category: str) -> dict[str, object]:
    results = ProbeResults(levels=0)
    results.error_categories[category] = 1
    return aggregate_document(results)


def _print_compact(document: dict[str, object]) -> None:
    print(json.dumps(document, separators=(",", ":"), sort_keys=True))


def main(
    argv: Sequence[str] | None = None,
    *,
    runner: Runner = subprocess.run,
    discoverer: Discoverer = discover_levels,
    path_resolver: PathResolver = resolve_existing,
) -> int:
    raw_argv = list(sys.argv[1:] if argv is None else argv)
    aggregate_requested = any(
        argument == "--aggregate-only" or argument.startswith("--aggregate-only=")
        for argument in raw_argv
    )
    parser = ProbeArgumentParser(aggregate_only=aggregate_requested)
    parser.add_argument("openomega", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    parser.add_argument(
        "--aggregate-only",
        action="store_true",
        help="emit fixed aggregate counts without level identities or raw diagnostics",
    )
    try:
        args = parser.parse_args(raw_argv)
    except AggregateArgumentError:
        _print_compact(aggregate_failure_document("config"))
        return 1

    try:
        executable = path_resolver(args.openomega)
        root = path_resolver(args.root)
    except Exception:
        if not args.aggregate_only:
            raise
        _print_compact(aggregate_failure_document("config"))
        return 1

    if args.timeout <= 0:
        if not args.aggregate_only:
            parser.error("--timeout must be positive")
        _print_compact(aggregate_failure_document("config"))
        return 1

    try:
        levels = discoverer(root)
    except (OSError, ValueError) as error:
        if args.aggregate_only:
            _print_compact(aggregate_failure_document("discovery"))
        else:
            print(
                json.dumps(
                    {
                        "levels": 0,
                        "valid": 0,
                        "errors": [str(error)],
                    },
                    sort_keys=True,
                )
            )
        return 1
    except Exception:
        if not args.aggregate_only:
            raise
        _print_compact(aggregate_failure_document("discovery"))
        return 1
    if not levels:
        if args.aggregate_only:
            _print_compact(aggregate_failure_document("discovery"))
        else:
            print(
                json.dumps(
                    {"levels": 0, "valid": 0, "errors": ["no level POP files found"]},
                    sort_keys=True,
                )
            )
        return 1

    results = probe_levels(
        executable,
        root,
        levels,
        args.timeout,
        runner=runner,
        aggregate_only=args.aggregate_only,
    )
    if args.aggregate_only:
        _print_compact(aggregate_document(results))
    else:
        _print_compact(detailed_document(results))
    return 1 if results.error_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
