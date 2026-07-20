#!/usr/bin/env python3
"""Measure aggregate HOG-member payload sizes without exporting identity.

The report has a frozen suffix vocabulary and contains only counts and payload-size
aggregates. It never emits input paths, member names, hashes, bytes, offsets,
per-file rows, unknown raw suffixes, or exception text. ``size_gcd`` is computed
only from payload sizes; it is never evidence of payload-address alignment.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
from dataclasses import dataclass, field, fields
from pathlib import Path
from types import MappingProxyType
from typing import BinaryIO, Mapping, Sequence

if __package__:
    from .fingerprint_assets import HogDirectory, Span, read_at
    from . import measure_frontend_hog_topology as hog_topology
else:  # Direct execution adds tools/ rather than the repository root.
    from fingerprint_assets import HogDirectory, Span, read_at
    import measure_frontend_hog_topology as hog_topology


SCHEMA_VERSION = 1
APPROVED_SUFFIXES = (".bnk", ".fnt", ".gui", ".gun", ".ie")
DEFAULT_SUFFIXES = (".fnt", ".gui", ".ie")
MAXIMUM_COUNTER_VALUE = (1 << 63) - 1

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
    "offset_table_limit",
    "distinct_size_limit",
    "arithmetic_limit",
    "output_limit",
    "resource_limit",
    "internal",
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
    maximum_offset_table_bytes: int = 64 * 1024 * 1024
    maximum_distinct_sizes_per_suffix: int = 65_536
    maximum_total_distinct_sizes: int = 262_144
    maximum_output_bytes: int = 8192


HARD_LIMITS: Mapping[str, int] = MappingProxyType({
    "maximum_root_archives": 4096,
    "maximum_filesystem_entries": 1_000_000,
    "maximum_filesystem_depth": 64,
    "maximum_root_archive_bytes": 1_073_741_824,
    "maximum_nested_archive_bytes": 536_870_912,
    "maximum_archive_directory_bytes": 134_217_728,
    "maximum_archive_entries_per_directory": 1_000_000,
    "maximum_archive_occurrences": 1_000_000,
    "maximum_hog_entries": 8_000_000,
    "maximum_name_table_bytes": 536_870_912,
    "maximum_name_bytes": 4096,
    "maximum_traversed_span_bytes": 68_719_476_736,
    "maximum_nesting_depth": 16,
    "maximum_offset_table_bytes": 134_217_728,
    "maximum_distinct_sizes_per_suffix": 262_144,
    "maximum_total_distinct_sizes": 1_000_000,
    "maximum_output_bytes": 16_384,
})


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
class SuffixMeasurement:
    count: int = 0
    payload_size_min: int | None = None
    payload_size_max: int | None = None
    size_gcd: int = 0
    distinct_sizes: set[int] = field(default_factory=set)

    def document(self) -> dict[str, int]:
        return {
            "count": self.count,
            "payload_size_min": self.payload_size_min or 0,
            "payload_size_max": self.payload_size_max or 0,
            "distinct_payload_size_count": len(self.distinct_sizes),
            "size_gcd": self.size_gcd,
        }


@dataclass
class FingerprintBudget:
    limits: ScanLimits
    offset_table_bytes: int = 0
    total_distinct_sizes: int = 0

    def _add(self, current: int, amount: int, maximum: int, category: str) -> int:
        if amount < 0 or current < 0 or current > maximum or amount > maximum - current:
            raise ScanFailure(category)
        if amount > MAXIMUM_COUNTER_VALUE - current:
            raise ScanFailure("arithmetic_limit")
        return current + amount

    def add_offset_table_bytes(self, amount: int) -> None:
        self.offset_table_bytes = self._add(
            self.offset_table_bytes,
            amount,
            self.limits.maximum_offset_table_bytes,
            "offset_table_limit",
        )

    def add_distinct_size(self, measurement: SuffixMeasurement, size: int) -> None:
        if size in measurement.distinct_sizes:
            return
        if len(measurement.distinct_sizes) >= self.limits.maximum_distinct_sizes_per_suffix:
            raise ScanFailure("distinct_size_limit")
        self.total_distinct_sizes = self._add(
            self.total_distinct_sizes,
            1,
            self.limits.maximum_total_distinct_sizes,
            "distinct_size_limit",
        )
        measurement.distinct_sizes.add(size)


@dataclass
class Aggregate:
    suffixes: tuple[str, ...]
    measurements: dict[str, SuffixMeasurement] = field(init=False)

    def __post_init__(self) -> None:
        self.measurements = {suffix: SuffixMeasurement() for suffix in self.suffixes}

    def document(self) -> dict[str, object]:
        return {
            "schema_version": SCHEMA_VERSION,
            "scope": (
                "fixed-vocabulary aggregate HOG-member payload-size structure only; "
                "size_gcd is derived only from payload sizes and is not payload-address "
                "alignment; no paths, member names, hashes, bytes, offsets, per-file "
                "rows, unknown raw suffixes, exception text, roles, or semantics"
            ),
            "measurements": {
                suffix: self.measurements[suffix].document() for suffix in self.suffixes
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


def _validate_suffixes(suffixes: Sequence[str]) -> tuple[str, ...]:
    normalized: list[str] = []
    seen: set[str] = set()
    for suffix in suffixes:
        if not isinstance(suffix, str):
            raise ScanFailure("config")
        canonical = suffix.lower()
        if canonical not in APPROVED_SUFFIXES or canonical in seen:
            raise ScanFailure("config")
        seen.add(canonical)
        normalized.append(canonical)
    if not normalized:
        raise ScanFailure("config")
    return tuple(sorted(normalized))


def _hog_limits(limits: ScanLimits) -> hog_topology.ScanLimits:
    return hog_topology.ScanLimits(
        maximum_root_archives=limits.maximum_root_archives,
        maximum_filesystem_entries=limits.maximum_filesystem_entries,
        maximum_filesystem_depth=limits.maximum_filesystem_depth,
        maximum_root_archive_bytes=limits.maximum_root_archive_bytes,
        maximum_nested_archive_bytes=limits.maximum_nested_archive_bytes,
        maximum_archive_directory_bytes=limits.maximum_archive_directory_bytes,
        maximum_archive_entries_per_directory=limits.maximum_archive_entries_per_directory,
        maximum_archive_occurrences=limits.maximum_archive_occurrences,
        maximum_hog_entries=limits.maximum_hog_entries,
        maximum_name_table_bytes=limits.maximum_name_table_bytes,
        maximum_name_bytes=limits.maximum_name_bytes,
        maximum_traversed_span_bytes=limits.maximum_traversed_span_bytes,
        maximum_nesting_depth=limits.maximum_nesting_depth,
    )


def _checked_offset_table_bytes(count: int) -> int:
    if count < 0 or count >= MAXIMUM_COUNTER_VALUE // 4:
        raise ScanFailure("arithmetic_limit")
    return 4 * (count + 1)


def _bounded_hog(
    stream: BinaryIO,
    span: Span,
    depth: int,
    hog_budget: hog_topology.ScanBudget,
    fingerprint_budget: FingerprintBudget,
) -> HogDirectory:
    """Apply extra read accounting, then use the established bounded HOG parser."""

    malformed = "root_hog_malformed" if depth == 0 else "nested_hog_malformed"
    maximum_size = (
        hog_budget.limits.maximum_root_archive_bytes
        if depth == 0
        else hog_budget.limits.maximum_nested_archive_bytes
    )
    if depth > hog_budget.limits.maximum_nesting_depth:
        raise hog_topology.ScanFailure("depth_limit")
    if span.size > maximum_size:
        raise hog_topology.ScanFailure("archive_size_limit")
    if span.size < 24:
        raise hog_topology.ScanFailure(malformed)

    try:
        header = read_at(stream, span.offset, 20)
        _tag, count, offsets_offset, names_offset, data_offset = struct.unpack(
            "<5I", header
        )
        if count > hog_budget.limits.maximum_archive_entries_per_directory:
            raise hog_topology.ScanFailure("archive_limit")
        if offsets_offset != 0x14:
            raise hog_topology.ScanFailure(malformed)
        expected_names_offset = offsets_offset + _checked_offset_table_bytes(count)
        if names_offset != expected_names_offset:
            raise hog_topology.ScanFailure(malformed)
        if not (names_offset <= data_offset <= span.size):
            raise hog_topology.ScanFailure(malformed)
        if data_offset > hog_budget.limits.maximum_archive_directory_bytes:
            raise hog_topology.ScanFailure("archive_limit")
        fingerprint_budget.add_offset_table_bytes(
            _checked_offset_table_bytes(count)
        )
        return hog_topology._bounded_hog(stream, span, depth, hog_budget)
    except (hog_topology.ScanFailure, ScanFailure):
        raise
    except (OSError, ValueError, struct.error) as exc:
        raise hog_topology.ScanFailure(malformed) from exc


def _observe(
    measurement: SuffixMeasurement, size: int, budget: FingerprintBudget
) -> None:
    if size < 0:
        raise ScanFailure("arithmetic_limit")
    if measurement.count >= MAXIMUM_COUNTER_VALUE:
        raise ScanFailure("arithmetic_limit")
    budget.add_distinct_size(measurement, size)
    measurement.count += 1
    measurement.payload_size_min = (
        size
        if measurement.payload_size_min is None
        else min(measurement.payload_size_min, size)
    )
    measurement.payload_size_max = (
        size
        if measurement.payload_size_max is None
        else max(measurement.payload_size_max, size)
    )
    measurement.size_gcd = math.gcd(measurement.size_gcd, size)


def _scan_archive(
    stream: BinaryIO,
    span: Span,
    depth: int,
    hog_budget: hog_topology.ScanBudget,
    fingerprint_budget: FingerprintBudget,
    aggregate: Aggregate,
) -> None:
    directory = _bounded_hog(
        stream, span, depth, hog_budget, fingerprint_budget
    )
    normalized_entries: list[tuple[str | None, Span]] = []
    seen: set[str] = set()
    for entry in directory.entries:
        normalized = hog_topology._normalize_member_name(
            entry.name, hog_budget.limits.maximum_name_bytes
        )
        if normalized in seen:
            raise hog_topology.ScanFailure("normalized_collision")
        seen.add(normalized)
        extension, _stem = hog_topology._extension_and_stem(normalized)
        normalized_entries.append((extension, entry))

        if extension in aggregate.measurements:
            _observe(aggregate.measurements[extension], entry.size, fingerprint_budget)

    for extension, entry in normalized_entries:
        if extension == ".hog":
            _scan_archive(
                stream,
                entry,
                depth + 1,
                hog_budget,
                fingerprint_budget,
                aggregate,
            )


def _serialized(document: dict[str, object], maximum_output_bytes: int) -> str:
    rendered = json.dumps(document, separators=(",", ":"), sort_keys=True)
    if len(rendered.encode("utf-8")) + 1 > maximum_output_bytes:
        raise ScanFailure("output_limit")
    return rendered


def scan_input(
    root: Path,
    limits: ScanLimits = ScanLimits(),
    suffixes: Sequence[str] = DEFAULT_SUFFIXES,
) -> dict[str, object]:
    _validate_limits(limits)
    selected = _validate_suffixes(suffixes)
    hog_limits = _hog_limits(limits)
    hog_topology._validate_limits(hog_limits)
    hog_budget = hog_topology.ScanBudget(hog_limits)
    fingerprint_budget = FingerprintBudget(limits)
    aggregate = Aggregate(selected)

    roots = hog_topology._discover_root_archives(root, hog_budget)
    for path in roots:
        with hog_topology._open_regular(path) as (stream, size):
            _scan_archive(
                stream,
                Span(name="", offset=0, size=size),
                0,
                hog_budget,
                fingerprint_budget,
                aggregate,
            )

    document = aggregate.document()
    _serialized(document, limits.maximum_output_bytes)
    return document


def _public_error_category(category: object) -> str:
    if isinstance(category, str) and category in ERROR_CATEGORIES:
        return category
    return "internal"


def failure_document(
    category: object, suffixes: Sequence[str] = DEFAULT_SUFFIXES
) -> dict[str, object]:
    selected = _validate_suffixes(suffixes)
    public_category = _public_error_category(category)
    document = Aggregate(selected).document()
    document["error_categories"][public_category] = 1
    return document


def main(argv: Sequence[str] | None = None) -> int:
    parser = AggregateArgumentParser(add_help=False)
    parser.add_argument("input", type=Path)
    parser.add_argument("--suffix", action="append")
    selected = DEFAULT_SUFFIXES
    try:
        args = parser.parse_args(argv)
        selected = _validate_suffixes(args.suffix or DEFAULT_SUFFIXES)
    except (AggregateArgumentError, ScanFailure):
        result = failure_document("config")
        print(_serialized(result, HARD_LIMITS["maximum_output_bytes"]))
        return 1

    try:
        result = scan_input(args.input, suffixes=selected)
    except (hog_topology.ScanFailure, ScanFailure) as failure:
        result = failure_document(failure.category, selected)
    except MemoryError:
        result = failure_document("resource_limit", selected)
    except Exception:
        result = failure_document("internal", selected)
    print(_serialized(result, HARD_LIMITS["maximum_output_bytes"]))
    return 1 if any(result["error_categories"].values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
