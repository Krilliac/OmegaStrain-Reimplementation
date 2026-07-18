#!/usr/bin/env python3
"""Profile five bounded POP candidate record shapes without assigning semantics.

The proven TER-to-GOB validator and ordered aligned-marker inventory are reused
unchanged.  This tool accepts only five preselected marker-relative arithmetic
formulas.  Exact arithmetic fit does not make a marker, count, record, column,
or numeric field type authoritative.
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
    "anonymous aggregate classifications for five fixed POP arithmetic candidate "
    "record shapes only; no paths, names, hashes, raw words, values, extrema, payload "
    "bytes, per-file records, fingerprints, or section semantics"
)
INTERPRETATION = (
    "zero/nonzero, IEEE-754 finite/nonfinite, small-unsigned, and distinct-cardinality "
    "labels classify opaque 32-bit bit patterns only; they do not assign a float, "
    "integer, coordinate, identifier, or any other numeric field type"
)

# A single fixed power-of-two bucket is intentionally used instead of a range or
# extrema.  It is a neutral structural counter and carries no field-type meaning.
SMALL_UNSIGNED_EXCLUSIVE_THRESHOLD = 4096

_UINT64_MAX = (1 << 64) - 1
_REPARSE_POINT_ATTRIBUTE = getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
_ENTRY_METADATA_OVERHEAD = 64
_FORMULA_METADATA_OVERHEAD = 128
_DISTINCT_METADATA_OVERHEAD = 40


@dataclass(frozen=True)
class CandidateFormula:
    literal_tag: str
    count_word_delta_bytes: int
    fixed_record_stride_bytes: int

    @property
    def header_extent_bytes(self) -> int:
        return self.count_word_delta_bytes + 4

    @property
    def column_count(self) -> int:
        return self.fixed_record_stride_bytes // 4


# These are the only formulas this tool is permitted to validate or profile.
CANDIDATE_FORMULAS: tuple[CandidateFormula, ...] = (
    CandidateFormula("INL:", 4, 36),
    CandidateFormula("PNT:", 4, 88),
    CandidateFormula("DIR:", 4, 44),
    CandidateFormula("ENV:", 4, 76),
    CandidateFormula("INV:", 4, 84),
)
_FORMULA_BY_TAG = {formula.literal_tag: formula for formula in CANDIDATE_FORMULAS}
_TOTAL_OUTPUT_COLUMNS = sum(formula.column_count for formula in CANDIDATE_FORMULAS)


@dataclass(frozen=True)
class ScanLimits:
    maximum_files: int = 4096
    maximum_walk_entries: int = 1 << 20
    maximum_directory_depth: int = 128
    maximum_metadata_bytes: int = 64 * 1024 * 1024
    maximum_file_bytes: int = 64 * 1024 * 1024
    maximum_total_file_bytes: int = 4 * 1024 * 1024 * 1024
    maximum_total_read_bytes: int = 12 * 1024 * 1024 * 1024
    maximum_terrain_records: int = 1 << 20
    maximum_total_terrain_records: int = 4 * (1 << 20)
    maximum_name_bytes: int = 4096
    maximum_marker_hits_per_file: int = 256
    maximum_formula_occurrences: int = 1 << 20
    maximum_records_per_formula: int = 1 << 20
    maximum_total_records: int = 16 * (1 << 20)
    maximum_columns_per_formula: int = 64
    maximum_total_output_columns: int = 256
    maximum_total_column_observations: int = 256 * (1 << 20)
    maximum_distinct_bit_patterns_per_column: int = 4096
    maximum_total_distinct_bit_patterns: int = 1 << 20
    maximum_profile_read_chunk_bytes: int = 64 * 1024
    maximum_output_bytes: int = 1024 * 1024


class ScanFailure(ValueError):
    def __init__(self, category: str) -> None:
        super().__init__(category)
        self.category = category


def _checked_add(current: int, amount: int, maximum: int) -> int:
    if type(current) is not int or type(amount) is not int or type(maximum) is not int:
        raise ScanFailure("limit_exceeded")
    if amount < 0 or maximum < 0 or current < 0 or current > maximum - amount:
        raise ScanFailure("limit_exceeded")
    return current + amount


def _checked_multiply(left: int, right: int, maximum: int) -> int:
    if type(left) is not int or type(right) is not int or type(maximum) is not int:
        raise ScanFailure("limit_exceeded")
    if left < 0 or right < 0 or maximum < 0:
        raise ScanFailure("limit_exceeded")
    if left and right > maximum // left:
        raise ScanFailure("limit_exceeded")
    return left * right


def _validate_limits(limits: ScanLimits) -> None:
    values = tuple(vars(limits).values())
    if any(type(value) is not int or value < 0 or value > _UINT64_MAX for value in values):
        raise ScanFailure("limit_exceeded")
    if (
        limits.maximum_files == 0
        or limits.maximum_walk_entries == 0
        or limits.maximum_metadata_bytes == 0
        or limits.maximum_file_bytes < 16
        or limits.maximum_total_file_bytes < 16
        or limits.maximum_total_read_bytes < 16
        or limits.maximum_total_terrain_records == 0
        or limits.maximum_name_bytes == 0
        or limits.maximum_marker_hits_per_file == 0
        or limits.maximum_formula_occurrences == 0
        or limits.maximum_records_per_formula == 0
        or limits.maximum_total_records == 0
        or limits.maximum_columns_per_formula == 0
        or limits.maximum_total_output_columns == 0
        or limits.maximum_total_column_observations == 0
        or limits.maximum_distinct_bit_patterns_per_column == 0
        or limits.maximum_total_distinct_bit_patterns == 0
        or limits.maximum_profile_read_chunk_bytes < 4
        or limits.maximum_output_bytes < 512
    ):
        raise ScanFailure("limit_exceeded")

    published = {
        tag.decode("ascii") for tag in envelope_scanner.PUBLISHED_LITERAL_TAGS
    }
    if len(_FORMULA_BY_TAG) != len(CANDIDATE_FORMULAS):
        raise ScanFailure("malformed")
    for formula in CANDIDATE_FORMULAS:
        if (
            formula.literal_tag not in published
            or len(formula.literal_tag) != 4
            or formula.count_word_delta_bytes != 4
            or formula.fixed_record_stride_bytes < 4
            or formula.fixed_record_stride_bytes % 4 != 0
            or formula.column_count > limits.maximum_columns_per_formula
            or formula.fixed_record_stride_bytes > limits.maximum_profile_read_chunk_bytes
        ):
            raise ScanFailure("limit_exceeded")
    if _TOTAL_OUTPUT_COLUMNS > limits.maximum_total_output_columns:
        raise ScanFailure("limit_exceeded")


@dataclass
class _MetadataBudget:
    maximum: int
    used: int = 0

    def add(self, amount: int) -> None:
        self.used = _checked_add(self.used, amount, self.maximum)

    def add_name(self, name: str) -> None:
        try:
            encoded_length = len(os.fsencode(name))
        except UnicodeError as exc:
            raise ScanFailure("unsafe_input") from exc
        self.add(encoded_length + _ENTRY_METADATA_OVERHEAD)


@dataclass
class _ReadBudget:
    maximum: int
    used: int = 0

    def ensure(self, amount: int) -> None:
        if type(amount) is not int or amount < 0 or self.used > self.maximum - amount:
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
        if not isinstance(data, bytes) or len(data) > size:
            raise ScanFailure("unsafe_input")
        self._budget.consume(len(data))
        return data

    def seek(self, offset: int, whence: int = os.SEEK_SET) -> int:
        if type(offset) is not int or type(whence) is not int:
            raise ScanFailure("unsafe_input")
        position = self._stream.seek(offset, whence)
        if type(position) is not int or position < 0:
            raise ScanFailure("unsafe_input")
        return position

    def tell(self) -> int:
        position = self._stream.tell()
        if type(position) is not int or position < 0:
            raise ScanFailure("unsafe_input")
        return position


def _read_exact(stream: _BudgetedStream, size: int) -> bytes:
    if type(size) is not int or size < 0:
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


def _read_u32(stream: _BudgetedStream, offset: int) -> int:
    if type(offset) is not int or offset < 0:
        raise ScanFailure("malformed")
    stream.seek(offset)
    return struct.unpack("<I", _read_exact(stream, 4))[0]


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
                entry_stat = os.stat(path, follow_symlinks=False)
                if _path_is_link_like(path, entry_stat):
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


@dataclass
class _RunBudget:
    limits: ScanLimits
    formula_occurrences: int = 0
    terrain_records: int = 0
    total_records: int = 0
    column_observations: int = 0
    distinct_bit_patterns: int = 0

    def add_formula(self) -> None:
        self.formula_occurrences = _checked_add(
            self.formula_occurrences, 1, self.limits.maximum_formula_occurrences
        )

    def add_terrain_records(self, count: int) -> None:
        self.terrain_records = _checked_add(
            self.terrain_records, count, self.limits.maximum_total_terrain_records
        )

    def add_records(self, count: int, column_count: int) -> None:
        if count > self.limits.maximum_records_per_formula:
            raise ScanFailure("limit_exceeded")
        self.total_records = _checked_add(
            self.total_records, count, self.limits.maximum_total_records
        )
        observations = _checked_multiply(
            count, column_count, self.limits.maximum_total_column_observations
        )
        self.column_observations = _checked_add(
            self.column_observations,
            observations,
            self.limits.maximum_total_column_observations,
        )

    def add_distinct_bit_pattern(self) -> None:
        self.distinct_bit_patterns = _checked_add(
            self.distinct_bit_patterns,
            1,
            self.limits.maximum_total_distinct_bit_patterns,
        )


@dataclass
class _ColumnAggregate:
    zero_bit_patterns: int = 0
    nonzero_bit_patterns: int = 0
    finite_ieee754_bit_patterns: int = 0
    nonfinite_ieee754_bit_patterns: int = 0
    small_unsigned_bit_patterns: int = 0
    distinct_bit_patterns: set[int] | None = None
    distinct_cardinality_capped: bool = False

    def observe(
        self,
        word: int,
        budget: _RunBudget,
        metadata: _MetadataBudget,
    ) -> None:
        if type(word) is not int or word < 0 or word > 0xFFFFFFFF:
            raise ScanFailure("malformed")
        if word == 0:
            self.zero_bit_patterns = _checked_add(
                self.zero_bit_patterns, 1, _UINT64_MAX
            )
        else:
            self.nonzero_bit_patterns = _checked_add(
                self.nonzero_bit_patterns, 1, _UINT64_MAX
            )

        if (word & 0x7F800000) == 0x7F800000:
            self.nonfinite_ieee754_bit_patterns = _checked_add(
                self.nonfinite_ieee754_bit_patterns, 1, _UINT64_MAX
            )
        else:
            self.finite_ieee754_bit_patterns = _checked_add(
                self.finite_ieee754_bit_patterns, 1, _UINT64_MAX
            )

        if word < SMALL_UNSIGNED_EXCLUSIVE_THRESHOLD:
            self.small_unsigned_bit_patterns = _checked_add(
                self.small_unsigned_bit_patterns, 1, _UINT64_MAX
            )

        if self.distinct_bit_patterns is None:
            self.distinct_bit_patterns = set()
        if word in self.distinct_bit_patterns:
            return
        if (
            len(self.distinct_bit_patterns)
            >= budget.limits.maximum_distinct_bit_patterns_per_column
        ):
            self.distinct_cardinality_capped = True
            return
        budget.add_distinct_bit_pattern()
        metadata.add(_DISTINCT_METADATA_OVERHEAD)
        self.distinct_bit_patterns.add(word)

    def as_dict(self, column_index: int, total_records: int) -> dict[str, object]:
        if (
            self.zero_bit_patterns + self.nonzero_bit_patterns != total_records
            or self.finite_ieee754_bit_patterns
            + self.nonfinite_ieee754_bit_patterns
            != total_records
            or self.small_unsigned_bit_patterns > total_records
        ):
            raise ScanFailure("malformed")
        cardinality = len(self.distinct_bit_patterns or ())
        return {
            "column_index": column_index,
            "zero_bit_patterns": self.zero_bit_patterns,
            "nonzero_bit_patterns": self.nonzero_bit_patterns,
            "finite_ieee754_bit_patterns": self.finite_ieee754_bit_patterns,
            "nonfinite_ieee754_bit_patterns": self.nonfinite_ieee754_bit_patterns,
            "small_unsigned_bit_patterns_below_threshold": (
                self.small_unsigned_bit_patterns
            ),
            "distinct_bit_pattern_cardinality": cardinality,
            "distinct_bit_pattern_cardinality_is_capped": (
                self.distinct_cardinality_capped
            ),
        }


@dataclass
class _TagAggregate:
    formula: CandidateFormula
    total_records: int = 0
    columns: list[_ColumnAggregate] | None = None

    def __post_init__(self) -> None:
        self.columns = [_ColumnAggregate() for _ in range(self.formula.column_count)]

    def add_records(self, count: int) -> None:
        self.total_records = _checked_add(self.total_records, count, _UINT64_MAX)

    def as_dict(self) -> dict[str, object]:
        if self.columns is None or len(self.columns) != self.formula.column_count:
            raise ScanFailure("malformed")
        return {
            "literal_tag": self.formula.literal_tag,
            "candidate_count_word_delta_bytes": self.formula.count_word_delta_bytes,
            "candidate_header_extent_bytes": self.formula.header_extent_bytes,
            "candidate_fixed_record_stride_bytes": (
                self.formula.fixed_record_stride_bytes
            ),
            "total_records": self.total_records,
            "column_count": self.formula.column_count,
            "columns": [
                column.as_dict(index, self.total_records)
                for index, column in enumerate(self.columns)
            ],
        }


@dataclass(frozen=True)
class _FormulaSpan:
    formula: CandidateFormula
    record_count: int
    records_offset: int


class _Aggregate:
    def __init__(self, metadata: _MetadataBudget) -> None:
        self.files = 0
        self.tags: dict[str, _TagAggregate] = {}
        for formula in CANDIDATE_FORMULAS:
            metadata.add(
                _FORMULA_METADATA_OVERHEAD
                + len(formula.literal_tag)
                + formula.column_count * _ENTRY_METADATA_OVERHEAD
            )
            self.tags[formula.literal_tag] = _TagAggregate(formula)

    def add_file(self) -> None:
        self.files = _checked_add(self.files, 1, _UINT64_MAX)

    def as_dict(self, budget: _RunBudget, limits: ScanLimits) -> dict[str, object]:
        shapes = [self.tags[formula.literal_tag].as_dict() for formula in CANDIDATE_FORMULAS]
        return {
            "schema_version": SCHEMA_VERSION,
            "scope": SCOPE,
            "interpretation": INTERPRETATION,
            "complete": True,
            "errors": {},
            "classification_parameters": {
                "small_unsigned_exclusive_threshold": (
                    SMALL_UNSIGNED_EXCLUSIVE_THRESHOLD
                ),
                "distinct_bit_pattern_cardinality_cap_per_column": (
                    limits.maximum_distinct_bit_patterns_per_column
                ),
            },
            "totals": {
                "files": self.files,
                "candidate_formula_occurrences": budget.formula_occurrences,
                "total_records": budget.total_records,
                "column_observations": budget.column_observations,
            },
            "candidate_record_shapes": shapes,
        }


def _validate_candidate_formulas(
    envelope: envelope_scanner.PopEnvelope,
    stream: _BudgetedStream,
    budget: _RunBudget,
    metadata: _MetadataBudget,
) -> list[_FormulaSpan]:
    markers = envelope.markers
    if (
        not markers
        or markers[0] != (envelope.gob_offset, "GOB:")
        or envelope.gob_offset % 4 != 0
    ):
        raise ScanFailure("malformed")

    spans: list[_FormulaSpan] = []
    seen: set[str] = set()
    previous_offset = -1
    for ordinal, (marker_offset, literal_tag) in enumerate(markers):
        try:
            encoded_tag = literal_tag.encode("ascii")
        except UnicodeError as exc:
            raise ScanFailure("malformed") from exc
        if (
            marker_offset <= previous_offset
            or marker_offset < envelope.gob_offset
            or marker_offset % 4 != 0
            or encoded_tag not in envelope_scanner.PUBLISHED_LITERAL_TAGS
        ):
            raise ScanFailure("malformed")
        previous_offset = marker_offset
        boundary = (
            markers[ordinal + 1][0]
            if ordinal + 1 < len(markers)
            else envelope.file_bytes
        )
        if boundary <= marker_offset or boundary > envelope.file_bytes:
            raise ScanFailure("malformed")

        formula = _FORMULA_BY_TAG.get(literal_tag)
        if formula is None:
            continue
        if literal_tag in seen:
            raise ScanFailure("formula_mismatch")
        seen.add(literal_tag)
        budget.add_formula()
        metadata.add(_FORMULA_METADATA_OVERHEAD + len(literal_tag))

        count_offset = marker_offset + formula.count_word_delta_bytes
        records_offset = count_offset + 4
        if records_offset > boundary:
            raise ScanFailure("formula_mismatch")
        record_count = _read_u32(stream, count_offset)
        budget.add_records(record_count, formula.column_count)
        record_bytes = _checked_multiply(
            record_count, formula.fixed_record_stride_bytes, _UINT64_MAX
        )
        expected_boundary = _checked_add(records_offset, record_bytes, _UINT64_MAX)
        # This exact equality also handles the zero/empty case: a zero count is
        # accepted only when the next ordered marker (or EOF) starts immediately
        # after the four-byte candidate count word.
        if expected_boundary != boundary:
            raise ScanFailure("formula_mismatch")
        spans.append(_FormulaSpan(formula, record_count, records_offset))

    if seen != set(_FORMULA_BY_TAG):
        raise ScanFailure("formula_mismatch")
    return spans


def _profile_formula_span(
    span: _FormulaSpan,
    stream: _BudgetedStream,
    aggregate: _TagAggregate,
    budget: _RunBudget,
    metadata: _MetadataBudget,
) -> None:
    aggregate.add_records(span.record_count)
    if span.record_count == 0:
        return
    if aggregate.columns is None:
        raise ScanFailure("malformed")

    stride = span.formula.fixed_record_stride_bytes
    records_per_chunk = budget.limits.maximum_profile_read_chunk_bytes // stride
    if records_per_chunk == 0:
        raise ScanFailure("limit_exceeded")
    stream.seek(span.records_offset)
    remaining = span.record_count
    while remaining:
        chunk_records = min(remaining, records_per_chunk)
        chunk_bytes = _checked_multiply(chunk_records, stride, _UINT64_MAX)
        data = _read_exact(stream, chunk_bytes)
        for record_index in range(chunk_records):
            base = record_index * stride
            for column_index, column in enumerate(aggregate.columns):
                word = struct.unpack_from("<I", data, base + column_index * 4)[0]
                column.observe(word, budget, metadata)
        remaining -= chunk_records


def _failure_result(category: str) -> dict[str, object]:
    return {
        "schema_version": SCHEMA_VERSION,
        "scope": SCOPE,
        "complete": False,
        "errors": {category: 1},
    }


def _result_fits_output_budget(result: dict[str, object], maximum_bytes: int) -> bool:
    encoded = json.dumps(result, indent=2, sort_keys=True).encode("utf-8")
    return len(encoded) <= maximum_bytes


def scan_root(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    try:
        _validate_limits(limits)
        root = root.absolute()
        metadata = _MetadataBudget(limits.maximum_metadata_bytes)
        metadata.add_name(root.name)
        reads = _ReadBudget(limits.maximum_total_read_bytes)
        budget = _RunBudget(limits)
        aggregate = _Aggregate(metadata)
        root_stat = os.stat(root, follow_symlinks=False)
        if not stat.S_ISDIR(root_stat.st_mode) or _path_is_link_like(root, root_stat):
            raise ScanFailure("unsafe_input")

        files_discovered = 0
        total_file_bytes = 0
        for candidate in _iter_pop_candidates(root, root_stat, limits, metadata):
            files_discovered = _checked_add(
                files_discovered, 1, limits.maximum_files
            )
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
                envelope = envelope_scanner.scan_pop_stream(
                    stream,
                    file_bytes,
                    envelope_scanner.ScanLimits(
                        maximum_file_bytes=limits.maximum_file_bytes,
                        maximum_terrain_records=limits.maximum_terrain_records,
                        maximum_name_bytes=limits.maximum_name_bytes,
                        maximum_marker_hits_per_file=limits.maximum_marker_hits_per_file,
                    ),
                )
                budget.add_terrain_records(envelope.terrain_records)
                spans = _validate_candidate_formulas(
                    envelope, stream, budget, metadata
                )
                for span in spans:
                    _profile_formula_span(
                        span,
                        stream,
                        aggregate.tags[span.formula.literal_tag],
                        budget,
                        metadata,
                    )

                final_stat = os.fstat(raw_stream.fileno())
                if (
                    not stat.S_ISREG(final_stat.st_mode)
                    or _stat_is_reparse(final_stat)
                    or not _same_file(final_stat, opened_stat)
                    or final_stat.st_size != opened_stat.st_size
                    or final_stat.st_mtime_ns != opened_stat.st_mtime_ns
                ):
                    raise ScanFailure("unsafe_input")
            aggregate.add_file()

        if files_discovered == 0:
            raise ScanFailure("no_candidates")
        result = aggregate.as_dict(budget, limits)
        if not _result_fits_output_budget(result, limits.maximum_output_bytes):
            raise ScanFailure("limit_exceeded")
        return result
    except envelope_scanner.ScanFailure as exc:
        return _failure_result(exc.category)
    except ScanFailure as exc:
        return _failure_result(exc.category)
    except (OSError, UnicodeError):
        return _failure_result("io_error")
    except Exception:
        # The public function follows the same privacy rule as the CLI: an
        # unexpected adapter or filesystem failure must not expose a partial
        # structural aggregate or a private path through an exception string.
        return _failure_result("io_error")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("disc_root", type=Path)
    parser.add_argument("--pretty", action="store_true")
    args = parser.parse_args()
    try:
        result = scan_root(args.disc_root)
    except Exception:  # Keep unexpected CLI failures from exposing private paths.
        result = _failure_result("io_error")
    if args.pretty:
        print(json.dumps(result, indent=2, sort_keys=True))
    else:
        print(json.dumps(result, sort_keys=True, separators=(",", ":")))
    return 0 if result["complete"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
