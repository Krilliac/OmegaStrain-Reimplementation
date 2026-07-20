from __future__ import annotations

import io
import json
import struct
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock

from tools import measure_member_structural_fingerprint as fingerprint


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
        0x10203040,
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
        0x10203040,
        declared_count,
        offsets_offset,
        names_offset,
        data_offset,
    )
    struct.pack_into(f"<{len(offsets)}I", data, offsets_offset, *offsets)
    data[names_offset:data_offset] = names
    data[data_offset:] = payload
    return bytes(data)


def run_main(arguments: list[str]) -> tuple[int, str, str, dict[str, object]]:
    stdout = io.StringIO()
    stderr = io.StringIO()
    with redirect_stdout(stdout), redirect_stderr(stderr):
        exit_code = fingerprint.main(arguments)
    rendered = stdout.getvalue()
    return exit_code, rendered, stderr.getvalue(), json.loads(rendered)


class MemberStructuralFingerprintTests(unittest.TestCase):
    def test_default_recursive_aggregate_has_only_fixed_size_fields(self) -> None:
        nested = make_hog(
            [
                ("ZXQNESTED100.GUI", b"n" * 14),
                ("ZXQNESTED200.IE", b"i" * 15),
            ]
        )
        root_hog = make_hog(
            [
                ("ZXQFIRST300.GUI", b"a" * 6),
                ("ZXQSECOND400.GUI", b"b" * 10),
                ("ZXQFONT500.FNT", b"f" * 9),
                ("ZXQZERO600.IE", b""),
                ("ZXQAUDIO700.BNK", b"private-audio"),
                ("ZXQWEAPON800.GUN", b"private-weapon"),
                ("ZXQUNKNOWN900.SECRET", b"private-unknown"),
                ("ZXQCHILD010.HOG", nested),
            ]
        )

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_MEMBER_SOURCE.HOG"
            path.write_bytes(root_hog)
            with mock.patch.object(
                fingerprint.hog_topology,
                "parse_hog_span",
                wraps=fingerprint.hog_topology.parse_hog_span,
            ) as parser:
                result = fingerprint.scan_input(path)

        self.assertEqual(parser.call_count, 2)
        self.assertEqual(
            set(result),
            {"schema_version", "scope", "measurements", "error_categories"},
        )
        self.assertEqual(result["schema_version"], 1)
        self.assertEqual(
            tuple(result["measurements"]), fingerprint.DEFAULT_SUFFIXES
        )
        self.assertEqual(
            result["measurements"][".gui"],
            {
                "count": 3,
                "payload_size_min": 6,
                "payload_size_max": 14,
                "distinct_payload_size_count": 3,
                "size_gcd": 2,
            },
        )
        self.assertEqual(
            result["measurements"][".fnt"],
            {
                "count": 1,
                "payload_size_min": 9,
                "payload_size_max": 9,
                "distinct_payload_size_count": 1,
                "size_gcd": 9,
            },
        )
        self.assertEqual(
            result["measurements"][".ie"],
            {
                "count": 2,
                "payload_size_min": 0,
                "payload_size_max": 15,
                "distinct_payload_size_count": 2,
                "size_gcd": 15,
            },
        )
        self.assertTrue(all(value == 0 for value in result["error_categories"].values()))

        serialized = json.dumps(result, sort_keys=True).lower()
        for forbidden in (
            "private_member_source",
            "zxqfirst300",
            "zxqnested100",
            "zxqaudio700",
            "zxqweapon800",
            "zxqunknown900",
            ".bnk",
            ".gun",
            ".secret",
            "private-audio",
            "private-weapon",
            "private-unknown",
        ):
            self.assertNotIn(forbidden, serialized)

    def test_size_gcd_divides_sizes_and_is_not_address_alignment(self) -> None:
        payload = make_hog(
            [("X.GUI", b"a" * 6), ("YY.GUI", b"b" * 10)]
        )
        data_offset = struct.unpack_from("<I", payload, 0x10)[0]
        self.assertEqual(data_offset % 2, 1)

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "SECRET_UNALIGNED_CONTAINER.HOG"
            path.write_bytes(payload)
            result = fingerprint.scan_input(path, suffixes=(".gui",))

        measured = result["measurements"][".gui"]
        self.assertEqual(measured["size_gcd"], 2)
        self.assertTrue(all(size % measured["size_gcd"] == 0 for size in (6, 10)))
        self.assertNotIn("alignment", measured)
        self.assertNotIn("offset", measured)

    def test_zero_size_members_preserve_nondecreasing_offset_grammar(self) -> None:
        payload = make_hog([("ZERO_A.GUI", b""), ("ZERO_B.GUI", b"")])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_ZERO.HOG"
            path.write_bytes(payload)
            result = fingerprint.scan_input(path, suffixes=(".gui",))

        self.assertEqual(
            result["measurements"][".gui"],
            {
                "count": 2,
                "payload_size_min": 0,
                "payload_size_max": 0,
                "distinct_payload_size_count": 1,
                "size_gcd": 0,
            },
        )

    def test_empty_measurement_uses_documented_zero_sentinels(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_EMPTY.HOG"
            path.write_bytes(make_hog([("OPAQUE.XYZ", b"x")]))
            result = fingerprint.scan_input(path)
        empty = {
            "count": 0,
            "payload_size_min": 0,
            "payload_size_max": 0,
            "distinct_payload_size_count": 0,
            "size_gcd": 0,
        }
        self.assertTrue(all(value == empty for value in result["measurements"].values()))

    def test_optional_suffixes_are_frozen_allowlist_only(self) -> None:
        self.assertEqual(
            fingerprint.APPROVED_SUFFIXES,
            (".bnk", ".fnt", ".gui", ".gun", ".ie"),
        )
        payload = make_hog(
            [("PRIVATE.BNK", b"b" * 12), ("PRIVATE.GUN", b"g" * 18)]
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_OPTIONAL.HOG"
            path.write_bytes(payload)
            first = fingerprint.scan_input(
                path, suffixes=(".GUN", ".BNK")
            )
            second = fingerprint.scan_input(
                path, suffixes=(".bnk", ".gun")
            )
            cli_code, _cli_text, cli_errors, cli_result = run_main(
                [str(path), "--suffix", ".GUN", "--suffix", ".BNK"]
            )

        self.assertEqual(first, second)
        self.assertEqual(cli_code, 0)
        self.assertEqual(cli_errors, "")
        self.assertEqual(cli_result, first)
        self.assertEqual(tuple(first["measurements"]), (".bnk", ".gun"))
        self.assertEqual(first["measurements"][".bnk"]["count"], 1)
        self.assertEqual(first["measurements"][".gun"]["count"], 1)

        for suffixes in ((), (".private",), (".gui", ".GUI")):
            with self.subTest(suffixes=suffixes):
                with self.assertRaises(fingerprint.ScanFailure) as raised:
                    fingerprint.scan_input(Path("PRIVATE_UNUSED"), suffixes=suffixes)
                self.assertEqual(raised.exception.category, "config")

    def test_schema_error_vocabulary_and_hard_ceilings_are_frozen(self) -> None:
        self.assertEqual(
            fingerprint.ERROR_CATEGORIES,
            (
                "config",
                "unsafe_input",
                "filesystem_limit",
                "missing_input",
                "root_archive_limit",
                "archive_size_limit",
                "archive_limit",
                "depth_limit",
                "root_hog_malformed",
                "nested_hog_malformed",
                "archive_name_invalid",
                "normalized_collision",
                "io",
                "offset_table_limit",
                "distinct_size_limit",
                "arithmetic_limit",
                "output_limit",
                "resource_limit",
                "internal",
            ),
        )
        expected_hard_limits = {
            "maximum_root_archives": 4096,
            "maximum_filesystem_entries": 1_000_000,
            "maximum_filesystem_depth": 64,
            "maximum_root_archive_bytes": 1_073_741_824,
            "maximum_nested_archive_bytes": 536_870_912,
            "maximum_archive_directory_bytes": 134_217_728,
            "maximum_archive_entries_per_directory": 1_000_000,
            "maximum_archive_occurrences": 1_000_000,
            "maximum_hog_entries": 8_000_000,
            "maximum_name_table_bytes": 536_870_912,
            "maximum_name_bytes": 4096,
            "maximum_traversed_span_bytes": 68_719_476_736,
            "maximum_nesting_depth": 16,
            "maximum_offset_table_bytes": 134_217_728,
            "maximum_distinct_sizes_per_suffix": 262_144,
            "maximum_total_distinct_sizes": 1_000_000,
            "maximum_output_bytes": 16_384,
        }
        self.assertEqual(dict(fingerprint.HARD_LIMITS), expected_hard_limits)
        self.assertEqual(
            set(vars(fingerprint.ScanLimits())), set(expected_hard_limits)
        )
        with self.assertRaises(TypeError):
            fingerprint.HARD_LIMITS["maximum_output_bytes"] = 1

        future = fingerprint.failure_document("future_upstream_category")
        self.assertEqual(future["error_categories"]["internal"], 1)
        self.assertNotIn("future_upstream_category", future["error_categories"])

        upstream_failure = fingerprint.hog_topology.ScanFailure("config")
        upstream_failure.category = "future_upstream_category"
        upstream_failure.args = ("ZXQ_PRIVATE_UPSTREAM_DETAIL",)
        with mock.patch.object(
            fingerprint, "scan_input", side_effect=upstream_failure
        ):
            code, rendered, errors, result = run_main(["ZXQ_PRIVATE_INPUT"])
        self.assertEqual(code, 1)
        self.assertEqual(errors, "")
        self.assertEqual(result["error_categories"]["internal"], 1)
        self.assertNotIn("future_upstream_category", rendered)
        self.assertNotIn("ZXQ_PRIVATE_UPSTREAM_DETAIL", rendered)

    def test_malformed_truncated_duplicate_and_bad_offsets_fail_closed(self) -> None:
        valid = make_hog([("ONE.GUI", b"1234"), ("TWO.FNT", b"5678")])
        nonmonotone = bytearray(valid)
        final_offset = struct.unpack_from("<I", nonmonotone, 0x1C)[0]
        struct.pack_into("<I", nonmonotone, 0x18, final_offset + 1)
        out_of_range = bytearray(valid)
        struct.pack_into("<I", out_of_range, 0x1C, len(valid) * 2)
        wrong_table = bytearray(valid)
        struct.pack_into("<I", wrong_table, 0x08, 0x18)
        duplicate = make_hog(
            [("DUPLICATE.GUI", b"a"), ("duplicate.gui", b"b")]
        )
        fixtures = (
            valid[:0],
            valid[:19],
            valid[:20],
            valid[:23],
            valid[:-1],
            bytes(nonmonotone),
            bytes(out_of_range),
            bytes(wrong_table),
            valid + b"\0",
        )
        for payload in fixtures:
            with self.subTest(size=len(payload)):
                with tempfile.TemporaryDirectory() as directory:
                    path = Path(directory) / "PRIVATE_MALFORMED.HOG"
                    path.write_bytes(payload)
                    with self.assertRaises(fingerprint.hog_topology.ScanFailure) as raised:
                        fingerprint.scan_input(path)
                self.assertEqual(raised.exception.category, "root_hog_malformed")

        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_DUPLICATE.HOG"
            path.write_bytes(duplicate)
            with self.assertRaises(fingerprint.hog_topology.ScanFailure) as raised:
                fingerprint.scan_input(path)
        self.assertEqual(raised.exception.category, "normalized_collision")

    def test_malformed_cli_suppresses_path_member_and_chained_parser_text(self) -> None:
        secret_path = "ZXQ_PRIVATE_MALFORMED_PATH_014"
        secret_member = "ZXQ_PRIVATE_MALFORMED_MEMBER_125.GUI"
        payload = bytearray(make_hog([(secret_member, b"payload")]))
        struct.pack_into("<I", payload, 0x18, len(payload) * 2)
        with tempfile.TemporaryDirectory(prefix=secret_path) as directory:
            path = Path(directory) / f"{secret_path}.HOG"
            path.write_bytes(payload)
            code, rendered, errors, result = run_main([str(path)])

        self.assertEqual(code, 1)
        self.assertEqual(errors, "")
        self.assertEqual(result["error_categories"]["root_hog_malformed"], 1)
        lowered = (rendered + errors).lower()
        self.assertNotIn(secret_path.lower(), lowered)
        self.assertNotIn(secret_member.lower(), lowered)
        self.assertNotIn("payload boundary", lowered)

        valid = make_hog([(secret_member, b"payload")])
        raw_exception = f"{secret_path}::{secret_member}::raw-parser-detail"
        with tempfile.TemporaryDirectory(prefix=secret_path) as directory:
            path = Path(directory) / f"{secret_path}.HOG"
            path.write_bytes(valid)
            with mock.patch.object(
                fingerprint.hog_topology,
                "parse_hog_span",
                side_effect=ValueError(raw_exception),
            ):
                code, rendered, errors, result = run_main([str(path)])

        self.assertEqual(code, 1)
        self.assertEqual(errors, "")
        self.assertEqual(result["error_categories"]["root_hog_malformed"], 1)
        lowered = (rendered + errors).lower()
        self.assertNotIn(secret_path.lower(), lowered)
        self.assertNotIn(secret_member.lower(), lowered)
        self.assertNotIn("raw-parser-detail", lowered)

    def test_nested_malformed_structure_is_typed(self) -> None:
        child = bytearray(make_hog([("INNER.GUI", b"payload")]))
        struct.pack_into("<I", child, 0x18, len(child) * 2)
        root = make_hog([("PRIVATE_CHILD.HOG", bytes(child))])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_ROOT.HOG"
            path.write_bytes(root)
            with self.assertRaises(fingerprint.hog_topology.ScanFailure) as raised:
                fingerprint.scan_input(path)
        self.assertEqual(raised.exception.category, "nested_hog_malformed")

    def test_nested_size_occurrence_and_depth_limits_are_exact(self) -> None:
        grandchild = make_hog([("PRIVATE_TARGET.GUI", b"payload")])
        child = make_hog([("PRIVATE_GRANDCHILD.HOG", grandchild)])
        root_hog = make_hog([("PRIVATE_CHILD.HOG", child)])
        exact = replace(
            fingerprint.ScanLimits(),
            maximum_nested_archive_bytes=max(len(child), len(grandchild)),
            maximum_archive_occurrences=3,
            maximum_nesting_depth=2,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_ROOT.HOG"
            path.write_bytes(root_hog)
            result = fingerprint.scan_input(path, exact)
            self.assertEqual(result["measurements"][".gui"]["count"], 1)

            cases = (
                (
                    "maximum_nested_archive_bytes",
                    max(len(child), len(grandchild)) - 1,
                    "archive_size_limit",
                ),
                ("maximum_archive_occurrences", 2, "archive_limit"),
                ("maximum_nesting_depth", 1, "depth_limit"),
            )
            for field, value, category in cases:
                with self.subTest(field=field):
                    with self.assertRaises(
                        (fingerprint.ScanFailure, fingerprint.hog_topology.ScanFailure)
                    ) as raised:
                        fingerprint.scan_input(path, replace(exact, **{field: value}))
                    self.assertEqual(raised.exception.category, category)

    def test_offset_distinct_container_entry_and_output_limits_are_exact(self) -> None:
        payload = make_hog(
            [("ONE.GUI", b"aa"), ("TWO.GUI", b"bbbb"), ("THREE.GUI", b"cccccccc")]
        )
        count = 3
        names_offset = struct.unpack_from("<I", payload, 0x0C)[0]
        data_offset = struct.unpack_from("<I", payload, 0x10)[0]
        exact = replace(
            fingerprint.ScanLimits(),
            maximum_root_archives=1,
            maximum_filesystem_entries=1,
            maximum_filesystem_depth=0,
            maximum_root_archive_bytes=len(payload),
            maximum_archive_directory_bytes=data_offset,
            maximum_archive_entries_per_directory=count,
            maximum_archive_occurrences=1,
            maximum_hog_entries=count,
            maximum_name_table_bytes=data_offset - names_offset,
            maximum_name_bytes=len("THREE.GUI"),
            maximum_traversed_span_bytes=len(payload),
            maximum_nesting_depth=0,
            maximum_offset_table_bytes=4 * (count + 1),
            maximum_distinct_sizes_per_suffix=3,
            maximum_total_distinct_sizes=3,
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "PRIVATE_LIMIT.HOG"
            path.write_bytes(payload)
            result = fingerprint.scan_input(path, exact, (".gui",))
            self.assertEqual(result["measurements"][".gui"]["count"], 3)

            rendered_bytes = len(
                json.dumps(result, separators=(",", ":"), sort_keys=True).encode("utf-8")
            ) + 1
            exact_output = replace(exact, maximum_output_bytes=rendered_bytes)
            self.assertEqual(
                fingerprint.scan_input(path, exact_output, (".gui",)), result
            )
            with self.assertRaises(fingerprint.ScanFailure) as raised:
                fingerprint.scan_input(
                    path,
                    replace(exact_output, maximum_output_bytes=rendered_bytes - 1),
                    (".gui",),
                )
            self.assertEqual(raised.exception.category, "output_limit")

            cases = (
                (
                    "maximum_root_archive_bytes",
                    len(payload) - 1,
                    "archive_size_limit",
                ),
                ("maximum_archive_directory_bytes", data_offset - 1, "archive_limit"),
                ("maximum_archive_entries_per_directory", count - 1, "archive_limit"),
                ("maximum_hog_entries", count - 1, "archive_limit"),
                ("maximum_name_table_bytes", data_offset - names_offset - 1, "archive_limit"),
                ("maximum_name_bytes", len("THREE.GUI") - 1, "archive_name_invalid"),
                ("maximum_traversed_span_bytes", len(payload) - 1, "archive_limit"),
                ("maximum_offset_table_bytes", 4 * (count + 1) - 1, "offset_table_limit"),
                ("maximum_distinct_sizes_per_suffix", 2, "distinct_size_limit"),
                ("maximum_total_distinct_sizes", 2, "distinct_size_limit"),
            )
            for field, value, category in cases:
                with self.subTest(field=field):
                    with self.assertRaises(
                        (fingerprint.ScanFailure, fingerprint.hog_topology.ScanFailure)
                    ) as raised:
                        fingerprint.scan_input(
                            path, replace(exact, **{field: value}), (".gui",)
                        )
                    self.assertEqual(raised.exception.category, category)

    def test_file_count_limit_is_enforced_before_second_archive(self) -> None:
        payload = make_hog([("ONE.GUI", b"x")])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "FIRST.HOG").write_bytes(payload)
            (root / "SECOND.HOG").write_bytes(payload)
            exact = fingerprint.scan_input(
                root,
                replace(
                    fingerprint.ScanLimits(),
                    maximum_root_archives=2,
                    maximum_filesystem_entries=2,
                ),
            )
            self.assertEqual(exact["measurements"][".gui"]["count"], 2)
            with self.assertRaises(fingerprint.hog_topology.ScanFailure) as raised:
                fingerprint.scan_input(
                    root,
                    replace(
                        fingerprint.ScanLimits(), maximum_root_archives=1
                    ),
                )
        self.assertEqual(raised.exception.category, "root_archive_limit")

    def test_filesystem_entry_depth_and_link_like_limits_fail_closed(self) -> None:
        payload = make_hog([("PRIVATE.GUI", b"x")])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            nested = root / "PRIVATE" / "DEEP"
            nested.mkdir(parents=True)
            (nested / "PRIVATE.HOG").write_bytes(payload)
            exact_limits = replace(
                fingerprint.ScanLimits(),
                maximum_filesystem_entries=3,
                maximum_filesystem_depth=2,
            )
            result = fingerprint.scan_input(root, exact_limits)
            self.assertEqual(result["measurements"][".gui"]["count"], 1)

            for field, value in (
                ("maximum_filesystem_entries", 2),
                ("maximum_filesystem_depth", 1),
            ):
                with self.subTest(field=field):
                    with self.assertRaises(
                        fingerprint.hog_topology.ScanFailure
                    ) as raised:
                        fingerprint.scan_input(
                            root, replace(exact_limits, **{field: value})
                        )
                    self.assertEqual(raised.exception.category, "filesystem_limit")

        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "TARGET.BIN"
            target.write_bytes(payload)
            link = root / "PRIVATE_LINK.HOG"
            try:
                link.symlink_to(target)
            except OSError:
                link.write_bytes(payload)
                original = fingerprint.hog_topology._is_link_like

                def classify(path: Path, info: object) -> bool:
                    return path.name == link.name or original(path, info)

                with mock.patch.object(
                    fingerprint.hog_topology,
                    "_is_link_like",
                    side_effect=classify,
                ):
                    with self.assertRaises(
                        fingerprint.hog_topology.ScanFailure
                    ) as raised:
                        fingerprint.scan_input(root)
            else:
                with self.assertRaises(
                    fingerprint.hog_topology.ScanFailure
                ) as raised:
                    fingerprint.scan_input(root)
        self.assertEqual(raised.exception.category, "unsafe_input")

    def test_hard_limits_cannot_be_widened_and_input_is_not_touched(self) -> None:
        limits = replace(
            fingerprint.ScanLimits(),
            maximum_offset_table_bytes=fingerprint.HARD_LIMITS[
                "maximum_offset_table_bytes"
            ]
            + 1,
        )
        with mock.patch.object(
            fingerprint.hog_topology,
            "_discover_root_archives",
            side_effect=AssertionError("input must not be inspected"),
        ) as discover:
            with self.assertRaises(fingerprint.ScanFailure) as raised:
                fingerprint.scan_input(Path("PRIVATE_UNUSED"), limits)
        self.assertEqual(raised.exception.category, "config")
        discover.assert_not_called()

    def test_cli_is_deterministic_path_free_and_silent_on_stderr(self) -> None:
        secret = "ZXQ_PRIVATE_PATH_781"
        member = "ZXQ_PRIVATE_MEMBER_892.GUI"
        payload = make_hog([(member, b"a" * 6), ("UNKNOWN.PRIVATE", b"x")])
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            path = Path(directory) / f"{secret}.HOG"
            path.write_bytes(payload)
            first = run_main([str(path)])
            second = run_main([str(path)])

        self.assertEqual(first, second)
        code, rendered, errors, result = first
        self.assertEqual(code, 0)
        self.assertEqual(errors, "")
        self.assertTrue(rendered.endswith("\n"))
        self.assertEqual(result["measurements"][".gui"]["count"], 1)
        lowered = (rendered + errors).lower()
        for forbidden in (
            secret.lower(),
            member.lower(),
            ".private",
        ):
            self.assertNotIn(forbidden, lowered)

    def test_cli_unknown_suffix_missing_path_and_raw_exception_are_private(self) -> None:
        secret = "ZXQ_PRIVATE_FAILURE_903"
        cases = (
            ([secret, "--suffix", ".private"], "config"),
            ([secret], "missing_input"),
            ([secret, f"{secret}_EXTRA"], "config"),
        )
        for arguments, category in cases:
            with self.subTest(category=category):
                code, rendered, errors, result = run_main(arguments)
                self.assertEqual(code, 1)
                self.assertEqual(errors, "")
                self.assertEqual(result["error_categories"][category], 1)
                self.assertNotIn(secret.lower(), (rendered + errors).lower())
                self.assertNotIn(".private", (rendered + errors).lower())

        with mock.patch.object(
            fingerprint, "scan_input", side_effect=RuntimeError(secret)
        ):
            code, rendered, errors, result = run_main([secret])
        self.assertEqual(code, 1)
        self.assertEqual(errors, "")
        self.assertEqual(result["error_categories"]["internal"], 1)
        self.assertNotIn(secret.lower(), (rendered + errors).lower())


if __name__ == "__main__":
    unittest.main()
