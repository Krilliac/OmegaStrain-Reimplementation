from __future__ import annotations

import hashlib
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPOSITORY_ROOT))
from tools import assemble_scene_fragment as assembler  # noqa: E402


WIRE_FIXTURE = (
    REPOSITORY_ROOT
    / "analysis"
    / "fixtures"
    / "scene_fragment_v1"
    / "synthetic-producer.hex"
)
_TEST_CONFIG_DIGEST = hashlib.sha256(b"public synthetic scene configuration").digest()
_IDENTITY_MATRIX = (
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    1.0,
    0.0,
    0.0,
    0.0,
    0.0,
    1.0,
)


class _FragmentBuilder:
    def __init__(self) -> None:
        self.output = bytearray()
        self.offsets: dict[str, int] = {}

    def mark(self, name: str) -> None:
        self.offsets[name] = len(self.output)

    def raw(self, value: bytes) -> None:
        self.output.extend(value)

    def u8(self, value: int, name: str | None = None) -> None:
        if name is not None:
            self.mark(name)
        self.output.extend(struct.pack("<B", value))

    def u32(self, value: int, name: str | None = None) -> None:
        if name is not None:
            self.mark(name)
        self.output.extend(struct.pack("<I", value))

    def u64(self, value: int, name: str | None = None) -> None:
        if name is not None:
            self.mark(name)
        self.output.extend(struct.pack("<Q", value))

    def f32(self, value: float, name: str | None = None) -> None:
        if name is not None:
            self.mark(name)
        self.output.extend(struct.pack("<f", value))

    def f64(self, value: float, name: str | None = None) -> None:
        if name is not None:
            self.mark(name)
        self.output.extend(struct.pack("<d", value))

    def optional_range(
        self,
        present: bool,
        minimum: float,
        maximum: float,
        name: str,
    ) -> None:
        self.u8(1 if present else 0, f"{name}.present")
        self.f64(minimum, f"{name}.minimum")
        self.f64(maximum, f"{name}.maximum")


def build_valid_fragment(
    *,
    digest: bytes = _TEST_CONFIG_DIGEST,
    camera_samples: tuple[tuple[int, int, float], ...] | None = None,
    edges: tuple[tuple[int, int], ...] = ((1, 1),),
) -> tuple[bytes, dict[str, int]]:
    if camera_samples is None:
        camera_samples = tuple(
            (1, component, value)
            for component, value in enumerate(_IDENTITY_MATRIX, start=1)
        )
    counts = (
        1,  # frames
        1,  # spans
        1,  # sites
        1,  # records
        16,  # camera components
        1,  # VIF destination extents
        1,  # INL rows
        1,  # VIF rows
        len(camera_samples),
        1,  # GS states
        1,  # draws
        len(edges),
    )

    builder = _FragmentBuilder()
    builder.raw(assembler.FRAGMENT_MAGIC)
    builder.u32(assembler.FRAGMENT_VERSION, "version")
    builder.mark("config_digest")
    builder.raw(digest)
    for index, count in enumerate(counts):
        builder.u32(count, f"count.{index}")

    builder.u32(1, "frame.id")
    builder.u32(1, "span.id")

    builder.u32(1, "site.id")
    builder.u32(1, "site.span")
    builder.u32(4, "site.width")

    builder.u32(1, "record.id")
    builder.u32(1, "record.span")
    builder.u32(8, "record.size")

    for component in range(1, 17):
        builder.u32(component, f"camera_component.{component}.id")

    builder.u32(1, "vif_destination.id")

    builder.u32(1, "inl.frame")
    builder.u32(1, "inl.site")
    builder.u32(1, "inl.record")
    builder.u32(0, "inl.offset")
    builder.u32(4, "inl.width")
    builder.u64(1, "inl.occurrences")

    builder.u32(1, "vif.frame")
    builder.u32(1, "vif.record")
    builder.u32(0, "vif.source_offset")
    builder.u32(1, "vif.word_count")
    builder.u32(1, "vif.remaining")
    builder.u32(0, "vif.prefix")
    builder.u32(1, "vif.destination")
    builder.u8(0, "vif.mode")
    builder.u32(1, "vif.cl")
    builder.u32(1, "vif.wl")
    builder.u8(0, "vif.cycle_class")
    builder.u8(4, "vif.components")
    builder.u8(32, "vif.bits")
    builder.u8(0, "vif.masked")
    builder.u8(0, "vif.unsigned")
    builder.u64(1, "vif.occurrences")

    for sample_index, (frame, component, value) in enumerate(camera_samples):
        builder.u32(frame, f"camera.{sample_index}.frame")
        builder.u32(component, f"camera.{sample_index}.component")
        builder.f32(value, f"camera.{sample_index}.value")

    builder.u32(1, "state.id")

    builder.u32(1, "draw.id")
    builder.u32(1, "draw.frame")
    builder.u32(1, "draw.state")
    builder.u8(0, "draw.disposition")
    builder.u64(3, "draw.index_count")
    builder.u64(3, "draw.unique_count")
    for primitive_index, count in enumerate((0, 0, 0, 1, 0, 0, 0)):
        builder.u64(count, f"draw.primitive.{primitive_index}")
    builder.u8(0, "draw.texture_kind")
    builder.optional_range(True, -1.0, 1.0, "draw.x")
    builder.optional_range(True, -1.0, 1.0, "draw.y")
    builder.optional_range(True, 0.0, 1.0, "draw.depth")
    builder.optional_range(False, 0.0, 0.0, "draw.texture_first")
    builder.optional_range(False, 0.0, 0.0, "draw.texture_second")
    builder.optional_range(False, 0.0, 0.0, "draw.texture_q")

    for edge_index, (record, draw) in enumerate(edges):
        builder.u32(record, f"edge.{edge_index}.record")
        builder.u32(draw, f"edge.{edge_index}.draw")
    return bytes(builder.output), builder.offsets


