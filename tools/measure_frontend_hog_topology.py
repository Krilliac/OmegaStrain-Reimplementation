#!/usr/bin/env python3
"""Measure recursive front-end HOG topology without exporting identity.

The report is deliberately fixed-vocabulary and aggregate-only. It never emits
input paths, archive/member names, hashes, offsets, payload bytes, raw suffixes,
per-archive rows, or proprietary identifiers. Members outside the frozen public
extension vocabulary collapse into the single ``other`` category.
"""

from __future__ import annotations

import argparse
import itertools
import json
import os
import stat
import struct
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass, field, fields
from pathlib import Path
from typing import BinaryIO, Iterator, Sequence

if __package__:
    from .fingerprint_assets import HogDirectory, Span, parse_hog_span, read_at
else:  # Direct execution adds tools/ rather than the repository root.
    from fingerprint_assets import HogDirectory, Span, parse_hog_span, read_at


APPROVED_EXTENSION_CATEGORIES = {
    ".col": "collision",
    ".gui": "gui",
    ".hog": "container",
    ".ie": "ie",
    ".pop": "scene",
    ".ska": "animation",
    ".skas": "animation",
    ".skl": "skeleton",
    ".skm": "mesh",
    ".so": "script",
    ".tbl": "table",
    ".tdx": "texture",
    ".txt": "text",
    ".vag": "audio",
    ".vum": "material",
}
APPROVED_EXTENSIONS = tuple(sorted(APPROVED_EXTENSION_CATEGORIES))
APPROVED_CATEGORIES = tuple(
    sorted(set(APPROVED_EXTENSION_CATEGORIES.values()))
)
CATEGORIES = (*APPROVED_CATEGORIES, "other")
PAIR_KEYS = tuple(
    f"{left}+{right}"
    for left, right in itertools.combinations(APPROVED_EXTENSIONS, 2)
)

EXTENT_BUCKETS = (
    "zero",
    "1_255",
    "256_4095",
    "4096_65535",
    "65536_1048575",
    "1048576_16777215",
    "16777216_plus",
)
ARCHIVE_EXTENT_FAMILIES = (
    "root_exact",
    "nested_exact",
    "nested_zero_padded",
)
MAXIMUM_REPORTED_ARCHIVE_DEPTH = 16
MAXIMUM_FILESYSTEM_DEPTH = 64
NAME_TABLE_SCAN_CHUNK_BYTES = 64 * 1024

ERROR_CATEGORIES = (
    "config",
    "unsafe_input",
    "filesystem_limit",
    "missing_input",
    "root_archive_limit",
    "archive_size_limit",
    "archive_limit",
    "depth_limit",
    "root_hog_malformed",
    "nested_hog_malformed",
    "archive_name_invalid",
    "normalized_collision",
    "io",
)

TOTAL_FIELDS = (
    "root_archives_discovered",
    "root_archives_scanned",
    "archive_occurrences",
    "nested_archive_occurrences",
    "member_occurrences",
    "approved_member_occurrences",
    "other_member_occurrences",
    "basename_groups",
    "basename_pairs",
    "errors",
)


@dataclass(frozen=True)
class ScanLimits:
    maximum_root_archives: int = 512
    maximum_filesystem_entries: int = 100_000
    maximum_filesystem_depth: int = 32
    maximum_root_archive_bytes: int = 512 * 1024 * 1024
    maximum_nested_archive_bytes: int = 256 * 1024 * 1024
    maximum_archive_directory_bytes: int = 64 * 1024 * 1024
    maximum_archive_entries_per_directory: int = 262_144
    maximum_archive_occurrences: int = 131_072
    maximum_hog_entries: int = 2_000_000
    maximum_name_table_bytes: int = 128 * 1024 * 1024
    maximum_name_bytes: int = 4096
    maximum_traversed_span_bytes: int = 16 * 1024 * 1024 * 1024
    maximum_nesting_depth: int = 16


HARD_LIMITS = {
    "maximum_root_archives": 4096,
    "maximum_filesystem_entries": 1_000_000,
    "maximum_filesystem_depth": MAXIMUM_FILESYSTEM_DEPTH,
    "maximum_root_archive_bytes": 1024 * 1024 * 1024,
    "maximum_nested_archive_bytes": 512 * 1024 * 1024,
    "maximum_archive_directory_bytes": 128 * 1024 * 1024,
    "maximum_archive_entries_per_directory": 1_000_000,
    "maximum_archive_occurrences": 1_000_000,
    "maximum_hog_entries": 8_000_000,
    "maximum_name_table_bytes": 512 * 1024 * 1024,
    "maximum_name_bytes": 4096,
    "maximum_traversed_span_bytes": 64 * 1024 * 1024 * 1024,
    "maximum_nesting_depth": MAXIMUM_REPORTED_ARCHIVE_DEPTH,
}


