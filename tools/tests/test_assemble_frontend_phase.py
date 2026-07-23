from __future__ import annotations

import hashlib
import importlib.util
import io
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from dataclasses import replace
from pathlib import Path
from unittest import mock


REPOSITORY_ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = REPOSITORY_ROOT / "tools" / "assemble_frontend_phase.py"
FIXTURE = (
    REPOSITORY_ROOT
    / "analysis"
    / "fixtures"
    / "frontend_phase_v2"
    / "synthetic-producer.hex"
)
CONTRACT = (
    REPOSITORY_ROOT
    / "analysis"
    / "fixtures"
    / "frontend_phase_v2"
    / "synthetic-contract.txt"
)
SPEC = importlib.util.spec_from_file_location("assemble_frontend_phase", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
assembler = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = assembler
SPEC.loader.exec_module(assembler)


_CAPTURE_DOMAIN = bytes(range(1, 33))
_RUNTIME_CONFIG = bytes(range(33, 65))
_SYNTHETIC_SITE_MAP = b"OpenOmega synthetic anonymous site map v1\n1\n2\n"
_SITE_MAP_DIGEST = hashlib.sha256(_SYNTHETIC_SITE_MAP).digest()


class _Builder:
    def __init__(self) -> None:
        self.output = bytearray()
        self.offsets: dict[str, int] = {}

    def blob(self, value: bytes, name: str) -> None:
        self.offsets[name] = len(self.output)
        self.output.extend(value)

    def u8(self, value: int, name: str) -> None:
        self.offsets[name] = len(self.output)
        self.output.extend(struct.pack("<B", value))

    def u32(self, value: int, name: str) -> None:
        self.offsets[name] = len(self.output)
        self.output.extend(struct.pack("<I", value))


def _default_counts() -> tuple[int, ...]:
    return (2, 2, 4, 3, 3, 3)


def expected_wire_contract() -> bytes:
    counts = _default_counts()
    offset = assembler._HEADER_BYTES
    offsets: list[tuple[str, int]] = []
    for name, count, width in zip(
        ("sites", "invocations", "events", "submissions", "draws", "memberships"),
        counts,
        assembler._ROW_WIDTHS,
        strict=True,
    ):
        offsets.append((name, offset))
        offset += count * width

    def enum_values(enum_type: type[assembler.IntEnum]) -> str:
        return ",".join(f"{item.name}:{int(item)}" for item in enum_type)

    failure_statuses = (
        assembler.TerminalStatus.TelemetryOverflow,
        assembler.TerminalStatus.TelemetryDropped,
        assembler.TerminalStatus.VmReset,
        assembler.TerminalStatus.SavestateDiscontinuity,
        assembler.TerminalStatus.QueueExhausted,
        assembler.TerminalStatus.OutputFailure,
        assembler.TerminalStatus.InternalFailure,
    )
    failure_precedence = ",".join(
        f"{status.name}:{counter}"
        for status, counter in zip(
            failure_statuses, assembler._FAILURE_COUNTER_NAMES, strict=True
        )
    )
    lines = [
        "schema=openomega-frontend-phase-wire-contract-v1",
        f"magic={assembler.FRAGMENT_MAGIC.decode('ascii')}",
        f"version={assembler.FRAGMENT_VERSION}",
        f"fixture_bytes={offset}",
        f"header_bytes={assembler._HEADER_BYTES}",
        "row_widths=" + ",".join(str(width) for width in assembler._ROW_WIDTHS),
        *(f"offset.{name}={value}" for name, value in offsets),
        f"offset.end={offset}",
        f"enum.terminal_status={enum_values(assembler.TerminalStatus)}",
        f"enum.commit_state={enum_values(assembler.CommitState)}",
        f"enum.event_kind={enum_values(assembler.EventKind)}",
        f"enum.draw_disposition={enum_values(assembler.DrawDisposition)}",
        f"failure_counter_precedence={failure_precedence}",
        *(f"limit.{name}={value}" for name, value in assembler.HARD_LIMITS.items()),
    ]
    return ("\n".join(lines) + "\n").encode("ascii")


def build_valid_fragment(
    *,
    status: int = int(assembler.TerminalStatus.Complete),
    commit_state: int | None = None,
    failure_counts: tuple[int, ...] | None = None,
    discovered_counts: tuple[int, ...] | None = None,
) -> tuple[bytes, dict[str, int]]:
    counts = _default_counts()
    if commit_state is None:
        commit_state = int(
            assembler.CommitState.Committed
            if status == int(assembler.TerminalStatus.Complete)
            else assembler.CommitState.Aborted
        )
    if failure_counts is None:
        failure_counts = (0,) * 7
    if discovered_counts is None:
        discovered_counts = counts
    if len(failure_counts) != 7 or len(discovered_counts) != 6:
        raise AssertionError("bad synthetic terminal inputs")

    builder = _Builder()
    builder.blob(assembler.FRAGMENT_MAGIC, "magic")
    builder.u32(assembler.FRAGMENT_VERSION, "version")
    builder.blob(_CAPTURE_DOMAIN, "capture_domain")
    builder.blob(_RUNTIME_CONFIG, "runtime_config")
    builder.blob(_SITE_MAP_DIGEST, "site_map_digest")
    builder.u32(3, "discovered_frame_count")
    builder.u32(3, "frame_count")
    builder.u32(status, "terminal.status")
    builder.u32(commit_state, "terminal.commit")
    for index, value in enumerate(failure_counts):
        builder.u32(value, f"failure.{index}")
    for index, value in enumerate(discovered_counts):
        builder.u32(value, f"discovered.{index}")
    for index, value in enumerate(counts):
        builder.u32(value, f"count.{index}")

    builder.u32(1, "site.0.id")
    builder.u32(2, "site.1.id")

    # Invocation 1 is the root. Invocation 2 is strictly nested.
    for index, values in enumerate(
        (
            (1, 1, 0, 0, 1, 4),
            (2, 2, 0, 1, 2, 3),
        )
    ):
        for field, value in zip(
            ("id", "site", "lane", "parent", "enter", "exit"),
            values,
            strict=True,
        ):
            builder.u32(value, f"invocation.{index}.{field}")

    # Global chronology: enter root, enter child, two submissions, draw,
    # exit child, root submission, draw, exit root, skipped draw.
    for index, values in enumerate(
        (
            (1, 1, 1, 1, 0, 0),
            (2, 2, 1, 2, 0, 0),
            (3, 6, 2, 2, 1, 0),
            (4, 9, 3, 1, 1, 0),
        )
    ):
        ordinal, sequence, frame, invocation, kind, reserved = values
        builder.u32(ordinal, f"event.{index}.id")
        builder.u32(sequence, f"event.{index}.sequence")
        builder.u32(frame, f"event.{index}.frame")
        builder.u32(invocation, f"event.{index}.invocation")
        builder.u8(kind, f"event.{index}.kind")
        builder.u32(reserved, f"event.{index}.reserved")

    for index, values in enumerate(
        (
            (1, 3, 1, 2, 0, 3),
            (2, 4, 2, 2, 3, 2),
            (3, 7, 2, 1, 5, 1),
        )
    ):
        for field, value in zip(
            ("id", "sequence", "frame", "invocation", "primitive_begin", "primitive_count"),
            values,
            strict=True,
        ):
            builder.u32(value, f"submission.{index}.{field}")

    for index, values in enumerate(
        (
            (1, 5, 2, 0, 0),
            (2, 8, 2, 0, 0),
            (3, 10, 3, 1, 0),
        )
    ):
        ordinal, sequence, frame, disposition, reserved = values
        builder.u32(ordinal, f"draw.{index}.id")
        builder.u32(sequence, f"draw.{index}.sequence")
        builder.u32(frame, f"draw.{index}.frame")
        builder.u8(disposition, f"draw.{index}.disposition")
        builder.u32(reserved, f"draw.{index}.reserved")

    for index, (submission, draw) in enumerate(((1, 1), (2, 1), (3, 2))):
        builder.u32(submission, f"edge.{index}.submission")
        builder.u32(draw, f"edge.{index}.draw")

    return bytes(builder.output), builder.offsets


def build_empty_complete_fragment() -> bytes:
    counts = (2, 0, 0, 0, 0, 0)
    builder = _Builder()
    builder.blob(assembler.FRAGMENT_MAGIC, "magic")
    builder.u32(assembler.FRAGMENT_VERSION, "version")
    builder.blob(_CAPTURE_DOMAIN, "capture_domain")
    builder.blob(_RUNTIME_CONFIG, "runtime_config")
    builder.blob(_SITE_MAP_DIGEST, "site_map_digest")
    builder.u32(1, "discovered_frame_count")
    builder.u32(1, "frame_count")
    builder.u32(int(assembler.TerminalStatus.Complete), "terminal.status")
    builder.u32(int(assembler.CommitState.Committed), "terminal.commit")
    for index in range(7):
        builder.u32(0, f"failure.{index}")
    for index, value in enumerate(counts):
        builder.u32(value, f"discovered.{index}")
    for index, value in enumerate(counts):
        builder.u32(value, f"count.{index}")
    builder.u32(1, "site.0.id")
    builder.u32(2, "site.1.id")
    return bytes(builder.output)


def clone_bytes(raw: bytes) -> bytes:
    result = bytes(bytearray(raw))
    assert result is not raw
    return result


def mutate_u8(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<B", result, offset, value)
    return bytes(result)


def mutate_u32(raw: bytes, offset: int, value: int) -> bytes:
    result = bytearray(raw)
    struct.pack_into("<I", result, offset, value)
    return bytes(result)


def mutate_many_u32(raw: bytes, changes: tuple[tuple[int, int], ...]) -> bytes:
    result = bytearray(raw)
    for offset, value in changes:
        struct.pack_into("<I", result, offset, value)
    return bytes(result)


def site_binding() -> assembler.SiteMapBinding:
    return assembler.SiteMapBinding(_RUNTIME_CONFIG, _SITE_MAP_DIGEST, 2)


def capture_manifest() -> assembler.CaptureDomainManifest:
    return assembler.CaptureDomainManifest(
        _CAPTURE_DOMAIN,
        _CAPTURE_DOMAIN,
        _CAPTURE_DOMAIN,
    )


def site_binding_bytes() -> bytes:
    return (
        json.dumps(
            {
                "schema": assembler.SITE_MAP_BINDING_SCHEMA,
                "runtime_config_digest": _RUNTIME_CONFIG.hex(),
                "site_map_digest": _SITE_MAP_DIGEST.hex(),
                "site_count": 2,
            },
            separators=(",", ":"),
        ).encode("ascii")
        + b"\n"
    )


def capture_manifest_bytes(
    *,
    scene: bytes = _CAPTURE_DOMAIN,
    frontend: bytes = _CAPTURE_DOMAIN,
) -> bytes:
    return (
        json.dumps(
            {
                "schema": assembler.CAPTURE_MANIFEST_SCHEMA,
                "phase_capture_domain": _CAPTURE_DOMAIN.hex(),
                "scene_capture_domain": scene.hex(),
                "frontend_capture_domain": frontend.hex(),
            },
            separators=(",", ":"),
        ).encode("ascii")
        + b"\n"
    )


def assembled_model(
    raw: bytes | None = None,
    *,
    limits: assembler.PhaseLimits = assembler.DEFAULT_LIMITS,
) -> assembler.VerifiedPhaseAssembly:
    if raw is None:
        raw, _ = build_valid_fragment()
    return assembler.assemble_phase_model(
        [raw],
        site_map_bytes=_SYNTHETIC_SITE_MAP,
        site_map_binding=site_binding(),
        capture_manifest=capture_manifest(),
        expected_fragment_sha256=hashlib.sha256(raw).hexdigest(),
        limits=limits,
    )


def run_main(args: list[str]) -> tuple[int, str, str]:
    output = io.StringIO()
    errors = io.StringIO()
    with redirect_stdout(output), redirect_stderr(errors):
        code = assembler.main(args)
    return code, output.getvalue(), errors.getvalue()


def write_cli_inputs(directory: Path, raw: bytes) -> tuple[Path, Path, Path, Path]:
    fragment = directory / "input.part"
    site_map = directory / "site-map.private"
    binding = directory / "site-binding.json"
    manifest = directory / "capture-manifest.json"
    fragment.write_bytes(raw)
    site_map.write_bytes(_SYNTHETIC_SITE_MAP)
    binding.write_bytes(site_binding_bytes())
    manifest.write_bytes(capture_manifest_bytes())
    return fragment, site_map, binding, manifest


def cli_args(
    directory: Path,
    raw: bytes,
    *,
    private: bool = False,
) -> list[str]:
    fragment, site_map, binding, manifest = write_cli_inputs(directory, raw)
    args = [
        "--site-map",
        str(site_map),
        "--site-map-binding",
        str(binding),
        "--capture-manifest",
        str(manifest),
        "--expected-sha256",
        hashlib.sha256(raw).hexdigest(),
        "--public-output",
        str(directory / "public.json"),
    ]
    if private:
        private_root = directory / "private"
        private_root.mkdir(exist_ok=True)
        args += ["--private-output", str(private_root / "private.json")]
    args.append(str(fragment))
    return args


class PhaseFragmentParserTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, self.offsets = build_valid_fragment()

    def assert_invalid(
        self,
        raw: bytes,
        *,
        limits: assembler.PhaseLimits = assembler.DEFAULT_LIMITS,
    ) -> None:
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(raw, limits=limits)

    def test_valid_fragment_retains_private_chronology_and_dispositions(self) -> None:
        model = assembler.parse_phase_fragment(self.raw)
        self.assertTrue(model.terminal_capture_complete)
        self.assertEqual(model.capture_domain, _CAPTURE_DOMAIN)
        self.assertEqual([row.site for row in model.invocations], [1, 2])
        self.assertEqual([row.sequence for row in model.events], [1, 2, 6, 9])
        self.assertEqual([row.sequence for row in model.submissions], [3, 4, 7])
        self.assertEqual([row.sequence for row in model.draws], [5, 8, 10])
        self.assertEqual(
            [row.disposition for row in model.draws],
            [
                assembler.DrawDisposition.Submitted,
                assembler.DrawDisposition.Submitted,
                assembler.DrawDisposition.Skipped,
            ],
        )
        self.assertEqual(
            [(row.submission, row.draw) for row in model.memberships],
            [(1, 1), (2, 1), (3, 2)],
        )

    def test_checked_in_fixture_matches_reference_builder(self) -> None:
        encoded = FIXTURE.read_bytes()
        self.assertEqual(encoded[-1:], b"\n")
        self.assertNotIn(b"\r", encoded)
        self.assertEqual(bytes.fromhex(encoded.decode("ascii")), self.raw)

    def test_checked_in_ascii_contract_matches_parser_constants(self) -> None:
        encoded = CONTRACT.read_bytes()
        self.assertEqual(encoded[-1:], b"\n")
        self.assertNotIn(b"\r", encoded)
        self.assertEqual(len(assembler.HARD_LIMITS), 20)
        self.assertEqual(encoded, expected_wire_contract())

    def test_wire_contains_only_neutral_binary_ordinals(self) -> None:
        for forbidden in (
            b"address",
            b"symbol",
            b"widget",
            b"text",
            b"animation",
            b"asset",
            b"path",
        ):
            self.assertNotIn(forbidden, self.raw.lower())

    def test_magic_version_consistency_values_and_exact_length_fail_closed(self) -> None:
        self.assert_invalid(b"X" + self.raw[1:])
        self.assert_invalid(mutate_u32(self.raw, self.offsets["version"], 3))
        for name in ("capture_domain", "runtime_config", "site_map_digest"):
            changed = bytearray(self.raw)
            begin = self.offsets[name]
            changed[begin : begin + 32] = bytes(32)
            self.assert_invalid(bytes(changed))
        self.assert_invalid(self.raw[:-1])
        self.assert_invalid(self.raw + b"X")
        self.assert_invalid(b"")
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(bytearray(self.raw))  # type: ignore[arg-type]

    def test_dense_site_and_invocation_references_are_enforced(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["site.0.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.site"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.site"], 3))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.lane"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.parent"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.enter"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.exit"], 2))

    def test_event_submission_and_draw_fields_are_bounded(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.frame"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.invocation"], 0))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["event.0.kind"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.0.reserved"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["submission.0.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["submission.0.frame"], 4))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["submission.0.invocation"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.id"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.frame"], 4))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["draw.0.disposition"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.0.reserved"], 1))

    def test_crossing_lifecycle_is_rejected_even_when_references_are_consistent(self) -> None:
        crossing = mutate_many_u32(
            self.raw,
            (
                (self.offsets["invocation.0.exit"], 3),
                (self.offsets["invocation.1.exit"], 4),
                (self.offsets["event.2.invocation"], 1),
                (self.offsets["event.3.invocation"], 2),
            ),
        )
        self.assert_invalid(crossing)

    def test_parent_stack_orphan_and_event_sequence_fail_closed(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.parent"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.2.kind"], 0))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.1.sequence"], 1))

    def test_wrong_double_claimed_and_orphan_lifecycle_boundaries_fail_closed(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.0.enter"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["invocation.1.enter"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["event.1.invocation"], 1))

    def test_submission_must_use_immutable_active_invocation_context(self) -> None:
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["submission.0.invocation"], 1)
        )
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["submission.2.invocation"], 2)
        )
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["submission.1.primitive_begin"], 4)
        )
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["submission.0.primitive_count"], 0)
        )

    def test_primitive_exclusive_end_accepts_two_to_32_and_rejects_overflow(self) -> None:
        exact = mutate_many_u32(
            self.raw,
            (
                (self.offsets["submission.0.primitive_count"], 0xFFFFFFFD),
                (self.offsets["submission.1.primitive_begin"], 0xFFFFFFFD),
                (self.offsets["submission.1.primitive_count"], 1),
                (self.offsets["submission.2.primitive_begin"], 0xFFFFFFFE),
                (self.offsets["submission.2.primitive_count"], 2),
            ),
        )
        assembler.parse_phase_fragment(exact)
        self.assert_invalid(
            mutate_u32(
                exact,
                self.offsets["submission.2.primitive_count"],
                3,
            )
        )

    def test_draw_must_follow_every_attributed_submission(self) -> None:
        changed = mutate_many_u32(
            self.raw,
            (
                (self.offsets["submission.1.sequence"], 5),
                (self.offsets["draw.0.sequence"], 4),
            ),
        )
        self.assert_invalid(changed)

    def test_draw_frame_cannot_precede_invocation_entry(self) -> None:
        changed = mutate_many_u32(
            self.raw,
            (
                (self.offsets["event.1.frame"], 2),
                (self.offsets["submission.0.frame"], 2),
                (self.offsets["draw.0.frame"], 1),
            ),
        )
        self.assert_invalid(changed)

    def test_global_temporal_sequence_rejects_gaps_and_duplicates(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.2.sequence"], 11))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["draw.2.sequence"], 9))

    def test_unified_frame_chronology_rejects_event_submission_and_draw_regression(self) -> None:
        for field in (
            "event.2.frame",
            "submission.2.frame",
            "draw.2.frame",
        ):
            with self.subTest(field=field):
                self.assert_invalid(mutate_u32(self.raw, self.offsets[field], 1))

    def test_membership_is_total_unique_ordered_and_disposition_consistent(self) -> None:
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.2.submission"], 2))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.1.submission"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.2.draw"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["edge.2.draw"], 3))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["draw.1.disposition"], 1))
        self.assert_invalid(mutate_u8(self.raw, self.offsets["draw.2.disposition"], 0))

    def test_complete_terminal_requires_commit_zero_failures_and_reconciliation(self) -> None:
        self.assert_invalid(
            mutate_u32(
                self.raw,
                self.offsets["terminal.commit"],
                int(assembler.CommitState.Aborted),
            )
        )
        self.assert_invalid(mutate_u32(self.raw, self.offsets["failure.0"], 1))
        self.assert_invalid(mutate_u32(self.raw, self.offsets["discovered.3"], 4))
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["discovered_frame_count"], 4)
        )
        self.assert_invalid(
            mutate_u32(self.raw, self.offsets["discovered_frame_count"], 2)
        )
        self.assert_invalid(mutate_u32(self.raw, self.offsets["frame_count"], 0))

    def test_every_incomplete_terminal_category_is_retained_and_invalidates_evidence(self) -> None:
        cases = (
            (assembler.TerminalStatus.TelemetryOverflow, 0),
            (assembler.TerminalStatus.TelemetryDropped, 1),
            (assembler.TerminalStatus.VmReset, 2),
            (assembler.TerminalStatus.SavestateDiscontinuity, 3),
            (assembler.TerminalStatus.QueueExhausted, 4),
            (assembler.TerminalStatus.OutputFailure, 5),
            (assembler.TerminalStatus.InternalFailure, 6),
        )
        for status, counter_index in cases:
            counters = [0] * 7
            counters[counter_index] = 1
            raw, _ = build_valid_fragment(
                status=int(status), failure_counts=tuple(counters)
            )
            with self.subTest(status=status.name):
                model = assembler.parse_phase_fragment(raw)
                self.assertIs(model.terminal.status, status)
                self.assertFalse(model.terminal_capture_complete)

        raw, _ = build_valid_fragment(status=int(assembler.TerminalStatus.ProducerAborted))
        model = assembler.parse_phase_fragment(raw)
        self.assertIs(model.terminal.status, assembler.TerminalStatus.ProducerAborted)
        self.assertFalse(model.terminal_capture_complete)

        dropped_raw, dropped_offsets = build_valid_fragment(
            status=int(assembler.TerminalStatus.TelemetryDropped),
            failure_counts=(0, 1, 0, 0, 0, 0, 0),
            discovered_counts=(2, 2, 4, 4, 3, 3),
        )
        dropped_raw = mutate_u32(
            dropped_raw,
            dropped_offsets["discovered_frame_count"],
            4,
        )
        dropped = assembler.parse_phase_fragment(dropped_raw)
        self.assertEqual(dropped.terminal.discovered_frame_count, 4)
        self.assertEqual(dropped.terminal.retained_frame_count, 3)
        self.assertEqual(dropped.terminal.discovered_counts[3], 4)
        self.assertEqual(dropped.terminal.retained_counts[3], 3)

    def test_terminal_precedence_and_commit_contradictions_fail_closed(self) -> None:
        raw, _ = build_valid_fragment(
            status=int(assembler.TerminalStatus.TelemetryDropped),
            failure_counts=(1, 1, 0, 0, 0, 0, 0),
        )
        self.assert_invalid(raw)
        raw, _ = build_valid_fragment(
            status=int(assembler.TerminalStatus.VmReset),
            failure_counts=(0, 0, 1, 0, 0, 0, 0),
            commit_state=int(assembler.CommitState.Committed),
        )
        self.assert_invalid(raw)
        raw, _ = build_valid_fragment(
            status=int(assembler.TerminalStatus.ProducerAborted),
            failure_counts=(0, 1, 0, 0, 0, 0, 0),
        )
        self.assert_invalid(raw)


class PrivateBindingAndAssemblyTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, self.offsets = build_valid_fragment()

    def test_private_binding_documents_are_exact_and_bounded(self) -> None:
        self.assertEqual(assembler.parse_site_map_binding(site_binding_bytes()), site_binding())
        self.assertEqual(
            assembler.parse_site_map_binding(
                site_binding_bytes(),
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    manifest_bytes=len(site_binding_bytes()),
                ),
            ),
            site_binding(),
        )
        self.assertEqual(
            assembler.parse_capture_domain_manifest(capture_manifest_bytes()),
            capture_manifest(),
        )
        duplicate = (
            b'{"schema":"'
            + assembler.SITE_MAP_BINDING_SCHEMA.encode("ascii")
            + b'","schema":"x","runtime_config_digest":"'
            + _RUNTIME_CONFIG.hex().encode("ascii")
            + b'","site_map_digest":"'
            + _SITE_MAP_DIGEST.hex().encode("ascii")
            + b'","site_count":2}'
        )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.parse_site_map_binding(duplicate)
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.parse_site_map_binding(
                site_binding_bytes(),
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    manifest_bytes=len(site_binding_bytes()) - 1,
                ),
            )

    def test_site_map_and_capture_domain_joins_fail_closed(self) -> None:
        wrong = bytes([255]) * 32
        for binding in (
            assembler.SiteMapBinding(wrong, _SITE_MAP_DIGEST, 2),
            assembler.SiteMapBinding(_RUNTIME_CONFIG, wrong, 2),
            assembler.SiteMapBinding(_RUNTIME_CONFIG, _SITE_MAP_DIGEST, 1),
        ):
            with self.subTest(binding=binding):
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler.assemble_phase_model(
                        [self.raw],
                        site_map_bytes=_SYNTHETIC_SITE_MAP,
                        site_map_binding=binding,
                        capture_manifest=capture_manifest(),
                    )
        for manifest in (
            assembler.CaptureDomainManifest(wrong, wrong, wrong),
            assembler.CaptureDomainManifest(_CAPTURE_DOMAIN, wrong, _CAPTURE_DOMAIN),
            assembler.CaptureDomainManifest(_CAPTURE_DOMAIN, _CAPTURE_DOMAIN, wrong),
        ):
            with self.subTest(manifest=manifest):
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler.assemble_phase_model(
                        [self.raw],
                        site_map_bytes=_SYNTHETIC_SITE_MAP,
                        site_map_binding=site_binding(),
                        capture_manifest=manifest,
                    )

    def test_repeat_copies_are_compared_but_reference_is_parsed_once(self) -> None:
        repeat = clone_bytes(self.raw)
        with mock.patch.object(
            assembler,
            "parse_phase_fragment",
            wraps=assembler.parse_phase_fragment,
        ) as parse:
            model = assembler.assemble_phase_model(
                [self.raw, repeat],
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
            )
        self.assertTrue(model.ordering_evidence_valid)
        parse.assert_called_once()

    def test_same_object_changed_repeat_and_bad_expected_digest_are_rejected(self) -> None:
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_model(
                [self.raw, self.raw],
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
            )
        changed = mutate_u32(self.raw, self.offsets["frame_count"], 2)
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_model(
                [self.raw, changed],
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
            )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_model(
                [self.raw],
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
                expected_fragment_sha256="0" * 64,
            )

    def test_total_input_limit_applies_to_independent_repeats(self) -> None:
        repeat = clone_bytes(self.raw)
        exact = replace(
            assembler.DEFAULT_LIMITS,
            total_input_bytes=len(_SYNTHETIC_SITE_MAP) + len(self.raw) * 2,
        )
        assembler.assemble_phase_model(
            [self.raw, repeat],
            site_map_bytes=_SYNTHETIC_SITE_MAP,
            site_map_binding=site_binding(),
            capture_manifest=capture_manifest(),
            limits=exact,
        )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_model(
                [self.raw, repeat],
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
                limits=replace(
                    exact,
                    total_input_bytes=len(_SYNTHETIC_SITE_MAP) + len(self.raw) * 2 - 1,
                ),
            )

    def test_fragment_byte_limit_preflights_every_copy_before_hash_or_parse(self) -> None:
        limits = replace(assembler.DEFAULT_LIMITS, fragment_bytes=len(self.raw) - 1)
        with (
            mock.patch.object(assembler.hashlib, "sha256") as sha256,
            mock.patch.object(assembler, "parse_phase_fragment") as parse,
            self.assertRaises(assembler.PhaseAssemblyError),
        ):
            assembler.assemble_phase_model(
                [self.raw, clone_bytes(self.raw)],
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
                limits=limits,
            )
        sha256.assert_not_called()
        parse.assert_not_called()

    def test_fragment_copy_count_accepts_exact_limit_and_rejects_plus_one(self) -> None:
        copies = [clone_bytes(self.raw) for _ in range(8)]
        assembler.assemble_phase_model(
            copies,
            site_map_bytes=_SYNTHETIC_SITE_MAP,
            site_map_binding=site_binding(),
            capture_manifest=capture_manifest(),
            limits=replace(assembler.DEFAULT_LIMITS, fragments=8),
        )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.assemble_phase_model(
                copies,
                site_map_bytes=_SYNTHETIC_SITE_MAP,
                site_map_binding=site_binding(),
                capture_manifest=capture_manifest(),
                limits=replace(assembler.DEFAULT_LIMITS, fragments=7),
            )


class DiagnosticSeparationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, _ = build_valid_fragment()
        self.model = assembled_model(self.raw)

    def test_structural_parse_cannot_bypass_private_joins_or_reach_reducers(self) -> None:
        parsed = assembler.parse_phase_fragment(self.raw)
        self.assertTrue(parsed.terminal_capture_complete)
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.VerifiedPhaseAssembly(parsed)
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.encode_public_report(parsed)  # type: ignore[arg-type]
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.encode_private_trace(parsed)  # type: ignore[arg-type]

    def test_public_report_is_categorical_deterministic_and_relationship_free(self) -> None:
        first = assembler.encode_public_report(self.model)
        second = assembler.encode_public_report(assembled_model(clone_bytes(self.raw)))
        self.assertEqual(first, second)
        document = json.loads(first)
        self.assertEqual(
            list(document),
            ["schema", "terminal_status", "completeness", "policy", "aggregate_counts"],
        )
        self.assertEqual(document["schema"], assembler.PUBLIC_REPORT_SCHEMA)
        self.assertEqual(document["terminal_status"], "Complete")
        self.assertEqual(document["completeness"], "Complete")
        self.assertEqual(document["policy"], "PrivateReviewRequired")
        self.assertEqual(len(document["aggregate_counts"]), 8)
        self.assertEqual(
            [list(row) for row in document["aggregate_counts"]],
            [["category", "count"]] * 8,
        )
        self.assertEqual(
            [row["category"] for row in document["aggregate_counts"]],
            list(assembler._PUBLIC_COUNT_CATEGORIES),
        )
        rendered = first.decode("ascii").lower()
        for forbidden in (
            _CAPTURE_DOMAIN.hex(),
            _RUNTIME_CONFIG.hex(),
            _SITE_MAP_DIGEST.hex(),
            "capture_domain",
            "digest",
            "sequence",
            "frame",
            "ordinal",
            "parent",
            "primitive",
            "enter_event",
            "exit_event",
        ):
            self.assertNotIn(forbidden, rendered)
        self.assertNotIn("invocations", document)
        self.assertNotIn("memberships", document)

    def test_same_count_private_identity_and_relationship_changes_reduce_identically(self) -> None:
        changed_capture = bytes([0xA1]) * 32
        changed_config = bytes([0xB2]) * 32
        variant_site_map = b"different synthetic anonymous site map"
        changed_site_map = hashlib.sha256(variant_site_map).digest()
        variant = bytearray(self.raw)
        _, offsets = build_valid_fragment()
        variant[offsets["capture_domain"] : offsets["capture_domain"] + 32] = changed_capture
        variant[offsets["runtime_config"] : offsets["runtime_config"] + 32] = changed_config
        variant[offsets["site_map_digest"] : offsets["site_map_digest"] + 32] = changed_site_map
        struct.pack_into("<I", variant, offsets["invocation.0.site"], 2)
        struct.pack_into("<I", variant, offsets["invocation.1.site"], 1)
        struct.pack_into("<I", variant, offsets["submission.0.primitive_count"], 2)
        struct.pack_into("<I", variant, offsets["submission.1.primitive_begin"], 2)
        struct.pack_into("<I", variant, offsets["submission.1.primitive_count"], 3)
        variant_bytes = bytes(variant)
        variant_model = assembler.assemble_phase_model(
            [variant_bytes],
            site_map_bytes=variant_site_map,
            site_map_binding=assembler.SiteMapBinding(
                changed_config, changed_site_map, 2
            ),
            capture_manifest=assembler.CaptureDomainManifest(
                changed_capture, changed_capture, changed_capture
            ),
        )
        self.assertEqual(
            assembler.encode_public_report(variant_model),
            assembler.encode_public_report(self.model),
        )
        self.assertNotEqual(
            assembler.encode_private_trace(variant_model),
            assembler.encode_private_trace(self.model),
        )

    def test_private_trace_retains_disposition_chronology_and_reconciliation(self) -> None:
        document = json.loads(assembler.encode_private_trace(self.model))
        self.assertEqual(document["capture_domain"], _CAPTURE_DOMAIN.hex())
        self.assertEqual([row["sequence"] for row in document["submissions"]], [3, 4, 7])
        self.assertEqual(
            [row["disposition"] for row in document["draws"]],
            ["Submitted", "Submitted", "Skipped"],
        )
        self.assertEqual(
            document["terminal"]["discovered_counts"],
            document["terminal"]["retained_counts"],
        )
        self.assertTrue(document["ordering_evidence_valid"])

    def test_private_and_public_output_caps_are_exact(self) -> None:
        private = assembler.encode_private_trace(self.model)
        public = assembler.encode_public_report(self.model)
        self.assertEqual(
            assembler.encode_private_trace(
                self.model,
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    private_output_bytes=len(private),
                ),
            ),
            private,
        )
        self.assertEqual(
            assembler.encode_public_report(
                self.model,
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    public_output_bytes=len(public),
                ),
            ),
            public,
        )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.encode_private_trace(
                self.model,
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    private_output_bytes=len(private) - 1,
                ),
            )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.encode_public_report(
                self.model,
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    public_output_bytes=len(public) - 1,
                ),
            )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.encode_public_report(
                self.model,
                limits=replace(
                    assembler.DEFAULT_LIMITS,
                    public_aggregate_rows=7,
                ),
            )

    def test_incomplete_trace_reduces_only_to_fixed_public_categories(self) -> None:
        counters = (0, 1, 0, 0, 0, 0, 0)
        raw, _ = build_valid_fragment(
            status=int(assembler.TerminalStatus.TelemetryDropped),
            failure_counts=counters,
        )
        model = assembled_model(raw)
        report = json.loads(assembler.encode_public_report(model))
        self.assertEqual(report["terminal_status"], "TelemetryDropped")
        self.assertEqual(report["completeness"], "Incomplete")
        self.assertEqual(report["policy"], "IncompleteCapture")
        private = json.loads(assembler.encode_private_trace(model))
        self.assertFalse(private["ordering_evidence_valid"])
        self.assertEqual(private["terminal"]["failure_counts"]["dropped_records"], 1)

    def test_complete_capture_without_submissions_has_no_ordering_evidence(self) -> None:
        model = assembled_model(build_empty_complete_fragment())
        report = json.loads(assembler.encode_public_report(model))
        self.assertEqual(report["terminal_status"], "Complete")
        self.assertEqual(report["completeness"], "Complete")
        self.assertEqual(report["policy"], "NoOrderingEvidence")
        counts = {
            row["category"]: row["count"] for row in report["aggregate_counts"]
        }
        self.assertEqual(counts["site-records"], 2)
        self.assertTrue(all(count == 0 for name, count in counts.items() if name != "site-records"))


class LimitTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, _ = build_valid_fragment()

    def test_every_caller_limit_accepts_hard_ceiling_and_rejects_plus_one(self) -> None:
        for name, ceiling in assembler.HARD_LIMITS.items():
            with self.subTest(name=name):
                assembler.PhaseLimits(**{name: ceiling})
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler.PhaseLimits(**{name: ceiling + 1})

    def test_table_count_boundaries_are_checked_before_row_allocation(self) -> None:
        def validate(counts: tuple[int, ...]) -> None:
            raw_size = assembler._HEADER_BYTES + sum(
                count * width
                for count, width in zip(counts, assembler._ROW_WIDTHS, strict=True)
            )
            assembler._validate_counts(
                counts,
                counts,
                raw_size,
                assembler.DEFAULT_LIMITS,
            )

        exact_cases = (
            (assembler.MAX_SITES, 0, 0, 0, 0, 0),
            (0, assembler.MAX_INVOCATIONS, assembler.MAX_EVENTS, 0, 0, 0),
        )
        for counts in exact_cases:
            with self.subTest(counts=counts):
                validate(counts)

        max_admitted_submissions = (
            assembler.MAX_SCRATCH_BYTES - 65_536
        ) // 1_024
        validate((0, 0, 0, max_admitted_submissions, 0, 0))
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            validate((0, 0, 0, max_admitted_submissions + 1, 0, 0))
        max_admitted_draws = (assembler.MAX_SCRATCH_BYTES - 65_536) // 768
        validate((0, 0, 0, 0, max_admitted_draws, 0))
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            validate((0, 0, 0, 0, max_admitted_draws + 1, 0))
        max_admitted_edges = (assembler.MAX_SCRATCH_BYTES - 65_536) // 512
        validate((0, 0, 0, 0, 0, max_admitted_edges))
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            validate((0, 0, 0, 0, 0, max_admitted_edges + 1))

        for counts in (
            (assembler.MAX_SITES + 1, 0, 0, 0, 0, 0),
            (
                0,
                assembler.MAX_INVOCATIONS + 1,
                assembler.MAX_EVENTS + 2,
                0,
                0,
                0,
            ),
            (0, 0, 0, 0, assembler.MAX_DRAWS + 1, 0),
            (0, 0, 0, 0, 0, assembler.MAX_EDGES + 1),
        ):
            with self.subTest(counts=counts):
                with self.assertRaises(assembler.PhaseFragmentValidationError):
                    validate(counts)

    def test_frame_and_failure_accounting_accept_exact_hard_boundary(self) -> None:
        frame_bound = mutate_many_u32(
            self.raw,
            (
                (build_valid_fragment()[1]["discovered_frame_count"], assembler.MAX_FRAMES),
                (build_valid_fragment()[1]["frame_count"], assembler.MAX_FRAMES),
            ),
        )
        assembler.parse_phase_fragment(frame_bound)
        offsets = build_valid_fragment()[1]
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(
                mutate_many_u32(
                    self.raw,
                    (
                        (offsets["discovered_frame_count"], assembler.MAX_FRAMES + 1),
                        (offsets["frame_count"], assembler.MAX_FRAMES + 1),
                    ),
                )
            )

        failure_raw, failure_offsets = build_valid_fragment(
            status=int(assembler.TerminalStatus.TelemetryOverflow),
            failure_counts=(
                assembler.MAX_FAILURE_RECORDS,
                0,
                0,
                0,
                0,
                0,
                0,
            ),
        )
        assembler.parse_phase_fragment(failure_raw)
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(
                mutate_u32(
                    failure_raw,
                    failure_offsets["failure.0"],
                    assembler.MAX_FAILURE_RECORDS + 1,
                )
            )

    def test_table_frame_nesting_scratch_and_lookup_limits_tighten(self) -> None:
        counts = _default_counts()
        scratch = assembler._logical_scratch_bytes(counts)
        work = assembler._logical_lookup_work(counts)
        exact = replace(
            assembler.DEFAULT_LIMITS,
            fragment_bytes=len(self.raw),
            frames=3,
            sites=2,
            nesting_depth=2,
            invocations=2,
            events=4,
            submissions=3,
            draws=3,
            edges=3,
            scratch_bytes=scratch,
            lookup_work=work,
        )
        assembler.parse_phase_fragment(self.raw, limits=exact)
        for field, value in (
            ("fragment_bytes", len(self.raw) - 1),
            ("frames", 2),
            ("sites", 1),
            ("nesting_depth", 1),
            ("invocations", 1),
            ("events", 3),
            ("submissions", 2),
            ("draws", 2),
            ("edges", 2),
            ("scratch_bytes", scratch - 1),
            ("lookup_work", work - 1),
        ):
            with self.subTest(field=field):
                with self.assertRaises(assembler.PhaseFragmentValidationError):
                    assembler.parse_phase_fragment(
                        self.raw,
                        limits=replace(exact, **{field: value}),
                    )

    def test_failure_accounting_and_string_limits_tighten(self) -> None:
        raw, _ = build_valid_fragment(
            status=int(assembler.TerminalStatus.TelemetryDropped),
            failure_counts=(0, 1, 0, 0, 0, 0, 0),
        )
        assembler.parse_phase_fragment(
            raw,
            limits=replace(assembler.DEFAULT_LIMITS, failure_records=1),
        )
        with self.assertRaises(assembler.PhaseFragmentValidationError):
            assembler.parse_phase_fragment(
                raw,
                limits=replace(assembler.DEFAULT_LIMITS, failure_records=0),
            )
        assembler.parse_site_map_binding(
            site_binding_bytes(),
            limits=replace(assembler.DEFAULT_LIMITS, string_bytes=64),
        )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler.parse_site_map_binding(
                site_binding_bytes(),
                limits=replace(assembler.DEFAULT_LIMITS, string_bytes=63),
            )


