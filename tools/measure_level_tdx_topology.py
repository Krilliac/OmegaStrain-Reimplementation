#!/usr/bin/env python3
"""Measure extension-bounded TDX containment in the common level archive graph.

The report is deliberately fixed-schema and aggregate-only.  It never emits
paths, archive/member names, hashes, payload bytes, per-level rows, or inferred
catalog/material bindings.  It scans each level's complete recursive common
DATA.HOG graph.  DATA.POP references designate cell-container occurrences for
separate accounting; they do not bound common-graph traversal.  Candidates are
recognized only by a normalized ``.TDX`` suffix, and sibling texture containers
are excluded.  Archive containment is the only relationship this tool measures.

Exact runtime logical-output and item budgets are intentionally not reported:
this Python scan does not execute the native measured TDX decoder or the native
level-texture runtime path, and it does not observe C++ object sizes.  Fixed
``measurement_gaps`` flags keep those absent metrics explicit without guessing
ABI-dependent ``sizeof`` values.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import struct
import sys
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterator, Sequence

if __package__:
    from .fingerprint_assets import HogDirectory, Span, parse_hog_span, range_is_zero, read_at
    from . import score_tdx_layout_hypotheses as tdx_layout
else:  # Direct execution adds tools/ rather than the repository root.
    from fingerprint_assets import HogDirectory, Span, parse_hog_span, range_is_zero, read_at
    import score_tdx_layout_hypotheses as tdx_layout


ERROR_CATEGORIES = (
    "config",
    "unsafe_input",
    "filesystem_limit",
    "missing_level_input",
    "pop_truncated",
    "pop_malformed",
    "pop_limit_exceeded",
    "common_hog_malformed",
    "cell_reference_invalid",
    "cell_hog_malformed",
    "nested_hog_malformed",
    "archive_name_invalid",
    "normalized_collision",
    "tdx_malformed",
    "tdx_limit_exceeded",
    "io",
)

TOTAL_FIELDS = (
    "levels_discovered",
    "levels_scanned",
    "manifest_cell_occurrences",
    "manifest_distinct_cell_locators",
    "repeated_manifest_cell_occurrences",
    "zero_tdx_cell_occurrences",
    "tdx_occurrences",
    "distinct_tdx_locators",
    "repeated_tdx_occurrences",
    "common_scope_tdx_occurrences",
    "cell_scope_tdx_occurrences",
    "deeper_scope_tdx_occurrences",
    "valid_tdx_occurrences",
    "malformed_tdx_occurrences",
    "archive_directories",
    "deeper_archive_occurrences",
    "normalized_collision_directories",
    "normalized_collision_entries",
    "errors",
)

MAXIMUM_FIELDS = (
    "manifest_cells_per_level",
    "tdx_occurrences_per_level",
    "tdx_occurrences_per_cell",
    "archive_entries_per_cell",
    "archive_entries_per_directory",
    "tdx_payload_bytes",
    "tdx_blocks",
    "tdx_primary_planes",
    "tdx_palette_entries",
    "tdx_structural_items",
    "tdx_owned_plane_bytes",
    "tdx_owned_palette_bytes",
    "tdx_owned_storage_bytes",
    "all_tdx_owned_storage_bytes_per_level",
    "open_container_input_bytes",
    "open_archive_entries_plus_locator_items",
    "open_normalized_locator_component_bytes",
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
    maximum_pop_bytes: int = 64 * 1024 * 1024
    maximum_data_hog_bytes: int = 64 * 1024 * 1024
    maximum_nested_hog_bytes: int = 64 * 1024 * 1024
    maximum_hog_directory_bytes: int = 128 * 1024 * 1024
    maximum_hog_entries: int = 2_000_000
    maximum_traversed_span_bytes: int = 16 * 1024 * 1024 * 1024
    maximum_nesting_depth: int = 32
    maximum_terrain_records: int = 1 << 20
    maximum_name_bytes: int = 4096
    maximum_owned_pop_name_bytes: int = 64 * 1024 * 1024
    maximum_tdx_occurrences: int = 1 << 20
    maximum_tdx_bytes: int = 64 * 1024 * 1024
    maximum_blocks_per_tdx: int = 32
    maximum_planes_per_block: int = 4
    maximum_plane_elements: int = 16 * 1024 * 1024


class ScanFailure(ValueError):
    def __init__(
        self, category: str, *, collision_directories: int = 0, collision_entries: int = 0
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


class TdxLimitFailure(ValueError):
    pass


@dataclass
class ScanBudget:
    limits: ScanLimits
    filesystem_entries: int = 0
    hog_entries: int = 0
    traversed_span_bytes: int = 0
    tdx_occurrences: int = 0

    def add_filesystem_entries(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_filesystem_entries - self.filesystem_entries:
            raise ScanFailure("filesystem_limit")
        self.filesystem_entries += count

    def add_hog_entries(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_hog_entries - self.hog_entries:
            raise ScanFailure("filesystem_limit")
        self.hog_entries += count

    def add_traversed_span(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_traversed_span_bytes - self.traversed_span_bytes:
            raise ScanFailure("filesystem_limit")
        self.traversed_span_bytes += count

    def add_tdx(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_tdx_occurrences - self.tdx_occurrences:
            raise TdxLimitFailure
        self.tdx_occurrences += count


@dataclass(frozen=True)
class TdxMeasurement:
    blocks: int
    primary_planes: int
    palette_entries: int
    structural_items: int
    owned_plane_bytes: int
    owned_palette_bytes: int

    @property
    def owned_storage_bytes(self) -> int:
        return self.owned_plane_bytes + self.owned_palette_bytes


@dataclass
class LevelMeasurement:
    totals: Counter[str] = field(default_factory=Counter)
    maxima: dict[str, int] = field(
        default_factory=lambda: {key: 0 for key in MAXIMUM_FIELDS}
    )
    errors: Counter[str] = field(default_factory=Counter)
    tdx_locators: set[tuple[str, ...]] = field(default_factory=set)
    cell_locators: set[str] = field(default_factory=set)
    open_container_input_bytes: int = 0
    open_archive_entries: int = 0
    open_locator_items: int = 0
    open_locator_component_bytes: int = 0
    all_tdx_owned_storage_bytes: int = 0

    def observe(self, field_name: str, value: int) -> None:
        self.maxima[field_name] = max(self.maxima[field_name], value)


@dataclass
class Aggregate:
    totals: Counter[str] = field(default_factory=Counter)
    maxima: dict[str, int] = field(
        default_factory=lambda: {key: 0 for key in MAXIMUM_FIELDS}
    )
    errors: Counter[str] = field(default_factory=Counter)

    def observe(self, field_name: str, value: int) -> None:
        self.maxima[field_name] = max(self.maxima[field_name], value)

    def record_failure(self, failure: ScanFailure) -> None:
        self.errors[failure.category] += 1
        self.totals["normalized_collision_directories"] += failure.collision_directories
        self.totals["normalized_collision_entries"] += failure.collision_entries

    def merge_level(self, level: LevelMeasurement) -> None:
        self.totals.update(level.totals)
        self.totals["manifest_distinct_cell_locators"] += len(level.cell_locators)
        self.totals["repeated_manifest_cell_occurrences"] += (
            level.totals["manifest_cell_occurrences"] - len(level.cell_locators)
        )
        self.totals["distinct_tdx_locators"] += len(level.tdx_locators)
        self.totals["repeated_tdx_occurrences"] += (
            level.totals["tdx_occurrences"] - len(level.tdx_locators)
        )
        self.errors.update(level.errors)
        level.observe("tdx_occurrences_per_level", level.totals["tdx_occurrences"])
        level.observe(
            "all_tdx_owned_storage_bytes_per_level",
            level.all_tdx_owned_storage_bytes,
        )
        level.observe("open_container_input_bytes", level.open_container_input_bytes)
        level.observe(
            "open_archive_entries_plus_locator_items",
            level.open_archive_entries + level.open_locator_items,
        )
        level.observe(
            "open_normalized_locator_component_bytes",
            level.open_locator_component_bytes,
        )
        for key, value in level.maxima.items():
            self.observe(key, value)

    def document(self) -> dict[str, object]:
        errors = sum(self.errors.values())
        totals = {key: int(self.totals[key]) for key in TOTAL_FIELDS}
        totals["errors"] = errors
        return {
            "schema_version": 1,
            "scope": (
                "fixed aggregate extension-bounded containment of normalized "
                ".TDX-suffixed members in the recursive common DATA.HOG graph, with "
                "manifest-designated cell-occurrence accounting from DATA.POP; sibling "
                "texture containers are excluded; no paths, names, hashes, payloads, "
                "per-level rows, or catalog bindings"
            ),
            "totals": totals,
            "maxima": {key: int(self.maxima[key]) for key in MAXIMUM_FIELDS},
            "error_categories": {
                key: int(self.errors[key]) for key in ERROR_CATEGORIES
            },
            "measurement_gaps": {key: 1 for key in MEASUREMENT_GAPS},
        }


def _validate_limits(limits: ScanLimits) -> None:
    values = (
        limits.maximum_levels,
        limits.maximum_filesystem_entries,
        limits.maximum_pop_bytes,
        limits.maximum_data_hog_bytes,
        limits.maximum_nested_hog_bytes,
        limits.maximum_hog_directory_bytes,
        limits.maximum_hog_entries,
        limits.maximum_traversed_span_bytes,
        limits.maximum_nesting_depth,
        limits.maximum_terrain_records,
        limits.maximum_name_bytes,
        limits.maximum_owned_pop_name_bytes,
        limits.maximum_tdx_occurrences,
        limits.maximum_tdx_bytes,
        limits.maximum_blocks_per_tdx,
        limits.maximum_planes_per_block,
        limits.maximum_plane_elements,
    )
    if any(value <= 0 for value in values):
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


def _require_directory(path: Path) -> None:
    try:
        info = os.stat(path, follow_symlinks=False)
    except OSError as exc:
        raise ScanFailure("unsafe_input") from exc
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


def _read_exact(stream: BinaryIO, size: int, truncated_category: str) -> bytes:
    if size < 0:
        raise ScanFailure(truncated_category)
    data = stream.read(size)
    if len(data) != size:
        raise ScanFailure(truncated_category)
    return data


def _parse_pop_cell_names(
    stream: BinaryIO, file_size: int, limits: ScanLimits
) -> tuple[str, ...]:
    if file_size > limits.maximum_pop_bytes:
        raise ScanFailure("pop_limit_exceeded")
    if file_size < 16:
        raise ScanFailure("pop_truncated")
    stream.seek(0)
    header = _read_exact(stream, 12, "pop_truncated")
    observed, tag, count = struct.unpack("<I4sI", header)
    if observed != 70 or tag != b"TER:":
        raise ScanFailure("pop_malformed")
    if count > limits.maximum_terrain_records:
        raise ScanFailure("pop_limit_exceeded")
    if count * 9 > file_size - 12:
        raise ScanFailure("pop_truncated")

    names: list[str] = []
    owned_name_bytes = 0
    for _ in range(count):
        _read_exact(stream, 8, "pop_truncated")
        raw_name = bytearray()
        while True:
            value = _read_exact(stream, 1, "pop_truncated")[0]
            if value == 0:
                break
            if value < 0x20 or value > 0x7E:
                raise ScanFailure("pop_malformed")
            if len(raw_name) >= limits.maximum_name_bytes:
                raise ScanFailure("pop_limit_exceeded")
            raw_name.append(value)
        if not raw_name:
            raise ScanFailure("pop_malformed")
        if len(raw_name) > limits.maximum_owned_pop_name_bytes - owned_name_bytes:
            raise ScanFailure("pop_limit_exceeded")
        owned_name_bytes += len(raw_name)
        names.append(raw_name.decode("ascii"))
        aligned = (stream.tell() + 3) & ~3
        if aligned > file_size:
            raise ScanFailure("pop_truncated")
        _read_exact(stream, aligned - stream.tell(), "pop_truncated")

    if _read_exact(stream, 4, "pop_truncated") != b"GOB:":
        raise ScanFailure("pop_malformed")
    return tuple(names)


def _normalize_game_path(value: str, maximum_bytes: int) -> str:
    if not value or len(value) > maximum_bytes:
        raise ScanFailure("archive_name_invalid")
    if value[0] in "/\\" or (
        len(value) >= 2 and value[0].isascii() and value[0].isalpha() and value[1] == ":"
    ):
        raise ScanFailure("archive_name_invalid")
    components: list[str] = []
    component: list[str] = []
    for character in value:
        code = ord(character)
        if character in "/\\":
            if component:
                item = "".join(component)
                if item in {".", ".."}:
                    raise ScanFailure("archive_name_invalid")
                components.append(item)
                component.clear()
            continue
        if code < 0x20 or code > 0x7E:
            raise ScanFailure("archive_name_invalid")
        component.append(chr(code - 32) if 0x61 <= code <= 0x7A else character)
    if component:
        item = "".join(component)
        if item in {".", ".."}:
            raise ScanFailure("archive_name_invalid")
        components.append(item)
    if not components:
        raise ScanFailure("archive_name_invalid")
    normalized = "/".join(components)
    if len(normalized) > maximum_bytes:
        raise ScanFailure("archive_name_invalid")
    return normalized


def _reference_stem(normalized: str) -> str:
    component_start = normalized.rfind("/") + 1
    extension = normalized.rfind(".")
    if extension < component_start + 1 or extension + 1 >= len(normalized):
        raise ScanFailure("cell_reference_invalid")
    return normalized[:extension]


def _bounded_hog(
    stream: BinaryIO,
    span: Span,
    budget: ScanBudget,
    *,
    nested: bool,
    malformed_category: str,
) -> HogDirectory:
    maximum_span = (
        budget.limits.maximum_nested_hog_bytes
        if nested
        else budget.limits.maximum_data_hog_bytes
    )
    if span.size < 20 or span.size > maximum_span:
        raise ScanFailure(malformed_category)
    try:
        header = read_at(stream, span.offset, 20)
        _tag, count, _offsets, _names, data_offset = struct.unpack("<5I", header)
        if data_offset > budget.limits.maximum_hog_directory_bytes:
            raise ScanFailure("filesystem_limit")
        budget.add_hog_entries(count)
        budget.add_traversed_span(span.size)
        return parse_hog_span(stream, span.offset, span.size)
    except ScanFailure:
        raise
    except (OSError, ValueError, struct.error) as exc:
        raise ScanFailure(malformed_category) from exc


def _directory_index(
    directory: HogDirectory, maximum_name_bytes: int
) -> dict[str, Span]:
    index: dict[str, Span] = {}
    collisions = 0
    for entry in directory.entries:
        normalized = _normalize_game_path(entry.name, maximum_name_bytes)
        if normalized in index:
            collisions += 1
        else:
            index[normalized] = entry
    if collisions:
        raise ScanFailure(
            "normalized_collision", collision_directories=1, collision_entries=collisions
        )
    return index


def _manifest_cell_entries(
    names: Sequence[str], directory: HogDirectory, maximum_name_bytes: int
) -> tuple[tuple[str, Span], ...]:
    index = _directory_index(directory, maximum_name_bytes)
    stems: dict[str, tuple[str, Span]] = {}
    collisions = 0
    for normalized, entry in index.items():
        stem = _reference_stem(normalized)
        if stem in stems:
            collisions += 1
        else:
            stems[stem] = (normalized, entry)
    if collisions:
        raise ScanFailure(
            "normalized_collision", collision_directories=1, collision_entries=collisions
        )

    resolved: list[tuple[str, Span]] = []
    for name in names:
        normalized = _normalize_game_path(name, maximum_name_bytes)
        stem = _reference_stem(normalized)
        match = stems.get(stem)
        if match is None:
            raise ScanFailure("cell_reference_invalid")
        resolved.append(match)
    return tuple(resolved)


def _checked_plane_bytes(transfer_code: int, width: int, height: int, limit: int) -> int:
    if width <= 0 or height <= 0 or width > limit or height > limit // width:
        raise TdxLimitFailure
    if width * height > limit:
        raise TdxLimitFailure
    return tdx_layout.rectangle_bytes(transfer_code, width, height)


def _measure_tdx_block(
    stream: BinaryIO,
    span: Span,
    header: tdx_layout.TdxHeader,
    block_index: int,
    limits: ScanLimits,
) -> tuple[int, int, int]:
    block_relative = tdx_layout.HEADER_BYTES + block_index * header.block_stride
    available = min(header.block_stride, max(0, span.size - block_relative))
    if available < tdx_layout.BLOCK_HEADER_BYTES:
        raise ValueError("truncated block")
    block_offset = span.offset + block_relative
    primary_base = tdx_layout.block_u32(stream, block_offset, available, 0x18)
    secondary_base = tdx_layout.block_u32(stream, block_offset, available, 0x1C)
    indexed = header.bits_per_pixel in (4, 8)
    primary_data_base = (
        primary_base
        + header.primary_descriptor_bytes
        + (tdx_layout.PALETTE_SLOT_BYTES if indexed else 0)
    )

    planes: list[tdx_layout.PlaneLayout] = []
    object_offsets: list[int] = []
    for plane_index in range(header.primary_count):
        object_offset = primary_base + tdx_layout.block_u32(
            stream, block_offset, available, plane_index * 4
        )
        if object_offset < tdx_layout.BLOCK_HEADER_BYTES or object_offset + tdx_layout.OBJECT_BYTES > available:
            raise ValueError("primary object extent")
        if object_offsets and object_offset <= object_offsets[-1]:
            raise ValueError("primary object order")
        if any(
            tdx_layout.ranges_overlap(
                object_offset, tdx_layout.OBJECT_BYTES, prior, tdx_layout.OBJECT_BYTES
            )
            for prior in object_offsets
        ):
            raise ValueError("object overlap")
        object_offsets.append(object_offset)
        transfer_code = (
            tdx_layout.block_u32(stream, block_offset, available, object_offset + 0x04)
            >> 24
        ) & 0x3F
        if (header.bits_per_pixel, transfer_code) not in tdx_layout.KNOWN_PRIMARY_PAIRS:
            raise ValueError("transfer family")
        width = tdx_layout.block_u32(stream, block_offset, available, object_offset + 0x20)
        height = tdx_layout.block_u32(stream, block_offset, available, object_offset + 0x24)
        byte_count = _checked_plane_bytes(
            transfer_code, width, height, limits.maximum_plane_elements
        )
        data_offset = primary_data_base + tdx_layout.block_u32(
            stream, block_offset, available, object_offset + 0x54
        )
        if data_offset > header.block_stride or data_offset > available:
            raise ValueError("primary data reference")
        planes.append(
            tdx_layout.PlaneLayout(
                object_offset, data_offset, width, height, transfer_code, byte_count
            )
        )

    palette: tdx_layout.PaletteLayout | None = None
    if indexed:
        palette_object = secondary_base + tdx_layout.block_u32(
            stream, block_offset, available, 0x14
        )
        if palette_object < tdx_layout.BLOCK_HEADER_BYTES or palette_object + tdx_layout.OBJECT_BYTES > available:
            raise ValueError("palette object extent")
        if any(
            tdx_layout.ranges_overlap(
                palette_object, tdx_layout.OBJECT_BYTES, prior, tdx_layout.OBJECT_BYTES
            )
            for prior in object_offsets
        ):
            raise ValueError("palette object overlap")
        object_offsets.append(palette_object)
        palette_transfer = (
            tdx_layout.block_u32(stream, block_offset, available, palette_object + 0x04)
            >> 24
        ) & 0x3F
        palette_width = tdx_layout.block_u32(
            stream, block_offset, available, palette_object + 0x20
        )
        palette_height = tdx_layout.block_u32(
            stream, block_offset, available, palette_object + 0x24
        )
        expected = (8, 2) if header.bits_per_pixel == 4 else (16, 16)
        if palette_transfer != 0 or (palette_width, palette_height) != expected:
            raise ValueError("palette layout")
        entry_count = 16 if header.bits_per_pixel == 4 else 256
        palette_bytes = entry_count * 4
        secondary_data_base = (
            secondary_base
            + header.primary_descriptor_bytes
            + header.secondary_descriptor_bytes
        )
        palette_data = secondary_data_base + tdx_layout.block_u32(
            stream, block_offset, available, palette_object + 0x54
        )
        if palette_data + palette_bytes > available:
            raise ValueError("palette data extent")
        palette = tdx_layout.PaletteLayout(
            palette_object, palette_data, entry_count, palette_bytes
        )

    for index, plane in enumerate(planes):
        expected_end = (
            planes[index + 1].data_offset
            if index + 1 < len(planes)
            else header.block_stride
        )
        if plane.data_offset + plane.byte_count != expected_end:
            raise ValueError("primary storage extent")
        if index and plane.data_offset <= planes[index - 1].data_offset:
            raise ValueError("primary storage order")

    if palette is not None:
        if palette.data_offset + tdx_layout.PALETTE_SLOT_BYTES != planes[0].data_offset:
            raise ValueError("palette slot extent")
        if not range_is_zero(
            stream,
            block_offset + palette.data_offset + palette.byte_count,
            tdx_layout.PALETTE_SLOT_BYTES - palette.byte_count,
        ):
            raise ValueError("palette slot padding")

    for object_offset in object_offsets:
        for plane in planes:
            if tdx_layout.ranges_overlap(
                object_offset, tdx_layout.OBJECT_BYTES, plane.data_offset, plane.byte_count
            ):
                raise ValueError("object/data overlap")
        if palette and tdx_layout.ranges_overlap(
            object_offset,
            tdx_layout.OBJECT_BYTES,
            palette.data_offset,
            tdx_layout.PALETTE_SLOT_BYTES,
        ):
            raise ValueError("object/palette overlap")

    uses_implicit_zero = False
    for plane in planes:
        if plane.data_offset + plane.byte_count <= available:
            continue
        missing = header.counted_size - span.size
        signature = (
            header.bits_per_pixel,
            header.sample_width,
            header.sample_height,
            missing,
            header.block_stride,
            primary_base,
            secondary_base,
            plane.object_offset,
            plane.data_offset,
            plane.transfer_code,
            plane.width,
            plane.height,
            palette.object_offset if palette else 0,
            palette.data_offset if palette else 0,
        )
        if (
            not header.implicit_zero_suffix
            or len(planes) != 1
            or block_index != 0
            or plane.data_offset + plane.byte_count != header.block_stride
            or signature not in tdx_layout.IMPLICIT_ZERO_FAMILIES
        ):
            raise ValueError("implicit zero family")
        uses_implicit_zero = True
    if header.implicit_zero_suffix != uses_implicit_zero:
        raise ValueError("implicit zero mismatch")

    return (
        sum(plane.byte_count for plane in planes),
        palette.byte_count if palette else 0,
        palette.entry_count if palette else 0,
    )


def _measure_tdx(
    stream: BinaryIO, span: Span, budget: ScanBudget
) -> TdxMeasurement:
    budget.add_tdx(1)
    limits = budget.limits
    if span.size < 0 or span.size > limits.maximum_tdx_bytes:
        raise TdxLimitFailure
    if span.size >= tdx_layout.HEADER_BYTES:
        raw = read_at(stream, span.offset, tdx_layout.HEADER_BYTES)
        blocks = struct.unpack_from("<H", raw, 0x22)[0]
        planes = struct.unpack_from("<H", raw, 0x24)[0]
        if blocks > limits.maximum_blocks_per_tdx or planes > limits.maximum_planes_per_block:
            raise TdxLimitFailure
    parser_limits = tdx_layout.ScanLimits(
        maximum_tdx_bytes=limits.maximum_tdx_bytes,
        maximum_blocks_per_tdx=limits.maximum_blocks_per_tdx,
        maximum_planes_per_block=limits.maximum_planes_per_block,
        maximum_plane_texels=limits.maximum_plane_elements,
    )
    header = tdx_layout.parse_header(stream, span, parser_limits)
    owned_plane_bytes = 0
    owned_palette_bytes = 0
    palette_entries = 0
    for block_index in range(header.block_count):
        plane_bytes, palette_bytes, entries = _measure_tdx_block(
            stream, span, header, block_index, limits
        )
        owned_plane_bytes += plane_bytes
        owned_palette_bytes += palette_bytes
        palette_entries += entries
    primary_planes = header.block_count * header.primary_count
    palette_objects = header.block_count if header.palette_count else 0
    structural_items = (
        1 + header.block_count + primary_planes + palette_objects + palette_entries
    )
    return TdxMeasurement(
        blocks=header.block_count,
        primary_planes=primary_planes,
        palette_entries=palette_entries,
        structural_items=structural_items,
        owned_plane_bytes=owned_plane_bytes,
        owned_palette_bytes=owned_palette_bytes,
    )


class _LevelScanner:
    def __init__(
        self,
        stream: BinaryIO,
        common_size: int,
        common_directory: HogDirectory,
        budget: ScanBudget,
        result: LevelMeasurement,
    ) -> None:
        self.stream = stream
        self.common_size = common_size
        self.common_directory = common_directory
        self.common_entries = len(common_directory.entries)
        self.budget = budget
        self.result = result

    def _record_archive(
        self,
        directory: HogDirectory,
        span_size: int,
        depth: int,
        *,
        deeper_archive: bool = False,
    ) -> None:
        self.result.totals["archive_directories"] += 1
        self.result.totals["deeper_archive_occurrences"] += int(deeper_archive)
        self.result.open_container_input_bytes += span_size
        self.result.open_archive_entries += len(directory.entries)
        self.result.observe("archive_entries_per_directory", len(directory.entries))
        self.result.observe("open_archive_depth", depth)

    def _record_tdx(
        self,
        entry: Span,
        normalized: str,
        chain: tuple[str, ...],
        chain_sizes: tuple[int, ...],
        chain_entry_counts: tuple[int, ...],
        scope: str,
    ) -> None:
        self.result.totals["tdx_occurrences"] += 1
        self.result.totals[f"{scope}_scope_tdx_occurrences"] += 1
        locator = (*chain, normalized)
        self.result.tdx_locators.add(locator)
        self.result.open_locator_items += 1
        self.result.open_locator_component_bytes += sum(len(item) for item in locator)
        self.result.observe("tdx_payload_bytes", entry.size)
        load_input = self.common_size + sum(chain_sizes) + entry.size
        self.result.observe("load_container_plus_tdx_input_bytes", load_input)
        self.result.observe("load_archive_depth", len(chain))

        try:
            measured = _measure_tdx(self.stream, entry, self.budget)
        except TdxLimitFailure:
            self.result.errors["tdx_limit_exceeded"] += 1
            self.result.totals["malformed_tdx_occurrences"] += 1
            return
        except (OSError, ValueError, struct.error):
            self.result.errors["tdx_malformed"] += 1
            self.result.totals["malformed_tdx_occurrences"] += 1
            return

        self.result.totals["valid_tdx_occurrences"] += 1
        self.result.observe("tdx_blocks", measured.blocks)
        self.result.observe("tdx_primary_planes", measured.primary_planes)
        self.result.observe("tdx_palette_entries", measured.palette_entries)
        self.result.observe("tdx_structural_items", measured.structural_items)
        self.result.observe("tdx_owned_plane_bytes", measured.owned_plane_bytes)
        self.result.observe("tdx_owned_palette_bytes", measured.owned_palette_bytes)
        self.result.observe("tdx_owned_storage_bytes", measured.owned_storage_bytes)
        self.result.all_tdx_owned_storage_bytes += measured.owned_storage_bytes
        structural_items = (
            self.common_entries + sum(chain_entry_counts) + measured.structural_items
        )
        self.result.observe(
            "load_archive_entries_plus_tdx_structural_items", structural_items
        )

    def scan_archive(
        self,
        directory: HogDirectory,
        chain: tuple[str, ...],
        chain_sizes: tuple[int, ...],
        chain_entry_counts: tuple[int, ...],
        direct_scope: str,
        *,
        skip_hog_names: frozenset[str] = frozenset(),
    ) -> int:
        if len(chain) > self.budget.limits.maximum_nesting_depth:
            raise ScanFailure("filesystem_limit")
        index = _directory_index(directory, self.budget.limits.maximum_name_bytes)
        tdx_count = 0
        for normalized, entry in index.items():
            if normalized.endswith(".TDX"):
                scope = direct_scope if len(chain) <= 1 else "deeper"
                self._record_tdx(
                    entry,
                    normalized,
                    chain,
                    chain_sizes,
                    chain_entry_counts,
                    scope,
                )
                tdx_count += 1
                continue
            if not normalized.endswith(".HOG") or normalized in skip_hog_names:
                continue
            nested = _bounded_hog(
                self.stream,
                entry,
                self.budget,
                nested=True,
                malformed_category="nested_hog_malformed",
            )
            next_chain = (*chain, normalized)
            next_sizes = (*chain_sizes, entry.size)
            next_counts = (*chain_entry_counts, len(nested.entries))
            self._record_archive(
                nested, entry.size, len(next_chain), deeper_archive=True
            )
            tdx_count += self.scan_archive(
                nested,
                next_chain,
                next_sizes,
                next_counts,
                "deeper",
            )
        return tdx_count


def _scan_level(
    pop_path: Path,
    hog_path: Path,
    budget: ScanBudget,
) -> LevelMeasurement:
    with _open_regular(pop_path) as (pop_stream, pop_size):
        cell_names = _parse_pop_cell_names(pop_stream, pop_size, budget.limits)
    with _open_regular(hog_path) as (hog_stream, hog_size):
        common_span = Span("", 0, hog_size)
        common = _bounded_hog(
            hog_stream,
            common_span,
            budget,
            nested=False,
            malformed_category="common_hog_malformed",
        )
        cells = _manifest_cell_entries(
            cell_names, common, budget.limits.maximum_name_bytes
        )
        result = LevelMeasurement()
        result.totals["manifest_cell_occurrences"] = len(cells)
        result.observe("manifest_cells_per_level", len(cells))
        result.cell_locators.update(normalized for normalized, _entry in cells)
        scanner = _LevelScanner(hog_stream, hog_size, common, budget, result)
        scanner._record_archive(common, hog_size, 0)

        referenced = frozenset(normalized for normalized, _entry in cells)
        scanner.scan_archive(
            common,
            (),
            (),
            (),
            "common",
            skip_hog_names=referenced,
        )
        for normalized, entry in cells:
            cell = _bounded_hog(
                hog_stream,
                entry,
                budget,
                nested=True,
                malformed_category="cell_hog_malformed",
            )
            scanner._record_archive(cell, entry.size, 1)
            result.observe("archive_entries_per_cell", len(cell.entries))
            before = result.totals["tdx_occurrences"]
            scanner.scan_archive(
                cell,
                (normalized,),
                (entry.size,),
                (len(cell.entries),),
                "cell",
            )
            cell_tdx = result.totals["tdx_occurrences"] - before
            result.observe("tdx_occurrences_per_cell", cell_tdx)
            if cell_tdx == 0:
                result.totals["zero_tdx_cell_occurrences"] += 1
        return result


def _discover_levels(
    root: Path, budget: ScanBudget
) -> tuple[tuple[Path, Path], ...]:
    gamedata = root / "GAMEDATA"
    _require_directory(gamedata)
    discovered: list[tuple[Path, Path]] = []
    try:
        with os.scandir(gamedata) as entries:
            ordered = []
            for entry in entries:
                budget.add_filesystem_entries(1)
                ordered.append(entry)
        ordered.sort(key=lambda entry: entry.name.upper())
        for entry in ordered:
            info = entry.stat(follow_symlinks=False)
            path = Path(entry.path)
            if entry.is_symlink() or getattr(entry, "is_junction", lambda: False)() or _stat_is_reparse(info):
                raise ScanFailure("unsafe_input")
            if not entry.is_dir(follow_symlinks=False):
                continue
            pop_path = path / "DATA.POP"
            hog_path = path / "DATA.HOG"
            pop_exists = pop_path.exists()
            hog_exists = hog_path.exists()
            budget.add_filesystem_entries(int(pop_exists) + int(hog_exists))
            if not pop_exists:
                continue
            if len(discovered) >= budget.limits.maximum_levels:
                raise ScanFailure("filesystem_limit")
            if not hog_exists:
                discovered.append((pop_path, hog_path))
                continue
            discovered.append((pop_path, hog_path))
    except ScanFailure:
        raise
    except OSError as exc:
        raise ScanFailure("io") from exc
    if not discovered:
        raise ScanFailure("missing_level_input")
    return tuple(discovered)


def scan_disc(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    _validate_limits(limits)
    _require_directory(root)
    budget = ScanBudget(limits)
    aggregate = Aggregate()
    levels = _discover_levels(root.absolute(), budget)
    aggregate.totals["levels_discovered"] = len(levels)
    for pop_path, hog_path in levels:
        if not hog_path.exists():
            aggregate.errors["missing_level_input"] += 1
            continue
        try:
            level = _scan_level(pop_path, hog_path, budget)
        except ScanFailure as failure:
            aggregate.record_failure(failure)
            continue
        aggregate.totals["levels_scanned"] += 1
        aggregate.merge_level(level)
    return aggregate.document()


def failure_document(category: str) -> dict[str, object]:
    aggregate = Aggregate()
    aggregate.errors[category] = 1
    return aggregate.document()


def main(argv: Sequence[str] | None = None) -> int:
    parser = AggregateArgumentParser()
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
