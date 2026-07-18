#!/usr/bin/env python3
"""Score bounded TDX display-layout hypotheses without exporting retail data.

The output is deliberately anonymous and aggregate-only.  It never includes
paths, archive entry names, hashes, dimensions from individual assets, palette
entries, texture bytes, or rendered samples.  A lower coherence score only
means that adjacent RGB values differ less under one named hypothesis; it does
not establish display semantics.
"""

from __future__ import annotations

import argparse
import collections
import json
import os
import struct
from dataclasses import dataclass, field
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
else:  # Direct execution adds tools/ rather than the repository root.
    from fingerprint_assets import HogDirectory, Span, parse_hog_span, range_is_zero, read_at


HEADER_BYTES = 64
BLOCK_HEADER_BYTES = 32
OBJECT_BYTES = 0x60
PALETTE_SLOT_BYTES = 0x400

LOW_NIBBLE_FIRST = "source_order_identity_palette_low_nibble_first"
HIGH_NIBBLE_FIRST = "source_order_identity_palette_high_nibble_first"
IDENTITY_PALETTE = "source_order_identity_palette"
SWAPPED_PALETTE = "source_order_bit3_bit4_swapped_palette_lookup"

KNOWN_HEADER_PAIRS = {
    (4, 0x14),
    (8, 0x13),
    (24, 0x01),
    (32, 0x00),
}
KNOWN_PRIMARY_PAIRS = {
    (4, 0x00),
    (4, 0x14),
    (8, 0x00),
    (8, 0x13),
    (24, 0x01),
    (32, 0x00),
}
OBSERVED_BLOCK_COUNTS = {1, 2, 3, 4, 6, 8, 10, 11, 19}

# Exact public structural allowlist already used by the native decoder.  It
# permits only the independently proven implicit-zero suffix family.
IMPLICIT_ZERO_FAMILIES = {
    (4, 64, 64, 32, 3360, 160, 32, 192, 1312, 0x00, 32, 16, 64, 288),
    (4, 64, 64, 64, 3360, 160, 32, 192, 1312, 0x00, 32, 16, 64, 288),
    (4, 32, 32, 32, 1824, 160, 32, 192, 1312, 0x00, 16, 8, 64, 288),
    (4, 128, 128, 32, 9504, 160, 32, 192, 1312, 0x00, 64, 32, 64, 288),
    (4, 128, 128, 64, 9504, 160, 32, 192, 1312, 0x00, 64, 32, 64, 288),
    (4, 32, 16, 16, 1568, 160, 32, 192, 1312, 0x14, 32, 16, 64, 288),
    (4, 128, 64, 32, 5408, 160, 32, 192, 1312, 0x14, 128, 64, 64, 288),
    (8, 16, 16, 256, 1568, 160, 32, 192, 1312, 0x00, 8, 8, 64, 288),
    (32, 16, 16, 16, 1184, 32, 32, 64, 160, 0x00, 16, 16, 0, 0),
}


@dataclass(frozen=True)
class ScanLimits:
    maximum_filesystem_entries: int = 500_000
    maximum_container_bytes: int = 4 * 1024 * 1024 * 1024
    maximum_hog_entries: int = 2_000_000
    maximum_nesting_depth: int = 32
    maximum_tdx_spans: int = 1 << 20
    maximum_tdx_bytes: int = 64 * 1024 * 1024
    maximum_blocks_per_tdx: int = 32
    maximum_planes_per_block: int = 4
    maximum_plane_texels: int = 16 * 1024 * 1024
    maximum_total_scored_texels: int = 256 * 1024 * 1024


