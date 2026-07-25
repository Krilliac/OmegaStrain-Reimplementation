#!/usr/bin/env python3
"""Test whether VUM visual-geometry batch starts coincide with render-payload pair references.

This is an analysis-only falsification experiment over bytes that are already located. It
compares two independently derived sets of final-payload-relative offsets:

* the start offset of every geometry batch found by a Python mirror of the native
  ``DecodeVumVisualGeometryBatches`` VIF walk, and
* the documented per-pair reference order recorded by ``InspectVumRenderPayload`` and
  ``analysis/formats/VUM.md`` -- ``compact-reference, Q, P0, P2, P3`` for compact middle
  spans and ``grouped-reference-0, Q, P0, grouped-reference-1, P2, P3`` for grouped spans.

Both quantities live in the same coordinate space: offsets relative to the start of the
final payload region. Coincidence would nominate a batch-to-pair index; it would not
establish a material, texture, draw, or render binding, and VUM.md already records that Q
span size does not determine the matching final region and that Q/P opaque words do not
form simple material-index ranges.

The native geometry decoder does not publish a batch's final-payload-relative start
offset, so this script recomputes the walk independently rather than consuming decoder
output. The mirror is not compiler-verified against the native decoder; it is a
transcription of ``native/src/retail/vum_visual_geometry_decoder.cpp``.

The fixed report contains aggregate counters only. It never emits paths, archive/member
names, per-file rows, offsets, references, or payload bytes.
"""

from __future__ import annotations

import argparse
import json
import struct
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Iterator, Sequence

try:
    from tools import vum_material_catalog_contract as vum_contract
    from tools.fingerprint_assets import Span, parse_hog_span, read_at
except ModuleNotFoundError:  # Direct execution from the tools directory.
    import vum_material_catalog_contract as vum_contract
    from fingerprint_assets import Span, parse_hog_span, read_at


# Native ``asset::DecodeLimits`` defaults, mirrored so acceptance matches the decoders.
MAXIMUM_INPUT_BYTES = 64 * 1024 * 1024
MAXIMUM_ITEMS = 1 << 20

METADATA_RECORD_BYTES = vum_contract.METADATA_RECORD_BYTES

# Documented per-pair reference roles, in the two observed combined orders.
COMPACT_ROLES = ("compact_reference", "q", "p0", "p2", "p3")
GROUPED_ROLES = (
    "grouped_reference_0",
    "q",
    "p0",
    "grouped_reference_1",
    "p2",
    "p3",
)
ALL_ROLES = (
    "compact_reference",
    "grouped_reference_0",
    "grouped_reference_1",
    "q",
    "p0",
    "p2",
    "p3",
)

# The two candidate definitions of "where a batch starts". A batch opens at a V4-32 UNPACK
# (the two anchor points); the VIF command word and its inline data are four bytes apart.
START_VARIANTS = ("command_word", "unpack_data")

MAXIMUM_NESTED_HOG_DEPTH = 4


class AnalysisError(Exception):
    """A fixed-category failure that cannot disclose proprietary input."""

    def __init__(self, category: str) -> None:
        super().__init__(category)
        self.category = category


# --------------------------------------------------------------------------------------
# Pair reference order, recomputed from the accepted layout.
# --------------------------------------------------------------------------------------


@dataclass(frozen=True)
class PairReferences:
    """One Q/P pair's final-payload-relative references in documented combined order."""

    grouped: bool
    ordered: tuple[int, ...]
    by_role: dict[str, int]


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def collect_pair_references(
    data: bytes, layout: vum_contract.VumPayloadLayout
) -> list[PairReferences]:
    """Rebuild the documented per-pair combined reference order for an accepted span.

    The layout has already been validated by the shared contract mirror, so the record
    grammar, ordering, span families, and reference bounds hold here by construction.
    """

    final_begin = layout.final_payload_begin
    q_middle: list[int] = []
    q_final: list[int] = []
    p_words: list[tuple[int, int, int]] = []

    non_t_ordinal = 0
    for index in range(layout.metadata_record_count):
        record = layout.materials_end + index * METADATA_RECORD_BYTES
        if (
            layout.target_block_start_index
            <= index
            < layout.target_block_start_index + layout.target_count
        ):
            continue  # T block: one contiguous run of forward Q targets.
        if non_t_ordinal % 2 == 0:
            q_middle.append(_u32(data, record + 4))
            q_final.append(_u32(data, record + 12))
        else:
            p_words.append(
                (
                    _u32(data, record),
                    _u32(data, record + 8),
                    _u32(data, record + 12),
                )
            )
        non_t_ordinal += 1

    if len(q_middle) != layout.pair_count or len(p_words) != layout.pair_count:
        raise AnalysisError("pair_recovery_mismatch")

    pairs: list[PairReferences] = []
    for index in range(layout.pair_count):
        span_begin = q_middle[index]
        span_end = (
            q_middle[index + 1] if index + 1 < layout.pair_count else final_begin
        )
        grouped = (span_end - span_begin) != 16
        p0, p2, p3 = p_words[index]
        if grouped:
            first = _u32(data, span_begin + 0x74)
            second = _u32(data, span_begin + 0xF4)
            ordered = (first, q_final[index], p0, second, p2, p3)
            roles = GROUPED_ROLES
        else:
            first = _u32(data, span_begin + 4)
            ordered = (first, q_final[index], p0, p2, p3)
            roles = COMPACT_ROLES
        relative = tuple(value - final_begin for value in ordered)
        pairs.append(
            PairReferences(
                grouped=grouped,
                ordered=relative,
                by_role=dict(zip(roles, relative)),
            )
        )
    return pairs