class ScanFailure(ValueError):
    def __init__(self, category: str) -> None:
        if category not in ERROR_CATEGORIES:
            raise ValueError("unknown aggregate error category")
        super().__init__(category)
        self.category = category


class AggregateArgumentError(Exception):
    pass


class AggregateArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise AggregateArgumentError from None


@dataclass
class ScanBudget:
    limits: ScanLimits
    filesystem_entries: int = 0
    archive_occurrences: int = 0
    hog_entries: int = 0
    name_table_bytes: int = 0
    traversed_span_bytes: int = 0

    def _add(self, field_name: str, count: int, maximum: int, category: str) -> None:
        current = getattr(self, field_name)
        if count < 0 or count > maximum - current:
            raise ScanFailure(category)
        setattr(self, field_name, current + count)

    def add_filesystem_entries(self, count: int) -> None:
        self._add(
            "filesystem_entries",
            count,
            self.limits.maximum_filesystem_entries,
            "filesystem_limit",
        )

    def add_archive(self) -> None:
        self._add(
            "archive_occurrences",
            1,
            self.limits.maximum_archive_occurrences,
            "archive_limit",
        )

    def add_hog_entries(self, count: int) -> None:
        self._add(
            "hog_entries", count, self.limits.maximum_hog_entries, "archive_limit"
        )

    def add_name_table_bytes(self, count: int) -> None:
        self._add(
            "name_table_bytes",
            count,
            self.limits.maximum_name_table_bytes,
            "archive_limit",
        )

    def add_traversed_span(self, count: int) -> None:
        self._add(
            "traversed_span_bytes",
            count,
            self.limits.maximum_traversed_span_bytes,
            "archive_limit",
        )


@dataclass
class Aggregate:
    totals: Counter[str] = field(default_factory=Counter)
    extension_counts: Counter[str] = field(default_factory=Counter)
    category_counts: Counter[str] = field(default_factory=Counter)
    depth_counts: Counter[int] = field(default_factory=Counter)
    archive_extent_families: Counter[str] = field(default_factory=Counter)
    extent_buckets: Counter[str] = field(default_factory=Counter)
    category_extent_buckets: dict[str, Counter[str]] = field(
        default_factory=lambda: {category: Counter() for category in CATEGORIES}
    )
    basename_pair_totals: Counter[str] = field(default_factory=Counter)

    def document(self) -> dict[str, object]:
        totals = {key: int(self.totals[key]) for key in TOTAL_FIELDS}
        return {
            "schema_version": 3,
            "scope": (
                "fixed-vocabulary aggregate recursive HOG containment only; no paths, "
                "member names, hashes, offsets, payload bytes, raw suffixes, "
                "identifiers, per-archive rows, asset roles, bindings, layout, "
                "behavior, or semantics"
            ),
            "totals": totals,
            "approved_extension_counts": {
                extension: int(self.extension_counts[extension])
                for extension in APPROVED_EXTENSIONS
            },
            "approved_category_counts": {
                category: int(self.category_counts[category])
                for category in APPROVED_CATEGORIES
            },
            "other_category_count": int(self.category_counts["other"]),
            "archive_depth_distribution": {
                str(depth): int(self.depth_counts[depth])
                for depth in range(MAXIMUM_REPORTED_ARCHIVE_DEPTH + 1)
            },
            "archive_extent_families": {
                family: int(self.archive_extent_families[family])
                for family in ARCHIVE_EXTENT_FAMILIES
            },
            "member_extent_buckets": {
                bucket: int(self.extent_buckets[bucket])
                for bucket in EXTENT_BUCKETS
            },
            "category_extent_buckets": {
                category: {
                    bucket: int(self.category_extent_buckets[category][bucket])
                    for bucket in EXTENT_BUCKETS
                }
                for category in CATEGORIES
            },
            "basename_pair_totals": {
                pair: int(self.basename_pair_totals[pair]) for pair in PAIR_KEYS
            },
            "error_categories": {category: 0 for category in ERROR_CATEGORIES},
        }


