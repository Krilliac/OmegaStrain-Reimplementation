from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import dataclass, replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import measure_level_tdx_topology as topology  # noqa: E402
from tools import score_tdx_layout_hypotheses as tdx_layout  # noqa: E402


@dataclass(frozen=True)
class PlaneSpec:
    transfer_code: int
    width: int
    height: int
    payload: bytes


def _u16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def _u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def make_tdx(
    bits_per_pixel: int = 32,
    sample_width: int = 4,
    sample_height: int = 4,
    planes: list[PlaneSpec] | None = None,
) -> bytes:
    if planes is None:
        planes = [PlaneSpec(0x00, 4, 4, bytes(range(64)))]
    indexed = bits_per_pixel in (4, 8)
    primary_count = len(planes)
    primary_base = 0xA0 if indexed else 0x20
    secondary_base = 0x20
    primary_descriptor_bytes = primary_count * 128
    secondary_descriptor_bytes = 128 if indexed else 0
    primary_data_base = (
        primary_base
        + primary_descriptor_bytes
        + (tdx_layout.PALETTE_SLOT_BYTES if indexed else 0)
    )
    stride = primary_data_base + sum(len(plane.payload) for plane in planes)
    if (tdx_layout.HEADER_BYTES + stride) % 16:
        raise ValueError("fixture TDX must be 16-byte aligned")
    data = bytearray(tdx_layout.HEADER_BYTES + stride)
    storage_format = {4: 0x14, 8: 0x13, 24: 0x01, 32: 0x00}[bits_per_pixel]
    _u16(data, 0x00, 5)
    _u16(data, 0x02, 1 if indexed else 0)
    _u16(data, 0x04, sample_width)
    _u16(data, 0x06, sample_height)
    _u16(data, 0x08, bits_per_pixel)
    _u16(data, 0x0A, storage_format)
    _u16(data, 0x0C, max(2 if indexed else 1, sample_width // 64))
    _u16(data, 0x10, 8 if bits_per_pixel == 4 else 16 if bits_per_pixel == 8 else 0)
    _u16(data, 0x12, 2 if bits_per_pixel == 4 else 16 if bits_per_pixel == 8 else 0)
    _u16(data, 0x14, 32 if indexed else 0)
    _u16(data, 0x18, 1 if indexed else 0)
    _u16(data, 0x1A, 4 if indexed else 0)
    _u16(data, 0x22, 1)
    _u16(data, 0x24, primary_count)
    _u16(data, 0x26, 1 if indexed else 0)
    _u16(data, 0x34, primary_descriptor_bytes)
    _u16(data, 0x36, secondary_descriptor_bytes)
    _u32(data, 0x38, stride)

    block = tdx_layout.HEADER_BYTES
    _u32(data, block + 0x18, primary_base)
    _u32(data, block + 0x1C, secondary_base)
    cursor = primary_data_base
    for index, plane in enumerate(planes):
        if len(plane.payload) != tdx_layout.rectangle_bytes(
            plane.transfer_code, plane.width, plane.height
        ):
            raise ValueError("fixture plane extent mismatch")
        object_offset = primary_base + tdx_layout.BLOCK_HEADER_BYTES + index * 128
        _u32(data, block + index * 4, object_offset - primary_base)
        _u32(data, block + object_offset + 0x04, plane.transfer_code << 24)
        _u32(data, block + object_offset + 0x20, plane.width)
        _u32(data, block + object_offset + 0x24, plane.height)
        _u32(data, block + object_offset + 0x40, plane.width * plane.height)
        _u32(data, block + object_offset + 0x54, cursor - primary_data_base)
        data[block + cursor : block + cursor + len(plane.payload)] = plane.payload
        cursor += len(plane.payload)

    if indexed:
        entry_count = 16 if bits_per_pixel == 4 else 256
        palette_object = secondary_base + tdx_layout.BLOCK_HEADER_BYTES
        _u32(data, block + 0x14, palette_object - secondary_base)
        _u32(data, block + palette_object + 0x20, 8 if bits_per_pixel == 4 else 16)
        _u32(data, block + palette_object + 0x24, 2 if bits_per_pixel == 4 else 16)
        _u32(data, block + palette_object + 0x40, entry_count)
        _u32(data, block + palette_object + 0x54, 0)
        secondary_data_base = (
            secondary_base + primary_descriptor_bytes + secondary_descriptor_bytes
        )
        palette = bytes(
            channel
            for entry in range(entry_count)
            for channel in (entry & 0xFF, entry & 0xFF, entry & 0xFF, 0x80)
        )
        data[
            block + secondary_data_base : block + secondary_data_base + len(palette)
        ] = palette
    return bytes(data)


def make_hog(entries: list[tuple[str, bytes]]) -> bytes:
    names = b"".join(name.encode("ascii") + b"\0" for name, _ in entries)
    offsets_offset = 0x14
    names_offset = offsets_offset + 4 * (len(entries) + 1)
    data_offset = names_offset + len(names)
    offsets = [0]
    for _name, payload in entries:
        offsets.append(offsets[-1] + len(payload))
    data = bytearray(data_offset + offsets[-1])
    struct.pack_into(
        "<5I",
        data,
        0,
        0x12345678,
        len(entries),
        offsets_offset,
        names_offset,
        data_offset,
    )
    struct.pack_into(f"<{len(offsets)}I", data, offsets_offset, *offsets)
    data[names_offset:data_offset] = names
    cursor = data_offset
    for _name, payload in entries:
        data[cursor : cursor + len(payload)] = payload
        cursor += len(payload)
    return bytes(data)


def make_pop(cell_names: list[str]) -> bytes:
    data = bytearray(struct.pack("<I4sI", 70, b"TER:", len(cell_names)))
    for index, name in enumerate(cell_names):
        data.extend(struct.pack("<II", 1, index))
        data.extend(name.encode("ascii") + b"\0")
        while len(data) % 4:
            data.append(0)
    data.extend(b"GOB:")
    return bytes(data)


def write_level(root: Path, pop: bytes, hog: bytes, name: str = "PRIVATELEVEL") -> None:
    level = root / "GAMEDATA" / name
    level.mkdir(parents=True)
    (level / "DATA.POP").write_bytes(pop)
    (level / "DATA.HOG").write_bytes(hog)


class LevelTdxTopologyTests(unittest.TestCase):
    def test_recursive_common_scope_repetition_distinctness_and_maxima(self) -> None:
        texture = make_tdx()
        nested_cell = make_hog([("CELL_DEEP.TDX", texture)])
        cell = make_hog(
            [("CELL_DIRECT.TDX", texture), ("NESTED.HOG", nested_cell)]
        )
        unreferenced = make_hog([("COMMON_DEEP.TDX", texture)])
        common = make_hog(
            [
                ("COMMON_DIRECT.TDX", texture),
                ("CELL_A.HOG", cell),
                ("UNREFERENCED.HOG", unreferenced),
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL_A.COL", "cell_a.vum"]), common)
            result = topology.scan_disc(root)

        totals = result["totals"]
        self.assertEqual(totals["levels_scanned"], 1)
        self.assertEqual(totals["manifest_cell_occurrences"], 2)
        self.assertEqual(totals["manifest_distinct_cell_locators"], 1)
        self.assertEqual(totals["repeated_manifest_cell_occurrences"], 1)
        self.assertEqual(totals["tdx_occurrences"], 6)
        self.assertEqual(totals["distinct_tdx_locators"], 4)
        self.assertEqual(totals["repeated_tdx_occurrences"], 2)
        self.assertEqual(totals["common_scope_tdx_occurrences"], 1)
        self.assertEqual(totals["cell_scope_tdx_occurrences"], 2)
        self.assertEqual(totals["deeper_scope_tdx_occurrences"], 3)
        self.assertEqual(totals["deeper_archive_occurrences"], 3)
        self.assertEqual(totals["valid_tdx_occurrences"], 6)
        self.assertEqual(result["maxima"]["tdx_occurrences_per_cell"], 2)
        self.assertEqual(result["maxima"]["tdx_occurrences_per_level"], 6)
        self.assertEqual(result["maxima"]["load_archive_depth"], 2)
        self.assertEqual(result["maxima"]["tdx_owned_storage_bytes"], 64)
        self.assertEqual(
            result["maxima"]["all_tdx_owned_storage_bytes_per_level"], 6 * 64
        )
        self.assertIn("extension-bounded", result["scope"])
        self.assertIn("normalized .TDX-suffixed members", result["scope"])
        self.assertIn("recursive common DATA.HOG graph", result["scope"])
        self.assertIn("manifest-designated cell-occurrence", result["scope"])

    def test_indexed_storage_counts_owned_palette_without_claiming_abi_output(self) -> None:
        packed = bytes(range(32))
        texture = make_tdx(
            4, 8, 8, [PlaneSpec(0x14, 8, 8, packed)]
        )
        cell = make_hog([("INDEXED.TDX", texture)])
        common = make_hog([("CELL.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL.COL"]), common)
            result = topology.scan_disc(root)

        maxima = result["maxima"]
        self.assertEqual(maxima["tdx_owned_plane_bytes"], 32)
        self.assertEqual(maxima["tdx_owned_palette_bytes"], 64)
        self.assertEqual(maxima["tdx_owned_storage_bytes"], 96)
        self.assertEqual(maxima["tdx_palette_entries"], 16)
        self.assertEqual(maxima["tdx_structural_items"], 20)

    def test_zero_tdx_cell_is_counted_without_catalog_binding(self) -> None:
        cell = make_hog([("UNRELATED.VUM", b"opaque")])
        common = make_hog([("CELL.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL.COL"]), common)
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["zero_tdx_cell_occurrences"], 1)
        self.assertEqual(result["totals"]["tdx_occurrences"], 0)

    def test_malformed_tdx_does_not_prevent_structural_topology(self) -> None:
        cell = make_hog([("BROKEN.TDX", b"not a bounded TDX")])
        common = make_hog([("CELL.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL.COL"]), common)
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["levels_scanned"], 1)
        self.assertEqual(result["totals"]["tdx_occurrences"], 1)
        self.assertEqual(result["totals"]["malformed_tdx_occurrences"], 1)
        self.assertEqual(result["error_categories"]["tdx_malformed"], 1)

    def test_normalized_collision_fails_closed_and_is_counted(self) -> None:
        texture = make_tdx()
        cell = make_hog([("DUP.TDX", texture), ("dup.tdx", texture)])
        common = make_hog([("CELL.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL.COL"]), common)
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["levels_scanned"], 0)
        self.assertEqual(result["totals"]["normalized_collision_directories"], 1)
        self.assertEqual(result["totals"]["normalized_collision_entries"], 1)
        self.assertEqual(result["error_categories"]["normalized_collision"], 1)

    def test_malformed_nested_hog_is_typed_and_level_is_not_partial(self) -> None:
        cell = make_hog([("DEEP.HOG", b"not a HOG")])
        common = make_hog([("CELL.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL.COL"]), common)
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["levels_scanned"], 0)
        self.assertEqual(result["totals"]["tdx_occurrences"], 0)
        self.assertEqual(result["error_categories"]["nested_hog_malformed"], 1)

    def test_missing_cell_reference_is_typed_without_identity(self) -> None:
        common = make_hog([("PRESENT.HOG", make_hog([]))])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["PRIVATE_MISSING.COL"]), common)
            result = topology.scan_disc(root)
        encoded = json.dumps(result, sort_keys=True)
        self.assertEqual(result["error_categories"]["cell_reference_invalid"], 1)
        self.assertNotIn("PRIVATE_MISSING", encoded)

    def test_output_schema_never_exposes_identity_or_per_level_rows(self) -> None:
        secret_texture = make_tdx()
        cell = make_hog([("SECRET_TEXTURE_ALPHA.TDX", secret_texture)])
        common = make_hog([("SECRET_CELL_ALPHA.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(
                root,
                make_pop(["SECRET_CELL_ALPHA.COL"]),
                common,
                "SECRET_LEVEL_ALPHA",
            )
            result = topology.scan_disc(root)
        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn("SECRET", encoded)
        self.assertNotIn(str(root), encoded)
        self.assertEqual(
            set(result),
            {
                "schema_version",
                "scope",
                "totals",
                "maxima",
                "error_categories",
                "measurement_gaps",
            },
        )
        self.assertNotIn("levels", result)

    def test_measurement_gaps_block_unavailable_runtime_maxima(self) -> None:
        texture = make_tdx()
        common = make_hog([("COMMON.TDX", texture)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop([]), common)
            result = topology.scan_disc(root)
        self.assertEqual(
            result["measurement_gaps"],
            {key: 1 for key in topology.MEASUREMENT_GAPS},
        )
        forbidden_confirmed = {
            "exact_runtime_open_items",
            "exact_runtime_open_logical_output_bytes",
            "exact_runtime_load_items",
            "exact_runtime_load_logical_output_bytes",
        }
        self.assertTrue(forbidden_confirmed.isdisjoint(result["maxima"]))

    def test_open_and_load_structural_input_formulas_are_cumulative(self) -> None:
        texture = make_tdx()
        cell = make_hog([("TEXTURE.TDX", texture)])
        common = make_hog([("CELL.HOG", cell)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop(["CELL.COL", "CELL.VUM"]), common)
            result = topology.scan_disc(root)
        self.assertEqual(
            result["maxima"]["open_container_input_bytes"],
            len(common) + 2 * len(cell),
        )
        self.assertEqual(
            result["maxima"]["load_container_plus_tdx_input_bytes"],
            len(common) + len(cell) + len(texture),
        )
        self.assertEqual(
            result["maxima"]["open_archive_entries_plus_locator_items"],
            1 + 2 * 1 + 2,
        )

    def test_tdx_limit_is_typed_and_does_not_become_confirmed_decoder_maximum(self) -> None:
        texture = make_tdx()
        common = make_hog([("COMMON.TDX", texture)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_pop([]), common)
            result = topology.scan_disc(
                root, replace(topology.ScanLimits(), maximum_tdx_bytes=len(texture) - 1)
            )
        self.assertEqual(result["error_categories"]["tdx_limit_exceeded"], 1)
        self.assertEqual(result["totals"]["valid_tdx_occurrences"], 0)
        self.assertEqual(result["maxima"]["tdx_structural_items"], 0)

    def test_pop_errors_and_cli_failure_are_aggregate_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, b"PRIVATE_BROKEN_POP", make_hog([]))
            result = topology.scan_disc(root)
            self.assertEqual(result["error_categories"]["pop_malformed"], 1)

            output = io.StringIO()
            with redirect_stdout(output):
                exit_code = topology.main([str(root / "PRIVATE_MISSING_ROOT")])
            cli_result = json.loads(output.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(cli_result["totals"]["errors"], 1)
        self.assertNotIn("PRIVATE", output.getvalue())

    def test_invalid_cli_is_fixed_config_failure_without_argument_reflection(self) -> None:
        output = io.StringIO()
        errors = io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            exit_code = topology.main(
                ["PRIVATE_SECRET_ROOT", "PRIVATE_SECRET_EXTRA_ARGUMENT"]
            )
        result = json.loads(output.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(result["error_categories"]["config"], 1)
        self.assertEqual(errors.getvalue(), "")
        self.assertNotIn("PRIVATE_SECRET", output.getvalue())

    def test_discovery_budget_aborts_before_materializing_unbounded_scandir(self) -> None:
        consumed = 0

        def generated_entries():
            nonlocal consumed
            for index in range(1000):
                consumed += 1
                yield mock.Mock(name=f"ENTRY_{index:04d}")

        class ScandirContext:
            def __enter__(self):
                return generated_entries()

            def __exit__(self, _kind, _value, _traceback):
                return False

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "GAMEDATA").mkdir()
            budget = topology.ScanBudget(
                replace(topology.ScanLimits(), maximum_filesystem_entries=1)
            )
            with mock.patch.object(topology.os, "scandir", return_value=ScandirContext()):
                with self.assertRaises(topology.ScanFailure) as raised:
                    topology._discover_levels(root, budget)
        self.assertEqual(raised.exception.category, "filesystem_limit")
        self.assertEqual(consumed, 2)


if __name__ == "__main__":
    unittest.main()
