#!/usr/bin/env python3
"""Scan the bounded POP envelope after the proven TER-to-GOB boundary.

This tool deliberately reports only corpus aggregates over literal, aligned tag
markers that are already documented publicly.  A marker hit is not treated as a
decoded section boundary and no post-TER count or payload meaning is assigned.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import stat
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator


SCHEMA_VERSION = 1
SCOPE = (
    "aggregate aligned literal-marker envelope only; no paths, names, hashes, "
    "payload bytes, executable data, or per-file fingerprints"
)

# These inventory labels already exist in public reconstruction tooling and
# evidence. Their spelling is the only property this scanner assigns to them.
PUBLISHED_LITERAL_TAGS: tuple[bytes, ...] = (
    b"TER:",
    b"GOB:",
    b"SND:",
    b"ACL:",
    b"INL:",
    b"NPC:",
    b"ANPC:",
    b"WPN:",
    b"PLR:",
    b"SKY:",
    b"PNT:",
    b"DIR:",
    b"ENV:",
    b"NOD:",
    b"GEN:",
    b"GRP:",
    b"BOX:",
    b"FIR:",
    b"CAM:",
    b"INV:",
    b"BUG:",
)

_TAG_TEXT = {tag: tag.decode("ascii") for tag in PUBLISHED_LITERAL_TAGS}
_MAXIMUM_TAG_BYTES = max(map(len, PUBLISHED_LITERAL_TAGS))
_SCAN_CHUNK_BYTES = 64 * 1024
_UINT64_MAX = (1 << 64) - 1


@dataclass(frozen=True)
class ScanLimits:
    maximum_files: int = 4096
    maximum_walk_entries: int = 1 << 20
    maximum_file_bytes: int = 64 * 1024 * 1024
    maximum_total_bytes: int = 4 * 1024 * 1024 * 1024
    maximum_terrain_records: int = 1 << 20
    maximum_name_bytes: int = 4096
    maximum_marker_hits_per_file: int = 256


@dataclass(frozen=True)
class PopEnvelope:
    file_bytes: int
    terrain_records: int
    nonzero_alignment_records: int
    gob_offset: int
    markers: tuple[tuple[int, str], ...]


class ScanFailure(ValueError):
    def __init__(self, category: str) -> None:
        super().__init__(category)
        self.category = category


def _validate_limits(limits: ScanLimits) -> None:
    values = (
        limits.maximum_files,
        limits.maximum_walk_entries,
        limits.maximum_file_bytes,
        limits.maximum_total_bytes,
        limits.maximum_terrain_records,
        limits.maximum_name_bytes,
        limits.maximum_marker_hits_per_file,
    )
    if any(value < 0 or value > _UINT64_MAX for value in values):
        raise ScanFailure("limit_exceeded")
    if (
        limits.maximum_files == 0
        or limits.maximum_walk_entries == 0
        or limits.maximum_file_bytes < 16
        or limits.maximum_total_bytes < 16
        or limits.maximum_name_bytes == 0
        or limits.maximum_marker_hits_per_file == 0
    ):
        raise ScanFailure("limit_exceeded")


@dataclass
class _Range:
    minimum: int | None = None
    maximum: int | None = None

    def observe(self, value: int) -> None:
        if value < 0 or value > _UINT64_MAX:
            raise ScanFailure("limit_exceeded")
        self.minimum = value if self.minimum is None else min(self.minimum, value)
        self.maximum = value if self.maximum is None else max(self.maximum, value)

    def as_list(self) -> list[int]:
        if self.minimum is None or self.maximum is None:
            return []
        return [self.minimum, self.maximum]


@dataclass
class _ExtentStats:
    count: int = 0
    extent: _Range | None = None

    def observe(self, value: int) -> None:
        if self.count == _UINT64_MAX:
            raise ScanFailure("limit_exceeded")
        self.count += 1
        if self.extent is None:
            self.extent = _Range()
        self.extent.observe(value)

    def as_dict(self) -> dict[str, object]:
        return {
            "count": self.count,
            "start_delta_bytes_range": self.extent.as_list() if self.extent else [],
        }


def _read_exact(stream: BinaryIO, size: int) -> bytes:
    if size < 0:
        raise ScanFailure("malformed")
    chunks: list[bytes] = []
    remaining = size
    while remaining:
        chunk = stream.read(remaining)
        if not chunk:
            raise ScanFailure("truncated")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def _align_up_4(value: int) -> int:
    if value < 0 or value > _UINT64_MAX - 3:
        raise ScanFailure("limit_exceeded")
    return (value + 3) & ~3


def _is_link_like(path: Path) -> bool:
    """Reject links and Windows junctions before they can escape the scan root."""
    return path.is_symlink() or path.is_junction()


def _read_terrain_prefix(
    stream: BinaryIO, file_bytes: int, limits: ScanLimits
) -> tuple[int, int, int]:
    if file_bytes < 16:
        raise ScanFailure("truncated")
    if file_bytes > limits.maximum_file_bytes:
        raise ScanFailure("limit_exceeded")

    stream.seek(0)
    header = _read_exact(stream, 12)
    observed_word, tag, terrain_records = struct.unpack("<I4sI", header)
    if observed_word != 70 or tag != b"TER:":
        raise ScanFailure("malformed")
    if terrain_records > limits.maximum_terrain_records:
        raise ScanFailure("limit_exceeded")

    # Every terrain record needs eight fixed bytes, at least one name byte, a
    # terminator, and the file must still contain the four-byte GOB marker.
    minimum_bytes = 16 + terrain_records * 10
    if minimum_bytes > file_bytes:
        raise ScanFailure("truncated")

    nonzero_alignment_records = 0
    for _ in range(terrain_records):
        _read_exact(stream, 8)
        name_bytes = 0
        while True:
            value = _read_exact(stream, 1)[0]
            if value == 0:
                if name_bytes == 0:
                    raise ScanFailure("malformed")
                break
            if name_bytes >= limits.maximum_name_bytes:
                raise ScanFailure("limit_exceeded")
            if value < 0x20 or value > 0x7E:
                raise ScanFailure("malformed")
            name_bytes += 1

        aligned = _align_up_4(stream.tell())
        if aligned > file_bytes:
            raise ScanFailure("truncated")
        alignment = _read_exact(stream, aligned - stream.tell())
        nonzero_alignment_records += int(any(alignment))

    gob_offset = stream.tell()
    if gob_offset % 4 != 0:
        raise ScanFailure("malformed")
    if _read_exact(stream, 4) != b"GOB:":
        raise ScanFailure("malformed")
    return terrain_records, nonzero_alignment_records, gob_offset


def _scan_aligned_published_markers(
    stream: BinaryIO,
    start_offset: int,
    file_bytes: int,
    maximum_hits: int,
) -> list[tuple[int, str]]:
    if start_offset < 0 or start_offset > file_bytes:
        raise ScanFailure("malformed")

    hits: list[tuple[int, str]] = []
    cursor = _align_up_4(start_offset)
    while cursor < file_bytes:
        block_end = min(file_bytes, cursor + _SCAN_CHUNK_BYTES)
        lookahead = min(_MAXIMUM_TAG_BYTES - 1, file_bytes - block_end)
        stream.seek(cursor)
        block = _read_exact(stream, block_end - cursor + lookahead)

        for absolute in range(cursor, block_end, 4):
            relative = absolute - cursor
            for tag in PUBLISHED_LITERAL_TAGS:
                if block[relative : relative + len(tag)] != tag:
                    continue
                if len(hits) >= maximum_hits:
                    raise ScanFailure("limit_exceeded")
                hits.append((absolute, _TAG_TEXT[tag]))
                break
        cursor = block_end

    return hits


def scan_pop_stream(
    stream: BinaryIO, file_bytes: int, limits: ScanLimits = ScanLimits()
) -> PopEnvelope:
    _validate_limits(limits)
    if file_bytes < 0:
        raise ScanFailure("malformed")
    terrain_records, nonzero_alignment_records, gob_offset = _read_terrain_prefix(
        stream, file_bytes, limits
    )
    markers = [(gob_offset, "GOB:")]
    markers.extend(
        _scan_aligned_published_markers(
            stream, gob_offset + 4, file_bytes, limits.maximum_marker_hits_per_file - 1
        )
    )
    return PopEnvelope(
        file_bytes=file_bytes,
        terrain_records=terrain_records,
        nonzero_alignment_records=nonzero_alignment_records,
        gob_offset=gob_offset,
        markers=tuple(markers),
    )


def _iter_pop_paths(root: Path, maximum_walk_entries: int) -> Iterator[Path]:
    def raise_walk_error(error: OSError) -> None:
        raise error

    walk_entries = 0
    for directory, subdirectories, filenames in os.walk(
        root, topdown=True, onerror=raise_walk_error, followlinks=False
    ):
        entries_here = len(subdirectories) + len(filenames)
        if walk_entries > maximum_walk_entries - entries_here:
            raise ScanFailure("limit_exceeded")
        walk_entries += entries_here
        directory_path = Path(directory)
        ordered_subdirectories = sorted(subdirectories)
        if any(_is_link_like(directory_path / name) for name in ordered_subdirectories):
            raise ScanFailure("unsafe_input")
        subdirectories[:] = ordered_subdirectories
        for filename in sorted(filenames):
            if Path(filename).suffix.casefold() == ".pop":
                path = directory_path / filename
                if _is_link_like(path):
                    raise ScanFailure("unsafe_input")
                yield path


class _Aggregate:
    def __init__(self) -> None:
        self.files = 0
        self.terrain_records_total = 0
        self.nonzero_alignment_records_total = 0
        self.literal_marker_hits_total = 0
        self.file_bytes = _Range()
        self.terrain_records = _Range()
        self.gob_offset = _Range()
        self.post_terrain_bytes = _Range()
        self.marker_hits = _Range()
        self.marker_occurrences: collections.Counter[str] = collections.Counter()
        self.ordinal_occurrences: list[collections.Counter[str]] = []
        self.transitions: dict[str, _ExtentStats] = {}
        self.ordered_sequences: set[tuple[str, ...]] = set()

    def add(self, envelope: PopEnvelope) -> None:
        if self.files == _UINT64_MAX:
            raise ScanFailure("limit_exceeded")
        self.files += 1
        self.terrain_records_total += envelope.terrain_records
        self.nonzero_alignment_records_total += envelope.nonzero_alignment_records
        self.literal_marker_hits_total += len(envelope.markers)
        for value in (
            self.terrain_records_total,
            self.nonzero_alignment_records_total,
            self.literal_marker_hits_total,
        ):
            if value > _UINT64_MAX:
                raise ScanFailure("limit_exceeded")

        self.file_bytes.observe(envelope.file_bytes)
        self.terrain_records.observe(envelope.terrain_records)
        self.gob_offset.observe(envelope.gob_offset)
        self.post_terrain_bytes.observe(envelope.file_bytes - envelope.gob_offset)
        self.marker_hits.observe(len(envelope.markers))

        sequence = tuple(tag for _, tag in envelope.markers)
        self.ordered_sequences.add(sequence)
        for ordinal, (_, tag) in enumerate(envelope.markers):
            self.marker_occurrences[tag] += 1
            while len(self.ordinal_occurrences) <= ordinal:
                self.ordinal_occurrences.append(collections.Counter())
            self.ordinal_occurrences[ordinal][tag] += 1

        for (left_offset, left_tag), (right_offset, right_tag) in zip(
            envelope.markers, envelope.markers[1:]
        ):
            key = f"{left_tag}>{right_tag}"
            self.transitions.setdefault(key, _ExtentStats()).observe(
                right_offset - left_offset
            )
        final_offset, final_tag = envelope.markers[-1]
        self.transitions.setdefault(f"{final_tag}>EOF", _ExtentStats()).observe(
            envelope.file_bytes - final_offset
        )

    def as_dict(self) -> dict[str, object]:
        ordinals = [
            {
                "ordinal": ordinal,
                "literal_tag_counts": dict(sorted(counter.items())),
            }
            for ordinal, counter in enumerate(self.ordinal_occurrences)
        ]
        return {
            "files": self.files,
            "file_bytes_range": self.file_bytes.as_list(),
            "terrain_records_total": self.terrain_records_total,
            "terrain_records_per_file_range": self.terrain_records.as_list(),
            "terrain_records_with_nonzero_alignment_bytes": (
                self.nonzero_alignment_records_total
            ),
            "gob_boundary_offset_range": self.gob_offset.as_list(),
            "post_terrain_envelope_bytes_range": self.post_terrain_bytes.as_list(),
            "literal_marker_hits_total": self.literal_marker_hits_total,
            "literal_marker_hits_per_file_range": self.marker_hits.as_list(),
            "literal_marker_occurrences": dict(sorted(self.marker_occurrences.items())),
            "distinct_ordered_literal_sequences": len(self.ordered_sequences),
            "ordered_literal_marker_ordinals": ordinals,
            "candidate_marker_transitions": {
                key: value.as_dict() for key, value in sorted(self.transitions.items())
            },
        }


def _failure_result(
    files_discovered: int, files_valid: int, errors: collections.Counter[str]
) -> dict[str, object]:
    return {
        "schema_version": SCHEMA_VERSION,
        "scope": SCOPE,
        "complete": False,
        "files_discovered": files_discovered,
        "files_valid": files_valid,
        "errors": dict(sorted(errors.items())),
    }


def scan_root(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    try:
        _validate_limits(limits)
    except ScanFailure as exc:
        return _failure_result(0, 0, collections.Counter({exc.category: 1}))
    try:
        root_is_safe = root.is_dir() and not _is_link_like(root)
    except OSError:
        return _failure_result(0, 0, collections.Counter({"io_error": 1}))
    if not root_is_safe:
        return _failure_result(0, 0, collections.Counter({"unsafe_input": 1}))

    aggregate = _Aggregate()
    errors: collections.Counter[str] = collections.Counter()
    files_discovered = 0
    total_bytes = 0
    try:
        candidates = _iter_pop_paths(root, limits.maximum_walk_entries)
        for path in candidates:
            files_discovered += 1
            if files_discovered > limits.maximum_files:
                errors["limit_exceeded"] += 1
                break
            try:
                path_stat = os.stat(path, follow_symlinks=False)
                if not stat.S_ISREG(path_stat.st_mode):
                    raise ScanFailure("unsafe_input")
                with path.open("rb") as stream:
                    file_stat = os.fstat(stream.fileno())
                    if (
                        not stat.S_ISREG(file_stat.st_mode)
                        or (file_stat.st_dev, file_stat.st_ino)
                        != (path_stat.st_dev, path_stat.st_ino)
                    ):
                        raise ScanFailure("unsafe_input")
                    file_bytes = file_stat.st_size
                    if file_bytes < 0 or total_bytes > limits.maximum_total_bytes - file_bytes:
                        raise ScanFailure("limit_exceeded")
                    total_bytes += file_bytes
                    envelope = scan_pop_stream(stream, file_bytes, limits)
                    final_stat = os.fstat(stream.fileno())
                    if (final_stat.st_size, final_stat.st_mtime_ns) != (
                        file_bytes,
                        file_stat.st_mtime_ns,
                    ):
                        raise ScanFailure("unsafe_input")
                aggregate.add(envelope)
            except ScanFailure as exc:
                errors[exc.category] += 1
            except OSError:
                errors["io_error"] += 1
    except ScanFailure as exc:
        errors[exc.category] += 1
    except OSError:
        errors["io_error"] += 1

    if files_discovered == 0 and not errors:
        errors["no_candidates"] += 1
    if errors:
        return _failure_result(files_discovered, aggregate.files, errors)
    return {
        "schema_version": SCHEMA_VERSION,
        "scope": SCOPE,
        "complete": True,
        "errors": {},
        **aggregate.as_dict(),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disc_root", type=Path)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()

    result = scan_root(args.disc_root.absolute())
    if args.pretty:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["complete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