@dataclass
class ScanBudget:
    limits: ScanLimits
    filesystem_entries: int = 0
    hog_entries: int = 0
    tdx_spans: int = 0
    scored_texels: int = 0

    def add_filesystem_entries(self, count: int) -> None:
        self.filesystem_entries += count
        if self.filesystem_entries > self.limits.maximum_filesystem_entries:
            raise ValueError("filesystem entry count exceeds safety limit")

    def add_hog_entry(self) -> None:
        self.hog_entries += 1
        if self.hog_entries > self.limits.maximum_hog_entries:
            raise ValueError("HOG entry count exceeds safety limit")

    def add_tdx(self, span_size: int) -> None:
        self.tdx_spans += 1
        if self.tdx_spans > self.limits.maximum_tdx_spans:
            raise ValueError("TDX span count exceeds safety limit")
        if span_size > self.limits.maximum_tdx_bytes:
            raise ValueError("TDX span exceeds safety limit")

    def add_scored_texels(self, texels: int) -> None:
        if texels > self.limits.maximum_plane_texels:
            raise ValueError("TDX plane texel count exceeds safety limit")
        self.scored_texels += texels
        if self.scored_texels > self.limits.maximum_total_scored_texels:
            raise ValueError("aggregate scored texel count exceeds safety limit")


@dataclass(frozen=True)
class TdxHeader:
    sample_width: int
    sample_height: int
    bits_per_pixel: int
    block_count: int
    primary_count: int
    palette_count: int
    primary_descriptor_bytes: int
    secondary_descriptor_bytes: int
    block_stride: int
    counted_size: int
    implicit_zero_suffix: bool


@dataclass(frozen=True)
class PlaneLayout:
    object_offset: int
    data_offset: int
    width: int
    height: int
    transfer_code: int
    byte_count: int


@dataclass(frozen=True)
class PaletteLayout:
    object_offset: int
    data_offset: int
    entry_count: int
    byte_count: int


@dataclass
class FamilyAggregate:
    header_bits_per_pixel: int
    primary_transfer_code: int
    planes: int = 0
    scored_planes: int = 0
    reconstructed_zero_suffix_planes: int = 0
    adjacency_edges: int = 0
    scores: collections.Counter[str] = field(default_factory=collections.Counter, init=False)
    wins: collections.Counter[str] = field(default_factory=collections.Counter, init=False)
    ties: int = 0

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = {
            "header_bits_per_pixel": self.header_bits_per_pixel,
            "primary_transfer_code": f"0x{self.primary_transfer_code:02X}",
            "planes": self.planes,
            "scored_planes": self.scored_planes,
            "reconstructed_zero_suffix_planes": self.reconstructed_zero_suffix_planes,
        }
        pair = (self.header_bits_per_pixel, self.primary_transfer_code)
        if pair in {(4, 0x00), (8, 0x00)}:
            result.update({
                "scoring_status": "not_scored_raw_32bit_upload_order",
                "reason": (
                    "the primary rectangle describes a packed 32-bit upload, not display "
                    "texel dimensions"
                ),
            })
        elif pair in {(24, 0x01), (32, 0x00)}:
            result.update({
                "scoring_status": "not_scored_nonindexed_family",
                "reason": "this bounded experiment compares indexed palette-layout hypotheses",
            })
        else:
            result.update({
                "scoring_status": "hypothesis_only",
                "adjacency_edge_count": self.adjacency_edges,
                "plane_ties": self.ties,
                "hypotheses": {
                    label: {
                        "rgb_adjacency_delta_sum": self.scores[label],
                        "plane_wins": self.wins[label],
                    }
                    for label in sorted(self.scores)
                },
            })
        return result


class Aggregate:
    def __init__(self) -> None:
        self.tdx_spans = 0
        self.blocks = 0
        self.primary_planes = 0
        self.reconstructed_zero_suffix_planes = 0
        self.families: dict[tuple[int, int], FamilyAggregate] = {}

    def family(self, bits_per_pixel: int, transfer_code: int) -> FamilyAggregate:
        key = (bits_per_pixel, transfer_code)
        if key not in self.families:
            self.families[key] = FamilyAggregate(bits_per_pixel, transfer_code)
        return self.families[key]

    def as_dict(self) -> dict[str, object]:
        scored_planes = sum(item.scored_planes for item in self.families.values())
        return {
            "schema_version": 1,
            "scope": (
                "anonymous aggregate hypothesis scores only; no paths, names, hashes, "
                "asset-specific dimensions, payload data, or samples"
            ),
            "interpretation": (
                "lower RGB adjacency-delta sums indicate only greater local coherence under a "
                "named hypothesis; they do not establish texture semantics"
            ),
            "totals": {
                "tdx_spans": self.tdx_spans,
                "blocks": self.blocks,
                "primary_planes": self.primary_planes,
                "scored_planes": scored_planes,
                "unscored_planes": self.primary_planes - scored_planes,
                "reconstructed_zero_suffix_planes": self.reconstructed_zero_suffix_planes,
            },
            "families": {
                family_key(*key): self.families[key].as_dict()
                for key in sorted(self.families)
            },
        }


