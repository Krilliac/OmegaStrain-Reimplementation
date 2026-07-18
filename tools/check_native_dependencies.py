#!/usr/bin/env python3
"""Enforce the native source dependency boundaries documented by the project."""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')


@dataclass(frozen=True)
class ModuleRule:
    path_prefix: str
    name: str
    forbidden_omega_prefixes: tuple[str, ...]
    platform_neutral: bool = False


# Longest path prefixes must come first. Tests and omega_tool intentionally sit
# outside this table: they are verification/inspection clients rather than
# shipping runtime libraries.
MODULE_RULES = (
    ModuleRule(
        "native/apps/openomega/sdl_",
        "omega_sdl_backend",
        ("omega/retail/",),
    ),
    ModuleRule(
        "native/apps/openomega/",
        "openomega",
        ("omega/retail/",),
    ),
    ModuleRule(
        "native/include/omega/simulation/",
        "omega_simulation",
        ("omega/content/", "omega/retail/", "omega/runtime/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/simulation/",
        "omega_simulation",
        ("omega/content/", "omega/retail/", "omega/runtime/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/runtime/",
        "omega_runtime",
        ("omega/retail/",),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/runtime/",
        "omega_runtime",
        ("omega/retail/",),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/content/",
        "omega_content",
        ("omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/content/",
        "omega_content",
        ("omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/retail/",
        "omega_retail_formats",
        ("omega/content/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/retail/",
        "omega_retail_formats",
        ("omega/content/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/asset/",
        "omega_assets",
        ("omega/content/", "omega/retail/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/archive/",
        "omega_core",
        ("omega/content/", "omega/retail/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/include/omega/vfs/",
        "omega_core",
        ("omega/content/", "omega/retail/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/archive/",
        "omega_core",
        ("omega/content/", "omega/retail/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/asset/",
        "omega_core",
        ("omega/content/", "omega/retail/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
    ModuleRule(
        "native/src/vfs/",
        "omega_core",
        ("omega/content/", "omega/retail/", "omega/runtime/", "omega/simulation/"),
        platform_neutral=True,
    ),
)

PCSX2_INCLUDE_PATTERN = re.compile(r"(?:^|[/_-])pcsx2(?:[/_.-]|$)", re.IGNORECASE)
PLATFORM_INCLUDE_PREFIXES = (
    "sdl",
    "windows.h",
    "d3d",
    "dxgi",
    "directx",
    "vulkan/",
    "metal/",
    "cocoa/",
)


def module_rule(relative_path: Path) -> ModuleRule | None:
    normalized = relative_path.as_posix()
    for rule in MODULE_RULES:
        if normalized.startswith(rule.path_prefix):
            return rule
    return None


def check_include(rule: ModuleRule, relative_path: Path, line_number: int, include: str) -> str | None:
    if PCSX2_INCLUDE_PATTERN.search(include):
        return (
            f"{relative_path.as_posix()}:{line_number}: {rule.name} includes forbidden "
            f"PCSX2 header <{include}>"
        )
    if any(include.startswith(prefix) for prefix in rule.forbidden_omega_prefixes):
        return (
            f"{relative_path.as_posix()}:{line_number}: {rule.name} includes forbidden "
            f"dependency <{include}>"
        )
    include_lower = include.lower()
    if rule.platform_neutral and any(
        include_lower.startswith(prefix) for prefix in PLATFORM_INCLUDE_PREFIXES
    ):
        return (
            f"{relative_path.as_posix()}:{line_number}: {rule.name} includes platform header "
            f"<{include}>"
        )
    return None


def check_file(root: Path, path: Path) -> list[str]:
    relative_path = path.relative_to(root)
    rule = module_rule(relative_path)
    if rule is None:
        return []

    errors: list[str] = []
    with path.open("r", encoding="utf-8", newline="") as source:
        for line_number, line in enumerate(source, start=1):
            match = INCLUDE_PATTERN.match(line)
            if match is None:
                continue
            error = check_include(rule, relative_path, line_number, match.group(1))
            if error is not None:
                errors.append(error)
    return errors


def check_tree(root: Path) -> tuple[int, list[str]]:
    native_root = root / "native"
    if not native_root.is_dir():
        return 0, [f"native source root is missing: {native_root}"]

    checked = 0
    errors: list[str] = []
    for path in sorted(native_root.rglob("*")):
        if not path.is_file() or path.is_symlink() or path.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        if module_rule(path.relative_to(root)) is None:
            continue
        checked += 1
        errors.extend(check_file(root, path))
    return checked, errors


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="repository root (defaults to the parent of tools/)",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    checked, errors = check_tree(root)
    if errors:
        print(f"native-dependency gate: FAILED ({len(errors)} issue(s), {checked} files checked)")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"native-dependency gate: OK ({checked} files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
