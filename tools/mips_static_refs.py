#!/usr/bin/env python3
"""Find simple MIPS address constructions and direct calls in an ELF32 image.

This is intentionally a metadata-only scanner: it reports virtual addresses and
decoded instruction fields, never executable bytes or decompiled source.
"""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class Segment:
    file_offset: int
    virtual_address: int
    file_size: int
    flags: int


def parse_int(text: str) -> int:
    return int(text, 0)


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def load_segments(data: bytes) -> list[Segment]:
    if data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        raise ValueError("expected a little-endian ELF32 file")
    phoff = struct.unpack_from("<I", data, 0x1C)[0]
    phentsize, phnum = struct.unpack_from("<HH", data, 0x2A)
    segments: list[Segment] = []
    for index in range(phnum):
        offset = phoff + index * phentsize
        p_type, p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz, p_flags, _p_align = (
            struct.unpack_from("<IIIIIIII", data, offset)
        )
        if p_type == 1 and p_filesz:
            segments.append(Segment(p_offset, p_vaddr, p_filesz, p_flags))
    return segments


def address_in_ranges(address: int, ranges: list[tuple[int, int]]) -> bool:
    return any(start <= address < end for start, end in ranges)


def describe_pair(
    first_va: int,
    second_va: int,
    base_reg: int,
    dest_reg: int,
    operation: str,
    address: int,
) -> dict[str, object]:
    return {
        "kind": "address_pair",
        "first_va": f"0x{first_va:08x}",
        "second_va": f"0x{second_va:08x}",
        "base_reg": base_reg,
        "dest_reg": dest_reg,
        "operation": operation,
        "address": f"0x{address & 0xFFFFFFFF:08x}",
    }


def scan(data: bytes, segments: list[Segment], ranges: list[tuple[int, int]], gp: int | None, window: int) -> list[dict[str, object]]:
    results: list[dict[str, object]] = []
    seen: set[tuple[object, ...]] = set()
    memory_ops = {
        0x20: "lb", 0x21: "lh", 0x23: "lw", 0x24: "lbu", 0x25: "lhu",
        0x28: "sb", 0x29: "sh", 0x2B: "sw", 0x31: "lwc1", 0x39: "swc1",
        0x37: "ld", 0x3F: "sd",
    }

    for segment in segments:
        start = segment.file_offset
        stop = start + segment.file_size
        for file_offset in range(start, stop - 3, 4):
            word = struct.unpack_from("<I", data, file_offset)[0]
            va = segment.virtual_address + file_offset - segment.file_offset

            if address_in_ranges(word, ranges):
                item = {
                    "kind": "word_pointer",
                    "from_va": f"0x{va:08x}",
                    "address": f"0x{word:08x}",
                }
                key = tuple(item.items())
                if key not in seen:
                    seen.add(key)
                    results.append(item)

            opcode = word >> 26
            rs = (word >> 21) & 0x1F
            rt = (word >> 16) & 0x1F
            imm = word & 0xFFFF

            if gp is not None and rs == 28 and opcode in memory_ops:
                address = (gp + signed16(imm)) & 0xFFFFFFFF
                if address_in_ranges(address, ranges):
                    item = {
                        "kind": "gp_memory",
                        "from_va": f"0x{va:08x}",
                        "operation": memory_ops[opcode],
                        "dest_reg": rt,
                        "gp": f"0x{gp:08x}",
                        "offset": signed16(imm),
                        "address": f"0x{address:08x}",
                    }
                    key = tuple(item.items())
                    if key not in seen:
                        seen.add(key)
                        results.append(item)

            if opcode != 0x0F:  # LUI
                continue
            high = imm << 16
            for step in range(1, window + 1):
                next_offset = file_offset + step * 4
                if next_offset + 4 > stop:
                    break
                next_word = struct.unpack_from("<I", data, next_offset)[0]
                next_opcode = next_word >> 26
                next_rs = (next_word >> 21) & 0x1F
                next_rt = (next_word >> 16) & 0x1F
                next_imm = next_word & 0xFFFF
                if next_rs == rt and next_opcode in memory_ops:
                    address = (high + signed16(next_imm)) & 0xFFFFFFFF
                    if address_in_ranges(address, ranges):
                        item = {
                            "kind": "absolute_memory",
                            "first_va": f"0x{va:08x}",
                            "second_va": f"0x{segment.virtual_address + next_offset - segment.file_offset:08x}",
                            "base_reg": rt,
                            "value_reg": next_rt,
                            "operation": memory_ops[next_opcode],
                            "address": f"0x{address:08x}",
                        }
                        key = tuple(item.items())
                        if key not in seen:
                            seen.add(key)
                            results.append(item)
                if next_rs == rt and next_opcode in (0x08, 0x09, 0x0D):
                    if next_opcode == 0x0D:
                        address = high | next_imm
                        operation = "ori"
                    else:
                        address = (high + signed16(next_imm)) & 0xFFFFFFFF
                        operation = "addi" if next_opcode == 0x08 else "addiu"
                    if address_in_ranges(address, ranges):
                        item = describe_pair(
                            va,
                            segment.virtual_address + next_offset - segment.file_offset,
                            rt,
                            next_rt,
                            operation,
                            address,
                        )
                        key = tuple(item.items())
                        if key not in seen:
                            seen.add(key)
                            results.append(item)

                # Stop only on opcodes whose architectural destination is rt.
                # Branches, jumps and stores reuse those bit positions for other
                # fields and must not terminate the constant chain.
                rt_writers = {
                    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
                    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
                    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
                }
                writes_rt = next_rt == rt and next_opcode in rt_writers
                if writes_rt and not (next_rs == rt and next_opcode in (0x08, 0x09, 0x0D)):
                    break

    return results


