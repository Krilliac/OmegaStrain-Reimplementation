#!/usr/bin/env python3
"""Validate canonical, sanitized front-end behavior trace bundles.

The accepted document is intentionally aggregate-only. Unknown fields fail closed,
and the fixed vocabulary leaves no place for executable addresses, registers, raw
state, payload bytes, source names, paths, offsets, or hashes.
"""

from __future__ import annotations

import json
import math
import re
import sys
from collections.abc import Sequence
from typing import cast


SCHEMA = "omega-frontend-trace-v1"
SCENARIOS = ("idle", "previous", "next", "confirm", "back")
TRIALS = (0, 1)
EVENT_CLASSES = ("control", "transition", "render", "resource", "other")
RESOURCE_CLASSES = ("texture", "font", "audio", "other")

_TOP_LEVEL_KEYS = ("schema", "input_relative_frame", "reports")
_REPORT_KEYS = (
    "scenario",
    "trial",
    "frame_count",
    "event_class_counts",
    "site_events",
    "transition_ordinals",
    "resource_class_totals",
)
_SITE_EVENT_KEYS = ("frame_delta", "site", "event_class", "count")
_TRANSITION_KEYS = ("frame_delta", "ordinal")

_MAX_REPORT_BYTES = 4 * 1024 * 1024
_MAX_FRAME_COUNT = 600
_MAX_INPUT_RELATIVE_FRAME = 120
_MAX_SITE_COUNT = 4_096
_MAX_SITE_EVENTS = 32_768
_MAX_TRANSITIONS = 1_024
_MAX_PER_CLASS_COUNT = 1_000_000
_MAX_JSON_DEPTH = 12
_MAX_JSON_NODES = 500_000
_MAX_LIST_ITEMS = 32_768
_MAX_OBJECT_MEMBERS = 16
_UINT32_MAX = (1 << 32) - 1

_FORBIDDEN_PRIVACY_TOKENS = frozenset(
    (
        "address",
        "byte",
        "bytes",
        "crc",
        "data",
        "file",
        "filename",
        "guest",
        "hash",
        "instruction",
        "memory",
        "name",
        "offset",
        "opcode",
        "path",
        "payload",
        "pc",
        "ram",
        "raw",
        "register",
        "registers",
        "state",
    )
)


class FrontendTraceValidationError(ValueError):
    """The input is not an accepted sanitized front-end trace bundle."""


def _fail(message: str) -> FrontendTraceValidationError:
    return FrontendTraceValidationError(message)


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
    separated = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", key)
    tokens = tuple(token.lower() for token in re.findall(r"[A-Za-z0-9]+", separated))
    return any(token in _FORBIDDEN_PRIVACY_TOKENS for token in tokens)


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


def _require_int(value: object, minimum: int, maximum: int) -> int:
    if type(value) is not int:
        raise _fail("expected a bounded JSON integer")
    result = cast(int, value)
    if result < minimum or result > maximum:
        raise _fail("JSON integer is outside its allowed range")
    return result


def _require_exact_key_order(value: dict[str, object], keys: tuple[str, ...]) -> None:
    if tuple(value) != keys:
        raise _fail("JSON object keys are missing, extra, or noncanonical")


def _checked_count_add(left: int, right: int) -> int:
    result = left + right
    if result > _UINT32_MAX:
        raise _fail("aggregate count exceeds uint32")
    return result


def _canonical_value_bytes(value: object) -> bytes:
    return json.dumps(
        value,
        ensure_ascii=True,
        allow_nan=False,
        separators=(",", ":"),
        sort_keys=False,
    ).encode("ascii")


def _validate_fixed_counts(
    raw_value: object, keys: tuple[str, ...]
) -> dict[str, int]:
    value = _require_dict(raw_value)
    _require_exact_key_order(value, keys)
    counts: dict[str, int] = {}
    total = 0
    for key in keys:
        count = _require_int(value[key], 0, _MAX_PER_CLASS_COUNT)
        counts[key] = count
        total = _checked_count_add(total, count)
    return counts


