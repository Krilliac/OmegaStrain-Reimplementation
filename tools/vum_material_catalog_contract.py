#!/usr/bin/env python3
"""Strict structural contract for canonical retail VUM material catalogs.

This module is a privacy-preserving Python mirror of the native
``detail::ValidateVumPayloadLayout`` acceptance boundary.  It intentionally
returns only normalized layout fields and fixed error codes: source bytes,
offsets, names, and decoder messages never enter an exception.

The native decoder remains authoritative.  Keep this mirror and its mutation
tests synchronized with ``native/src/retail/vum_layout_internal.cpp``.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Final, Literal


NAME_REGION_OFFSET: Final = 112
MATERIAL_RECORD_BYTES: Final = 92
METADATA_RECORD_BYTES: Final = 16

ContractErrorCode = Literal[
    "truncated",
    "malformed",
    "unsupported_variant",
    "invalid_reference",
    "limit_exceeded",
]


class VumContractError(ValueError):
    """A fixed-code VUM rejection that cannot disclose proprietary input."""

    def __init__(self, code: ContractErrorCode) -> None:
        super().__init__(code)
        self.code = code


@dataclass(frozen=True)
class VumPayloadLayout:
    name_count: int
    material_count: int
    names_end: int
    materials_end: int
    metadata_end: int
    middle_payload_begin: int
    final_payload_begin: int
    primary_end: int
    pair_count: int
    grouped_pair_count: int
    target_count: int
    target_block_start_index: int
    metadata_record_count: int


def _fail(code: ContractErrorCode) -> None:
    raise VumContractError(code)


def _u32(data: bytes | bytearray | memoryview, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _in_range_aligned(value: int, begin: int, end: int, alignment: int) -> bool:
    return begin <= value < end and value % alignment == 0


def _strictly_inside_aligned(
    value: int, begin: int, end: int, alignment: int
) -> bool:
    return begin < value < end and value % alignment == 0


def _is_record_start(value: int, begin: int, end: int) -> bool:
    return begin <= value < end and (value - begin) % METADATA_RECORD_BYTES == 0


def _classify_metadata_record(
    data: bytes | bytearray | memoryview,
    record_offset: int,
    metadata_begin: int,
    metadata_end: int,
    middle_payload_begin: int,
    final_payload_begin: int,
    primary_end: int,
) -> Literal["P", "Q", "T", "unknown"]:
    words = tuple(_u32(data, record_offset + slot * 4) for slot in range(4))
    if _is_record_start(words[2], metadata_begin, metadata_end):
        return "T"
    if _in_range_aligned(words[1], middle_payload_begin, final_payload_begin, 16) and (
        _strictly_inside_aligned(words[3], final_payload_begin, primary_end, 4)
    ):
        return "Q"
    if (
        _strictly_inside_aligned(words[0], final_payload_begin, primary_end, 4)
        and _strictly_inside_aligned(words[2], final_payload_begin, primary_end, 4)
        and _strictly_inside_aligned(words[3], final_payload_begin, primary_end, 4)
    ):
        return "P"
    return "unknown"


def _is_observed_middle_span(span_bytes: int) -> bool:
    return span_bytes in (16, 256, 480, 704)


def _is_observed_final_tail(span_bytes: int) -> bool:
    return span_bytes in (4, 8, 12, 16)


def _validate_middle_payload_references(
    data: bytes | bytearray | memoryview,
    span_begin: int,
    span_bytes: int,
    final_payload_begin: int,
    primary_end: int,
    pair_references: tuple[int, int, int, int],
    previous_combined_reference: int | None,
) -> bool:
    grouped = span_bytes != 16
    first_reference = _u32(data, span_begin + (0x74 if grouped else 4))
    if not _strictly_inside_aligned(
        first_reference, final_payload_begin, primary_end, 16
    ):
        _fail("invalid_reference")
    if previous_combined_reference is None:
        if first_reference != final_payload_begin + 16:
            _fail("invalid_reference")
    elif first_reference <= previous_combined_reference:
        _fail("invalid_reference")

    if not grouped:
        if not (
            first_reference
            < pair_references[0]
            < pair_references[1]
            < pair_references[2]
            < pair_references[3]
        ):
            _fail("invalid_reference")
        return False

    second_reference = _u32(data, span_begin + 0xF4)
    if not _strictly_inside_aligned(
        second_reference, final_payload_begin, primary_end, 16
    ) or not (
        first_reference
        < pair_references[0]
        < pair_references[1]
        < second_reference
        < pair_references[2]
        < pair_references[3]
    ):
        _fail("invalid_reference")
    return True


def validate_vum_payload_layout(
    data: bytes | bytearray | memoryview,
    *,
    maximum_input_bytes: int,
    maximum_items: int,
) -> VumPayloadLayout:
    """Validate the native canonical VUM payload-layout contract.

    ``maximum_items`` is the native decoder's per-decode item limit.  This
    function applies it to metadata records exactly where the native layout
    validator does; callers decoding catalog entries must also apply their
    combined root/name/material/reference/metadata item count.
    """

    if maximum_input_bytes < 0 or maximum_items < 0:
        raise ValueError("VUM validation limits must be nonnegative")
    if len(data) > maximum_input_bytes:
        _fail("limit_exceeded")
    if len(data) < NAME_REGION_OFFSET:
        _fail("truncated")
    if len(data) % 16:
        _fail("malformed")
    if bytes(data[:4]) != b"VUMS":
        _fail("malformed")

    name_count = _u32(data, 20)
    material_count = _u32(data, 24)
    names_end = _u32(data, 80)
    materials_end = _u32(data, 84)
    primary_end = _u32(data, 88)
    if (
        names_end < NAME_REGION_OFFSET
        or names_end % 4
        or materials_end % 4
        or primary_end % 16
        or names_end > materials_end
        or materials_end > primary_end
    ):
        _fail("malformed")
    if primary_end > len(data):
        _fail("truncated")
    if name_count == 0 or material_count == 0:
        _fail("unsupported_variant")
    if material_count * MATERIAL_RECORD_BYTES != materials_end - names_end:
        _fail("malformed")

    middle_payload_begin = _u32(data, 92)
    final_payload_begin = _u32(data, 96)
    if (
        middle_payload_begin % 16
        or final_payload_begin % 16
        or middle_payload_begin < materials_end
        or middle_payload_begin > final_payload_begin
        or final_payload_begin > primary_end
    ):
        _fail("malformed")
    if any(_u32(data, offset) != 0 for offset in (100, 104, 108)):
        _fail("unsupported_variant")

    paired_count_word = _u32(data, 12)
    target_count = _u32(data, 16)
    if paired_count_word == 0 or paired_count_word % 2 == 0:
        _fail("unsupported_variant")
    pair_count = (paired_count_word - 1) // 2
    metadata_record_count = pair_count * 2 + target_count
    if metadata_record_count > maximum_items:
        _fail("limit_exceeded")
    metadata_end = materials_end + metadata_record_count * METADATA_RECORD_BYTES
    if metadata_end > middle_payload_begin:
        _fail("malformed")
    alignment = data[metadata_end:middle_payload_begin]
    if len(alignment) > 12 or any(alignment):
        _fail("unsupported_variant")

    p_count = 0
    q_count = 0
    t_count = 0
    non_t_ordinal = 0
    inside_t_block = False
    after_t_block = False
    target_block_start_index = metadata_record_count
    previous_t_target: int | None = None
    previous_q_payload: int | None = None
    previous_final_boundary: int | None = None
    previous_combined_reference: int | None = None
    current_pair_references = [0, 0, 0, 0]
    grouped_pair_count = 0

    for index in range(metadata_record_count):
        record_offset = materials_end + index * METADATA_RECORD_BYTES
        kind = _classify_metadata_record(
            data,
            record_offset,
            materials_end,
            metadata_end,
            middle_payload_begin,
            final_payload_begin,
            primary_end,
        )
        if kind == "unknown":
            _fail("unsupported_variant")

        if kind == "T":
            if after_t_block:
                _fail("malformed")
            if not inside_t_block:
                target_block_start_index = index
            inside_t_block = True
            t_count += 1
            target = _u32(data, record_offset + 8)
            if target <= record_offset or not _is_record_start(
                target, materials_end, metadata_end
            ):
                _fail("invalid_reference")
            if (
                _classify_metadata_record(
                    data,
                    target,
                    materials_end,
                    metadata_end,
                    middle_payload_begin,
                    final_payload_begin,
                    primary_end,
                )
                != "Q"
            ):
                _fail("invalid_reference")
            if previous_t_target is not None and target <= previous_t_target:
                _fail("invalid_reference")
            previous_t_target = target
            continue

        if inside_t_block:
            after_t_block = True
        expected = "Q" if non_t_ordinal % 2 == 0 else "P"
        if kind != expected:
            _fail("malformed")
        non_t_ordinal += 1

        if kind == "Q":
            q_count += 1
            q_payload = _u32(data, record_offset + 4)
            if previous_q_payload is None:
                if q_payload != middle_payload_begin:
                    _fail("invalid_reference")
            else:
                if q_payload <= previous_q_payload:
                    _fail("unsupported_variant")
                span_bytes = q_payload - previous_q_payload
                if not _is_observed_middle_span(span_bytes):
                    _fail("unsupported_variant")
                grouped_pair_count += int(
                    _validate_middle_payload_references(
                        data,
                        previous_q_payload,
                        span_bytes,
                        final_payload_begin,
                        primary_end,
                        tuple(current_pair_references),
                        previous_combined_reference,
                    )
                )
                previous_combined_reference = current_pair_references[3]
            previous_q_payload = q_payload

            reference = _u32(data, record_offset + 12)
            if (
                previous_final_boundary is not None
                and reference <= previous_final_boundary
            ):
                _fail("invalid_reference")
            previous_final_boundary = reference
            current_pair_references[0] = reference
            continue

        p_count += 1
        for reference_index, reference_offset in enumerate((0, 8, 12)):
            reference = _u32(data, record_offset + reference_offset)
            if (
                previous_final_boundary is None
                or reference <= previous_final_boundary
            ):
                _fail("invalid_reference")
            previous_final_boundary = reference
            current_pair_references[reference_index + 1] = reference

    if p_count != pair_count or q_count != pair_count or t_count != target_count:
        _fail("malformed")
    if previous_q_payload is not None:
        if final_payload_begin <= previous_q_payload:
            _fail("unsupported_variant")
        final_span_bytes = final_payload_begin - previous_q_payload
        if not _is_observed_middle_span(final_span_bytes):
            _fail("unsupported_variant")
        grouped_pair_count += int(
            _validate_middle_payload_references(
                data,
                previous_q_payload,
                final_span_bytes,
                final_payload_begin,
                primary_end,
                tuple(current_pair_references),
                previous_combined_reference,
            )
        )
        if previous_final_boundary is None or not _is_observed_final_tail(
            primary_end - previous_final_boundary
        ):
            _fail("unsupported_variant")
    else:
        if middle_payload_begin != final_payload_begin:
            _fail("malformed")
        if primary_end - final_payload_begin != 16:
            _fail("unsupported_variant")

    return VumPayloadLayout(
        name_count=name_count,
        material_count=material_count,
        names_end=names_end,
        materials_end=materials_end,
        metadata_end=metadata_end,
        middle_payload_begin=middle_payload_begin,
        final_payload_begin=final_payload_begin,
        primary_end=primary_end,
        pair_count=pair_count,
        grouped_pair_count=grouped_pair_count,
        target_count=target_count,
        target_block_start_index=target_block_start_index,
        metadata_record_count=metadata_record_count,
    )
