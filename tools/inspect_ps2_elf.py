#!/usr/bin/env python3
"""Read the ELF32 header/program table without external dependencies."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("elf", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    data = args.elf.read_bytes()
    if data[:4] != b"\x7fELF" or data[4] != 1:
        parser.error("expected an ELF32 file")
    endian = "<" if data[5] == 1 else ">"
    header = struct.unpack_from(endian + "HHIIIIIHHHHHH", data, 16)
    fields = (
        "type", "machine", "version", "entry", "phoff", "shoff", "flags",
        "ehsize", "phentsize", "phnum", "shentsize", "shnum", "shstrndx",
    )
    elf = dict(zip(fields, header))

    program_headers = []
    for index in range(elf["phnum"]):
        offset = elf["phoff"] + index * elf["phentsize"]
        values = struct.unpack_from(endian + "IIIIIIII", data, offset)
        program_headers.append(dict(zip(
            ("type", "offset", "vaddr", "paddr", "filesz", "memsz", "flags", "align"),
            values,
        )))

    result = {
        "path": args.elf.name,
        "size": len(data),
        "class": "ELF32",
        "endianness": "little" if endian == "<" else "big",
        "machine": elf["machine"],
        "machine_name": "MIPS" if elf["machine"] == 8 else "unknown",
        "entry": f"0x{elf['entry']:08X}",
        "flags": f"0x{elf['flags']:08X}",
        "program_headers": program_headers,
        "section_count": elf["shnum"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

