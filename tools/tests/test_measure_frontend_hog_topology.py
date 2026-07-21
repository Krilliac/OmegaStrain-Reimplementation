from __future__ import annotations

import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import measure_frontend_hog_topology as topology  # noqa: E402


def make_hog(entries: list[tuple[str, bytes]]) -> bytes:
    names = b"".join(name.encode("ascii") + b"\0" for name, _payload in entries)
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


def make_hog_with_raw_name_table(
    declared_count: int, names: bytes, payload: bytes = b""
) -> bytes:
    offsets_offset = 0x14
    names_offset = offsets_offset + 4 * (declared_count + 1)
    data_offset = names_offset + len(names)
    offsets = [0] * (declared_count + 1)
    offsets[-1] = len(payload)
    data = bytearray(data_offset + len(payload))
    struct.pack_into(
        "<5I",
        data,
        0,
        0x12345678,
        declared_count,
        offsets_offset,
        names_offset,
        data_offset,
    )
    struct.pack_into(f"<{len(offsets)}I", data, offsets_offset, *offsets)
    data[names_offset:data_offset] = names
    data[data_offset:] = payload
    return bytes(data)


def run_main(arguments: list[str]) -> tuple[int, str, dict[str, object]]:
    stdout = io.StringIO()
    with redirect_stdout(stdout):
        exit_code = topology.main(arguments)
    rendered = stdout.getvalue()
    return exit_code, rendered, json.loads(rendered)


