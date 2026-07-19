from __future__ import annotations

import io
import json
import os
import struct
import sys
import tempfile
import unittest
from contextlib import contextmanager, redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock


sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tools import measure_level_material_texture_name_candidates as candidates  # noqa: E402
from tools import vum_material_catalog_contract as vum_contract  # noqa: E402


def _align(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def _u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


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
        data += struct.pack("<2I", 3, index)
        data += name.encode("ascii") + b"\0"
        data += bytes(_align(len(data), 4) - len(data))
    data += b"GOB:"
    return bytes(data)


def _write_material(data: bytearray, offset: int, references: tuple[int, ...]) -> None:
    data[offset : offset + 4] = b"MTRL"
    _u32(data, offset + 68, 0xFFFFFFFF)
    _u32(data, offset + 84, 0xFFFFFFFF)
    for slot in range(3):
        _u32(
            data,
            offset + 56 + slot * 4,
            references[slot] if slot < len(references) else 0xFFFFFFFF,
        )
        _u32(data, offset + 72 + slot * 4, 0xFFFFFFFF)
    usages = {1: (2,), 2: (2, 11), 3: (2, 12, 14)}[len(references)]
    for slot, usage in enumerate(usages):
        _u32(data, offset + 72 + slot * 4, usage)
    _u32(data, offset + 88, len(references))


def make_vum(names: list[str], materials: list[tuple[int, ...]]) -> bytes:
    encoded_names = b"".join(name.encode("ascii") + b"\0" for name in names)
    names_end = _align(112 + len(encoded_names), 4)
    materials_end = names_end + len(materials) * 92
    payload_begin = _align(materials_end, 16)
    primary_end = payload_begin + 16
    data = bytearray(primary_end)
    data[:4] = b"VUMS"
    _u32(data, 12, 1)
    _u32(data, 16, 0)
    _u32(data, 20, len(names))
    _u32(data, 24, len(materials))
    _u32(data, 80, names_end)
    _u32(data, 84, materials_end)
    _u32(data, 88, primary_end)
    _u32(data, 92, payload_begin)
    _u32(data, 96, payload_begin)
    data[112 : 112 + len(encoded_names)] = encoded_names
    for index, references in enumerate(materials):
        _write_material(data, names_end + index * 92, references)
    return bytes(data)


def make_nonempty_vum() -> bytes:
    """Port of the native catalog decoder's canonical T/Q/P fixture."""

    names_end = 132
    materials_end = 316
    metadata_t = materials_end
    metadata_q = materials_end + 16
    metadata_p = materials_end + 32
    payload_a = 368
    payload_b = 384
    primary_end = 432
    data = bytearray(448)
    data[:4] = b"VUMS"
    _u32(data, 4, 2)
    _u32(data, 12, 3)
    _u32(data, 16, 1)
    _u32(data, 20, 2)
    _u32(data, 24, 2)
    _u32(data, 28, 1)
    _u32(data, 80, names_end)
    _u32(data, 84, materials_end)
    _u32(data, 88, primary_end)
    _u32(data, 92, payload_a)
    _u32(data, 96, payload_b)
    data[112:120] = b"BASE.TDX"
    data[120] = 0
    data[121:131] = b"DETAIL.TDX"
    data[131] = 0
    _write_material(data, names_end, (0,))
    _write_material(data, names_end + 92, (1, 0, 1))
    _u32(data, metadata_t + 8, metadata_q)
    _u32(data, metadata_q + 4, payload_a)
    _u32(data, payload_a + 4, payload_b + 16)
    _u32(data, metadata_q + 12, payload_b + 32)
    _u32(data, metadata_p, payload_b + 36)
    _u32(data, metadata_p + 8, payload_b + 40)
    _u32(data, metadata_p + 12, payload_b + 44)
    data[primary_end:] = bytes([0xA5]) * (len(data) - primary_end)
    return bytes(data)


def make_grouped_vum(
    second_middle_span_bytes: int = 256, target_pairs: tuple[int, ...] = (1,)
) -> bytes:
    """Port of the native two-pair compact/grouped render-layout fixture."""

    names_end = 116
    materials_end = 208
    target_count = len(target_pairs)
    pair_count = 2
    qp_begin = materials_end + target_count * 16
    metadata_end = qp_begin + pair_count * 32
    middle_begin = _align(metadata_end, 16)
    second_middle_begin = middle_begin + 16
    final_begin = second_middle_begin + second_middle_span_bytes
    first_final_boundary = final_begin + 256
    primary_end = first_final_boundary + 7 * 64 + 16
    data = bytearray(primary_end + 16)
    data[:4] = b"VUMS"
    _u32(data, 12, pair_count * 2 + 1)
    _u32(data, 16, target_count)
    _u32(data, 20, 1)
    _u32(data, 24, 1)
    _u32(data, 80, names_end)
    _u32(data, 84, materials_end)
    _u32(data, 88, primary_end)
    _u32(data, 92, middle_begin)
    _u32(data, 96, final_begin)
    data[112:114] = b"A\0"
    _write_material(data, names_end, (0,))
    for index, target_pair in enumerate(target_pairs):
        _u32(data, materials_end + index * 16 + 8, qp_begin + target_pair * 32)
    for pair in range(pair_count):
        q = qp_begin + pair * 32
        p = q + 16
        _u32(data, q + 4, middle_begin if pair == 0 else second_middle_begin)
        _u32(data, q + 12, first_final_boundary + pair * 4 * 64)
        _u32(data, p, first_final_boundary + (pair * 4 + 1) * 64)
        _u32(data, p + 8, first_final_boundary + (pair * 4 + 2) * 64)
        _u32(data, p + 12, first_final_boundary + (pair * 4 + 3) * 64)
    data[middle_begin:primary_end] = bytes([0xA5]) * (
        primary_end - middle_begin
    )
    _u32(data, middle_begin + 4, final_begin + 16)
    _u32(data, second_middle_begin + 0x74, final_begin + 464)
    _u32(data, second_middle_begin + 0xF4, final_begin + 608)
    data[primary_end:] = bytes([0xC3]) * (len(data) - primary_end)
    return bytes(data)


def make_cell(vum_entries: list[tuple[str, bytes]]) -> bytes:
    return make_hog([("CELL.COL", b"opaque"), *vum_entries])


def write_level(
    root: Path,
    level_name: str,
    vum_entries: list[tuple[str, bytes]],
    primary_entries: list[tuple[str, bytes]],
    map_entries: list[tuple[str, bytes]],
    *,
    pop: bytes | None = None,
    common: bytes | None = None,
    write_primary: bool = True,
    write_map: bool = True,
) -> Path:
    level = root / "GAMEDATA" / level_name
    level.mkdir(parents=True)
    (level / "DATA.POP").write_bytes(pop if pop is not None else make_pop(["CELL.COL"]))
    (level / "DATA.HOG").write_bytes(
        common if common is not None else make_hog([("CELL.HOG", make_cell(vum_entries))])
    )
    if write_primary:
        (level / "TEX.HOG").write_bytes(make_hog(primary_entries))
    if write_map:
        (level / "MAPTEX.HOG").write_bytes(make_hog(map_entries))
    return level


def exact_branch(result: dict[str, object]) -> dict[str, object]:
    return result["candidate_classes"]["normalized_exact_terminal_name"]


class VumMaterialCatalogContractParityTests(unittest.TestCase):
    def validate(
        self, data: bytes | bytearray, maximum_items: int = 1 << 20
    ) -> vum_contract.VumPayloadLayout:
        return vum_contract.validate_vum_payload_layout(
            data,
            maximum_input_bytes=len(data),
            maximum_items=maximum_items,
        )

    def assert_contract_error(
        self,
        data: bytes | bytearray,
        code: vum_contract.ContractErrorCode,
        *,
        maximum_input_bytes: int | None = None,
        maximum_items: int = 1 << 20,
    ) -> None:
        with self.assertRaises(vum_contract.VumContractError) as caught:
            vum_contract.validate_vum_payload_layout(
                data,
                maximum_input_bytes=(
                    len(data) if maximum_input_bytes is None else maximum_input_bytes
                ),
                maximum_items=maximum_items,
            )
        self.assertEqual(caught.exception.code, code)
        self.assertEqual(str(caught.exception), code)

    def test_native_valid_empty_and_nonempty_families_extract_identically(self) -> None:
        empty = make_vum(["ALPHA.TDX"], [(0,)])
        empty_layout = self.validate(empty)
        self.assertEqual(empty_layout.pair_count, 0)
        self.assertEqual(empty_layout.metadata_record_count, 0)
        self.assertEqual(
            empty_layout.middle_payload_begin, empty_layout.final_payload_begin
        )
        self.assertEqual(empty_layout.primary_end - empty_layout.final_payload_begin, 16)

        nonempty = make_nonempty_vum()
        layout = self.validate(nonempty)
        self.assertEqual(layout.pair_count, 1)
        self.assertEqual(layout.target_count, 1)
        self.assertEqual(layout.metadata_record_count, 3)
        self.assertEqual(layout.grouped_pair_count, 0)

        budget = candidates.ScanBudget(candidates.ScanLimits())
        catalog = candidates._parse_vum_catalog(nonempty, budget)
        self.assertEqual(catalog.names, ("BASE.TDX", "DETAIL.TDX"))
        self.assertEqual(catalog.materials, ((0,), (1, 0, 1)))
        self.assertEqual(budget.vum_catalogs, 1)
        self.assertEqual(budget.vum_metadata_records, 3)

    def test_legacy_zero_metadata_fixture_is_rejected_without_budget_commit(self) -> None:
        invalid = bytearray(make_nonempty_vum())
        invalid[316:364] = bytes(48)
        self.assert_contract_error(invalid, "unsupported_variant")

        budget = candidates.ScanBudget(candidates.ScanLimits())
        with self.assertRaises(candidates.ScanFailure) as caught:
            candidates._parse_vum_catalog(bytes(invalid), budget)
        self.assertEqual(caught.exception.category, "vum_malformed")
        self.assertEqual(budget.vum_catalogs, 0)
        self.assertEqual(budget.vum_names, 0)
        self.assertEqual(budget.material_records, 0)
        self.assertEqual(budget.dense_name_references, 0)
        self.assertEqual(budget.vum_metadata_records, 0)

    def test_native_reference_partition_and_tail_mutations_are_rejected(self) -> None:
        cases: list[tuple[str, bytearray, vum_contract.ContractErrorCode]] = []

        backward_target = bytearray(make_nonempty_vum())
        _u32(backward_target, 316 + 8, 316)
        cases.append(("backward T target", backward_target, "invalid_reference"))

        non_q_target = bytearray(make_nonempty_vum())
        _u32(non_q_target, 316 + 8, 348)
        cases.append(("T target is not Q", non_q_target, "invalid_reference"))

        compact_reference = bytearray(make_nonempty_vum())
        _u32(compact_reference, 368 + 4, 384 + 32)
        cases.append(
            ("compact combined reference", compact_reference, "invalid_reference")
        )

        unsupported_middle_span = bytearray(make_nonempty_vum())
        _u32(unsupported_middle_span, 96, 400)
        cases.append(
            (
                "unsupported final Q middle span",
                unsupported_middle_span,
                "unsupported_variant",
            )
        )

        unsupported_tail = bytearray(make_nonempty_vum())
        _u32(unsupported_tail, 88, 448)
        cases.append(("unsupported final tail", unsupported_tail, "unsupported_variant"))

        nonzero_alignment = bytearray(make_nonempty_vum())
        nonzero_alignment[364] = 1
        cases.append(
            ("nonzero metadata alignment", nonzero_alignment, "unsupported_variant")
        )

        for label, invalid, code in cases:
            with self.subTest(label=label):
                self.assert_contract_error(invalid, code)

    def test_grouped_middle_spans_and_interleaving_match_native_contract(self) -> None:
        for span_bytes in (256, 480, 704):
            with self.subTest(span_bytes=span_bytes):
                layout = self.validate(make_grouped_vum(span_bytes))
                self.assertEqual(layout.pair_count, 2)
                self.assertEqual(layout.grouped_pair_count, 1)

        invalid = bytearray(make_grouped_vum())
        qp_begin = 224
        second_middle = struct.unpack_from("<I", invalid, qp_begin + 32 + 4)[0]
        final_begin = struct.unpack_from("<I", invalid, 96)[0]
        _u32(invalid, second_middle + 0xF4, final_begin + 576)
        self.assert_contract_error(invalid, "invalid_reference")

    def test_target_block_position_order_and_qp_alternation_match_native(self) -> None:
        midstream = bytearray(make_grouped_vum())
        target = bytes(midstream[208:224])
        first_q = bytes(midstream[224:240])
        first_p = bytes(midstream[240:256])
        midstream[208:224] = first_q
        midstream[224:240] = first_p
        midstream[240:256] = target
        self.assertEqual(self.validate(midstream).target_block_start_index, 2)

        for target_pairs in ((1, 1), (1, 0)):
            with self.subTest(target_pairs=target_pairs):
                self.assert_contract_error(
                    make_grouped_vum(target_pairs=target_pairs), "invalid_reference"
                )

        broken_alternation = bytearray(make_grouped_vum())
        first_q = bytes(broken_alternation[224:240])
        first_p = bytes(broken_alternation[240:256])
        broken_alternation[224:240] = first_p
        broken_alternation[240:256] = first_q
        self.assert_contract_error(broken_alternation, "malformed")

        noncontiguous_targets = bytearray(
            make_grouped_vum(target_pairs=(0, 1))
        )
        second_target = bytes(noncontiguous_targets[224:240])
        first_q = bytes(noncontiguous_targets[240:256])
        first_p = bytes(noncontiguous_targets[256:272])
        noncontiguous_targets[224:240] = first_q
        noncontiguous_targets[240:256] = first_p
        noncontiguous_targets[256:272] = second_target
        _u32(noncontiguous_targets, 208 + 8, 224)
        self.assert_contract_error(noncontiguous_targets, "malformed")

    def test_q_partitions_final_boundaries_and_combined_order_match_native(self) -> None:
        invalid_cases: list[tuple[str, bytearray, vum_contract.ContractErrorCode]] = []

        first_q_partition = bytearray(make_grouped_vum())
        middle_begin = struct.unpack_from("<I", first_q_partition, 92)[0]
        _u32(first_q_partition, 224 + 4, middle_begin + 16)
        invalid_cases.append(
            ("first Q partition", first_q_partition, "invalid_reference")
        )

        decreasing_q_partition = bytearray(make_grouped_vum())
        _u32(decreasing_q_partition, 256 + 4, middle_begin)
        invalid_cases.append(
            ("decreasing Q partition", decreasing_q_partition, "unsupported_variant")
        )

        unsupported_q_span = bytearray(make_grouped_vum())
        _u32(unsupported_q_span, 256 + 4, middle_begin + 32)
        invalid_cases.append(
            ("unsupported Q span", unsupported_q_span, "unsupported_variant")
        )

        duplicate_pair_boundary = bytearray(make_grouped_vum())
        first_q_final = struct.unpack_from("<I", duplicate_pair_boundary, 224 + 12)[0]
        _u32(duplicate_pair_boundary, 240, first_q_final)
        invalid_cases.append(
            ("duplicate pair boundary", duplicate_pair_boundary, "invalid_reference")
        )

        duplicate_cross_pair_boundary = bytearray(make_grouped_vum())
        first_pair_last = struct.unpack_from(
            "<I", duplicate_cross_pair_boundary, 240 + 12
        )[0]
        _u32(duplicate_cross_pair_boundary, 256 + 12, first_pair_last)
        invalid_cases.append(
            (
                "duplicate cross-pair boundary",
                duplicate_cross_pair_boundary,
                "invalid_reference",
            )
        )

        nonadvancing_combined = bytearray(make_grouped_vum())
        second_middle = struct.unpack_from("<I", nonadvancing_combined, 256 + 4)[0]
        _u32(nonadvancing_combined, second_middle + 0x74, first_pair_last)
        invalid_cases.append(
            ("nonadvancing combined reference", nonadvancing_combined, "invalid_reference")
        )

        for label, invalid, code in invalid_cases:
            with self.subTest(label=label):
                self.assert_contract_error(invalid, code)

    def test_empty_family_middle_and_sentinel_contract_match_native(self) -> None:
        nonempty_middle = bytearray(make_vum(["A"], [(0,)]))
        payload_begin = struct.unpack_from("<I", nonempty_middle, 96)[0]
        nonempty_middle.extend(bytes(16))
        _u32(nonempty_middle, 96, payload_begin + 16)
        _u32(nonempty_middle, 88, payload_begin + 32)
        self.assert_contract_error(nonempty_middle, "malformed")

        unsupported_sentinel = bytearray(make_vum(["A"], [(0,)]))
        unsupported_sentinel.extend(bytes(16))
        _u32(unsupported_sentinel, 88, payload_begin + 32)
        self.assert_contract_error(unsupported_sentinel, "unsupported_variant")

    def test_exact_input_string_and_combined_item_limits_pass_one_below_fails(self) -> None:
        data = make_nonempty_vum()
        self.validate(data, maximum_items=3)
        self.assert_contract_error(data, "limit_exceeded", maximum_items=2)
        self.assert_contract_error(
            data, "limit_exceeded", maximum_input_bytes=len(data) - 1
        )

        exact_limits = replace(
            candidates.ScanLimits(),
            maximum_vum_bytes=len(data),
            maximum_vum_catalog_items=12,
            maximum_name_bytes=len("DETAIL.TDX"),
        )
        exact = candidates._parse_vum_catalog(
            data, candidates.ScanBudget(exact_limits)
        )
        self.assertEqual(exact.names[-1], "DETAIL.TDX")

        for limits in (
            replace(exact_limits, maximum_vum_catalog_items=11),
            replace(exact_limits, maximum_name_bytes=len("DETAIL.TDX") - 1),
        ):
            with self.subTest(limits=limits):
                with self.assertRaises(candidates.ScanFailure) as caught:
                    candidates._parse_vum_catalog(
                        data, candidates.ScanBudget(limits)
                    )
                self.assertEqual(caught.exception.category, "vum_limit_exceeded")

    def test_structural_rejection_is_atomic_and_private_in_fixed_report(self) -> None:
        secret = "PRIVATE_VUM_CONTRACT_SECRET"
        invalid = bytearray(make_vum([f"{secret}.TDX"], [(0,)]))
        _u32(invalid, 100, 1)
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            write_level(
                root,
                secret,
                [(f"{secret}.VUM", bytes(invalid))],
                [(f"{secret}.TDX", b"private")],
                [("MAP.TDX", b"private")],
            )
            result = candidates.scan_disc(root)

        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(str(root), encoded)
        self.assertEqual(result["totals"]["levels_scanned"], 0)
        self.assertEqual(result["totals"]["vum_catalogs"], 0)
        self.assertEqual(result["totals"]["vum_name_occurrences"], 0)
        self.assertEqual(result["totals"]["material_records"], 0)
        self.assertEqual(result["totals"]["dense_name_references"], 0)
        self.assertEqual(result["error_categories"]["vum_malformed"], 1)


class LevelMaterialTextureNameCandidateTests(unittest.TestCase):
    def assert_private_atomic_unsafe_result(
        self,
        result: dict[str, object],
        secret: str,
        root: Path,
        levels_discovered: int = 1,
    ) -> None:
        expected_totals = {field: 0 for field in candidates.TOTAL_FIELDS}
        expected_totals["levels_discovered"] = levels_discovered
        expected_totals["errors"] = 1
        self.assertEqual(result["totals"], expected_totals)
        self.assertEqual(
            result["error_categories"],
            {
                category: int(category == "unsafe_input")
                for category in candidates.ERROR_CATEGORIES
            },
        )
        branch = exact_branch(result)
        for field_group in branch.values():
            self.assertTrue(all(value == 0 for value in field_group.values()))
        self.assertTrue(all(value == 0 for value in result["maxima"].values()))
        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(str(root), encoded)

    def test_exact_casefolded_candidates_references_flags_and_locator_coverage(self) -> None:
        names = [
            "alpha.tdx",
            "BETA.TDX",
            "SHARED.TDX",
            "MISSING.TDX",
            "NOT_TEXTURE.BIN",
            "../PRIVATE_ESCAPE.TDX",
        ]
        materials = [(0, 1), (2,), (3, 4, 5), (0, 0, 0)]
        vum = make_vum(names, materials)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(
                root,
                "LEVEL_A",
                [("CELL.VUM", vum)],
                [("ALPHA.TDX", b"private"), ("SHARED.TDX", b"private")],
                [("beta.tdx", b"private"), ("shared.tdx", b"private")],
            )
            result = candidates.scan_disc(root)

        self.assertEqual(
            result["totals"],
            {
                "levels_discovered": 1,
                "levels_scanned": 1,
                "manifest_cell_occurrences": 1,
                "vum_catalogs": 1,
                "vum_name_occurrences": 6,
                "material_records": 4,
                "dense_name_references": 9,
                "texture_containers": 2,
                "tdx_locator_occurrences": 4,
                "errors": 0,
            },
        )
        branch = exact_branch(result)
        self.assertEqual(
            branch["name_occurrences"],
            {
                "invalid_member_candidate": 1,
                "non_tdx_suffix": 1,
                "unmatched": 1,
                "unique_primary": 1,
                "unique_map": 1,
                "ambiguous_cross_class": 1,
            },
        )
        self.assertEqual(
            branch["dense_name_references"],
            {
                "invalid_member_candidate": 1,
                "non_tdx_suffix": 1,
                "unmatched": 1,
                "unique_primary": 4,
                "unique_map": 1,
                "ambiguous_cross_class": 1,
            },
        )
        self.assertEqual(
            branch["material_record_flags"],
            {
                "all_references_unique": 2,
                "any_unique": 2,
                "any_unmatched": 1,
                "any_ambiguous": 1,
                "any_ineligible": 1,
            },
        )
        self.assertEqual(
            branch["tdx_locator_occurrences"],
            {
                "reached_by_unique_candidate": 2,
                "reached_only_ambiguously": 2,
                "unreached": 0,
            },
        )
        self.assertEqual(
            result["maxima"],
            {
                "vum_names_per_catalog": 6,
                "material_records_per_catalog": 4,
                "dense_name_references_per_catalog": 9,
                "tdx_locators_per_level": 4,
                "candidate_locators_per_name": 2,
            },
        )

    def test_texture_collision_fails_closed_without_partial_level_totals(self) -> None:
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(
                root,
                "GOOD_A",
                [("CELL.VUM", vum)],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            write_level(
                root,
                "BAD_B",
                [("CELL.VUM", vum)],
                [("DUP.TDX", b"one"), ("dup.tdx", b"two")],
                [("MAP.TDX", b"two")],
            )
            result = candidates.scan_disc(root)

        self.assertEqual(result["totals"]["levels_discovered"], 2)
        self.assertEqual(result["totals"]["levels_scanned"], 1)
        self.assertEqual(result["totals"]["vum_catalogs"], 1)
        self.assertEqual(result["totals"]["tdx_locator_occurrences"], 2)
        self.assertEqual(result["totals"]["errors"], 1)
        self.assertEqual(result["error_categories"]["normalized_collision"], 1)

    def test_missing_duplicate_and_malformed_vum_are_typed_and_atomic(self) -> None:
        valid = make_vum(["ALPHA.TDX"], [(0,)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(
                root,
                "MISSING_A",
                [],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            write_level(
                root,
                "DUPLICATE_B",
                [("A.VUM", valid), ("B.VUM", valid)],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            write_level(
                root,
                "MALFORMED_C",
                [("CELL.VUM", b"not-vum")],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            result = candidates.scan_disc(root)

        self.assertEqual(result["totals"]["levels_discovered"], 3)
        self.assertEqual(result["totals"]["levels_scanned"], 0)
        self.assertEqual(result["totals"]["vum_catalogs"], 0)
        self.assertEqual(result["totals"]["tdx_locator_occurrences"], 0)
        self.assertEqual(result["totals"]["errors"], 3)
        self.assertEqual(result["error_categories"]["vum_missing"], 1)
        self.assertEqual(result["error_categories"]["vum_duplicate"], 1)
        self.assertEqual(result["error_categories"]["vum_malformed"], 1)

    def test_exact_vum_and_filesystem_limits_pass_and_one_below_fails(self) -> None:
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            write_level(
                root,
                "LIMIT_A",
                [("CELL.VUM", vum)],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            exact = candidates.scan_disc(
                root,
                replace(
                    candidates.ScanLimits(),
                    maximum_vum_bytes=len(vum),
                    maximum_filesystem_entries=5,
                ),
            )
            below_vum = candidates.scan_disc(
                root,
                replace(candidates.ScanLimits(), maximum_vum_bytes=len(vum) - 1),
            )
            with self.assertRaises(candidates.ScanFailure) as caught:
                candidates.scan_disc(
                    root,
                    replace(candidates.ScanLimits(), maximum_filesystem_entries=4),
                )

        self.assertEqual(exact["totals"]["levels_scanned"], 1)
        self.assertEqual(exact["totals"]["errors"], 0)
        self.assertEqual(below_vum["totals"]["levels_scanned"], 0)
        self.assertEqual(below_vum["error_categories"]["vum_limit_exceeded"], 1)
        self.assertEqual(caught.exception.category, "filesystem_limit")

    def test_missing_container_invalid_archive_name_and_pop_failure_are_private(self) -> None:
        secret = "PRIVATE_SECRET_MEMBER"
        vum = make_vum([f"{secret}.TDX"], [(0,)])
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            write_level(
                root,
                "MISSING_A",
                [("CELL.VUM", vum)],
                [(f"{secret}.TDX", b"one")],
                [],
                write_map=False,
            )
            write_level(
                root,
                "INVALID_B",
                [("CELL.VUM", vum)],
                [(f"../{secret}.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            write_level(
                root,
                "POP_C",
                [("CELL.VUM", vum)],
                [(f"{secret}.TDX", b"one")],
                [("MAP.TDX", b"two")],
                pop=b"bad",
            )
            result = candidates.scan_disc(root)

        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(str(root), encoded)
        self.assertEqual(result["totals"]["levels_scanned"], 0)
        self.assertEqual(result["totals"]["errors"], 3)
        self.assertEqual(result["error_categories"]["missing_texture_container"], 1)
        self.assertEqual(result["error_categories"]["archive_name_invalid"], 1)
        self.assertEqual(result["error_categories"]["pop_truncated"], 1)

    def test_fixed_schema_non_claims_and_cli_never_expose_identity(self) -> None:
        secret = "PRIVATE_SECRET_ALPHA"
        vum = make_vum([f"{secret}.TDX"], [(0,)])
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            write_level(
                root,
                "SECRET_LEVEL",
                [(f"{secret}.VUM", vum)],
                [(f"{secret}.TDX", b"private-payload")],
                [("MAP.TDX", b"private-payload")],
            )
            result = candidates.scan_disc(root)
            output = io.StringIO()
            errors = io.StringIO()
            with redirect_stdout(output), redirect_stderr(errors):
                exit_code = candidates.main([secret, f"{secret}_EXTRA"])

        encoded = json.dumps(result, sort_keys=True)
        self.assertNotIn(secret, encoded)
        self.assertNotIn(str(root), encoded)
        self.assertEqual(
            set(result),
            {
                "schema_version",
                "scope",
                "totals",
                "candidate_classes",
                "maxima",
                "error_categories",
                "measurement_gaps",
                "non_claims",
            },
        )
        self.assertEqual(set(result["totals"]), set(candidates.TOTAL_FIELDS))
        self.assertEqual(set(result["maxima"]), set(candidates.MAXIMUM_FIELDS))
        self.assertEqual(
            set(result["error_categories"]), set(candidates.ERROR_CATEGORIES)
        )
        self.assertEqual(
            result["measurement_gaps"],
            {key: 1 for key in candidates.MEASUREMENT_GAPS},
        )
        self.assertEqual(
            result["non_claims"], {key: 1 for key in candidates.NON_CLAIMS}
        )
        branch = exact_branch(result)
        self.assertEqual(
            set(branch),
            {
                "name_occurrences",
                "dense_name_references",
                "material_record_flags",
                "tdx_locator_occurrences",
            },
        )
        self.assertEqual(set(branch["name_occurrences"]), set(candidates.MATCH_STATUS_FIELDS))
        self.assertEqual(
            set(branch["dense_name_references"]), set(candidates.MATCH_STATUS_FIELDS)
        )
        self.assertNotIn("levels", result)
        self.assertEqual(exit_code, 1)
        self.assertEqual(errors.getvalue(), "")
        self.assertNotIn(secret, output.getvalue())
        cli_result = json.loads(output.getvalue())
        self.assertEqual(cli_result["error_categories"]["config"], 1)

    def test_reparse_like_discovery_fails_with_only_fixed_aggregate_output(self) -> None:
        secret = "PRIVATE_REPARSE_LEVEL"
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            write_level(
                root,
                secret,
                [("CELL.VUM", vum)],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            output = io.StringIO()
            errors = io.StringIO()
            with mock.patch.object(candidates, "_entry_is_link_like", return_value=True):
                with redirect_stdout(output), redirect_stderr(errors):
                    exit_code = candidates.main([str(root)])

        result = json.loads(output.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(errors.getvalue(), "")
        self.assertEqual(result["error_categories"]["unsafe_input"], 1)
        self.assertNotIn(secret, output.getvalue())

    def test_case_equivalent_filesystem_names_have_a_total_deterministic_order(self) -> None:
        expected = ["LEVEL", "Level", "level", "MAP", "map"]
        inputs = (
            ["level", "MAP", "Level", "map", "LEVEL"],
            list(reversed(expected)),
            ["map", "level", "MAP", "LEVEL", "Level"],
        )
        for values in inputs:
            with self.subTest(values=values):
                self.assertEqual(
                    sorted(values, key=candidates._filesystem_name_sort_key), expected
                )
        self.assertEqual(
            len({candidates._filesystem_name_sort_key(value) for value in expected}),
            len(expected),
        )

    def test_directory_identity_mismatch_and_concurrent_change_fail_closed(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            discovered = root / "DISCOVERED"
            replacement = root / "REPLACEMENT"
            discovered.mkdir()
            replacement.mkdir()
            discovered_stat = os.stat(discovered, follow_symlinks=False)
            budget = candidates.ScanBudget(candidates.ScanLimits())

            with self.assertRaises(candidates.ScanFailure) as mismatch:
                candidates._bounded_directory_entries(
                    replacement, budget, discovered_stat
                )
            self.assertEqual(mismatch.exception.category, "unsafe_input")
            self.assertEqual(budget.filesystem_entries, 0)

            original_scandir = candidates.os.scandir

            @contextmanager
            def mutate_after_enumeration(path: os.PathLike[str] | str):
                with original_scandir(path) as entries:
                    yield entries
                changed = Path(path) / "LATE_ENTRY"
                changed.write_bytes(b"bounded")
                before_ns = os.stat(path, follow_symlinks=False).st_mtime_ns
                os.utime(path, ns=(before_ns + 2_000_000_000,) * 2)

            stable_stat = os.stat(discovered, follow_symlinks=False)
            with mock.patch.object(
                candidates.os, "scandir", side_effect=mutate_after_enumeration
            ):
                with self.assertRaises(candidates.ScanFailure) as changed:
                    candidates._bounded_directory_entries(
                        discovered,
                        candidates.ScanBudget(candidates.ScanLimits()),
                        stable_stat,
                    )
            self.assertEqual(changed.exception.category, "unsafe_input")

    def test_directory_discovery_snapshot_change_fails_before_budget_consumption(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            level = root / "LEVEL"
            level.mkdir()
            discovered_stat = os.stat(level, follow_symlinks=False)
            os.utime(
                level,
                ns=(
                    discovered_stat.st_mtime_ns + 2_000_000_000,
                    discovered_stat.st_mtime_ns + 2_000_000_000,
                ),
            )
            changed_stat = os.stat(level, follow_symlinks=False)
            self.assertTrue(candidates._same_identity(discovered_stat, changed_stat))
            self.assertEqual(discovered_stat.st_mode, changed_stat.st_mode)
            self.assertNotEqual(
                discovered_stat.st_mtime_ns, changed_stat.st_mtime_ns
            )
            budget = candidates.ScanBudget(candidates.ScanLimits())

            with self.assertRaises(candidates.ScanFailure) as changed:
                candidates._bounded_directory_entries(
                    level, budget, discovered_stat
                )

            self.assertEqual(changed.exception.category, "unsafe_input")
            self.assertEqual(budget.filesystem_entries, 0)

    def test_level_directory_change_between_parent_discovery_and_child_use_is_private(
        self,
    ) -> None:
        secret = "PRIVATE_CHANGED_LEVEL_DIRECTORY"
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        original_bounded_entries = candidates._bounded_directory_entries

        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            write_level(
                root,
                secret,
                [("CELL.VUM", vum)],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            mutated = False

            def mutate_after_parent_discovery(
                path: Path,
                budget: candidates.ScanBudget,
                discovered_stat: os.stat_result,
            ) -> list[candidates._FilesystemEntry]:
                nonlocal mutated
                entries = original_bounded_entries(path, budget, discovered_stat)
                if path.name.upper() == "GAMEDATA" and not mutated:
                    level_entry = next(entry for entry in entries if entry.name == secret)
                    changed_time = (
                        level_entry.discovered_stat.st_mtime_ns + 2_000_000_000
                    )
                    os.utime(level_entry.path, ns=(changed_time, changed_time))
                    changed_stat = os.stat(
                        level_entry.path, follow_symlinks=False
                    )
                    self.assertTrue(
                        candidates._same_identity(
                            level_entry.discovered_stat, changed_stat
                        )
                    )
                    self.assertNotEqual(
                        level_entry.discovered_stat.st_mtime_ns,
                        changed_stat.st_mtime_ns,
                    )
                    mutated = True
                return entries

            output = io.StringIO()
            errors = io.StringIO()
            with mock.patch.object(
                candidates,
                "_bounded_directory_entries",
                mutate_after_parent_discovery,
            ):
                with redirect_stdout(output), redirect_stderr(errors):
                    exit_code = candidates.main([str(root)])

            result = json.loads(output.getvalue())
            self.assertTrue(mutated)
            self.assertEqual(exit_code, 1)
            self.assertEqual(errors.getvalue(), "")
            self.assert_private_atomic_unsafe_result(
                result, secret, root, levels_discovered=0
            )

    def test_discovery_to_open_replacement_for_each_level_file_fails_atomically(self) -> None:
        secret = "PRIVATE_REPLACED_LEVEL_INPUT"
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        target_names = ("DATA.POP", "DATA.HOG", "TEX.HOG", "MAPTEX.HOG")
        original_open_regular = candidates._open_regular

        for target_name in target_names:
            with self.subTest(target_name=target_name):
                with tempfile.TemporaryDirectory(prefix=secret) as directory:
                    root = Path(directory)
                    write_level(
                        root,
                        secret,
                        [("CELL.VUM", vum)],
                        [("ALPHA.TDX", b"one")],
                        [("MAP.TDX", b"two")],
                    )
                    replaced = False

                    @contextmanager
                    def replace_before_open(
                        source: candidates._StableFileInput,
                        missing_category: str,
                    ):
                        nonlocal replaced
                        if source.path.name.upper() == target_name and not replaced:
                            replacement = source.path.with_name(
                                f".{source.path.name}.replacement"
                            )
                            replacement.write_bytes(source.path.read_bytes())
                            os.utime(
                                replacement,
                                ns=(
                                    source.discovered_snapshot.modification_time_ns,
                                    source.discovered_snapshot.modification_time_ns,
                                ),
                            )
                            replacement_stat = os.stat(
                                replacement, follow_symlinks=False
                            )
                            self.assertNotEqual(
                                (
                                    replacement_stat.st_dev,
                                    replacement_stat.st_ino,
                                ),
                                (
                                    source.discovered_snapshot.device,
                                    source.discovered_snapshot.inode,
                                ),
                            )
                            os.replace(replacement, source.path)
                            replaced = True
                        with original_open_regular(
                            source, missing_category
                        ) as opened:
                            yield opened

                    with mock.patch.object(
                        candidates, "_open_regular", replace_before_open
                    ):
                        result = candidates.scan_disc(root)

                    self.assertTrue(replaced)
                    self.assert_private_atomic_unsafe_result(result, secret, root)

    def test_same_size_mutation_after_open_for_each_level_file_fails_atomically(self) -> None:
        secret = "PRIVATE_MUTATED_LEVEL_INPUT"
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        target_names = ("DATA.POP", "DATA.HOG", "TEX.HOG", "MAPTEX.HOG")
        original_open_regular = candidates._open_regular

        for target_name in target_names:
            with self.subTest(target_name=target_name):
                with tempfile.TemporaryDirectory(prefix=secret) as directory:
                    root = Path(directory)
                    write_level(
                        root,
                        secret,
                        [("CELL.VUM", vum)],
                        [("ALPHA.TDX", b"one")],
                        [("MAP.TDX", b"two")],
                    )
                    mutated = False

                    @contextmanager
                    def mutate_after_open(
                        source: candidates._StableFileInput,
                        missing_category: str,
                    ):
                        nonlocal mutated
                        with original_open_regular(
                            source, missing_category
                        ) as opened:
                            if source.path.name.upper() == target_name and not mutated:
                                with source.path.open("r+b") as writer:
                                    writer.seek(-1, os.SEEK_END)
                                    previous = writer.read(1)
                                    writer.seek(-1, os.SEEK_END)
                                    writer.write(bytes((previous[0] ^ 1,)))
                                    writer.flush()
                                    os.fsync(writer.fileno())
                                os.utime(
                                    source.path,
                                    ns=(
                                        source.discovered_snapshot.modification_time_ns
                                        + 2_000_000_000,
                                        source.discovered_snapshot.modification_time_ns
                                        + 2_000_000_000,
                                    ),
                                )
                                self.assertEqual(
                                    os.stat(
                                        source.path, follow_symlinks=False
                                    ).st_size,
                                    source.discovered_snapshot.size,
                                )
                                mutated = True
                            yield opened

                    with mock.patch.object(
                        candidates, "_open_regular", mutate_after_open
                    ):
                        result = candidates.scan_disc(root)

                    self.assertTrue(mutated)
                    self.assert_private_atomic_unsafe_result(result, secret, root)

    def test_real_directory_symlink_or_reparse_entry_fails_privately(self) -> None:
        secret = "PRIVATE_REAL_REPARSE_LEVEL"
        vum = make_vum(["ALPHA.TDX"], [(0,)])
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            target = write_level(
                root,
                "REAL_LEVEL",
                [("CELL.VUM", vum)],
                [("ALPHA.TDX", b"one")],
                [("MAP.TDX", b"two")],
            )
            link = root / "GAMEDATA" / secret
            try:
                link.symlink_to(target, target_is_directory=True)
            except (NotImplementedError, OSError) as exc:
                self.skipTest(f"directory symlink creation unavailable: {type(exc).__name__}")

            output = io.StringIO()
            errors = io.StringIO()
            with redirect_stdout(output), redirect_stderr(errors):
                exit_code = candidates.main([str(root)])

        result = json.loads(output.getvalue())
        self.assertEqual(exit_code, 1)
        self.assertEqual(errors.getvalue(), "")
        self.assertEqual(result["error_categories"]["unsafe_input"], 1)
        self.assertNotIn(secret, output.getvalue())
        self.assertNotIn(str(root), output.getvalue())

    def test_aggregate_overflow_is_typed(self) -> None:
        with self.assertRaises(candidates.ScanFailure) as caught:
            candidates._checked_add((1 << 64) - 1, 1)
        self.assertEqual(caught.exception.category, "aggregate_overflow")

    def test_aggregate_merge_overflow_is_transactional(self) -> None:
        aggregate = candidates.Aggregate()
        for index, field in enumerate(candidates.TOTAL_FIELDS):
            aggregate.totals[field] = index + 1
        for index, field in enumerate(candidates.MATCH_STATUS_FIELDS):
            aggregate.name_occurrences[field] = index + 11
            aggregate.dense_name_references[field] = index + 21
        for index, field in enumerate(candidates.MATERIAL_RECORD_FLAG_FIELDS):
            aggregate.material_record_flags[field] = index + 31
        for index, field in enumerate(candidates.LOCATOR_COVERAGE_FIELDS):
            aggregate.locator_coverage[field] = index + 41
        for index, field in enumerate(candidates.MAXIMUM_FIELDS):
            aggregate.maxima[field] = index + 51
        aggregate.errors["io"] = 61

        overflow_field = candidates.MATCH_STATUS_FIELDS[-1]
        aggregate.dense_name_references[overflow_field] = (1 << 64) - 1
        before = (
            dict(aggregate.totals),
            dict(aggregate.name_occurrences),
            dict(aggregate.dense_name_references),
            dict(aggregate.material_record_flags),
            dict(aggregate.locator_coverage),
            dict(aggregate.maxima),
            dict(aggregate.errors),
        )

        measured = candidates.LevelMeasurement()
        for field in candidates.TOTAL_FIELDS:
            measured.totals[field] = 1
        for field in candidates.MATCH_STATUS_FIELDS:
            measured.name_occurrences[field] = 1
            measured.dense_name_references[field] = 1
        measured.maxima[candidates.MAXIMUM_FIELDS[0]] = 999

        with self.assertRaises(candidates.ScanFailure) as caught:
            aggregate.merge_level(measured)
        self.assertEqual(caught.exception.category, "aggregate_overflow")
        self.assertEqual(
            (
                dict(aggregate.totals),
                dict(aggregate.name_occurrences),
                dict(aggregate.dense_name_references),
                dict(aggregate.material_record_flags),
                dict(aggregate.locator_coverage),
                dict(aggregate.maxima),
                dict(aggregate.errors),
            ),
            before,
        )


if __name__ == "__main__":
    unittest.main()
