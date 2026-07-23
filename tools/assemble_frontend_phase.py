#!/usr/bin/env python3
"""Assemble a bounded private front-end phase fragment into a sanitized observation.

The input format is the fixed-width `OMEGAFRPHASE0001` research fragment. No
producer implements it yet: this module defines and validates the public wire
contract only (see `analysis/formats/FRONTEND-PHASE.md`). Input bytes and all
source identities remain private.

The fragment records only which anonymous front-end call/interval
("invocation") ordinal was open when the vertices behind a finalized GS draw
were submitted. It never proves ownership, visibility, occlusion, glyph
identity, asset identity, pixel contribution, or causality, and it carries no
invocation name, source location, symbol, address, or string of any kind.
"""

from __future__ import annotations

import hashlib
import json
import sys
from dataclasses import dataclass
from typing import Final, TextIO, cast
from collections.abc import Sequence


FRAGMENT_MAGIC: Final = b"OMEGAFRPHASE0001"
FRAGMENT_VERSION: Final = 1
PHASE_OBSERVATION_SCHEMA: Final = "openomega-sanitized-frontend-phase-observation-v1"

MAX_FRAGMENT_BYTES: Final = 4 * 1024 * 1024
MAX_FRAGMENTS: Final = 8
MAX_FRAMES: Final = 600
MAX_INVOCATIONS: Final = 4_096
MAX_EVENTS: Final = 8_192
MAX_DRAWS: Final = 32_768
MAX_EDGES: Final = 131_072

_CLOSED_KIND_ENTER: Final = 0
_CLOSED_KIND_EXIT: Final = 1

_COUNT_NAMES: Final = ("invocations", "events", "draws", "edges")
_COUNT_LIMITS: Final = (MAX_INVOCATIONS, MAX_EVENTS, MAX_DRAWS, MAX_EDGES)
_ROW_WIDTHS: Final = (20, 17, 13, 8)
_HEADER_BYTES: Final = len(FRAGMENT_MAGIC) + 4 + 32 + len(_COUNT_NAMES) * 4

_PHASE_KEYS: Final = (
    "schema",
    "invocation_parents",
    "draw_count",
    "skipped_draw_count",
    "phase_draw_edges",
)


class PhaseFragmentValidationError(ValueError):
    """The input is not an accepted bounded front-end phase fragment."""


class PhaseAssemblyError(ValueError):
    """Validated fragments cannot be assembled without unsupported inference."""


def _fragment_fail(message: str) -> PhaseFragmentValidationError:
    return PhaseFragmentValidationError(message)


def _assembly_fail(message: str) -> PhaseAssemblyError:
    return PhaseAssemblyError(message)


@dataclass(frozen=True)
class _InvocationRow:
    frame: int
    parent_invocation: int
    enter_event: int
    exit_event: int


@dataclass(frozen=True)
class _EventRow:
    frame: int
    invocation: int
    closed_kind: int


@dataclass(frozen=True)
class ParsedPhaseFragment:
    """Minimal post-validation state; per-event and per-draw detail is discarded."""

    runtime_config_digest: bytes
    invocation_parents: tuple[int, ...]
    draw_count: int
    skipped_draw_count: int
    phase_draw_edges: tuple[tuple[int, int], ...]


class _Reader:
    def __init__(self, raw: bytes) -> None:
        self._raw = raw
        self.offset = 0

    def bytes(self, size: int) -> bytes:
        if size < 0 or self.offset > len(self._raw) - size:
            raise _fragment_fail("phase fragment is truncated")
        begin = self.offset
        self.offset += size
        return self._raw[begin : self.offset]

    def u8(self) -> int:
        return self.bytes(1)[0]

    def u32(self) -> int:
        return int.from_bytes(self.bytes(4), "little", signed=False)


def _require_dense_id(actual: int, index: int) -> None:
    if actual != index + 1:
        raise _fragment_fail("descriptor IDs are not dense one-based ordinals")


def _require_reference(value: int, count: int, message: str) -> None:
    if value == 0 or value > count:
        raise _fragment_fail(message)


