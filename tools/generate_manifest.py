#!/usr/bin/env python3
"""Generate a deterministic JSONL file manifest and compact summary."""

from __future__ import annotations

import argparse
import hashlib
import json
from collections import Counter
from pathlib import Path


def hash_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def manifest_sort_key(relative_posix: str) -> tuple[str, str]:
    """Return a total-order sort key for a repository-relative POSIX path.

    Ordering is case-insensitive first (stable, human-friendly grouping), then
    breaks ties by the exact original path. Using the case-folded value alone is
    not a total order: two members differing only in case collide, after which a
    stable sort preserves filesystem-walk order, which varies by OS. The second
    component makes the order fully determined by the tree contents, so the same
    tree yields byte-identical manifests on every machine.
    """
    return (relative_posix.lower(), relative_posix)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    manifest = args.manifest.resolve()
    summary_path = (args.summary or manifest.with_suffix(".summary.json")).resolve()
    if not root.is_dir():
        parser.error(f"root is not a directory: {root}")

    files = sorted(
        (path for path in root.rglob("*") if path.is_file()),
        key=lambda path: manifest_sort_key(path.relative_to(root).as_posix()),
    )
    manifest.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    extensions: Counter[str] = Counter()
    top_level: Counter[str] = Counter()
    total_bytes = 0

    with manifest.open("w", encoding="utf-8", newline="\n") as output:
        for path in files:
            relative = path.relative_to(root).as_posix()
            size = path.stat().st_size
            extension = path.suffix.lower() or "<none>"
            top = relative.split("/", 1)[0]
            record = {
                "path": relative,
                "size": size,
                "sha256": hash_file(path),
            }
            output.write(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")
            extensions[extension] += 1
            top_level[top] += size
            total_bytes += size

    summary = {
        "root_label": root.name,
        "file_count": len(files),
        "total_bytes": total_bytes,
        "extensions": dict(sorted(extensions.items())),
        "top_level_bytes": dict(sorted(top_level.items())),
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"manifest": str(manifest), "summary": str(summary_path), **summary}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

