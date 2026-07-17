#!/usr/bin/env python3
"""Inspect and safely extract Omega Strain HOG archives."""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path, PurePosixPath


@dataclass(frozen=True)
class HogEntry:
    name: str
    offset: int
    size: int


@dataclass(frozen=True)
class HogArchive:
    tag: int
    count: int
    offsets_offset: int
    names_offset: int
    data_offset: int
    entries: tuple[HogEntry, ...]


def parse_hog(path: Path) -> tuple[HogArchive, bytes]:
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError("file is too small for a HOG header")

    tag, count, offsets_offset, names_offset, data_offset = struct.unpack_from("<5I", data)
    expected_names_offset = offsets_offset + 4 * (count + 1)
    if offsets_offset != 0x14:
        raise ValueError(f"unexpected offsets offset: 0x{offsets_offset:X}")
    if names_offset != expected_names_offset:
        raise ValueError(f"name table mismatch: 0x{names_offset:X} != 0x{expected_names_offset:X}")
    if not (names_offset <= data_offset <= len(data)):
        raise ValueError("invalid name/data table boundaries")

    offsets = struct.unpack_from(f"<{count + 1}I", data, offsets_offset)
    if offsets[0] != 0 or any(left > right for left, right in zip(offsets, offsets[1:])):
        raise ValueError("payload offsets are not monotonic from zero")
    if data_offset + offsets[-1] != len(data):
        raise ValueError(
            f"payload boundary mismatch: 0x{data_offset + offsets[-1]:X} != 0x{len(data):X}"
        )

    names_blob = data[names_offset:data_offset]
    raw_names = names_blob.rstrip(b"\0").split(b"\0") if names_blob.rstrip(b"\0") else []
    if len(raw_names) != count:
        raise ValueError(f"expected {count} names, found {len(raw_names)}")

    entries = []
    for index, raw_name in enumerate(raw_names):
        name = raw_name.decode("ascii")
        entries.append(HogEntry(name=name, offset=data_offset + offsets[index], size=offsets[index + 1] - offsets[index]))

    return HogArchive(
        tag=tag,
        count=count,
        offsets_offset=offsets_offset,
        names_offset=names_offset,
        data_offset=data_offset,
        entries=tuple(entries),
    ), data


def safe_destination(root: Path, name: str) -> Path:
    normalized = PurePosixPath(name.replace("\\", "/"))
    if normalized.is_absolute() or any(part in ("", ".", "..") for part in normalized.parts):
        raise ValueError(f"unsafe archive path: {name!r}")
    destination = root.joinpath(*normalized.parts).resolve()
    resolved_root = root.resolve()
    if destination != resolved_root and resolved_root not in destination.parents:
        raise ValueError(f"archive path escapes output directory: {name!r}")
    return destination


def archive_record(path: Path, archive: HogArchive) -> dict[str, object]:
    return {
        "path": str(path),
        "tag": f"0x{archive.tag:08X}",
        "count": archive.count,
        "offsets_offset": archive.offsets_offset,
        "names_offset": archive.names_offset,
        "data_offset": archive.data_offset,
        "entries": [
            {"name": entry.name, "offset": entry.offset, "size": entry.size}
            for entry in archive.entries
        ],
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("archive", type=Path)
    parser.add_argument("--json", type=Path)
    parser.add_argument("--extract", type=Path)
    args = parser.parse_args()

    archive, data = parse_hog(args.archive)
    record = archive_record(args.archive, archive)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(record, indent=2, sort_keys=True) + "\n", encoding="utf-8")

    if args.extract:
        args.extract.mkdir(parents=True, exist_ok=True)
        for entry in archive.entries:
            destination = safe_destination(args.extract, entry.name)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data[entry.offset:entry.offset + entry.size])

    print(json.dumps({key: value for key, value in record.items() if key != "entries"}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

