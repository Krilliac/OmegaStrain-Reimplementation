#!/usr/bin/env python3
"""Assemble a bounded private scene fragment into a sanitized scene observation.

The input format is the fixed-width research fragment emitted by the separately
maintained PCSX2 observer.  Input bytes and all source identities remain private.
The only accepted public output is a canonical renderer-neutral JSON value with
anonymous transform components and no source provenance. The current evidence
does not establish a camera transform direction or multiplication convention.

The current producer contains draw-level counts and extrema, not vertex positions
or index connectivity.  Consequently this version intentionally emits no render
meshes, instances, or material buckets.  It never invents geometry from bounds.
"""

from __future__ import annotations

import hashlib
import json
import math
import struct
import sys
from dataclasses import dataclass
from typing import Final, TextIO, cast
from collections.abc import Sequence


FRAGMENT_MAGIC: Final = b"OMEGASCNPART0001"
FRAGMENT_VERSION: Final = 1
SCENE_OBSERVATION_SCHEMA: Final = "openomega-sanitized-scene-observation-v1"

MAX_FRAGMENT_BYTES: Final = 16 * 1024 * 1024
MAX_FRAGMENTS: Final = 8
MAX_FRAMES: Final = 600
MAX_SPANS: Final = 16
MAX_EE_SITES: Final = 512
MAX_RECORDS: Final = 16_384
MAX_CAMERA_COMPONENTS: Final = 64
MAX_VIF_DESTINATION_EXTENTS: Final = 32_768
MAX_INL_ROWS: Final = 32_768
MAX_VIF_ROWS: Final = 32_768
MAX_CAMERA_SAMPLES: Final = MAX_FRAMES * MAX_CAMERA_COMPONENTS
MAX_GS_STATE_KEYS: Final = 4_096
MAX_DRAWS: Final = 32_768
MAX_EDGES: Final = 131_072

_UINT32_MAX: Final = (1 << 32) - 1
_UINT64_MAX: Final = (1 << 64) - 1
_INT32_MIN: Final = -(1 << 31)
_INT32_MAX: Final = (1 << 31) - 1

_COUNT_NAMES: Final = (
    "frames",
    "spans",
    "ee_sites",
    "records",
    "camera_components",
    "vif_destination_extents",
    "inl_rows",
    "vif_rows",
    "camera_samples",
    "gs_state_keys",
    "draws",
    "edges",
)
_COUNT_LIMITS: Final = (
    MAX_FRAMES,
    MAX_SPANS,
    MAX_EE_SITES,
    MAX_RECORDS,
    MAX_CAMERA_COMPONENTS,
    MAX_VIF_DESTINATION_EXTENTS,
    MAX_INL_ROWS,
    MAX_VIF_ROWS,
    MAX_CAMERA_SAMPLES,
    MAX_GS_STATE_KEYS,
    MAX_DRAWS,
    MAX_EDGES,
)
_ROW_WIDTHS: Final = (
    4,    # frame descriptor
    4,    # span descriptor
    12,   # EE site descriptor
    12,   # record descriptor
    4,    # camera component descriptor
    4,    # VIF destination extent descriptor
    28,   # INL observation
    50,   # VIF metadata observation
    12,   # camera sample
    4,    # GS state descriptor
    188,  # draw summary
    8,    # membership edge
)
_HEADER_BYTES: Final = len(FRAGMENT_MAGIC) + 4 + 32 + len(_COUNT_NAMES) * 4
_PRIMITIVE_ARITIES: Final = (1, 2, 2, 3, 3, 3, 2)
_SUPPORTED_ACCESS_WIDTHS: Final = frozenset((1, 2, 4, 8, 16))
_SUPPORTED_VIF_COMPONENT_WIDTHS: Final = frozenset((5, 8, 16, 32))
_SCENE_KEYS: Final = (
    "schema",
    "camera",
    "material_buckets",
    "render_meshes",
    "mesh_instances",
)
_ANONYMOUS_CAMERA_KEYS: Final = ("anonymous_components",)

_OptionalRange = tuple[bool, float, float]
_DerivedExtrema = tuple[
    int,
    _OptionalRange,
    _OptionalRange,
    _OptionalRange,
    _OptionalRange,
    _OptionalRange,
    _OptionalRange,
]


