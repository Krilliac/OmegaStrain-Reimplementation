from __future__ import annotations

import io
import json
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import measure_level_texture_container_topology as topology  # noqa: E402
from tools import score_tdx_layout_hypotheses as tdx_layout  # noqa: E402


def _u16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def _u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def make_tdx(block_count: int = 1, plane_count: int = 1) -> bytes:
    width = 4
    height = 4
    payload = bytes(range(width * height * 4))
    primary_base = 0x20
    primary_descriptor_bytes = plane_count * 128
    primary_data_base = primary_base + primary_descriptor_bytes
    stride = primary_data_base + plane_count * len(payload)
    data = bytearray(tdx_layout.HEADER_BYTES + block_count * stride)
    _u16(data, 0x00, 5)
    _u16(data, 0x04, width)
    _u16(data, 0x06, height)
    _u16(data, 0x08, 32)
    _u16(data, 0x0C, 1)
    _u16(data, 0x22, block_count)
    _u16(data, 0x24, plane_count)
    _u16(data, 0x34, primary_descriptor_bytes)
    _u32(data, 0x38, stride)

    for block_index in range(block_count):
        block = tdx_layout.HEADER_BYTES + block_index * stride
        _u32(data, block + 0x18, primary_base)
        _u32(data, block + 0x1C, 0x20)
        cursor = primary_data_base
        for plane_index in range(plane_count):
            object_offset = (
                primary_base
                + tdx_layout.BLOCK_HEADER_BYTES
                + plane_index * 128
            )
            _u32(
                data,
                block + plane_index * 4,
                object_offset - primary_base,
            )
            _u32(data, block + object_offset + 0x04, 0)
            _u32(data, block + object_offset + 0x20, width)
            _u32(data, block + object_offset + 0x24, height)
            _u32(data, block + object_offset + 0x40, width * height)
            _u32(
                data,
                block + object_offset + 0x54,
                cursor - primary_data_base,
            )
            start = block + cursor
            data[start : start + len(payload)] = payload
            cursor += len(payload)
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


def write_level(
    root: Path,
    primary: bytes | None,
    map_container: bytes | None,
    name: str = "PRIVATE_LEVEL",
) -> Path:
    level = root / "GAMEDATA" / name
    level.mkdir(parents=True)
    (level / "DATA.POP").write_bytes(b"reference only")
    (level / "DATA.HOG").write_bytes(b"reference only")
    if primary is not None:
        (level / "TEX.HOG").write_bytes(primary)
    if map_container is not None:
        (level / "MAPTEX.HOG").write_bytes(map_container)
    return level


