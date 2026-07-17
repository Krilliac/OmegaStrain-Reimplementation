#!/usr/bin/env python3
"""Validate the inferred HOG layout across an extracted disc."""

from __future__ import annotations

import argparse
import json
from collections import Counter
from pathlib import Path

from hog import parse_hog


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    archives = sorted(args.root.rglob("*.HOG"), key=lambda path: path.as_posix().lower())
    valid = []
    errors = []
    entry_extensions: Counter[str] = Counter()
    total_entries = 0

    for path in archives:
        relative = path.relative_to(args.root).as_posix()
        try:
            archive, _ = parse_hog(path)
            total_entries += archive.count
            for entry in archive.entries:
                entry_extensions[Path(entry.name).suffix.lower() or "<none>"] += 1
            valid.append({
                "path": relative,
                "tag": f"0x{archive.tag:08X}",
                "entry_count": archive.count,
                "data_offset": archive.data_offset,
            })
        except (OSError, UnicodeDecodeError, ValueError) as error:
            errors.append({"path": relative, "error": str(error)})

    result = {
        "archive_count": len(archives),
        "valid_count": len(valid),
        "error_count": len(errors),
        "total_entries": total_entries,
        "entry_extensions": dict(sorted(entry_extensions.items())),
        "archives": valid,
        "errors": errors,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in result.items() if key not in ("archives", "errors")}, sort_keys=True))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())