class PhaseAssemblerCliTests(unittest.TestCase):
    def setUp(self) -> None:
        self.raw, _ = build_valid_fragment()

    def test_help_argument_and_fixed_failure_diagnostics(self) -> None:
        for help_argument in ("-h", "--help"):
            code, output, errors = run_main([help_argument])
            self.assertEqual((code, errors), (0, ""))
            self.assertIn("--private-output", output)
        for args in ([], ["--unknown"], ["--expected-sha256"]):
            with self.subTest(args=args):
                code, output, errors = run_main(args)
                self.assertEqual(code, 1)
                self.assertEqual(output, "")
                self.assertEqual(errors, "Phase fragment processing failed.\n")

    def test_cli_argument_caps_and_limit_grammar_are_exact(self) -> None:
        base = [
            "--public-output",
            "o",
            "--site-map",
            "s",
            "--site-map-binding",
            "b",
            "--capture-manifest",
            "c",
            "--expected-sha256",
            "0" * 64,
            "f",
            "g",
        ]
        exact_count = base[:10] + ["--private-output", "p"]
        for name, ceiling in assembler.HARD_LIMITS.items():
            exact_count += ["--limit", f"{name}={ceiling}"]
        exact_count += [f"f{index}" for index in range(8)]
        self.assertEqual(len(exact_count), assembler.MAX_CLI_ARGUMENTS)
        assembler._parse_cli(exact_count)
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler._parse_cli(exact_count + ["one-too-many"])

        byte_base = list(base)
        used_without_last = sum(len(value.encode("utf-8")) for value in byte_base[:-1])
        byte_base[-1] = "x" * (assembler.MAX_CLI_ARGUMENT_BYTES - used_without_last)
        assembler._parse_cli(byte_base)
        byte_base[-1] += "x"
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler._parse_cli(byte_base)

        for malformed in ("frames", "unknown=1", "frames=x", f"frames={assembler.MAX_FRAMES + 1}"):
            with self.subTest(malformed=malformed):
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler._parse_cli(["--limit", malformed, *base])
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler._parse_cli(
                ["--limit", "frames=1", "--limit", "frames=2", *base]
            )

    def test_default_writes_only_public_and_private_sink_is_explicit(self) -> None:
        with tempfile.TemporaryDirectory() as first_dir, tempfile.TemporaryDirectory() as second_dir:
            first = Path(first_dir)
            second = Path(second_dir)
            code, stdout, stderr = run_main(cli_args(first, self.raw))
            self.assertEqual((code, stdout, stderr), (0, "Phase fragment processing succeeded.\n", ""))
            self.assertTrue((first / "public.json").is_file())
            self.assertFalse((first / "private.json").exists())

            with mock.patch.object(assembler, "_REPOSITORY_ROOT", second):
                code, stdout, stderr = run_main(cli_args(second, self.raw, private=True))
            self.assertEqual((code, stdout, stderr), (0, "Phase fragment processing succeeded.\n", ""))
            self.assertTrue((second / "private" / "private.json").is_file())
            self.assertEqual(
                (first / "public.json").read_bytes(),
                (second / "public.json").read_bytes(),
            )

    def test_private_output_failure_cannot_leave_a_successful_public_commit_marker(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = cli_args(root, self.raw, private=True)
            private_path = Path(args[args.index("--private-output") + 1])
            private_path.write_bytes(b"preexisting private sentinel")
            with mock.patch.object(assembler, "_REPOSITORY_ROOT", root):
                code, stdout, stderr = run_main(args)
            self.assertEqual(
                (code, stdout, stderr),
                (1, "", "Phase fragment processing failed.\n"),
            )
            self.assertEqual(private_path.read_bytes(), b"preexisting private sentinel")
            self.assertFalse((root / "public.json").exists())

    def test_cli_requires_selected_site_map_to_match_private_binding(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = cli_args(root, self.raw)
            site_map_index = args.index("--site-map") + 1
            Path(args[site_map_index]).write_bytes(b"different synthetic map")
            code, stdout, stderr = run_main(args)
            self.assertEqual(
                (code, stdout, stderr),
                (1, "", "Phase fragment processing failed.\n"),
            )
            self.assertFalse((root / "public.json").exists())

    def test_cli_total_input_limit_counts_manifests_site_map_and_fragment(self) -> None:
        auxiliary_size = (
            len(site_binding_bytes())
            + len(capture_manifest_bytes())
            + len(_SYNTHETIC_SITE_MAP)
        )
        exact_size = auxiliary_size + len(self.raw)
        with tempfile.TemporaryDirectory() as exact_dir:
            root = Path(exact_dir)
            args = cli_args(root, self.raw)
            args[0:0] = ["--limit", f"total_input_bytes={exact_size}"]
            code, stdout, stderr = run_main(args)
            self.assertEqual(
                (code, stdout, stderr),
                (0, "Phase fragment processing succeeded.\n", ""),
            )
        with tempfile.TemporaryDirectory() as short_dir:
            root = Path(short_dir)
            args = cli_args(root, self.raw)
            args[0:0] = ["--limit", f"total_input_bytes={exact_size - 1}"]
            code, stdout, stderr = run_main(args)
            self.assertEqual(
                (code, stdout, stderr),
                (1, "", "Phase fragment processing failed.\n"),
            )
            self.assertFalse((root / "public.json").exists())

    def test_private_output_path_must_resolve_to_a_recognized_ignored_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(assembler.PhaseAssemblyError):
                assembler._validate_private_output_path(
                    str(Path(directory) / "outside.json")
                )
        assembler._validate_private_output_path(
            str(REPOSITORY_ROOT / "private" / "example.json")
        )
        assembler._validate_private_output_path(
            str(REPOSITORY_ROOT / "analysis" / "output" / "example.json")
        )
        with self.assertRaises(assembler.PhaseAssemblyError):
            assembler._validate_private_output_path(
                str(REPOSITORY_ROOT / "analysis" / "formats" / "example.json")
            )

    def test_cli_refuses_a_private_output_path_outside_any_ignored_root(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = cli_args(root, self.raw)
            not_ignored = REPOSITORY_ROOT / "analysis" / "formats" / "unsafe-private-output.json"
            args += ["--private-output", str(not_ignored)]
            self.assertFalse(not_ignored.exists())
            try:
                code, stdout, stderr = run_main(args)
                self.assertEqual(
                    (code, stdout, stderr), (1, "", "Phase fragment processing failed.\n")
                )
                self.assertFalse(not_ignored.exists())
                self.assertFalse((root / "public.json").exists())
            finally:
                if not_ignored.exists():
                    not_ignored.unlink()

    def test_incomplete_capture_writes_reports_and_returns_distinct_status(self) -> None:
        raw, _ = build_valid_fragment(
            status=int(assembler.TerminalStatus.QueueExhausted),
            failure_counts=(0, 0, 0, 0, 1, 0, 0),
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with mock.patch.object(assembler, "_REPOSITORY_ROOT", root):
                code, stdout, stderr = run_main(cli_args(root, raw, private=True))
            self.assertEqual(code, 2)
            self.assertEqual(stdout, "")
            self.assertEqual(stderr, "Phase capture is incomplete.\n")
            self.assertEqual(
                json.loads((root / "public.json").read_bytes())["terminal_status"],
                "QueueExhausted",
            )
            self.assertFalse(
                json.loads((root / "private" / "private.json").read_bytes())[
                    "ordering_evidence_valid"
                ]
            )

    def test_path_repeats_stream_compare_distinct_files_and_reject_same_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            first = root / "first.part"
            second = root / "second.part"
            selected_site_map = root / "site-map.private"
            first.write_bytes(self.raw)
            second.write_bytes(self.raw)
            selected_site_map.write_bytes(_SYNTHETIC_SITE_MAP)
            with mock.patch.object(
                assembler,
                "parse_phase_fragment",
                wraps=assembler.parse_phase_fragment,
            ) as parse:
                model = assembler.assemble_phase_paths(
                    [str(first), str(second)],
                    site_map_path=str(selected_site_map),
                    site_map_binding=site_binding(),
                    capture_manifest=capture_manifest(),
                    expected_fragment_sha256=hashlib.sha256(self.raw).hexdigest(),
                )
            self.assertTrue(model.ordering_evidence_valid)
            parse.assert_called_once()
            with self.assertRaises(assembler.PhaseAssemblyError):
                assembler.assemble_phase_paths(
                    [str(first), str(first)],
                    site_map_path=str(selected_site_map),
                    site_map_binding=site_binding(),
                    capture_manifest=capture_manifest(),
                    expected_fragment_sha256=hashlib.sha256(self.raw).hexdigest(),
                )

    def test_path_reference_obeys_total_input_limit_before_parse(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.part"
            selected_site_map = Path(directory) / "site-map.private"
            path.write_bytes(self.raw)
            selected_site_map.write_bytes(_SYNTHETIC_SITE_MAP)
            with mock.patch.object(
                assembler,
                "parse_phase_fragment",
                wraps=assembler.parse_phase_fragment,
            ) as parse:
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler.assemble_phase_paths(
                        [str(path)],
                        site_map_path=str(selected_site_map),
                        site_map_binding=site_binding(),
                        capture_manifest=capture_manifest(),
                        expected_fragment_sha256=hashlib.sha256(self.raw).hexdigest(),
                        limits=replace(
                            assembler.DEFAULT_LIMITS,
                            total_input_bytes=(
                                len(_SYNTHETIC_SITE_MAP) + len(self.raw) - 1
                            ),
                        ),
                    )
            parse.assert_not_called()

    def test_mutating_snapshot_is_rejected_before_parse(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "input.part"
            path.write_bytes(self.raw)
            real_signature = assembler._fstat_signature
            calls = 0

            def changing_signature(stream: object) -> tuple[int, int, int, int]:
                nonlocal calls
                calls += 1
                signature = real_signature(stream)
                if calls == 2:
                    return signature[:3] + (signature[3] + 1,)
                return signature

            with mock.patch.object(
                assembler,
                "_fstat_signature",
                side_effect=changing_signature,
            ):
                with self.assertRaises(assembler.PhaseAssemblyError):
                    assembler._read_snapshot(str(path), len(self.raw))

    def test_site_map_stream_digest_obeys_exact_byte_limit(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "site-map.private"
            path.write_bytes(_SYNTHETIC_SITE_MAP)
            size, digest = assembler._digest_snapshot(
                str(path), len(_SYNTHETIC_SITE_MAP)
            )
            self.assertEqual(size, len(_SYNTHETIC_SITE_MAP))
            self.assertEqual(digest, _SITE_MAP_DIGEST.hex())
            with self.assertRaises(assembler.PhaseAssemblyError):
                assembler._digest_snapshot(
                    str(path), len(_SYNTHETIC_SITE_MAP) - 1
                )

    def test_short_output_write_is_left_in_place_without_unlink(self) -> None:
        class ShortWriter:
            def __enter__(self) -> ShortWriter:
                return self

            def __exit__(self, *_args: object) -> None:
                return None

            def write(self, payload: object) -> int:
                return max(0, len(payload) - 1)  # type: ignore[arg-type]

            def flush(self) -> None:
                return None

        with mock.patch("builtins.open", return_value=ShortWriter()), mock.patch(
            "pathlib.Path.unlink"
        ) as unlink:
            with self.assertRaises(OSError):
                assembler._write_new_file("synthetic-output", b"payload")
        unlink.assert_not_called()

    def test_cli_never_echoes_private_paths_or_exception_text(self) -> None:
        secret = "ZXQ_PRIVATE_PHASE_PATH_731"
        with tempfile.TemporaryDirectory(prefix=secret) as directory:
            root = Path(directory)
            args = cli_args(root, self.raw)
            Path(args[-1]).write_bytes(b"not-a-fragment")
            code, stdout, stderr = run_main(args)
        self.assertEqual((code, stdout, stderr), (1, "", "Phase fragment processing failed.\n"))
        self.assertNotIn(secret, stdout + stderr)

        with mock.patch.object(
            assembler,
            "assemble_phase_paths",
            side_effect=RuntimeError(f"{secret}: internal private detail"),
        ):
            with tempfile.TemporaryDirectory() as directory:
                code, stdout, stderr = run_main(cli_args(Path(directory), self.raw))
        self.assertEqual((code, stdout, stderr), (1, "", "Phase fragment processing failed.\n"))
        self.assertNotIn(secret, stdout + stderr)

    def test_real_process_runs_isolated_and_default_output_is_public_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            args = cli_args(root, self.raw)
            completed = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    "-E",
                    "-s",
                    "-S",
                    "-B",
                    str(MODULE_PATH),
                    *args,
                ],
                cwd=directory,
                env={**os.environ, "PYTHONPATH": str(root / "hostile")},
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=10,
            )
            self.assertEqual(completed.returncode, 0)
            self.assertEqual(completed.stdout, b"Phase fragment processing succeeded.\n")
            self.assertEqual(completed.stderr, b"")
            self.assertEqual(
                json.loads((root / "public.json").read_bytes())["schema"],
                assembler.PUBLIC_REPORT_SCHEMA,
            )
            self.assertFalse((root / "private.json").exists())


if __name__ == "__main__":
    unittest.main()
