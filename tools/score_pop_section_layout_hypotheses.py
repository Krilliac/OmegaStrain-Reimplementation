#!/usr/bin/env python3
"""Score bounded POP marker-span layout hypotheses without decoding sections.

The scorer starts only at the GOB offset produced by the proven TER-prefix
parser.  Later aligned literal markers, candidate count fields, and fixed
record strides remain structural hypotheses.  Output is anonymous and
aggregate-only.
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

if __package__:
    from . import scan_pop_post_terrain as envelope_scanner
else:  # Direct execution adds tools/ rather than the repository root.
    import scan_pop_post_terrain as envelope_scanner


SCHEMA_VERSION = 1
SCOPE = (
    "anonymous aggregate candidate-marker/count/extent hypotheses only; no paths, names, "
    "hashes, raw words, payload bytes, per-file fingerprints, or section semantics"
)
INTERPRETATION = (
    "an exact extent match means only that one bounded opaque word and one fixed stride can "
    "arithmetically fill a candidate marker span; markers, counts, records, and boundaries "
    "remain unconfirmed hypotheses"
)

_UINT64_MAX = (1 << 64) - 1
_REPARSE_POINT_ATTRIBUTE = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
_ENTRY_METADATA_OVERHEAD = 64


@dataclass(frozen=True)
class ScanLimits:
    maximum_files: int = 4096
    maximum_walk_entries: int = 1 << 20
    maximum_directory_depth: int = 128
    maximum_metadata_bytes: int = 64 * 1024 * 1024
    maximum_file_bytes: int = 64 * 1024 * 1024
    maximum_total_file_bytes: int = 4 * 1024 * 1024 * 1024
    maximum_total_read_bytes: int = 5 * 1024 * 1024 * 1024
    maximum_terrain_records: int = 1 << 20
    maximum_total_terrain_records: int = 4 * (1 << 20)
    maximum_name_bytes: int = 4096
    maximum_marker_hits_per_file: int = 256
    maximum_candidate_spans: int = 1 << 20
    maximum_count_field_slots: int = 8
    maximum_field_probes: int = 8 * (1 << 20)
    maximum_candidate_records: int = 1 << 20
    maximum_fixed_record_stride: int = 256
    maximum_output_hypotheses: int = 4096
    maximum_output_bytes: int = 1024 * 1024


class ScanFailure(ValueError):
    def __init__(self, category: str) -> None:
        super().__init__(category)
        self.category = category


def _validate_limits(limits: ScanLimits) -> None:
    values = (
        limits.maximum_files,
        limits.maximum_walk_entries,
        limits.maximum_directory_depth,
        limits.maximum_metadata_bytes,
        limits.maximum_file_bytes,
        limits.maximum_total_file_bytes,
        limits.maximum_total_read_bytes,
        limits.maximum_terrain_records,
        limits.maximum_total_terrain_records,
        limits.maximum_name_bytes,
        limits.maximum_marker_hits_per_file,
        limits.maximum_candidate_spans,
        limits.maximum_count_field_slots,
        limits.maximum_field_probes,
        limits.maximum_candidate_records,
        limits.maximum_fixed_record_stride,
        limits.maximum_output_hypotheses,
        limits.maximum_output_bytes,
    )
    if any(type(value) is not int or value < 0 or value > _UINT64_MAX for value in values):
        raise ScanFailure("limit_exceeded")
    if (
        limits.maximum_files == 0
        or limits.maximum_walk_entries == 0
        or limits.maximum_metadata_bytes == 0
        or limits.maximum_file_bytes < 16
        or limits.maximum_total_file_bytes < 16
        or limits.maximum_total_read_bytes < 16
        or limits.maximum_name_bytes == 0
        or limits.maximum_total_terrain_records == 0
        or limits.maximum_marker_hits_per_file == 0
        or limits.maximum_candidate_spans == 0
        or not 1 <= limits.maximum_count_field_slots <= 64
        or limits.maximum_field_probes == 0
        or limits.maximum_candidate_records == 0
        or not 4 <= limits.maximum_fixed_record_stride <= 4096
        or limits.maximum_output_hypotheses == 0
        or limits.maximum_output_bytes < 512
    ):
        raise ScanFailure("limit_exceeded")


def _checked_add(current: int, amount: int, maximum: int) -> int:
    if amount < 0 or current > maximum - amount:
        raise ScanFailure("limit_exceeded")
    return current + amount


@dataclass
class _MetadataBudget:
    maximum: int
    used: int = 0

    def add(self, amount: int) -> None:
        self.used = _checked_add(self.used, amount, self.maximum)

    def add_name(self, name: str) -> None:
        self.add(len(os.fsencode(name)) + _ENTRY_METADATA_OVERHEAD)


@dataclass
class _ReadBudget:
    maximum: int
    used: int = 0

    def ensure(self, amount: int) -> None:
        if amount < 0 or self.used > self.maximum - amount:
            raise ScanFailure("limit_exceeded")

    def consume(self, amount: int) -> None:
        self.used = _checked_add(self.used, amount, self.maximum)


class _BudgetedStream:
    def __init__(self, stream: BinaryIO, budget: _ReadBudget) -> None:
        self._stream = stream
        self._budget = budget

    def read(self, size: int = -1) -> bytes:
        if type(size) is not int or size < 0:
            raise ScanFailure("limit_exceeded")
        self._budget.ensure(size)
        data = self._stream.read(size)
        if len(data) > size:
            raise ScanFailure("unsafe_input")
        self._budget.consume(len(data))
        return data

    def seek(self, offset: int, whence: int = os.SEEK_SET) -> int:
        return self._stream.seek(offset, whence)

    def tell(self) -> int:
        return self._stream.tell()


@dataclass(frozen=True)
class _CandidatePath:
    path: Path
    discovered_stat: os.stat_result


def _stat_is_reparse(value: os.stat_result) -> bool:
    attributes = getattr(value, "st_file_attributes", 0)
    return bool(attributes & _REPARSE_POINT_ATTRIBUTE)


def _path_is_link_like(path: Path, value: os.stat_result) -> bool:
    is_junction = getattr(path, "is_junction", lambda: False)()
    return path.is_symlink() or is_junction or _stat_is_reparse(value)


def _same_file(left: os.stat_result, right: os.stat_result) -> bool:
    return (left.st_dev, left.st_ino) == (right.st_dev, right.st_ino)


def _iter_pop_candidates(
    root: Path,
    root_stat: os.stat_result,
    limits: ScanLimits,
    metadata: _MetadataBudget,
) -> Iterator[_CandidatePath]:
    walk_entries = 0

    def walk(
        directory: Path, discovered_stat: os.stat_result, depth: int
    ) -> Iterator[_CandidatePath]:
        nonlocal walk_entries
        if depth > limits.maximum_directory_depth:
            raise ScanFailure("limit_exceeded")
        prewalk_stat = os.stat(directory, follow_symlinks=False)
        if (
            not stat.S_ISDIR(prewalk_stat.st_mode)
            or _path_is_link_like(directory, prewalk_stat)
            or not _same_file(prewalk_stat, discovered_stat)
        ):
            raise ScanFailure("unsafe_input")
        with os.scandir(directory) as entries:
            for entry in entries:
                walk_entries = _checked_add(
                    walk_entries, 1, limits.maximum_walk_entries
                )
                metadata.add_name(entry.name)
                path = Path(entry.path)
                # On Windows, DirEntry.stat() can report zero device/inode
                # identifiers.  A path lstat supplies the stable identity used
                # by the later open-race check.
                entry_stat = os.stat(path, follow_symlinks=False)
                if entry.is_symlink() or _stat_is_reparse(entry_stat):
                    raise ScanFailure("unsafe_input")
                if stat.S_ISDIR(entry_stat.st_mode):
                    yield from walk(path, entry_stat, depth + 1)
                elif stat.S_ISREG(entry_stat.st_mode):
                    if path.suffix.casefold() == ".pop":
                        yield _CandidatePath(path, entry_stat)
                else:
                    raise ScanFailure("unsafe_input")
        final_stat = os.stat(directory, follow_symlinks=False)
        if (
            not stat.S_ISDIR(final_stat.st_mode)
            or _path_is_link_like(directory, final_stat)
            or not _same_file(final_stat, prewalk_stat)
            or final_stat.st_mtime_ns != prewalk_stat.st_mtime_ns
        ):
            raise ScanFailure("unsafe_input")

    yield from walk(root, root_stat, 0)


def _align_up_4(value: int) -> int:
    if value < 0 or value > _UINT64_MAX - 3:
        raise ScanFailure("limit_exceeded")
    return (value + 3) & ~3


def _read_u32(stream: _BudgetedStream, offset: int) -> int:
    if offset < 0:
        raise ScanFailure("malformed")
    stream.seek(offset)
    data = stream.read(4)
    if len(data) != 4:
        raise ScanFailure("truncated")
    return struct.unpack("<I", data)[0]


class _Aggregate:
    def __init__(self) -> None:
        self.files = 0
        self.terrain_records = 0
        self.candidate_spans = 0
        self.field_probes = 0
        self.bounded_nonzero_count_probes = 0
        self.bounded_zero_count_probes = 0
        self._tested: collections.Counter[tuple[str, int]] = collections.Counter()
        self._zero_tested: collections.Counter[tuple[str, int]] = collections.Counter()
        self._zero_empty_extent: collections.Counter[tuple[str, int]] = collections.Counter()
        self._exact: collections.Counter[tuple[str, int, int]] = collections.Counter()

    def add(
        self,
        candidate: envelope_scanner.PopEnvelope,
        stream: _BudgetedStream,
        limits: ScanLimits,
        metadata: _MetadataBudget,
    ) -> None:
        markers = candidate.markers
        if (
            not markers
            or markers[0] != (candidate.gob_offset, "GOB:")
            or candidate.gob_offset % 4 != 0
        ):
            raise ScanFailure("malformed")
        self.files = _checked_add(self.files, 1, _UINT64_MAX)
        self.terrain_records = _checked_add(
            self.terrain_records,
            candidate.terrain_records,
            limits.maximum_total_terrain_records,
        )

        previous_offset = -1
        for ordinal, (marker_offset, literal_marker) in enumerate(markers):
            if (
                marker_offset <= previous_offset
                or marker_offset < candidate.gob_offset
                or marker_offset % 4 != 0
                or literal_marker.encode("ascii")
                not in envelope_scanner.PUBLISHED_LITERAL_TAGS
            ):
                raise ScanFailure("malformed")
            previous_offset = marker_offset
            boundary = (
                markers[ordinal + 1][0]
                if ordinal + 1 < len(markers)
                else candidate.file_bytes
            )
            if boundary < marker_offset or boundary > candidate.file_bytes:
                raise ScanFailure("malformed")

            self.candidate_spans = _checked_add(
                self.candidate_spans, 1, limits.maximum_candidate_spans
            )
            metadata.add(_ENTRY_METADATA_OVERHEAD + len(literal_marker))
            first_field = _align_up_4(marker_offset + len(literal_marker))
            for slot in range(limits.maximum_count_field_slots):
                self.field_probes = _checked_add(
                    self.field_probes, 1, limits.maximum_field_probes
                )
                field_offset = first_field + slot * 4
                field_end = field_offset + 4
                if field_end > boundary:
                    continue
                candidate_count = _read_u32(stream, field_offset)
                if candidate_count > limits.maximum_candidate_records:
                    continue

                field_delta = field_offset - marker_offset
                tested_key = (literal_marker, field_delta)
                opaque_extent = boundary - field_end
                if candidate_count == 0:
                    if tested_key not in self._zero_tested:
                        metadata.add(2 * _ENTRY_METADATA_OVERHEAD + len(literal_marker))
                    self._zero_tested[tested_key] += 1
                    self.bounded_zero_count_probes = _checked_add(
                        self.bounded_zero_count_probes, 1, _UINT64_MAX
                    )
                    if opaque_extent == 0:
                        self._zero_empty_extent[tested_key] += 1
                    continue
                if tested_key not in self._tested:
                    metadata.add(2 * _ENTRY_METADATA_OVERHEAD + len(literal_marker))
                self._tested[tested_key] += 1
                self.bounded_nonzero_count_probes = _checked_add(
                    self.bounded_nonzero_count_probes, 1, _UINT64_MAX
                )
                if opaque_extent % candidate_count != 0:
                    continue
                fixed_stride = opaque_extent // candidate_count
                if (
                    fixed_stride < 4
                    or fixed_stride > limits.maximum_fixed_record_stride
                    or fixed_stride % 4 != 0
                ):
                    continue
                exact_key = (literal_marker, field_delta, fixed_stride)
                if exact_key not in self._exact:
                    if len(self._exact) >= limits.maximum_output_hypotheses:
                        raise ScanFailure("limit_exceeded")
                    metadata.add(3 * _ENTRY_METADATA_OVERHEAD + len(literal_marker))
                self._exact[exact_key] += 1

    def as_dict(self, limits: ScanLimits) -> dict[str, object]:
        if len(self._exact) > limits.maximum_output_hypotheses:
            raise ScanFailure("limit_exceeded")
        hypotheses: list[dict[str, object]] = []
        for (literal_marker, field_delta, fixed_stride), exact_matches in sorted(
            self._exact.items()
        ):
            tested = self._tested[(literal_marker, field_delta)]
            if tested < exact_matches:
                raise ScanFailure("malformed")
            zero_tested = self._zero_tested[(literal_marker, field_delta)]
            zero_empty_extent = self._zero_empty_extent[(literal_marker, field_delta)]
            if zero_tested < zero_empty_extent:
                raise ScanFailure("malformed")
            hypotheses.append({
                "literal_marker": literal_marker,
                "candidate_count_field_delta_bytes": field_delta,
                "candidate_header_extent_bytes": field_delta + 4,
                "candidate_fixed_record_stride_bytes": fixed_stride,
                "bounded_nonzero_count_occurrences_tested": tested,
                "candidate_extent_exact_matches": exact_matches,
                "candidate_extent_mismatches": tested - exact_matches,
                "zero_count_occurrences_tested": zero_tested,
                "zero_count_empty_extent_matches": zero_empty_extent,
                "bounded_count_occurrences_tested": tested + zero_tested,
                "candidate_extent_exact_matches_including_zero": (
                    exact_matches + zero_empty_extent
                ),
                "candidate_extent_mismatches_including_zero": (
                    tested - exact_matches + zero_tested - zero_empty_extent
                ),
            })
        return {
            "schema_version": SCHEMA_VERSION,
            "scope": SCOPE,
            "interpretation": INTERPRETATION,
            "complete": True,
            "errors": {},
            "totals": {
                "files": self.files,
                "candidate_marker_spans": self.candidate_spans,
                "count_field_probes": self.field_probes,
                "bounded_nonzero_count_probes": self.bounded_nonzero_count_probes,
                "bounded_zero_count_probes": self.bounded_zero_count_probes,
                "exact_extent_hypothesis_hits": sum(self._exact.values()),
                "hypothesis_candidates_emitted": len(hypotheses),
            },
            "candidate_layout_hypotheses": hypotheses,
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


def _result_fits_output_budget(result: dict[str, object], maximum_bytes: int) -> bool:
    encoded = json.dumps(result, indent=2, sort_keys=True).encode("utf-8")
    return len(encoded) <= maximum_bytes


def scan_root(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    try:
        _validate_limits(limits)
    except ScanFailure as exc:
        return _failure_result(0, 0, collections.Counter({exc.category: 1}))

    errors: collections.Counter[str] = collections.Counter()
    files_discovered = 0
    files_valid = 0
    total_file_bytes = 0
    metadata = _MetadataBudget(limits.maximum_metadata_bytes)
    reads = _ReadBudget(limits.maximum_total_read_bytes)
    aggregate = _Aggregate()
    try:
        root_stat = os.stat(root, follow_symlinks=False)
        metadata.add_name(root.name)
        if not stat.S_ISDIR(root_stat.st_mode) or _path_is_link_like(root, root_stat):
            raise ScanFailure("unsafe_input")

        for candidate in _iter_pop_candidates(root, root_stat, limits, metadata):
            files_discovered = _checked_add(
                files_discovered, 1, limits.maximum_files
            )
            try:
                preopen_stat = os.stat(candidate.path, follow_symlinks=False)
                if (
                    not stat.S_ISREG(preopen_stat.st_mode)
                    or _path_is_link_like(candidate.path, preopen_stat)
                    or not _same_file(preopen_stat, candidate.discovered_stat)
                ):
                    raise ScanFailure("unsafe_input")
                with candidate.path.open("rb") as raw_stream:
                    opened_stat = os.fstat(raw_stream.fileno())
                    if (
                        not stat.S_ISREG(opened_stat.st_mode)
                        or _stat_is_reparse(opened_stat)
                        or not _same_file(opened_stat, preopen_stat)
                    ):
                        raise ScanFailure("unsafe_input")
                    file_bytes = opened_stat.st_size
                    if file_bytes < 0 or file_bytes > limits.maximum_file_bytes:
                        raise ScanFailure("limit_exceeded")
                    total_file_bytes = _checked_add(
                        total_file_bytes, file_bytes, limits.maximum_total_file_bytes
                    )
                    stream = _BudgetedStream(raw_stream, reads)
                    candidate_envelope = envelope_scanner.scan_pop_stream(
                        stream,
                        file_bytes,
                        envelope_scanner.ScanLimits(
                            maximum_file_bytes=limits.maximum_file_bytes,
                            maximum_terrain_records=limits.maximum_terrain_records,
                            maximum_name_bytes=limits.maximum_name_bytes,
                            maximum_marker_hits_per_file=(
                                limits.maximum_marker_hits_per_file
                            ),
                        ),
                    )
                    aggregate.add(candidate_envelope, stream, limits, metadata)
                    final_stat = os.fstat(raw_stream.fileno())
                    if (
                        not _same_file(final_stat, opened_stat)
                        or final_stat.st_size != opened_stat.st_size
                        or final_stat.st_mtime_ns != opened_stat.st_mtime_ns
                    ):
                        raise ScanFailure("unsafe_input")
                files_valid += 1
            except envelope_scanner.ScanFailure as exc:
                errors[exc.category] += 1
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
        return _failure_result(files_discovered, files_valid, errors)
    try:
        result = aggregate.as_dict(limits)
    except ScanFailure as exc:
        return _failure_result(
            files_discovered, files_valid, collections.Counter({exc.category: 1})
        )
    if not _result_fits_output_budget(result, limits.maximum_output_bytes):
        return _failure_result(
            files_discovered, files_valid, collections.Counter({"limit_exceeded": 1})
        )
    return result


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
