#!/usr/bin/env python3
"""Inspect Omega Strain script modules without extracting proprietary payloads.

The game's ``.SO`` files are custom little-endian VM modules, not ELF shared
objects.  This tool reads them directly from ``SCRIPTS.HOG`` archives (or from
an explicitly supplied ``.SO`` file) and emits structural metadata only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from hog import parse_hog


UINT32_MAX = 0xFFFFFFFF
FUNCTION_MARKER_LOW16 = 0x003B


class ParseError(ValueError):
    """Raised when a module violates the recovered serialization grammar."""


class Reader:
    def __init__(self, data: bytes) -> None:
        self.data = data
        self.offset = 0

    def seek(self, offset: int) -> None:
        if not 0 <= offset <= len(self.data):
            raise ParseError(f"seek outside module: 0x{offset:X}")
        self.offset = offset

    def u32(self) -> int:
        if self.offset + 4 > len(self.data):
            raise ParseError(f"u32 reaches EOF at 0x{self.offset:X}")
        value = struct.unpack_from("<I", self.data, self.offset)[0]
        self.offset += 4
        return value

    def cells(self, count: int) -> tuple[int, ...]:
        if count < 0 or self.offset + 4 * count > len(self.data):
            raise ParseError(f"{count} cells reach EOF at 0x{self.offset:X}")
        values = struct.unpack_from(f"<{count}I", self.data, self.offset) if count else ()
        self.offset += 4 * count
        return tuple(values)

    def lp_bytes(self) -> bytes:
        """Read a 32-bit-length-prefixed, NUL-terminated, 4-byte-padded value."""
        length_offset = self.offset
        length = self.u32()
        if length > len(self.data) - self.offset:
            raise ParseError(f"string length {length} reaches EOF at 0x{length_offset:X}")
        raw = self.data[self.offset:self.offset + length]
        if length and raw[-1] != 0:
            raise ParseError(f"string lacks NUL terminator at 0x{length_offset:X}")
        self.offset += length
        padded_offset = (self.offset + 3) & ~3
        if padded_offset > len(self.data):
            raise ParseError(f"string padding reaches EOF at 0x{length_offset:X}")
        if any(self.data[self.offset:padded_offset]):
            raise ParseError(f"string has nonzero padding at 0x{length_offset:X}")
        self.offset = padded_offset
        return raw[:-1] if length else b""

    def lp_ascii(self) -> str:
        raw = self.lp_bytes()
        try:
            value = raw.decode("ascii")
        except UnicodeDecodeError as exc:
            raise ParseError(f"non-ASCII metadata name near 0x{self.offset:X}") from exc
        if any(ord(char) < 0x20 or ord(char) > 0x7E for char in value):
            raise ParseError(f"control character in metadata name near 0x{self.offset:X}")
        return value


@dataclass(frozen=True)
class TypeRecord:
    name: str
    external_flag: int
    base: str
    members: tuple[tuple[str, int], ...]


@dataclass(frozen=True)
class EnumRecord:
    name: str
    external_flag: int
    values: tuple[tuple[str, int], ...]


@dataclass(frozen=True)
class GlobalRecord:
    name: str
    fields: tuple[int, int, int, int]
    initializer_cells: tuple[int, ...]


@dataclass(frozen=True)
class CallableRecord:
    name: str
    fields: tuple[int, ...]
    parameter_types: tuple[int, ...]


def hash_json(value: object) -> str:
    encoded = json.dumps(value, ensure_ascii=True, separators=(",", ":"), sort_keys=True).encode()
    return hashlib.sha256(encoded).hexdigest()


def hex_histogram(values: Iterable[int]) -> dict[str, int]:
    counts = Counter(values)
    return {f"0x{value:08X}": counts[value] for value in sorted(counts)}


def schema_hashes(types: list[TypeRecord], enums: list[EnumRecord]) -> dict[str, str]:
    type_rows = [
        [record.name, record.external_flag, record.base, list(record.members)]
        for record in types
    ]
    enum_rows = [
        [record.name, record.external_flag, list(record.values)]
        for record in enums
    ]
    return {
        "types_all": hash_json(type_rows),
        "types_external_flag_1": hash_json([row for row in type_rows if row[1] == 1]),
        "enums_all": hash_json(enum_rows),
        "enums_external_flag_1": hash_json([row for row in enum_rows if row[1] == 1]),
    }


def parse_module(data: bytes, module_id: str) -> dict[str, Any]:
    reader = Reader(data)
    code_offset = reader.u32()
    code_cell_count = reader.u32()
    if code_offset != reader.offset:
        raise ParseError(
            f"code offset 0x{code_offset:X} does not match header end 0x{reader.offset:X}"
        )
    reader.seek(code_offset)
    code_cells = reader.cells(code_cell_count)
    code_end = reader.offset

    literals_start = reader.offset
    literal_count = reader.u32()
    for _ in range(literal_count):
        reader.lp_bytes()
    literals_end = reader.offset

    types_start = reader.offset
    type_count = reader.u32()
    types: list[TypeRecord] = []
    for _ in range(type_count):
        name = reader.lp_ascii()
        external_flag = reader.u32()
        if external_flag not in (0, 1):
            raise ParseError(f"type {name!r} has flag {external_flag}")
        base = reader.lp_ascii()
        member_count = reader.u32()
        members = tuple((reader.lp_ascii(), reader.u32()) for _ in range(member_count))
        types.append(TypeRecord(name, external_flag, base, members))
    types_end = reader.offset

    enums_start = reader.offset
    enum_count = reader.u32()
    enums: list[EnumRecord] = []
    for _ in range(enum_count):
        name = reader.lp_ascii()
        external_flag = reader.u32()
        if external_flag not in (0, 1):
            raise ParseError(f"enum {name!r} has flag {external_flag}")
        values: tuple[tuple[str, int], ...] = ()
        if external_flag == 0:
            value_count = reader.u32()
            values = tuple((reader.lp_ascii(), reader.u32()) for _ in range(value_count))
        enums.append(EnumRecord(name, external_flag, values))
    enums_end = reader.offset

    globals_start = reader.offset
    globals_reserved = reader.u32()
    global_count = reader.u32()
    globals_: list[GlobalRecord] = []
    for _ in range(global_count):
        name = reader.lp_ascii()
        fields = tuple(reader.u32() for _ in range(4))
        if fields[1] not in (0, 1):
            raise ParseError(f"global {name!r} has flag {fields[1]}")
        initializer_cells = reader.cells(fields[3])
        globals_.append(GlobalRecord(name, fields, initializer_cells))
    globals_end = reader.offset

    callables_start = reader.offset
    callable_count = reader.u32()
    callables: list[CallableRecord] = []
    for _ in range(callable_count):
        name = reader.lp_ascii()
        fields = tuple(reader.u32() for _ in range(10))
        if fields[1] not in (0, 1):
            raise ParseError(f"callable {name!r} has flag {fields[1]}")
        parameter_count = reader.u32()
        parameter_types = reader.cells(parameter_count)
        callables.append(CallableRecord(name, fields, parameter_types))
    if reader.offset != len(data):
        raise ParseError(f"{len(data) - reader.offset} trailing bytes at 0x{reader.offset:X}")

    global_flags = [record.fields[1] for record in globals_]
    callable_flags = [record.fields[1] for record in callables]
    local_globals = [record for record in globals_ if record.fields[1] == 0]
    external_globals = [record for record in globals_ if record.fields[1] == 1]
    local_callables = [record for record in callables if record.fields[1] == 0]
    external_callables = [record for record in callables if record.fields[1] == 1]
    local_entries = [record.fields[4] for record in local_callables]
    marker_matches = 0
    for record in local_callables:
        label_id = record.fields[3]
        entry = record.fields[4]
        if 0 < entry <= len(code_cells):
            marker = code_cells[entry - 1]
            if marker & 0xFFFF == FUNCTION_MARKER_LOW16 and marker >> 16 == label_id:
                marker_matches += 1

    type_external = sum(record.external_flag == 1 for record in types)
    enum_external = sum(record.external_flag == 1 for record in enums)
    parameter_types = [value for record in callables for value in record.parameter_types]
    schema = schema_hashes(types, enums)

    return {
        "module": module_id,
        "size": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "sections": {
            "code": {"offset": code_offset, "end": code_end, "cell_count": code_cell_count},
            "literals": {
                "offset": literals_start,
                "end": literals_end,
                "count": literal_count,
                "serialized_sha256": hashlib.sha256(data[literals_start:literals_end]).hexdigest(),
            },
            "types": {"offset": types_start, "end": types_end},
            "enums": {"offset": enums_start, "end": enums_end},
            "globals": {"offset": globals_start, "end": globals_end},
            "callables": {"offset": callables_start, "end": reader.offset},
        },
        "types": {
            "count": len(types),
            "external_flag_1": type_external,
            "local_flag_0": len(types) - type_external,
            "member_count": sum(len(record.members) for record in types),
        },
        "enums": {
            "count": len(enums),
            "external_flag_1": enum_external,
            "local_flag_0": len(enums) - enum_external,
            "local_value_count": sum(len(record.values) for record in enums),
        },
        "schema_sha256": schema,
        "globals": {
            "reserved_u32": globals_reserved,
            "count": len(globals_),
            "external_flag_1": sum(flag == 1 for flag in global_flags),
            "local_flag_0": len(local_globals),
            "field_0_histogram": hex_histogram(record.fields[0] for record in globals_),
            "local_field_0_histogram": hex_histogram(record.fields[0] for record in local_globals),
            "initializer_record_count": sum(bool(record.initializer_cells) for record in globals_),
            "initializer_cell_count": sum(len(record.initializer_cells) for record in globals_),
            "external_initializers_are_empty": all(
                not record.initializer_cells for record in external_globals
            ),
            "ordinals_match_table_order": all(
                record.fields[2] == index for index, record in enumerate(globals_)
            ),
        },
        "callables": {
            "count": len(callables),
            "external_flag_1": sum(flag == 1 for flag in callable_flags),
            "local_flag_0": len(local_callables),
            "field_0_histogram": hex_histogram(record.fields[0] for record in callables),
            "local_field_0_histogram": hex_histogram(record.fields[0] for record in local_callables),
            "parameter_count": sum(len(record.parameter_types) for record in callables),
            "max_parameter_count": max((len(record.parameter_types) for record in callables), default=0),
            "parameter_type_histogram": hex_histogram(parameter_types),
            "ordinals_match_table_order": all(
                record.fields[2] == index for index, record in enumerate(callables)
            ),
            "local_entries_strictly_increasing": all(
                left < right for left, right in zip(local_entries, local_entries[1:])
            ),
            "external_entry_cells_are_0": all(
                record.fields[4] == 0 for record in external_callables
            ),
            "local_entry_cells_are_nonzero": all(
                record.fields[4] != 0 for record in local_callables
            ),
            "local_entry_cells_are_in_code": all(
                0 < record.fields[4] < len(code_cells) for record in local_callables
            ),
            "local_entry_marker_matches": marker_matches,
            "local_entry_marker_expected": len(local_callables),
            "unknown_fields_5_to_9_nondefault": [
                sum(
                    record.fields[index] not in (0, UINT32_MAX)
                    for record in callables
                )
                for index in range(5, 10)
            ],
        },
    }


def relative_name(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        return path.name


def iter_modules(source: Path) -> Iterable[tuple[str, bytes]]:
    if source.is_file() and source.suffix.casefold() == ".so":
        yield source.name, source.read_bytes()
        return

    if source.is_file() and source.suffix.casefold() == ".hog":
        archives = [source]
        root = source.parent
    elif source.is_dir():
        archives = sorted(source.rglob("SCRIPTS.HOG"))
        root = source
    else:
        raise FileNotFoundError(source)

    if not archives:
        raise FileNotFoundError(f"no SCRIPTS.HOG archives under {source}")

    for archive_path in archives:
        archive, data = parse_hog(archive_path)
        archive_name = relative_name(archive_path, root)
        for entry in archive.entries:
            if not entry.name.casefold().endswith(".so"):
                continue
            module_id = f"{archive_name}::{entry.name.replace(chr(92), '/')}"
            yield module_id, data[entry.offset:entry.offset + entry.size]


def aggregate(modules: list[dict[str, Any]], errors: list[dict[str, str]]) -> dict[str, Any]:
    def total(*keys: str) -> int:
        values: list[int] = []
        for module in modules:
            value: Any = module
            for key in keys:
                value = value[key]
            values.append(value)
        return sum(values)

    schema_keys = (
        "types_all",
        "types_external_flag_1",
        "enums_all",
        "enums_external_flag_1",
    )
    schema_variants = {
        key: len({module["schema_sha256"][key] for module in modules})
        for key in schema_keys
    }
    marker_matches = total("callables", "local_entry_marker_matches")
    marker_expected = total("callables", "local_entry_marker_expected")
    return {
        "module_count": len(modules),
        "error_count": len(errors),
        "byte_count": total("size"),
        "code_cell_count": total("sections", "code", "cell_count"),
        "literal_count": total("sections", "literals", "count"),
        "type_record_count": total("types", "count"),
        "type_member_count": total("types", "member_count"),
        "enum_record_count": total("enums", "count"),
        "local_enum_value_count": total("enums", "local_value_count"),
        "global_record_count": total("globals", "count"),
        "local_global_count": total("globals", "local_flag_0"),
        "global_initializer_cell_count": total("globals", "initializer_cell_count"),
        "callable_record_count": total("callables", "count"),
        "local_callable_count": total("callables", "local_flag_0"),
        "callable_parameter_count": total("callables", "parameter_count"),
        "schema_variant_counts": schema_variants,
        "invariants": {
            "all_code_offsets_are_8": all(module["sections"]["code"]["offset"] == 8 for module in modules),
            "all_globals_reserved_u32_are_0": all(module["globals"]["reserved_u32"] == 0 for module in modules),
            "all_global_ordinals_match_table_order": all(module["globals"]["ordinals_match_table_order"] for module in modules),
            "all_external_global_initializers_are_empty": all(module["globals"]["external_initializers_are_empty"] for module in modules),
            "all_callable_ordinals_match_table_order": all(module["callables"]["ordinals_match_table_order"] for module in modules),
            "all_local_entries_strictly_increasing": all(module["callables"]["local_entries_strictly_increasing"] for module in modules),
            "all_external_callable_entry_cells_are_0": all(module["callables"]["external_entry_cells_are_0"] for module in modules),
            "all_local_callable_entry_cells_are_nonzero": all(module["callables"]["local_entry_cells_are_nonzero"] for module in modules),
            "all_local_callable_entry_cells_are_in_code": all(module["callables"]["local_entry_cells_are_in_code"] for module in modules),
            "function_marker_low16": f"0x{FUNCTION_MARKER_LOW16:04X}",
            "function_marker_matches": marker_matches,
            "function_marker_expected": marker_expected,
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="extracted disc root, SCRIPTS.HOG, or .SO")
    parser.add_argument("--json", type=Path, help="write deterministic metadata JSON")
    args = parser.parse_args()

    modules: list[dict[str, Any]] = []
    errors: list[dict[str, str]] = []
    for module_id, data in iter_modules(args.source):
        try:
            modules.append(parse_module(data, module_id))
        except (ParseError, struct.error, UnicodeError) as exc:
            errors.append({"module": module_id, "error": str(exc)})

    modules.sort(key=lambda module: module["module"])
    errors.sort(key=lambda error: error["module"])
    result = {
        "format": "omega-strain-so-inspection-v1",
        "aggregate": aggregate(modules, errors),
        "errors": errors,
        "modules": modules,
    }
    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")

    print(json.dumps(result["aggregate"], sort_keys=True))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