class FrontendHogTopologyTests(unittest.TestCase):
    def test_recursive_exact_and_zero_padded_archives_are_aggregate_only(self) -> None:
        exact_child = make_hog(
            [
                ("ZXQPAIR731.SKM", b"mesh"),
                ("ZXQPAIR731.SKL", b"skeleton"),
                ("ZXQFONT842.TXT", b"text"),
                ("ZXQFONT842.TBL", b"table"),
            ]
        )
        padded_child = make_hog(
            [
                ("ZXQSPLASH953.TDX", b"texture"),
                ("ZXQCLICK064.VAG", b"audio"),
                ("ZXQUNKNOWN175.XYZ", b"opaque"),
            ]
        ) + bytes(32)
        root_hog = make_hog(
            [
                ("ZXQGROUP286.SKA", b"animation"),
                ("ZXQGROUP286.SKL", b"skeleton"),
                ("ZXQEXACT397.HOG", exact_child),
                ("ZXQPADDED408.HOG", padded_child),
                ("ZXQNOSUFFIX519", b"opaque"),
            ]
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_FRONTEND.HOG"
            path.write_bytes(root_hog)
            result = topology.scan_input(path)

        self.assertEqual(result["totals"]["root_archives_discovered"], 1)
        self.assertEqual(result["totals"]["root_archives_scanned"], 1)
        self.assertEqual(result["totals"]["archive_occurrences"], 3)
        self.assertEqual(result["totals"]["nested_archive_occurrences"], 2)
        self.assertEqual(result["totals"]["member_occurrences"], 12)
        self.assertEqual(result["totals"]["approved_member_occurrences"], 10)
        self.assertEqual(result["totals"]["other_member_occurrences"], 2)
        self.assertEqual(result["totals"]["basename_groups"], 3)
        self.assertEqual(result["totals"]["basename_pairs"], 3)
        self.assertEqual(result["archive_depth_distribution"]["0"], 1)
        self.assertEqual(result["archive_depth_distribution"]["1"], 2)
        self.assertEqual(result["archive_extent_families"]["root_exact"], 1)
        self.assertEqual(result["archive_extent_families"]["nested_exact"], 1)
        self.assertEqual(
            result["archive_extent_families"]["nested_zero_padded"], 1
        )
        self.assertEqual(result["approved_extension_counts"][".hog"], 2)
        self.assertEqual(result["approved_extension_counts"][".skl"], 2)
        self.assertEqual(result["approved_category_counts"]["container"], 2)
        self.assertEqual(result["approved_category_counts"]["skeleton"], 2)
        self.assertEqual(result["other_category_count"], 2)
        self.assertEqual(result["basename_pair_totals"][".ska+.skl"], 1)
        self.assertEqual(result["basename_pair_totals"][".skl+.skm"], 1)
        self.assertEqual(result["basename_pair_totals"][".tbl+.txt"], 1)
        self.assertEqual(sum(result["member_extent_buckets"].values()), 12)
        self.assertEqual(result["totals"]["errors"], 0)

        serialized = json.dumps(result, sort_keys=True).lower()
        for forbidden in (
            "zxqunknown175",
            "zxqnosuffix519",
            "zxqsplash953",
            "zxqclick064",
            "zxqgroup286",
            "zxqpair731",
            "zxqfont842",
            "zxqexact397",
            "zxqpadded408",
        ):
            self.assertNotIn(forbidden, serialized)

    def test_gui_ie_nested_pair_is_aggregate_only_and_fnt_stays_other(self) -> None:
        nested_hog = make_hog(
            [
                ("ZXQPAIR100.GUI", b"gui"),
                ("ZXQPAIR100.IE", b"ie"),
                ("ZXQCROSS700.IE", b"ie"),
                ("ZXQGLYPH300.FNT", b"font"),
            ]
        )
        root_hog = make_hog(
            [
                ("ZXQCHILD900.HOG", nested_hog),
                ("ZXQCROSS700.GUI", b"gui"),
                ("ZXQOUTER800.IE", b"ie"),
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_FRONTEND.HOG"
            path.write_bytes(root_hog)
            result = topology.scan_input(path)

        # Schema version was bumped to record the vocabulary extension.
        self.assertEqual(result["schema_version"], 3)

        # `.gui` and `.ie` are first-class, neutrally named vocabulary slots.
        self.assertIn(".gui", topology.APPROVED_EXTENSIONS)
        self.assertIn(".ie", topology.APPROVED_EXTENSIONS)
        self.assertIn("gui", topology.APPROVED_CATEGORIES)
        self.assertIn("ie", topology.APPROVED_CATEGORIES)
        self.assertIn(".gui", result["approved_extension_counts"])
        self.assertIn(".ie", result["approved_extension_counts"])
        self.assertIn("gui", result["approved_category_counts"])
        self.assertIn("ie", result["approved_category_counts"])
        self.assertEqual(result["approved_extension_counts"][".gui"], 2)
        self.assertEqual(result["approved_extension_counts"][".ie"], 3)
        self.assertEqual(result["approved_category_counts"]["gui"], 2)
        self.assertEqual(result["approved_category_counts"]["ie"], 3)

        # `.fnt` is not promoted and still collapses into ``other``.
        self.assertNotIn(".fnt", topology.APPROVED_EXTENSIONS)
        self.assertNotIn(".fnt", result["approved_extension_counts"])
        self.assertEqual(result["totals"]["other_member_occurrences"], 1)
        self.assertEqual(result["other_category_count"], 1)

        self.assertEqual(result["totals"]["archive_occurrences"], 2)
        self.assertEqual(result["totals"]["nested_archive_occurrences"], 1)
        self.assertEqual(result["archive_depth_distribution"]["1"], 1)
        self.assertEqual(result["totals"]["member_occurrences"], 7)
        self.assertEqual(result["totals"]["approved_member_occurrences"], 6)

        # The fixed `.gui+.ie` key counts one same-basename sibling group in a
        # nested HOG. A shared stem split across root and child does not pair
        # across archive-directory boundaries.
        self.assertIn(".gui+.ie", topology.PAIR_KEYS)
        self.assertEqual(result["basename_pair_totals"][".gui+.ie"], 1)
        self.assertEqual(result["totals"]["basename_groups"], 1)
        self.assertEqual(result["totals"]["basename_pairs"], 1)

        # Both neutral categories participate in fixed extent buckets.
        self.assertEqual(
            sum(result["category_extent_buckets"]["gui"].values()), 2
        )
        self.assertEqual(
            sum(result["category_extent_buckets"]["ie"].values()), 3
        )

        # Privacy: no member identity or raw/unapproved suffix ever leaks.
        serialized = json.dumps(result, sort_keys=True).lower()
        for forbidden in (
            "zxqpair100",
            "zxqchild900",
            "zxqcross700",
            "zxqouter800",
            "zxqglyph300",
            ".fnt",
        ):
            self.assertNotIn(forbidden, serialized)

    def test_frozen_public_vocabulary_matches_documented_set(self) -> None:
        # Guards the frozen contract: exactly these suffixes/categories are
        # public. `.gui`/`gui` and `.ie`/`ie` are present; `.fnt` and its
        # imagined category are absent.
        self.assertEqual(
            topology.APPROVED_EXTENSIONS,
            (
                ".col",
                ".gui",
                ".hog",
                ".ie",
                ".pop",
                ".ska",
                ".skas",
                ".skl",
                ".skm",
                ".so",
                ".tbl",
                ".tdx",
                ".txt",
                ".vag",
                ".vum",
            ),
        )
        self.assertEqual(
            topology.APPROVED_CATEGORIES,
            (
                "animation",
                "audio",
                "collision",
                "container",
                "gui",
                "ie",
                "material",
                "mesh",
                "scene",
                "script",
                "skeleton",
                "table",
                "text",
                "texture",
            ),
        )
        self.assertEqual(topology.CATEGORIES, (*topology.APPROVED_CATEGORIES, "other"))
        # Pair keys are derived from the approved extensions only.
        self.assertEqual(
            topology.PAIR_KEYS,
            tuple(
                f"{left}+{right}"
                for index, left in enumerate(topology.APPROVED_EXTENSIONS)
                for right in topology.APPROVED_EXTENSIONS[index + 1 :]
            ),
        )

    def test_directory_discovery_is_recursive_and_case_insensitive(self) -> None:
        archive = make_hog([("ITEM.TDX", b"x")])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nested = root / "PRIVATE" / "DEEP"
            nested.mkdir(parents=True)
            (root / "FIRST.HOG").write_bytes(archive)
            (nested / "SECOND.hOg").write_bytes(archive)
            (nested / "IGNORED.DAT").write_bytes(b"not an archive")
            result = topology.scan_input(root)

            for limits, category in (
                (
                    replace(
                        topology.ScanLimits(), maximum_filesystem_entries=1
                    ),
                    "filesystem_limit",
                ),
                (
                    replace(topology.ScanLimits(), maximum_root_archives=1),
                    "root_archive_limit",
                ),
            ):
                with self.subTest(category=category):
                    with self.assertRaises(topology.ScanFailure) as raised:
                        topology.scan_input(root, limits)
                    self.assertEqual(raised.exception.category, category)

        self.assertEqual(result["totals"]["root_archives_discovered"], 2)
        self.assertEqual(result["totals"]["root_archives_scanned"], 2)
        self.assertEqual(result["totals"]["archive_occurrences"], 2)
        self.assertEqual(result["approved_extension_counts"][".tdx"], 2)

    def test_malformed_offsets_and_root_padding_fail_closed(self) -> None:
        valid = make_hog([("ITEM.TDX", b"payload")])
        malformed = bytearray(valid)
        struct.pack_into("<I", malformed, 0x18, len(valid) * 2)
        for payload, category in (
            (bytes(malformed), "root_hog_malformed"),
            (valid + bytes(16), "root_hog_malformed"),
        ):
            with self.subTest(category=category, size=len(payload)):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "PRIVATE.HOG"
                    path.write_bytes(payload)
                    with self.assertRaises(topology.ScanFailure) as raised:
                        topology.scan_input(path)
                self.assertEqual(raised.exception.category, category)

    def test_malformed_nested_offset_is_typed(self) -> None:
        child = bytearray(make_hog([("ITEM.TDX", b"payload")]))
        struct.pack_into("<I", child, 0x18, len(child) * 2)
        valid = make_hog([("ITEM.TDX", b"payload")])
        for nested in (bytes(child), valid + bytes(15) + b"\x01"):
            with self.subTest(size=len(nested)):
                root_hog = make_hog([("CHILD.HOG", nested)])
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "PRIVATE.HOG"
                    path.write_bytes(root_hog)
                    with self.assertRaises(topology.ScanFailure) as raised:
                        topology.scan_input(path)
                self.assertEqual(raised.exception.category, "nested_hog_malformed")

    def test_invalid_names_and_normalized_collisions_fail_closed(self) -> None:
        fixtures = (
            (make_hog([("../PRIVATE.TDX", b"x")]), "archive_name_invalid"),
            (
                make_hog([("DUP.TDX", b"a"), ("dup.tdx", b"b")]),
                "normalized_collision",
            ),
            (make_hog([("DIR//ITEM.TDX", b"x")]), "archive_name_invalid"),
        )
        for payload, category in fixtures:
            with self.subTest(category=category):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "PRIVATE.HOG"
                    path.write_bytes(payload)
                    with self.assertRaises(topology.ScanFailure) as raised:
                        topology.scan_input(path)
                self.assertEqual(raised.exception.category, category)

    def test_name_table_shape_is_bounded_before_shared_parser(self) -> None:
        malicious = make_hog_with_raw_name_table(1, b"X\0" * 1_000_001)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE.HOG"
            path.write_bytes(malicious)
            with mock.patch.object(
                topology,
                "parse_hog_span",
                side_effect=AssertionError("shared parser must not be reached"),
            ) as parser:
                with self.assertRaises(topology.ScanFailure) as raised:
                    topology.scan_input(path)
        self.assertEqual(raised.exception.category, "root_hog_malformed")
        parser.assert_not_called()

    def test_name_table_precheck_preserves_trailing_nuls_and_name_limit(self) -> None:
        trailing_nuls = make_hog_with_raw_name_table(1, b"ITEM.TDX\0\0\0", b"x")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE.HOG"
            path.write_bytes(trailing_nuls)
            result = topology.scan_input(path)
        self.assertEqual(result["totals"]["member_occurrences"], 1)
        self.assertEqual(result["approved_extension_counts"][".tdx"], 1)

        too_long = make_hog_with_raw_name_table(1, b"ABCDE")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE.HOG"
            path.write_bytes(too_long)
            with mock.patch.object(
                topology,
                "parse_hog_span",
                side_effect=AssertionError("shared parser must not be reached"),
            ) as parser:
                with self.assertRaises(topology.ScanFailure) as raised:
                    topology.scan_input(
                        path,
                        replace(topology.ScanLimits(), maximum_name_bytes=4),
                    )
        self.assertEqual(raised.exception.category, "archive_name_invalid")
        parser.assert_not_called()

        too_few = make_hog_with_raw_name_table(2, b"ONLY_ONE")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE.HOG"
            path.write_bytes(too_few)
            with mock.patch.object(
                topology,
                "parse_hog_span",
                side_effect=AssertionError("shared parser must not be reached"),
            ) as parser:
                with self.assertRaises(topology.ScanFailure) as raised:
                    topology.scan_input(path)
        self.assertEqual(raised.exception.category, "root_hog_malformed")
        parser.assert_not_called()

    def test_depth_entry_name_and_size_limits_are_enforced(self) -> None:
        leaf = make_hog([("ITEM.TDX", b"x")])
        depth_two = make_hog([("SECOND.HOG", leaf)])
        depth_one = make_hog([("FIRST.HOG", depth_two)])
        cases = (
            (
                depth_one,
                replace(topology.ScanLimits(), maximum_nesting_depth=1),
                "depth_limit",
            ),
            (
                leaf,
                replace(
                    topology.ScanLimits(), maximum_root_archive_bytes=len(leaf) - 1
                ),
                "archive_size_limit",
            ),
            (
                make_hog([("CHILD.HOG", leaf)]),
                replace(
                    topology.ScanLimits(), maximum_nested_archive_bytes=len(leaf) - 1
                ),
                "archive_size_limit",
            ),
            (
                make_hog([("LONGNAME.TDX", b"x")]),
                replace(topology.ScanLimits(), maximum_name_bytes=4),
                "archive_name_invalid",
            ),
            (
                make_hog([("ONE.TDX", b"a"), ("TWO.TDX", b"b")]),
                replace(topology.ScanLimits(), maximum_hog_entries=1),
                "archive_limit",
            ),
        )
        for payload, limits, category in cases:
            with self.subTest(category=category, limits=limits):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "PRIVATE.HOG"
                    path.write_bytes(payload)
                    with self.assertRaises(topology.ScanFailure) as raised:
                        topology.scan_input(path, limits)
                self.assertEqual(raised.exception.category, category)

    def test_extent_bucket_boundaries_are_fixed(self) -> None:
        expected = {
            0: "zero",
            1: "1_255",
            255: "1_255",
            256: "256_4095",
            4095: "256_4095",
            4096: "4096_65535",
            65_535: "4096_65535",
            65_536: "65536_1048575",
            1_048_575: "65536_1048575",
            1_048_576: "1048576_16777215",
            16_777_215: "1048576_16777215",
            16_777_216: "16777216_plus",
        }
        for size, bucket in expected.items():
            with self.subTest(size=size):
                self.assertEqual(topology._extent_bucket(size), bucket)

    def test_json_output_is_deterministic_and_identity_free(self) -> None:
        payload = make_hog(
            [
                ("ZXQIDENTITY620.TDX", b"payload"),
                ("ZXQIDENTITY620.SKM", b"mesh"),
                ("ZXQRAWSUFFIX731.SECRET", b"opaque"),
            ]
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_SOURCE_CONTAINER.HOG"
            path.write_bytes(payload)
            first_code, first_text, first = run_main([str(path)])
            second_code, second_text, second = run_main([str(path)])

        self.assertEqual(first_code, 0)
        self.assertEqual(second_code, 0)
        self.assertEqual(first_text, second_text)
        self.assertEqual(first, second)
        self.assertEqual(first["schema_version"], 3)
        self.assertTrue(first_text.endswith("\n"))
        lowered = first_text.lower()
        for forbidden in (
            "private_source_container",
            "zxqidentity620",
            "zxqrawsuffix731",
            ".secret",
        ):
            self.assertNotIn(forbidden, lowered)

    def test_cli_failures_are_fixed_schema_and_do_not_echo_input(self) -> None:
        secret = "PRIVATE_MISSING_FRONTEND_SOURCE"
        exit_code, rendered, result = run_main([secret])
        self.assertEqual(exit_code, 1)
        self.assertNotIn(secret.lower(), rendered.lower())
        self.assertEqual(result["schema_version"], 3)
        self.assertEqual(result["approved_extension_counts"][".ie"], 0)
        self.assertEqual(result["basename_pair_totals"][".gui+.ie"], 0)
        self.assertEqual(result["totals"]["errors"], 1)
        self.assertEqual(result["error_categories"]["missing_input"], 1)
        self.assertEqual(
            set(result["error_categories"]), set(topology.ERROR_CATEGORIES)
        )

        exit_code, rendered, result = run_main([secret, f"{secret}_EXTRA"])
        self.assertEqual(exit_code, 1)
        self.assertNotIn(secret.lower(), rendered.lower())
        self.assertEqual(result["error_categories"]["config"], 1)

    def test_invalid_limit_configuration_is_rejected(self) -> None:
        with self.assertRaises(topology.ScanFailure) as raised:
            topology.scan_input(
                Path("PRIVATE_UNUSED"),
                replace(
                    topology.ScanLimits(),
                    maximum_nesting_depth=topology.MAXIMUM_REPORTED_ARCHIVE_DEPTH + 1,
                ),
            )
        self.assertEqual(raised.exception.category, "config")


if __name__ == "__main__":
    unittest.main()
