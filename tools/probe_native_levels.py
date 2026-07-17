#!/usr/bin/env python3
"""Run OpenOmega's headless native startup against every discovered level."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
from pathlib import Path


MAX_LEVEL_DIRECTORIES = 4096
LEVEL_SUMMARY = re.compile(r"OpenOmega level: code=([A-Z0-9]+) terrain_cells=(\d+)\b")


def discover_levels(root: Path) -> list[str]:
    gamedata = root / "GAMEDATA"
    if not gamedata.is_dir() or gamedata.is_symlink():
        raise ValueError("data root has no safe GAMEDATA directory")

    levels: list[str] = []
    visited = 0
    for candidate in gamedata.iterdir():
        visited += 1
        if visited > MAX_LEVEL_DIRECTORIES:
            raise ValueError("GAMEDATA directory count exceeds safety limit")
        if candidate.is_symlink() or not candidate.is_dir():
            continue
        pop = candidate / "DATA.POP"
        if pop.is_file() and not pop.is_symlink():
            levels.append(candidate.name)
    return sorted(levels, key=str.upper)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("openomega", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("--timeout", type=float, default=60.0)
    args = parser.parse_args()

    executable = args.openomega.resolve(strict=True)
    root = args.root.resolve(strict=True)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")

    try:
        levels = discover_levels(root)
    except (OSError, ValueError) as error:
        print(json.dumps({"levels": 0, "valid": 0, "errors": [str(error)]}, sort_keys=True))
        return 1
    if not levels:
        print(json.dumps({"levels": 0, "valid": 0, "errors": ["no level POP files found"]}, sort_keys=True))
        return 1

    valid = 0
    terrain_cells = 0
    errors: list[dict[str, object]] = []
    for level in levels:
        try:
            completed = subprocess.run(
                [
                    str(executable),
                    f"--data-root={root}",
                    f"--level={level}",
                    "--probe-only",
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=args.timeout,
            )
        except subprocess.TimeoutExpired:
            errors.append({"level": level, "error": "probe timed out"})
            continue

        match = LEVEL_SUMMARY.search(completed.stdout)
        if completed.returncode != 0 or match is None or match.group(1) != level.upper():
            errors.append(
                {
                    "level": level,
                    "exit": completed.returncode,
                    "error": completed.stderr.strip() or "missing native level summary",
                }
            )
            continue
        valid += 1
        terrain_cells += int(match.group(2))

    print(
        json.dumps(
            {
                "levels": len(levels),
                "valid": valid,
                "terrain_cells": terrain_cells,
                "errors": errors,
            },
            separators=(",", ":"),
            sort_keys=True,
        )
    )
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
