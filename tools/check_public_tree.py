#!/usr/bin/env python3
"""Fail when the Git index contains private inputs, retail payloads, or common secrets."""

from __future__ import annotations

import re
import subprocess
from pathlib import Path


BLOCKED_ROOTS = {
    "analysis/output",
    "build",
    "downloads",
    "private",
    "runtime",
    "third_party",
}

BLOCKED_EXTENSIONS = {
    ".bin", ".col", ".elf", ".erom", ".hog", ".irx", ".iso", ".lpd",
    ".mec", ".nvm", ".p2s", ".par", ".pop", ".pss", ".rom", ".rom0",
    ".rom1", ".rom2", ".skl", ".skm", ".so", ".tdx", ".vag", ".vpk",
    ".vum",
}

SECRET_PATTERNS = {
    "private-key marker": re.compile(rb"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----"),
    "GitHub token": re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{20,}\b"),
    "OpenAI-style token": re.compile(rb"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}\b"),
    "AWS access key": re.compile(rb"\bAKIA[0-9A-Z]{16}\b"),
}

MAX_TRACKED_BYTES = 5 * 1024 * 1024


def tracked_paths() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    return [Path(raw.decode("utf-8")) for raw in result.stdout.split(b"\0") if raw]


def check_path(path: Path) -> list[str]:
    errors: list[str] = []
    normalized = path.as_posix()
    root = normalized.split("/", 1)[0].lower()
    if root in BLOCKED_ROOTS:
        errors.append(f"blocked root: {normalized}")
    if path.suffix.lower() in BLOCKED_EXTENSIONS:
        errors.append(f"blocked retail/payload extension: {normalized}")
    if path.name.upper() == "SCUS_972.64":
        errors.append(f"retail executable name: {normalized}")
    if not path.is_file():
        errors.append(f"tracked path is missing from worktree: {normalized}")
        return errors

    size = path.stat().st_size
    if size > MAX_TRACKED_BYTES:
        errors.append(f"tracked file exceeds {MAX_TRACKED_BYTES} bytes: {normalized} ({size})")

    data = path.read_bytes()
    if b"\0" in data:
        errors.append(f"binary/NUL content requires explicit policy review: {normalized}")
    if data.startswith(b"\x7fELF"):
        errors.append(f"ELF executable content: {normalized}")
    for label, pattern in SECRET_PATTERNS.items():
        if pattern.search(data):
            errors.append(f"{label}: {normalized}")
    return errors


def main() -> int:
    paths = tracked_paths()
    errors = [error for path in paths for error in check_path(path)]
    if errors:
        print(f"public-tree gate: FAILED ({len(errors)} issue(s))")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"public-tree gate: OK ({len(paths)} tracked text files checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