def mutate_u8(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<B", result, offset, value)
    return bytes(result)


def mutate_u32(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def mutate_u64(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<Q", result, offset, value)
    return bytes(result)


def run_main(args: list[str]) -> tuple[int, str, str]:
    output = io.StringIO()
    errors = io.StringIO()
    with redirect_stdout(output), redirect_stderr(errors):
        code = assembler.main(args)
    return code, output.getvalue(), errors.getvalue()


class SceneFragmentParserTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, self.offsets = build_valid_fragment()

    def assert_invalid(self, raw: bytes) -> None:
        with self.assertRaises(assembler.SceneFragmentValidationError):
            assembler.parse_scene_fragment(raw)

    def test_valid_fragment_parses_and_discards_provenance_tables(self) -> None:
        parsed = assembler.parse_scene_fragment(self.raw)
        self.assertEqual(parsed.runtime_config_digest, _TEST_CONFIG_DIGEST)
        self.assertEqual(parsed.camera_component_count, 16)
        self.assertEqual(parsed.complete_camera_frames, ((1, _IDENTITY_MATRIX),))
        self.assertEqual(len(self.raw), 670)

    def test_cpp_producer_wire_golden_is_the_parser_fixture(self) -> None:
        encoded = WIRE_FIXTURE.read_bytes()
        self.assertEqual(encoded[-1:], b"\n")
        self.assertNotIn(b"\r", encoded)
        golden = bytes.fromhex(encoded.decode("ascii"))
        self.assertEqual(golden, self.raw)
        self.assertEqual(
            assembler.parse_scene_fragment(golden).complete_camera_frames,
            ((1, _IDENTITY_MATRIX),),
        )

    def test_magic_version_digest_and_exact_length_fail_closed(self) -> None:
        self.assert_invalid(b"X" + self.raw[1:])
        self.assert_invalid(mutate_u32(self.raw, self.offsets["version"], 2))
        zero_digest = bytearray(self.raw)
        begin = self.offsets["config_digest"]
        zero_digest[begin : begin + 32] = bytes(32)
        self.assert_invalid(bytes(zero_digest))
        self.assert_invalid(self.raw[:-1])
        self.assert_invalid(self.raw + b"X")
        self.assert_invalid(b"")
        self.assert_invalid(b" " * (assembler.MAX_FRAGMENT_BYTES + 1))
        with self.assertRaises(assembler.SceneFragmentValidationError):
            assembler.parse_scene_fragment(bytearray(self.raw))  # type: ignore[arg-type]

    def test_counts_are_bounded_before_rows_are_allocated(self) -> None:
        too_many_frames = mutate_u32(
            self.raw, self.offsets["count.0"], assembler.MAX_FRAMES + 1
        )
        self.assert_invalid(too_many_frames)
        no_frames = mutate_u32(self.raw, self.offsets["count.0"], 0)
        self.assert_invalid(no_frames)
        too_many_camera_samples = mutate_u32(
            self.raw, self.offsets["count.8"], 17
        )
        self.assert_invalid(too_many_camera_samples)
        too_many_vif_destinations = mutate_u32(
            self.raw, self.offsets["count.5"], 2
        )
        self.assert_invalid(too_many_vif_destinations)

    def test_descriptors_require_dense_ids_valid_references_and_widths(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["frame.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["span.id"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["site.span"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["site.width"], 3))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["record.span"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["record.size"], 0))
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["camera_component.2.id"], 1)
        )
        self.assert_invalid(mutate_u32(self.raw, self.offsets["state.id"], 2))

    def test_inl_rows_validate_descriptors_extent_occurrences_and_uniqueness(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["inl.frame"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["inl.site"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["inl.record"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["inl.width"], 8))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["inl.offset"], 8))
        self.assert_invalid(mutate_u64(self.raw, self.offsets["inl.occurrences"], 0))

    def test_vif_rows_validate_all_bounded_metadata(self) -> None:
        mutations = (
            ("vif.frame", 0, mutate_u32),
            ("vif.record", 0, mutate_u32),
            ("vif.destination", 0, mutate_u32),
            ("vif.word_count", 0, mutate_u32),
            ("vif.remaining", 0, mutate_u32),
            ("vif.prefix", 16, mutate_u32),
            ("vif.mode", 3, mutate_u8),
            ("vif.cl", 257, mutate_u32),
            ("vif.wl", 257, mutate_u32),
            ("vif.cycle_class", 3, mutate_u8),
            ("vif.components", 0, mutate_u8),
            ("vif.components", 5, mutate_u8),
            ("vif.bits", 7, mutate_u8),
            ("vif.masked", 2, mutate_u8),
            ("vif.unsigned", 2, mutate_u8),
            ("vif.occurrences", 0, mutate_u64),
        )
        for name, value, mutate in mutations:
            with self.subTest(name=name, value=value):
                self.assert_invalid(mutate(self.raw, self.offsets[name], value))

        skip_contradiction = mutate_u8(
            self.raw, self.offsets["vif.cycle_class"], 2
        )
        self.assert_invalid(skip_contradiction)
        packed_width = mutate_u8(self.raw, self.offsets["vif.bits"], 5)
        packed_width = mutate_u8(
            packed_width, self.offsets["vif.components"], 3
        )
        self.assert_invalid(packed_width)

    def test_vif_source_extent_must_fit_its_anonymous_record(self) -> None:
        for name, value in (
            ("vif.source_offset", 4),
            ("vif.word_count", 2),
            ("vif.prefix", 4),
        ):
            with self.subTest(name=name, value=value, boundary="exact"):
                assembler.parse_scene_fragment(
                    mutate_u32(self.raw, self.offsets[name], value)
                )

        for name, value in (
            ("vif.source_offset", 5),
            ("vif.word_count", 3),
            ("vif.prefix", 5),
        ):
            with self.subTest(name=name, value=value, boundary="outside"):
                self.assert_invalid(
                    mutate_u32(self.raw, self.offsets[name], value)
                )

    def test_camera_rows_require_unique_finite_canonical_values(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["camera.0.frame"], 0))
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["camera.0.component"], 17)
        )
        duplicate = mutate_u32(
            self.raw, self.offsets["camera.1.component"], 1
        )
        self.assert_invalid(duplicate)
        nan_value = mutate_u32(
            self.raw, self.offsets["camera.0.value"], 0x7FC00000
        )
        self.assert_invalid(nan_value)
        negative_zero = mutate_u32(
            self.raw, self.offsets["camera.1.value"], 0x80000000
        )
        self.assert_invalid(negative_zero)

    def test_draws_validate_counts_state_and_derived_ranges(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.frame"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.state"], 0))
        self.assert_invalid(
            mutate_u8(self.raw, self.offsets["draw.disposition"], 2)
        )
        self.assert_invalid(
            mutate_u64(self.raw, self.offsets["draw.index_count"], 4)
        )
        self.assert_invalid(
            mutate_u64(self.raw, self.offsets["draw.unique_count"], 4)
        )
        self.assert_invalid(
            mutate_u64(self.raw, self.offsets["draw.primitive.3"], 0)
        )
        self.assert_invalid(
            mutate_u8(self.raw, self.offsets["draw.texture_kind"], 3)
        )
        self.assert_invalid(mutate_u8(self.raw, self.offsets["draw.x.present"], 0))
        self.assert_invalid(
            mutate_u64(self.raw, self.offsets["draw.y.minimum"], 0x7FF0000000000000)
        )
        self.assert_invalid(
            mutate_u64(self.raw, self.offsets["draw.depth.minimum"], 0x8000000000000000)
        )
        retained_absent = mutate_u64(
            self.raw,
            self.offsets["draw.texture_first.minimum"],
            struct.unpack("<Q", struct.pack("<d", 1.0))[0],
        )
        self.assert_invalid(retained_absent)

    def test_membership_edges_require_valid_unique_ordered_references(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.record"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.draw"], 0))
        duplicate_raw, _ = build_valid_fragment(edges=((1, 1), (1, 1)))
        self.assert_invalid(duplicate_raw)


class SceneAssemblerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, self.offsets = build_valid_fragment()

    def test_single_and_repeat_output_remain_explicitly_anonymous(self) -> None:
        expected = {
            "schema": assembler.SCENE_OBSERVATION_SCHEMA,
            "camera": {"anonymous_components": list(_IDENTITY_MATRIX)},
            "material_buckets": [],
            "render_meshes": [],
            "mesh_instances": [],
        }
        for fragments in ([self.raw], [self.raw, self.raw]):
            with self.subTest(fragment_count=len(fragments)):
                document = assembler.assemble_scene_document(fragments)
                self.assertEqual(document, expected)
                self.assertEqual(
                    assembler.encode_scene_document(document),
                    json.dumps(
                        expected,
                        ensure_ascii=True,
                        allow_nan=False,
                        separators=(",", ":"),
                    ).encode("ascii")
                    + b"\n",
                )
                rendered = assembler.encode_scene_document(document)
                self.assertNotIn(b"world_to_clip", rendered)
                self.assertNotIn(b"matrix", rendered)
                self.assertNotIn(b"transform", rendered)
                self.assertNotIn(_TEST_CONFIG_DIGEST.hex().encode("ascii"), rendered)

    def test_expected_digest_and_repeat_bytes_are_verified_but_never_emitted(self) -> None:
        expected_hash = hashlib.sha256(self.raw).hexdigest()
        document = assembler.assemble_scene_document(
            [self.raw, self.raw],
            expected_fragment_sha256=expected_hash.upper(),
        )
        rendered = assembler.encode_scene_document(document).decode("ascii")
        self.assertNotIn(expected_hash, rendered)
        self.assertNotIn(_TEST_CONFIG_DIGEST.hex(), rendered)

        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document(
                [self.raw],
                expected_fragment_sha256="0" * 64,
            )
        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document(
                [self.raw],
                expected_fragment_sha256="bad",
            )

        changed = bytearray(self.raw)
        changed[self.offsets["config_digest"]] ^= 1
        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document(
                [self.raw, bytes(changed)],
            )

    def test_complete_camera_and_fragment_count_are_required(self) -> None:
        incomplete_samples = tuple(
            (1, component, value)
            for component, value in enumerate(_IDENTITY_MATRIX[:-1], start=1)
        )
        incomplete, _ = build_valid_fragment(camera_samples=incomplete_samples)
        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document([incomplete])
        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document([])
        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document(
                [self.raw] * (assembler.MAX_FRAGMENTS + 1)
            )

    def test_input_collection_requires_immutable_fragment_elements(self) -> None:
        for value in (self.raw, "not-a-sequence"):
            with self.subTest(value_type=type(value).__name__):
                with self.assertRaises(assembler.SceneAssemblyError):
                    assembler.assemble_scene_document(value)  # type: ignore[arg-type]
        with self.assertRaises(assembler.SceneAssemblyError):
            assembler.assemble_scene_document(  # type: ignore[list-item]
                [bytearray(self.raw)]
            )

    def test_output_validator_rejects_noncanonical_or_fabricated_values(self) -> None:
        document = assembler.assemble_scene_document([self.raw])
        assembler.validate_scene_document(document)
        for mutation in (
            {**document, "extra": None},
            {**document, "schema": "future"},
            {**document, "camera": {"world_to_clip": [0.0] * 16}},
            {**document, "camera": {"anonymous_components": [0.0] * 64 + [1.0]}},
            {**document, "camera": {"anonymous_components": [float("inf")]}},
            {**document, "camera": {"anonymous_components": [-0.0]}},
            {**document, "render_meshes": [{}]},
            {**document, "material_buckets": [{}]},
            {**document, "mesh_instances": [{}]},
        ):
            with self.subTest(mutation=mutation):
                with self.assertRaises(assembler.SceneAssemblyError):
                    assembler.validate_scene_document(mutation)

    def test_output_contains_no_private_provenance_surfaces(self) -> None:
        document = assembler.assemble_scene_document([self.raw])
        rendered = assembler.encode_scene_document(document).decode("ascii").lower()
        for forbidden in (
            "digest",
            "hash",
            "address",
            "site",
            "span",
            "offset",
            "packet",
            "register",
            "payload",
            "provenance",
            "record",
            "gs_state",
            "disc",
            "executable",
            "path",
        ):
            self.assertNotIn(forbidden, rendered)


class SceneAssemblerCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, _ = build_valid_fragment()

    def test_help_argument_and_fixed_failure_diagnostics(self) -> None:
        for help_argument in ("-h", "--help"):
            with self.subTest(help_argument=help_argument):
                code, output, errors = run_main([help_argument])
                self.assertEqual(code, 0)
                self.assertNotIn("camera-attestation", output)
                self.assertEqual(errors, "")
        for args in ([], ["--unknown"], ["--camera-attestation", "private.json"]):
            with self.subTest(args=args):
                code, output, errors = run_main(args)
                self.assertEqual(code, 1)
                self.assertEqual(output, "")
                self.assertEqual(errors, "Scene fragment assembly failed.\n")

    def test_short_output_write_is_left_in_place_without_path_unlink(self) -> None:
        class ShortWriter:
            def __enter__(self) -> ShortWriter:
                return self

            def __exit__(self, *_args: object) -> None:
                return None

            def write(self, payload: bytes) -> int:
                return max(0, len(payload) - 1)

            def flush(self) -> None:
                return None

        with mock.patch("builtins.open", return_value=ShortWriter()), mock.patch(
            "pathlib.Path.unlink"
        ) as unlink:
            with self.assertRaises(OSError):
                assembler._write_new_file("synthetic-output", b"payload")
        unlink.assert_not_called()

    def test_cli_writes_new_canonical_file_and_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fragment = Path(directory) / "input.part"
            output = Path(directory) / "scene.json"
            fragment.write_bytes(self.raw)
            args = [
                "--expected-sha256",
                hashlib.sha256(self.raw).hexdigest(),
                "--output",
                str(output),
                str(fragment),
                str(fragment),
            ]
            code, stdout, stderr = run_main(args)
            self.assertEqual(code, 0)
            self.assertEqual(stdout, "Scene fragment assembly succeeded.\n")
            self.assertEqual(stderr, "")
            expected = assembler.encode_scene_document(
                assembler.assemble_scene_document([self.raw])
            )
            self.assertEqual(output.read_bytes(), expected)

            code, stdout, stderr = run_main(args)
            self.assertEqual(code, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "Scene fragment assembly failed.\n")
            self.assertEqual(output.read_bytes(), expected)

    def test_cli_never_echoes_private_paths_or_internal_errors(self) -> None:
        secret = "ZXQ_PRIVATE_SCENE_PATH_731"
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            fragment = Path(directory) / f"{secret}.part"
            output = Path(directory) / "scene.json"
            fragment.write_bytes(b"not-a-fragment")
            code, stdout, stderr = run_main(
                [
                    "--output",
                    str(output),
                    str(fragment),
                ]
            )
        self.assertEqual(code, 1)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "Scene fragment assembly failed.\n")
        self.assertNotIn(secret, stdout + stderr)

        with mock.patch.object(
            assembler,
            "assemble_scene_document",
            side_effect=RuntimeError(f"{secret}: internal private detail"),
        ):
            with tempfile.TemporaryDirectory() as directory:
                fragment = Path(directory) / "input.part"
                output = Path(directory) / "scene.json"
                fragment.write_bytes(self.raw)
                code, stdout, stderr = run_main(
                    [
                        "--output",
                        str(output),
                        str(fragment),
                    ]
                )
        self.assertEqual(code, 1)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "Scene fragment assembly failed.\n")
        self.assertNotIn(secret, stdout + stderr)

    def test_real_process_runs_in_isolated_mode_from_unrelated_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fragment = Path(directory) / "input.part"
            output = Path(directory) / "scene.json"
            fragment.write_bytes(self.raw)
            completed = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    "-E",
                    "-s",
                    "-S",
                    "-B",
                    str(REPOSITORY_ROOT / "tools" / "assemble_scene_fragment.py"),
                    "--output",
                    str(output),
                    str(fragment),
                ],
                cwd=directory,
                env={**os.environ, "PYTHONPATH": str(Path(directory) / "hostile")},
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0)
            self.assertEqual(completed.stdout, b"Scene fragment assembly succeeded.\n")
            self.assertEqual(completed.stderr, b"")
            self.assertEqual(
                json.loads(output.read_text(encoding="ascii"))["schema"],
                assembler.SCENE_OBSERVATION_SCHEMA,
            )


if __name__ == "__main__":
    unittest.main()
