#!/usr/bin/env python3
"""Validate a private front-end phase trace and reduce it to a public report.

``OMEGAFRPHASE0002`` is a generated-fixture-only research contract.  The
detailed normalized trace is private and may be written only through the
explicit ``--private-output`` option.  The default public report contains
fixed categories and aggregate counts only.  Neither output assigns retail
semantics or promotes an ordering rule.
"""

from __future__ import annotations

import hashlib
import json
import os
import sys
from collections.abc import Callable, Sequence
from dataclasses import dataclass, fields, replace
from enum import IntEnum
from pathlib import Path
from typing import BinaryIO, Final, TextIO, cast


FRAGMENT_MAGIC: Final = b"OMEGAFRPHASE0002"
FRAGMENT_VERSION: Final = 2
PRIVATE_TRACE_SCHEMA: Final = "openomega-private-frontend-phase-trace-v2"
PUBLIC_REPORT_SCHEMA: Final = "openomega-public-frontend-phase-report-v1"
SITE_MAP_BINDING_SCHEMA: Final = "openomega-private-frontend-site-map-binding-v1"
CAPTURE_MANIFEST_SCHEMA: Final = "openomega-private-capture-domain-manifest-v1"

MAX_FRAGMENT_BYTES: Final = 4 * 1024 * 1024
MAX_TOTAL_INPUT_BYTES: Final = 32 * 1024 * 1024
MAX_FRAGMENTS: Final = 8
MAX_MANIFEST_BYTES: Final = 64 * 1024
MAX_STRING_BYTES: Final = 256
MAX_FRAMES: Final = 600
MAX_SITES: Final = 4_096
MAX_NESTING_DEPTH: Final = 256
MAX_INVOCATIONS: Final = 4_096
MAX_EVENTS: Final = 8_192
MAX_SUBMISSIONS: Final = 131_072
MAX_DRAWS: Final = 32_768
MAX_EDGES: Final = 131_072
MAX_FAILURE_RECORDS: Final = 131_072
MAX_LOOKUP_WORK: Final = 1_048_576
MAX_SCRATCH_BYTES: Final = 16 * 1024 * 1024
MAX_PRIVATE_OUTPUT_BYTES: Final = 16 * 1024 * 1024
MAX_PUBLIC_AGGREGATE_ROWS: Final = 8
MAX_PUBLIC_OUTPUT_BYTES: Final = 4 * 1024
MAX_CLI_ARGUMENTS: Final = 64
MAX_CLI_ARGUMENT_BYTES: Final = 32 * 1024
_READ_CHUNK_BYTES: Final = 64 * 1024

_COUNT_NAMES: Final = ("sites", "invocations", "events", "submissions", "draws", "edges")
_ROW_WIDTHS: Final = (4, 24, 21, 24, 17, 8)
_PUBLIC_COUNT_CATEGORIES: Final = (
    "site-records",
    "invocation-records",
    "lifecycle-records",
    "submission-records",
    "final-draw-records",
    "submitted-draw-records",
    "skipped-draw-records",
    "membership-records",
)
_FAILURE_COUNTER_NAMES: Final = (
    "overflow_records",
    "dropped_records",
    "vm_reset_count",
    "savestate_discontinuity_count",
    "queue_exhaustion_count",
    "output_failure_count",
    "internal_failure_count",
)
_HEADER_BYTES: Final = (
    len(FRAGMENT_MAGIC)
    + 4
    + 32 * 3
    + 4  # retained frame count
    + 4  # terminal status
    + 4  # commit state
    + 4 * len(_FAILURE_COUNTER_NAMES)
    + 4 * len(_COUNT_NAMES) * 2  # discovered and retained/table counts
)


class PhaseFragmentValidationError(ValueError):
    """The input is not an accepted bounded private phase fragment."""


class PhaseAssemblyError(ValueError):
    """Validated private inputs cannot be joined or reduced safely."""


def _fragment_fail(message: str) -> PhaseFragmentValidationError:
    return PhaseFragmentValidationError(message)


def _assembly_fail(message: str) -> PhaseAssemblyError:
    return PhaseAssemblyError(message)


class TerminalStatus(IntEnum):
    Complete = 0
    TelemetryOverflow = 1
    TelemetryDropped = 2
    VmReset = 3
    SavestateDiscontinuity = 4
    QueueExhausted = 5
    ProducerAborted = 6
    OutputFailure = 7
    InternalFailure = 8


class CommitState(IntEnum):
    Aborted = 0
    Committed = 1


class EventKind(IntEnum):
    Enter = 0
    Exit = 1


class DrawDisposition(IntEnum):
    Submitted = 0
    Skipped = 1


HARD_LIMITS: Final[dict[str, int]] = {
    "fragment_bytes": MAX_FRAGMENT_BYTES,
    "total_input_bytes": MAX_TOTAL_INPUT_BYTES,
    "fragments": MAX_FRAGMENTS,
    "manifest_bytes": MAX_MANIFEST_BYTES,
    "string_bytes": MAX_STRING_BYTES,
    "frames": MAX_FRAMES,
    "sites": MAX_SITES,
    "nesting_depth": MAX_NESTING_DEPTH,
    "invocations": MAX_INVOCATIONS,
    "events": MAX_EVENTS,
    "submissions": MAX_SUBMISSIONS,
    "draws": MAX_DRAWS,
    "edges": MAX_EDGES,
    "failure_records": MAX_FAILURE_RECORDS,
    "lookup_work": MAX_LOOKUP_WORK,
    "scratch_bytes": MAX_SCRATCH_BYTES,
    "private_output_bytes": MAX_PRIVATE_OUTPUT_BYTES,
    "public_aggregate_rows": MAX_PUBLIC_AGGREGATE_ROWS,
    "public_output_bytes": MAX_PUBLIC_OUTPUT_BYTES,
}


