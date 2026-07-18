#!/usr/bin/env python3
"""Validate sanitized VUM aggregate read-trace reports.

The validator intentionally accepts aggregate counters only.  It fails closed on
unknown fields so a tracer cannot accidentally add retail addresses, payloads,
instructions, names, or paths to a report without also changing this policy.
"""

from __future__ import annotations

import json
import math
import re
import sys
from bisect import bisect_left, bisect_right
from collections.abc import Sequence
from typing import cast


SCHEMA = "omega-vum-read-trace-v1"

_MAX_REPORT_BYTES = 16 * 1024 * 1024
_MIN_EXPECTED_SIZE = 112
_MAX_EXPECTED_SIZE = 32 * 1024 * 1024
_MAX_FRAME_COUNT = 10_000
_MAX_SELECTED_COPY_COUNT = 64
_MAX_ANONYMOUS_SITE_COUNT = 65_536
_MAX_AGGREGATE_KEY_COUNT = 1_048_576
_MAX_JSON_DEPTH = 32
_MAX_LIST_ITEMS = 262_144
_MAX_OBJECT_MEMBERS = 1_024
_MAX_JSON_NODES = 2_000_000
_UINT32_MAX = (1 << 32) - 1
_UINT64_MAX = (1 << 64) - 1

_EE_WIDTHS = frozenset((1, 2, 4, 8, 16))
_FAILURE_REASONS = frozenset(
    (
        "arithmetic_overflow",
        "capacity_exceeded",
        "counter_overflow",
        "dropped_boundary_access",
        "dropped_site_token",
        "invalid_environment",
        "ordinary_breakpoint_present",
        "ordinary_memcheck_present",
        "output_already_exists",
        "overlapping_runtime_copies",
        "runtime_copy_changed",
        "runtime_copy_not_found",
        "runtime_identity_changed",
        "savestate_load_failed",
        "savestate_required",
        "unaligned_access",
        "unsupported_execution_mode",
        "unsupported_host",
        "unsupported_load_width",
        "wrong_disc",
    )
)

_TOP_LEVEL_KEYS = frozenset(
    (
        "schema",
        "status",
        "stop_reason",
        "selected_copy_count",
        "frame_count",
        "matching_event_observed",
        "ee_reads",
        "anonymous_sites",
        "vif1_unpack_chunks",
    )
)
_EE_KEYS = frozenset(("relative_offset", "width", "execution_count"))
_ANONYMOUS_KEYS = frozenset(
    (
        "anonymous_site",
        "width",
        "execution_count",
        "distinct_relative_offset_count",
        "minimum_relative_offset",
        "maximum_relative_offset",
        "loop_candidate_heuristic",
    )
)
_VIF_KEYS = frozenset(
    (
        "source_relative_offset",
        "source_width",
        "source_word_count",
        "remaining_output_element_count_before_chunk",
        "event_count",
    )
)

_FORBIDDEN_PRIVACY_TOKENS = frozenset(
    (
        "pc",
        "address",
        "ram",
        "memory",
        "hash",
        "path",
        "name",
        "payload",
        "bytes",
        "data",
        "instruction",
        "instructions",
        "opcode",
        "opcodes",
        "register",
        "registers",
    )
)


class TraceValidationError(ValueError):
    """The report is not a valid sanitized VUM read-trace document."""


def _fail(message: str) -> TraceValidationError:
    return TraceValidationError(message)


def _reject_json_constant(_value: str) -> object:
    raise _fail("non-finite JSON constants are not allowed")