def _validate_limits(limits: ScanLimits) -> None:
    for item in fields(limits):
        value = getattr(limits, item.name)
        if not isinstance(value, int) or isinstance(value, bool):
            raise ScanFailure("config")
        minimum = 0 if item.name in {
            "maximum_filesystem_depth",
            "maximum_nesting_depth",
        } else 1
        if value < minimum or value > HARD_LIMITS[item.name]:
            raise ScanFailure("config")


def _stat_is_reparse(info: os.stat_result) -> bool:
    flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(flag and getattr(info, "st_file_attributes", 0) & flag)


def _is_link_like(path: Path, info: os.stat_result) -> bool:
    return (
        path.is_symlink()
        or getattr(path, "is_junction", lambda: False)()
        or _stat_is_reparse(info)
    )


@contextmanager
def _open_regular(path: Path) -> Iterator[tuple[BinaryIO, int]]:
    try:
        before = os.stat(path, follow_symlinks=False)
        if _is_link_like(path, before) or not stat.S_ISREG(before.st_mode):
            raise ScanFailure("unsafe_input")
        stream = path.open("rb")
    except ScanFailure:
        raise
    except FileNotFoundError as exc:
        raise ScanFailure("missing_input") from exc
    except OSError as exc:
        raise ScanFailure("io") from exc

    try:
        opened = os.fstat(stream.fileno())
        if (
            not stat.S_ISREG(opened.st_mode)
            or _stat_is_reparse(opened)
            or (opened.st_dev, opened.st_ino) != (before.st_dev, before.st_ino)
        ):
            raise ScanFailure("unsafe_input")
        yield stream, opened.st_size
        after = os.fstat(stream.fileno())
        if (
            after.st_dev,
            after.st_ino,
            after.st_size,
            after.st_mtime_ns,
        ) != (
            opened.st_dev,
            opened.st_ino,
            opened.st_size,
            opened.st_mtime_ns,
        ):
            raise ScanFailure("unsafe_input")
    finally:
        stream.close()


def _directory_entries(path: Path, budget: ScanBudget) -> list[os.DirEntry[str]]:
    try:
        with os.scandir(path) as iterator:
            entries: list[os.DirEntry[str]] = []
            for entry in iterator:
                budget.add_filesystem_entries(1)
                entries.append(entry)
    except ScanFailure:
        raise
    except OSError as exc:
        raise ScanFailure("io") from exc
    entries.sort(key=lambda entry: (entry.name.upper(), entry.name))
    return entries


def _discover_root_archives(root: Path, budget: ScanBudget) -> tuple[Path, ...]:
    try:
        root_info = os.stat(root, follow_symlinks=False)
    except FileNotFoundError as exc:
        raise ScanFailure("missing_input") from exc
    except OSError as exc:
        raise ScanFailure("io") from exc
    if _is_link_like(root, root_info):
        raise ScanFailure("unsafe_input")
    if stat.S_ISREG(root_info.st_mode):
        return (root.absolute(),)
    if not stat.S_ISDIR(root_info.st_mode):
        raise ScanFailure("unsafe_input")

    discovered: list[Path] = []
    stack: list[tuple[Path, int]] = [(root.absolute(), 0)]
    while stack:
        directory, depth = stack.pop()
        entries = _directory_entries(directory, budget)
        folded_names: set[str] = set()
        child_directories: list[Path] = []
        for entry in entries:
            folded = entry.name.casefold()
            if folded in folded_names:
                raise ScanFailure("unsafe_input")
            folded_names.add(folded)
            try:
                info = entry.stat(follow_symlinks=False)
            except OSError as exc:
                raise ScanFailure("io") from exc
            entry_path = Path(entry.path)
            if _is_link_like(entry_path, info):
                raise ScanFailure("unsafe_input")
            if stat.S_ISDIR(info.st_mode):
                if depth >= budget.limits.maximum_filesystem_depth:
                    raise ScanFailure("filesystem_limit")
                child_directories.append(entry_path)
            elif stat.S_ISREG(info.st_mode):
                if entry.name.lower().endswith(".hog"):
                    if len(discovered) >= budget.limits.maximum_root_archives:
                        raise ScanFailure("root_archive_limit")
                    discovered.append(entry_path)
            else:
                raise ScanFailure("unsafe_input")
        stack.extend((path, depth + 1) for path in reversed(child_directories))

    if not discovered:
        raise ScanFailure("missing_input")
    discovered.sort(key=lambda path: (path.as_posix().upper(), path.as_posix()))
    return tuple(discovered)


