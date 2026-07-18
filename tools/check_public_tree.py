#!/usr/bin/env python3
"""Fail when the Git index contains private inputs, retail payloads, or common secrets."""

from __future__ import annotations

import re
import subprocess
from dataclasses import dataclass
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
    ".7z", ".a", ".avi", ".bin", ".bmp", ".cbs", ".chd", ".col",
    ".cso", ".dds", ".dll", ".elf", ".erom", ".exe", ".flac", ".gif",
    ".glb", ".gltf", ".gz", ".hog", ".img", ".irx", ".iso", ".jpg",
    ".jpeg", ".key", ".ktx", ".lib", ".lpd", ".map", ".max", ".mec",
    ".mkv", ".mp3", ".mp4", ".nvm", ".ogg", ".p12", ".p2s", ".par",
    ".pdb", ".pem", ".pfx", ".png", ".pop", ".pss", ".psu", ".rar",
    ".resym", ".rom", ".rom0", ".rom1", ".rom2", ".sav", ".ska",
    ".skas", ".skl", ".skm", ".so", ".sps", ".sys", ".tar", ".tdx", ".tga", ".vag",
    ".vpk", ".vum", ".wav", ".webm", ".webp", ".xps", ".xz", ".zip",
}

SECRET_PATTERNS = {
    "private-key marker": re.compile(
        rb"-----BEGIN (?:RSA |EC |DSA |OPENSSH |PGP )?PRIVATE KEY(?: BLOCK)?-----"
    ),
    "GitHub token": re.compile(rb"\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})\b"),
    "OpenAI-style token": re.compile(rb"\bsk-(?:proj-)?[A-Za-z0-9_-]{20,}\b"),
    "AWS access key": re.compile(rb"\bAKIA[0-9A-Z]{16}\b"),
}

MAX_TRACKED_BYTES = 5 * 1024 * 1024
PS2_EXECUTABLE_NAME = re.compile(
    r"(?:SCUS|SLUS|SLES|SCES|SLPS|SLPM|SCPS|SCPM|PBPX|PAPX)[_-]?\d{3}[._]\d{2}",
    re.IGNORECASE,
)
SENSITIVE_NAMES = {
    ".env",
    "credentials.json",
    "id_dsa",
    "id_ecdsa",
    "id_ed25519",
    "id_rsa",
    "secrets.json",
}


@dataclass(frozen=True)
class TrackedBlob:
    mode: str
    object_id: str
    path: Path


def tracked_blobs() -> list[TrackedBlob]:
    result = subprocess.run(
        ["git", "ls-files", "--cached", "--stage", "-z"],
        check=True,
        stdout=subprocess.PIPE,
    )
    blobs: list[TrackedBlob] = []
    for raw in result.stdout.split(b"\0"):
        if not raw:
            continue
        metadata, path_bytes = raw.split(b"\t", 1)
        mode, object_id, stage = metadata.decode("ascii").split(" ")
        if stage != "0":
            raise RuntimeError("Git index contains unresolved merge stages")
        blobs.append(TrackedBlob(mode, object_id, Path(path_bytes.decode("utf-8"))))
    return blobs


def read_blob(object_id: str) -> bytes:
    return subprocess.run(
        ["git", "cat-file", "blob", object_id],
        check=True,
        stdout=subprocess.PIPE,
    ).stdout


def check_blob(blob: TrackedBlob) -> list[str]:
    errors: list[str] = []
    path = blob.path
    normalized = path.as_posix()
    normalized_lower = normalized.lower()
    if any(
        normalized_lower == root or normalized_lower.startswith(root + "/")
        for root in BLOCKED_ROOTS
    ):
        errors.append(f"blocked root: {normalized}")
    if path.suffix.lower() in BLOCKED_EXTENSIONS:
        errors.append(f"blocked retail/payload extension: {normalized}")
    if PS2_EXECUTABLE_NAME.fullmatch(path.name):
        errors.append(f"retail executable name: {normalized}")
    name_lower = path.name.lower()
    if name_lower in SENSITIVE_NAMES or name_lower.startswith(".env."):
        errors.append(f"sensitive filename requires explicit policy review: {normalized}")
    if blob.mode not in {"100644", "100755"}:
        errors.append(f"unsupported Git mode {blob.mode}: {normalized}")

    data = read_blob(blob.object_id)
    size = len(data)
    if size > MAX_TRACKED_BYTES:
        errors.append(f"tracked file exceeds {MAX_TRACKED_BYTES} bytes: {normalized} ({size})")

    if b"\0" in data:
        errors.append(f"binary/NUL content requires explicit policy review: {normalized}")
    try:
        data.decode("utf-8")
    except UnicodeDecodeError:
        errors.append(f"non-UTF-8 content requires explicit policy review: {normalized}")
    if data.startswith(b"version https://git-lfs.github.com/spec/v1"):
        errors.append(f"Git LFS pointer requires explicit policy review: {normalized}")
    if data.startswith(b"\x7fELF"):
        errors.append(f"ELF executable content: {normalized}")
    for label, pattern in SECRET_PATTERNS.items():
        if pattern.search(data):
            errors.append(f"{label}: {normalized}")
    return errors


def main() -> int:
    blobs = tracked_blobs()
    errors = [error for blob in blobs for error in check_blob(blob)]
    if errors:
        print(f"public-tree gate: FAILED ({len(errors)} issue(s))")
        for error in errors:
            print(f"- {error}")
        return 1
    print(f"public-tree gate: OK ({len(blobs)} indexed text blobs checked)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
