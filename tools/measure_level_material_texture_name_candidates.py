#!/usr/bin/env python3
"""Measure exact VUM-name to level-TDX-locator candidates without exporting identity.

This is an analysis-only lexical-coherence experiment. It compares only normalized,
full terminal names ending in .TDX. Equality nominates a candidate; it does not prove
that a catalog name is a texture name or that retail code performs the lookup.

The fixed report contains aggregate counters and maxima only. It never emits paths,
archive/member/catalog names, hashes, offsets, payload bytes, locator identities,
per-level rows, or inferred material/render semantics. Private strings exist only in
bounded per-level working sets and are discarded before the next level is scanned.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import stat
import struct
from collections import Counter
from contextlib import contextmanager
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterator, Sequence

try:
    from tools import measure_level_tdx_topology as base_topology
    from tools import vum_material_catalog_contract as vum_contract
    from tools.fingerprint_assets import HogDirectory, Span, parse_hog_span, read_at
except ModuleNotFoundError:  # Direct execution from the tools directory.
    import measure_level_tdx_topology as base_topology
    import vum_material_catalog_contract as vum_contract
    from fingerprint_assets import HogDirectory, Span, parse_hog_span, read_at


CONTAINER_CLASSES = ("primary", "map")
CONTAINER_FILENAMES = {"primary": "TEX.HOG", "map": "MAPTEX.HOG"}

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
    "vum_missing",
    "vum_duplicate",
    "vum_malformed",
    "vum_limit_exceeded",
    "missing_texture_container",
    "texture_container_malformed",
    "archive_name_invalid",
    "normalized_collision",
    "aggregate_overflow",
    "io",
)

TOTAL_FIELDS = (
    "levels_discovered",
    "levels_scanned",
    "manifest_cell_occurrences",
    "vum_catalogs",
    "vum_name_occurrences",
    "material_records",
    "dense_name_references",
    "texture_containers",
    "tdx_locator_occurrences",
    "errors",
)

MATCH_STATUS_FIELDS = (
    "invalid_member_candidate",
    "non_tdx_suffix",
    "unmatched",
    "unique_primary",
    "unique_map",
    "ambiguous_cross_class",
)

MATERIAL_RECORD_FLAG_FIELDS = (
    "all_references_unique",
    "any_unique",
    "any_unmatched",
    "any_ambiguous",
    "any_ineligible",
)

LOCATOR_COVERAGE_FIELDS = (
    "reached_by_unique_candidate",
    "reached_only_ambiguously",
    "unreached",
)

MAXIMUM_FIELDS = (
    "vum_names_per_catalog",
    "material_records_per_catalog",
    "dense_name_references_per_catalog",
    "tdx_locators_per_level",
    "candidate_locators_per_name",
)

MEASUREMENT_GAPS = (
    "retail_name_lookup_observed",
    "retail_material_record_consumption_observed",
    "independent_behavioral_corroboration",
)

NON_CLAIMS = (
    "catalog_names_are_texture_names",
    "material_records_bind_textures",
    "normalized_equality_is_retail_lookup",
    "container_class_implies_priority",
    "locator_implies_cell_mesh_or_draw_binding",
    "runtime_integration_is_justified",
)

_MAX_U64 = (1 << 64) - 1
_VUM_NAME_REGION_OFFSET = vum_contract.NAME_REGION_OFFSET
_VUM_MATERIAL_RECORD_BYTES = vum_contract.MATERIAL_RECORD_BYTES
_VUM_INACTIVE_WORD = 0xFFFFFFFF


@dataclass(frozen=True)
class ScanLimits:
    maximum_levels: int = 4096
    maximum_filesystem_entries: int = 500_000
    maximum_pop_bytes: int = 64 * 1024 * 1024
    maximum_data_hog_bytes: int = 64 * 1024 * 1024
    maximum_cell_hog_bytes: int = 64 * 1024 * 1024
    maximum_texture_container_bytes: int = 64 * 1024 * 1024
    maximum_hog_directory_bytes: int = 128 * 1024 * 1024
    maximum_hog_entries: int = 2_000_000
    maximum_traversed_span_bytes: int = 16 * 1024 * 1024 * 1024
    maximum_terrain_records: int = 1 << 20
    maximum_name_bytes: int = 4096
    maximum_owned_pop_name_bytes: int = 64 * 1024 * 1024
    maximum_vum_bytes: int = 64 * 1024 * 1024
    maximum_vum_catalogs: int = 1 << 20
    maximum_vum_catalog_items: int = 1 << 20
    maximum_vum_names: int = 1 << 22
    maximum_material_records: int = 1 << 22
    maximum_dense_name_references: int = 1 << 23
    maximum_vum_metadata_records: int = 1 << 23
    maximum_tdx_occurrences: int = 1 << 20


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
    hog_entries: int = 0
    traversed_span_bytes: int = 0
    vum_catalogs: int = 0
    vum_names: int = 0
    material_records: int = 0
    dense_name_references: int = 0
    vum_metadata_records: int = 0
    tdx_occurrences: int = 0

    def _consume(self, field_name: str, count: int, maximum: int, category: str) -> None:
        current = getattr(self, field_name)
        if count < 0 or count > maximum - current:
            raise ScanFailure(category)
        setattr(self, field_name, current + count)

    def add_filesystem_entries(self, count: int) -> None:
        self._consume(
            "filesystem_entries",
            count,
            self.limits.maximum_filesystem_entries,
            "filesystem_limit",
        )

    def add_hog_entries(self, count: int) -> None:
        self._consume(
            "hog_entries", count, self.limits.maximum_hog_entries, "filesystem_limit"
        )

    def add_traversed_span(self, count: int) -> None:
        self._consume(
            "traversed_span_bytes",
            count,
            self.limits.maximum_traversed_span_bytes,
            "filesystem_limit",
        )

    def add_catalog(
        self,
        names: int,
        materials: int,
        references: int,
        metadata_records: int,
    ) -> None:
        self._consume(
            "vum_catalogs", 1, self.limits.maximum_vum_catalogs, "vum_limit_exceeded"
        )
        self._consume(
            "vum_names", names, self.limits.maximum_vum_names, "vum_limit_exceeded"
        )
        self._consume(
            "material_records",
            materials,
            self.limits.maximum_material_records,
            "vum_limit_exceeded",
        )
        self._consume(
            "dense_name_references",
            references,
            self.limits.maximum_dense_name_references,
            "vum_limit_exceeded",
        )
        self._consume(
            "vum_metadata_records",
            metadata_records,
            self.limits.maximum_vum_metadata_records,
            "vum_limit_exceeded",
        )

    def add_tdx(self, count: int) -> None:
        self._consume(
            "tdx_occurrences",
            count,
            self.limits.maximum_tdx_occurrences,
            "filesystem_limit",
        )


@dataclass(frozen=True)
class _FileSnapshot:
    device: int
    inode: int
    file_type: int
    size: int
    modification_time_ns: int
    change_time_ns: int | None
    birth_time_ns: int | None


@dataclass(frozen=True)
class _StableFileInput:
    path: Path
    discovered_snapshot: _FileSnapshot


@dataclass(frozen=True)
class LevelInputs:
    pop: _StableFileInput
    common: _StableFileInput | None
    primary: _StableFileInput | None
    map: _StableFileInput | None


@dataclass(frozen=True)
class _FilesystemEntry:
    name: str
    path: Path
    discovered_stat: os.stat_result


@dataclass(frozen=True)
class MaterialCatalog:
    names: tuple[str, ...]
    materials: tuple[tuple[int, ...], ...]


@dataclass
class LevelMeasurement:
    totals: Counter[str] = field(default_factory=Counter)
    name_occurrences: Counter[str] = field(default_factory=Counter)
    dense_name_references: Counter[str] = field(default_factory=Counter)
    material_record_flags: Counter[str] = field(default_factory=Counter)
    locator_coverage: Counter[str] = field(default_factory=Counter)
    maxima: dict[str, int] = field(
        default_factory=lambda: {key: 0 for key in MAXIMUM_FIELDS}
    )

    def observe(self, field_name: str, value: int) -> None:
        self.maxima[field_name] = max(self.maxima[field_name], value)


def _checked_add(left: int, right: int) -> int:
    if left < 0 or right < 0 or right > _MAX_U64 - left:
        raise ScanFailure("aggregate_overflow")
    return left + right


def _merged_counter(
    current: Counter[str], incoming: Counter[str], fields: Sequence[str]
) -> Counter[str]:
    merged = Counter(current)
    for key in fields:
        merged[key] = _checked_add(int(current[key]), int(incoming[key]))
    return merged


@dataclass
class Aggregate:
    totals: Counter[str] = field(default_factory=Counter)
    name_occurrences: Counter[str] = field(default_factory=Counter)
    dense_name_references: Counter[str] = field(default_factory=Counter)
    material_record_flags: Counter[str] = field(default_factory=Counter)
    locator_coverage: Counter[str] = field(default_factory=Counter)
    maxima: dict[str, int] = field(
        default_factory=lambda: {key: 0 for key in MAXIMUM_FIELDS}
    )
    errors: Counter[str] = field(default_factory=Counter)

    def record_failure(self, failure: ScanFailure) -> None:
        self.errors[failure.category] = _checked_add(
            int(self.errors[failure.category]), 1
        )

    def merge_level(self, measured: LevelMeasurement) -> None:
        total_fields = tuple(key for key in TOTAL_FIELDS if key != "errors")
        totals = _merged_counter(self.totals, measured.totals, total_fields)
        names = _merged_counter(
            self.name_occurrences, measured.name_occurrences, MATCH_STATUS_FIELDS
        )
        references = _merged_counter(
            self.dense_name_references,
            measured.dense_name_references,
            MATCH_STATUS_FIELDS,
        )
        record_flags = _merged_counter(
            self.material_record_flags,
            measured.material_record_flags,
            MATERIAL_RECORD_FLAG_FIELDS,
        )
        coverage = _merged_counter(
            self.locator_coverage,
            measured.locator_coverage,
            LOCATOR_COVERAGE_FIELDS,
        )
        maxima = {
            key: max(self.maxima[key], measured.maxima[key])
            for key in MAXIMUM_FIELDS
        }
        self.totals = totals
        self.name_occurrences = names
        self.dense_name_references = references
        self.material_record_flags = record_flags
        self.locator_coverage = coverage
        self.maxima = maxima

    def document(self) -> dict[str, object]:
        totals = {key: int(self.totals[key]) for key in TOTAL_FIELDS}
        totals["errors"] = int(sum(self.errors.values()))
        return {
            "schema_version": 1,
            "scope": (
                "fixed aggregate normalized exact terminal .TDX lexical-coherence "
                "experiment only; candidate equality is not a retail binding; no paths, "
                "names, hashes, offsets, payloads, per-level rows, locator identities, or "
                "inferred semantics"
            ),
            "totals": totals,
            "candidate_classes": {
                "normalized_exact_terminal_name": {
                    "name_occurrences": {
                        key: int(self.name_occurrences[key])
                        for key in MATCH_STATUS_FIELDS
                    },
                    "dense_name_references": {
                        key: int(self.dense_name_references[key])
                        for key in MATCH_STATUS_FIELDS
                    },
                    "material_record_flags": {
                        key: int(self.material_record_flags[key])
                        for key in MATERIAL_RECORD_FLAG_FIELDS
                    },
                    "tdx_locator_occurrences": {
                        key: int(self.locator_coverage[key])
                        for key in LOCATOR_COVERAGE_FIELDS
                    },
                }
            },
            "maxima": {key: int(self.maxima[key]) for key in MAXIMUM_FIELDS},
            "error_categories": {
                key: int(self.errors[key]) for key in ERROR_CATEGORIES
            },
            "measurement_gaps": {key: 1 for key in MEASUREMENT_GAPS},
            "non_claims": {key: 1 for key in NON_CLAIMS},
        }


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


def _entry_is_link_like(entry: os.DirEntry[str], info: os.stat_result) -> bool:
    return (
        entry.is_symlink()
        or getattr(entry, "is_junction", lambda: False)()
        or _stat_is_reparse(info)
    )


def _same_identity(left: os.stat_result, right: os.stat_result) -> bool:
    return (left.st_dev, left.st_ino) == (right.st_dev, right.st_ino)


def _file_snapshot(info: os.stat_result) -> _FileSnapshot:
    # Python's Windows path stat and handle fstat can expose different
    # st_ctime_ns semantics (creation time versus change/write time).  It is
    # therefore not a stable cross-query change-time field on Windows.  The
    # explicit birth-time field remains comparable there; POSIX st_ctime_ns is
    # a real metadata-change timestamp and is included when available.
    change_time = None if os.name == "nt" else getattr(info, "st_ctime_ns", None)
    birth_time = getattr(info, "st_birthtime_ns", None)
    return _FileSnapshot(
        device=int(info.st_dev),
        inode=int(info.st_ino),
        file_type=stat.S_IFMT(info.st_mode),
        size=int(info.st_size),
        modification_time_ns=int(info.st_mtime_ns),
        change_time_ns=None if change_time is None else int(change_time),
        birth_time_ns=None if birth_time is None else int(birth_time),
    )


def _same_directory_snapshot(left: os.stat_result, right: os.stat_result) -> bool:
    return (
        _same_identity(left, right)
        and left.st_mode == right.st_mode
        and left.st_mtime_ns == right.st_mtime_ns
    )


def _filesystem_name_sort_key(value: str) -> tuple[str, str]:
    return value.upper(), value


def _stable_file_input(entry: _FilesystemEntry | None) -> _StableFileInput | None:
    if entry is None:
        return None
    if not stat.S_ISREG(entry.discovered_stat.st_mode):
        raise ScanFailure("unsafe_input")
    return _StableFileInput(entry.path, _file_snapshot(entry.discovered_stat))


def _require_directory(path: Path, missing_category: str) -> os.stat_result:
    try:
        info = os.stat(path, follow_symlinks=False)
    except FileNotFoundError as exc:
        raise ScanFailure(missing_category) from exc
    except OSError as exc:
        raise ScanFailure("io") from exc
    if _is_link_like(path, info) or not stat.S_ISDIR(info.st_mode):
        raise ScanFailure("unsafe_input")
    return info


@contextmanager
def _open_regular(
    source: _StableFileInput, missing_category: str
) -> Iterator[tuple[BinaryIO, int]]:
    path = source.path
    try:
        before = os.stat(path, follow_symlinks=False)
        if (
            _is_link_like(path, before)
            or not stat.S_ISREG(before.st_mode)
            or _file_snapshot(before) != source.discovered_snapshot
        ):
            raise ScanFailure("unsafe_input")
        stream = path.open("rb")
    except ScanFailure:
        raise
    except FileNotFoundError as exc:
        raise ScanFailure("unsafe_input") from exc
    except OSError as exc:
        raise ScanFailure("io") from exc
    try:
        opened = os.fstat(stream.fileno())
        if (
            not stat.S_ISREG(opened.st_mode)
            or _stat_is_reparse(opened)
            or _file_snapshot(opened) != source.discovered_snapshot
        ):
            raise ScanFailure("unsafe_input")
        yield stream, source.discovered_snapshot.size
    finally:
        final_snapshot: _FileSnapshot | None = None
        final_failed = False
        try:
            final_info = os.fstat(stream.fileno())
            final_failed = (
                not stat.S_ISREG(final_info.st_mode)
                or _stat_is_reparse(final_info)
            )
            if not final_failed:
                final_snapshot = _file_snapshot(final_info)
        except OSError:
            final_failed = True
        finally:
            stream.close()

        post_snapshot: _FileSnapshot | None = None
        post_failed = False
        try:
            post_info = os.stat(path, follow_symlinks=False)
            post_failed = (
                _is_link_like(path, post_info)
                or not stat.S_ISREG(post_info.st_mode)
            )
            if not post_failed:
                post_snapshot = _file_snapshot(post_info)
        except OSError:
            post_failed = True

        if (
            final_failed
            or post_failed
            or final_snapshot != source.discovered_snapshot
            or post_snapshot != source.discovered_snapshot
        ):
            raise ScanFailure("unsafe_input")


def _bounded_directory_entries(
    path: Path, budget: ScanBudget, discovered_stat: os.stat_result
) -> list[_FilesystemEntry]:
    try:
        before = os.stat(path, follow_symlinks=False)
        if (
            _is_link_like(path, before)
            or not stat.S_ISDIR(before.st_mode)
            or not _same_directory_snapshot(before, discovered_stat)
        ):
            raise ScanFailure("unsafe_input")
        with os.scandir(path) as entries:
            bounded: list[_FilesystemEntry] = []
            for entry in entries:
                budget.add_filesystem_entries(1)
                entry_path = path / entry.name
                entry_info = os.stat(entry_path, follow_symlinks=False)
                if _entry_is_link_like(entry, entry_info):
                    raise ScanFailure("unsafe_input")
                bounded.append(
                    _FilesystemEntry(entry.name, entry_path, entry_info)
                )
        after = os.stat(path, follow_symlinks=False)
        if (
            _is_link_like(path, after)
            or not stat.S_ISDIR(after.st_mode)
            or not _same_directory_snapshot(before, after)
        ):
            raise ScanFailure("unsafe_input")
    except ScanFailure:
        raise
    except OSError as exc:
        raise ScanFailure("io") from exc
    bounded.sort(key=lambda entry: _filesystem_name_sort_key(entry.name))
    return bounded


def _discover_levels(root: Path, budget: ScanBudget) -> tuple[LevelInputs, ...]:
    gamedata = root / "GAMEDATA"
    gamedata_stat = _require_directory(gamedata, "missing_level_input")
    discovered: list[LevelInputs] = []
    target_names = frozenset(
        ("DATA.POP", "DATA.HOG", *CONTAINER_FILENAMES.values())
    )

    for entry in _bounded_directory_entries(gamedata, budget, gamedata_stat):
        if not stat.S_ISDIR(entry.discovered_stat.st_mode):
            continue

        child_index: dict[str, _FilesystemEntry] = {}
        for child in _bounded_directory_entries(
            entry.path, budget, entry.discovered_stat
        ):
            normalized = child.name.upper()
            if normalized in child_index:
                raise ScanFailure("unsafe_input")
            if normalized in target_names and not stat.S_ISREG(
                child.discovered_stat.st_mode
            ):
                raise ScanFailure("unsafe_input")
            child_index[normalized] = child

        pop_entry = child_index.get("DATA.POP")
        if pop_entry is None:
            continue
        if len(discovered) >= budget.limits.maximum_levels:
            raise ScanFailure("filesystem_limit")
        discovered.append(
            LevelInputs(
                pop=_StableFileInput(
                    pop_entry.path, _file_snapshot(pop_entry.discovered_stat)
                ),
                common=_stable_file_input(child_index.get("DATA.HOG")),
                primary=_stable_file_input(
                    child_index.get(CONTAINER_FILENAMES["primary"])
                ),
                map=_stable_file_input(
                    child_index.get(CONTAINER_FILENAMES["map"])
                ),
            )
        )

    if not discovered:
        raise ScanFailure("missing_level_input")
    return tuple(discovered)


def _bounded_hog(
    stream: BinaryIO,
    span: Span,
    maximum_span_bytes: int,
    budget: ScanBudget,
    malformed_category: str,
    *,
    require_exact: bool = False,
) -> HogDirectory:
    if span.size < 20 or span.size > maximum_span_bytes:
        raise ScanFailure(malformed_category)
    try:
        header = read_at(stream, span.offset, 20)
        _tag, count, _offsets, _names, data_offset = struct.unpack("<5I", header)
        if data_offset > budget.limits.maximum_hog_directory_bytes:
            raise ScanFailure("filesystem_limit")
        budget.add_hog_entries(count)
        budget.add_traversed_span(span.size)
        directory = parse_hog_span(stream, span.offset, span.size)
        if require_exact and directory.padding_size:
            raise ScanFailure(malformed_category)
        return directory
    except ScanFailure:
        raise
    except (OSError, ValueError, struct.error) as exc:
        raise ScanFailure(malformed_category) from exc


def _normalize_archive_name(value: str, maximum_name_bytes: int) -> str:
    try:
        return base_topology._normalize_game_path(value, maximum_name_bytes)
    except base_topology.ScanFailure as exc:
        raise ScanFailure("archive_name_invalid") from exc


def _directory_index(
    directory: HogDirectory, maximum_name_bytes: int
) -> dict[str, Span]:
    index: dict[str, Span] = {}
    for entry in directory.entries:
        normalized = _normalize_archive_name(entry.name, maximum_name_bytes)
        if normalized in index:
            raise ScanFailure("normalized_collision")
        index[normalized] = entry
    return index


def _parse_pop_cell_names(
    stream: BinaryIO, file_size: int, limits: ScanLimits
) -> tuple[str, ...]:
    try:
        return base_topology._parse_pop_cell_names(stream, file_size, limits)
    except base_topology.ScanFailure as exc:
        category = exc.category if exc.category in ERROR_CATEGORIES else "pop_malformed"
        raise ScanFailure(category) from exc


def _manifest_cell_entries(
    names: Sequence[str], directory: HogDirectory, maximum_name_bytes: int
) -> tuple[tuple[str, Span], ...]:
    try:
        return base_topology._manifest_cell_entries(
            names, directory, maximum_name_bytes
        )
    except base_topology.ScanFailure as exc:
        category = (
            exc.category if exc.category in ERROR_CATEGORIES else "cell_reference_invalid"
        )
        raise ScanFailure(category) from exc


def _read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _usage_family(record: bytes, active_count: int) -> bool:
    usage = tuple(_read_u32(record, 72 + slot * 4) for slot in range(3))
    if active_count == 1:
        return usage[0] in (2, 13)
    if active_count == 2:
        return usage[0] == 2 and usage[1] in (11, 10, 2)
    return usage == (2, 12, 14)


def _parse_vum_catalog(data: bytes, budget: ScanBudget) -> MaterialCatalog:
    limits = budget.limits
    try:
        layout = vum_contract.validate_vum_payload_layout(
            data,
            maximum_input_bytes=limits.maximum_vum_bytes,
            maximum_items=limits.maximum_vum_catalog_items,
        )
    except vum_contract.VumContractError as exc:
        category = (
            "vum_limit_exceeded"
            if exc.code == "limit_exceeded"
            else "vum_malformed"
        )
        raise ScanFailure(category) from exc

    name_count = layout.name_count
    material_count = layout.material_count
    names_end = layout.names_end
    if (
        name_count > limits.maximum_vum_names
        or material_count > limits.maximum_material_records
    ):
        raise ScanFailure("vum_limit_exceeded")
    metadata_record_count = layout.metadata_record_count
    if metadata_record_count > limits.maximum_vum_metadata_records:
        raise ScanFailure("vum_limit_exceeded")

    name_region = data[_VUM_NAME_REGION_OFFSET:names_end]
    if not name_region or name_region[-1] != 0:
        raise ScanFailure("vum_malformed")
    names: list[str] = []
    start = 0
    for offset, value in enumerate(name_region):
        if value == 0:
            if offset != start:
                names.append(name_region[start:offset].decode("ascii"))
            start = offset + 1
            continue
        if value < 0x20 or value > 0x7E:
            raise ScanFailure("vum_malformed")
        if offset - start + 1 > limits.maximum_name_bytes:
            raise ScanFailure("vum_limit_exceeded")
    if len(names) != name_count:
        raise ScanFailure("vum_malformed")

    materials: list[tuple[int, ...]] = []
    reference_count = 0
    for index in range(material_count):
        offset = names_end + index * _VUM_MATERIAL_RECORD_BYTES
        record = data[offset : offset + _VUM_MATERIAL_RECORD_BYTES]
        if record[:4] != b"MTRL" or any(record[16:56]):
            raise ScanFailure("vum_malformed")
        first_scalar, second_scalar = struct.unpack_from("<ff", record, 8)
        if not math.isfinite(first_scalar) or not math.isfinite(second_scalar):
            raise ScanFailure("vum_malformed")
        if (
            _read_u32(record, 68) != _VUM_INACTIVE_WORD
            or _read_u32(record, 84) != _VUM_INACTIVE_WORD
        ):
            raise ScanFailure("vum_malformed")
        active_count = _read_u32(record, 88)
        if active_count not in (1, 2, 3) or not _usage_family(record, active_count):
            raise ScanFailure("vum_malformed")

        references: list[int] = []
        for slot in range(3):
            reference = _read_u32(record, 56 + slot * 4)
            usage = _read_u32(record, 72 + slot * 4)
            if slot < active_count:
                if reference >= name_count:
                    raise ScanFailure("vum_malformed")
                references.append(reference)
            elif reference != _VUM_INACTIVE_WORD or usage != _VUM_INACTIVE_WORD:
                raise ScanFailure("vum_malformed")
        reference_count += active_count
        if reference_count > limits.maximum_dense_name_references:
            raise ScanFailure("vum_limit_exceeded")
        materials.append(tuple(references))

    decoded_items = (
        1
        + len(names)
        + len(materials)
        + reference_count
        + metadata_record_count
    )
    if decoded_items > limits.maximum_vum_catalog_items:
        raise ScanFailure("vum_limit_exceeded")

    budget.add_catalog(
        len(names), len(materials), reference_count, metadata_record_count
    )
    return MaterialCatalog(tuple(names), tuple(materials))


def _read_vum_catalog(
    stream: BinaryIO, entry: Span, budget: ScanBudget
) -> MaterialCatalog:
    if entry.size > budget.limits.maximum_vum_bytes:
        raise ScanFailure("vum_limit_exceeded")
    budget.add_traversed_span(entry.size)
    try:
        return _parse_vum_catalog(read_at(stream, entry.offset, entry.size), budget)
    except ScanFailure:
        raise
    except (OSError, ValueError, struct.error, UnicodeDecodeError) as exc:
        raise ScanFailure("vum_malformed") from exc


def _scan_texture_container(
    source: _StableFileInput, container_class: str, budget: ScanBudget
) -> set[tuple[str, str]]:
    with _open_regular(source, "missing_texture_container") as (stream, file_size):
        directory = _bounded_hog(
            stream,
            Span("", 0, file_size),
            budget.limits.maximum_texture_container_bytes,
            budget,
            "texture_container_malformed",
            require_exact=True,
        )
        index = _directory_index(directory, budget.limits.maximum_name_bytes)
        names = {normalized for normalized in index if normalized.endswith(".TDX")}
        budget.add_tdx(len(names))
        return {(container_class, normalized) for normalized in names}


def _classify_candidate(
    value: str,
    lookup: dict[str, frozenset[str]],
    maximum_name_bytes: int,
) -> tuple[str, tuple[tuple[str, str], ...]]:
    try:
        normalized = base_topology._normalize_game_path(value, maximum_name_bytes)
    except base_topology.ScanFailure:
        return "invalid_member_candidate", ()
    if not normalized.endswith(".TDX"):
        return "non_tdx_suffix", ()
    classes = lookup.get(normalized, frozenset())
    if not classes:
        return "unmatched", ()
    locators = tuple((container_class, normalized) for container_class in sorted(classes))
    if len(classes) > 1:
        return "ambiguous_cross_class", locators
    container_class = next(iter(classes))
    return f"unique_{container_class}", locators


def _scan_level(level: LevelInputs, budget: ScanBudget) -> LevelMeasurement:
    assert level.common is not None and level.primary is not None and level.map is not None

    with _open_regular(level.pop, "missing_level_input") as (stream, file_size):
        cell_names = _parse_pop_cell_names(stream, file_size, budget.limits)

    locators: set[tuple[str, str]] = set()
    for container_class in CONTAINER_CLASSES:
        path = getattr(level, container_class)
        assert path is not None
        locators.update(_scan_texture_container(path, container_class, budget))
    lookup_mutable: dict[str, set[str]] = {}
    for container_class, normalized in locators:
        lookup_mutable.setdefault(normalized, set()).add(container_class)
    lookup = {
        normalized: frozenset(classes)
        for normalized, classes in lookup_mutable.items()
    }

    measured = LevelMeasurement()
    measured.totals["texture_containers"] = len(CONTAINER_CLASSES)
    measured.totals["tdx_locator_occurrences"] = len(locators)
    measured.observe("tdx_locators_per_level", len(locators))
    reached_unique: set[tuple[str, str]] = set()
    reached_ambiguous: set[tuple[str, str]] = set()

    with _open_regular(level.common, "missing_level_input") as (stream, file_size):
        common = _bounded_hog(
            stream,
            Span("", 0, file_size),
            budget.limits.maximum_data_hog_bytes,
            budget,
            "common_hog_malformed",
        )
        cells = _manifest_cell_entries(
            cell_names, common, budget.limits.maximum_name_bytes
        )
        measured.totals["manifest_cell_occurrences"] = len(cells)

        for _normalized_cell, cell_entry in cells:
            cell = _bounded_hog(
                stream,
                cell_entry,
                budget.limits.maximum_cell_hog_bytes,
                budget,
                "cell_hog_malformed",
            )
            cell_index = _directory_index(cell, budget.limits.maximum_name_bytes)
            vum_entries = [
                entry for normalized, entry in cell_index.items() if normalized.endswith(".VUM")
            ]
            if not vum_entries:
                raise ScanFailure("vum_missing")
            if len(vum_entries) != 1:
                raise ScanFailure("vum_duplicate")
            catalog = _read_vum_catalog(stream, vum_entries[0], budget)
            reference_count = sum(len(material) for material in catalog.materials)
            measured.totals["vum_catalogs"] += 1
            measured.totals["vum_name_occurrences"] += len(catalog.names)
            measured.totals["material_records"] += len(catalog.materials)
            measured.totals["dense_name_references"] += reference_count
            measured.observe("vum_names_per_catalog", len(catalog.names))
            measured.observe("material_records_per_catalog", len(catalog.materials))
            measured.observe("dense_name_references_per_catalog", reference_count)

            classified: list[tuple[str, tuple[tuple[str, str], ...]]] = []
            for name in catalog.names:
                status, candidates = _classify_candidate(
                    name, lookup, budget.limits.maximum_name_bytes
                )
                measured.name_occurrences[status] += 1
                measured.observe("candidate_locators_per_name", len(candidates))
                if status.startswith("unique_"):
                    reached_unique.update(candidates)
                elif status == "ambiguous_cross_class":
                    reached_ambiguous.update(candidates)
                classified.append((status, candidates))

            for material in catalog.materials:
                statuses = [classified[index][0] for index in material]
                for status in statuses:
                    measured.dense_name_references[status] += 1
                unique = [status.startswith("unique_") for status in statuses]
                measured.material_record_flags["all_references_unique"] += int(
                    all(unique)
                )
                measured.material_record_flags["any_unique"] += int(any(unique))
                measured.material_record_flags["any_unmatched"] += int(
                    "unmatched" in statuses
                )
                measured.material_record_flags["any_ambiguous"] += int(
                    "ambiguous_cross_class" in statuses
                )
                measured.material_record_flags["any_ineligible"] += int(
                    any(
                        status in ("invalid_member_candidate", "non_tdx_suffix")
                        for status in statuses
                    )
                )

    reached_only_ambiguously = reached_ambiguous - reached_unique
    reached = reached_unique | reached_ambiguous
    measured.locator_coverage["reached_by_unique_candidate"] = len(reached_unique)
    measured.locator_coverage["reached_only_ambiguously"] = len(
        reached_only_ambiguously
    )
    measured.locator_coverage["unreached"] = len(locators - reached)
    measured.totals["levels_scanned"] = 1
    return measured


def scan_disc(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    _validate_limits(limits)
    _require_directory(root, "unsafe_input")
    budget = ScanBudget(limits)
    aggregate = Aggregate()
    levels = _discover_levels(root.absolute(), budget)
    aggregate.totals["levels_discovered"] = len(levels)

    for level in levels:
        missing = False
        if level.common is None:
            aggregate.record_failure(ScanFailure("missing_level_input"))
            missing = True
        for container_class in CONTAINER_CLASSES:
            if getattr(level, container_class) is None:
                aggregate.record_failure(ScanFailure("missing_texture_container"))
                missing = True
        if missing:
            continue
        try:
            measured = _scan_level(level, budget)
            aggregate.merge_level(measured)
        except ScanFailure as failure:
            aggregate.record_failure(failure)
    return aggregate.document()


def failure_document(category: str) -> dict[str, object]:
    aggregate = Aggregate()
    aggregate.record_failure(ScanFailure(category))
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
