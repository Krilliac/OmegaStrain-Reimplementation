#!/usr/bin/env python3
"""Score bounded VUM render-payload topology hypotheses without exporting retail data.

The renderer currently reconstructs each VUM visual batch as one alternating-winding
triangle strip, and the result is honestly sparse.  This tool tests, offline and
corpus-wide, *why* it is sparse and which of the candidate per-vertex/per-batch
strip-break carriers survives contact with the owned NTSC-U corpus.

ACCEPTANCE GATES (stated before the run, so every hypothesis can fail)
----------------------------------------------------------------------
``h1_chunked_container`` is SUPPORTED only if, across every accepted VUM:

  * every candidate chunk start is also an independently proven Q/P/middle
    metadata reference into the final payload (cross-structure corroboration,
    zero unexplained starts);
  * every chunk's own VIF walk terminates exactly at the next chunk start or at
    the end of the payload, with zero unknown-command aborts and zero truncated
    blocks;
  * the walked byte coverage strictly exceeds the current single-stream model's
    coverage; and
  * the reconstructed vertex count strictly exceeds the current model's.

``h2_header_block_topology_slots`` is STRUCTURALLY SUPPORTED only if, in every
canonical chunk, the twelve header words following the two extracted anchors are
all congruent to two modulo three, never exceed that chunk's own single-strip
flat triangle-index bound, have a strictly increasing active prefix, always end
in the inactive sentinel, and the following word encodes that chunk's vertex
count.  Structural support is NOT topology: the hypothesis is only SEMANTICALLY
proven if ``h2b_slot_groups_are_independent_strips`` also passes, which requires
the per-group vertex demand to sum exactly to the chunk's declared vertex count
in every canonical chunk.

``h3_v4_16_positions``, ``h4_stmask_strow_per_vertex_flag`` and
``h5_mscal_immediate_selects_format`` are REJECTED if their falsifier fires even
once; each is stated so that a single counterexample kills it.

A separate ``non_degenerate_and_finite`` block reports whether the reconstructed
triangles are actually usable.  A model that increases the triangle count while
emitting degenerate triangles has not earned promotion to a decoder.

PRIVACY
-------
The output is deliberately anonymous, fixed-schema and aggregate-only.  It never
contains paths, level codes, archive entry names, member identities, per-file or
per-level rows, hashes, byte offsets, payload bytes, opaque word values, or magic
value histograms.  Only counts, ratios and per-hypothesis verdicts are emitted.
The tool opens corpus inputs read-only and writes nothing but this JSON document
to standard output.

A verdict here is evidence about a *hypothesis*, not a decoded format.  Nothing in
this report promotes a hypothesis to a decoder, assigns render, material, draw,
placement or visibility meaning, or licenses a change to
``vum_visual_geometry_decoder.cpp``.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import BinaryIO, Sequence

if __package__:
    from .fingerprint_assets import parse_hog_span
    from .vum_material_catalog_contract import (
        VumContractError,
        validate_vum_payload_layout,
    )
else:  # Direct execution adds tools/ rather than the repository root.
    from fingerprint_assets import parse_hog_span
    from vum_material_catalog_contract import (
        VumContractError,
        validate_vum_payload_layout,
    )


# The retail chunk tag inside the VUM final payload.  This is the same class of
# structural evidence as the already-documented ``VUMS`` and ``MTRL`` tags; it is
# a grammar constant, and no tag byte reaches the emitted report.
CHUNK_TAG = b"MSCL"

METADATA_RECORD_BYTES = 16
COMPACT_MIDDLE_SPAN_BYTES = 16
COMPACT_REFERENCE_OFFSET = 4
GROUPED_REFERENCE_OFFSETS = (0x74, 0xF4)

# One VIF UNPACK: bytes per component for the vl field.  Mirrors ``VlBytes`` in
# native/src/retail/vum_visual_geometry_decoder.cpp so the current-model figures
# in this report are the ones that decoder actually produces.
VL_COMPONENT_BYTES = {0: 4, 1: 2, 2: 1}
DEFAULT_VL_COMPONENT_BYTES = 2

# The header block layout under test: two anchor quadwords, twelve candidate
# topology slots, then one marker word.
HEADER_QUADWORDS = 6
ANCHOR_WORDS = 8
SLOT_WORDS = 12
MARKER_WORD_INDEX = 20
SLOT_SENTINEL = 2
MARKER_HIGH_BIT = 0x8000

CANONICAL_CHUNK_SHAPE = ((4, 32), (3, 16), (3, 8), (3, 8), (2, 16))

ERROR_CATEGORIES = (
    "config",
    "io",
    "unsafe_input",
    "missing_level_input",
    "filesystem_limit",
    "container_limit",
    "payload_limit",
    "layout_rejected",
)


@dataclass(frozen=True)
class ScanLimits:
    maximum_levels: int = 4096
    maximum_filesystem_entries: int = 500_000
    maximum_container_bytes: int = 4 * 1024 * 1024 * 1024
    maximum_hog_entries: int = 2_000_000
    maximum_nesting_depth: int = 32
    maximum_vum_occurrences: int = 1 << 20
    maximum_vum_bytes: int = 64 * 1024 * 1024
    maximum_total_payload_bytes: int = 16 * 1024 * 1024 * 1024
    maximum_chunks: int = 1 << 22
    maximum_vertices: int = 1 << 28
    maximum_vif_commands_per_walk: int = 1 << 20


class ScanFailure(ValueError):
    def __init__(self, category: str) -> None:
        if category not in ERROR_CATEGORIES:
            raise ValueError("unknown aggregate error category")
        super().__init__(category)
        self.category = category


class AggregateArgumentError(Exception):
    pass


class AggregateArgumentParser(argparse.ArgumentParser):
    def error(self, message: str) -> None:
        del message
        raise AggregateArgumentError from None


@dataclass
class ScanBudget:
    limits: ScanLimits
    filesystem_entries: int = 0
    container_bytes: int = 0
    hog_entries: int = 0
    vum_occurrences: int = 0
    payload_bytes: int = 0
    chunks: int = 0
    vertices: int = 0

    def add_filesystem_entries(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_filesystem_entries - self.filesystem_entries:
            raise ScanFailure("filesystem_limit")
        self.filesystem_entries += count

    def add_container_bytes(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_container_bytes - self.container_bytes:
            raise ScanFailure("container_limit")
        self.container_bytes += count

    def add_hog_entries(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_hog_entries - self.hog_entries:
            raise ScanFailure("container_limit")
        self.hog_entries += count

    def add_vum(self, span_bytes: int) -> None:
        if span_bytes < 0 or span_bytes > self.limits.maximum_vum_bytes:
            raise ScanFailure("payload_limit")
        if self.vum_occurrences >= self.limits.maximum_vum_occurrences:
            raise ScanFailure("payload_limit")
        if span_bytes > self.limits.maximum_total_payload_bytes - self.payload_bytes:
            raise ScanFailure("payload_limit")
        self.vum_occurrences += 1
        self.payload_bytes += span_bytes

    def add_chunks(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_chunks - self.chunks:
            raise ScanFailure("payload_limit")
        self.chunks += count

    def add_vertices(self, count: int) -> None:
        if count < 0 or count > self.limits.maximum_vertices - self.vertices:
            raise ScanFailure("payload_limit")
        self.vertices += count


class Totals:
    """Named integer counters only.  No value ever identifies a corpus member."""

    def __init__(self) -> None:
        self._counts: dict[str, int] = {}

    def add(self, name: str, amount: int = 1) -> None:
        self._counts[name] = self._counts.get(name, 0) + amount

    def get(self, name: str) -> int:
        return self._counts.get(name, 0)

    def ensure(self, *names: str) -> None:
        for name in names:
            self._counts.setdefault(name, 0)

    def document(self) -> dict[str, int]:
        return dict(sorted(self._counts.items()))


def _ratio(numerator: int, denominator: int) -> float:
    if denominator <= 0:
        return 0.0
    return round(numerator / denominator, 6)


def _verdict(tested: int, passed: int) -> str:
    if tested == 0:
        return "untested"
    if passed == tested:
        return "supported"
    if passed == 0:
        return "rejected"
    return "inconclusive"


def _falsifier_verdict(tested: int, falsifier_hits: int) -> str:
    if tested == 0:
        return "untested"
    return "rejected" if falsifier_hits > 0 else "not_falsified"


# ---------------------------------------------------------------------------
# Bounded, read-only corpus traversal
# ---------------------------------------------------------------------------


def _stat_is_reparse(info: os.stat_result) -> bool:
    return bool(getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def _require_directory(path: Path) -> None:
    try:
        info = path.lstat()
    except OSError as exc:
        raise ScanFailure("missing_level_input") from exc
    if stat.S_ISLNK(info.st_mode) or _stat_is_reparse(info):
        raise ScanFailure("unsafe_input")
    if not stat.S_ISDIR(info.st_mode):
        raise ScanFailure("missing_level_input")


def _discover_level_archives(root: Path, budget: ScanBudget) -> tuple[Path, ...]:
    """Every ``GAMEDATA/<level>/DATA.HOG`` under the owner-supplied root."""

    gamedata = root / "GAMEDATA"
    _require_directory(gamedata)
    discovered: list[Path] = []
    try:
        with os.scandir(gamedata) as entries:
            ordered = sorted(entries, key=lambda entry: entry.name.upper())
        for entry in ordered:
            budget.add_filesystem_entries(1)
            info = entry.stat(follow_symlinks=False)
            if entry.is_symlink() or _stat_is_reparse(info):
                raise ScanFailure("unsafe_input")
            if not entry.is_dir(follow_symlinks=False):
                continue
            archive = Path(entry.path) / "DATA.HOG"
            budget.add_filesystem_entries(1)
            if not archive.is_file():
                continue
            if len(discovered) >= budget.limits.maximum_levels:
                raise ScanFailure("filesystem_limit")
            discovered.append(archive)
    except ScanFailure:
        raise
    except OSError as exc:
        raise ScanFailure("io") from exc
    if not discovered:
        raise ScanFailure("missing_level_input")
    return tuple(discovered)


def _collect_vum_spans(
    stream: BinaryIO, base: int, span_bytes: int, depth: int, budget: ScanBudget
) -> list[tuple[int, int]]:
    """Depth-bounded HOG traversal returning ``(offset, size)`` for each VUM member."""

    if depth > budget.limits.maximum_nesting_depth:
        return []
    try:
        directory = parse_hog_span(stream, base, span_bytes)
    except ValueError:
        return []
    budget.add_hog_entries(len(directory.entries))
    found: list[tuple[int, int]] = []
    for entry in directory.entries:
        suffix = Path(entry.name).suffix.lower()
        if suffix == ".vum":
            found.append((entry.offset, entry.size))
        elif suffix == ".hog":
            found.extend(_collect_vum_spans(stream, entry.offset, entry.size, depth + 1, budget))
    return found


# ---------------------------------------------------------------------------
# Proven metadata references (public layout fields only)
# ---------------------------------------------------------------------------


def _proven_final_reference_offsets(data: bytes, layout) -> set[int]:
    """Final-payload-relative offsets of every already-proven Q/P/middle reference.

    Uses only the published P/Q/T contract: the T records form one contiguous
    block starting at ``target_block_start_index``, and removing that block leaves
    exact ``Q,P,Q,P,...`` ordering.  No new structural claim is introduced here.
    """

    begin = layout.final_payload_begin
    references: set[int] = set()
    middle_starts: list[int] = []
    non_target_ordinal = 0
    target_end = layout.target_block_start_index + layout.target_count
    for index in range(layout.metadata_record_count):
        if layout.target_block_start_index <= index < target_end:
            continue
        record = layout.materials_end + index * METADATA_RECORD_BYTES
        words = struct.unpack_from("<4I", data, record)
        if non_target_ordinal % 2 == 0:  # Q
            middle_starts.append(words[1])
            references.add(words[3] - begin)
        else:  # P
            references.add(words[0] - begin)
            references.add(words[2] - begin)
            references.add(words[3] - begin)
        non_target_ordinal += 1

    middle_starts.append(begin)
    for span_start, span_end in zip(middle_starts, middle_starts[1:]):
        span_bytes = span_end - span_start
        if span_bytes == COMPACT_MIDDLE_SPAN_BYTES:
            slots = (COMPACT_REFERENCE_OFFSET,)
        else:
            slots = GROUPED_REFERENCE_OFFSETS
        for slot in slots:
            if span_start + slot + 4 <= len(data):
                references.add(struct.unpack_from("<I", data, span_start + slot)[0] - begin)
    return references


# ---------------------------------------------------------------------------
# VIF walks
# ---------------------------------------------------------------------------


@dataclass
class WalkResult:
    end_offset: int
    stop: str  # "end" | "boundary" | "unknown_command" | "truncated" | "budget"
    blocks: list[tuple[int, int, int, int, int]] = field(default_factory=list)
    command_counts: dict[str, int] = field(default_factory=dict)


def _walk_vif(payload: memoryview, start: int, limit: int, budget: ScanBudget,
              stop_at_tag: bool, widen_three_component_16: bool = False) -> WalkResult:
    """Byte-synced VIF walk mirroring the native decoder's command handling.

    ``widen_three_component_16`` is the H3 falsifier: it consumes every V3-16
    UNPACK at four components (eight bytes per vertex) instead of the declared
    three, and reports whether the chunk still parses to its boundary.
    """

    result = WalkResult(end_offset=start, stop="end")
    position = start
    commands = result.command_counts
    executed = 0
    while position + 4 <= limit:
        if executed >= budget.limits.maximum_vif_commands_per_walk:
            result.stop = "budget"
            break
        executed += 1
        if stop_at_tag and position + 4 <= limit and bytes(payload[position:position + 4]) == CHUNK_TAG:
            result.stop = "boundary"
            break
        immediate = payload[position] | (payload[position + 1] << 8)
        num = payload[position + 2]
        command = payload[position + 3] & 0x7F
        position += 4
        if command <= 0x07:
            commands["framing"] = commands.get("framing", 0) + 1
            continue
        if 0x10 <= command <= 0x17:
            commands["microprogram_family"] = commands.get("microprogram_family", 0) + 1
            continue
        if command == 0x20:
            commands["stmask"] = commands.get("stmask", 0) + 1
            position += 4
            continue
        if command in (0x30, 0x31):
            commands["strow_stcol"] = commands.get("strow_stcol", 0) + 1
            position += 16
            continue
        if command >= 0x60:
            vn = (command >> 2) & 0x3
            vl = command & 0x3
            components = vn + 1
            element_bytes = VL_COMPONENT_BYTES.get(vl, DEFAULT_VL_COMPONENT_BYTES)
            consumed_components = components
            if widen_three_component_16 and components == 3 and element_bytes == 2:
                consumed_components = 4
            data_bytes = (num * consumed_components * element_bytes + 3) & ~3
            if position + data_bytes > limit:
                result.stop = "truncated"
                position -= 4
                break
            commands["unpack"] = commands.get("unpack", 0) + 1
            result.blocks.append((components, element_bytes * 8, num, position, immediate))
            position += data_bytes
            continue
        result.stop = "unknown_command"
        position -= 4
        break
    result.end_offset = position
    return result


def _tag_offsets(payload: bytes) -> list[int]:
    offsets: list[int] = []
    index = payload.find(CHUNK_TAG)
    while index != -1:
        if index % 4 == 0:
            offsets.append(index)
        index = payload.find(CHUNK_TAG, index + 1)
    return offsets


# ---------------------------------------------------------------------------
# Reconstruction helpers
# ---------------------------------------------------------------------------


def _single_strip_triangles(vertex_count: int) -> int:
    return max(vertex_count - 2, 0)


def _degenerate_strip_triangles(view: memoryview, offset: int, vertex_count: int) -> int:
    """Triangles of the naive alternating strip whose corners are not distinct."""

    if vertex_count < 3:
        return 0
    points = [struct.unpack_from("<3h", view, offset + index * 6) for index in range(vertex_count)]
    degenerate = 0
    for index in range(vertex_count - 2):
        a, b, c = points[index], points[index + 1], points[index + 2]
        if a == b or b == c or a == c:
            degenerate += 1
    return degenerate


def _current_model_batches(view: memoryview, payload_bytes: int, budget: ScanBudget) -> tuple[int, int, int, str]:
    """Vertices, triangles, consumed bytes and stop reason of the shipped decoder walk.

    Mirrors ``DecodeVumVisualGeometryBatches`` plus ``FlushBatch``: a batch needs two
    nonzero anchors, at least three positions and a nonempty UV block, and its vertex
    count is ``min(positions, uvs)``.
    """

    result = _walk_vif(view, 0, payload_bytes, budget, stop_at_tag=False)
    vertices = 0
    triangles = 0
    has_anchors = False
    positions = 0
    uvs = 0

    def flush() -> tuple[int, int]:
        if not has_anchors or positions < 3 or uvs == 0:
            return 0, 0
        count = min(positions, uvs)
        if count < 3:
            return 0, 0
        return count, _single_strip_triangles(count)

    for components, bits, num, offset, immediate in result.blocks:
        if components == 4 and bits == 32:
            if has_anchors or positions:
                gained_v, gained_t = flush()
                vertices += gained_v
                triangles += gained_t
                has_anchors, positions, uvs = False, 0, 0
            found = 0
            for vertex in range(num):
                base = offset + vertex * 16
                x, y, z = struct.unpack_from("<3f", view, base)
                if x or y or z:
                    found += 1
                if found >= 2:
                    break
            has_anchors = found >= 2
        elif components == 3 and bits == 16:
            positions = num
        elif components == 2 and bits == 16:
            uvs = num
    gained_v, gained_t = flush()
    vertices += gained_v
    triangles += gained_t
    return vertices, triangles, result.end_offset, result.stop


# ---------------------------------------------------------------------------
# Per-VUM hypothesis scoring
# ---------------------------------------------------------------------------


def _score_vum(data: bytes, totals: Totals, budget: ScanBudget) -> None:
    try:
        layout = validate_vum_payload_layout(
            data,
            maximum_input_bytes=budget.limits.maximum_vum_bytes,
            maximum_items=budget.limits.maximum_vertices,
        )
    except (VumContractError, ValueError, struct.error):
        totals.add("vum_layout_rejected")
        return
    totals.add("vum_layout_accepted")

    payload = data[layout.final_payload_begin:layout.primary_end]
    payload_bytes = len(payload)
    view = memoryview(payload)
    totals.add("final_payload_bytes", payload_bytes)

    # --- current shipped model -------------------------------------------------
    vertices, triangles, consumed, stop = _current_model_batches(view, payload_bytes, budget)
    totals.add("current_model_vertices", vertices)
    totals.add("current_model_triangles", triangles)
    totals.add("current_model_bytes_consumed", consumed)
    totals.add(f"current_model_walk_stop_{stop}")

    # --- H1: chunked container -------------------------------------------------
    starts = _tag_offsets(payload)
    budget.add_chunks(len(starts))
    totals.add("chunk_starts", len(starts))
    if not starts:
        totals.add("vum_without_chunk_start")
        return

    proven = _proven_final_reference_offsets(data, layout)
    totals.add("chunk_starts_that_are_proven_references", sum(1 for s in starts if s in proven))
    totals.add("proven_reference_offsets", len(proven))

    bounds = starts + [payload_bytes]
    chunked_bytes = 0
    last_index = len(starts) - 1
    for chunk_index, (start, next_start) in enumerate(zip(bounds, bounds[1:])):
        walk = _walk_vif(view, start + 4, next_start, budget, stop_at_tag=True)
        chunked_bytes += walk.end_offset - start
        residue = next_start - walk.end_offset
        totals.add("chunk_residue_bytes", max(residue, 0))
        if walk.stop in ("end", "boundary"):
            totals.add("chunks_parsed_to_boundary")
        elif walk.stop == "unknown_command":
            totals.add("chunks_unknown_command_abort")
        elif walk.stop == "truncated":
            totals.add("chunks_truncated_block")
        else:
            totals.add("chunks_budget_stop")
        if residue == 0:
            totals.add("chunks_with_zero_residue")

        for name, count in walk.command_counts.items():
            totals.add(f"chunk_command_{name}", count)

        shape = tuple((components, bits) for components, bits, _n, _o, _i in walk.blocks)
        if shape != CANONICAL_CHUNK_SHAPE:
            totals.add("chunks_non_canonical_shape")
            if chunk_index == last_index:
                totals.add("chunks_non_canonical_shape_at_vum_tail")
            continue
        totals.add("chunks_canonical_shape")

        header, position_block, color_a, color_b, uv_block = walk.blocks
        vertex_count = position_block[2]
        budget.add_vertices(vertex_count)
        totals.add("chunked_model_vertices", vertex_count)
        totals.add("chunked_model_triangles_single_strip", _single_strip_triangles(vertex_count))

        # H4 falsifier: a per-vertex flag needs at least one carrier word per
        # vertex.  These carriers are emitted a fixed number of times per chunk,
        # before the attribute blocks, so any chunk with more vertices than
        # carrier words falsifies the per-vertex reading.
        stmask_words = walk.command_counts.get("stmask", 0)
        strow_words = walk.command_counts.get("strow_stcol", 0)
        totals.add("chunk_stmask_words", stmask_words)
        totals.add("chunk_strow_quadwords", strow_words)
        if stmask_words < vertex_count:
            totals.add("chunks_where_stmask_count_below_vertex_count")
        if strow_words < vertex_count:
            totals.add("chunks_where_strow_count_below_vertex_count")

        # H5 falsifier: the microprogram-select family must actually occur.
        totals.add("chunk_microprogram_commands", walk.command_counts.get("microprogram_family", 0))

        # H3 falsifier: the position UNPACK declares its own component count.
        totals.add("position_unpacks_declaring_three_components")
        if all(block[2] == vertex_count for block in (color_a, color_b, uv_block)):
            totals.add("chunks_attribute_counts_agree_with_positions")
        # H3 falsifier: re-walk this chunk consuming the position UNPACK at four
        # 16-bit components.  Only a stride that keeps the walk byte-synced can
        # still reach the chunk boundary with the same canonical block shape.
        totals.add("chunks_where_v3_16_preserves_tiling")
        widened_walk = _walk_vif(
            view, start + 4, next_start, budget, stop_at_tag=True,
            widen_three_component_16=True)
        widened_shape = tuple(
            (components, bits) for components, bits, _n, _o, _i in widened_walk.blocks)
        if widened_walk.stop in ("end", "boundary") and widened_shape == CANONICAL_CHUNK_SHAPE:
            totals.add("chunks_where_v4_16_preserves_tiling")

        # Degeneracy of the naive per-chunk strip.
        degenerate = _degenerate_strip_triangles(view, position_block[3], vertex_count)
        totals.add("chunked_model_degenerate_triangles", degenerate)
        if degenerate == 0:
            totals.add("chunks_with_no_degenerate_strip_triangle")

        # --- H2: header-block topology slots -----------------------------------
        if header[2] != HEADER_QUADWORDS:
            totals.add("chunks_header_block_unexpected_length")
            continue
        totals.add("chunks_header_block_tested")
        words = struct.unpack_from("<24I", view, header[3])
        slots = words[ANCHOR_WORDS:ANCHOR_WORDS + SLOT_WORDS]
        marker = words[MARKER_WORD_INDEX]
        if marker == (MARKER_HIGH_BIT | vertex_count):
            totals.add("chunks_marker_word_encodes_vertex_count")
        if all(slot % 3 == SLOT_SENTINEL % 3 for slot in slots):
            totals.add("chunks_slots_all_two_mod_three")
        flat_index_bound = 3 * max(vertex_count - 2, 1) - 1
        if all(slot <= flat_index_bound for slot in slots):
            totals.add("chunks_slots_within_flat_index_bound")
        active = [slot for slot in slots if slot != SLOT_SENTINEL]
        if all(left < right for left, right in zip(active, active[1:])):
            totals.add("chunks_active_prefix_strictly_increasing")
        if slots[-1] == SLOT_SENTINEL:
            totals.add("chunks_terminal_slot_is_sentinel")
        if not active:
            totals.add("chunks_with_zero_active_slots")
            continue

        # --- H2b: are the slot groups independent strips? ----------------------
        totals.add("chunks_slot_group_model_tested")
        previous = -1
        group_vertex_demand = 0
        group_triangles = 0
        for slot in active:
            triangles_in_group = (slot - previous) // 3
            group_triangles += triangles_in_group
            group_vertex_demand += triangles_in_group + 2
            previous = slot
        totals.add("slot_model_triangles", group_triangles)
        if group_vertex_demand == vertex_count:
            totals.add("chunks_slot_groups_consume_exact_vertex_count")
        if group_triangles <= _single_strip_triangles(vertex_count):
            totals.add("chunks_slot_triangles_within_single_strip_total")

    totals.add("chunked_model_bytes_consumed", chunked_bytes)


# ---------------------------------------------------------------------------
# Scan driver and fixed-schema document
# ---------------------------------------------------------------------------


def scan_corpus(root: Path, limits: ScanLimits = ScanLimits()) -> dict[str, object]:
    _require_directory(root)
    budget = ScanBudget(limits)
    totals = Totals()
    archives = _discover_level_archives(root.absolute(), budget)
    totals.add("level_archives_discovered", len(archives))

    for archive in archives:
        try:
            info = archive.lstat()
            if stat.S_ISLNK(info.st_mode) or _stat_is_reparse(info):
                raise ScanFailure("unsafe_input")
            budget.add_container_bytes(info.st_size)
            with archive.open("rb") as stream:
                spans = _collect_vum_spans(stream, 0, info.st_size, 0, budget)
                totals.add("level_archives_scanned")
                for offset, size in spans:
                    budget.add_vum(size)
                    totals.add("vum_occurrences")
                    stream.seek(offset)
                    data = stream.read(size)
                    if len(data) != size:
                        totals.add("vum_short_read")
                        continue
                    _score_vum(data, totals, budget)
        except ScanFailure:
            raise
        except (OSError, ValueError, struct.error):
            totals.add("level_archive_errors")
    return _document(totals)


def _document(totals: Totals) -> dict[str, object]:
    totals.ensure(
        "chunk_starts",
        "chunk_starts_that_are_proven_references",
        "chunks_active_prefix_strictly_increasing",
        "chunks_attribute_counts_agree_with_positions",
        "chunks_budget_stop",
        "chunks_canonical_shape",
        "chunks_header_block_tested",
        "chunks_header_block_unexpected_length",
        "chunks_marker_word_encodes_vertex_count",
        "chunks_non_canonical_shape",
        "chunks_non_canonical_shape_at_vum_tail",
        "chunks_parsed_to_boundary",
        "chunks_slot_group_model_tested",
        "chunks_slot_groups_consume_exact_vertex_count",
        "chunks_slot_triangles_within_single_strip_total",
        "chunks_slots_all_two_mod_three",
        "chunks_slots_within_flat_index_bound",
        "chunks_terminal_slot_is_sentinel",
        "chunks_truncated_block",
        "chunks_unknown_command_abort",
        "chunks_where_v3_16_preserves_tiling",
        "chunks_where_v4_16_preserves_tiling",
        "chunk_stmask_words",
        "chunk_strow_quadwords",
        "chunks_with_no_degenerate_strip_triangle",
        "chunks_where_stmask_count_below_vertex_count",
        "chunks_where_strow_count_below_vertex_count",
        "chunks_with_zero_active_slots",
        "chunks_with_zero_residue",
        "chunk_microprogram_commands",
        "chunk_residue_bytes",
        "chunked_model_bytes_consumed",
        "chunked_model_degenerate_triangles",
        "chunked_model_triangles_single_strip",
        "chunked_model_vertices",
        "current_model_bytes_consumed",
        "current_model_triangles",
        "current_model_vertices",
        "final_payload_bytes",
        "level_archive_errors",
        "level_archives_discovered",
        "level_archives_scanned",
        "position_unpacks_declaring_three_components",
        "proven_reference_offsets",
        "slot_model_triangles",
        "vum_layout_accepted",
        "vum_layout_rejected",
        "vum_occurrences",
        "vum_short_read",
        "vum_without_chunk_start",
    )
    counts = totals.document()
    chunks = counts["chunk_starts"]
    canonical = counts["chunks_canonical_shape"]
    header_tested = counts["chunks_header_block_tested"]
    slot_tested = counts["chunks_slot_group_model_tested"]

    h1_checks = {
        "chunk_starts": chunks,
        "chunk_starts_corroborated_by_proven_reference": counts[
            "chunk_starts_that_are_proven_references"],
        "chunk_starts_uncorroborated": chunks - counts["chunk_starts_that_are_proven_references"],
        "chunks_parsed_to_boundary": counts["chunks_parsed_to_boundary"],
        "chunks_unknown_command_abort": counts["chunks_unknown_command_abort"],
        "chunks_truncated_block": counts["chunks_truncated_block"],
        "chunks_with_zero_residue": counts["chunks_with_zero_residue"],
        "residue_bytes": counts["chunk_residue_bytes"],
        "final_payload_bytes": counts["final_payload_bytes"],
        "current_model_bytes_consumed": counts["current_model_bytes_consumed"],
        "chunked_model_bytes_consumed": counts["chunked_model_bytes_consumed"],
        "current_model_coverage_ratio": _ratio(
            counts["current_model_bytes_consumed"], counts["final_payload_bytes"]),
        "chunked_model_coverage_ratio": _ratio(
            counts["chunked_model_bytes_consumed"], counts["final_payload_bytes"]),
        "current_model_vertices": counts["current_model_vertices"],
        "chunked_model_vertices": counts["chunked_model_vertices"],
        "vertex_gain_ratio": _ratio(
            counts["chunked_model_vertices"], max(counts["current_model_vertices"], 1)),
    }
    h1_passed = (
        chunks > 0
        and h1_checks["chunk_starts_uncorroborated"] == 0
        and counts["chunks_unknown_command_abort"] == 0
        and counts["chunks_truncated_block"] == 0
        and counts["chunks_parsed_to_boundary"] == chunks
        and counts["chunked_model_bytes_consumed"] > counts["current_model_bytes_consumed"]
        and counts["chunked_model_vertices"] > counts["current_model_vertices"]
    )

    return {
        "schema": "vum_strip_topology_hypotheses",
        "schema_version": 1,
        "population": {
            "level_archives_discovered": counts["level_archives_discovered"],
            "level_archives_scanned": counts["level_archives_scanned"],
            "vum_occurrences": counts["vum_occurrences"],
            "vum_layout_accepted": counts["vum_layout_accepted"],
            "vum_layout_rejected": counts["vum_layout_rejected"],
            "vum_without_chunk_start": counts["vum_without_chunk_start"],
            "final_payload_bytes": counts["final_payload_bytes"],
            "proven_reference_offsets": counts["proven_reference_offsets"],
        },
        "current_single_stream_model": {
            "vertices": counts["current_model_vertices"],
            "triangles": counts["current_model_triangles"],
            "bytes_consumed": counts["current_model_bytes_consumed"],
            "coverage_ratio": h1_checks["current_model_coverage_ratio"],
            "walk_stop_end": counts.get("current_model_walk_stop_end", 0),
            "walk_stop_unknown_command": counts.get("current_model_walk_stop_unknown_command", 0),
            "walk_stop_truncated": counts.get("current_model_walk_stop_truncated", 0),
            "walk_stop_budget": counts.get("current_model_walk_stop_budget", 0),
        },
        "hypotheses": {
            "h1_chunked_container": {
                "verdict": "supported" if h1_passed else "rejected",
                "checks": h1_checks,
            },
            "h2_header_block_topology_slots": {
                "verdict": _verdict(
                    header_tested * 5,
                    counts["chunks_marker_word_encodes_vertex_count"]
                    + counts["chunks_slots_all_two_mod_three"]
                    + counts["chunks_slots_within_flat_index_bound"]
                    + counts["chunks_active_prefix_strictly_increasing"]
                    + counts["chunks_terminal_slot_is_sentinel"]),
                "scope": "structural_only",
                "checks": {
                    "chunks_tested": header_tested,
                    "marker_word_encodes_vertex_count": counts[
                        "chunks_marker_word_encodes_vertex_count"],
                    "slots_all_two_mod_three": counts["chunks_slots_all_two_mod_three"],
                    "slots_within_flat_index_bound": counts["chunks_slots_within_flat_index_bound"],
                    "active_prefix_strictly_increasing": counts[
                        "chunks_active_prefix_strictly_increasing"],
                    "terminal_slot_is_sentinel": counts["chunks_terminal_slot_is_sentinel"],
                    "chunks_with_zero_active_slots": counts["chunks_with_zero_active_slots"],
                    "chunks_header_block_unexpected_length": counts[
                        "chunks_header_block_unexpected_length"],
                },
            },
            "h2b_slot_groups_are_independent_strips": {
                "verdict": _verdict(
                    slot_tested, counts["chunks_slot_groups_consume_exact_vertex_count"]),
                "scope": "semantic_gate_for_h2",
                "checks": {
                    "chunks_tested": slot_tested,
                    "groups_consume_exact_vertex_count": counts[
                        "chunks_slot_groups_consume_exact_vertex_count"],
                    "agreement_ratio": _ratio(
                        counts["chunks_slot_groups_consume_exact_vertex_count"], slot_tested),
                    "slot_triangles_within_single_strip_total": counts[
                        "chunks_slot_triangles_within_single_strip_total"],
                    "slot_model_triangles": counts["slot_model_triangles"],
                    "single_strip_triangles": counts["chunked_model_triangles_single_strip"],
                },
            },
            "h3_v4_16_positions": {
                "verdict": _falsifier_verdict(
                    counts["position_unpacks_declaring_three_components"],
                    counts["position_unpacks_declaring_three_components"]
                    - counts["chunks_where_v4_16_preserves_tiling"]),
                "scope": "falsifier",
                "checks": {
                    "position_unpacks_tested": counts["position_unpacks_declaring_three_components"],
                    "position_unpacks_declaring_three_components": counts[
                        "position_unpacks_declaring_three_components"],
                    "declared_v3_16_stride_preserves_chunk_tiling": counts[
                        "chunks_where_v3_16_preserves_tiling"],
                    "reinterpretation_preserves_chunk_tiling": counts[
                        "chunks_where_v4_16_preserves_tiling"],
                    "attribute_counts_agree_with_positions": counts[
                        "chunks_attribute_counts_agree_with_positions"],
                },
            },
            "h4_stmask_strow_per_vertex_flag": {
                "verdict": _falsifier_verdict(
                    canonical, counts["chunks_where_stmask_count_below_vertex_count"]),
                "scope": "falsifier",
                "checks": {
                    "chunks_tested": canonical,
                    "stmask_words_total": counts["chunk_stmask_words"],
                    "strow_quadwords_total": counts["chunk_strow_quadwords"],
                    "chunks_where_stmask_count_below_vertex_count": counts[
                        "chunks_where_stmask_count_below_vertex_count"],
                    "chunks_where_strow_count_below_vertex_count": counts[
                        "chunks_where_strow_count_below_vertex_count"],
                    "vertices_covered": counts["chunked_model_vertices"],
                },
            },
            "h5_mscal_immediate_selects_format": {
                # The hypothesis requires the field to exist.  Zero occurrences of
                # the microprogram-call family across every chunk falsifies it.
                "verdict": _falsifier_verdict(
                    canonical, canonical if counts["chunk_microprogram_commands"] == 0 else 0),
                "scope": "falsifier",
                "checks": {
                    "chunks_tested": canonical,
                    "microprogram_family_commands_observed": counts["chunk_microprogram_commands"],
                },
            },
        },
        "non_degenerate_and_finite": {
            "chunks_canonical_shape": canonical,
            "chunks_non_canonical_shape": counts["chunks_non_canonical_shape"],
            "chunks_non_canonical_shape_at_vum_tail": counts[
                "chunks_non_canonical_shape_at_vum_tail"],
            "chunks_with_no_degenerate_strip_triangle": counts[
                "chunks_with_no_degenerate_strip_triangle"],
            "degenerate_triangles_under_per_chunk_single_strip": counts[
                "chunked_model_degenerate_triangles"],
            "triangles_under_per_chunk_single_strip": counts[
                "chunked_model_triangles_single_strip"],
            "degenerate_ratio": _ratio(
                counts["chunked_model_degenerate_triangles"],
                counts["chunked_model_triangles_single_strip"]),
        },
        "errors": {
            "level_archive_errors": counts["level_archive_errors"],
            "vum_layout_rejected": counts["vum_layout_rejected"],
            "vum_short_read": counts["vum_short_read"],
            "chunks_budget_stop": counts["chunks_budget_stop"],
        },
    }


def failure_document(category: str) -> dict[str, object]:
    return {
        "schema": "vum_strip_topology_hypotheses",
        "schema_version": 1,
        "population": {},
        "current_single_stream_model": {},
        "hypotheses": {},
        "non_degenerate_and_finite": {},
        "errors": {category: 1},
    }


def main(argv: Sequence[str] | None = None) -> int:
    parser = AggregateArgumentParser(add_help=False)
    parser.add_argument("corpus_root", type=Path)
    try:
        args = parser.parse_args(argv)
    except AggregateArgumentError:
        print(json.dumps(failure_document("config"), separators=(",", ":"), sort_keys=True))
        return 1
    try:
        result = scan_corpus(args.corpus_root)
    except ScanFailure as failure:
        result = failure_document(failure.category)
    except (OSError, ValueError, struct.error):
        result = failure_document("io")
    print(json.dumps(result, separators=(",", ":"), sort_keys=True))
    return 1 if any(result["errors"].values()) else 0


if __name__ == "__main__":
    sys.exit(main())
