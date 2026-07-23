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
from tools import assemble_frontend_phase as assembler  # noqa: E402


WIRE_FIXTURE = (
    REPOSITORY_ROOT
    / "analysis"
    / "fixtures"
    / "frontend_phase_v1"
    / "synthetic-producer.hex"
)
_TEST_CONFIG_DIGEST = hashlib.sha256(b"public synthetic frontend phase configuration").digest()
_DEFAULT_EDGES = ((1, 1), (2, 1), (2, 2))


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


def build_valid_fragment(
    *,
    digest: bytes = _TEST_CONFIG_DIGEST,
    edges: tuple[tuple[int, int], ...] = _DEFAULT_EDGES,
) -> tuple[bytes, dict[str, int]]:
    """Two invocations (root + nested child), each with an Enter/Exit event
    pair, two draws, and (by default) three phase/draw edges: the root phase
    contributes to draw 1, and the nested child phase contributes to both
    draw 1 and draw 2."""

    counts = (
        2,  # invocations
        4,  # events
        2,  # draws
        len(edges),
    )

    builder = _FragmentBuilder()
    builder.raw(assembler.FRAGMENT_MAGIC)
    builder.u32(assembler.FRAGMENT_VERSION, "version")
    builder.mark("config_digest")
    builder.raw(digest)
    for index, count in enumerate(counts):
        builder.u32(count, f"count.{index}")

    # Invocation 1: root, spans events 1 (enter) and 4 (exit).
    builder.u32(1, "invocation.0.id")
    builder.u32(1, "invocation.0.frame")
    builder.u32(0, "invocation.0.parent")
    builder.u32(1, "invocation.0.enter")
    builder.u32(4, "invocation.0.exit")

    # Invocation 2: nested under 1, spans events 2 (enter) and 3 (exit).
    builder.u32(2, "invocation.1.id")
    builder.u32(2, "invocation.1.frame")
    builder.u32(1, "invocation.1.parent")
    builder.u32(2, "invocation.1.enter")
    builder.u32(3, "invocation.1.exit")

    builder.u32(1, "event.0.id")
    builder.u32(1, "event.0.frame")
    builder.u32(1, "event.0.invocation")
    builder.u8(0, "event.0.closed_kind")
    builder.u32(0, "event.0.reserved")

    builder.u32(2, "event.1.id")
    builder.u32(2, "event.1.frame")
    builder.u32(2, "event.1.invocation")
    builder.u8(0, "event.1.closed_kind")
    builder.u32(0, "event.1.reserved")

    builder.u32(3, "event.2.id")
    builder.u32(3, "event.2.frame")
    builder.u32(2, "event.2.invocation")
    builder.u8(1, "event.2.closed_kind")
    builder.u32(0, "event.2.reserved")

    builder.u32(4, "event.3.id")
    builder.u32(4, "event.3.frame")
    builder.u32(1, "event.3.invocation")
    builder.u8(1, "event.3.closed_kind")
    builder.u32(0, "event.3.reserved")

    builder.u32(1, "draw.0.id")
    builder.u32(2, "draw.0.frame")
    builder.u8(0, "draw.0.disposition")
    builder.u32(0, "draw.0.reserved")

    builder.u32(2, "draw.1.id")
    builder.u32(3, "draw.1.frame")
    builder.u8(0, "draw.1.disposition")
    builder.u32(0, "draw.1.reserved")

    for index, (event, draw) in enumerate(edges):
        builder.u32(event, f"edge.{index}.event")
        builder.u32(draw, f"edge.{index}.draw")

    return bytes(builder.output), builder.offsets