# --------------------------------------------------------------------------------------
# Independent mirror of DecodeVumVisualGeometryBatches.
# --------------------------------------------------------------------------------------


@dataclass
class _BatchState:
    start_command_offset: int | None = None
    has_anchors: bool = False
    position_count: int = 0
    uv_count: int = 0


@dataclass(frozen=True)
class BatchStart:
    command_word: int
    unpack_data: int
    emitted: bool


def _vl_bytes(vl: int) -> int:
    if vl == 0:
        return 4
    if vl == 1:
        return 2
    if vl == 2:
        return 1
    return 2


def _align_up_4(value: int) -> int:
    return (value + 3) & ~3


@dataclass(frozen=True)
class WalkResult:
    starts: tuple[BatchStart, ...]
    # Batches flushed without ever opening at a V4-32 anchor UNPACK. These have no defined
    # start offset and are excluded from every alignment hypothesis.
    unanchored_batches: int
    # Why the fail-soft walk stopped: the native decoder returns the coherent batches it
    # already has rather than reporting an error, so this is the honest coverage signal.
    termination: str
    bytes_consumed: int


def walk_geometry_batches(final_payload: bytes) -> WalkResult:
    """Mirror the native VIF walk and report each batch's start offset."""

    size = len(final_payload)
    pos = 0
    batch = _BatchState()
    total_vertices = 0
    starts: list[BatchStart] = []
    unanchored_batches = 0
    termination = "stream_end"

    def flush() -> None:
        nonlocal batch, unanchored_batches
        emitted = (
            batch.has_anchors
            and batch.position_count >= 3
            and batch.uv_count > 0
            and min(batch.position_count, batch.uv_count) >= 3
        )
        if batch.start_command_offset is None:
            if emitted:
                unanchored_batches += 1
        else:
            starts.append(
                BatchStart(
                    command_word=batch.start_command_offset,
                    unpack_data=batch.start_command_offset + 4,
                    emitted=emitted,
                )
            )
        batch = _BatchState()

    while pos + 4 <= size:
        num = final_payload[pos + 2]
        cmd = final_payload[pos + 3] & 0x7F  # drop the interrupt bit
        command_offset = pos
        pos += 4

        if cmd == 0x00:
            continue  # NOP
        if 0x01 <= cmd <= 0x07:
            continue  # STCYCL/OFFSET/BASE/ITOP/STMOD/MSKPATH3/MARK -- immediate only
        if 0x10 <= cmd <= 0x17:
            continue  # FLUSH family / MSCAL / MSCNT
        if cmd == 0x20:
            pos += 4  # STMASK
            continue
        if cmd in (0x30, 0x31):
            pos += 16  # STROW / STCOL
            continue
        if cmd >= 0x60:
            vn = (cmd >> 2) & 0x3
            vl = cmd & 0x3
            components = vn + 1
            data_bytes = _align_up_4(num * components * _vl_bytes(vl))
            if pos + data_bytes > size:
                termination = "truncated_unpack"
                pos = command_offset
                break  # truncated -- stop, keep coherent batches
            data = pos

            if vn == 3 and vl == 0:  # V4-32 -> anchors, start of a batch
                if batch.has_anchors or batch.position_count:
                    flush()
                batch.start_command_offset = command_offset
                found = 0
                for vertex in range(num):
                    if found >= 2:
                        break
                    voff = data + vertex * 16
                    components_xyz = struct.unpack_from("<3f", final_payload, voff)
                    if any(value != 0.0 for value in components_xyz):
                        found += 1
                if found >= 2:
                    batch.has_anchors = True
            elif vn == 2 and vl == 1:  # V3-16 -> positions
                total_vertices += num
                if total_vertices > MAXIMUM_ITEMS:
                    raise AnalysisError("geometry_item_limit")
                batch.position_count = num
            elif vn == 2 and vl == 2:  # V3-8 -> colors (first block only)
                pass
            elif vn == 1 and vl == 1:  # V2-16 -> UVs
                batch.uv_count = num
            pos += data_bytes
            continue
        termination = "unknown_command"
        pos = command_offset
        break  # unknown VIF command -- data size unknown, stop

    flush()
    return WalkResult(
        starts=tuple(starts),
        unanchored_batches=unanchored_batches,
        termination=termination,
        bytes_consumed=pos,
    )