@dataclass(frozen=True)
class PhaseLimits:
    """Caller-tightenable capacities beneath immutable hard ceilings."""

    fragment_bytes: int = MAX_FRAGMENT_BYTES
    total_input_bytes: int = MAX_TOTAL_INPUT_BYTES
    fragments: int = MAX_FRAGMENTS
    manifest_bytes: int = MAX_MANIFEST_BYTES
    string_bytes: int = MAX_STRING_BYTES
    frames: int = MAX_FRAMES
    sites: int = MAX_SITES
    nesting_depth: int = MAX_NESTING_DEPTH
    invocations: int = MAX_INVOCATIONS
    events: int = MAX_EVENTS
    submissions: int = MAX_SUBMISSIONS
    draws: int = MAX_DRAWS
    edges: int = MAX_EDGES
    failure_records: int = MAX_FAILURE_RECORDS
    lookup_work: int = MAX_LOOKUP_WORK
    scratch_bytes: int = MAX_SCRATCH_BYTES
    private_output_bytes: int = MAX_PRIVATE_OUTPUT_BYTES
    public_aggregate_rows: int = MAX_PUBLIC_AGGREGATE_ROWS
    public_output_bytes: int = MAX_PUBLIC_OUTPUT_BYTES

    def __post_init__(self) -> None:
        for item in fields(self):
            value = getattr(self, item.name)
            if type(value) is not int or value < 0 or value > HARD_LIMITS[item.name]:
                raise _assembly_fail("a caller phase limit is invalid")


DEFAULT_LIMITS: Final = PhaseLimits()


@dataclass(frozen=True)
class SiteMapBinding:
    runtime_config_digest: bytes
    site_map_digest: bytes
    site_count: int


@dataclass(frozen=True)
class CaptureDomainManifest:
    phase_capture_domain: bytes
    scene_capture_domain: bytes
    frontend_capture_domain: bytes


@dataclass(frozen=True)
class SiteRow:
    ordinal: int


@dataclass(frozen=True)
class InvocationRow:
    ordinal: int
    site: int
    lane: int
    parent: int
    enter_event: int
    exit_event: int


@dataclass(frozen=True)
class EventRow:
    ordinal: int
    sequence: int
    frame: int
    invocation: int
    kind: EventKind


@dataclass(frozen=True)
class SubmissionRow:
    ordinal: int
    sequence: int
    frame: int
    invocation: int
    primitive_begin: int
    primitive_count: int


@dataclass(frozen=True)
class DrawRow:
    ordinal: int
    sequence: int
    frame: int
    disposition: DrawDisposition


@dataclass(frozen=True)
class MembershipRow:
    submission: int
    draw: int


@dataclass(frozen=True)
class TerminalRecord:
    status: TerminalStatus
    commit_state: CommitState
    failure_counts: tuple[int, ...]
    discovered_counts: tuple[int, ...]
    retained_counts: tuple[int, ...]


@dataclass(frozen=True)
class ParsedPhaseFragment:
    """Fully validated private trace; no semantic promotion is implied."""

    capture_domain: bytes
    runtime_config_digest: bytes
    site_map_digest: bytes
    frame_count: int
    terminal: TerminalRecord
    sites: tuple[SiteRow, ...]
    invocations: tuple[InvocationRow, ...]
    events: tuple[EventRow, ...]
    submissions: tuple[SubmissionRow, ...]
    draws: tuple[DrawRow, ...]
    memberships: tuple[MembershipRow, ...]

    @property
    def ordering_evidence_valid(self) -> bool:
        return (
            self.terminal.status is TerminalStatus.Complete
            and self.terminal.commit_state is CommitState.Committed
            and not any(self.terminal.failure_counts)
            and self.terminal.discovered_counts == self.terminal.retained_counts
        )


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


def _logical_scratch_bytes(counts: tuple[int, ...]) -> int:
    return sum(count * (width + 16) for count, width in zip(counts, _ROW_WIDTHS, strict=True))


def _logical_lookup_work(counts: tuple[int, ...]) -> int:
    sites, invocations, events, submissions, draws, edges = counts
    return sites + invocations * 5 + events * 3 + submissions * 5 + draws * 3 + edges * 5


def _validate_counts(
    counts: tuple[int, ...],
    discovered_counts: tuple[int, ...],
    raw_size: int,
    limits: PhaseLimits,
) -> None:
    limit_values = (
        limits.sites,
        limits.invocations,
        limits.events,
        limits.submissions,
        limits.draws,
        limits.edges,
    )
    for retained, discovered, limit in zip(counts, discovered_counts, limit_values, strict=True):
        if retained > limit or discovered > limit:
            raise _fragment_fail("a phase fragment table exceeds a caller capacity")
        if discovered < retained:
            raise _fragment_fail("a discovered table count is smaller than its retained count")

    if counts[2] != counts[1] * 2:
        raise _fragment_fail("event count must be exactly twice the invocation count")
    if _logical_scratch_bytes(counts) > limits.scratch_bytes:
        raise _fragment_fail("phase fragment logical scratch exceeds the caller capacity")
    if _logical_lookup_work(counts) > limits.lookup_work:
        raise _fragment_fail("phase fragment lookup work exceeds the caller capacity")

    expected_size = _HEADER_BYTES
    for count, width in zip(counts, _ROW_WIDTHS, strict=True):
        expected_size += count * width
    if expected_size > limits.fragment_bytes or expected_size != raw_size:
        raise _fragment_fail("phase fragment length does not match its bounded table counts")


def _validate_terminal(
    status_value: int,
    commit_value: int,
    failure_counts: tuple[int, ...],
    discovered_counts: tuple[int, ...],
    retained_counts: tuple[int, ...],
    limits: PhaseLimits,
) -> TerminalRecord:
    try:
        status = TerminalStatus(status_value)
        commit_state = CommitState(commit_value)
    except ValueError as error:
        raise _fragment_fail("phase fragment terminal state is unsupported") from error

    if sum(failure_counts) > limits.failure_records:
        raise _fragment_fail("phase fragment failure accounting exceeds the caller capacity")

    if status is TerminalStatus.Complete:
        if commit_state is not CommitState.Committed:
            raise _fragment_fail("a complete phase fragment was not committed")
        if any(failure_counts):
            raise _fragment_fail("a complete phase fragment reports a failure")
        if discovered_counts != retained_counts:
            raise _fragment_fail("a complete phase fragment has unreconciled records")
    else:
        if commit_state is not CommitState.Aborted:
            raise _fragment_fail("an incomplete phase fragment was committed")
        nonzero = [index for index, count in enumerate(failure_counts) if count]
        if status is TerminalStatus.ProducerAborted:
            if nonzero:
                raise _fragment_fail("producer-aborted status contradicts failure counters")
        else:
            expected_statuses = (
                TerminalStatus.TelemetryOverflow,
                TerminalStatus.TelemetryDropped,
                TerminalStatus.VmReset,
                TerminalStatus.SavestateDiscontinuity,
                TerminalStatus.QueueExhausted,
                TerminalStatus.OutputFailure,
                TerminalStatus.InternalFailure,
            )
            if not nonzero or status is not expected_statuses[nonzero[0]]:
                raise _fragment_fail("terminal status does not match failure-counter precedence")

    return TerminalRecord(
        status=status,
        commit_state=commit_state,
        failure_counts=failure_counts,
        discovered_counts=discovered_counts,
        retained_counts=retained_counts,
    )