class LevelTextureContainerTopologyTests(unittest.TestCase):
    def test_two_container_happy_path_is_direct_only_and_generic(self) -> None:
        texture = make_tdx()
        nested = make_hog([("SECRET_NESTED.TDX", texture)])
        primary = make_hog(
            [
                ("SECRET_PRIMARY.TDX", texture),
                ("SECRET_NESTED.HOG", nested),
                ("SECRET_OTHER.BIN", b"opaque"),
            ]
        )
        map_container = make_hog([("SECRET_MAP.TDX", texture)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, primary, map_container)
            result = topology.scan_disc(root)

        self.assertEqual(result["totals"]["levels_discovered"], 1)
        self.assertEqual(result["totals"]["levels_scanned"], 1)
        self.assertEqual(result["totals"]["containers_scanned"], 2)
        self.assertEqual(result["totals"]["archive_entries"], 4)
        self.assertEqual(result["totals"]["tdx_occurrences"], 2)
        self.assertEqual(result["totals"]["valid_tdx_occurrences"], 2)
        self.assertEqual(result["totals"]["non_tdx_entries"], 2)
        self.assertEqual(
            result["container_classes"]["primary"]["totals"]["tdx_occurrences"],
            1,
        )
        self.assertEqual(
            result["container_classes"]["map"]["totals"]["tdx_occurrences"],
            1,
        )
        self.assertEqual(result["maxima"]["open_archive_depth"], 0)
        self.assertEqual(result["maxima"]["load_archive_depth"], 0)
        self.assertEqual(result["maxima"]["archive_bytes"], len(primary))
        self.assertEqual(result["maxima"]["archive_entries"], 3)
        self.assertEqual(
            result["maxima"]["open_container_input_bytes"], len(primary)
        )
        self.assertEqual(
            result["maxima"]["open_archive_entries_plus_locator_items"], 4
        )
        self.assertEqual(
            result["maxima"]["load_container_plus_tdx_input_bytes"],
            len(primary) + len(texture),
        )
        self.assertEqual(
            result["maxima"]["load_archive_entries_plus_tdx_structural_items"],
            6,
        )
        self.assertEqual(
            result["maxima"]["all_tdx_owned_storage_bytes_per_container"], 64
        )
        primary_maxima = result["container_classes"]["primary"]["maxima"]
        map_maxima = result["container_classes"]["map"]["maxima"]
        self.assertEqual(primary_maxima["open_container_input_bytes"], len(primary))
        self.assertEqual(primary_maxima["open_archive_entries_plus_locator_items"], 4)
        self.assertEqual(
            primary_maxima["load_container_plus_tdx_input_bytes"],
            len(primary) + len(texture),
        )
        self.assertEqual(
            primary_maxima["load_archive_entries_plus_tdx_structural_items"], 6
        )
        self.assertEqual(map_maxima["open_container_input_bytes"], len(map_container))
        self.assertEqual(map_maxima["open_archive_entries_plus_locator_items"], 2)
        self.assertEqual(
            map_maxima["load_container_plus_tdx_input_bytes"],
            len(map_container) + len(texture),
        )
        self.assertEqual(
            map_maxima["load_archive_entries_plus_tdx_structural_items"], 4
        )

    def test_missing_sibling_is_typed_per_class_and_level_is_atomic(self) -> None:
        texture_hog = make_hog([("DIRECT.TDX", make_tdx())])
        for missing_class in topology.CONTAINER_CLASSES:
            with self.subTest(missing_class=missing_class):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    primary = None if missing_class == "primary" else texture_hog
                    map_container = None if missing_class == "map" else texture_hog
                    write_level(root, primary, map_container)
                    result = topology.scan_disc(root)
                self.assertEqual(result["totals"]["levels_discovered"], 1)
                self.assertEqual(result["totals"]["levels_scanned"], 0)
                self.assertEqual(result["totals"]["containers_scanned"], 0)
                self.assertEqual(
                    result["container_classes"][missing_class]["error_categories"][
                        "missing_texture_container"
                    ],
                    1,
                )

    def test_malformed_and_zero_padded_containers_are_not_exact_hogs(self) -> None:
        valid = make_hog([("VALID.TDX", make_tdx())])
        cases = (b"not a HOG", valid + b"\0")
        for malformed in cases:
            with self.subTest(size=len(malformed)):
                with tempfile.TemporaryDirectory() as directory:
                    root = Path(directory)
                    write_level(root, malformed, valid)
                    result = topology.scan_disc(root)
                self.assertEqual(result["totals"]["levels_scanned"], 0)
                self.assertEqual(result["totals"]["containers_scanned"], 0)
                self.assertEqual(result["totals"]["archive_entries"], 0)
                self.assertEqual(result["totals"]["tdx_occurrences"], 0)
                self.assertTrue(all(value == 0 for value in result["maxima"].values()))
                self.assertEqual(
                    result["container_classes"]["primary"]["error_categories"][
                        "texture_container_malformed"
                    ],
                    1,
                )
                map_result = result["container_classes"]["map"]
                self.assertTrue(
                    all(value == 0 for value in map_result["totals"].values())
                )
                self.assertTrue(
                    all(value == 0 for value in map_result["maxima"].values())
                )

    def test_complete_collision_validation_happens_before_tdx_filter(self) -> None:
        collision = make_hog(
            [("SECRET_OTHER.BIN", b"a"), ("secret_other.bin", b"b")]
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, collision, make_hog([]))
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["levels_scanned"], 0)
        self.assertEqual(result["totals"]["normalized_collision_directories"], 1)
        self.assertEqual(result["totals"]["normalized_collision_entries"], 1)
        self.assertEqual(
            result["container_classes"]["primary"]["error_categories"][
                "normalized_collision"
            ],
            1,
        )

    def test_reparse_like_texture_container_is_global_discovery_failure(self) -> None:
        empty = make_hog([])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, empty, empty)
            original = topology._entry_is_link_like

            def classify(entry: os.DirEntry[str], info: os.stat_result) -> bool:
                return entry.name.upper() == "TEX.HOG" or original(entry, info)

            with mock.patch.object(
                topology, "_entry_is_link_like", side_effect=classify
            ):
                with self.assertRaises(topology.ScanFailure) as raised:
                    topology.scan_disc(root)
        self.assertEqual(raised.exception.category, "unsafe_input")

    def test_malformed_tdx_is_typed_without_losing_container_topology(self) -> None:
        primary = make_hog(
            [("GOOD.TDX", make_tdx()), ("BROKEN.TDX", b"not a TDX")]
        )
        map_container = make_hog([("MAP.TDX", make_tdx())])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, primary, map_container)
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["levels_scanned"], 1)
        self.assertEqual(result["totals"]["containers_scanned"], 2)
        self.assertEqual(result["totals"]["tdx_occurrences"], 3)
        self.assertEqual(result["totals"]["valid_tdx_occurrences"], 2)
        self.assertEqual(result["totals"]["malformed_tdx_occurrences"], 1)
        self.assertEqual(result["error_categories"]["tdx_malformed"], 1)

    def test_runtime_discovery_requires_pair_and_is_bounded(self) -> None:
        empty = make_hog([])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            incomplete = root / "GAMEDATA" / "NOT_RUNTIME"
            incomplete.mkdir(parents=True)
            (incomplete / "DATA.POP").write_bytes(b"only one marker")
            (incomplete / "TEX.HOG").write_bytes(empty)
            (incomplete / "MAPTEX.HOG").write_bytes(empty)
            write_level(root, empty, empty, "RUNTIME")
            result = topology.scan_disc(root)
        self.assertEqual(result["totals"]["levels_discovered"], 1)

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
            with mock.patch.object(
                topology.os, "scandir", return_value=ScandirContext()
            ):
                with self.assertRaises(topology.ScanFailure) as raised:
                    topology._discover_levels(root, budget)
        self.assertEqual(raised.exception.category, "filesystem_limit")
        self.assertEqual(consumed, 2)

    def test_exact_shared_limits_succeed_and_one_below_is_typed(self) -> None:
        texture = make_tdx()
        primary = make_hog([("ONE.TDX", texture)])
        map_container = make_hog([("TWO.TDX", texture)])
        data_offset = struct.unpack_from("<I", primary, 16)[0]
        exact = replace(
            topology.ScanLimits(),
            maximum_levels=1,
            maximum_filesystem_entries=5,
            maximum_texture_container_bytes=len(primary),
            maximum_hog_directory_bytes=data_offset,
            maximum_hog_entries=2,
            maximum_traversed_span_bytes=len(primary) + len(map_container),
            maximum_name_bytes=len("ONE.TDX"),
            maximum_tdx_occurrences=2,
            maximum_tdx_bytes=len(texture),
            maximum_blocks_per_tdx=1,
            maximum_planes_per_block=1,
            maximum_plane_elements=16,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, primary, map_container)
            result = topology.scan_disc(root, exact)
            self.assertEqual(result["totals"]["errors"], 0)
            self.assertEqual(result["totals"]["valid_tdx_occurrences"], 2)

            for field, category in (
                ("maximum_texture_container_bytes", "container_limit_exceeded"),
                ("maximum_hog_directory_bytes", "filesystem_limit"),
                ("maximum_hog_entries", "filesystem_limit"),
                ("maximum_traversed_span_bytes", "filesystem_limit"),
                ("maximum_name_bytes", "archive_name_invalid"),
                ("maximum_tdx_occurrences", "tdx_limit_exceeded"),
                ("maximum_tdx_bytes", "tdx_limit_exceeded"),
                ("maximum_plane_elements", "tdx_limit_exceeded"),
            ):
                with self.subTest(field=field):
                    limited = replace(exact, **{field: getattr(exact, field) - 1})
                    result = topology.scan_disc(root, limited)
                    self.assertGreater(result["error_categories"][category], 0)

            with self.assertRaises(topology.ScanFailure) as raised:
                topology.scan_disc(
                    root, replace(exact, maximum_filesystem_entries=4)
                )
            self.assertEqual(raised.exception.category, "filesystem_limit")

    def test_level_limit_exact_and_one_below_fails_atomically(self) -> None:
        empty = make_hog([])
        limits = replace(
            topology.ScanLimits(),
            maximum_levels=2,
            maximum_filesystem_entries=10,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, empty, empty, "LEVEL_A")
            write_level(root, empty, empty, "LEVEL_B")
            result = topology.scan_disc(root, limits)
            self.assertEqual(result["totals"]["levels_discovered"], 2)
            self.assertEqual(result["totals"]["levels_scanned"], 2)
            self.assertEqual(result["totals"]["errors"], 0)

            with self.assertRaises(topology.ScanFailure) as raised:
                topology.scan_disc(root, replace(limits, maximum_levels=1))
        self.assertEqual(raised.exception.category, "filesystem_limit")

    def test_block_and_plane_limits_are_exact_and_one_below_is_typed(self) -> None:
        texture = make_tdx(block_count=2, plane_count=2)
        limits = replace(
            topology.ScanLimits(),
            maximum_blocks_per_tdx=2,
            maximum_planes_per_block=2,
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(root, make_hog([("MULTI.TDX", texture)]), make_hog([]))
            result = topology.scan_disc(root, limits)
            self.assertEqual(result["totals"]["valid_tdx_occurrences"], 1)
            self.assertEqual(result["maxima"]["tdx_blocks"], 2)
            self.assertEqual(result["maxima"]["tdx_primary_planes"], 4)

            for field in (
                "maximum_blocks_per_tdx",
                "maximum_planes_per_block",
            ):
                with self.subTest(field=field):
                    limited = replace(limits, **{field: 1})
                    result = topology.scan_disc(root, limited)
                    self.assertEqual(
                        result["error_categories"]["tdx_limit_exceeded"], 1
                    )
                    self.assertEqual(result["totals"]["valid_tdx_occurrences"], 0)

    def test_fixed_schema_measurement_gaps_and_cli_never_expose_identity(self) -> None:
        secret = "PRIVATE_SECRET_ALPHA"
        texture = make_tdx()
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            write_level(
                root,
                make_hog([(f"{secret}.TDX", texture)]),
                make_hog([(f"{secret}_MAP.TDX", texture)]),
                secret,
            )
            result = topology.scan_disc(root)
        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(str(root), encoded)
        self.assertEqual(
            set(result),
            {
                "schema_version",
                "scope",
                "totals",
                "maxima",
                "container_classes",
                "error_categories",
                "measurement_gaps",
            },
        )
        self.assertEqual(set(result["totals"]), set(topology.TOTAL_FIELDS))
        self.assertEqual(set(result["maxima"]), set(topology.MAXIMUM_FIELDS))
        self.assertEqual(
            result["measurement_gaps"],
            {key: 1 for key in topology.MEASUREMENT_GAPS},
        )
        for container_class in topology.CONTAINER_CLASSES:
            class_result = result["container_classes"][container_class]
            self.assertEqual(
                set(class_result), {"totals", "maxima", "error_categories"}
            )
            self.assertEqual(
                set(class_result["totals"]), set(topology.CONTAINER_TOTAL_FIELDS)
            )
        self.assertNotIn("levels", result)

        output = io.StringIO()
        errors = io.StringIO()
        with redirect_stdout(output), redirect_stderr(errors):
            exit_code = topology.main([secret, f"{secret}_EXTRA"])
        cli_result = json.loads(output.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(cli_result["error_categories"]["config"], 1)
        self.assertEqual(errors.getvalue(), "")
        self.assertNotIn(secret, output.getvalue())


if __name__ == "__main__":
    unittest.main()