def mutate_u8(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<B", result, offset, value)
    return bytes(result)


def mutate_u32(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def run_main(args: list[str]) -> tuple[int, str, str]:
    output = io.StringIO()
    errors = io.StringIO()
    with redirect_stdout(output), redirect_stderr(errors):
        code = assembler.main(args)
    return code, output.getvalue(), errors.getvalue()


class PhaseFragmentParserTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, self.offsets = build_valid_fragment()

    def assert_invalid(self, raw: bytes) -> None:
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(raw)

    def test_valid_fragment_parses_and_discards_event_and_draw_detail(self) -> None:
        parsed = assembler.parse_phase_fragment(self.raw)
        self.assertEqual(parsed.runtime_config_digest, _TEST_CONFIG_DIGEST)
        self.assertEqual(parsed.invocation_parents, (0, 1))
        self.assertEqual(parsed.draw_count, 2)
        self.assertEqual(parsed.skipped_draw_count, 0)
        self.assertEqual(parsed.phase_draw_edges, ((1, 1), (2, 1), (2, 2)))
        self.assertEqual(len(self.raw), 226)

    def test_checked_in_fixture_matches_the_python_reference_builder(self) -> None:
        encoded = WIRE_FIXTURE.read_bytes()
        self.assertEqual(encoded[-1:], b"\n")
        self.assertNotIn(b"\r", encoded)
        golden = bytes.fromhex(encoded.decode("ascii"))
        self.assertEqual(golden, self.raw)
        self.assertEqual(
            assembler.parse_phase_fragment(golden).phase_draw_edges,
            ((1, 1), (2, 1), (2, 2)),
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
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(bytearray(self.raw))  # type: ignore[arg-type]

    def test_counts_are_bounded_before_rows_are_allocated(self) -> None:
        too_many_invocations = mutate_u32(
            self.raw, self.offsets["count.0"], assembler.MAX_INVOCATIONS + 1
        )
        self.assert_invalid(too_many_invocations)
        no_invocations = mutate_u32(self.raw, self.offsets["count.0"], 0)
        self.assert_invalid(no_invocations)
        mismatched_events = mutate_u32(self.raw, self.offsets["count.1"], 5)
        self.assert_invalid(mismatched_events)
        too_many_edges = mutate_u32(self.raw, self.offsets["count.3"], assembler.MAX_EDGES + 1)
        self.assert_invalid(too_many_edges)

    def test_invocations_require_dense_ids_bounded_frame_and_valid_parent(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.id"], 2))
        # Own dense id is 2; a parent of 2 is a self-reference, and anything
        # greater is a forward reference. Both must fail the same way.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.parent"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.frame"], 0))
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["invocation.0.frame"], assembler.MAX_FRAMES + 1)
        )
        # Raising invocation 1's frame above invocation 2's breaks the
        # non-decreasing rule across the table.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.frame"], 5))

    def test_invocation_enter_exit_must_be_present_and_ordered(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.enter"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.exit"], 0))
        # Invocation 2's enter (2) and exit (3) are both in-bounds event IDs;
        # collapsing exit onto enter violates ordering without touching bounds.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.exit"], 2))

    def test_events_validate_reference_bounds_kind_and_reserved(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.invocation"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.invocation"], 3))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["event.0.closed_kind"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.reserved"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.frame"], 0))
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["event.0.frame"], assembler.MAX_FRAMES + 1)
        )
        # Raising event 1's frame above event 2's breaks non-decreasing order.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.frame"], 3))

    def test_cross_check_requires_matching_invocation_kind_and_frame(self) -> None:
        # Invocation 1's enter (event 1) must name invocation 1 with Enter kind.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.invocation"], 2))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["event.0.closed_kind"], 1))
        # Invocation 1's exit (event 4) must name invocation 1 with Exit kind.
        self.assert_invalid(mutate_u8(self.raw, self.offsets["event.3.closed_kind"], 0))
        # Invocation 1's own frame (1) must equal its enter event's frame.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.frame"], 2))

    def test_draws_validate_dense_id_frame_disposition_and_reserved(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.frame"], 0))
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["draw.0.frame"], assembler.MAX_FRAMES + 1)
        )
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.frame"], 5))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["draw.0.disposition"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.reserved"], 1))

    def test_edges_require_enter_kind_valid_bounds_order_and_uniqueness(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.event"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.event"], 5))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.draw"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.draw"], 3))
        # Event 4 is invocation 1's Exit event; edges may only name Enter events.
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.0.event"], 4))

        out_of_order, _ = build_valid_fragment(edges=((2, 2), (1, 1)))
        self.assert_invalid(out_of_order)
        duplicate, _ = build_valid_fragment(edges=((1, 1), (2, 1), (2, 1)))
        self.assert_invalid(duplicate)

    def test_required_descriptor_tables_cannot_be_empty(self) -> None:
        # Zero edges (and therefore zero attribution) is rejected at the count
        # header, matching the other three required tables.
        empty_counts = mutate_u32(self.raw, self.offsets["count.3"], 0)
        self.assert_invalid(empty_counts)


class PhaseAssemblerTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, self.offsets = build_valid_fragment()

    def test_single_and_repeat_output_match_expected_document(self) -> None:
        expected = {
            "schema": assembler.PHASE_OBSERVATION_SCHEMA,
            "invocation_parents": [0, 1],
            "draw_count": 2,
            "skipped_draw_count": 0,
            "phase_draw_edges": [[1, 1], [2, 1], [2, 2]],
        }
        for fragments in ([self.raw], [self.raw, self.raw]):
            with self.subTest(fragment_count=len(fragments)):
                document = assembler.assemble_phase_document(fragments)
                self.assertEqual(document, expected)
                self.assertEqual(
                    assembler.encode_phase_document(document),
                    json.dumps(
                        expected,
                        ensure_ascii=True,
                        allow_nan=False,
                        separators=(",", ":"),
                    ).encode("ascii")
                    + b"\n",
                )
                self.assertNotIn(
                    _TEST_CONFIG_DIGEST.hex().encode("ascii"),
                    assembler.encode_phase_document(document),
                )

    def test_expected_digest_and_repeat_bytes_are_verified_but_never_emitted(self) -> None:
        expected_hash = hashlib.sha256(self.raw).hexdigest()
        document = assembler.assemble_phase_document(
            [self.raw, self.raw],
            expected_fragment_sha256=expected_hash.upper(),
        )
        rendered = assembler.encode_phase_document(document).decode("ascii")
        self.assertNotIn(expected_hash, rendered)
        self.assertNotIn(_TEST_CONFIG_DIGEST.hex(), rendered)

        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_document(
                [self.raw],
                expected_fragment_sha256="0" * 64,
            )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_document(
                [self.raw],
                expected_fragment_sha256="bad",
            )

        changed = bytearray(self.raw)
        changed[self.offsets["config_digest"]] ^= 1
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_document([self.raw, bytes(changed)])

    def test_fragment_count_bounds_are_enforced(self) -> None:
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_document([])
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_document([self.raw] * (assembler.MAX_FRAGMENTS + 1))

    def test_input_collection_requires_immutable_fragment_elements(self) -> None:
        for value in (self.raw, "not-a-sequence"):
            with self.subTest(value_type=type(value).__name__):
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler.assemble_phase_document(value)  # type: ignore[arg-type]
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_document([bytearray(self.raw)])  # type: ignore[list-item]

    def test_output_validator_rejects_noncanonical_or_fabricated_values(self) -> None:
        document = assembler.assemble_phase_document([self.raw])
        assembler.validate_phase_document(document)
        for mutation in (
            {**document, "extra": None},
            {**document, "schema": "future"},
            {**document, "invocation_parents": []},
            {**document, "invocation_parents": [0, 2]},
            {**document, "draw_count": 0},
            {**document, "draw_count": True},
            {**document, "skipped_draw_count": 3},
            {**document, "phase_draw_edges": []},
            {**document, "phase_draw_edges": [[1, 1], [1, 1]]},
            {**document, "phase_draw_edges": [[2, 1], [1, 1]]},
            {**document, "phase_draw_edges": [[3, 1]]},
            {**document, "phase_draw_edges": [[1, 3]]},
            {**document, "phase_draw_edges": [[1, 1, 1]]},
        ):
            with self.subTest(mutation=mutation):
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler.validate_phase_document(mutation)

    def test_output_contains_no_private_provenance_surfaces(self) -> None:
        document = assembler.assemble_phase_document([self.raw])
        rendered = assembler.encode_phase_document(document).decode("ascii").lower()
        for forbidden in (
            "digest",
            "hash",
            "address",
            "frame",
            "event",
            "offset",
            "packet",
            "register",
            "payload",
            "provenance",
            "disc",
            "executable",
            "path",
        ):
            self.assertNotIn(forbidden, rendered)


class PhaseAssemblerCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, _ = build_valid_fragment()

    def test_help_argument_and_fixed_failure_diagnostics(self) -> None:
        for help_argument in ("-h", "--help"):
            with self.subTest(help_argument=help_argument):
                code, output, errors = run_main([help_argument])
                self.assertEqual(code, 0)
                self.assertEqual(errors, "")
        for args in ([], ["--unknown"], ["--expected-sha256"]):
            with self.subTest(args=args):
                code, output, errors = run_main(args)
                self.assertEqual(code, 1)
                self.assertEqual(output, "")
                self.assertEqual(errors, "Phase fragment assembly failed.\n")

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
            output = Path(directory) / "phase.json"
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
            self.assertEqual(stdout, "Phase fragment assembly succeeded.\n")
            self.assertEqual(stderr, "")
            expected = assembler.encode_phase_document(
                assembler.assemble_phase_document([self.raw])
            )
            self.assertEqual(output.read_bytes(), expected)

            code, stdout, stderr = run_main(args)
            self.assertEqual(code, 1)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "Phase fragment assembly failed.\n")
            self.assertEqual(output.read_bytes(), expected)

    def test_cli_never_echoes_private_paths_or_internal_errors(self) -> None:
        secret = "ZXQ_PRIVATE_PHASE_PATH_731"
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            fragment = Path(directory) / f"{secret}.part"
            output = Path(directory) / "phase.json"
            fragment.write_bytes(b"not-a-fragment")
            code, stdout, stderr = run_main(["--output", str(output), str(fragment)])
        self.assertEqual(code, 1)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "Phase fragment assembly failed.\n")
        self.assertNotIn(secret, stdout + stderr)

        with mock.patch.object(
            assembler,
            "assemble_phase_document",
            side_effect=RuntimeError(f"{secret}: internal private detail"),
        ):
            with tempfile.TemporaryDirectory() as directory:
                fragment = Path(directory) / "input.part"
                output = Path(directory) / "phase.json"
                fragment.write_bytes(self.raw)
                code, stdout, stderr = run_main(["--output", str(output), str(fragment)])
        self.assertEqual(code, 1)
        self.assertEqual(stdout, "")
        self.assertEqual(stderr, "Phase fragment assembly failed.\n")
        self.assertNotIn(secret, stdout + stderr)

    def test_real_process_runs_in_isolated_mode_from_unrelated_directory(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            fragment = Path(directory) / "input.part"
            output = Path(directory) / "phase.json"
            fragment.write_bytes(self.raw)
            completed = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    "-E",
                    "-s",
                    "-S",
                    "-B",
                    str(REPOSITORY_ROOT / "tools" / "assemble_frontend_phase.py"),
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
            self.assertEqual(completed.stdout, b"Phase fragment assembly succeeded.\n")
            self.assertEqual(completed.stderr, b"")
            self.assertEqual(
                json.loads(output.read_text(encoding="ascii"))["schema"],
                assembler.PHASE_OBSERVATION_SCHEMA,
            )


if __name__ == "__main__":
    unittest.main()