class SceneFragmentValidationError(ValueError):
    """The input is not an accepted bounded scene fragment."""


class SceneAssemblyError(ValueError):
    """Validated fragments cannot be assembled without unsupported inference."""


def _fragment_fail(message: str) -> SceneFragmentValidationError:
    return SceneFragmentValidationError(message)


def _assembly_fail(message: str) -> SceneAssemblyError:
    return SceneAssemblyError(message)


@dataclass(frozen=True)
class ParsedSceneFragment:
    """Minimal post-validation state; private provenance tables are discarded."""

    runtime_config_digest: bytes
    camera_component_count: int
    complete_camera_frames: tuple[tuple[int, tuple[float, ...]], ...]


class _Reader:
    def __init__(self, raw: bytes) -> None:
        self._raw = raw
        self.offset = 0

    def bytes(self, size: int) -> bytes:
        if size < 0 or self.offset > len(self._raw) - size:
            raise _fragment_fail("scene fragment is truncated")
        begin = self.offset
        self.offset += size
        return self._raw[begin : self.offset]

    def u8(self) -> int:
        return self.bytes(1)[0]

    def u32(self) -> int:
        return int.from_bytes(self.bytes(4), "little", signed=False)

    def u64(self) -> int:
        return int.from_bytes(self.bytes(8), "little", signed=False)

    def f32(self) -> float:
        bits = self.u32()
        if bits == 0x80000000:
            raise _fragment_fail("camera sample contains noncanonical negative zero")
        value = struct.unpack("<f", bits.to_bytes(4, "little"))[0]
        if not math.isfinite(value):
            raise _fragment_fail("camera sample is not finite")
        return value

    def f64_bits(self) -> tuple[int, float]:
        bits = self.u64()
        value = struct.unpack("<d", bits.to_bytes(8, "little"))[0]
        return bits, value


def _require_dense_id(actual: int, index: int) -> None:
    if actual != index + 1:
        raise _fragment_fail("descriptor IDs are not dense one-based ordinals")


def _require_id(value: int, count: int) -> None:
    if value == 0 or value > count:
        raise _fragment_fail("scene fragment contains an invalid table reference")


def _require_bool(value: int) -> bool:
    if value not in (0, 1):
        raise _fragment_fail("scene fragment contains a noncanonical boolean")
    return value == 1


def _read_range(reader: _Reader) -> _OptionalRange:
    present = _require_bool(reader.u8())
    minimum_bits, minimum = reader.f64_bits()
    maximum_bits, maximum = reader.f64_bits()
    if not present:
        if minimum_bits != 0 or maximum_bits != 0:
            raise _fragment_fail("an absent derived range contains retained values")
        return False, 0.0, 0.0
    if not math.isfinite(minimum) or not math.isfinite(maximum):
        raise _fragment_fail("a derived range is not finite")
    if minimum_bits == 0x8000000000000000 or maximum_bits == 0x8000000000000000:
        raise _fragment_fail("a derived range contains noncanonical negative zero")
    if minimum > maximum:
        raise _fragment_fail("a derived range is reversed")
    return True, minimum, maximum


def _read_extrema(
    reader: _Reader,
) -> _DerivedExtrema:
    texture_kind = reader.u8()
    if texture_kind > 2:
        raise _fragment_fail("draw summary contains an unsupported texture-coordinate class")
    x_range = _read_range(reader)
    y_range = _read_range(reader)
    depth_range = _read_range(reader)
    first_range = _read_range(reader)
    second_range = _read_range(reader)
    q_range = _read_range(reader)
    texture_presence = (first_range[0], second_range[0], q_range[0])
    expected_presence = {
        0: (False, False, False),
        1: (True, True, True),
        2: (True, True, False),
    }[texture_kind]
    if texture_presence != expected_presence:
        raise _fragment_fail("draw texture-coordinate ranges contradict their class")
    return (
        texture_kind,
        x_range,
        y_range,
        depth_range,
        first_range,
        second_range,
        q_range,
    )


def _is_exact_f32(value: float) -> bool:
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0] == value
    except (OverflowError, struct.error):
        return False


