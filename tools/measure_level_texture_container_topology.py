#!/usr/bin/env python3
"""Measure direct level sibling-texture containment without exporting identity.

The fixed report contains aggregate counts and structural maxima only.  It never
emits paths, archive or member names, hashes, payload bytes, or per-level rows.
The two scanned sibling-container classes are reported only as the generic
``primary`` and ``map`` classes; their selection is the experiment's explicit
scope, not a runtime ownership or necessity claim. No texture-to-material or
render binding is asserted.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import struct
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterator, Sequence

if __package__:
    from .fingerprint_assets import HogDirectory, Span, parse_hog_span, read_at
    from . import measure_level_tdx_topology as base_topology
else:
    from fingerprint_assets import HogDirectory, Span, parse_hog_span, read_at
    import measure_level_tdx_topology as base_topology


CONTAINER_CLASSES = ("primary", "map")
CONTAINER_FILENAMES = {"primary": "TEX.HOG", "map": "MAPTEX.HOG"}

ERROR_CATEGORIES = (
    "config",
    "unsafe_input",
    "filesystem_limit",
    "missing_level_input",
    "missing_texture_container",
    "container_limit_exceeded",
    "texture_container_malformed",
    "archive_name_invalid",
    "normalized_collision",
    "tdx_malformed",
    "tdx_limit_exceeded",
    "io",
)

TOTAL_FIELDS = (
    "levels_discovered",
    "levels_scanned",
    "containers_scanned",
    "archive_entries",
    "tdx_occurrences",
    "valid_tdx_occurrences",
    "malformed_tdx_occurrences",
    "non_tdx_entries",
    "normalized_collision_directories",
    "normalized_collision_entries",
    "errors",
)

CONTAINER_TOTAL_FIELDS = (
    "containers_scanned",
    "archive_entries",
    "tdx_occurrences",
    "valid_tdx_occurrences",
    "malformed_tdx_occurrences",
    "non_tdx_entries",
    "normalized_collision_directories",
    "normalized_collision_entries",
    "errors",
)

MAXIMUM_FIELDS = (
    "archive_bytes",
    "archive_entries",
    "tdx_occurrences_per_container",
    "non_tdx_entries_per_container",
    "tdx_payload_bytes",
    "tdx_blocks",
    "tdx_primary_planes",
    "tdx_palette_entries",
    "tdx_structural_items",
    "tdx_owned_plane_bytes",
    "tdx_owned_palette_bytes",
    "tdx_owned_storage_bytes",
    "all_tdx_owned_storage_bytes_per_container",
    "open_container_input_bytes",
    "open_archive_entries_plus_locator_items",
    "open_archive_depth",
    "load_container_plus_tdx_input_bytes",
    "load_archive_entries_plus_tdx_structural_items",
    "load_archive_depth",
)

MEASUREMENT_GAPS = (
    "exact_runtime_open_items_maximum",
    "exact_runtime_open_logical_output_bytes_maximum",
    "exact_runtime_load_items_maximum",
    "exact_runtime_load_logical_output_bytes_maximum",
)


@dataclass(frozen=True)
class ScanLimits:
    maximum_levels: int = 4096
    maximum_filesystem_entries: int = 500_000
    maximum_texture_container_bytes: int = 64 * 1024 * 1024
    maximum_hog_directory_bytes: int = 64 * 1024 * 1024
    maximum_hog_entries: int = 2_000_000
    maximum_traversed_span_bytes: int = 16 * 1024 * 1024 * 1024
    maximum_name_bytes: int = 4096
    maximum_tdx_occurrences: int = 1 << 20
    maximum_tdx_bytes: int = 64 * 1024 * 1024
    maximum_blocks_per_tdx: int = 32
    maximum_planes_per_block: int = 4
    maximum_plane_elements: int = 16 * 1024 * 1024


class ScanFailure(ValueError):
    def __init__(
        self,
        category: str,
        *,
        collision_directories: int = 0,
        collision_entries: int = 0,
    ) -> None:
        if category not in ERROR_CATEGORIES:
            raise ValueError("unknown aggregate error category")
        super().__init__(category)
        self.category = category
        self.collision_directories = collision_directories
        self.collision_entries = collision_entries


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
    hog_entries: int = 0
    traversed_span_bytes: int = 0
    tdx_occurrences: int = 0

    def add_filesystem_entries(self, count: int) -> None:
        remaining = self.limits.maximum_filesystem_entries - self.filesystem_entries
        if count < 0 or count > remaining:
            raise ScanFailure("filesystem_limit")
        self.filesystem_entries += count

    def add_hog_entries(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_hog_entries - self.hog_entries:
            raise ScanFailure("filesystem_limit")
        self.hog_entries += count

    def add_traversed_span(self, count: int) -> None:
        remaining = self.limits.maximum_traversed_span_bytes - self.traversed_span_bytes
        if count < 0 or count > remaining:
            raise ScanFailure("filesystem_limit")
        self.traversed_span_bytes += count

    def add_tdx(self, count: int) -> None:
        remaining = self.limits.maximum_tdx_occurrences - self.tdx_occurrences
        if count < 0 or count > remaining:
            raise base_topology.TdxLimitFailure
        self.tdx_occurrences += count


@dataclass
class ClassMeasurement:
    totals: Counter[str] = field(default_factory=Counter)
    maxima: dict[str, int] = field(
        default_factory=lambda: {key: 0 for key in MAXIMUM_FIELDS}
    )
    errors: Counter[str] = field(default_factory=Counter)

    def observe(self, key: str, value: int) -> None:
        self.maxima[key] = max(self.maxima[key], value)


@dataclass
class Aggregate:
    totals: Counter[str] = field(default_factory=Counter)
    maxima: dict[str, int] = field(
        default_factory=lambda: {key: 0 for key in MAXIMUM_FIELDS}
    )
    errors: Counter[str] = field(default_factory=Counter)
    classes: dict[str, ClassMeasurement] = field(
        default_factory=lambda: {key: ClassMeasurement() for key in CONTAINER_CLASSES}
    )

    def observe(self, key: str, value: int) -> None:
        self.maxima[key] = max(self.maxima[key], value)

    def record_failure(self, container_class: str | None, failure: ScanFailure) -> None:
        self.errors[failure.category] += 1
        self.totals["normalized_collision_directories"] += failure.collision_directories
        self.totals["normalized_collision_entries"] += failure.collision_entries
        if container_class is not None:
            measured = self.classes[container_class]
            measured.errors[failure.category] += 1
            measured.totals["normalized_collision_directories"] += (
                failure.collision_directories
            )
            measured.totals["normalized_collision_entries"] += failure.collision_entries

    def merge_class(self, container_class: str, measured: ClassMeasurement) -> None:
        target = self.classes[container_class]
        target.totals.update(measured.totals)
        target.errors.update(measured.errors)
        self.totals.update(measured.totals)
        self.errors.update(measured.errors)
        for key, value in measured.maxima.items():
            target.observe(key, value)
            self.observe(key, value)

    def document(self) -> dict[str, object]:
        totals = {key: int(self.totals[key]) for key in TOTAL_FIELDS}
        totals["errors"] = int(sum(self.errors.values()))
        classes: dict[str, object] = {}
        for container_class in CONTAINER_CLASSES:
            measured = self.classes[container_class]
            class_totals = {
                key: int(measured.totals[key]) for key in CONTAINER_TOTAL_FIELDS
            }
            class_totals["errors"] = int(sum(measured.errors.values()))
            classes[container_class] = {
                "totals": class_totals,
                "maxima": {key: int(measured.maxima[key]) for key in MAXIMUM_FIELDS},
                "error_categories": {
                    key: int(measured.errors[key]) for key in ERROR_CATEGORIES
                },
            }
        return {
            "schema_version": 1,
            "scope": (
                "fixed aggregate direct level sibling-texture containment only; no "
                "paths, names, hashes, payloads, per-level rows, or render/material "
                "bindings"
            ),
            "totals": totals,
            "maxima": {key: int(self.maxima[key]) for key in MAXIMUM_FIELDS},
            "container_classes": classes,
            "error_categories": {
                key: int(self.errors[key]) for key in ERROR_CATEGORIES
            },
            "measurement_gaps": {key: 1 for key in MEASUREMENT_GAPS},
        }


@dataclass(frozen=True)
class LevelContainers:
    primary: Path | None
    map: Path | None


def _validate_limits(limits: ScanLimits) -> None:
    if any(value <= 0 for value in vars(limits).values()):
        raise ScanFailure("config")


def _stat_is_reparse(info: os.stat_result) -> bool:
    flag = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0)
    return bool(flag and getattr(info, "st_file_attributes", 0) & flag)


def _is_link_like(path: Path, info: os.stat_result | None = None) -> bool:
    checked = info if info is not None else os.stat(path, follow_symlinks=False)
    return (
        path.is_symlink()
        or getattr(path, "is_junction", lambda: False)()
        or _stat_is_reparse(checked)
    )


def _require_directory(path: Path, *, missing_category: str = "unsafe_input") -> None:
    try:
        info = os.stat(path, follow_symlinks=False)
    except FileNotFoundError as exc:
        raise ScanFailure(missing_category) from exc
    except OSError as exc:
        raise ScanFailure("io") from exc
    if _is_link_like(path, info) or not stat.S_ISDIR(info.st_mode):
        raise ScanFailure("unsafe_input")


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
        raise ScanFailure("missing_texture_container") from exc
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


def _bounded_directory_entries(
    path: Path, budget: ScanBudget
) -> list[os.DirEntry[str]]:
    try:
        with os.scandir(path) as entries:
            bounded: list[os.DirEntry[str]] = []
            for entry in entries:
                budget.add_filesystem_entries(1)
                bounded.append(entry)
    except ScanFailure:
        raise
    except OSError as exc:
        raise ScanFailure("io") from exc
    bounded.sort(key=lambda entry: entry.name.upper())
    return bounded


def _entry_is_link_like(entry: os.DirEntry[str], info: os.stat_result) -> bool:
    return (
        entry.is_symlink()
        or getattr(entry, "is_junction", lambda: False)()
        or _stat_is_reparse(info)
    )


def _discover_levels(root: Path, budget: ScanBudget) -> tuple[LevelContainers, ...]:
    gamedata = root / "GAMEDATA"
    _require_directory(gamedata, missing_category="missing_level_input")
    discovered: list[LevelContainers] = []
    target_names = frozenset(
        ("DATA.POP", "DATA.HOG", *CONTAINER_FILENAMES.values())
    )

    for entry in _bounded_directory_entries(gamedata, budget):
        try:
            info = entry.stat(follow_symlinks=False)
        except OSError as exc:
            raise ScanFailure("io") from exc
        if _entry_is_link_like(entry, info):
            raise ScanFailure("unsafe_input")
        if not stat.S_ISDIR(info.st_mode):
            continue

        child_index: dict[str, Path] = {}
        for child in _bounded_directory_entries(Path(entry.path), budget):
            try:
                child_info = child.stat(follow_symlinks=False)
            except OSError as exc:
                raise ScanFailure("io") from exc
            if _entry_is_link_like(child, child_info):
                raise ScanFailure("unsafe_input")
            normalized = child.name.upper()
            if normalized in child_index:
                raise ScanFailure("unsafe_input")
            if normalized in target_names and not stat.S_ISREG(child_info.st_mode):
                raise ScanFailure("unsafe_input")
            child_index[normalized] = Path(child.path)

        if "DATA.POP" not in child_index or "DATA.HOG" not in child_index:
            continue
        if len(discovered) >= budget.limits.maximum_levels:
            raise ScanFailure("filesystem_limit")
        discovered.append(
            LevelContainers(
                primary=child_index.get(CONTAINER_FILENAMES["primary"]),
                map=child_index.get(CONTAINER_FILENAMES["map"]),
            )
        )

    if not discovered:
        raise ScanFailure("missing_level_input")
    return tuple(discovered)


def _bounded_exact_hog(
    stream: BinaryIO, file_size: int, budget: ScanBudget
) -> HogDirectory:
    if file_size > budget.limits.maximum_texture_container_bytes:
        raise ScanFailure("container_limit_exceeded")
    if file_size < 24:
        raise ScanFailure("texture_container_malformed")
    try:
        header = read_at(stream, 0, 20)
        _tag, count, _offsets, _names, data_offset = struct.unpack("<5I", header)
        if data_offset > budget.limits.maximum_hog_directory_bytes:
            raise ScanFailure("filesystem_limit")
        budget.add_hog_entries(count)
        budget.add_traversed_span(file_size)
        directory = parse_hog_span(stream, 0, file_size)
        if directory.padding_size:
            raise ScanFailure("texture_container_malformed")
        return directory
    except ScanFailure:
        raise
    except (OSError, ValueError, struct.error) as exc:
        raise ScanFailure("texture_container_malformed") from exc


def _directory_index(
    directory: HogDirectory, maximum_name_bytes: int
) -> dict[str, Span]:
    index: dict[str, Span] = {}
    collisions = 0
    for entry in directory.entries:
        try:
            normalized = base_topology._normalize_game_path(
                entry.name, maximum_name_bytes
            )
        except base_topology.ScanFailure as exc:
            raise ScanFailure("archive_name_invalid") from exc
        if normalized in index:
            collisions += 1
        else:
            index[normalized] = entry
    if collisions:
        raise ScanFailure(
            "normalized_collision",
            collision_directories=1,
            collision_entries=collisions,
        )
    return index


def _scan_container(path: Path, budget: ScanBudget) -> ClassMeasurement:
    with _open_regular(path) as (stream, file_size):
        directory = _bounded_exact_hog(stream, file_size, budget)
        index = _directory_index(directory, budget.limits.maximum_name_bytes)
        tdx_entries = [
            entry for normalized, entry in index.items() if normalized.endswith(".TDX")
        ]
        result = ClassMeasurement()
        entry_count = len(directory.entries)
        tdx_count = len(tdx_entries)
        non_tdx_count = entry_count - tdx_count
        result.totals["containers_scanned"] = 1
        result.totals["archive_entries"] = entry_count
        result.totals["tdx_occurrences"] = tdx_count
        result.totals["non_tdx_entries"] = non_tdx_count
        result.observe("archive_bytes", file_size)
        result.observe("archive_entries", entry_count)
        result.observe("tdx_occurrences_per_container", tdx_count)
        result.observe("non_tdx_entries_per_container", non_tdx_count)
        result.observe("open_container_input_bytes", file_size)
        result.observe(
            "open_archive_entries_plus_locator_items", entry_count + tdx_count
        )

        all_owned_storage = 0
        for entry in tdx_entries:
            result.observe("tdx_payload_bytes", entry.size)
            result.observe(
                "load_container_plus_tdx_input_bytes", file_size + entry.size
            )
            try:
                measured = base_topology._measure_tdx(stream, entry, budget)
            except base_topology.TdxLimitFailure:
                result.errors["tdx_limit_exceeded"] += 1
                result.totals["malformed_tdx_occurrences"] += 1
                continue
            except (OSError, ValueError, struct.error):
                result.errors["tdx_malformed"] += 1
                result.totals["malformed_tdx_occurrences"] += 1
                continue

            result.totals["valid_tdx_occurrences"] += 1
            result.observe("tdx_blocks", measured.blocks)
            result.observe("tdx_primary_planes", measured.primary_planes)
            result.observe("tdx_palette_entries", measured.palette_entries)
            result.observe("tdx_structural_items", measured.structural_items)
            result.observe("tdx_owned_plane_bytes", measured.owned_plane_bytes)
            result.observe("tdx_owned_palette_bytes", measured.owned_palette_bytes)
            result.observe("tdx_owned_storage_bytes", measured.owned_storage_bytes)
            result.observe(
                "load_archive_entries_plus_tdx_structural_items",
                entry_count + measured.structural_items,
            )
            all_owned_storage += measured.owned_storage_bytes
        result.observe(
            "all_tdx_owned_storage_bytes_per_container", all_owned_storage
        )
        return result


def scan_disc(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    _validate_limits(limits)
    _require_directory(root)
    budget = ScanBudget(limits)
    aggregate = Aggregate()
    levels = _discover_levels(root.absolute(), budget)
    aggregate.totals["levels_discovered"] = len(levels)

    for level in levels:
        missing = False
        for container_class in CONTAINER_CLASSES:
            if getattr(level, container_class) is None:
                aggregate.record_failure(
                    container_class, ScanFailure("missing_texture_container")
                )
                missing = True
        if missing:
            continue

        measured_classes: dict[str, ClassMeasurement] = {}
        structural_failure = False
        for container_class in CONTAINER_CLASSES:
            path = getattr(level, container_class)
            assert path is not None
            try:
                measured_classes[container_class] = _scan_container(path, budget)
            except ScanFailure as failure:
                aggregate.record_failure(container_class, failure)
                structural_failure = True
        if structural_failure:
            continue

        aggregate.totals["levels_scanned"] += 1
        for container_class in CONTAINER_CLASSES:
            aggregate.merge_class(container_class, measured_classes[container_class])
    return aggregate.document()


def failure_document(category: str) -> dict[str, object]:
    aggregate = Aggregate()
    aggregate.errors[category] = 1
    return aggregate.document()


def main(argv: Sequence[str] | None = None) -> int:
    parser = AggregateArgumentParser(add_help=False)
    parser.add_argument("disc_root", type=Path)
    try:
        args = parser.parse_args(argv)
    except AggregateArgumentError:
        result = failure_document("config")
        print(json.dumps(result, separators=(",", ":"), sort_keys=True))
        return 1
    try:
        result = scan_disc(args.disc_root)
    except ScanFailure as failure:
        result = failure_document(failure.category)
    except (OSError, ValueError, struct.error):
        result = failure_document("io")
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 1 if result["totals"]["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