# --------------------------------------------------------------------------------------
# Hypothesis evaluation.
# --------------------------------------------------------------------------------------


@dataclass
class Aggregate:
    totals: Counter[str] = field(default_factory=Counter)
    errors: Counter[str] = field(default_factory=Counter)
    coverage: Counter[str] = field(default_factory=Counter)
    membership: Counter[str] = field(default_factory=Counter)
    role_membership: Counter[str] = field(default_factory=Counter)
    role_index_aligned: Counter[str] = field(default_factory=Counter)
    hypotheses: Counter[str] = field(default_factory=Counter)

    def document(self) -> dict[str, object]:
        spans = self.totals["spans_accepted"]
        count_equal = self.hypotheses["batch_count_equals_pair_count_spans"]
        starts = self.totals["batch_starts_defined"]
        return {
            "schema_version": 1,
            "scope": (
                "aggregate offline coincidence counts only; no identities, per-file rows, "
                "offsets, or payload bytes"
            ),
            "totals": {key: value for key, value in sorted(self.totals.items())},
            "errors": {key: value for key, value in sorted(self.errors.items())},
            "walk_coverage": {
                key: value for key, value in sorted(self.coverage.items())
            },
            "counters": {
                key: value for key, value in sorted(self.hypotheses.items())
            },
            "membership": {
                key: value for key, value in sorted(self.membership.items())
            },
            "role_membership": {
                key: value for key, value in sorted(self.role_membership.items())
            },
            "role_index_aligned": {
                key: value for key, value in sorted(self.role_index_aligned.items())
            },
            "hypotheses": {
                "h1_batch_count_equals_pair_count": {
                    "spans_pass": count_equal,
                    "spans_fail": spans - count_equal,
                    "pass": bool(spans) and count_equal == spans,
                },
                "h2_every_batch_start_is_some_pair_reference": {
                    "variants": {
                        variant: {
                            "starts_matched": self.membership[
                                f"{variant}_in_any_reference"
                            ],
                            "starts_unmatched": starts
                            - self.membership[f"{variant}_in_any_reference"],
                            "pass": bool(starts)
                            and self.membership[f"{variant}_in_any_reference"] == starts,
                        }
                        for variant in START_VARIANTS
                    }
                },
                "h3_ith_batch_start_equals_ith_pair_role": {
                    "variants": {
                        variant: {
                            role: {
                                "exact": self.role_index_aligned[
                                    f"{variant}_{role}_exact"
                                ],
                                "compared": self.role_index_aligned[
                                    f"{variant}_{role}_compared"
                                ],
                                "pass": bool(
                                    self.role_index_aligned[
                                        f"{variant}_{role}_compared"
                                    ]
                                )
                                and self.role_index_aligned[f"{variant}_{role}_exact"]
                                == self.role_index_aligned[
                                    f"{variant}_{role}_compared"
                                ]
                                and count_equal == spans,
                            }
                            for role in ALL_ROLES
                        }
                        for variant in START_VARIANTS
                    }
                },
            },
        }


