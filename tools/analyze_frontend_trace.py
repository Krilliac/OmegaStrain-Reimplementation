#!/usr/bin/env python3
"""Derive a bounded action summary from one sanitized front-end trace bundle.

The input must first satisfy the complete ``omega-frontend-trace-v1`` contract.
The derived document retains only fixed aggregate deltas and anonymous transition-
shape comparisons against the idle scenario. It never publishes paths, site IDs,
state identities, raw observations, hashes, addresses, or payload information.
"""

from __future__ import annotations

import json
import sys
from collections.abc import Sequence
from typing import cast

if __package__:
    from . import validate_frontend_trace as validator
else:  # Direct execution adds tools/ rather than the repository root.
    try:
        import validate_frontend_trace as validator
    except ModuleNotFoundError as exc:
        # Isolated mode (python -I, or -P) keeps the script directory off
        # sys.path, so the sibling module is simply not importable by name. Only
        # that exact case is handled here: a ModuleNotFoundError naming any other
        # module -- including one raised from inside a validator that WAS found --
        # must propagate unchanged rather than be masked by a path-based reload.
        if exc.name != "validate_frontend_trace":
            raise
        import importlib.util
        import os.path

        _validator_path = os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "validate_frontend_trace.py"
        )
        # A package-qualified spec name cannot collide with an unrelated
        # top-level module already in sys.modules, and the real "tools" package
        # is never imported on this direct-execution branch.
        _spec = importlib.util.spec_from_file_location(
            "tools.validate_frontend_trace", _validator_path
        )
        if _spec is None or _spec.loader is None:
            raise ModuleNotFoundError(
                f"cannot locate sibling validator at {_validator_path}",
                name="validate_frontend_trace",
            ) from exc
        validator = importlib.util.module_from_spec(_spec)
        # Register before execution so a self-reference would resolve, and remove
        # the half-initialized module again if execution fails.
        sys.modules[_spec.name] = validator
        try:
            _spec.loader.exec_module(validator)
        except BaseException:
            sys.modules.pop(_spec.name, None)
            raise


SUMMARY_SCHEMA = "omega-frontend-trace-summary-v1"
SOURCE_SCHEMA = "omega-frontend-trace-v1"
ACTION_SCENARIOS = ("previous", "next", "confirm", "back")
_EXPECTED_SOURCE_SCENARIOS = ("idle",) + ACTION_SCENARIOS
_EXPECTED_SOURCE_TRIALS = (0, 1)
EVENT_CLASSES = ("control", "transition", "render", "resource", "other")
RESOURCE_CLASSES = ("texture", "font", "audio", "other")
_MAXIMUM_INPUT_BYTES = 4 * 1024 * 1024
_MAXIMUM_SUMMARY_BYTES = 4096


class FrontendTraceAnalysisError(ValueError):
    """The validated source contract cannot produce the fixed summary contract."""


def _require_source_contract() -> None:
    if (
        validator.SCHEMA != SOURCE_SCHEMA
        or validator.SCENARIOS != _EXPECTED_SOURCE_SCENARIOS
        or validator.TRIALS != _EXPECTED_SOURCE_TRIALS
        or validator.EVENT_CLASSES != EVENT_CLASSES
        or validator.RESOURCE_CLASSES != RESOURCE_CLASSES
    ):
        raise FrontendTraceAnalysisError("unsupported source contract")


def _counts(report: dict[str, object], key: str) -> dict[str, int]:
    return cast(dict[str, int], report[key])


def _absolute_transition_shape(
    report: dict[str, object],
) -> tuple[tuple[int, int], ...]:
    rows = cast(list[dict[str, int]], report["transition_ordinals"])
    absolute_frame = 0
    shape: list[tuple[int, int]] = []
    for row in rows:
        absolute_frame += row["frame_delta"]
        shape.append((absolute_frame, row["ordinal"]))
    return tuple(shape)


def _first_transition_divergence_frame(
    idle: tuple[tuple[int, int], ...],
    action: tuple[tuple[int, int], ...],
) -> int | None:
    common_count = min(len(idle), len(action))
    for index in range(common_count):
        if idle[index] != action[index]:
            return min(idle[index][0], action[index][0])
    if len(idle) == len(action):
        return None
    remaining = idle if len(idle) > common_count else action
    return remaining[common_count][0]