def _validate_temporal_model(
    frame_count: int,
    terminal: TerminalRecord,
    invocations: list[InvocationRow],
    events: list[EventRow],
    submissions: list[SubmissionRow],
    draws: list[DrawRow],
    memberships: list[MembershipRow],
    limits: PhaseLimits,
) -> None:
    del terminal  # Structural validity is required even for an explicitly incomplete capture.

    invocation_count = len(invocations)
    event_count = len(events)
    draw_count = len(draws)

    for invocation in invocations:
        _require_reference(
            invocation.enter_event, event_count, "invocation enter event reference is invalid"
        )
        _require_reference(
            invocation.exit_event, event_count, "invocation exit event reference is invalid"
        )

    enter_sequences = [0] * invocation_count
    exit_sequences = [0] * invocation_count
    temporal_count = len(events) + len(submissions) + len(draws)
    temporal: list[tuple[int, int] | None] = [None] * temporal_count

    def retain_temporal(sequence: int, kind: int, ordinal: int) -> None:
        if sequence == 0 or sequence > temporal_count:
            raise _fragment_fail("temporal sequence contains a gap")
        if temporal[sequence - 1] is not None:
            raise _fragment_fail("temporal sequence contains a duplicate")
        temporal[sequence - 1] = (kind, ordinal)

    for event in events:
        retain_temporal(event.sequence, 0, event.ordinal)
    for submission in submissions:
        retain_temporal(submission.sequence, 1, submission.ordinal)
    for draw in draws:
        retain_temporal(draw.sequence, 2, draw.ordinal)
    if any(item is None for item in temporal):
        raise _fragment_fail("temporal sequence contains a gap")

    stack: list[int] = []
    for sequence, temporal_item in enumerate(temporal, start=1):
        assert temporal_item is not None
        kind, ordinal = temporal_item
        if kind != 0:
            continue
        event = events[ordinal - 1]
        invocation = invocations[event.invocation - 1]
        if event.kind is EventKind.Enter:
            expected_parent = stack[-1] if stack else 0
            if invocation.parent != expected_parent:
                raise _fragment_fail("invocation lifecycle violates single-lane stack discipline")
            if invocation.enter_event != event.ordinal:
                raise _fragment_fail("invocation enter event does not name this invocation")
            stack.append(event.invocation)
            if len(stack) > limits.nesting_depth:
                raise _fragment_fail("phase fragment nesting exceeds the caller capacity")
            enter_sequences[event.invocation - 1] = sequence
        else:
            if not stack or stack[-1] != event.invocation:
                raise _fragment_fail("invocation lifecycle intervals cross or are orphaned")
            if invocation.exit_event != event.ordinal:
                raise _fragment_fail("invocation exit event does not name this invocation")
            stack.pop()
            exit_sequences[event.invocation - 1] = sequence
    if stack or any(sequence == 0 for sequence in enter_sequences + exit_sequences):
        raise _fragment_fail("invocation lifecycle is incomplete")

    event_by_id = {event.ordinal: event for event in events}
    for invocation in invocations:
        enter = event_by_id[invocation.enter_event]
        exit_ = event_by_id[invocation.exit_event]
        if enter.invocation != invocation.ordinal or enter.kind is not EventKind.Enter:
            raise _fragment_fail("invocation enter event does not name this invocation")
        if exit_.invocation != invocation.ordinal or exit_.kind is not EventKind.Exit:
            raise _fragment_fail("invocation exit event does not name this invocation")
        if enter.sequence >= exit_.sequence or enter.frame > exit_.frame:
            raise _fragment_fail("invocation lifecycle interval is temporally invalid")
        if invocation.parent:
            parent = invocations[invocation.parent - 1]
            parent_enter = event_by_id[parent.enter_event]
            parent_exit = event_by_id[parent.exit_event]
            if not (
                parent_enter.sequence < enter.sequence < exit_.sequence < parent_exit.sequence
            ):
                raise _fragment_fail("child invocation is not strictly contained by its parent")

    prior_primitive_end = 0
    for submission in submissions:
        invocation_index = submission.invocation - 1
        if not (
            enter_sequences[invocation_index]
            < submission.sequence
            < exit_sequences[invocation_index]
        ):
            raise _fragment_fail("submission context is outside its invocation lifetime")
        enter = event_by_id[invocations[invocation_index].enter_event]
        exit_ = event_by_id[invocations[invocation_index].exit_event]
        if submission.frame < enter.frame or submission.frame > exit_.frame:
            raise _fragment_fail("submission frame is outside its invocation lifetime")
        if submission.primitive_count == 0 or submission.primitive_begin != prior_primitive_end:
            raise _fragment_fail("submission primitive ranges contain a gap or overlap")
        prior_primitive_end += submission.primitive_count
        if prior_primitive_end > 0xFFFFFFFF:
            raise _fragment_fail("submission primitive range overflows u32")

    member_for_submission = [0] * len(submissions)
    members_per_draw = [0] * draw_count
    prior_key: tuple[int, int] | None = None
    for membership in memberships:
        key = (membership.draw, membership.submission)
        if prior_key is not None and key <= prior_key:
            raise _fragment_fail("membership rows are duplicated or out of producer order")
        prior_key = key
        if member_for_submission[membership.submission - 1] != 0:
            raise _fragment_fail("a submission is claimed by more than one draw")
        member_for_submission[membership.submission - 1] = membership.draw
        members_per_draw[membership.draw - 1] += 1

    if any(draw == 0 for draw in member_for_submission):
        raise _fragment_fail("a submission has no final-draw membership")

    for submission in submissions:
        draw = draws[member_for_submission[submission.ordinal - 1] - 1]
        if draw.disposition is not DrawDisposition.Submitted:
            raise _fragment_fail("a skipped draw has a submission membership")
        if draw.sequence <= submission.sequence or draw.frame < submission.frame:
            raise _fragment_fail("a final draw precedes its attributed submission")
        enter = event_by_id[invocations[submission.invocation - 1].enter_event]
        if draw.frame < enter.frame:
            raise _fragment_fail("a final draw frame precedes invocation entry")

    for draw, member_count in zip(draws, members_per_draw, strict=True):
        if draw.disposition is DrawDisposition.Submitted and member_count == 0:
            raise _fragment_fail("a submitted draw has no submission membership")
        if draw.disposition is DrawDisposition.Skipped and member_count != 0:
            raise _fragment_fail("a skipped draw has a submission membership")
        if draw.frame > frame_count:
            raise _fragment_fail("draw frame exceeds retained frame accounting")