def _object_without_duplicates(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _fail("duplicate JSON object key")
        result[key] = value
    return result


def _privacy_key_is_forbidden(key: str) -> bool:
    # Split both snake/kebab case and camel case.  In particular, token matching
    # avoids treating the letters "ram" in the allowed word "frame" as RAM.
    separated = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", key)
    tokens = tuple(token.lower() for token in re.findall(r"[A-Za-z0-9]+", separated))
    for token in tokens:
        if token in _FORBIDDEN_PRIVACY_TOKENS or token.startswith("crc"):
            return True
    collapsed = "".join(tokens)
    return collapsed.startswith(("guestpc", "guestaddress", "guestram"))


def _preflight_json_tree(document: object) -> None:
    nodes = 0

    def visit(value: object, depth: int) -> None:
        nonlocal nodes
        nodes += 1
        if nodes > _MAX_JSON_NODES:
            raise _fail("JSON document contains too many values")
        if depth > _MAX_JSON_DEPTH:
            raise _fail("JSON document is nested too deeply")

        if isinstance(value, dict):
            if len(value) > _MAX_OBJECT_MEMBERS:
                raise _fail("JSON object contains too many members")
            for key, child in value.items():
                if not isinstance(key, str):
                    raise _fail("JSON object keys must be strings")
                if _privacy_key_is_forbidden(key):
                    raise _fail("report contains a forbidden privacy field")
                visit(child, depth + 1)
            return
        if isinstance(value, list):
            if len(value) > _MAX_LIST_ITEMS:
                raise _fail("JSON list contains too many items")
            for child in value:
                visit(child, depth + 1)
            return
        if isinstance(value, float) and not math.isfinite(value):
            raise _fail("non-finite JSON numbers are not allowed")
        if value is None or isinstance(value, (str, int, float, bool)):
            return
        raise _fail("document contains a non-JSON value")

    visit(document, 0)


def _require_exact_keys(value: dict[str, object], keys: frozenset[str]) -> None:
    if frozenset(value) != keys:
        raise _fail("JSON object does not have the exact required schema")


def _require_dict(value: object) -> dict[str, object]:
    if not isinstance(value, dict):
        raise _fail("expected a JSON object")
    return cast(dict[str, object], value)


def _require_list(value: object) -> list[object]:
    if not isinstance(value, list):
        raise _fail("expected a JSON list")
    return cast(list[object], value)


def _require_string(value: object) -> str:
    if not isinstance(value, str):
        raise _fail("expected a JSON string")
    return value


def _require_bool(value: object) -> bool:
    if type(value) is not bool:
        raise _fail("expected a JSON boolean")
    return cast(bool, value)


def _require_int(value: object, minimum: int, maximum: int) -> int:
    # bool is an int subclass in Python, but JSON booleans are never counters.
    if type(value) is not int:
        raise _fail("expected a bounded JSON integer")
    result = cast(int, value)
    if result < minimum or result > maximum:
        raise _fail("JSON integer is outside its allowed range")
    return result


def _validate_expected_size(expected_size: int) -> None:
    if type(expected_size) is not int:
        raise _fail("expected size must be an integer")
    if expected_size < _MIN_EXPECTED_SIZE or expected_size > _MAX_EXPECTED_SIZE:
        raise _fail("expected size is outside the allowed range")
    if expected_size % 16 != 0:
        raise _fail("expected size must be 16-byte aligned")


def _checked_u64_add(left: int, right: int) -> int:
    result = left + right
    if result > _UINT64_MAX:
        raise _fail("aggregate execution count overflows uint64")
    return result


def _validate_ee_reads(
    rows: list[object], expected_size: int
) -> tuple[dict[tuple[int, int], int], dict[int, int], int]:
    reads: dict[tuple[int, int], int] = {}
    per_width: dict[int, int] = {}
    total = 0
    previous_key: tuple[int, int] | None = None

    for raw_row in rows:
        row = _require_dict(raw_row)
        _require_exact_keys(row, _EE_KEYS)
        relative_offset = _require_int(row["relative_offset"], 0, expected_size - 1)
        width = _require_int(row["width"], 1, 16)
        if width not in _EE_WIDTHS:
            raise _fail("EE read width is not supported")
        execution_count = _require_int(row["execution_count"], 1, _UINT64_MAX)
        if relative_offset % width != 0 or relative_offset + width > expected_size:
            raise _fail("EE read is unaligned or outside the VUM span")

        key = (relative_offset, width)
        if previous_key is not None and key <= previous_key:
            raise _fail("EE reads are not strictly sorted and unique")
        previous_key = key
        reads[key] = execution_count
        per_width[width] = _checked_u64_add(per_width.get(width, 0), execution_count)
        total = _checked_u64_add(total, execution_count)

    return reads, per_width, total


def _validate_anonymous_sites(
    rows: list[object],
    expected_size: int,
    ee_reads: dict[tuple[int, int], int],
    ee_per_width: dict[int, int],
    ee_total: int,
) -> None:
    if bool(rows) != bool(ee_reads):
        raise _fail("anonymous sites must be empty exactly when EE reads are empty")
    if len(rows) > _MAX_ANONYMOUS_SITE_COUNT:
        raise _fail("anonymous-site count exceeds the tracer capacity")

    site_per_width: dict[int, int] = {}
    site_total = 0
    site_offset_membership_count = 0
    required_extrema_counts: dict[tuple[int, int], int] = {}
    previous_order: tuple[int, int, int, int, int] | None = None
    ee_offsets_per_width: dict[int, list[int]] = {}
    ee_execution_prefixes_per_width: dict[int, list[int]] = {}
    for (relative_offset, width), execution_count in ee_reads.items():
        ee_offsets_per_width.setdefault(width, []).append(relative_offset)
        prefixes = ee_execution_prefixes_per_width.setdefault(width, [0])
        prefixes.append(prefixes[-1] + execution_count)

    for ordinal, raw_row in enumerate(rows):
        row = _require_dict(raw_row)
        _require_exact_keys(row, _ANONYMOUS_KEYS)
        anonymous_site = _require_int(row["anonymous_site"], 0, _UINT64_MAX)
        if anonymous_site != ordinal:
            raise _fail("anonymous site ordinals are not zero-based and sequential")

        width = _require_int(row["width"], 1, 16)
        if width not in _EE_WIDTHS:
            raise _fail("anonymous site width is not supported")
        execution_count = _require_int(row["execution_count"], 1, _UINT64_MAX)
        distinct_count = _require_int(
            row["distinct_relative_offset_count"], 1, _UINT64_MAX
        )
        minimum = _require_int(row["minimum_relative_offset"], 0, expected_size - 1)
        maximum = _require_int(row["maximum_relative_offset"], 0, expected_size - 1)
        heuristic = _require_bool(row["loop_candidate_heuristic"])

        if minimum > maximum:
            raise _fail("anonymous site range is reversed")
        if minimum % width != 0 or maximum % width != 0:
            raise _fail("anonymous site range is not aligned")
        if minimum + width > expected_size or maximum + width > expected_size:
            raise _fail("anonymous site range is outside the VUM span")
        if distinct_count > execution_count:
            raise _fail("anonymous site distinct count exceeds executions")

        reachable_offsets = ((maximum - minimum) // width) + 1
        if distinct_count > reachable_offsets:
            raise _fail("anonymous site distinct count exceeds its reachable span")
        observed_offsets = ee_offsets_per_width.get(width, [])
        if distinct_count > len(observed_offsets):
            raise _fail("anonymous site distinct count exceeds observed EE offsets")
        range_begin = bisect_left(observed_offsets, minimum)
        range_end = bisect_right(observed_offsets, maximum)
        observed_in_range = range_end - range_begin
        if distinct_count > observed_in_range:
            raise _fail("anonymous site distinct count exceeds observed offsets in its range")
        execution_prefixes = ee_execution_prefixes_per_width[width]
        executions_in_range = execution_prefixes[range_end] - execution_prefixes[range_begin]
        if execution_count > executions_in_range:
            raise _fail("anonymous site executions exceed EE reads in its range")
        site_offset_membership_count += distinct_count
        if site_offset_membership_count > _MAX_AGGREGATE_KEY_COUNT:
            raise _fail("anonymous-site offset memberships exceed the tracer capacity")
        if minimum != maximum and distinct_count < 2:
            raise _fail("anonymous site range requires at least two distinct offsets")
        if heuristic != (execution_count > 1 and distinct_count > 1):
            raise _fail("anonymous site loop heuristic is inconsistent")
        if (minimum, width) not in ee_reads or (maximum, width) not in ee_reads:
            raise _fail("anonymous site extrema are absent from EE reads")
        minimum_key = (minimum, width)
        if maximum == minimum:
            required_extrema_counts[minimum_key] = (
                required_extrema_counts.get(minimum_key, 0) + execution_count
            )
        elif distinct_count == observed_in_range:
            for offset_index in range(range_begin, range_end):
                observed_offset = observed_offsets[offset_index]
                observed_key = (observed_offset, width)
                required_extrema_counts[observed_key] = (
                    required_extrema_counts.get(observed_key, 0) + 1
                )
        else:
            required_extrema_counts[minimum_key] = (
                required_extrema_counts.get(minimum_key, 0) + 1
            )
            maximum_key = (maximum, width)
            required_extrema_counts[maximum_key] = required_extrema_counts.get(maximum_key, 0) + 1

        order = (minimum, maximum, width, execution_count, distinct_count)
        if previous_order is not None and order < previous_order:
            raise _fail("anonymous sites are not in tracer order")
        previous_order = order
        site_per_width[width] = _checked_u64_add(
            site_per_width.get(width, 0), execution_count
        )
        site_total = _checked_u64_add(site_total, execution_count)

    for key, required_count in required_extrema_counts.items():
        if required_count > ee_reads[key]:
            raise _fail("anonymous site extrema require more executions than EE reads")
    if site_per_width != ee_per_width or site_total != ee_total:
        raise _fail("anonymous-site execution totals do not match EE reads")


def _validate_vif_chunks(rows: list[object], expected_size: int) -> None:
    previous_key: tuple[int, int, int] | None = None
    for raw_row in rows:
        row = _require_dict(raw_row)
        _require_exact_keys(row, _VIF_KEYS)
        source_offset = _require_int(
            row["source_relative_offset"], 0, expected_size - 1
        )
        source_width = _require_int(row["source_width"], 4, 4)
        source_word_count = _require_int(row["source_word_count"], 1, _UINT64_MAX)
        remaining = _require_int(
            row["remaining_output_element_count_before_chunk"], 0, _UINT32_MAX
        )
        _require_int(row["event_count"], 1, _UINT64_MAX)

        if source_width != 4 or source_offset % 4 != 0:
            raise _fail("VIF source range is not four-byte aligned")
        if source_word_count > (expected_size - source_offset) // source_width:
            raise _fail("VIF source range is outside the VUM span")

        key = (source_offset, source_word_count, remaining)
        if previous_key is not None and key <= previous_key:
            raise _fail("VIF chunks are not strictly sorted and unique")
        previous_key = key


def validate_trace_document(document: object, expected_size: int) -> dict[str, object]:
    """Validate an already-decoded trace and return the same root dictionary."""

    _validate_expected_size(expected_size)
    _preflight_json_tree(document)
    root = _require_dict(document)

    status_value = root.get("status")
    status = _require_string(status_value)
    expected_keys = _TOP_LEVEL_KEYS | ({"failure_reason"} if status == "failed" else set())
    _require_exact_keys(root, frozenset(expected_keys))

    if _require_string(root["schema"]) != SCHEMA:
        raise _fail("unsupported trace schema")
    if status not in ("complete", "partial", "inconclusive", "failed"):
        raise _fail("unsupported trace status")
    stop_reason = _require_string(root["stop_reason"])
    matching = _require_bool(root["matching_event_observed"])

    minimum_copies = 0 if status == "failed" else 1
    _require_int(
        root["selected_copy_count"], minimum_copies, _MAX_SELECTED_COPY_COUNT
    )
    frame_count = _require_int(root["frame_count"], 0, _MAX_FRAME_COUNT)

    if status == "complete":
        if stop_reason not in ("quiet_frames", "maximum_frames") or not matching:
            raise _fail("complete trace has inconsistent stop state")
        if frame_count == 0:
            raise _fail("complete trace stopped before its first frame")
        if stop_reason == "quiet_frames" and frame_count == _MAX_FRAME_COUNT:
            raise _fail("quiet-frame trace reached the maximum-frame stop first")
    elif status == "partial":
        if stop_reason != "vm_shutdown" or not matching:
            raise _fail("partial trace has inconsistent stop state")
        if frame_count == _MAX_FRAME_COUNT:
            raise _fail("partial trace reached the maximum-frame stop first")
    elif status == "inconclusive":
        if stop_reason not in ("vm_shutdown", "maximum_frames") or matching:
            raise _fail("inconclusive trace has inconsistent stop state")
        if stop_reason == "maximum_frames" and frame_count == 0:
            raise _fail("maximum-frame trace stopped before its first frame")
        if stop_reason == "vm_shutdown" and frame_count == _MAX_FRAME_COUNT:
            raise _fail("shutdown trace reached the maximum-frame stop first")
    else:
        if stop_reason != "failure":
            raise _fail("failed trace has inconsistent stop state")
        failure_reason = _require_string(root["failure_reason"])
        if failure_reason not in _FAILURE_REASONS:
            raise _fail("failed trace has an unknown failure reason")

    ee_rows = _require_list(root["ee_reads"])
    anonymous_rows = _require_list(root["anonymous_sites"])
    vif_rows = _require_list(root["vif1_unpack_chunks"])
    if status == "failed" and (ee_rows or anonymous_rows or vif_rows):
        raise _fail("failed traces must not contain aggregate rows")

    ee_reads, ee_per_width, ee_total = _validate_ee_reads(ee_rows, expected_size)
    _validate_anonymous_sites(
        anonymous_rows, expected_size, ee_reads, ee_per_width, ee_total
    )
    _validate_vif_chunks(vif_rows, expected_size)

    if status != "failed" and matching != bool(ee_rows or vif_rows):
        raise _fail("matching-event flag does not match aggregate rows")

    return root


def parse_and_validate_trace(raw: bytes, expected_size: int) -> dict[str, object]:
    """Strictly parse and validate one UTF-8 JSON report."""

    if not isinstance(raw, bytes):
        raise _fail("trace input must be bytes")
    if len(raw) > _MAX_REPORT_BYTES:
        raise _fail("trace report exceeds the maximum byte size")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise _fail("trace report is not valid UTF-8") from exc
    try:
        document = json.loads(
            text,
            object_pairs_hook=_object_without_duplicates,
            parse_constant=_reject_json_constant,
        )
    except TraceValidationError:
        raise
    except (json.JSONDecodeError, RecursionError, UnicodeError, ValueError) as exc:
        raise _fail("trace report is not strict JSON") from exc
    return validate_trace_document(document, expected_size)


def validate_repeat(
    primary_raw: bytes, repeat_raw: bytes, expected_size: int
) -> dict[str, object]:
    """Validate two reports and require their serialized bytes to be identical."""

    primary = parse_and_validate_trace(primary_raw, expected_size)
    parse_and_validate_trace(repeat_raw, expected_size)
    if primary_raw != repeat_raw:
        raise _fail("repeat report is not byte-identical")
    return primary


def _read_report(path: str) -> bytes:
    # Read one byte beyond the policy cap so even special/non-seekable files are
    # rejected without first allocating their entire contents.
    with open(path, "rb") as stream:
        raw = stream.read(_MAX_REPORT_BYTES + 1)
    if len(raw) > _MAX_REPORT_BYTES:
        raise _fail("trace report exceeds the maximum byte size")
    return raw


def _parse_cli(argv: Sequence[str]) -> tuple[str, int, str | None]:
    args = list(argv)
    if len(args) not in (3, 5) or not all(isinstance(arg, str) for arg in args):
        raise _fail("invalid command line")
    if args[1] != "--expected-size":
        raise _fail("invalid command line")
    if len(args) == 5 and args[3] != "--repeat":
        raise _fail("invalid command line")
    try:
        expected_size = int(args[2], 10)
    except ValueError as exc:
        raise _fail("invalid expected size") from exc
    _validate_expected_size(expected_size)
    return args[0], expected_size, args[4] if len(args) == 5 else None


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point; emits only fixed messages that cannot disclose report data."""

    args = sys.argv[1:] if argv is None else argv
    if list(args) in (["-h"], ["--help"]):
        print("usage: validate_vum_read_trace.py REPORT --expected-size N [--repeat SECOND]")
        return 0

    try:
        report_path, expected_size, repeat_path = _parse_cli(args)
        primary_raw = _read_report(report_path)
        if repeat_path is None:
            document = parse_and_validate_trace(primary_raw, expected_size)
        else:
            repeat_raw = _read_report(repeat_path)
            document = validate_repeat(primary_raw, repeat_raw, expected_size)
    except (TraceValidationError, OSError, OverflowError, TypeError, ValueError):
        print("VUM read-trace report validation failed.", file=sys.stderr)
        return 1

    if document["status"] == "complete":
        print("VUM read-trace report is structurally valid and complete.")
        return 0
    print("VUM read-trace report is structurally valid but non-complete.")
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