def _validate_count_table(counts: tuple[int, ...], raw_size: int) -> None:
    for count, limit in zip(counts, _COUNT_LIMITS, strict=True):
        if count > limit:
            raise _fragment_fail("a phase fragment table exceeds its fixed capacity")
    invocation_count, event_count, draw_count, edge_count = counts
    if invocation_count == 0 or event_count == 0 or draw_count == 0 or edge_count == 0:
        raise _fragment_fail("required phase fragment descriptor tables are empty")
    # Every invocation requires exactly one Enter and one Exit event, and every
    # event is claimed by exactly one invocation (enforced below), so a
    # well-formed fragment always has twice as many events as invocations.
    # Checking this before allocating row state rejects a malformed count
    # header without reading a single row.
    if event_count != invocation_count * 2:
        raise _fragment_fail("event count must be exactly twice the invocation count")

    expected_size = _HEADER_BYTES
    for count, width in zip(counts, _ROW_WIDTHS, strict=True):
        expected_size += count * width
    if expected_size > MAX_FRAGMENT_BYTES or expected_size != raw_size:
        raise _fragment_fail("phase fragment length does not match its table counts")


def parse_phase_fragment(raw: bytes) -> ParsedPhaseFragment:
    """Strictly validate one producer fragment and discard event/draw detail."""

    if type(raw) is not bytes:
        raise _fragment_fail("phase fragment input must be immutable bytes")
    if len(raw) > MAX_FRAGMENT_BYTES:
        raise _fragment_fail("phase fragment exceeds the maximum byte size")
    if len(raw) < _HEADER_BYTES:
        raise _fragment_fail("phase fragment is truncated")

    reader = _Reader(raw)
    if reader.bytes(len(FRAGMENT_MAGIC)) != FRAGMENT_MAGIC:
        raise _fragment_fail("phase fragment magic is unsupported")
    if reader.u32() != FRAGMENT_VERSION:
        raise _fragment_fail("phase fragment version is unsupported")
    runtime_config_digest = reader.bytes(32)
    if not any(runtime_config_digest):
        raise _fragment_fail("phase fragment configuration digest is invalid")

    counts = tuple(reader.u32() for _ in _COUNT_NAMES)
    _validate_count_table(counts, len(raw))
    invocation_count, event_count, draw_count, edge_count = counts

    invocations: list[_InvocationRow] = []
    for index in range(invocation_count):
        _require_dense_id(reader.u32(), index)
        frame = reader.u32()
        parent_invocation = reader.u32()
        enter_event = reader.u32()
        exit_event = reader.u32()
        if frame == 0 or frame > MAX_FRAMES:
            raise _fragment_fail("invocation frame is outside the bounded capture window")
        if invocations and frame < invocations[-1].frame:
            raise _fragment_fail("invocation frame values are not non-decreasing")
        if parent_invocation != 0 and parent_invocation > index:
            raise _fragment_fail("invocation parent reference is not an earlier invocation")
        if enter_event == 0 or exit_event == 0:
            raise _fragment_fail("invocation is missing an enter or exit event reference")
        if enter_event >= exit_event:
            raise _fragment_fail("invocation enter event must precede its exit event")
        invocations.append(_InvocationRow(frame, parent_invocation, enter_event, exit_event))

    events: list[_EventRow] = []
    for index in range(event_count):
        _require_dense_id(reader.u32(), index)
        frame = reader.u32()
        invocation = reader.u32()
        closed_kind = reader.u8()
        reserved = reader.u32()
        if frame == 0 or frame > MAX_FRAMES:
            raise _fragment_fail("event frame is outside the bounded capture window")
        if events and frame < events[-1].frame:
            raise _fragment_fail("event frame values are not non-decreasing")
        _require_reference(invocation, invocation_count, "event invocation reference is invalid")
        if closed_kind not in (_CLOSED_KIND_ENTER, _CLOSED_KIND_EXIT):
            raise _fragment_fail("event closed_kind is unsupported")
        if reserved != 0:
            raise _fragment_fail("event reserved field must be zero")
        events.append(_EventRow(frame, invocation, closed_kind))

    # Invocation and event rows reference each other, so this cross-check runs
    # only after both tables are fully read. Given the exact event-count-is-
    # twice-invocation-count precondition above, a fixed event row can name at
    # most one invocation as its enter (or exit) owner, so this loop alone
    # already forces a bijection between invocations' claims and event rows;
    # no separate orphan/double-claim totality check can find anything new.
    for own_index, invocation_row in enumerate(invocations):
        own_id = own_index + 1
        _require_reference(
            invocation_row.enter_event, event_count, "invocation enter event reference is invalid"
        )
        _require_reference(
            invocation_row.exit_event, event_count, "invocation exit event reference is invalid"
        )
        enter = events[invocation_row.enter_event - 1]
        exit_ = events[invocation_row.exit_event - 1]
        if enter.invocation != own_id or enter.closed_kind != _CLOSED_KIND_ENTER:
            raise _fragment_fail("invocation enter event does not name this invocation")
        if exit_.invocation != own_id or exit_.closed_kind != _CLOSED_KIND_EXIT:
            raise _fragment_fail("invocation exit event does not name this invocation")
        if enter.frame != invocation_row.frame:
            raise _fragment_fail("invocation frame contradicts its enter event")

    draw_dispositions: list[int] = []
    prior_draw_frame = 0
    for index in range(draw_count):
        _require_dense_id(reader.u32(), index)
        frame = reader.u32()
        submitted_or_skipped = reader.u8()
        reserved = reader.u32()
        if frame == 0 or frame > MAX_FRAMES:
            raise _fragment_fail("draw frame is outside the bounded capture window")
        if frame < prior_draw_frame:
            raise _fragment_fail("draw frame values are not non-decreasing")
        prior_draw_frame = frame
        if submitted_or_skipped not in (0, 1):
            raise _fragment_fail("draw submitted_or_skipped is unsupported")
        if reserved != 0:
            raise _fragment_fail("draw reserved field must be zero")
        draw_dispositions.append(submitted_or_skipped)

    seen_edges: set[tuple[int, int]] = set()
    prior_draw = 0
    raw_edges: list[tuple[int, int]] = []
    for _ in range(edge_count):
        event = reader.u32()
        draw = reader.u32()
        _require_reference(event, event_count, "phase/draw edge event reference is invalid")
        _require_reference(draw, draw_count, "phase/draw edge draw reference is invalid")
        if events[event - 1].closed_kind != _CLOSED_KIND_ENTER:
            raise _fragment_fail("phase/draw edge must reference an Enter-kind event")
        if draw < prior_draw:
            raise _fragment_fail("phase/draw edges are not in producer draw order")
        prior_draw = draw
        pair = (event, draw)
        if pair in seen_edges:
            raise _fragment_fail("phase/draw edge table contains duplicate rows")
        seen_edges.add(pair)
        raw_edges.append(pair)

    if reader.offset != len(raw):
        raise _fragment_fail("phase fragment contains trailing bytes")

    event_to_invocation = {
        invocation_row.enter_event: own_index + 1
        for own_index, invocation_row in enumerate(invocations)
    }
    edge_pairs = sorted((draw, event_to_invocation[event]) for event, draw in raw_edges)
    phase_draw_edges = tuple((invocation, draw) for draw, invocation in edge_pairs)

    return ParsedPhaseFragment(
        runtime_config_digest=runtime_config_digest,
        invocation_parents=tuple(row.parent_invocation for row in invocations),
        draw_count=draw_count,
        skipped_draw_count=sum(1 for disposition in draw_dispositions if disposition == 1),
        phase_draw_edges=phase_draw_edges,
    )