def measure_span(aggregate: Aggregate, data: bytes) -> None:
    layout = vum_contract.validate_vum_payload_layout(
        data,
        maximum_input_bytes=MAXIMUM_INPUT_BYTES,
        maximum_items=MAXIMUM_ITEMS,
    )
    pairs = collect_pair_references(data, layout)
    final_payload = data[layout.final_payload_begin : layout.primary_end]
    walk = walk_geometry_batches(final_payload)
    starts = walk.starts
    unanchored = walk.unanchored_batches

    emitted = [start for start in starts if start.emitted]
    aggregate.coverage[f"walk_terminated_{walk.termination}"] += 1
    aggregate.totals["walk_bytes_consumed"] += walk.bytes_consumed
    if len(emitted) >= 5:
        aggregate.coverage["spans_with_5_or_more_batches"] += 1
    else:
        aggregate.coverage[f"spans_with_{len(emitted)}_batches"] += 1
    aggregate.totals["spans_accepted"] += 1
    aggregate.totals["pairs"] += len(pairs)
    aggregate.totals["pairs_compact"] += sum(1 for pair in pairs if not pair.grouped)
    aggregate.totals["pairs_grouped"] += sum(1 for pair in pairs if pair.grouped)
    aggregate.totals["pair_references"] += sum(len(pair.ordered) for pair in pairs)
    aggregate.totals["batch_starts_defined"] += len(emitted)
    aggregate.totals["batch_starts_not_emitted"] += len(starts) - len(emitted)
    aggregate.totals["batches_emitted_without_start"] += unanchored
    aggregate.totals["final_payload_bytes"] += len(final_payload)
    if len(emitted) == len(pairs):
        aggregate.hypotheses["batch_count_equals_pair_count_spans"] += 1
    elif len(emitted) < len(pairs):
        aggregate.hypotheses["fewer_batches_than_pairs_spans"] += 1
    else:
        aggregate.hypotheses["more_batches_than_pairs_spans"] += 1
    if not emitted:
        aggregate.hypotheses["spans_without_any_batch"] += 1
    if not pairs:
        aggregate.hypotheses["spans_without_any_pair"] += 1

    all_references = {value for pair in pairs for value in pair.ordered}
    references_by_role: dict[str, set[int]] = {role: set() for role in ALL_ROLES}
    for pair in pairs:
        for role, value in pair.by_role.items():
            references_by_role[role].add(value)

    for variant in START_VARIANTS:
        for start in emitted:
            value = getattr(start, variant)
            if value in all_references:
                aggregate.membership[f"{variant}_in_any_reference"] += 1
            if value % 16 == 0:
                aggregate.membership[f"{variant}_16_byte_aligned"] += 1
            # VUM.md records that the first combined reference is always 16 bytes into
            # the final region, so a start of exactly 16 matches trivially.
            if value == 16:
                aggregate.membership[f"{variant}_equals_documented_first_reference"] += 1
            for role in ALL_ROLES:
                if value in references_by_role[role]:
                    aggregate.role_membership[f"{variant}_{role}"] += 1

        for index in range(min(len(emitted), len(pairs))):
            value = getattr(emitted[index], variant)
            for role in ALL_ROLES:
                expected = pairs[index].by_role.get(role)
                if expected is None:
                    continue
                aggregate.role_index_aligned[f"{variant}_{role}_compared"] += 1
                if value == expected:
                    aggregate.role_index_aligned[f"{variant}_{role}_exact"] += 1


# --------------------------------------------------------------------------------------
# Corpus discovery.
# --------------------------------------------------------------------------------------


def _iter_hog_vum_spans(
    file: BinaryIO, entries: Sequence[Span], depth: int
) -> Iterator[bytes]:
    for entry in entries:
        suffix = Path(entry.name).suffix.lower()
        if suffix == ".vum":
            if entry.size and entry.size <= MAXIMUM_INPUT_BYTES:
                yield read_at(file, entry.offset, entry.size)
            continue
        if suffix != ".hog" or depth >= MAXIMUM_NESTED_HOG_DEPTH:
            continue
        try:
            nested = parse_hog_span(file, entry.offset, entry.size)
        except ValueError:
            continue
        yield from _iter_hog_vum_spans(file, nested.entries, depth + 1)


def iter_vum_spans(disc_root: Path, aggregate: Aggregate) -> Iterator[bytes]:
    for path in sorted(disc_root.rglob("*.HOG")):
        aggregate.totals["containers_scanned"] += 1
        try:
            with path.open("rb") as file:
                try:
                    directory = parse_hog_span(file, 0, path.stat().st_size)
                except ValueError:
                    aggregate.errors["container_malformed"] += 1
                    continue
                yield from _iter_hog_vum_spans(file, directory.entries, 0)
        except OSError:
            aggregate.errors["container_io"] += 1
    for path in sorted(disc_root.rglob("*.VUM")):
        try:
            size = path.stat().st_size
            if size and size <= MAXIMUM_INPUT_BYTES:
                yield path.read_bytes()
        except OSError:
            aggregate.errors["loose_span_io"] += 1


def scan_disc(disc_root: Path) -> dict[str, object]:
    if not disc_root.is_dir():
        raise AnalysisError("unsafe_input")
    aggregate = Aggregate()
    for data in iter_vum_spans(disc_root, aggregate):
        aggregate.totals["spans_discovered"] += 1
        try:
            measure_span(aggregate, data)
        except vum_contract.VumContractError as error:
            aggregate.errors[f"vum_{error.code}"] += 1
        except AnalysisError as error:
            aggregate.errors[error.category] += 1
        except (struct.error, IndexError, ValueError):
            aggregate.errors["span_walk"] += 1
    return aggregate.document()


def failure_document(category: str) -> dict[str, object]:
    aggregate = Aggregate()
    aggregate.errors[category] += 1
    return aggregate.document()


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("disc_root", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args(argv)

    try:
        result = scan_disc(args.disc_root)
    except AnalysisError as error:
        result = failure_document(error.category)
    except OSError:
        result = failure_document("io")

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 1 if sum(result["errors"].values()) else 0


if __name__ == "__main__":
    raise SystemExit(main())