def _signed_count_deltas(
    idle: dict[str, int],
    action: dict[str, int],
    keys: tuple[str, ...],
) -> dict[str, int]:
    return {key: action[key] - idle[key] for key in keys}


def summarize_trace_document(document: object) -> dict[str, object]:
    """Validate and summarize one decoded front-end trace document.

    Trial zero represents each scenario only after the source validator proves its
    normalized trial-one repeat byte-identical. Transition ordinals remain anonymous;
    equality and divergence describe structural shape only, never state identity.
    """

    _require_source_contract()
    validated = validator.validate_trace_document(document)
    reports = cast(list[dict[str, object]], validated["reports"])
    idle_report = reports[0]
    idle_frame_count = cast(int, idle_report["frame_count"])
    idle_events = _counts(idle_report, "event_class_counts")
    idle_resources = _counts(idle_report, "resource_class_totals")
    idle_shape = _absolute_transition_shape(idle_report)

    actions: list[dict[str, object]] = []
    for scenario_index, scenario in enumerate(ACTION_SCENARIOS, start=1):
        report = reports[scenario_index * len(_EXPECTED_SOURCE_TRIALS)]
        if report["frame_count"] != idle_frame_count:
            raise FrontendTraceAnalysisError("scenario frame spans differ")
        action_shape = _absolute_transition_shape(report)
        actions.append(
            {
                "scenario": scenario,
                "event_class_deltas": _signed_count_deltas(
                    idle_events,
                    _counts(report, "event_class_counts"),
                    EVENT_CLASSES,
                ),
                "resource_class_deltas": _signed_count_deltas(
                    idle_resources,
                    _counts(report, "resource_class_totals"),
                    RESOURCE_CLASSES,
                ),
                "transition_row_count_delta": len(action_shape) - len(idle_shape),
                "transition_shape_matches_idle": action_shape == idle_shape,
                "first_transition_divergence_frame": (
                    _first_transition_divergence_frame(idle_shape, action_shape)
                ),
            }
        )

    return {
        "schema": SUMMARY_SCHEMA,
        "input_relative_frame": validated["input_relative_frame"],
        "actions": actions,
    }


def encode_summary_document(document: object) -> bytes:
    """Validate a source bundle and encode its only accepted summary byte form."""

    summary = summarize_trace_document(document)
    encoded = json.dumps(
        summary,
        ensure_ascii=True,
        allow_nan=False,
        separators=(",", ":"),
        sort_keys=False,
    ).encode("ascii") + b"\n"
    if len(encoded) > _MAXIMUM_SUMMARY_BYTES:
        raise FrontendTraceAnalysisError("summary exceeds fixed output bound")
    return encoded


def parse_and_analyze_trace(raw: bytes) -> bytes:
    """Strictly validate canonical source bytes and return canonical summary bytes."""

    validated = validator.parse_and_validate_trace(raw)
    return encode_summary_document(validated)


def _read_report(path: str) -> bytes:
    with open(path, "rb") as stream:
        return stream.read(_MAXIMUM_INPUT_BYTES + 1)


def _write_summary(rendered: bytes) -> None:
    """Publish canonical bytes without platform newline translation."""

    binary_output = getattr(sys.stdout, "buffer", None)
    if binary_output is not None:
        written = binary_output.write(rendered)
        if written != len(rendered):
            raise OSError("summary output was incomplete")
        binary_output.flush()
        return

    # In-process callers may replace stdout with a text-only stream such as
    # io.StringIO. The production CLI always takes the binary branch above.
    text = rendered.decode("ascii")
    written = sys.stdout.write(text)
    if written != len(text):
        raise OSError("summary output was incomplete")
    sys.stdout.flush()


def main(argv: Sequence[str] | None = None) -> int:
    """CLI entry point with fixed diagnostics that cannot disclose source data."""

    args = list(sys.argv[1:] if argv is None else argv)
    if args in (["-h"], ["--help"]):
        print("usage: analyze_frontend_trace.py REPORT")
        return 0
    if len(args) != 1 or not isinstance(args[0], str):
        print("Front-end trace analysis failed.", file=sys.stderr)
        return 1

    try:
        rendered = parse_and_analyze_trace(_read_report(args[0]))
        _write_summary(rendered)
    except Exception:
        print("Front-end trace analysis failed.", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