def _canonical_json_bytes(document: object) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=True,
            allow_nan=False,
            separators=(",", ":"),
            sort_keys=False,
        ).encode("ascii")
        + b"\n"
    )


def _validate_expected_sha256(value: str | None) -> str | None:
    if value is None:
        return None
    if len(value) != 64 or any(character not in "0123456789abcdefABCDEF" for character in value):
        raise _assembly_fail("expected fragment SHA-256 is malformed")
    return value.lower()


def assemble_phase_document(
    raw_fragments: Sequence[bytes],
    *,
    expected_fragment_sha256: str | None = None,
) -> dict[str, object]:
    """Validate repeat fragments and return the anonymous sanitized observation."""

    if not isinstance(raw_fragments, Sequence) or isinstance(raw_fragments, (bytes, bytearray, str)):
        raise _assembly_fail("phase fragment collection must be a sequence")
    if not raw_fragments or len(raw_fragments) > MAX_FRAGMENTS:
        raise _assembly_fail("phase fragment count is outside its fixed bound")
    expected_digest = _validate_expected_sha256(expected_fragment_sha256)

    reference_raw: bytes | None = None
    reference_hash: str | None = None
    parsed_fragments: list[ParsedPhaseFragment] = []
    for raw in raw_fragments:
        if type(raw) is not bytes:
            raise _assembly_fail("phase fragments must be immutable bytes")
        raw_hash = hashlib.sha256(raw).hexdigest()
        if expected_digest is not None and raw_hash != expected_digest:
            raise _assembly_fail("phase fragment SHA-256 does not match the expected digest")
        if reference_hash is None:
            reference_hash = raw_hash
            reference_raw = raw
        elif raw_hash != reference_hash or raw != reference_raw:
            raise _assembly_fail("repeat phase fragments are not byte-identical")
        parsed_fragments.append(parse_phase_fragment(raw))

    reference = parsed_fragments[0]
    for parsed in parsed_fragments[1:]:
        if parsed.runtime_config_digest != reference.runtime_config_digest:
            raise _assembly_fail("repeat phase fragments use different configuration digests")

    document: dict[str, object] = {
        "schema": PHASE_OBSERVATION_SCHEMA,
        "invocation_parents": list(reference.invocation_parents),
        "draw_count": reference.draw_count,
        "skipped_draw_count": reference.skipped_draw_count,
        "phase_draw_edges": [list(pair) for pair in reference.phase_draw_edges],
    }
    validate_phase_document(document)
    return document


