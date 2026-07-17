#!/usr/bin/env python3
"""Fingerprint all HOG headers to identify container families before decoding."""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from pathlib import Path


def printable(data: bytes) -> str:
    return "".join(chr(value) if 32 <= value < 127 else "." for value in data)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--summary", type=Path)
    args = parser.parse_args()

    root = args.root.resolve()
    output = args.output.resolve()
    summary_path = (args.summary or output.with_suffix(".summary.json")).resolve()
    hogs = sorted(root.rglob("*.HOG"), key=lambda path: path.relative_to(root).as_posix().lower())
    output.parent.mkdir(parents=True, exist_ok=True)
    summary_path.parent.mkdir(parents=True, exist_ok=True)

    first_words: Counter[str] = Counter()
    sizes = []
    with output.open("w", encoding="utf-8", newline="\n") as stream:
        for path in hogs:
            size = path.stat().st_size
            with path.open("rb") as source:
                header = source.read(64)
            padded = header.ljust(64, b"\0")
            words = struct.unpack("<16I", padded)
            first_word = f"0x{words[0]:08X}"
            first_words[first_word] += 1
            sizes.append(size)
            record = {
                "path": path.relative_to(root).as_posix(),
                "size": size,
                "header_hex": header.hex().upper(),
                "header_ascii": printable(header),
                "le_u32": [f"0x{word:08X}" for word in words[:8]],
            }
            stream.write(json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n")

    summary = {
        "archive_count": len(hogs),
        "first_word_families": dict(first_words.most_common()),
        "minimum_size": min(sizes, default=0),
        "maximum_size": max(sizes, default=0),
        "total_bytes": sum(sizes),
    }
    summary_path.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "summary": str(summary_path), **summary}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

