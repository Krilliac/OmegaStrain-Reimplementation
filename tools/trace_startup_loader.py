#!/usr/bin/env python3
"""Verify metadata-only startup-mode and level-loader evidence in the retail ELF.

The report contains addresses, decoded control-flow metadata, and hashes of a
small set of path templates. It never emits executable bytes, decompiled code,
retail strings, or the input's parent path.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path


EXPECTED_ELF_SHA256 = "9924da91767c8145411f37fa6c14c9d77208264c17f1ce9ee157d51abdd31dc6"


@dataclass(frozen=True)
class Segment:
    file_offset: int
    virtual_address: int
    file_size: int


MEMORY_OPS = {
    0x20: "lb",
    0x21: "lh",
    0x23: "lw",
    0x24: "lbu",
    0x25: "lhu",
    0x28: "sb",
    0x29: "sh",
    0x2B: "sw",
    0x31: "lwc1",
    0x37: "ld",
    0x39: "swc1",
    0x3F: "sd",
}


PATH_TEMPLATE_HASHES = {
    "level_loading_archive": (
        0x0049AA70,
        17,
        "e2bbe3402169e13959879f7c25d71365862011bafb9c48bfe8b8f4b97fd39fbd",
    ),
    "common_loading_archive": (
        0x0049AA90,
        21,
        "c822f1f0c77c3ceda5e0da21de272e37daf2edd1a20e5c2d4deeb3c8f6f51d7e",
    ),
    "localized_strings_suffix": (
        0x004BE460,
        14,
        "07cf6daf3ed8869b2d5bd4cfc9eb0c2d9c82103d6eeeee29a8168c711ebb9e0b",
    ),
    "level_data_archive": (
        0x0049F2A8,
        14,
        "96fcc23be0279bfeff819770249ca89c2311017d1e33ad3880f0b6b6fe8bc21c",
    ),
    "level_weapon_archive": (
        0x0049B2C0,
        13,
        "68915457565d67ccb398ca50f11a6f49f133c3a20c9e812a2b3e1678c74a9c1d",
    ),
    "common_weapon_archive": (
        0x0049B210,
        20,
        "41182a0b4e44d5453b9922c51e4652f95fa8164195769979773878a1e09e8ae2",
    ),
    "level_npc_texture_archive": (
        0x0049B2D0,
        16,
        "acd2ed7b7a36d9f298d555ce200f446d412c99a1bd4e5b717f4ee9019f0c176c",
    ),
    "level_npc_mesh_archive": (
        0x0049B2F0,
        16,
        "69efeae675eb83b5b167f7bfd93be9ec5a2242d154e3fe9b134629c32fea3ee0",
    ),
}


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


class ElfImage:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if self.data[:6] != b"\x7fELF\x01\x01":
            raise ValueError("expected a little-endian ELF32 file")
        phoff = struct.unpack_from("<I", self.data, 0x1C)[0]
        phentsize, phnum = struct.unpack_from("<HH", self.data, 0x2A)
        self.segments: list[Segment] = []
        for index in range(phnum):
            offset = phoff + index * phentsize
            p_type, p_offset, p_vaddr, _p_paddr, p_filesz, _p_memsz, _p_flags, _p_align = (
                struct.unpack_from("<IIIIIIII", self.data, offset)
            )
            if p_type == 1 and p_filesz:
                self.segments.append(Segment(p_offset, p_vaddr, p_filesz))

    def file_offset(self, virtual_address: int) -> int:
        for segment in self.segments:
            relative = virtual_address - segment.virtual_address
            if 0 <= relative < segment.file_size:
                return segment.file_offset + relative
        raise ValueError(f"virtual address is not file-backed: 0x{virtual_address:08x}")

    def word(self, virtual_address: int) -> int:
        return struct.unpack_from("<I", self.data, self.file_offset(virtual_address))[0]

    def c_string(self, virtual_address: int) -> bytes:
        start = self.file_offset(virtual_address)
        end = self.data.find(b"\0", start)
        if end < 0:
            raise ValueError(f"unterminated string at 0x{virtual_address:08x}")
        return self.data[start:end]


def jal_target(word: int, virtual_address: int) -> int:
    if word >> 26 != 0x03:
        raise ValueError(f"expected JAL at 0x{virtual_address:08x}")
    return ((virtual_address + 4) & 0xF0000000) | ((word & 0x03FFFFFF) << 2)


def branch_target(word: int, virtual_address: int) -> int:
    if word >> 26 not in (0x04, 0x05):
        raise ValueError(f"expected BEQ/BNE at 0x{virtual_address:08x}")
    return virtual_address + 4 + signed16(word & 0xFFFF) * 4


def expect_call(image: ElfImage, site: int, expected_target: int) -> dict[str, str]:
    target = jal_target(image.word(site), site)
    if target != expected_target:
        raise ValueError(
            f"call target mismatch at 0x{site:08x}: expected 0x{expected_target:08x}, "
            f"got 0x{target:08x}"
        )
    return {"site": f"0x{site:08x}", "target": f"0x{target:08x}"}


def expect_branch(image: ElfImage, site: int, expected_target: int) -> dict[str, str]:
    target = branch_target(image.word(site), site)
    if target != expected_target:
        raise ValueError(
            f"branch target mismatch at 0x{site:08x}: expected 0x{expected_target:08x}, "
            f"got 0x{target:08x}"
        )
    return {"site": f"0x{site:08x}", "target": f"0x{target:08x}"}


def adjacent_absolute_accesses(image: ElfImage, target: int) -> list[dict[str, str]]:
    """Find direct LUI + memory accesses where the instructions are adjacent."""
    accesses: list[dict[str, str]] = []
    for segment in image.segments:
        start = segment.file_offset + 4
        stop = segment.file_offset + segment.file_size
        for offset in range(start, stop - 3, 4):
            word = struct.unpack_from("<I", image.data, offset)[0]
            opcode = word >> 26
            operation = MEMORY_OPS.get(opcode)
            if operation is None:
                continue
            base_register = (word >> 21) & 0x1F
            previous = struct.unpack_from("<I", image.data, offset - 4)[0]
            if previous >> 26 != 0x0F or ((previous >> 16) & 0x1F) != base_register:
                continue
            address = (((previous & 0xFFFF) << 16) + signed16(word & 0xFFFF)) & 0xFFFFFFFF
            if address != target:
                continue
            virtual_address = segment.virtual_address + offset - segment.file_offset
            accesses.append(
                {
                    "access_va": f"0x{virtual_address:08x}",
                    "operation": operation,
                }
            )
    return accesses


def verify_path_templates(image: ElfImage) -> list[dict[str, object]]:
    results: list[dict[str, object]] = []
    for label, (address, expected_length, expected_hash) in PATH_TEMPLATE_HASHES.items():
        value = image.c_string(address)
        digest = hashlib.sha256(value).hexdigest()
        if len(value) != expected_length or digest != expected_hash:
            raise ValueError(f"path-template fingerprint mismatch for {label}")
        results.append(
            {
                "label": label,
                "length": len(value),
                "sha256": digest,
                "va": f"0x{address:08x}",
            }
        )
    return results


def build_report(image: ElfImage) -> dict[str, object]:
    elf_hash = hashlib.sha256(image.data).hexdigest()
    if elf_hash != EXPECTED_ELF_SHA256:
        raise ValueError(
            "unexpected executable SHA-256; this evidence profile is for the verified NTSC-U image"
        )

    flag_accesses = adjacent_absolute_accesses(image, 0x004FFBB2)
    expected_accesses = [
        {"access_va": "0x0013d484", "operation": "sb"},
        {"access_va": "0x00167eec", "operation": "lbu"},
        {"access_va": "0x001c1fb8", "operation": "lbu"},
        {"access_va": "0x002c7e04", "operation": "lbu"},
    ]
    if flag_accesses != expected_accesses:
        raise ValueError(f"startup-mode direct-access set changed: {flag_accesses!r}")

    parser_stages = [
        0x001C1A20,
        0x001C1770,
        0x001C0920,
        0x001C1E40,
        0x001C3940,
        0x001C1470,
        0x001C12A0,
        0x001C2AE0,
        0x001C2A10,
        0x001C0EE0,
        0x001C0C00,
        0x001C0A90,
        0x001C4000,
    ]
    parser_call_sites = [
        0x001C422C,
        0x001C4238,
        0x001C4244,
        0x001C4254,
        0x001C4268,
        0x001C4274,
        0x001C4280,
        0x001C428C,
        0x001C4298,
        0x001C42AC,
        0x001C42C0,
        0x001C42CC,
        0x001C42D4,
    ]

    return {
        "elf": {
            "name": image.path.name,
            "sha256": elf_hash,
            "size": len(image.data),
        },
        "level_loader": {
            "data_archive": {
                "archive_mount": expect_call(image, 0x001C41F4, 0x003F96E0),
                "ordered_parser_calls": [
                    expect_call(image, site, target)
                    for site, target in zip(parser_call_sites, parser_stages, strict=True)
                ],
                "parser_call_count": len(parser_stages),
                "path_format": expect_call(image, 0x001C41E0, 0x00128BD0),
                "reader_cleanup": expect_call(image, 0x001C42E0, 0x00178C60),
                "reader_init": expect_call(image, 0x001C420C, 0x00178D40),
            },
            "loading_archive": {
                "archive_lookup": expect_call(image, 0x00166494, 0x003F9600),
                "archive_mount": expect_call(image, 0x00166484, 0x003F96E0),
                "availability_branch": expect_branch(image, 0x00166470, 0x00166548),
                "availability_probe": expect_call(image, 0x00166464, 0x0036DEF0),
                "candidate_format": expect_call(image, 0x0013544C, 0x00128BD0),
                "candidate_probe": expect_call(image, 0x00135468, 0x0036DEF0),
                "fallback_branch": expect_branch(image, 0x00135474, 0x00135490),
                "handoff": expect_call(image, 0x001354A0, 0x00166430),
                "localization_reload": expect_call(image, 0x0013542C, 0x003FDCA0),
                "loading_screen_construct": expect_call(image, 0x00166524, 0x002721B0),
            },
            "path_template_fingerprints": verify_path_templates(image),
            "weapon_archives": {
                "common_weapon_mount": expect_call(image, 0x00145B34, 0x003F96E0),
                "level_npc_mesh_mount": expect_call(image, 0x00145BCC, 0x003F96E0),
                "level_npc_texture_mount": expect_call(image, 0x00145B80, 0x003F96E0),
                "level_weapon_mount": expect_call(image, 0x00145AF0, 0x003F96E0),
            },
        },
        "startup_mode": {
            "direct_flag_accesses": flag_accesses,
            "flag_va": "0x004ffbb2",
            "handler_va": "0x0013d408",
            "semantic_branches": {
                "character_first_record": {
                    "branch": expect_branch(image, 0x001C1FBC, 0x001C1FC8),
                    "clear_mode_override_va": "0x001c1fc4",
                },
                "splash_backend_guard": {
                    "branch": expect_branch(image, 0x002C7E08, 0x002C7E30),
                    "fallback_call": expect_call(image, 0x002C7E1C, 0x002C7C40),
                },
                "startup_splash_dispatch": {
                    "branch": expect_branch(image, 0x00167EF0, 0x00167F28),
                    "clear_mode_call": expect_call(image, 0x00167F30, 0x002C7C40),
                    "set_mode_calls": [
                        expect_call(image, 0x00167EFC, 0x002CB8C0),
                        expect_call(image, 0x00167F14, 0x002C7D90),
                    ],
                },
            },
        },
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    args = parser.parse_args()
    try:
        report = build_report(ElfImage(args.elf))
    except (OSError, ValueError, struct.error) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