def _require_exact_keys(value: dict[str, object], keys: tuple[str, ...]) -> None:
    if tuple(value) != keys:
        raise _assembly_fail("phase observation keys are missing, extra, or noncanonical")


def _require_plain_int(value: object, message: str) -> int:
    if type(value) is not int:
        raise _assembly_fail(message)
    return value


def validate_phase_document(document: object) -> dict[str, object]:
    """Validate the non-semantic sanitized phase observation."""

    if not isinstance(document, dict):
        raise _assembly_fail("phase observation root must be an object")
    root = cast(dict[str, object], document)
    _require_exact_keys(root, _PHASE_KEYS)
    if root["schema"] != PHASE_OBSERVATION_SCHEMA:
        raise _assembly_fail("phase observation schema is unsupported")

    invocation_parents = root["invocation_parents"]
    if not isinstance(invocation_parents, list) or not invocation_parents:
        raise _assembly_fail("phase observation invocation_parents must be a nonempty array")
    if len(invocation_parents) > MAX_INVOCATIONS:
        raise _assembly_fail("phase observation invocation_parents exceeds its fixed bound")
    for index, parent in enumerate(invocation_parents):
        parent_value = _require_plain_int(
            parent, "phase observation invocation parent must be an integer"
        )
        if parent_value < 0 or parent_value > index:
            raise _assembly_fail("phase observation invocation parent is not an earlier ordinal")

    draw_count = _require_plain_int(
        root["draw_count"], "phase observation draw_count must be an integer"
    )
    skipped_draw_count = _require_plain_int(
        root["skipped_draw_count"], "phase observation skipped_draw_count must be an integer"
    )
    if draw_count <= 0 or draw_count > MAX_DRAWS:
        raise _assembly_fail("phase observation draw_count is outside its fixed bound")
    if skipped_draw_count < 0 or skipped_draw_count > draw_count:
        raise _assembly_fail("phase observation skipped_draw_count contradicts draw_count")

    edges = root["phase_draw_edges"]
    if not isinstance(edges, list) or not edges:
        raise _assembly_fail("phase observation phase_draw_edges must be a nonempty array")
    if len(edges) > MAX_EDGES:
        raise _assembly_fail("phase observation phase_draw_edges exceeds its fixed bound")

    seen_pairs: set[tuple[int, int]] = set()
    prior_sort_key: tuple[int, int] | None = None
    for edge in edges:
        if not isinstance(edge, list) or len(edge) != 2:
            raise _assembly_fail("phase observation edge must be a two-element array")
        invocation = _require_plain_int(edge[0], "phase observation edge invocation must be an integer")
        draw = _require_plain_int(edge[1], "phase observation edge draw must be an integer")
        if invocation < 1 or invocation > len(invocation_parents):
            raise _assembly_fail("phase observation edge invocation ordinal is invalid")
        if draw < 1 or draw > draw_count:
            raise _assembly_fail("phase observation edge draw ordinal is invalid")
        pair = (invocation, draw)
        if pair in seen_pairs:
            raise _assembly_fail("phase observation edge table contains duplicate rows")
        seen_pairs.add(pair)
        sort_key = (draw, invocation)
        if prior_sort_key is not None and sort_key < prior_sort_key:
            raise _assembly_fail("phase observation edges are not canonically sorted")
        prior_sort_key = sort_key
    return root