def _validate_site_events(
    raw_value: object,
    frame_count: int,
    expected_counts: dict[str, int],
) -> None:
    rows = _require_list(raw_value)
    if not rows or len(rows) > _MAX_SITE_EVENTS:
        raise _fail("site-event count is outside its allowed range")

    class_order = {name: ordinal for ordinal, name in enumerate(EVENT_CLASSES)}
    observed_counts = {name: 0 for name in EVENT_CLASSES}
    site_classes: dict[int, str] = {}
    next_site = 0
    absolute_frame = 0
    prior_same_frame_key: tuple[int, int] | None = None

    for row_index, raw_row in enumerate(rows):
        row = _require_dict(raw_row)
        _require_exact_key_order(row, _SITE_EVENT_KEYS)
        frame_delta = _require_int(row["frame_delta"], 0, _MAX_FRAME_COUNT - 1)
        site = _require_int(row["site"], 0, _MAX_SITE_COUNT - 1)
        event_class = _require_string(row["event_class"])
        if event_class not in class_order:
            raise _fail("site event has an unsupported event class")
        count = _require_int(row["count"], 1, _MAX_PER_CLASS_COUNT)

        if row_index == 0:
            absolute_frame = frame_delta
        else:
            absolute_frame += frame_delta
        if absolute_frame >= frame_count:
            raise _fail("site-event frame deltas exceed the trace frame count")

        same_frame_key = (site, class_order[event_class])
        if frame_delta == 0 and prior_same_frame_key is not None:
            if same_frame_key <= prior_same_frame_key:
                raise _fail("same-frame site events are not strictly ordered")
        else:
            prior_same_frame_key = None
        prior_same_frame_key = same_frame_key

        known_class = site_classes.get(site)
        if known_class is None:
            if site != next_site:
                raise _fail("anonymous site IDs are not zero-based and sequential")
            site_classes[site] = event_class
            next_site += 1
        elif known_class != event_class:
            raise _fail("an anonymous site changed event class")

        observed_counts[event_class] = _checked_count_add(
            observed_counts[event_class], count
        )

    if observed_counts != expected_counts:
        raise _fail("site-event totals do not match event-class counts")


def _validate_transition_ordinals(
    raw_value: object, frame_count: int, expected_count: int
) -> None:
    rows = _require_list(raw_value)
    if not rows or len(rows) > _MAX_TRANSITIONS:
        raise _fail("transition count is outside its allowed range")
    if len(rows) != expected_count:
        raise _fail("transition ordinals do not match the transition event count")

    absolute_frame = 0
    next_ordinal = 0
    prior_ordinal: int | None = None
    for row_index, raw_row in enumerate(rows):
        row = _require_dict(raw_row)
        _require_exact_key_order(row, _TRANSITION_KEYS)
        frame_delta = _require_int(row["frame_delta"], 0, _MAX_FRAME_COUNT - 1)
        ordinal = _require_int(row["ordinal"], 0, _MAX_TRANSITIONS - 1)

        if row_index == 0:
            absolute_frame = frame_delta
        else:
            absolute_frame += frame_delta
        if absolute_frame >= frame_count:
            raise _fail("transition frame deltas exceed the trace frame count")
        if prior_ordinal == ordinal:
            raise _fail("consecutive transition ordinals must differ")
        if ordinal == next_ordinal:
            next_ordinal += 1
        elif ordinal > next_ordinal:
            raise _fail("transition ordinals are not normalized by first appearance")
        prior_ordinal = ordinal

    if next_ordinal == 0:
        raise _fail("transition ordinal zero was not introduced first")


def _normalized_report_bytes(report: dict[str, object]) -> bytes:
    normalized = {
        "scenario": report["scenario"],
        "frame_count": report["frame_count"],
        "event_class_counts": report["event_class_counts"],
        "site_events": report["site_events"],
        "transition_ordinals": report["transition_ordinals"],
        "resource_class_totals": report["resource_class_totals"],
    }
    return _canonical_value_bytes(normalized)