def parse_phase_fragment(
    raw: bytes,
    *,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> ParsedPhaseFragment:
    """Validate one immutable private producer fragment and retain its chronology."""

    if type(raw) is not bytes:
        raise _fragment_fail("phase fragment input must be immutable bytes")
    if len(raw) > limits.fragment_bytes:
        raise _fragment_fail("phase fragment exceeds the caller byte capacity")
    if len(raw) < _HEADER_BYTES:
        raise _fragment_fail("phase fragment is truncated")

    reader = _Reader(raw)
    if reader.bytes(len(FRAGMENT_MAGIC)) != FRAGMENT_MAGIC:
        raise _fragment_fail("phase fragment magic is unsupported")
    if reader.u32() != FRAGMENT_VERSION:
        raise _fragment_fail("phase fragment version is unsupported")
    capture_domain = reader.bytes(32)
    runtime_config_digest = reader.bytes(32)
    site_map_digest = reader.bytes(32)
    if not all(any(value) for value in (capture_domain, runtime_config_digest, site_map_digest)):
        raise _fragment_fail("phase fragment consistency values are invalid")

    frame_count = reader.u32()
    if frame_count > limits.frames:
        raise _fragment_fail("phase fragment frame count exceeds the caller capacity")
    status_value = reader.u32()
    commit_value = reader.u32()
    failure_counts = tuple(reader.u32() for _ in _FAILURE_COUNTER_NAMES)
    discovered_counts = tuple(reader.u32() for _ in _COUNT_NAMES)
    counts = tuple(reader.u32() for _ in _COUNT_NAMES)
    _validate_counts(counts, discovered_counts, len(raw), limits)
    terminal = _validate_terminal(
        status_value,
        commit_value,
        failure_counts,
        discovered_counts,
        counts,
        limits,
    )
    if terminal.status is TerminalStatus.Complete and frame_count == 0:
        raise _fragment_fail("a complete phase fragment has no retained frames")

    site_count, invocation_count, event_count, submission_count, draw_count, edge_count = counts

    sites: list[SiteRow] = []
    for index in range(site_count):
        ordinal = reader.u32()
        _require_dense_id(ordinal, index)
        sites.append(SiteRow(ordinal))

    invocations: list[InvocationRow] = []
    for index in range(invocation_count):
        ordinal = reader.u32()
        _require_dense_id(ordinal, index)
        site = reader.u32()
        lane = reader.u32()
        parent = reader.u32()
        enter_event = reader.u32()
        exit_event = reader.u32()
        _require_reference(site, site_count, "invocation site reference is invalid")
        if lane != 0:
            raise _fragment_fail("phase fragment admits only the single fixed lane")
        if parent != 0 and parent > index:
            raise _fragment_fail("invocation parent is not an earlier ordinal")
        if enter_event == 0 or exit_event == 0 or enter_event >= exit_event:
            raise _fragment_fail("invocation enter/exit references are invalid")
        invocations.append(
            InvocationRow(ordinal, site, lane, parent, enter_event, exit_event)
        )

    events: list[EventRow] = []
    prior_event_sequence = 0
    for index in range(event_count):
        ordinal = reader.u32()
        _require_dense_id(ordinal, index)
        sequence = reader.u32()
        frame = reader.u32()
        invocation = reader.u32()
        kind_value = reader.u8()
        reserved = reader.u32()
        if sequence <= prior_event_sequence:
            raise _fragment_fail("event sequence is not strictly increasing")
        prior_event_sequence = sequence
        if frame == 0 or frame > frame_count:
            raise _fragment_fail("event frame is outside retained frame accounting")
        _require_reference(invocation, invocation_count, "event invocation reference is invalid")
        try:
            kind = EventKind(kind_value)
        except ValueError as error:
            raise _fragment_fail("event kind is unsupported") from error
        if reserved != 0:
            raise _fragment_fail("event reserved field must be zero")
        events.append(EventRow(ordinal, sequence, frame, invocation, kind))

    submissions: list[SubmissionRow] = []
    prior_submission_sequence = 0
    for index in range(submission_count):
        ordinal = reader.u32()
        _require_dense_id(ordinal, index)
        sequence = reader.u32()
        frame = reader.u32()
        invocation = reader.u32()
        primitive_begin = reader.u32()
        primitive_count = reader.u32()
        if sequence <= prior_submission_sequence:
            raise _fragment_fail("submission sequence is not strictly increasing")
        prior_submission_sequence = sequence
        if frame == 0 or frame > frame_count:
            raise _fragment_fail("submission frame is outside retained frame accounting")
        _require_reference(
            invocation, invocation_count, "submission invocation reference is invalid"
        )
        submissions.append(
            SubmissionRow(
                ordinal,
                sequence,
                frame,
                invocation,
                primitive_begin,
                primitive_count,
            )
        )

    draws: list[DrawRow] = []
    prior_draw_sequence = 0
    for index in range(draw_count):
        ordinal = reader.u32()
        _require_dense_id(ordinal, index)
        sequence = reader.u32()
        frame = reader.u32()
        disposition_value = reader.u8()
        reserved = reader.u32()
        if sequence <= prior_draw_sequence:
            raise _fragment_fail("draw sequence is not strictly increasing")
        prior_draw_sequence = sequence
        if frame == 0 or frame > frame_count:
            raise _fragment_fail("draw frame is outside retained frame accounting")
        try:
            disposition = DrawDisposition(disposition_value)
        except ValueError as error:
            raise _fragment_fail("draw disposition is unsupported") from error
        if reserved != 0:
            raise _fragment_fail("draw reserved field must be zero")
        draws.append(DrawRow(ordinal, sequence, frame, disposition))

    memberships: list[MembershipRow] = []
    for _ in range(edge_count):
        submission = reader.u32()
        draw = reader.u32()
        _require_reference(
            submission, submission_count, "membership submission reference is invalid"
        )
        _require_reference(draw, draw_count, "membership draw reference is invalid")
        memberships.append(MembershipRow(submission, draw))

    if reader.offset != len(raw):
        raise _fragment_fail("phase fragment contains trailing bytes")

    _validate_temporal_model(
        frame_count,
        terminal,
        invocations,
        events,
        submissions,
        draws,
        memberships,
        limits,
    )

    return ParsedPhaseFragment(
        capture_domain=capture_domain,
        runtime_config_digest=runtime_config_digest,
        site_map_digest=site_map_digest,
        frame_count=frame_count,
        terminal=terminal,
        sites=tuple(sites),
        invocations=tuple(invocations),
        events=tuple(events),
        submissions=tuple(submissions),
        draws=tuple(draws),
        memberships=tuple(memberships),
    )


def _bounded_canonical_json(document: object, byte_limit: int) -> bytes:
    output = bytearray()
    encoder = json.JSONEncoder(
        ensure_ascii=True,
        allow_nan=False,
        separators=(",", ":"),
        sort_keys=False,
    )
    for chunk in encoder.iterencode(document):
        encoded = chunk.encode("ascii")
        if len(output) > byte_limit - len(encoded):
            raise _assembly_fail("JSON output exceeds the caller byte capacity")
        output.extend(encoded)
    if len(output) >= byte_limit:
        raise _assembly_fail("JSON output exceeds the caller byte capacity")
    output.append(0x0A)
    return bytes(output)


class _BoundedJsonWriter:
    """Minimal canonical JSON writer that never builds the private document tree."""

    def __init__(self, byte_limit: int) -> None:
        self._limit = byte_limit
        self._output = bytearray()

    def raw(self, value: bytes) -> None:
        if len(self._output) > self._limit - len(value):
            raise _assembly_fail("private trace exceeds the caller byte capacity")
        self._output.extend(value)

    def string(self, value: str) -> None:
        self.raw(
            json.dumps(
                value,
                ensure_ascii=True,
                allow_nan=False,
                separators=(",", ":"),
            ).encode("ascii")
        )

    def integer(self, value: int) -> None:
        self.raw(str(value).encode("ascii"))

    def boolean(self, value: bool) -> None:
        self.raw(b"true" if value else b"false")

    def finish(self) -> bytes:
        self.raw(b"\n")
        return bytes(self._output)


def _reject_duplicate_json_pairs(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _assembly_fail("a private manifest contains a duplicate key")
        result[key] = value
    return result


def _parse_json_object(raw: bytes, limits: PhaseLimits) -> dict[str, object]:
    if type(raw) is not bytes or len(raw) > limits.manifest_bytes:
        raise _assembly_fail("a private manifest exceeds the caller capacity")
    try:
        text = raw.decode("ascii")
    except UnicodeDecodeError as error:
        raise _assembly_fail("a private manifest is not ASCII") from error
    try:
        document = json.loads(text, object_pairs_hook=_reject_duplicate_json_pairs)
    except (json.JSONDecodeError, PhaseAssemblyError) as error:
        raise _assembly_fail("a private manifest is malformed") from error
    if not isinstance(document, dict):
        raise _assembly_fail("a private manifest root must be an object")
    return cast(dict[str, object], document)


def _require_exact_keys(document: dict[str, object], expected: tuple[str, ...]) -> None:
    if tuple(document) != expected:
        raise _assembly_fail("a private manifest has noncanonical keys")


def _decode_hex_digest(value: object, limits: PhaseLimits) -> bytes:
    if type(value) is not str or len(value.encode("ascii", "ignore")) > limits.string_bytes:
        raise _assembly_fail("a private consistency value is malformed")
    if len(value) != 64 or any(character not in "0123456789abcdef" for character in value):
        raise _assembly_fail("a private consistency value is malformed")
    decoded = bytes.fromhex(value)
    if not any(decoded):
        raise _assembly_fail("a private consistency value is zero")
    return decoded


def parse_site_map_binding(
    raw: bytes,
    *,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> SiteMapBinding:
    document = _parse_json_object(raw, limits)
    _require_exact_keys(
        document,
        ("schema", "runtime_config_digest", "site_map_digest", "site_count"),
    )
    if document["schema"] != SITE_MAP_BINDING_SCHEMA:
        raise _assembly_fail("private site-map binding schema is unsupported")
    site_count = document["site_count"]
    if type(site_count) is not int or site_count < 0 or site_count > limits.sites:
        raise _assembly_fail("private site-map count exceeds the caller capacity")
    return SiteMapBinding(
        runtime_config_digest=_decode_hex_digest(document["runtime_config_digest"], limits),
        site_map_digest=_decode_hex_digest(document["site_map_digest"], limits),
        site_count=site_count,
    )


def parse_capture_domain_manifest(
    raw: bytes,
    *,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> CaptureDomainManifest:
    document = _parse_json_object(raw, limits)
    _require_exact_keys(
        document,
        (
            "schema",
            "phase_capture_domain",
            "scene_capture_domain",
            "frontend_capture_domain",
        ),
    )
    if document["schema"] != CAPTURE_MANIFEST_SCHEMA:
        raise _assembly_fail("private capture-domain manifest schema is unsupported")
    return CaptureDomainManifest(
        phase_capture_domain=_decode_hex_digest(document["phase_capture_domain"], limits),
        scene_capture_domain=_decode_hex_digest(document["scene_capture_domain"], limits),
        frontend_capture_domain=_decode_hex_digest(document["frontend_capture_domain"], limits),
    )


def validate_private_bindings(
    model: ParsedPhaseFragment,
    site_map_binding: SiteMapBinding,
    capture_manifest: CaptureDomainManifest,
) -> None:
    """Verify private configuration/site and three-artifact capture-domain joins."""

    if (
        site_map_binding.runtime_config_digest != model.runtime_config_digest
        or site_map_binding.site_map_digest != model.site_map_digest
        or site_map_binding.site_count != len(model.sites)
    ):
        raise _assembly_fail("private site-map binding does not match the phase fragment")
    if not (
        capture_manifest.phase_capture_domain
        == capture_manifest.scene_capture_domain
        == capture_manifest.frontend_capture_domain
        == model.capture_domain
    ):
        raise _assembly_fail("private capture-domain artifacts do not form one capture")


def _validate_expected_sha256(value: str | None) -> str | None:
    if value is None:
        return None
    if len(value) != 64 or any(character not in "0123456789abcdefABCDEF" for character in value):
        raise _assembly_fail("expected fragment SHA-256 is malformed")
    return value.lower()


def assemble_phase_model(
    raw_fragments: Sequence[bytes],
    *,
    site_map_binding: SiteMapBinding,
    capture_manifest: CaptureDomainManifest,
    expected_fragment_sha256: str | None = None,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> ParsedPhaseFragment:
    """Compare independent in-memory copies and parse only the reference bytes."""

    if not isinstance(raw_fragments, Sequence) or isinstance(
        raw_fragments, (bytes, bytearray, str)
    ):
        raise _assembly_fail("phase fragment collection must be a sequence")
    if not raw_fragments or len(raw_fragments) > limits.fragments:
        raise _assembly_fail("phase fragment count exceeds the caller capacity")
    if sum(len(raw) for raw in raw_fragments if type(raw) is bytes) > limits.total_input_bytes:
        raise _assembly_fail("phase fragments exceed the total caller byte capacity")

    expected_digest = _validate_expected_sha256(expected_fragment_sha256)
    reference = raw_fragments[0]
    if type(reference) is not bytes:
        raise _assembly_fail("phase fragments must be immutable bytes")
    reference_hash = hashlib.sha256(reference).hexdigest()
    if expected_digest is not None and reference_hash != expected_digest:
        raise _assembly_fail("phase fragment SHA-256 does not match the expected consistency value")
    for repeat in raw_fragments[1:]:
        if type(repeat) is not bytes:
            raise _assembly_fail("phase fragments must be immutable bytes")
        if repeat is reference:
            raise _assembly_fail("repeat verification requires an independent byte object")
        repeat_hash = hashlib.sha256(repeat).hexdigest()
        if repeat_hash != reference_hash or repeat != reference:
            raise _assembly_fail("repeat phase fragments are not byte-identical")

    model = parse_phase_fragment(reference, limits=limits)
    validate_private_bindings(model, site_map_binding, capture_manifest)
    return model


def _public_report_document(
    model: ParsedPhaseFragment,
    limits: PhaseLimits,
) -> dict[str, object]:
    submitted = sum(
        draw.disposition is DrawDisposition.Submitted for draw in model.draws
    )
    skipped = len(model.draws) - submitted
    counts = (
        len(model.sites),
        len(model.invocations),
        len(model.events),
        len(model.submissions),
        len(model.draws),
        submitted,
        skipped,
        len(model.memberships),
    )
    if len(counts) > limits.public_aggregate_rows:
        raise _assembly_fail("public aggregate rows exceed the caller capacity")
    if not model.ordering_evidence_valid:
        policy = "IncompleteCapture"
    elif not model.submissions:
        policy = "NoOrderingEvidence"
    else:
        policy = "PrivateReviewRequired"
    return {
        "schema": PUBLIC_REPORT_SCHEMA,
        "terminal_status": model.terminal.status.name,
        "completeness": "Complete" if model.ordering_evidence_valid else "Incomplete",
        "policy": policy,
        "aggregate_counts": [
            {"category": category, "count": count}
            for category, count in zip(_PUBLIC_COUNT_CATEGORIES, counts, strict=True)
        ],
    }


def encode_public_report(
    model: ParsedPhaseFragment,
    *,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> bytes:
    return _bounded_canonical_json(
        _public_report_document(model, limits),
        limits.public_output_bytes,
    )


def _private_map(
    writer: _BoundedJsonWriter,
    names: tuple[str, ...],
    values: tuple[int, ...],
) -> None:
    writer.raw(b"{")
    for index, (name, value) in enumerate(zip(names, values, strict=True)):
        if index:
            writer.raw(b",")
        writer.string(name)
        writer.raw(b":")
        writer.integer(value)
    writer.raw(b"}")


def _private_rows(
    writer: _BoundedJsonWriter,
    rows: Sequence[object],
    render: Callable[[_BoundedJsonWriter, object], None],
) -> None:
    writer.raw(b"[")
    for index, row in enumerate(rows):
        if index:
            writer.raw(b",")
        render(writer, row)
    writer.raw(b"]")


def _write_site(writer: _BoundedJsonWriter, value: object) -> None:
    row = cast(SiteRow, value)
    writer.raw(b'{"ordinal":')
    writer.integer(row.ordinal)
    writer.raw(b"}")


def _write_invocation(writer: _BoundedJsonWriter, value: object) -> None:
    row = cast(InvocationRow, value)
    writer.raw(b'{"ordinal":')
    writer.integer(row.ordinal)
    writer.raw(b',"site":')
    writer.integer(row.site)
    writer.raw(b',"lane":')
    writer.integer(row.lane)
    writer.raw(b',"parent":')
    writer.integer(row.parent)
    writer.raw(b',"enter_event":')
    writer.integer(row.enter_event)
    writer.raw(b',"exit_event":')
    writer.integer(row.exit_event)
    writer.raw(b"}")


def _write_event(writer: _BoundedJsonWriter, value: object) -> None:
    row = cast(EventRow, value)
    writer.raw(b'{"ordinal":')
    writer.integer(row.ordinal)
    writer.raw(b',"sequence":')
    writer.integer(row.sequence)
    writer.raw(b',"frame":')
    writer.integer(row.frame)
    writer.raw(b',"invocation":')
    writer.integer(row.invocation)
    writer.raw(b',"kind":')
    writer.string(row.kind.name)
    writer.raw(b"}")


def _write_submission(writer: _BoundedJsonWriter, value: object) -> None:
    row = cast(SubmissionRow, value)
    writer.raw(b'{"ordinal":')
    writer.integer(row.ordinal)
    writer.raw(b',"sequence":')
    writer.integer(row.sequence)
    writer.raw(b',"frame":')
    writer.integer(row.frame)
    writer.raw(b',"invocation":')
    writer.integer(row.invocation)
    writer.raw(b',"primitive_begin":')
    writer.integer(row.primitive_begin)
    writer.raw(b',"primitive_count":')
    writer.integer(row.primitive_count)
    writer.raw(b"}")


def _write_draw(writer: _BoundedJsonWriter, value: object) -> None:
    row = cast(DrawRow, value)
    writer.raw(b'{"ordinal":')
    writer.integer(row.ordinal)
    writer.raw(b',"sequence":')
    writer.integer(row.sequence)
    writer.raw(b',"frame":')
    writer.integer(row.frame)
    writer.raw(b',"disposition":')
    writer.string(row.disposition.name)
    writer.raw(b"}")


def _write_membership(writer: _BoundedJsonWriter, value: object) -> None:
    row = cast(MembershipRow, value)
    writer.raw(b'{"submission":')
    writer.integer(row.submission)
    writer.raw(b',"draw":')
    writer.integer(row.draw)
    writer.raw(b"}")


def encode_private_trace(
    model: ParsedPhaseFragment,
    *,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> bytes:
    writer = _BoundedJsonWriter(limits.private_output_bytes)
    writer.raw(b'{"schema":')
    writer.string(PRIVATE_TRACE_SCHEMA)
    writer.raw(b',"capture_domain":')
    writer.string(model.capture_domain.hex())
    writer.raw(b',"runtime_config_digest":')
    writer.string(model.runtime_config_digest.hex())
    writer.raw(b',"site_map_digest":')
    writer.string(model.site_map_digest.hex())
    writer.raw(b',"frame_count":')
    writer.integer(model.frame_count)
    writer.raw(b',"ordering_evidence_valid":')
    writer.boolean(model.ordering_evidence_valid)
    writer.raw(b',"terminal":{"status":')
    writer.string(model.terminal.status.name)
    writer.raw(b',"commit_state":')
    writer.string(model.terminal.commit_state.name)
    writer.raw(b',"failure_counts":')
    _private_map(writer, _FAILURE_COUNTER_NAMES, model.terminal.failure_counts)
    writer.raw(b',"discovered_counts":')
    _private_map(writer, _COUNT_NAMES, model.terminal.discovered_counts)
    writer.raw(b',"retained_counts":')
    _private_map(writer, _COUNT_NAMES, model.terminal.retained_counts)
    writer.raw(b'},"sites":')
    _private_rows(writer, model.sites, _write_site)
    writer.raw(b',"invocations":')
    _private_rows(writer, model.invocations, _write_invocation)
    writer.raw(b',"events":')
    _private_rows(writer, model.events, _write_event)
    writer.raw(b',"submissions":')
    _private_rows(writer, model.submissions, _write_submission)
    writer.raw(b',"draws":')
    _private_rows(writer, model.draws, _write_draw)
    writer.raw(b',"memberships":')
    _private_rows(writer, model.memberships, _write_membership)
    writer.raw(b"}")
    return writer.finish()


def _fstat_signature(stream: BinaryIO) -> tuple[int, int, int, int]:
    stat = os.fstat(stream.fileno())
    return (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns)


def _read_snapshot(
    path: str,
    byte_limit: int,
) -> tuple[bytes, tuple[int, int], str]:
    with open(path, "rb") as stream:
        before = _fstat_signature(stream)
        if before[2] > byte_limit:
            raise _assembly_fail("a private input exceeds the caller byte capacity")
        output = bytearray()
        digest = hashlib.sha256()
        remaining = before[2]
        while remaining:
            chunk = stream.read(min(_READ_CHUNK_BYTES, remaining))
            if not chunk:
                raise _assembly_fail("a private input changed during its authenticated snapshot")
            output.extend(chunk)
            digest.update(chunk)
            remaining -= len(chunk)
        if stream.read(1):
            raise _assembly_fail("a private input grew during its authenticated snapshot")
        after = _fstat_signature(stream)
    if before != after:
        raise _assembly_fail("a private input changed during its authenticated snapshot")
    return bytes(output), (before[0], before[1]), digest.hexdigest()


def _compare_snapshot(
    path: str,
    reference: bytes,
    reference_hash: str,
    byte_limit: int,
) -> tuple[int, int]:
    with open(path, "rb") as stream:
        before = _fstat_signature(stream)
        if before[2] > byte_limit or before[2] != len(reference):
            raise _assembly_fail("repeat phase fragments are not byte-identical")
        digest = hashlib.sha256()
        offset = 0
        while offset < len(reference):
            chunk = stream.read(min(_READ_CHUNK_BYTES, len(reference) - offset))
            if not chunk:
                raise _assembly_fail("a repeat fragment changed during comparison")
            if chunk != reference[offset : offset + len(chunk)]:
                raise _assembly_fail("repeat phase fragments are not byte-identical")
            digest.update(chunk)
            offset += len(chunk)
        if stream.read(1):
            raise _assembly_fail("a repeat fragment grew during comparison")
        after = _fstat_signature(stream)
    if before != after or digest.hexdigest() != reference_hash:
        raise _assembly_fail("a repeat fragment failed integrity comparison")
    return (before[0], before[1])


def assemble_phase_paths(
    paths: Sequence[str],
    *,
    site_map_binding: SiteMapBinding,
    capture_manifest: CaptureDomainManifest,
    expected_fragment_sha256: str,
    limits: PhaseLimits = DEFAULT_LIMITS,
) -> ParsedPhaseFragment:
    """Stream-compare stored copies, then parse only the reference snapshot."""

    if not paths or len(paths) > limits.fragments:
        raise _assembly_fail("phase fragment count exceeds the caller capacity")
    expected_digest = _validate_expected_sha256(expected_fragment_sha256)
    if expected_digest is None:
        raise _assembly_fail("path assembly requires an expected consistency value")

    reference, reference_identity, reference_hash = _read_snapshot(
        paths[0], limits.fragment_bytes
    )
    if reference_hash != expected_digest:
        raise _assembly_fail("phase fragment SHA-256 does not match the expected consistency value")
    total_bytes = len(reference)
    if total_bytes > limits.total_input_bytes:
        raise _assembly_fail("phase fragments exceed the total caller byte capacity")
    identities = {reference_identity}
    for path in paths[1:]:
        if total_bytes > limits.total_input_bytes - len(reference):
            raise _assembly_fail("phase fragments exceed the total caller byte capacity")
        identity = _compare_snapshot(
            path, reference, reference_hash, limits.fragment_bytes
        )
        if identity in identities:
            raise _assembly_fail("repeat verification requires a distinct stored copy")
        identities.add(identity)
        total_bytes += len(reference)

    model = parse_phase_fragment(reference, limits=limits)
    validate_private_bindings(model, site_map_binding, capture_manifest)
    return model


@dataclass(frozen=True)
class _CliOptions:
    public_output: str
    private_output: str | None
    site_map_binding: str
    capture_manifest: str
    expected_sha256: str
    fragments: tuple[str, ...]
    limits: PhaseLimits


def _apply_limit(limits: PhaseLimits, specification: str) -> PhaseLimits:
    if "=" not in specification:
        raise _assembly_fail("CLI limit is malformed")
    name, value_text = specification.split("=", 1)
    if name not in HARD_LIMITS or not value_text.isascii() or not value_text.isdecimal():
        raise _assembly_fail("CLI limit is malformed")
    value = int(value_text, 10)
    return replace(limits, **{name: value})


def _parse_cli(args: Sequence[str]) -> _CliOptions:
    if len(args) > MAX_CLI_ARGUMENTS or sum(len(arg.encode("utf-8")) for arg in args) > MAX_CLI_ARGUMENT_BYTES:
        raise _assembly_fail("CLI arguments exceed fixed capacities")
    values: dict[str, str] = {}
    private_output: str | None = None
    fragments: list[str] = []
    limits = DEFAULT_LIMITS
    index = 0
    value_options = {
        "--public-output": "public_output",
        "--private-output": "private_output",
        "--site-map-binding": "site_map_binding",
        "--capture-manifest": "capture_manifest",
        "--expected-sha256": "expected_sha256",
    }
    while index < len(args):
        argument = args[index]
        if argument == "--limit":
            index += 1
            if index >= len(args):
                raise _assembly_fail("CLI option is missing its value")
            limits = _apply_limit(limits, args[index])
        elif argument in value_options:
            index += 1
            if index >= len(args):
                raise _assembly_fail("CLI option is missing its value")
            key = value_options[argument]
            if key in values:
                raise _assembly_fail("CLI option was repeated")
            values[key] = args[index]
        elif argument.startswith("-"):
            raise _assembly_fail("CLI option is unsupported")
        else:
            fragments.append(argument)
        index += 1

    required = (
        "public_output",
        "site_map_binding",
        "capture_manifest",
        "expected_sha256",
    )
    if any(not values.get(key) for key in required):
        raise _assembly_fail("required CLI options are missing")
    if not fragments or len(fragments) > limits.fragments:
        raise _assembly_fail("phase fragment count exceeds the caller capacity")
    private_output = values.get("private_output")
    return _CliOptions(
        public_output=values["public_output"],
        private_output=private_output,
        site_map_binding=values["site_map_binding"],
        capture_manifest=values["capture_manifest"],
        expected_sha256=values["expected_sha256"],
        fragments=tuple(fragments),
        limits=limits,
    )


# Recognized gitignored roots a --private-output path may resolve under when
# it lands inside this repository. This module cannot police a destination
# outside the repository (a foreign filesystem has no tracked tree to check
# against), so a path resolving elsewhere is accepted unconditionally.
_IGNORED_PRIVATE_ROOTS: Final = ("private", "runtime", "analysis/output")
_REPOSITORY_ROOT: Final = Path(__file__).resolve().parents[1]


def _validate_private_output_path(path: str) -> None:
    """Refuse a --private-output destination that is not explicitly ignored.

    A path that resolves inside this repository but outside a recognized
    ignored root is refused: writing the detailed private trace there would
    make it a public-tree candidate by accident. A path resolving outside the
    repository entirely is accepted unconditionally.
    """

    resolved = Path(path).resolve()
    try:
        relative = resolved.relative_to(_REPOSITORY_ROOT)
    except ValueError:
        return
    posix = relative.as_posix()
    if not any(posix == root or posix.startswith(root + "/") for root in _IGNORED_PRIVATE_ROOTS):
        raise _assembly_fail(
            "private output path is inside the repository but not under a recognized ignored root"
        )


def _write_new_file(path: str, payload: bytes) -> None:
    # Never unlink on failure: a path replacement could make that destructive.
    with open(path, "xb") as stream:
        view = memoryview(payload)
        written = 0
        while written < len(view):
            count = stream.write(view[written:])
            if count is None or count <= 0:
                raise OSError("short phase report write")
            written += count
        if written != len(payload):
            raise OSError("short phase report write")
        stream.flush()


def _write_ascii_line(stream: TextIO, value: str) -> None:
    payload = value.encode("ascii") + b"\n"
    binary = getattr(stream, "buffer", None)
    if binary is not None:
        binary.write(payload)
    else:
        stream.write(payload.decode("ascii"))


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point with fixed diagnostics and an opt-in private sink."""

    args = list(sys.argv[1:] if argv is None else argv)
    if args in (["-h"], ["--help"]):
        _write_ascii_line(
            sys.stdout,
            "usage: assemble_frontend_phase.py --site-map-binding FILE "
            "--capture-manifest FILE --expected-sha256 HEX "
            "--public-output FILE [--private-output FILE] "
            "[--limit NAME=VALUE] FRAGMENT [FRAGMENT ...]",
        )
        return 0

    try:
        options = _parse_cli(args)
        if options.private_output is not None:
            _validate_private_output_path(options.private_output)
        site_raw, _, _ = _read_snapshot(
            options.site_map_binding, options.limits.manifest_bytes
        )
        capture_raw, _, _ = _read_snapshot(
            options.capture_manifest, options.limits.manifest_bytes
        )
        site_binding = parse_site_map_binding(site_raw, limits=options.limits)
        capture_manifest = parse_capture_domain_manifest(
            capture_raw, limits=options.limits
        )
        model = assemble_phase_paths(
            options.fragments,
            site_map_binding=site_binding,
            capture_manifest=capture_manifest,
            expected_fragment_sha256=options.expected_sha256,
            limits=options.limits,
        )
        public_payload = encode_public_report(model, limits=options.limits)
        private_payload = (
            encode_private_trace(model, limits=options.limits)
            if options.private_output is not None
            else None
        )
        _write_new_file(options.public_output, public_payload)
        if options.private_output is not None and private_payload is not None:
            _write_new_file(options.private_output, private_payload)
    except Exception:
        _write_ascii_line(sys.stderr, "Phase fragment processing failed.")
        return 1

    if not model.ordering_evidence_valid:
        _write_ascii_line(sys.stderr, "Phase capture is incomplete.")
        return 2
    _write_ascii_line(sys.stdout, "Phase fragment processing succeeded.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