def encode_phase_document(document: object) -> bytes:
    """Return the sole canonical byte encoding for a sanitized phase document."""

    validated = validate_phase_document(document)
    return _canonical_json_bytes(validated)


def _read_fragment(path: str) -> bytes:
    with open(path, "rb") as stream:
        raw = stream.read(MAX_FRAGMENT_BYTES + 1)
    if len(raw) > MAX_FRAGMENT_BYTES:
        raise _fragment_fail("phase fragment exceeds the maximum byte size")
    return raw


@dataclass(frozen=True)
class _CliOptions:
    output: str
    fragments: tuple[str, ...]
    expected_sha256: str | None


def _parse_cli(args: Sequence[str]) -> _CliOptions:
    output: str | None = None
    expected_sha256: str | None = None
    fragments: list[str] = []
    index = 0
    while index < len(args):
        argument = args[index]
        if argument in ("--output", "--expected-sha256"):
            index += 1
            if index >= len(args):
                raise _assembly_fail("CLI option is missing its value")
            value = args[index]
            if argument == "--output":
                if output is not None:
                    raise _assembly_fail("output option was repeated")
                output = value
            elif argument == "--expected-sha256":
                if expected_sha256 is not None:
                    raise _assembly_fail("expected SHA-256 option was repeated")
                expected_sha256 = value
        elif argument.startswith("-"):
            raise _assembly_fail("CLI option is unsupported")
        else:
            fragments.append(argument)
        index += 1

    if output is None or not output:
        raise _assembly_fail("required CLI options are missing")
    if not fragments or len(fragments) > MAX_FRAGMENTS:
        raise _assembly_fail("phase fragment count is outside its fixed bound")
    return _CliOptions(output, tuple(fragments), expected_sha256)


def _write_new_file(path: str, payload: bytes) -> None:
    # Leave a short/failed exclusive output in place. Deleting later by pathname
    # would race a replacement in a shared directory and could remove a file this
    # process did not create. A retry must always choose or explicitly clear a path.
    with open(path, "xb") as stream:
        if stream.write(payload) != len(payload):
            raise OSError("short phase document write")
        stream.flush()


def _write_ascii_line(stream: TextIO, value: str) -> None:
    payload = value.encode("ascii") + b"\n"
    binary = getattr(stream, "buffer", None)
    if binary is not None:
        binary.write(payload)
    else:
        stream.write(payload.decode("ascii"))


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point with diagnostics that cannot disclose private input data."""

    args = list(sys.argv[1:] if argv is None else argv)
    if args in (["-h"], ["--help"]):
        _write_ascii_line(
            sys.stdout,
            "usage: assemble_frontend_phase.py [--expected-sha256 HEX] "
            "--output OUTPUT FRAGMENT [FRAGMENT ...]",
        )
        return 0

    try:
        options = _parse_cli(args)
        raw_fragments = [_read_fragment(path) for path in options.fragments]
        document = assemble_phase_document(
            raw_fragments,
            expected_fragment_sha256=options.expected_sha256,
        )
        _write_new_file(options.output, encode_phase_document(document))
    except Exception:
        _write_ascii_line(sys.stderr, "Phase fragment assembly failed.")
        return 1

    _write_ascii_line(sys.stdout, "Phase fragment assembly succeeded.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