def _normalize_member_name(value: str, maximum_bytes: int) -> str:
    try:
        encoded = value.encode("ascii")
    except UnicodeEncodeError as exc:
        raise ScanFailure("archive_name_invalid") from exc
    if not encoded or len(encoded) > maximum_bytes:
        raise ScanFailure("archive_name_invalid")
    normalized = value.replace("\\", "/")
    if normalized.startswith("/") or (
        len(normalized) >= 2 and normalized[0].isalpha() and normalized[1] == ":"
    ):
        raise ScanFailure("archive_name_invalid")
    components = normalized.split("/")
    if any(component in {"", ".", ".."} for component in components):
        raise ScanFailure("archive_name_invalid")
    for component in components:
        if ":" in component or any(
            ord(character) < 0x20 or ord(character) > 0x7E
            for character in component
        ):
            raise ScanFailure("archive_name_invalid")
    canonical = "/".join(component.upper() for component in components)
    if len(canonical.encode("ascii")) > maximum_bytes:
        raise ScanFailure("archive_name_invalid")
    return canonical


def _extension_and_stem(normalized: str) -> tuple[str | None, str | None]:
    component_start = normalized.rfind("/") + 1
    extension_start = normalized.rfind(".")
    if extension_start <= component_start or extension_start == len(normalized) - 1:
        return None, None
    return normalized[extension_start:].lower(), normalized[:extension_start]


def _extent_bucket(size: int) -> str:
    if size == 0:
        return "zero"
    if size < 256:
        return "1_255"
    if size < 4096:
        return "256_4095"
    if size < 65_536:
        return "4096_65535"
    if size < 1_048_576:
        return "65536_1048575"
    if size < 16_777_216:
        return "1048576_16777215"
    return "16777216_plus"


def _precheck_name_table_shape(
    stream: BinaryIO,
    table_offset: int,
    table_size: int,
    expected_count: int,
    maximum_name_bytes: int,
    malformed_category: str,
) -> None:
    """Validate split shape with fixed memory before the shared HOG parser.

    ``parse_hog_span`` strips all trailing NUL bytes before splitting. Keep that
    behavior exactly: pending delimiters count only when a later non-NUL byte
    proves they are internal to the stripped table.
    """

    remaining = table_size
    cursor = table_offset
    completed_names = 0
    current_name_bytes = 0
    pending_nuls = 0
    saw_non_nul = False

    while remaining:
        chunk_size = min(remaining, NAME_TABLE_SCAN_CHUNK_BYTES)
        chunk = read_at(stream, cursor, chunk_size)
        cursor += chunk_size
        remaining -= chunk_size

        nul_count = chunk.count(0)
        if nul_count == len(chunk):
            pending_nuls += nul_count
            continue
        if nul_count == 0:
            if pending_nuls:
                completed_names += pending_nuls
                pending_nuls = 0
                current_name_bytes = 0
            if completed_names >= expected_count:
                raise ScanFailure(malformed_category)
            saw_non_nul = True
            current_name_bytes += len(chunk)
            if current_name_bytes > maximum_name_bytes:
                raise ScanFailure("archive_name_invalid")
            continue

        for value in chunk:
            if value == 0:
                pending_nuls += 1
                continue

            if pending_nuls:
                completed_names += pending_nuls
                pending_nuls = 0
                current_name_bytes = 0
            if completed_names >= expected_count:
                raise ScanFailure(malformed_category)

            saw_non_nul = True
            current_name_bytes += 1
            if current_name_bytes > maximum_name_bytes:
                raise ScanFailure("archive_name_invalid")

    observed_count = completed_names + 1 if saw_non_nul else 0
    if observed_count != expected_count:
        raise ScanFailure(malformed_category)


def _bounded_hog(
    stream: BinaryIO,
    span: Span,
    depth: int,
    budget: ScanBudget,
) -> HogDirectory:
    if depth > budget.limits.maximum_nesting_depth:
        raise ScanFailure("depth_limit")
    maximum_size = (
        budget.limits.maximum_root_archive_bytes
        if depth == 0
        else budget.limits.maximum_nested_archive_bytes
    )
    if span.size > maximum_size:
        raise ScanFailure("archive_size_limit")
    malformed = "root_hog_malformed" if depth == 0 else "nested_hog_malformed"
    if span.size < 24:
        raise ScanFailure(malformed)

    try:
        header = read_at(stream, span.offset, 20)
        _tag, count, offsets_offset, names_offset, data_offset = struct.unpack(
            "<5I", header
        )
        if count > budget.limits.maximum_archive_entries_per_directory:
            raise ScanFailure("archive_limit")
        if offsets_offset != 0x14:
            raise ScanFailure(malformed)
        expected_names_offset = offsets_offset + 4 * (count + 1)
        if names_offset != expected_names_offset:
            raise ScanFailure(malformed)
        if not (names_offset <= data_offset <= span.size):
            raise ScanFailure(malformed)
        if data_offset > budget.limits.maximum_archive_directory_bytes:
            raise ScanFailure("archive_limit")
        name_bytes = data_offset - names_offset
        budget.add_archive()
        budget.add_hog_entries(count)
        budget.add_name_table_bytes(name_bytes)
        budget.add_traversed_span(span.size)
        _precheck_name_table_shape(
            stream,
            span.offset + names_offset,
            name_bytes,
            count,
            budget.limits.maximum_name_bytes,
            malformed,
        )
        directory = parse_hog_span(stream, span.offset, span.size)
    except ScanFailure:
        raise
    except (OSError, ValueError, struct.error) as exc:
        raise ScanFailure(malformed) from exc

    if depth == 0 and directory.padding_size:
        raise ScanFailure("root_hog_malformed")
    return directory