def _validate_service_extrema(
    extrema: _DerivedExtrema,
) -> None:
    texture_kind, x_range, y_range, depth_range, first_range, second_range, q_range = extrema
    if not x_range[0] or not y_range[0] or not depth_range[0]:
        raise _fragment_fail("draw summary is missing required position extrema")
    for coordinate_range in (x_range, y_range):
        if any(
            value < _INT32_MIN or value > _INT32_MAX or not value.is_integer()
            for value in coordinate_range[1:]
        ):
            raise _fragment_fail("draw position extrema exceed the producer domain")
    if any(
        value < 0 or value > _UINT32_MAX or not value.is_integer()
        for value in depth_range[1:]
    ):
        raise _fragment_fail("draw depth extrema exceed the producer domain")
    if texture_kind != 0:
        for texture_range in (first_range, second_range, q_range):
            if texture_range[0] and not all(_is_exact_f32(value) for value in texture_range[1:]):
                raise _fragment_fail("draw texture extrema are not exact producer floats")


def _validate_count_table(counts: tuple[int, ...], raw_size: int) -> None:
    for count, limit in zip(counts, _COUNT_LIMITS, strict=True):
        if count > limit:
            raise _fragment_fail("a scene fragment table exceeds its fixed capacity")
    frames, spans, sites = counts[:3]
    if frames == 0 or spans == 0 or sites == 0:
        raise _fragment_fail("required scene fragment descriptor tables are empty")
    camera_components = counts[4]
    vif_destination_extents = counts[5]
    vif_rows = counts[7]
    camera_samples = counts[8]
    if camera_samples > frames * camera_components:
        raise _fragment_fail("camera sample count exceeds the descriptor product")
    if vif_destination_extents > vif_rows:
        raise _fragment_fail("VIF destination extent count exceeds observation count")

    expected_size = _HEADER_BYTES
    for count, width in zip(counts, _ROW_WIDTHS, strict=True):
        expected_size += count * width
    if expected_size > MAX_FRAGMENT_BYTES or expected_size != raw_size:
        raise _fragment_fail("scene fragment length does not match its table counts")