def family_key(bits_per_pixel: int, transfer_code: int) -> str:
    return f"header_bpp_{bits_per_pixel}__primary_transfer_0x{transfer_code:02X}"


def u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def is_power_of_two(value: int) -> bool:
    return value > 0 and value & (value - 1) == 0


def known_flag_family(bits_per_pixel: int, flags: int) -> bool:
    if bits_per_pixel in (4, 8):
        return flags in (1, 3, 5, 7)
    if bits_per_pixel == 24:
        return flags == 0
    return bits_per_pixel == 32 and flags in (0, 2, 4, 6)


def rectangle_bytes(transfer_code: int, width: int, height: int) -> int:
    if width <= 0 or height <= 0:
        raise ValueError("TDX plane has an empty rectangle")
    elements = width * height
    if transfer_code == 0x00:
        return elements * 4
    if transfer_code == 0x01:
        return elements * 3
    if transfer_code == 0x13:
        return elements
    if transfer_code == 0x14:
        return (elements + 1) // 2
    raise ValueError("TDX plane has an unsupported transfer family")


def ranges_overlap(left: int, left_size: int, right: int, right_size: int) -> bool:
    return left < right + right_size and right < left + left_size


def parse_header(file: BinaryIO, span: Span, limits: ScanLimits) -> TdxHeader:
    if span.size < HEADER_BYTES or span.size % 16 != 0:
        raise ValueError("TDX span has an invalid header extent")
    header = read_at(file, span.offset, HEADER_BYTES)
    version = u16(header, 0)
    flags = u16(header, 2)
    sample_width = u16(header, 4)
    sample_height = u16(header, 6)
    bits_per_pixel = u16(header, 8)
    storage_format = u16(header, 10)
    if version != 5:
        raise ValueError("TDX version is outside the observed family")
    if not is_power_of_two(sample_width) or not is_power_of_two(sample_height):
        raise ValueError("TDX sample dimensions are outside the observed family")
    if (bits_per_pixel, storage_format) not in KNOWN_HEADER_PAIRS:
        raise ValueError("TDX header encoding pair is outside the observed family")
    if not known_flag_family(bits_per_pixel, flags):
        raise ValueError("TDX flags are outside the observed family")

    indexed = bits_per_pixel in (4, 8)
    expected_layout_words = {
        0x10: 8 if bits_per_pixel == 4 else 16 if bits_per_pixel == 8 else 0,
        0x12: 2 if bits_per_pixel == 4 else 16 if bits_per_pixel == 8 else 0,
        0x14: 32 if indexed else 0,
        0x18: 1 if indexed else 0,
        0x1A: 4 if indexed else 0,
        0x26: 1 if indexed else 0,
        0x36: 128 if indexed else 0,
    }
    if any(u16(header, offset) != value for offset, value in expected_layout_words.items()):
        raise ValueError("TDX layout signature is outside the observed family")
    if any(u16(header, offset) != 0 for offset in (0x16, 0x1E, 0x28, 0x30, 0x32)):
        raise ValueError("TDX reserved layout word is nonzero")
    if u32(header, 0x3C) != 0:
        raise ValueError("TDX reserved trailing word is nonzero")

    block_count = u16(header, 0x22)
    primary_count = u16(header, 0x24)
    palette_count = u16(header, 0x26)
    primary_descriptor_bytes = u16(header, 0x34)
    secondary_descriptor_bytes = u16(header, 0x36)
    block_stride = u32(header, 0x38)
    if block_count not in OBSERVED_BLOCK_COUNTS or block_count > limits.maximum_blocks_per_tdx:
        raise ValueError("TDX block count exceeds the observed bounded family")
    if not 1 <= primary_count <= limits.maximum_planes_per_block:
        raise ValueError("TDX primary-plane count exceeds the observed bounded family")
    if primary_descriptor_bytes != primary_count * 128:
        raise ValueError("TDX primary descriptor extent is inconsistent")
    if palette_count != (1 if indexed else 0):
        raise ValueError("TDX palette count contradicts the header encoding")
    if secondary_descriptor_bytes != (128 if indexed else 0):
        raise ValueError("TDX secondary descriptor extent is inconsistent")
    expected_width_units = max(2 if bits_per_pixel <= 8 else 1, sample_width // 64)
    if u16(header, 0x0C) != expected_width_units:
        raise ValueError("TDX width-unit word is outside the observed family")
    if block_stride < BLOCK_HEADER_BYTES:
        raise ValueError("TDX block stride is shorter than its pointer header")

    counted_size = HEADER_BYTES + block_count * block_stride
    implicit_zero_suffix = counted_size > span.size
    if implicit_zero_suffix:
        missing = counted_size - span.size
        if block_count != 1 or primary_count != 1 or missing > 256 or missing % 16 != 0:
            raise ValueError("TDX counted extent exceeds its span outside the proven family")
    elif counted_size < span.size and not range_is_zero(
        file, span.offset + counted_size, span.size - counted_size
    ):
        raise ValueError("TDX span has a nonzero tail after its counted extent")

    return TdxHeader(
        sample_width=sample_width,
        sample_height=sample_height,
        bits_per_pixel=bits_per_pixel,
        block_count=block_count,
        primary_count=primary_count,
        palette_count=palette_count,
        primary_descriptor_bytes=primary_descriptor_bytes,
        secondary_descriptor_bytes=secondary_descriptor_bytes,
        block_stride=block_stride,
        counted_size=counted_size,
        implicit_zero_suffix=implicit_zero_suffix,
    )


def block_u32(file: BinaryIO, block_offset: int, available: int, offset: int) -> int:
    if offset < 0 or offset + 4 > available:
        raise ValueError("TDX block reference exceeds the available span")
    return struct.unpack("<I", read_at(file, block_offset + offset, 4))[0]


def clut_bit3_bit4_swap(index: int) -> int:
    return (index & 0xE7) | ((index & 0x08) << 1) | ((index & 0x10) >> 1)


def palette_rgb(palette_data: bytes, entry_count: int) -> tuple[tuple[int, int, int], ...]:
    if len(palette_data) != entry_count * 4:
        raise ValueError("TDX palette read is incomplete")
    return tuple(
        (palette_data[index], palette_data[index + 1], palette_data[index + 2])
        for index in range(0, len(palette_data), 4)
    )


def adjacency_edges(width: int, height: int) -> int:
    return height * max(0, width - 1) + width * max(0, height - 1)


def rgb_delta(
    palette: tuple[tuple[int, int, int], ...], left: int, right: int
) -> int:
    left_rgb = palette[left]
    right_rgb = palette[right]
    return (
        abs(left_rgb[0] - right_rgb[0])
        + abs(left_rgb[1] - right_rgb[1])
        + abs(left_rgb[2] - right_rgb[2])
    )


def score_indexed8(
    payload: bytes,
    width: int,
    height: int,
    palette: tuple[tuple[int, int, int], ...],
    swap_lookup: bool,
) -> int:
    if len(payload) != width * height or len(palette) != 256:
        raise ValueError("TDX indexed-8 scoring inputs are inconsistent")

    def mapped(position: int) -> int:
        value = payload[position]
        return clut_bit3_bit4_swap(value) if swap_lookup else value

    score = 0
    for row in range(height):
        row_start = row * width
        for column in range(1, width):
            score += rgb_delta(palette, mapped(row_start + column - 1), mapped(row_start + column))
        if row:
            previous_start = row_start - width
            for column in range(width):
                score += rgb_delta(palette, mapped(previous_start + column), mapped(row_start + column))
    return score


def score_indexed4(
    payload: bytes,
    width: int,
    height: int,
    palette: tuple[tuple[int, int, int], ...],
    low_nibble_first: bool,
) -> int:
    texels = width * height
    if len(payload) != (texels + 1) // 2 or len(palette) != 16:
        raise ValueError("TDX indexed-4 scoring inputs are inconsistent")

    def index_at(position: int) -> int:
        packed = payload[position // 2]
        low_position = position % 2 == 0
        use_low = low_position == low_nibble_first
        return packed & 0x0F if use_low else packed >> 4

    score = 0
    for row in range(height):
        row_start = row * width
        for column in range(1, width):
            score += rgb_delta(
                palette, index_at(row_start + column - 1), index_at(row_start + column)
            )
        if row:
            previous_start = row_start - width
            for column in range(width):
                score += rgb_delta(
                    palette, index_at(previous_start + column), index_at(row_start + column)
                )
    return score


def record_scores(
    family: FamilyAggregate,
    payload: bytes,
    plane: PlaneLayout,
    palette: tuple[tuple[int, int, int], ...],
    budget: ScanBudget,
) -> None:
    texels = plane.width * plane.height
    budget.add_scored_texels(texels)
    family.scored_planes += 1
    family.adjacency_edges += adjacency_edges(plane.width, plane.height)
    if family.header_bits_per_pixel == 4 and plane.transfer_code == 0x14:
        scores = {
            LOW_NIBBLE_FIRST: score_indexed4(
                payload, plane.width, plane.height, palette, low_nibble_first=True
            ),
            HIGH_NIBBLE_FIRST: score_indexed4(
                payload, plane.width, plane.height, palette, low_nibble_first=False
            ),
        }
    elif family.header_bits_per_pixel == 8 and plane.transfer_code == 0x13:
        scores = {
            IDENTITY_PALETTE: score_indexed8(
                payload, plane.width, plane.height, palette, swap_lookup=False
            ),
            SWAPPED_PALETTE: score_indexed8(
                payload, plane.width, plane.height, palette, swap_lookup=True
            ),
        }
    else:
        raise ValueError("attempted to score an unscoped TDX family")
    family.scores.update(scores)
    best = min(scores.values())
    winners = [label for label, score in scores.items() if score == best]
    if len(winners) == 1:
        family.wins[winners[0]] += 1
    else:
        family.ties += 1


def parse_block(
    file: BinaryIO,
    span: Span,
    header: TdxHeader,
    block_index: int,
    aggregate: Aggregate,
    budget: ScanBudget,
) -> None:
    block_relative = HEADER_BYTES + block_index * header.block_stride
    available = min(header.block_stride, max(0, span.size - block_relative))
    if available < BLOCK_HEADER_BYTES:
        raise ValueError("TDX block pointer header is truncated")
    block_offset = span.offset + block_relative
    primary_base = block_u32(file, block_offset, available, 0x18)
    secondary_base = block_u32(file, block_offset, available, 0x1C)
    indexed = header.bits_per_pixel in (4, 8)
    primary_data_base = (
        primary_base + header.primary_descriptor_bytes + (PALETTE_SLOT_BYTES if indexed else 0)
    )

    planes: list[PlaneLayout] = []
    object_offsets: list[int] = []
    for plane_index in range(header.primary_count):
        object_offset = primary_base + block_u32(
            file, block_offset, available, plane_index * 4
        )
        if object_offset < BLOCK_HEADER_BYTES or object_offset + OBJECT_BYTES > available:
            raise ValueError("TDX primary object is outside the available block")
        if object_offsets and object_offset <= object_offsets[-1]:
            raise ValueError("TDX primary object order is outside the observed family")
        if any(ranges_overlap(object_offset, OBJECT_BYTES, prior, OBJECT_BYTES) for prior in object_offsets):
            raise ValueError("TDX object records overlap")
        object_offsets.append(object_offset)
        transfer_code = (
            block_u32(file, block_offset, available, object_offset + 0x04) >> 24
        ) & 0x3F
        if (header.bits_per_pixel, transfer_code) not in KNOWN_PRIMARY_PAIRS:
            raise ValueError("TDX primary transfer contradicts the header encoding")
        width = block_u32(file, block_offset, available, object_offset + 0x20)
        height = block_u32(file, block_offset, available, object_offset + 0x24)
        byte_count = rectangle_bytes(transfer_code, width, height)
        data_offset = primary_data_base + block_u32(
            file, block_offset, available, object_offset + 0x54
        )
        if data_offset > header.block_stride or data_offset > available:
            raise ValueError("TDX primary data reference is outside the available block")
        planes.append(PlaneLayout(
            object_offset=object_offset,
            data_offset=data_offset,
            width=width,
            height=height,
            transfer_code=transfer_code,
            byte_count=byte_count,
        ))

    palette: PaletteLayout | None = None
    if indexed:
        palette_object = secondary_base + block_u32(file, block_offset, available, 0x14)
        if palette_object < BLOCK_HEADER_BYTES or palette_object + OBJECT_BYTES > available:
            raise ValueError("TDX palette object is outside the available block")
        if any(ranges_overlap(palette_object, OBJECT_BYTES, prior, OBJECT_BYTES) for prior in object_offsets):
            raise ValueError("TDX palette object overlaps a primary object")
        object_offsets.append(palette_object)
        palette_transfer = (
            block_u32(file, block_offset, available, palette_object + 0x04) >> 24
        ) & 0x3F
        palette_width = block_u32(file, block_offset, available, palette_object + 0x20)
        palette_height = block_u32(file, block_offset, available, palette_object + 0x24)
        expected_width, expected_height = ((8, 2) if header.bits_per_pixel == 4 else (16, 16))
        if palette_transfer != 0 or (palette_width, palette_height) != (
            expected_width, expected_height
        ):
            raise ValueError("TDX palette object is outside the observed family")
        entry_count = 16 if header.bits_per_pixel == 4 else 256
        palette_bytes = entry_count * 4
        secondary_data_base = (
            secondary_base + header.primary_descriptor_bytes + header.secondary_descriptor_bytes
        )
        palette_data = secondary_data_base + block_u32(
            file, block_offset, available, palette_object + 0x54
        )
        if palette_data + palette_bytes > available:
            raise ValueError("TDX palette data is outside the available block")
        palette = PaletteLayout(palette_object, palette_data, entry_count, palette_bytes)

    for index, plane in enumerate(planes):
        plane_end = plane.data_offset + plane.byte_count
        expected_end = planes[index + 1].data_offset if index + 1 < len(planes) else header.block_stride
        if plane_end != expected_end:
            raise ValueError("TDX primary storage rectangles do not exactly fill their suffix")
        if index and plane.data_offset <= planes[index - 1].data_offset:
            raise ValueError("TDX primary data order is outside the observed family")

    if palette is not None:
        if palette.data_offset + PALETTE_SLOT_BYTES != planes[0].data_offset:
            raise ValueError("TDX palette does not occupy the observed storage slot")
        palette_data_end = palette.data_offset + palette.byte_count
        if not range_is_zero(
            file,
            block_offset + palette_data_end,
            PALETTE_SLOT_BYTES - palette.byte_count,
        ):
            raise ValueError("TDX palette-slot padding is nonzero")

    for object_offset in object_offsets:
        for plane in planes:
            if ranges_overlap(object_offset, OBJECT_BYTES, plane.data_offset, plane.byte_count):
                raise ValueError("TDX primary data overlaps an object record")
        if palette and ranges_overlap(
            object_offset, OBJECT_BYTES, palette.data_offset, PALETTE_SLOT_BYTES
        ):
            raise ValueError("TDX palette storage overlaps an object record")

    uses_implicit_zero = False
    for plane in planes:
        family = aggregate.family(header.bits_per_pixel, plane.transfer_code)
        family.planes += 1
        aggregate.primary_planes += 1
        plane_end = plane.data_offset + plane.byte_count
        reconstructed = plane_end > available
        if reconstructed:
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
                or plane_end != header.block_stride
                or signature not in IMPLICIT_ZERO_FAMILIES
            ):
                raise ValueError("TDX primary data is truncated outside the proven family")
            uses_implicit_zero = True
            family.reconstructed_zero_suffix_planes += 1
            aggregate.reconstructed_zero_suffix_planes += 1

        if (header.bits_per_pixel, plane.transfer_code) not in {(4, 0x14), (8, 0x13)}:
            continue
        if palette is None:
            raise ValueError("scoreable indexed TDX plane has no palette")
        available_payload = min(plane.byte_count, max(0, available - plane.data_offset))
        payload = read_at(file, block_offset + plane.data_offset, available_payload)
        if available_payload < plane.byte_count:
            payload += bytes(plane.byte_count - available_payload)
        raw_palette = read_at(file, block_offset + palette.data_offset, palette.byte_count)
        record_scores(
            family,
            payload,
            plane,
            palette_rgb(raw_palette, palette.entry_count),
            budget,
        )

    if header.implicit_zero_suffix != uses_implicit_zero:
        raise ValueError("TDX implicit-zero suffix does not end in its final primary plane")


def score_tdx_span(
    file: BinaryIO,
    span: Span,
    aggregate: Aggregate,
    budget: ScanBudget,
) -> None:
    budget.add_tdx(span.size)
    header = parse_header(file, span, budget.limits)
    aggregate.tdx_spans += 1
    aggregate.blocks += header.block_count
    for block_index in range(header.block_count):
        parse_block(file, span, header, block_index, aggregate, budget)


def walk_hog(
    file: BinaryIO,
    directory: HogDirectory,
    depth: int,
    aggregate: Aggregate,
    budget: ScanBudget,
) -> None:
    for entry in directory.entries:
        budget.add_hog_entry()
        suffix = Path(entry.name).suffix.lower()
        if suffix == ".tdx":
            score_tdx_span(file, entry, aggregate, budget)
        if suffix != ".hog":
            continue
        if depth >= budget.limits.maximum_nesting_depth:
            raise ValueError("nested HOG depth exceeds safety limit")
        nested = parse_hog_span(file, entry.offset, entry.size)
        walk_hog(file, nested, depth + 1, aggregate, budget)


def iter_corpus_files(root: Path, budget: ScanBudget) -> Iterator[Path]:
    def walk_error(error: OSError) -> None:
        raise error

    for directory, directory_names, file_names in os.walk(root, followlinks=False, onerror=walk_error):
        directory_names.sort()
        file_names.sort()
        directory_path = Path(directory)
        for name in directory_names:
            path = directory_path / name
            budget.add_filesystem_entries(1)
            if path.is_symlink():
                raise ValueError("corpus contains a symbolic-link directory")
        for name in file_names:
            path = directory_path / name
            budget.add_filesystem_entries(1)
            if path.is_symlink():
                raise ValueError("corpus contains a symbolic-link file")
            if path.suffix.lower() in {".hog", ".tdx"}:
                yield path


def scan_disc(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    if root.is_symlink():
        raise ValueError("corpus root must not be a symbolic link")
    resolved = root.resolve()
    if not resolved.is_dir():
        raise ValueError("corpus root is not a directory")
    budget = ScanBudget(limits)
    aggregate = Aggregate()
    for path in iter_corpus_files(resolved, budget):
        size = path.stat().st_size
        if size > limits.maximum_container_bytes:
            raise ValueError("corpus container exceeds safety limit")
        with path.open("rb") as file:
            if path.suffix.lower() == ".hog":
                directory = parse_hog_span(file, 0, size)
                walk_hog(file, directory, 0, aggregate, budget)
            else:
                score_tdx_span(file, Span(path.name, 0, size), aggregate, budget)
    if aggregate.tdx_spans == 0:
        raise ValueError("corpus contains no bounded TDX spans")
    return aggregate.as_dict()


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("disc_root", type=Path)
    args = parser.parse_args()
    try:
        result = scan_disc(args.disc_root)
    except (OSError, ValueError):
        print(json.dumps({"error": "unable to score corpus safely"}, sort_keys=True))
        return 2
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