def _scan_archive(
    stream: BinaryIO,
    span: Span,
    depth: int,
    budget: ScanBudget,
    aggregate: Aggregate,
) -> None:
    directory = _bounded_hog(stream, span, depth, budget)
    aggregate.totals["archive_occurrences"] += 1
    aggregate.depth_counts[depth] += 1
    if depth == 0:
        aggregate.archive_extent_families["root_exact"] += 1
    else:
        aggregate.totals["nested_archive_occurrences"] += 1
        family = "nested_zero_padded" if directory.padding_size else "nested_exact"
        aggregate.archive_extent_families[family] += 1

    normalized_entries: list[tuple[str, str | None, str | None, Span]] = []
    seen: set[str] = set()
    basename_extensions: dict[str, set[str]] = {}
    for entry in directory.entries:
        normalized = _normalize_member_name(
            entry.name, budget.limits.maximum_name_bytes
        )
        if normalized in seen:
            raise ScanFailure("normalized_collision")
        seen.add(normalized)
        extension, stem = _extension_and_stem(normalized)
        normalized_entries.append((normalized, extension, stem, entry))

        aggregate.totals["member_occurrences"] += 1
        category = APPROVED_EXTENSION_CATEGORIES.get(extension or "", "other")
        if category == "other":
            aggregate.totals["other_member_occurrences"] += 1
        else:
            assert extension is not None
            aggregate.totals["approved_member_occurrences"] += 1
            aggregate.extension_counts[extension] += 1
            if stem is not None:
                basename_extensions.setdefault(stem, set()).add(extension)
        aggregate.category_counts[category] += 1
        bucket = _extent_bucket(entry.size)
        aggregate.extent_buckets[bucket] += 1
        aggregate.category_extent_buckets[category][bucket] += 1

    for extensions in basename_extensions.values():
        if len(extensions) < 2:
            continue
        aggregate.totals["basename_groups"] += 1
        for left, right in itertools.combinations(sorted(extensions), 2):
            key = f"{left}+{right}"
            aggregate.basename_pair_totals[key] += 1
            aggregate.totals["basename_pairs"] += 1

    for _normalized, extension, _stem, entry in normalized_entries:
        if extension == ".hog":
            _scan_archive(stream, entry, depth + 1, budget, aggregate)


def scan_input(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    _validate_limits(limits)
    budget = ScanBudget(limits)
    roots = _discover_root_archives(root, budget)
    aggregate = Aggregate()
    aggregate.totals["root_archives_discovered"] = len(roots)
    for path in roots:
        with _open_regular(path) as (stream, size):
            _scan_archive(
                stream, Span(name="", offset=0, size=size), 0, budget, aggregate
            )
        aggregate.totals["root_archives_scanned"] += 1
    return aggregate.document()


def failure_document(category: str) -> dict[str, object]:
    aggregate = Aggregate()
    document = aggregate.document()
    document["totals"]["errors"] = 1
    document["error_categories"][category] = 1
    return document


def main(argv: Sequence[str] | None = None) -> int:
    parser = AggregateArgumentParser(add_help=False)
    parser.add_argument("input", type=Path)
    try:
        args = parser.parse_args(argv)
    except AggregateArgumentError:
        result = failure_document("config")
        print(json.dumps(result, separators=(",", ":"), sort_keys=True))
        return 1

    try:
        result = scan_input(args.input)
    except ScanFailure as failure:
        result = failure_document(failure.category)
    except (OSError, ValueError, struct.error):
        result = failure_document("io")
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 1 if result["totals"]["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