def parse_scene_fragment(raw: bytes) -> ParsedSceneFragment:
    """Strictly validate one producer fragment and discard provenance tables."""

    if type(raw) is not bytes:
        raise _fragment_fail("scene fragment input must be immutable bytes")
    if len(raw) > MAX_FRAGMENT_BYTES:
        raise _fragment_fail("scene fragment exceeds the maximum byte size")
    if len(raw) < _HEADER_BYTES:
        raise _fragment_fail("scene fragment is truncated")

    reader = _Reader(raw)
    if reader.bytes(len(FRAGMENT_MAGIC)) != FRAGMENT_MAGIC:
        raise _fragment_fail("scene fragment magic is unsupported")
    if reader.u32() != FRAGMENT_VERSION:
        raise _fragment_fail("scene fragment version is unsupported")
    runtime_config_digest = reader.bytes(32)
    if not any(runtime_config_digest):
        raise _fragment_fail("scene fragment configuration digest is invalid")

    counts = tuple(reader.u32() for _ in _COUNT_NAMES)
    _validate_count_table(counts, len(raw))
    (
        frame_count,
        span_count,
        site_count,
        record_count,
        camera_component_count,
        vif_destination_count,
        inl_count,
        vif_count,
        camera_sample_count,
        gs_state_count,
        draw_count,
        edge_count,
    ) = counts

    for index in range(frame_count):
        _require_dense_id(reader.u32(), index)
    for index in range(span_count):
        _require_dense_id(reader.u32(), index)

    sites: list[tuple[int, int]] = []
    for index in range(site_count):
        _require_dense_id(reader.u32(), index)
        span = reader.u32()
        width = reader.u32()
        _require_id(span, span_count)
        if width not in _SUPPORTED_ACCESS_WIDTHS:
            raise _fragment_fail("EE site descriptor has an unsupported access width")
        sites.append((span, width))

    records: list[tuple[int, int]] = []
    for index in range(record_count):
        _require_dense_id(reader.u32(), index)
        span = reader.u32()
        byte_size = reader.u32()
        _require_id(span, span_count)
        if byte_size == 0:
            raise _fragment_fail("record descriptor has zero size")
        records.append((span, byte_size))

    for index in range(camera_component_count):
        _require_dense_id(reader.u32(), index)
    for index in range(vif_destination_count):
        _require_dense_id(reader.u32(), index)

    seen_inl: set[tuple[int, int, int, int]] = set()
    for _ in range(inl_count):
        frame = reader.u32()
        site = reader.u32()
        record = reader.u32()
        record_byte_offset = reader.u32()
        access_width = reader.u32()
        occurrences = reader.u64()
        _require_id(frame, frame_count)
        _require_id(site, site_count)
        _require_id(record, record_count)
        site_span, site_width = sites[site - 1]
        record_span, record_size = records[record - 1]
        if site_span != record_span or access_width != site_width:
            raise _fragment_fail("INL observation contradicts its descriptors")
        if record_byte_offset > record_size or access_width > record_size - record_byte_offset:
            raise _fragment_fail("INL observation exceeds its anonymous record")
        if occurrences == 0:
            raise _fragment_fail("INL observation has zero occurrences")
        key = (frame, site, record, record_byte_offset)
        if key in seen_inl:
            raise _fragment_fail("INL observation table contains duplicate rows")
        seen_inl.add(key)

    seen_vif: set[tuple[int, ...]] = set()
    used_vif_destinations: set[int] = set()
    for _ in range(vif_count):
        frame = reader.u32()
        record = reader.u32()
        source_relative_byte_offset = reader.u32()
        source_word_count = reader.u32()
        remaining_output_elements = reader.u32()
        buffered_prefix_bytes = reader.u32()
        destination_extent = reader.u32()
        mode = reader.u8()
        cycle_cl = reader.u32()
        cycle_wl = reader.u32()
        cycle_class = reader.u8()
        components_per_element = reader.u8()
        bits_per_component = reader.u8()
        masked = reader.u8()
        unsigned_values = reader.u8()
        occurrences = reader.u64()

        _require_id(frame, frame_count)
        _require_id(record, record_count)
        _require_id(destination_extent, vif_destination_count)
        if source_word_count == 0 or remaining_output_elements == 0:
            raise _fragment_fail("VIF observation contains a zero required count")
        source_byte_count = source_word_count * 4 + buffered_prefix_bytes
        if source_byte_count > _UINT32_MAX or source_relative_byte_offset > _UINT32_MAX - source_byte_count:
            raise _fragment_fail("VIF source extent exceeds uint32")
        _, record_size = records[record - 1]
        if (
            source_relative_byte_offset > record_size
            or source_byte_count > record_size - source_relative_byte_offset
        ):
            raise _fragment_fail("VIF observation exceeds its anonymous record")
        if buffered_prefix_bytes > 15 or mode > 2:
            raise _fragment_fail("VIF observation contains unsupported metadata")
        if cycle_cl > 256 or cycle_wl > 256 or cycle_class > 2:
            raise _fragment_fail("VIF observation contains unsupported cycle metadata")
        if cycle_class == 0 and cycle_wl < cycle_cl:
            raise _fragment_fail("VIF cycle class contradicts its raw cycle values")
        if cycle_class == 2 and cycle_wl >= cycle_cl:
            raise _fragment_fail("VIF cycle class contradicts its raw cycle values")
        if components_per_element == 0 or components_per_element > 4:
            raise _fragment_fail("VIF observation has an unsupported component count")
        if bits_per_component not in _SUPPORTED_VIF_COMPONENT_WIDTHS:
            raise _fragment_fail("VIF observation has an unsupported component width")
        if bits_per_component == 5 and components_per_element != 4:
            raise _fragment_fail("VIF packed component metadata is inconsistent")
        _require_bool(masked)
        _require_bool(unsigned_values)
        if occurrences == 0:
            raise _fragment_fail("VIF observation has zero occurrences")

        key = (
            frame,
            record,
            destination_extent,
            source_relative_byte_offset,
            source_word_count,
            remaining_output_elements,
            buffered_prefix_bytes,
            mode,
            cycle_cl,
            cycle_wl,
            cycle_class,
            components_per_element,
            bits_per_component,
            masked,
            unsigned_values,
        )
        if key in seen_vif:
            raise _fragment_fail("VIF observation table contains duplicate rows")
        seen_vif.add(key)
        used_vif_destinations.add(destination_extent)
    if used_vif_destinations != set(range(1, vif_destination_count + 1)):
        raise _fragment_fail("VIF destination descriptors are not all observed")

    camera_values: dict[int, dict[int, float]] = {}
    for _ in range(camera_sample_count):
        frame = reader.u32()
        component = reader.u32()
        value = reader.f32()
        _require_id(frame, frame_count)
        _require_id(component, camera_component_count)
        frame_values = camera_values.setdefault(frame, {})
        if component in frame_values:
            raise _fragment_fail("camera sample table contains duplicate rows")
        frame_values[component] = value

    for index in range(gs_state_count):
        _require_dense_id(reader.u32(), index)

    used_states: set[int] = set()
    for index in range(draw_count):
        draw_id = reader.u32()
        frame = reader.u32()
        state = reader.u32()
        disposition = reader.u8()
        index_count = reader.u64()
        unique_vertex_count = reader.u64()
        primitive_counts = tuple(reader.u64() for _ in _PRIMITIVE_ARITIES)
        extrema = _read_extrema(reader)

        _require_dense_id(draw_id, index)
        _require_id(frame, frame_count)
        _require_id(state, gs_state_count)
        if disposition not in (0, 1):
            raise _fragment_fail("draw summary has an unsupported disposition")
        if index_count == 0 or unique_vertex_count == 0 or unique_vertex_count > index_count:
            raise _fragment_fail("draw summary has inconsistent index counts")
        derived_index_count = sum(
            count * arity
            for count, arity in zip(primitive_counts, _PRIMITIVE_ARITIES, strict=True)
        )
        if derived_index_count > _UINT64_MAX or derived_index_count != index_count:
            raise _fragment_fail("draw primitive counts do not match its index count")
        _validate_service_extrema(extrema)
        used_states.add(state)
    if used_states != set(range(1, gs_state_count + 1)):
        raise _fragment_fail("GS state descriptors are not all referenced")

    seen_edges: set[tuple[int, int]] = set()
    prior_draw = 0
    for _ in range(edge_count):
        record = reader.u32()
        draw = reader.u32()
        _require_id(record, record_count)
        _require_id(draw, draw_count)
        if draw < prior_draw:
            raise _fragment_fail("membership edges are not in producer draw order")
        prior_draw = draw
        edge = (record, draw)
        if edge in seen_edges:
            raise _fragment_fail("membership edge table contains duplicate rows")
        seen_edges.add(edge)

    if reader.offset != len(raw):
        raise _fragment_fail("scene fragment contains trailing bytes")

    complete_frames: list[tuple[int, tuple[float, ...]]] = []
    expected_components = set(range(1, camera_component_count + 1))
    for frame in sorted(camera_values):
        values = camera_values[frame]
        if set(values) == expected_components:
            complete_frames.append(
                (frame, tuple(values[component] for component in range(1, camera_component_count + 1)))
            )
    return ParsedSceneFragment(
        runtime_config_digest=runtime_config_digest,
        camera_component_count=camera_component_count,
        complete_camera_frames=tuple(complete_frames),
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


def assemble_scene_document(
    raw_fragments: Sequence[bytes],
    *,
    expected_fragment_sha256: str | None = None,
) -> dict[str, object]:
    """Validate repeat fragments and return anonymous sanitized observations."""

    if not isinstance(raw_fragments, Sequence) or isinstance(raw_fragments, (bytes, bytearray, str)):
        raise _assembly_fail("scene fragment collection must be a sequence")
    if not raw_fragments or len(raw_fragments) > MAX_FRAGMENTS:
        raise _assembly_fail("scene fragment count is outside its fixed bound")
    expected_digest = _validate_expected_sha256(expected_fragment_sha256)

    reference_raw: bytes | None = None
    reference_hash: str | None = None
    parsed_fragments: list[ParsedSceneFragment] = []
    for raw in raw_fragments:
        if type(raw) is not bytes:
            raise _assembly_fail("scene fragments must be immutable bytes")
        raw_hash = hashlib.sha256(raw).hexdigest()
        if expected_digest is not None and raw_hash != expected_digest:
            raise _assembly_fail("scene fragment SHA-256 does not match the expected digest")
        if reference_hash is None:
            reference_hash = raw_hash
            reference_raw = raw
        elif raw_hash != reference_hash or raw != reference_raw:
            raise _assembly_fail("repeat scene fragments are not byte-identical")
        parsed_fragments.append(parse_scene_fragment(raw))

    reference = parsed_fragments[0]
    for parsed in parsed_fragments[1:]:
        if parsed.runtime_config_digest != reference.runtime_config_digest:
            raise _assembly_fail("repeat scene fragments use different configuration digests")
    if reference.camera_component_count != 0 and not reference.complete_camera_frames:
        raise _assembly_fail("scene fragment has no complete anonymous camera observation")

    # A capture may observe the same configured camera more than once. The final
    # complete producer frame is selected deterministically; incomplete frames are
    # never padded or inferred. With no components, the anonymous observation is
    # empty rather than an invented identity matrix.
    component_values = (
        reference.complete_camera_frames[-1][1]
        if reference.complete_camera_frames
        else ()
    )
    document: dict[str, object] = {
        "schema": SCENE_OBSERVATION_SCHEMA,
        "camera": {"anonymous_components": list(component_values)},
        "material_buckets": [],
        "render_meshes": [],
        "mesh_instances": [],
    }
    validate_scene_document(document)
    return document


def _require_exact_keys(value: dict[str, object], keys: tuple[str, ...]) -> None:
    if tuple(value) != keys:
        raise _assembly_fail("scene observation keys are missing, extra, or noncanonical")


def validate_scene_document(document: object) -> dict[str, object]:
    """Validate the non-semantic sanitized scene observation."""

    if not isinstance(document, dict):
        raise _assembly_fail("scene observation root must be an object")
    root = cast(dict[str, object], document)
    _require_exact_keys(root, _SCENE_KEYS)
    if root["schema"] != SCENE_OBSERVATION_SCHEMA:
        raise _assembly_fail("scene observation schema is unsupported")
    camera = root["camera"]
    if not isinstance(camera, dict):
        raise _assembly_fail("scene observation camera must be an object")
    camera_object = cast(dict[str, object], camera)
    _require_exact_keys(camera_object, _ANONYMOUS_CAMERA_KEYS)
    values = camera_object["anonymous_components"]
    if not isinstance(values, list):
        raise _assembly_fail("sanitized camera values must be an array")
    if len(values) > MAX_CAMERA_COMPONENTS:
        raise _assembly_fail("sanitized camera component count exceeds its fixed bound")
    for value in values:
        if type(value) is not float or not math.isfinite(value):
            raise _assembly_fail("sanitized camera contains a non-finite value")
        if value == 0.0 and math.copysign(1.0, value) < 0.0:
            raise _assembly_fail("sanitized camera contains negative zero")
    for key in ("material_buckets", "render_meshes", "mesh_instances"):
        if root[key] != []:
            raise _assembly_fail("this observation version cannot contain fabricated geometry")
    return root


def encode_scene_document(document: object) -> bytes:
    """Return the sole canonical byte encoding for a sanitized scene document."""

    validated = validate_scene_document(document)
    return _canonical_json_bytes(validated)


def _read_fragment(path: str) -> bytes:
    with open(path, "rb") as stream:
        raw = stream.read(MAX_FRAGMENT_BYTES + 1)
    if len(raw) > MAX_FRAGMENT_BYTES:
        raise _fragment_fail("scene fragment exceeds the maximum byte size")
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
        raise _assembly_fail("scene fragment count is outside its fixed bound")
    return _CliOptions(output, tuple(fragments), expected_sha256)


def _write_new_file(path: str, payload: bytes) -> None:
    # Leave a short/failed exclusive output in place. Deleting later by pathname
    # would race a replacement in a shared directory and could remove a file this
    # process did not create. A retry must always choose or explicitly clear a path.
    with open(path, "xb") as stream:
        if stream.write(payload) != len(payload):
            raise OSError("short scene document write")
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
            "usage: assemble_scene_fragment.py [--expected-sha256 HEX] "
            "--output OUTPUT FRAGMENT [FRAGMENT ...]",
        )
        return 0

    try:
        options = _parse_cli(args)
        raw_fragments = [_read_fragment(path) for path in options.fragments]
        document = assemble_scene_document(
            raw_fragments,
            expected_fragment_sha256=options.expected_sha256,
        )
        _write_new_file(options.output, encode_scene_document(document))
    except Exception:
        _write_ascii_line(sys.stderr, "Scene fragment assembly failed.")
        return 1

    _write_ascii_line(sys.stdout, "Scene fragment assembly succeeded.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