def direct_calls(data: bytes, segments: list[Segment], targets: set[int]) -> list[dict[str, str]]:
    results: list[dict[str, str]] = []
    for segment in segments:
        start = segment.file_offset
        stop = start + segment.file_size
        for file_offset in range(start, stop - 3, 4):
            word = struct.unpack_from("<I", data, file_offset)[0]
            if word >> 26 != 0x03:  # JAL
                continue
            va = segment.virtual_address + file_offset - segment.file_offset
            target = ((va + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)
            if target in targets:
                results.append({
                    "kind": "direct_call",
                    "from_va": f"0x{va:08x}",
                    "target": f"0x{target:08x}",
                })
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument(
        "--range",
        dest="ranges",
        action="append",
        default=[],
        metavar="START:END",
        help="half-open virtual-address range; repeatable",
    )
    parser.add_argument("--target", action="append", default=[], help="exact address; repeatable")
    parser.add_argument("--call-target", action="append", default=[], help="find direct JALs to this address")
    parser.add_argument("--gp", type=parse_int, help="global-pointer value for GP-relative memory references")
    parser.add_argument("--window", type=int, default=12, help="instructions to search after LUI (default: 12)")
    args = parser.parse_args()

    ranges: list[tuple[int, int]] = []
    for spec in args.ranges:
        start_text, end_text = spec.split(":", 1)
        ranges.append((parse_int(start_text), parse_int(end_text)))
    for target_text in args.target:
        target = parse_int(target_text)
        ranges.append((target, target + 1))
    if not ranges and not args.call_target:
        parser.error("provide at least one --range, --target, or --call-target")

    data = args.elf.read_bytes()
    segments = load_segments(data)
    results = scan(data, segments, ranges, args.gp, args.window) if ranges else []
    call_targets = {parse_int(value) for value in args.call_target}
    results.extend(direct_calls(data, segments, call_targets))
    results.sort(key=lambda item: tuple(str(value) for value in item.values()))

    print(json.dumps({
        # Keep output deterministic across checkout roots and safe to redirect
        # into evidence without exposing an owner's private absolute path.
        "elf": args.elf.name,
        "ranges": [[f"0x{s:08x}", f"0x{e:08x}"] for s, e in ranges],
        "gp": None if args.gp is None else f"0x{args.gp:08x}",
        "count": len(results),
        "results": results,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