def validate_trace_document(document: object) -> dict[str, object]:
    """Validate an already-decoded bundle and return the same root dictionary."""

    _preflight_json_tree(document)
    root = _require_dict(document)
    _require_exact_key_order(root, _TOP_LEVEL_KEYS)
    if _require_string(root["schema"]) != SCHEMA:
        raise _fail("unsupported front-end trace schema")
    input_frame = _require_int(
        root["input_relative_frame"], 1, _MAX_INPUT_RELATIVE_FRAME
    )

    reports = _require_list(root["reports"])
    expected_report_count = len(SCENARIOS) * len(TRIALS)
    if len(reports) != expected_report_count:
        raise _fail("bundle must contain exactly two trials for every scenario")

    validated_reports: list[dict[str, object]] = []
    for report_index, raw_report in enumerate(reports):
        report = _require_dict(raw_report)
        _require_exact_key_order(report, _REPORT_KEYS)
        expected_scenario = SCENARIOS[report_index // len(TRIALS)]
        expected_trial = TRIALS[report_index % len(TRIALS)]
        if _require_string(report["scenario"]) != expected_scenario:
            raise _fail("reports are not in canonical scenario order")
        if _require_int(report["trial"], 0, 1) != expected_trial:
            raise _fail("reports are not in canonical trial order")

        frame_count = _require_int(report["frame_count"], 1, _MAX_FRAME_COUNT)
        if input_frame >= frame_count:
            raise _fail("fixed input frame is outside a trial's frame span")
        event_counts = _validate_fixed_counts(
            report["event_class_counts"], EVENT_CLASSES
        )
        _validate_site_events(report["site_events"], frame_count, event_counts)
        _validate_transition_ordinals(
            report["transition_ordinals"],
            frame_count,
            event_counts["transition"],
        )
        resource_counts = _validate_fixed_counts(
            report["resource_class_totals"], RESOURCE_CLASSES
        )
        if sum(resource_counts.values()) != event_counts["resource"]:
            raise _fail("resource-class totals do not match resource events")
        validated_reports.append(report)

    for scenario_index in range(len(SCENARIOS)):
        primary = validated_reports[scenario_index * 2]
        repeat = validated_reports[(scenario_index * 2) + 1]
        if _normalized_report_bytes(primary) != _normalized_report_bytes(repeat):
            raise _fail("normalized repeat trials are not byte-identical")

    idle_signature = _canonical_value_bytes(validated_reports[0]["transition_ordinals"])
    action_signatures = (
        _canonical_value_bytes(validated_reports[index * 2]["transition_ordinals"])
        for index in range(1, len(SCENARIOS))
    )
    if all(signature == idle_signature for signature in action_signatures):
        raise _fail("no action scenario has a transition signature distinct from idle")

    return root


def encode_trace_document(document: object) -> bytes:
    """Validate and encode a bundle in the only accepted canonical byte form."""

    validated = validate_trace_document(document)
    return _canonical_value_bytes(validated) + b"\n"


def parse_and_validate_trace(raw: bytes) -> dict[str, object]:
    """Strictly parse one canonical UTF-8 JSON bundle."""

    if not isinstance(raw, bytes):
        raise _fail("trace input must be bytes")
    if len(raw) > _MAX_REPORT_BYTES:
        raise _fail("front-end trace bundle exceeds the maximum byte size")
    try:
        text = raw.decode("utf-8", errors="strict")
    except UnicodeDecodeError as exc:
        raise _fail("front-end trace bundle is not valid UTF-8") from exc
    try:
        document = json.loads(
            text,
            object_pairs_hook=_object_without_duplicates,
            parse_constant=_reject_json_constant,
        )
    except FrontendTraceValidationError:
        raise
    except (json.JSONDecodeError, RecursionError, UnicodeError, ValueError) as exc:
        raise _fail("front-end trace bundle is not strict JSON") from exc

    validated = validate_trace_document(document)
    if raw != _canonical_value_bytes(validated) + b"\n":
        raise _fail("front-end trace bundle is not canonically encoded")
    return validated


def _read_report(path: str) -> bytes:
    with open(path, "rb") as stream:
        raw = stream.read(_MAX_REPORT_BYTES + 1)
    if len(raw) > _MAX_REPORT_BYTES:
        raise _fail("front-end trace bundle exceeds the maximum byte size")
    return raw


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point with fixed diagnostics that cannot disclose report data."""

    args = list(sys.argv[1:] if argv is None else argv)
    if args in (["-h"], ["--help"]):
        print("usage: validate_frontend_trace.py REPORT")
        return 0
    if len(args) != 1 or not isinstance(args[0], str):
        print("Front-end trace report validation failed.", file=sys.stderr)
        return 1

    try:
        document = parse_and_validate_trace(_read_report(args[0]))
    except (FrontendTraceValidationError, OSError, OverflowError, TypeError, ValueError):
        print("Front-end trace report validation failed.", file=sys.stderr)
        return 1

    if document["schema"] != SCHEMA:
        print("Front-end trace report validation failed.", file=sys.stderr)
        return 1
    print("Front-end trace report is canonical, sanitized, and repeat-stable.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
