#!/usr/bin/env python3
"""Prove the observed shortened-TDX zero suffixes without publishing asset identity or data."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterator

if __package__:
    from .fingerprint_assets import (
        HogDirectory,
        Span,
        parse_hog_span,
        range_is_zero,
        read_at,
    )
else:  # Direct script execution adds tools/ rather than the repository root.
    from fingerprint_assets import HogDirectory, Span, parse_hog_span, range_is_zero, read_at


MAXIMUM_ASSET_BYTES = 64 * 1024 * 1024
MAXIMUM_CANDIDATES = 1 << 20
MAXIMUM_NESTED_DEPTH = 32

EXPECTED_PROOF: dict[str, object] = {
    "shortened_occurrences": 62,
    "shortened_unique_prefixes": 16,
    "missing_bytes": {"16": 3, "32": 45, "64": 5, "256": 9},
    "complete_candidates_at_matching_declared_sizes": 10_679,
    "zero_suffix_match_unique_prefixes": 16,
    "zero_suffix_match_occurrences": 62,
    "nonzero_suffix_match_unique_prefixes": 0,
    "nonzero_suffix_match_occurrences": 0,
    "unmatched_occurrences": 0,
}


@dataclass(frozen=True)
class LocatedSpan:
    path: Path
    span: Span


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def walk_hog(
    file: BinaryIO, directory: HogDirectory, depth: int = 0
) -> Iterator[Span]:
    for entry in directory.entries:
        suffix = Path(entry.name).suffix.lower()
        if suffix == ".tdx":
            yield entry
        if suffix != ".hog" or depth >= MAXIMUM_NESTED_DEPTH:
            continue
        try:
            nested = parse_hog_span(file, entry.offset, entry.size)
        except ValueError:
            continue
        yield from walk_hog(file, nested, depth + 1)


def iter_spans(root: Path) -> Iterator[LocatedSpan]:
    candidates = 0
    for path in sorted(root.rglob("*.HOG")):
        if path.is_symlink() or not path.is_file():
            continue
        with path.open("rb") as file:
            try:
                directory = parse_hog_span(file, 0, path.stat().st_size)
            except ValueError:
                continue
            for span in walk_hog(file, directory):
                candidates += 1
                if candidates > MAXIMUM_CANDIDATES:
                    raise ValueError("TDX candidate count exceeds safety limit")
                if span.size > MAXIMUM_ASSET_BYTES:
                    raise ValueError("TDX span exceeds safety limit")
                yield LocatedSpan(path, span)
    for path in sorted(root.rglob("*.TDX")):
        if path.is_symlink() or not path.is_file():
            continue
        candidates += 1
        if candidates > MAXIMUM_CANDIDATES:
            raise ValueError("TDX candidate count exceeds safety limit")
        size = path.stat().st_size
        if size > MAXIMUM_ASSET_BYTES:
            raise ValueError("TDX span exceeds safety limit")
        yield LocatedSpan(path, Span(path.name, 0, size))


def declared_size(header: bytes) -> int:
    return 64 + u16(header, 0x22) * u32(header, 0x38)


def digest(data: bytes) -> bytes:
    return hashlib.sha256(data).digest()


def matches_expected_baseline(result: dict[str, object]) -> bool:
    return all(result.get(key) == value for key, value in EXPECTED_PROOF.items())


def prove(root: Path) -> dict[str, object]:
    shortened: collections.Counter[tuple[int, int, bytes]] = collections.Counter()
    lengths_by_expected: dict[int, set[int]] = collections.defaultdict(set)
    missing_bytes: collections.Counter[int] = collections.Counter()
    for located in iter_spans(root):
        with located.path.open("rb") as file:
            if located.span.size < 64:
                continue
            header = read_at(file, located.span.offset, 64)
            expected = declared_size(header)
            if expected <= located.span.size:
                continue
            content = read_at(file, located.span.offset, located.span.size)
            shortened[(expected, located.span.size, digest(content))] += 1
            lengths_by_expected[expected].add(located.span.size)
            missing_bytes[expected - located.span.size] += 1

    zero_matches: set[tuple[int, int, bytes]] = set()
    nonzero_matches: set[tuple[int, int, bytes]] = set()
    complete_candidates = 0
    for located in iter_spans(root):
        with located.path.open("rb") as file:
            if located.span.size < 64:
                continue
            header = read_at(file, located.span.offset, 64)
            expected = declared_size(header)
            prefix_sizes = lengths_by_expected.get(expected)
            if not prefix_sizes or located.span.size < expected:
                continue
            complete_candidates += 1
            for prefix_size in prefix_sizes:
                key = (expected, prefix_size,
                    digest(read_at(file, located.span.offset, prefix_size)))
                if key not in shortened:
                    continue
                if range_is_zero(
                    file, located.span.offset + prefix_size, expected - prefix_size
                ):
                    zero_matches.add(key)
                else:
                    nonzero_matches.add(key)

    shortened_occurrences = sum(shortened.values())
    zero_covered = sum(shortened[key] for key in zero_matches)
    nonzero_covered = sum(shortened[key] for key in nonzero_matches)
    return {
        "schema_version": 1,
        "scope": "aggregate duplicate-prefix proof only; no paths, names, hashes, or payload bytes",
        "shortened_occurrences": shortened_occurrences,
        "shortened_unique_prefixes": len(shortened),
        "missing_bytes": {
            str(size): count for size, count in sorted(missing_bytes.items())
        },
        "complete_candidates_at_matching_declared_sizes": complete_candidates,
        "zero_suffix_match_unique_prefixes": len(zero_matches),
        "zero_suffix_match_occurrences": zero_covered,
        "nonzero_suffix_match_unique_prefixes": len(nonzero_matches),
        "nonzero_suffix_match_occurrences": nonzero_covered,
        "unmatched_occurrences": shortened_occurrences - zero_covered,
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("disc_root", type=Path)
    args = parser.parse_args()
    try:
        disc_root = args.disc_root.resolve()
        if not disc_root.is_dir():
            raise ValueError("disc root is not a directory")
        result = prove(disc_root)
    except (OSError, ValueError):
        print(json.dumps({"error": "unable to scan corpus safely"}, sort_keys=True))
        return 2
    result["baseline_match"] = matches_expected_baseline(result)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0 if result["baseline_match"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
