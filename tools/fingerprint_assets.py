#!/usr/bin/env python3
"""Fingerprint Omega Strain asset containers without exporting asset payloads.

The output is intentionally aggregate-only: counts, header invariants, and
cross-file relationships.  It contains no extracted textures, audio, meshes,
scripts, or exhaustive asset-name lists.
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, Iterable


@dataclass(frozen=True)
class Span:
    name: str
    offset: int
    size: int


@dataclass(frozen=True)
class HogDirectory:
    entries: tuple[Span, ...]
    logical_size: int
    padding_size: int


class Aggregate:
    def __init__(self) -> None:
        self.values: dict[str, int] = collections.Counter()
        self.counters: dict[str, collections.Counter[object]] = collections.defaultdict(collections.Counter)
        self.ranges: dict[str, list[int]] = {}

    def add(self, key: str, amount: int = 1) -> None:
        self.values[key] += amount

    def count(self, key: str, value: object) -> None:
        self.counters[key][value] += 1

    def observe(self, key: str, value: int) -> None:
        if key not in self.ranges:
            self.ranges[key] = [value, value]
        else:
            self.ranges[key][0] = min(self.ranges[key][0], value)
            self.ranges[key][1] = max(self.ranges[key][1], value)

    def record_span(
        self,
        file: BinaryIO,
        offset: int,
        size: int,
        declared_size: int,
        label: str,
    ) -> None:
        if declared_size > size:
            self.add(f"{label}_exceeds_span")
        elif declared_size == size:
            self.add(f"{label}_exact")
        elif range_is_zero(file, offset + declared_size, size - declared_size):
            self.add(f"{label}_zero_padded")
            self.observe(f"{label}_padding_bytes", size - declared_size)
        else:
            self.add(f"{label}_nonzero_tail")

    def as_dict(self) -> dict[str, object]:
        result: dict[str, object] = dict(sorted(self.values.items()))
        for key, counter in sorted(self.counters.items()):
            result[key] = {str(k): v for k, v in sorted(counter.items(), key=lambda item: str(item[0]))}
        for key, bounds in sorted(self.ranges.items()):
            result[f"{key}_range"] = bounds
        return result


def read_at(file: BinaryIO, offset: int, size: int) -> bytes:
    file.seek(offset)
    data = file.read(size)
    if len(data) != size:
        raise ValueError(f"short read at 0x{offset:X}: wanted {size}, got {len(data)}")
    return data


def range_is_zero(file: BinaryIO, offset: int, size: int) -> bool:
    file.seek(offset)
    remaining = size
    while remaining:
        chunk = file.read(min(65536, remaining))
        if not chunk or any(chunk):
            return False
        remaining -= len(chunk)
    return True


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def parse_hog_span(file: BinaryIO, base: int, span_size: int) -> HogDirectory:
    if span_size < 24:
        raise ValueError("span is too small for a HOG directory")
    header = read_at(file, base, 20)
    _tag, count, offsets_offset, names_offset, data_offset = struct.unpack("<5I", header)
    if count > 1_000_000:
        raise ValueError("implausible HOG entry count")
    if offsets_offset != 0x14:
        raise ValueError("HOG offset table does not start at 0x14")
    if names_offset != offsets_offset + 4 * (count + 1):
        raise ValueError("HOG name-table offset does not follow the offset table")
    if not (names_offset <= data_offset <= span_size):
        raise ValueError("HOG table boundaries exceed the containing span")

    raw_offsets = read_at(file, base + offsets_offset, 4 * (count + 1))
    offsets = struct.unpack(f"<{count + 1}I", raw_offsets)
    if offsets[0] != 0 or any(left > right for left, right in zip(offsets, offsets[1:])):
        raise ValueError("HOG payload offsets are not monotonic from zero")
    logical_size = data_offset + offsets[-1]
    if logical_size > span_size:
        raise ValueError("HOG payload exceeds the containing span")
    padding_size = span_size - logical_size
    if padding_size and not range_is_zero(file, base + logical_size, padding_size):
        raise ValueError("HOG trailing span is not zero padding")

    names_blob = read_at(file, base + names_offset, data_offset - names_offset)
    stripped = names_blob.rstrip(b"\0")
    raw_names = stripped.split(b"\0") if stripped else []
    if len(raw_names) != count:
        raise ValueError(f"HOG expected {count} names, found {len(raw_names)}")
    try:
        names = [name.decode("ascii") for name in raw_names]
    except UnicodeDecodeError as exc:
        raise ValueError("HOG filename is not ASCII") from exc

    entries = tuple(
        Span(
            name=name,
            offset=base + data_offset + offsets[index],
            size=offsets[index + 1] - offsets[index],
        )
        for index, name in enumerate(names)
    )
    return HogDirectory(entries=entries, logical_size=logical_size, padding_size=padding_size)


def compression_magic(data: bytes) -> str | None:
    fixed = (
        (b"\x1f\x8b", "gzip"),
        (b"PK\x03\x04", "zip"),
        (b"BZh", "bzip2"),
        (b"\xfd7zXZ\x00", "xz"),
        (b"7z\xbc\xaf\x27\x1c", "7zip"),
        (b"\x04\x22\x4d\x18", "lz4_frame"),
        (b"\x28\xb5\x2f\xfd", "zstandard"),
        (b"RNC\x01", "rnc1"),
        (b"RNC\x02", "rnc2"),
        (b"LZSS", "lzss"),
        (b"LZ77", "lz77"),
        (b"Yaz0", "yaz0"),
    )
    for prefix, label in fixed:
        if data.startswith(prefix):
            return label
    return None


def fingerprint_tdx(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 64:
        stats.add("short_header")
        return
    header = read_at(file, span.offset, 64)
    version, flags, width, height, bpp, psm, planes, storage_units = struct.unpack_from("<8H", header)
    payload_size = struct.unpack_from("<I", header, 56)[0]
    stats.count("version", version)
    stats.count("flags", flags)
    stats.count("bits_per_pixel", bpp)
    stats.count("ps2_gs_psm", f"0x{psm:02X}")
    stats.count("bpp_psm_pair", f"{bpp}/0x{psm:02X}")
    stats.count("dimensions", f"{width}x{height}")
    stats.count("planes", planes)
    expected_texel_bytes = width * height * bpp // 8
    if expected_texel_bytes == storage_units * 256:
        stats.add("storage_units_match_uncompressed_texel_bytes")
        stats.count("storage_formula_match_by_bpp", bpp)
    else:
        stats.count("storage_formula_mismatch_by_bpp", bpp)
    stats.record_span(file, span.offset, span.size, 64 + payload_size, "header_size_field_span")


def fingerprint_skm(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 4:
        stats.add("short_header")
        return
    prefix = read_at(file, span.offset, min(span.size, 4))
    chunk_count, version = prefix[0], prefix[1]
    stats.count("version_byte", version)
    stats.observe("chunk_count", chunk_count)
    header_size = align_up(2 + 2 * chunk_count, 16)
    if chunk_count == 0 or header_size > span.size:
        stats.add("invalid_chunk_table")
        return
    header = read_at(file, span.offset, header_size)
    qword_counts = [header[2 + 2 * index] for index in range(chunk_count)]
    vertex_counts = [header[3 + 2 * index] for index in range(chunk_count)]
    logical_size = header_size + 16 * sum(qword_counts)
    stats.observe("qwords_per_chunk", min(qword_counts))
    stats.observe("qwords_per_chunk", max(qword_counts))
    stats.observe("secondary_count_per_chunk", min(vertex_counts))
    stats.observe("secondary_count_per_chunk", max(vertex_counts))
    stats.record_span(file, span.offset, span.size, logical_size, "computed_qword_layout_span")


def text_payload(file: BinaryIO, span: Span, maximum: int = 1_048_576) -> tuple[bytes, int] | None:
    if span.size > maximum:
        return None
    raw = read_at(file, span.offset, span.size)
    content = raw.rstrip(b"\0")
    return content, span.size - len(content)


def record_line_endings(text: str, stats: Aggregate) -> None:
    """Classify line endings after separating CRLF from lone CR/LF bytes."""
    without_crlf = text.replace("\r\n", "")
    has_crlf = "\r\n" in text
    has_cr = "\r" in without_crlf
    has_lf = "\n" in without_crlf
    if has_crlf and not has_cr and not has_lf:
        stats.add("crlf_only")
    elif has_cr and not has_crlf and not has_lf:
        stats.add("cr_only")
    elif has_lf and not has_crlf and not has_cr:
        stats.add("lf_only")
    elif has_crlf or has_cr or has_lf:
        stats.add("mixed_line_endings")
    else:
        stats.add("no_line_endings")


def fingerprint_skl(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    payload = text_payload(file, span)
    if payload is None:
        stats.add("too_large_for_text_validation")
        return
    content, padding = payload
    try:
        text = content.decode("ascii")
    except UnicodeDecodeError:
        stats.add("non_ascii")
        return
    stats.add("ascii")
    if text.startswith("BONENOSCALE"):
        stats.add("starts_with_bonenoscale")
    record_line_endings(text, stats)
    stats.observe("line_count", len(text.splitlines()))
    if padding:
        stats.add("zero_padded")
        stats.observe("padding_bytes", padding)
    else:
        stats.add("exact_text_span")


def fingerprint_vag(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 48:
        stats.add("short_header")
        return
    header = read_at(file, span.offset, 48)
    if header[:4] == b"VAGp":
        stats.add("vagp_magic")
    else:
        stats.add("bad_magic")
        return
    version, reserved, data_size, sample_rate = struct.unpack_from(">4I", header, 4)
    stats.count("version", f"0x{version:08X}")
    stats.count("reserved_word", f"0x{reserved:08X}")
    stats.count("sample_rate_hz", sample_rate)
    if data_size % 16 == 0:
        stats.add("adpcm_data_16_byte_aligned")
    stats.record_span(file, span.offset, span.size, 48 + data_size, "declared_data_span")


def fingerprint_lpd(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 4:
        stats.add("short_header")
        return
    word_count = struct.unpack("<I", read_at(file, span.offset, 4))[0]
    stats.count("header_word_count", word_count)
    header_size = word_count * 4
    if word_count == 0 or header_size > span.size or word_count > 4096:
        stats.add("invalid_header_word_count")
        return
    words = struct.unpack(f"<{word_count}I", read_at(file, span.offset, header_size))
    logical_size = header_size + 4 * sum(words[1:])
    stats.record_span(file, span.offset, span.size, logical_size, "computed_count_layout_span")


VERSION_LINE = re.compile(r"^([0-9]+\.[0-9]+)\s*;version(?:\r\n|\n)")


def fingerprint_par(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    payload = text_payload(file, span)
    if payload is None:
        stats.add("too_large_for_text_validation")
        return
    content, padding = payload
    try:
        text = content.decode("ascii")
    except UnicodeDecodeError:
        stats.add("non_ascii")
        return
    stats.add("ascii")
    match = VERSION_LINE.match(text)
    if match:
        stats.add("version_comment_first_line")
        stats.count("declared_version", match.group(1))
    record_line_endings(text, stats)
    if padding:
        stats.add("zero_padded")
        stats.observe("padding_bytes", padding)
    else:
        stats.add("exact_text_span")


def fingerprint_col(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 48:
        stats.add("short_header")
        return
    header = read_at(file, span.offset, 48)
    if header[:3] == b"COL":
        stats.add("col_prefix")
        stats.count("version_byte", header[3])
    else:
        stats.add("bad_magic")
    stats.count("variant_word", struct.unpack_from("<I", header, 4)[0])
    if struct.unpack_from("<I", header, 8)[0] == 48:
        stats.add("header_size_48")
    if span.size % 16 == 0:
        stats.add("span_16_byte_aligned")


def fingerprint_vum(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 92:
        stats.add("short_header")
        return
    header = read_at(file, span.offset, 92)
    if header[:4] == b"VUMS":
        stats.add("vums_magic")
    else:
        stats.add("bad_magic")
    stats.count("variant_word", struct.unpack_from("<I", header, 4)[0])
    logical_size = struct.unpack_from("<I", header, 88)[0]
    stats.record_span(file, span.offset, span.size, logical_size, "header_size_field_span")
    if span.size % 16 == 0:
        stats.add("span_16_byte_aligned")


def fingerprint_vpk(file: BinaryIO, span: Span, stats: Aggregate) -> None:
    stats.add("count")
    stats.observe("span_bytes", span.size)
    if span.size < 16:
        stats.add("short_header")
        return
    header = read_at(file, span.offset, 16)
    if header[:4] == b" KPV":
        stats.add("little_endian_vpk_fourcc")
    else:
        stats.add("bad_magic")
    stats.count("declared_header_bytes", struct.unpack_from("<I", header, 8)[0])
    if span.size % 2048 == 0:
        stats.add("span_2048_byte_aligned")


FORMAT_HANDLERS = {
    ".tdx": fingerprint_tdx,
    ".skm": fingerprint_skm,
    ".skl": fingerprint_skl,
    ".vag": fingerprint_vag,
    ".lpd": fingerprint_lpd,
    ".par": fingerprint_par,
    ".col": fingerprint_col,
    ".vum": fingerprint_vum,
    ".vpk": fingerprint_vpk,
}


def scan_asset(
    file: BinaryIO,
    span: Span,
    depth: int,
    formats: dict[str, Aggregate],
    scan: Aggregate,
) -> None:
    extension = Path(span.name).suffix.lower() or "<none>"
    scan.add("asset_spans_scanned")
    scan.count("extensions", extension)
    scan.count("depth", depth)
    head = read_at(file, span.offset, min(16, span.size)) if span.size else b""
    # A HOG's unconstrained numeric tag can coincidentally equal a compression
    # magic.  HOG candidates are validated structurally instead of classified
    # from their first bytes.
    if extension != ".hog":
        scan.add("standard_compression_spans_checked")
        wrapper = compression_magic(head)
        if wrapper:
            scan.add("standard_compression_magic_hits")
            scan.count("standard_compression_magic", wrapper)
            scan.count("compression_hit_extension", extension)
    handler = FORMAT_HANDLERS.get(extension)
    if handler:
        handler(file, span, formats[extension])


def scan_hog_tree(
    file: BinaryIO,
    directory: HogDirectory,
    depth: int,
    formats: dict[str, Aggregate],
    scan: Aggregate,
    maximum_depth: int,
) -> None:
    names_by_extension: dict[str, set[str]] = collections.defaultdict(set)
    for entry in directory.entries:
        extension = Path(entry.name).suffix.lower()
        names_by_extension[extension].add(Path(entry.name).stem.casefold())
        scan_asset(file, entry, depth, formats, scan)
        if extension != ".hog":
            continue
        scan.add("nested_hog_candidates")
        if depth >= maximum_depth:
            scan.add("nested_hog_depth_limit")
            continue
        try:
            nested = parse_hog_span(file, entry.offset, entry.size)
        except ValueError:
            scan.add("nested_hog_invalid")
            continue
        scan.add("nested_hog_valid")
        if nested.padding_size:
            scan.add("nested_hog_zero_padded")
            scan.observe("nested_hog_padding_bytes", nested.padding_size)
        else:
            scan.add("nested_hog_exact_span")
        scan_hog_tree(file, nested, depth + 1, formats, scan, maximum_depth)

    lpd_names = names_by_extension.get(".lpd", set())
    vag_names = names_by_extension.get(".vag", set())
    if lpd_names:
        formats[".lpd"].add("same_directory_vag_companions", len(lpd_names & vag_names))
        formats[".lpd"].add("without_same_directory_vag_companion", len(lpd_names - vag_names))


def parse_pop_terrain(data: bytes) -> tuple[list[str], list[int], int, int]:
    if len(data) < 16 or struct.unpack_from("<I", data, 0)[0] != 70 or data[4:8] != b"TER:":
        raise ValueError("POP does not begin with the observed TER header")
    count = struct.unpack_from("<I", data, 8)[0]
    offset = 12
    names: list[str] = []
    kinds: list[int] = []
    nonzero_alignment_records = 0
    for _ in range(count):
        if offset + 8 > len(data):
            raise ValueError("truncated POP terrain record")
        kind, _index = struct.unpack_from("<2I", data, offset)
        offset += 8
        end = data.find(b"\0", offset)
        if end < 0:
            raise ValueError("unterminated POP terrain name")
        names.append(data[offset:end].decode("ascii"))
        kinds.append(kind)
        next_offset = align_up(end + 1, 4)
        nonzero_alignment_records += int(any(data[end + 1:next_offset]))
        offset = next_offset
    return names, kinds, offset, nonzero_alignment_records


def scan_pop_files(disc_root: Path) -> tuple[dict[str, object], dict[str, object]]:
    pop = Aggregate()
    minsk: dict[str, object] = {}
    section_tags = (
        b"TER:", b"GOB:", b"SND:", b"ACL:", b"INL:", b"NPC:", b"ANPC:",
        b"WPN:", b"PLR:", b"SKY:", b"PNT:", b"DIR:", b"ENV:", b"NOD:",
        b"GEN:", b"GRP:", b"BOX:", b"FIR:", b"CAM:", b"INV:", b"BUG:",
    )
    for path in sorted(disc_root.rglob("*.POP")):
        pop.add("file_count")
        data = path.read_bytes()
        pop.observe("file_bytes", len(data))
        try:
            names, kinds, next_offset, nonzero_alignment_records = parse_pop_terrain(data)
        except (ValueError, UnicodeDecodeError):
            pop.add("terrain_parse_failures")
            continue
        pop.add("terrain_parse_successes")
        pop.add("terrain_record_count", len(names))
        pop.add("terrain_records_with_nonzero_alignment_bytes", nonzero_alignment_records)
        for kind in kinds:
            pop.count("terrain_kind", kind)
        if data[next_offset:next_offset + 4] == b"GOB:":
            pop.add("gob_follows_terrain_records")

        data_hog_path = path.with_name("DATA.HOG")
        with data_hog_path.open("rb") as file:
            directory = parse_hog_span(file, 0, data_hog_path.stat().st_size)
        hog_stems = {Path(entry.name).stem.casefold() for entry in directory.entries}
        pop_stems = {Path(name).stem.casefold() for name in names}
        pop.add("terrain_names_matching_data_hog", len(pop_stems & hog_stems))
        pop.add("terrain_names_missing_from_data_hog", len(pop_stems - hog_stems))
        pop.add("data_hog_names_not_in_terrain", len(hog_stems - pop_stems))

        if path.parent.name.casefold() == "minsk":
            found_tags = []
            for tag in section_tags:
                position = data.find(tag)
                if position >= 0 and position % 4 == 0:
                    found_tags.append((position, tag.decode("ascii")))
            minsk["pop"] = {
                "bytes": len(data),
                "terrain_records": len(names),
                "terrain_records_with_nonzero_alignment_bytes": nonzero_alignment_records,
                "terrain_kind_values": sorted(set(kinds)),
                "next_section": data[next_offset:next_offset + 4].decode("ascii", "replace"),
                "ordered_section_tags_observed": [tag for _, tag in sorted(found_tags)],
                "terrain_names_matching_data_hog": len(pop_stems & hog_stems),
                "terrain_names_missing_from_data_hog": len(pop_stems - hog_stems),
                "data_hog_names_not_in_terrain": len(hog_stems - pop_stems),
            }
    return pop.as_dict(), minsk


def minsk_container_summary(disc_root: Path, minsk: dict[str, object]) -> None:
    level_root = disc_root / "GAMEDATA" / "MINSK"
    result: dict[str, object] = {}
    for filename in ("DATA.HOG", "MAPVUM.HOG", "MAPTEX.HOG"):
        path = level_root / filename
        with path.open("rb") as file:
            directory = parse_hog_span(file, 0, path.stat().st_size)
            record: dict[str, object] = {"entries": len(directory.entries)}
            if filename in ("DATA.HOG", "MAPVUM.HOG"):
                valid = 0
                padded = 0
                sector_aligned = 0
                patterns: collections.Counter[str] = collections.Counter()
                col_versions: collections.Counter[int] = collections.Counter()
                vum_size_field_exact = 0
                vum_count = 0
                for entry in directory.entries:
                    try:
                        nested = parse_hog_span(file, entry.offset, entry.size)
                    except ValueError:
                        continue
                    valid += 1
                    padded += int(nested.padding_size != 0)
                    sector_aligned += int(entry.size % 2048 == 0)
                    pattern = "+".join(sorted(Path(item.name).suffix.lower() for item in nested.entries))
                    patterns[pattern] += 1
                    for item in nested.entries:
                        extension = Path(item.name).suffix.lower()
                        if extension == ".col" and item.size >= 4:
                            col_versions[read_at(file, item.offset, 4)[3]] += 1
                        elif extension == ".vum" and item.size >= 92:
                            vum_count += 1
                            declared_size = struct.unpack_from("<I", read_at(file, item.offset + 88, 4))[0]
                            vum_size_field_exact += int(declared_size == item.size)
                record["nested_hog_valid"] = valid
                record["nested_hog_zero_padded"] = padded
                record["nested_hog_sector_aligned"] = sector_aligned
                record["nested_extension_patterns"] = dict(sorted(patterns.items()))
                record["col_version_bytes"] = {str(k): v for k, v in sorted(col_versions.items())}
                record["vum_header_size_field_exact"] = vum_size_field_exact
                record["vum_count"] = vum_count
            elif filename == "MAPTEX.HOG":
                tdx_versions: collections.Counter[int] = collections.Counter()
                for entry in directory.entries:
                    if Path(entry.name).suffix.lower() == ".tdx" and entry.size >= 2:
                        tdx_versions[struct.unpack("<H", read_at(file, entry.offset, 2))[0]] += 1
                record["tdx_version_words"] = {str(k): v for k, v in sorted(tdx_versions.items())}
            result[filename] = record
    minsk["containers"] = result


def direct_map_summary(disc_root: Path) -> dict[str, object]:
    result = Aggregate()
    for path in sorted(disc_root.rglob("*.MAP")):
        result.add("file_count")
        data = path.read_bytes()
        result.observe("file_bytes", len(data))
        try:
            data.decode("ascii")
        except UnicodeDecodeError:
            result.add("non_ascii")
        else:
            result.add("ascii_text")
    return result.as_dict()


def scan_disc(disc_root: Path, maximum_depth: int) -> dict[str, object]:
    formats = collections.defaultdict(Aggregate)
    scan = Aggregate()
    scan.add("standard_compression_magic_hits", 0)
    top_level_errors: list[str] = []

    hog_paths = sorted(disc_root.rglob("*.HOG"))
    scan.add("top_level_hog_files", len(hog_paths))
    for path in hog_paths:
        with path.open("rb") as file:
            try:
                directory = parse_hog_span(file, 0, path.stat().st_size)
            except ValueError as exc:
                top_level_errors.append(f"{path.relative_to(disc_root).as_posix()}: {exc}")
                continue
            scan.add("top_level_hog_valid")
            scan.add("top_level_hog_entries", len(directory.entries))
            if directory.padding_size:
                scan.add("top_level_hog_zero_padded")
            else:
                scan.add("top_level_hog_exact_span")
            scan_hog_tree(file, directory, 0, formats, scan, maximum_depth)

    target_extensions = set(FORMAT_HANDLERS)
    for path in sorted(item for item in disc_root.rglob("*") if item.is_file()):
        extension = path.suffix.lower()
        if extension == ".hog" or extension not in target_extensions:
            continue
        with path.open("rb") as file:
            scan_asset(file, Span(path.name, 0, path.stat().st_size), -1, formats, scan)

    pop, minsk = scan_pop_files(disc_root)
    minsk_container_summary(disc_root, minsk)
    output = {
        "schema_version": 1,
        "scope": "aggregate structural fingerprints only; no proprietary payloads exported",
        "scan": scan.as_dict(),
        "top_level_hog_errors": top_level_errors,
        "formats": {extension[1:]: stats.as_dict() for extension, stats in sorted(formats.items())},
        "population_files": pop,
        "direct_map_files": direct_map_summary(disc_root),
        "minsk": minsk,
    }
    return output


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("disc_root", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--maximum-depth", type=int, default=4)
    args = parser.parse_args()

    result = scan_disc(args.disc_root.resolve(), args.maximum_depth)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({
        "top_level_hog_files": result["scan"].get("top_level_hog_files", 0),
        "nested_hog_valid": result["scan"].get("nested_hog_valid", 0),
        "asset_spans_scanned": result["scan"].get("asset_spans_scanned", 0),
        "standard_compression_magic_hits": result["scan"].get("standard_compression_magic_hits", 0),
    }, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
