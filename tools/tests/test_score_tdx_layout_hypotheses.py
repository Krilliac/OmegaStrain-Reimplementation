from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from dataclasses import dataclass, replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import score_tdx_layout_hypotheses as scorer  # noqa: E402
from tools.fingerprint_assets import Span  # noqa: E402


@dataclass(frozen=True)
class PlaneSpec:
    transfer_code: int
    width: int
    height: int
    payload: bytes


def write_u16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def gray_palette(entry_count: int) -> bytes:
    scale = 16 if entry_count == 16 else 1
    return bytes(
        channel
        for entry in range(entry_count)
        for channel in (entry * scale, entry * scale, entry * scale, 255)
    )


def pack_low_nibble_first(indices: list[int]) -> bytes:
    packed = bytearray()
    for offset in range(0, len(indices), 2):
        low = indices[offset]
        high = indices[offset + 1] if offset + 1 < len(indices) else 0
        packed.append(low | (high << 4))
    return bytes(packed)


def make_tdx(
    bits_per_pixel: int,
    sample_width: int,
    sample_height: int,
    planes: list[PlaneSpec],
    palette: bytes | None = None,
) -> bytes:
    indexed = bits_per_pixel in (4, 8)
    if indexed and palette is None:
        palette = gray_palette(16 if bits_per_pixel == 4 else 256)
    if not indexed and palette is not None:
        raise ValueError("direct fixture must not carry a palette")
    primary_count = len(planes)
    primary_base = 0xA0 if indexed else 0x20
    secondary_base = 0x20
    primary_descriptor_bytes = primary_count * 128
    secondary_descriptor_bytes = 128 if indexed else 0
    primary_data_base = (
        primary_base + primary_descriptor_bytes + (scorer.PALETTE_SLOT_BYTES if indexed else 0)
    )
    stride = primary_data_base + sum(len(plane.payload) for plane in planes)
    if (scorer.HEADER_BYTES + stride) % 16:
        raise ValueError("synthetic fixture must have a 16-byte-aligned extent")
    data = bytearray(scorer.HEADER_BYTES + stride)

    header_storage_format = {4: 0x14, 8: 0x13, 24: 0x01, 32: 0x00}[bits_per_pixel]
    write_u16(data, 0x00, 5)
    write_u16(data, 0x02, 1 if indexed else 0)
    write_u16(data, 0x04, sample_width)
    write_u16(data, 0x06, sample_height)
    write_u16(data, 0x08, bits_per_pixel)
    write_u16(data, 0x0A, header_storage_format)
    write_u16(data, 0x0C, max(2 if indexed else 1, sample_width // 64))
    write_u16(data, 0x0E, sample_width * sample_height * bits_per_pixel // 8 // 256)
    write_u16(data, 0x10, 8 if bits_per_pixel == 4 else 16 if bits_per_pixel == 8 else 0)
    write_u16(data, 0x12, 2 if bits_per_pixel == 4 else 16 if bits_per_pixel == 8 else 0)
    write_u16(data, 0x14, 32 if indexed else 0)
    write_u16(data, 0x18, 1 if indexed else 0)
    write_u16(data, 0x1A, 4 if indexed else 0)
    write_u16(data, 0x22, 1)
    write_u16(data, 0x24, primary_count)
    write_u16(data, 0x26, 1 if indexed else 0)
    write_u16(data, 0x34, primary_descriptor_bytes)
    write_u16(data, 0x36, secondary_descriptor_bytes)
    write_u32(data, 0x38, stride)

    block = scorer.HEADER_BYTES
    write_u32(data, block + 0x18, primary_base)
    write_u32(data, block + 0x1C, secondary_base)
    data_cursor = primary_data_base
    for index, plane in enumerate(planes):
        expected_bytes = scorer.rectangle_bytes(
            plane.transfer_code, plane.width, plane.height
        )
        if len(plane.payload) != expected_bytes:
            raise ValueError("synthetic payload does not match its transfer rectangle")
        object_offset = primary_base + scorer.BLOCK_HEADER_BYTES + index * 128
        write_u32(data, block + index * 4, object_offset - primary_base)
        write_u32(data, block + object_offset + 0x04, plane.transfer_code << 24)
        write_u32(data, block + object_offset + 0x20, plane.width)
        write_u32(data, block + object_offset + 0x24, plane.height)
        write_u32(data, block + object_offset + 0x40, plane.width * plane.height)
        write_u32(data, block + object_offset + 0x54, data_cursor - primary_data_base)
        data[block + data_cursor:block + data_cursor + len(plane.payload)] = plane.payload
        data_cursor += len(plane.payload)

    if indexed:
        assert palette is not None
        entry_count = 16 if bits_per_pixel == 4 else 256
        if len(palette) != entry_count * 4:
            raise ValueError("synthetic palette has the wrong entry count")
        palette_object = secondary_base + scorer.BLOCK_HEADER_BYTES
        write_u32(data, block + 0x14, palette_object - secondary_base)
        write_u32(data, block + palette_object + 0x20, 8 if bits_per_pixel == 4 else 16)
        write_u32(data, block + palette_object + 0x24, 2 if bits_per_pixel == 4 else 16)
        write_u32(data, block + palette_object + 0x40, entry_count)
        secondary_data_base = (
            secondary_base + primary_descriptor_bytes + secondary_descriptor_bytes
        )
        write_u32(data, block + palette_object + 0x54, 0)
        data[block + secondary_data_base:block + secondary_data_base + len(palette)] = palette
    return bytes(data)


def make_hog(name: str, payload: bytes) -> bytes:
    encoded_name = name.encode("ascii") + b"\0"
    offsets_offset = 0x14
    names_offset = offsets_offset + 8
    data_offset = names_offset + len(encoded_name)
    data = bytearray(data_offset + len(payload))
    struct.pack_into("<5I", data, 0, 0x12345678, 1, offsets_offset, names_offset, data_offset)
    struct.pack_into("<2I", data, offsets_offset, 0, len(payload))
    data[names_offset:data_offset] = encoded_name
    data[data_offset:] = payload
    return bytes(data)


def score_one(payload: bytes, limits: scorer.ScanLimits | None = None) -> dict[str, object]:
    selected_limits = limits or scorer.ScanLimits()
    aggregate = scorer.Aggregate()
    budget = scorer.ScanBudget(selected_limits)
    scorer.score_tdx_span(
        io.BytesIO(payload), Span("PRIVATE_NAME.TDX", 0, len(payload)), aggregate, budget
    )
    return aggregate.as_dict()


class TdxLayoutHypothesisTests(unittest.TestCase):
    def test_indexed4_scores_nibble_order_with_plane_dimensions(self) -> None:
        row = list(range(8))
        payload = pack_low_nibble_first(row * 4)
        result = score_one(make_tdx(4, 32, 32, [PlaneSpec(0x14, 8, 4, payload)]))
        family = result["families"][scorer.family_key(4, 0x14)]
        self.assertEqual(family["planes"], 1)
        self.assertEqual(family["adjacency_edge_count"], 52)
        self.assertEqual(family["plane_ties"], 0)
        self.assertEqual(family["hypotheses"][scorer.LOW_NIBBLE_FIRST]["plane_wins"], 1)
        self.assertLess(
            family["hypotheses"][scorer.LOW_NIBBLE_FIRST]["rgb_adjacency_delta_sum"],
            family["hypotheses"][scorer.HIGH_NIBBLE_FIRST]["rgb_adjacency_delta_sum"],
        )

    def test_indexed8_scores_palette_lookup_permutation(self) -> None:
        payload = bytes(list(range(16)) * 4)
        result = score_one(make_tdx(8, 16, 4, [PlaneSpec(0x13, 16, 4, payload)]))
        family = result["families"][scorer.family_key(8, 0x13)]
        self.assertEqual(family["scored_planes"], 1)
        self.assertEqual(family["hypotheses"][scorer.IDENTITY_PALETTE]["plane_wins"], 1)
        self.assertLess(
            family["hypotheses"][scorer.IDENTITY_PALETTE]["rgb_adjacency_delta_sum"],
            family["hypotheses"][scorer.SWAPPED_PALETTE]["rgb_adjacency_delta_sum"],
        )

    def test_packed32_upload_family_is_counted_but_not_scored(self) -> None:
        payload = bytes(range(32))
        result = score_one(make_tdx(4, 8, 8, [PlaneSpec(0x00, 4, 2, payload)]))
        family = result["families"][scorer.family_key(4, 0x00)]
        self.assertEqual(family["planes"], 1)
        self.assertEqual(family["scored_planes"], 0)
        self.assertEqual(family["scoring_status"], "not_scored_raw_32bit_upload_order")
        self.assertNotIn("hypotheses", family)

    def test_each_primary_plane_is_stratified_by_its_own_transfer_code(self) -> None:
        packed32 = bytes(range(16))
        source_order = pack_low_nibble_first(list(range(8)) * 4)
        result = score_one(make_tdx(4, 8, 8, [
            PlaneSpec(0x00, 2, 2, packed32),
            PlaneSpec(0x14, 8, 4, source_order),
        ]))
        packed_family = result["families"][scorer.family_key(4, 0x00)]
        direct_family = result["families"][scorer.family_key(4, 0x14)]
        self.assertEqual(packed_family["planes"], 1)
        self.assertEqual(packed_family["scored_planes"], 0)
        self.assertEqual(direct_family["planes"], 1)
        self.assertEqual(direct_family["scored_planes"], 1)

    def test_proven_implicit_zero_suffix_is_reconstructed_aggregate_only(self) -> None:
        payload = bytes(256)
        complete = make_tdx(4, 32, 16, [PlaneSpec(0x14, 32, 16, payload)])
        result = score_one(complete[:-16])
        family = result["families"][scorer.family_key(4, 0x14)]
        self.assertEqual(result["totals"]["reconstructed_zero_suffix_planes"], 1)
        self.assertEqual(family["reconstructed_zero_suffix_planes"], 1)

    def test_unproven_shortened_family_fails_closed(self) -> None:
        payload = pack_low_nibble_first(list(range(8)) * 4)
        complete = make_tdx(4, 8, 4, [PlaneSpec(0x14, 8, 4, payload)])
        with self.assertRaises(ValueError):
            score_one(complete[:-16])

    def test_mismatched_primary_transfer_fails_closed(self) -> None:
        payload = pack_low_nibble_first(list(range(8)) * 4)
        malformed = bytearray(make_tdx(4, 8, 4, [PlaneSpec(0x14, 8, 4, payload)]))
        primary_object = scorer.HEADER_BYTES + 0xC0
        write_u32(malformed, primary_object + 0x04, 0x13 << 24)
        with self.assertRaises(ValueError):
            score_one(bytes(malformed))

    def test_scored_texel_budget_fails_closed(self) -> None:
        payload = pack_low_nibble_first(list(range(8)) * 4)
        limits = replace(scorer.ScanLimits(), maximum_plane_texels=31)
        with self.assertRaises(ValueError):
            score_one(make_tdx(4, 8, 4, [PlaneSpec(0x14, 8, 4, payload)]), limits)

    def test_hog_scan_emits_no_asset_identity_or_dimensions(self) -> None:
        payload = bytes(list(range(16)) * 4)
        tdx = make_tdx(8, 16, 4, [PlaneSpec(0x13, 16, 4, payload)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "SECRET_ARCHIVE.HOG").write_bytes(make_hog("SECRET_TEXTURE.TDX", tdx))
            result = scorer.scan_disc(root)
        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn("SECRET", encoded)
        self.assertEqual(result["totals"]["tdx_spans"], 1)

        def assert_private_keys_absent(value: object) -> None:
            if isinstance(value, dict):
                for key, child in value.items():
                    self.assertNotIn(key, {"path", "name", "hash", "width", "height", "payload"})
                    assert_private_keys_absent(child)
            elif isinstance(value, list):
                for child in value:
                    assert_private_keys_absent(child)

        assert_private_keys_absent(result)

    def test_empty_corpus_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                scorer.scan_disc(Path(directory))

    def test_cli_failure_is_sanitized(self) -> None:
        private_path = "C:/owned/private/SECRET_DISC_ROOT"
        output = io.StringIO()
        with mock.patch.object(sys, "argv", ["score_tdx_layout_hypotheses.py", private_path]):
            with redirect_stdout(output):
                self.assertEqual(scorer.main(), 2)
        self.assertEqual(
            json.loads(output.getvalue()), {"error": "unable to score corpus safely"}
        )
        self.assertNotIn("SECRET", output.getvalue())


if __name__ == "__main__":
    unittest.main()
