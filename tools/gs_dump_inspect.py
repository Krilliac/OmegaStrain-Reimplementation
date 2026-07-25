#!/usr/bin/env python3
"""Parse a PCSX2 GS dump (.gs / .gs.xz / .gs.zst) into structured draw-call JSON.

The container and packet grammar implemented here is transcribed directly from the
PCSX2 sources that define it -- ``pcsx2/GS/GSDump.cpp`` (writer, ``GSDumpBase::AddHeader``
/ ``Transfer`` / ``ReadFIFO`` / ``VSync``), ``pcsx2/GS/GSDump.h`` (``GSDumpHeader``) and
``pcsx2/GS/GSLzma.cpp`` (reader, ``GSDumpFile::ReadFile``). Nothing here is guessed from
third-party descriptions of the format.

Container layout (new-style header, the only one PCSX2 writes today)::

    u32  0xFFFFFFFF            fake CRC marking a new-style header
    u32  header_size           == sizeof(GSDumpHeader) + serial + screenshot
    u8   header_size bytes     GSDumpHeader, then serial text, then BGRA screenshot
    u8   header.state_size     the real freeze/state blob
    u8   8192                  GSPrivRegSet
    ...  packet stream until EOF

Packet stream (``GSDumpFile::ReadFile``)::

    id 0 Transfer   : u8 path, u32 size, size bytes of GIF data
    id 1 VSync      : u8 field
    id 2 ReadFIFO2  : u32 size
    id 3 Registers  : 8192 bytes (GSPrivRegSet)

GIF data inside a Transfer packet is decoded per the GIFtag/PACKED/REGLIST/IMAGE rules
so that vertex kicks (XYZ2/XYZF2/XYZ3/XYZF3) and the drawing-environment registers that
delimit batches can be enumerated as draw calls.

Privacy: this tool emits geometry and register state only. It never writes texture
payloads, framebuffer contents or the embedded screenshot to its output.

Usage::

    python -B tools/gs_dump_inspect.py <dump.gs.zst> <out.json> [options]
"""

from __future__ import annotations

import argparse
import json
import shutil
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# --------------------------------------------------------------------------------------
# Decompression
# --------------------------------------------------------------------------------------


def _decompress_zstd(raw: bytes) -> tuple[bytes, str]:
    try:
        import zstandard  # type: ignore
    except ImportError:
        pass
    else:
        dctx = zstandard.ZstdDecompressor()
        # GSDumpZst compresses with ZSTD_e_continue and a single ZSTD_e_end, so the
        # frame header carries no content size and one-shot decompress() refuses it.
        # read_across_frames is set so a writer that ever ends a frame early still
        # decodes completely.
        with dctx.stream_reader(raw, read_across_frames=True) as reader:
            return reader.read(), "python-zstandard"

    exe = shutil.which("zstd")
    if exe:
        proc = subprocess.run([exe, "-d", "-c"], input=raw, capture_output=True)
        if proc.returncode != 0:
            raise RuntimeError(f"zstd CLI failed: {proc.stderr[:200]!r}")
        return proc.stdout, "zstd-cli"

    raise RuntimeError(
        "no zstd decompressor available: install the 'zstandard' Python module "
        "or put the 'zstd' CLI on PATH"
    )


def _decompress_xz(raw: bytes) -> tuple[bytes, str]:
    import lzma

    dec = lzma.LZMADecompressor(format=lzma.FORMAT_XZ)
    out = bytearray()
    out += dec.decompress(raw)
    # Concatenated .xz streams: PCSX2 flushes multiple streams into one file.
    while dec.eof and dec.unused_data:
        rest = dec.unused_data
        dec = lzma.LZMADecompressor(format=lzma.FORMAT_XZ)
        out += dec.decompress(rest)
    return bytes(out), "python-lzma"


def load_dump_bytes(path: Path) -> tuple[bytes, str]:
    """Return (uncompressed dump bytes, name of the decompression path used)."""
    raw = path.read_bytes()
    name = path.name.lower()
    if name.endswith(".zst"):
        return _decompress_zstd(raw)
    if name.endswith(".xz"):
        return _decompress_xz(raw)
    return raw, "uncompressed"


# --------------------------------------------------------------------------------------
# Container
# --------------------------------------------------------------------------------------

GS_DUMP_HEADER = struct.Struct("<9I")  # GSDump.h: GSDumpHeader, #pragma pack(4)

PACKET_TRANSFER = 0
PACKET_VSYNC = 1
PACKET_READFIFO2 = 2
PACKET_REGISTERS = 3

TRANSFER_PATH_NAMES = {0: "Path1Old", 1: "Path2", 2: "Path3", 3: "Path1New", 4: "Dummy"}


@dataclass
class Packet:
    index: int
    kind: int
    offset: int
    length: int
    path: int | None = None


@dataclass
class Container:
    crc: int
    serial: str
    state_version: int
    state_size: int
    screenshot_width: int
    screenshot_height: int
    screenshot_size: int
    regs_data: bytes
    packets: list[Packet]
    data: bytes  # the whole uncompressed dump; packet payloads are slices of it


def parse_container(buf: bytes) -> Container:
    """Transcription of GSDumpFile::ReadFile (GSLzma.cpp)."""
    pos = 0

    def u32() -> int:
        nonlocal pos
        v = struct.unpack_from("<I", buf, pos)[0]
        pos += 4
        return v

    crc = u32()
    ss = u32()
    if pos + ss > len(buf):
        raise ValueError("truncated dump: state blob exceeds file")
    state_blob = buf[pos : pos + ss]
    pos += ss

    serial = ""
    state_version = 0
    state_size = ss
    shot_w = shot_h = shot_size = 0

    if crc == 0xFFFFFFFF:
        if len(state_blob) < GS_DUMP_HEADER.size:
            raise ValueError("GSDump header is corrupted")
        (
            state_version,
            state_size,
            serial_offset,
            serial_size,
            crc,
            shot_w,
            shot_h,
            _shot_off,
            shot_size,
        ) = GS_DUMP_HEADER.unpack_from(state_blob, 0)
        if serial_size:
            serial = state_blob[serial_offset : serial_offset + serial_size].decode(
                "ascii", "replace"
            )
        # The real state data follows the header blob.
        if pos + state_size > len(buf):
            raise ValueError("truncated dump: real state blob exceeds file")
        pos += state_size

    regs_data = buf[pos : pos + 8192]
    if len(regs_data) != 8192:
        raise ValueError("truncated dump: missing GSPrivRegSet")
    pos += 8192

    packets: list[Packet] = []
    idx = 0
    total = len(buf)
    while pos < total:
        kind = buf[pos]
        pos += 1
        path = None
        if kind == PACKET_TRANSFER:
            if pos + 5 > total:
                break
            path = buf[pos]
            length = struct.unpack_from("<I", buf, pos + 1)[0]
            pos += 5
        elif kind == PACKET_VSYNC:
            length = 1
        elif kind == PACKET_READFIFO2:
            length = 4
        elif kind == PACKET_REGISTERS:
            length = 8192
        else:
            raise ValueError(f"unknown packet type {kind} at offset {pos - 1}")
        if pos + length > total:
            # GSLzma.cpp drops a trailing short packet rather than failing.
            break
        packets.append(Packet(idx, kind, pos, length, path))
        pos += length
        idx += 1

    return Container(
        crc=crc,
        serial=serial,
        state_version=state_version,
        state_size=state_size,
        screenshot_width=shot_w,
        screenshot_height=shot_h,
        screenshot_size=shot_size,
        regs_data=regs_data,
        packets=packets,
        data=buf,
    )


def decode_priv_regs(regs: bytes) -> dict[str, Any]:
    """Decode the display-relevant part of GSPrivRegSet (GSRegs.h)."""

    def q(off: int) -> int:
        return struct.unpack_from("<Q", regs, off)[0]

    def bits(v: int, shift: int, n: int) -> int:
        return (v >> shift) & ((1 << n) - 1)

    out: dict[str, Any] = {"PMODE": q(0x00)}
    for i, base in enumerate((0x70, 0x90)):
        dispfb = q(base)
        display = q(base + 0x10)
        out[f"DISP{i}"] = {
            "DISPFB": {
                "FBP": bits(dispfb, 0, 9),
                "FBW": bits(dispfb, 9, 6),
                "PSM": bits(dispfb, 15, 5),
                "DBX": bits(dispfb, 32, 11),
                "DBY": bits(dispfb, 43, 11),
            },
            "DISPLAY": {
                "DX": bits(display, 0, 12),
                "DY": bits(display, 12, 11),
                "MAGH": bits(display, 23, 4),
                "MAGV": bits(display, 27, 2),
                "DW": bits(display, 32, 12),
                "DH": bits(display, 44, 11),
                # Visible area in pixels, per the GS DISPLAY register definition.
                "width_px": (bits(display, 32, 12) + 1) // (bits(display, 23, 4) + 1),
                "height_px": (bits(display, 44, 11) + 1) // (bits(display, 27, 2) + 1),
            },
        }
    return out


# --------------------------------------------------------------------------------------
# GIF / GS register decoding
# --------------------------------------------------------------------------------------

GIF_FLG_PACKED = 0
GIF_FLG_REGLIST = 1
GIF_FLG_IMAGE = 2
GIF_FLG_IMAGE2 = 3

REG_PRIM = 0x00
REG_RGBAQ = 0x01
REG_ST = 0x02
REG_UV = 0x03
REG_XYZF2 = 0x04
REG_XYZ2 = 0x05
REG_TEX0_1 = 0x06
REG_TEX0_2 = 0x07
REG_CLAMP_1 = 0x08
REG_CLAMP_2 = 0x09
REG_FOG = 0x0A
REG_XYZF3 = 0x0C
REG_XYZ3 = 0x0D
REG_AD = 0x0E
REG_NOP = 0x0F
REG_TEX1_1 = 0x14
REG_TEX1_2 = 0x15
REG_TEX2_1 = 0x16
REG_TEX2_2 = 0x17
REG_XYOFFSET_1 = 0x18
REG_XYOFFSET_2 = 0x19
REG_PRMODECONT = 0x1A
REG_PRMODE = 0x1B
REG_TEXCLUT = 0x1C
REG_SCANMSK = 0x22
REG_TEXA = 0x3B
REG_FOGCOL = 0x3D
REG_TEXFLUSH = 0x3F
REG_SCISSOR_1 = 0x40
REG_SCISSOR_2 = 0x41
REG_ALPHA_1 = 0x42
REG_ALPHA_2 = 0x43
REG_COLCLAMP = 0x46
REG_TEST_1 = 0x47
REG_TEST_2 = 0x48
REG_FBA_1 = 0x4A
REG_FBA_2 = 0x4B
REG_FRAME_1 = 0x4C
REG_FRAME_2 = 0x4D
REG_ZBUF_1 = 0x4E
REG_ZBUF_2 = 0x4F
REG_BITBLTBUF = 0x50
REG_TRXPOS = 0x51
REG_TRXREG = 0x52
REG_TRXDIR = 0x53

PRIM_NAMES = {
    0: "point",
    1: "line",
    2: "line_strip",
    3: "triangle",
    4: "triangle_strip",
    5: "triangle_fan",
    6: "sprite",
    7: "invalid",
}

PSM_NAMES = {
    0x00: "PSMCT32", 0x01: "PSMCT24", 0x02: "PSMCT16", 0x0A: "PSMCT16S",
    0x13: "PSMT8", 0x14: "PSMT4", 0x1B: "PSMT8H", 0x24: "PSMT4HL",
    0x2C: "PSMT4HH", 0x30: "PSMZ32", 0x31: "PSMZ24", 0x32: "PSMZ16",
    0x3A: "PSMZ16S",
}

# Vertices required to complete each primitive, and whether the primitive is a
# running strip/fan (which retains vertices after a draw kick).
PRIM_VERTS = {0: 1, 1: 2, 2: 2, 3: 3, 4: 3, 5: 3, 6: 2, 7: 0}

# Drawing-environment registers that PCSX2 tracks with m_dirty_gs_regs (GSState.h,
# DIRTY_REG_*). A batch is only broken when one of these actually CHANGES value
# relative to the environment as of the previous flush -- this mirrors
# GSState::CheckFlushes(), which flushes on a vertex kick iff m_dirty_gs_regs != 0
# and vertices are queued. Registers marked with a context index only dirty the
# state when they belong to the context PRIM currently selects.
DIRTY_REGS_CTX = {
    REG_TEX0_1: 0, REG_TEX0_2: 1,
    REG_TEX1_1: 0, REG_TEX1_2: 1,
    REG_CLAMP_1: 0, REG_CLAMP_2: 1,
    REG_SCISSOR_1: 0, REG_SCISSOR_2: 1,
    REG_ALPHA_1: 0, REG_ALPHA_2: 1,
    REG_TEST_1: 0, REG_TEST_2: 1,
    REG_FRAME_1: 0, REG_FRAME_2: 1,
    REG_ZBUF_1: 0, REG_ZBUF_2: 1,
    REG_XYOFFSET_1: 0, REG_XYOFFSET_2: 1,
    REG_FBA_1: 0, REG_FBA_2: 1,
}
DIRTY_REGS_GLOBAL = frozenset({REG_TEXA, REG_COLCLAMP, REG_SCANMSK, REG_FOGCOL})


def _bits(v: int, shift: int, n: int) -> int:
    return (v >> shift) & ((1 << n) - 1)


def _f32(v: int) -> float:
    return struct.unpack("<f", struct.pack("<I", v & 0xFFFFFFFF))[0]


def decode_prim(v: int) -> dict[str, int]:
    return {
        "PRIM": _bits(v, 0, 3),
        "IIP": _bits(v, 3, 1),
        "TME": _bits(v, 4, 1),
        "FGE": _bits(v, 5, 1),
        "ABE": _bits(v, 6, 1),
        "AA1": _bits(v, 7, 1),
        "FST": _bits(v, 8, 1),
        "CTXT": _bits(v, 9, 1),
        "FIX": _bits(v, 10, 1),
    }


def decode_tex0(v: int) -> dict[str, Any]:
    psm = _bits(v, 20, 6)
    return {
        "TBP0": _bits(v, 0, 14),
        "TBW": _bits(v, 14, 6),
        "PSM": psm,
        "PSM_name": PSM_NAMES.get(psm, f"0x{psm:02x}"),
        "TW": _bits(v, 26, 4),
        "TH": _bits(v, 30, 4),
        "width": 1 << _bits(v, 26, 4),
        "height": 1 << _bits(v, 30, 4),
        "TCC": _bits(v, 34, 1),
        "TFX": _bits(v, 35, 2),
        "CBP": _bits(v, 37, 14),
        "CPSM": _bits(v, 51, 4),
        "CSM": _bits(v, 55, 1),
        "CSA": _bits(v, 56, 5),
        "CLD": _bits(v, 61, 3),
    }


def decode_frame(v: int) -> dict[str, Any]:
    psm = _bits(v, 24, 6)
    return {
        "FBP": _bits(v, 0, 9),
        "FBW": _bits(v, 16, 6),
        "PSM": psm,
        "PSM_name": PSM_NAMES.get(psm, f"0x{psm:02x}"),
        "FBMSK": _bits(v, 32, 32),
    }


def decode_scissor(v: int) -> dict[str, int]:
    return {
        "SCAX0": _bits(v, 0, 11),
        "SCAX1": _bits(v, 16, 11),
        "SCAY0": _bits(v, 32, 11),
        "SCAY1": _bits(v, 48, 11),
    }


def decode_test(v: int) -> dict[str, int]:
    return {
        "ATE": _bits(v, 0, 1), "ATST": _bits(v, 1, 3), "AREF": _bits(v, 4, 8),
        "AFAIL": _bits(v, 12, 2), "DATE": _bits(v, 14, 1), "DATM": _bits(v, 15, 1),
        "ZTE": _bits(v, 16, 1), "ZTST": _bits(v, 17, 2),
    }


def decode_alpha(v: int) -> dict[str, int]:
    return {
        "A": _bits(v, 0, 2), "B": _bits(v, 2, 2), "C": _bits(v, 4, 2),
        "D": _bits(v, 6, 2), "FIX": _bits(v, 32, 8),
    }


@dataclass
class Vertex:
    x: int  # raw 12.4 fixed, framebuffer-absolute
    y: int
    z: int
    f: int
    u: float  # UV (FST=1) in texels, or S (FST=0)
    v: float
    q: float
    r: int
    g: int
    b: int
    a: int
    adc: int


@dataclass
class Context:
    tex0: int = 0
    tex1: int = 0
    clamp: int = 0
    xyoffset: int = 0
    scissor: int = 0
    alpha: int = 0
    test: int = 0
    frame: int = 0
    zbuf: int = 0


@dataclass
class DrawCall:
    index: int
    packet_index: int
    prim_raw: int
    prim: dict[str, int]
    vertices: list[Vertex]
    ctx_index: int
    ctx: Context
    texa: int
    vsync_index: int
    break_reason: str


class GSStateMachine:
    """GS state tracker sufficient to enumerate draw calls.

    Batching follows GSState::CheckFlushes(): the queued primitives are emitted as
    one draw call when a vertex kick occurs while at least one drawing-environment
    register differs from its value at the previous flush.
    """

    def __init__(self) -> None:
        self.prim_raw = 0
        self.prim = decode_prim(0)
        self.prmodecont = 1
        self.prmode_raw = 0
        self.ctx = [Context(), Context()]
        self.texa = 0
        self.rgba = (0, 0, 0, 0)
        self.stq = (0.0, 0.0, 1.0)
        self.uv = (0.0, 0.0)
        self.fog = 0
        self.queue: list[Vertex] = []
        self.pending: list[Vertex] = []
        self.draws: list[DrawCall] = []
        self.vsync_index = 0
        self.packet_index = 0
        self.image_qwords = 0
        self.unknown_regs: dict[int, int] = {}
        # env / prev_env implement m_env vs m_prev_env + m_dirty_gs_regs.
        self.env: dict[int, int] = {}
        self.prev_env: dict[int, int] = {}
        self.dirty: set[str] = set()
        # State captured at the moment the current batch started drawing.
        self._batch_prim_raw = 0
        self._batch_prim = decode_prim(0)
        self._batch_ctx = Context()
        self._batch_ctx_index = 0
        self._batch_texa = 0
        self._batch_packet = 0

    # -- batching ---------------------------------------------------------------

    def _snapshot(self) -> None:
        idx = self.prim["CTXT"]
        self._batch_prim_raw = self.prim_raw
        self._batch_prim = dict(self.prim)
        self._batch_ctx = Context(**vars(self.ctx[idx]))
        self._batch_ctx_index = idx
        self._batch_texa = self.texa
        self._batch_packet = self.packet_index

    def flush(self, reason: str) -> None:
        if not self.queue:
            self.prev_env = dict(self.env)
            self.dirty.clear()
            return
        self.draws.append(
            DrawCall(
                index=len(self.draws),
                packet_index=self._batch_packet,
                prim_raw=self._batch_prim_raw,
                prim=dict(self._batch_prim),
                vertices=self.queue,
                ctx_index=self._batch_ctx_index,
                ctx=self._batch_ctx,
                texa=self._batch_texa,
                vsync_index=self.vsync_index,
                break_reason=reason,
            )
        )
        self.queue = []
        self.prev_env = dict(self.env)
        self.dirty.clear()

    def _mark(self, addr: int, value: int) -> None:
        """Record a drawing-environment write and update the dirty set."""
        self.env[addr] = value
        ctx = DIRTY_REGS_CTX.get(addr)
        if ctx is not None and ctx != self.prim["CTXT"]:
            return
        key = f"ctx{ctx}:{addr:02x}" if ctx is not None else f"g:{addr:02x}"
        if self.prev_env.get(addr) != value:
            self.dirty.add(key)
        else:
            self.dirty.discard(key)

    # -- register writes --------------------------------------------------------

    def write_reg(self, addr: int, value: int) -> None:
        if addr == REG_PRIM:
            # ApplyPRIM: with PRMODECONT.AC == 0 only the PRIM field is taken from
            # this write; the mode bits keep whatever PRMODE last set.
            if self.prmodecont:
                self.prim_raw = value & 0x7FF
            else:
                self.prim_raw = (self.prim_raw & ~0x7) | (value & 0x7)
            value = self.prim_raw
            self.prim = decode_prim(value)
            self.env[REG_PRIM] = value
            if self.prev_env.get(REG_PRIM) != value:
                self.dirty.add("g:prim")
            else:
                self.dirty.discard("g:prim")
            # ApplyPRIM drops any partially accumulated vertices.
            self.pending = []
            return
        if addr in (REG_PRMODECONT, REG_PRMODE):
            if addr == REG_PRMODECONT:
                self.prmodecont = value & 1
                self.env[addr] = value
                return
            # GIFRegHandlerPRMODE: ignored entirely while PRMODECONT.AC == 1.
            self.prmode_raw = value
            if self.prmodecont:
                return
            keep = self.prim_raw & 0x7
            self.prim_raw = (value & 0x7FF & ~0x7) | keep
            self.prim = decode_prim(self.prim_raw)
            self.env[REG_PRIM] = self.prim_raw
            if self.prev_env.get(REG_PRIM) != self.prim_raw:
                self.dirty.add("g:prim")
            else:
                self.dirty.discard("g:prim")
            return
        if addr == REG_RGBAQ:
            self.rgba = (
                _bits(value, 0, 8),
                _bits(value, 8, 8),
                _bits(value, 16, 8),
                _bits(value, 24, 8),
            )
            self.stq = (self.stq[0], self.stq[1], _f32(_bits(value, 32, 32)))
            return
        if addr == REG_ST:
            self.stq = (_f32(_bits(value, 0, 32)), _f32(_bits(value, 32, 32)), self.stq[2])
            return
        if addr == REG_UV:
            self.uv = (_bits(value, 0, 14) / 16.0, _bits(value, 32, 14) / 16.0)
            return
        if addr == REG_FOG:
            self.fog = _bits(value, 56, 8)
            return
        if addr in (REG_XYZ2, REG_XYZ3):
            self.kick(
                _bits(value, 0, 16), _bits(value, 16, 16), _bits(value, 32, 32),
                self.fog, adc=1 if addr == REG_XYZ3 else 0,
            )
            return
        if addr in (REG_XYZF2, REG_XYZF3):
            self.kick(
                _bits(value, 0, 16), _bits(value, 16, 16), _bits(value, 32, 24),
                _bits(value, 56, 8), adc=1 if addr == REG_XYZF3 else 0,
            )
            return

        if addr in DIRTY_REGS_CTX or addr in DIRTY_REGS_GLOBAL:
            self._mark(addr, value)
        c = self.ctx
        if addr == REG_TEX0_1: c[0].tex0 = value
        elif addr == REG_TEX0_2: c[1].tex0 = value
        elif addr == REG_TEX1_1: c[0].tex1 = value
        elif addr == REG_TEX1_2: c[1].tex1 = value
        elif addr == REG_CLAMP_1: c[0].clamp = value
        elif addr == REG_CLAMP_2: c[1].clamp = value
        elif addr == REG_XYOFFSET_1: c[0].xyoffset = value
        elif addr == REG_XYOFFSET_2: c[1].xyoffset = value
        elif addr == REG_SCISSOR_1: c[0].scissor = value
        elif addr == REG_SCISSOR_2: c[1].scissor = value
        elif addr == REG_ALPHA_1: c[0].alpha = value
        elif addr == REG_ALPHA_2: c[1].alpha = value
        elif addr == REG_TEST_1: c[0].test = value
        elif addr == REG_TEST_2: c[1].test = value
        elif addr == REG_FRAME_1: c[0].frame = value
        elif addr == REG_FRAME_2: c[1].frame = value
        elif addr == REG_ZBUF_1: c[0].zbuf = value
        elif addr == REG_ZBUF_2: c[1].zbuf = value
        elif addr == REG_TEXA: self.texa = value
        elif addr in (REG_NOP, REG_TEXFLUSH):
            pass
        elif addr not in (
            REG_TEX2_1, REG_TEX2_2, REG_TEXCLUT, REG_SCANMSK, REG_FOGCOL,
            REG_COLCLAMP, REG_FBA_1, REG_FBA_2, REG_BITBLTBUF, REG_TRXPOS,
            REG_TRXREG, REG_TRXDIR,
            0x34, 0x35, 0x36, 0x37,  # MIPTBP1/2
            0x44, 0x45, 0x49,        # DIMX, DTHE, PABE
            0x54, 0x60, 0x61, 0x62,  # HWREG, SIGNAL, FINISH, LABEL
        ):
            self.unknown_regs[addr] = self.unknown_regs.get(addr, 0) + 1

    # -- vertex kicks -----------------------------------------------------------

    def kick(self, x: int, y: int, z: int, f: int, adc: int) -> None:
        # CheckFlushes(): a pending state change closes the batch built so far.
        if self.dirty and self.queue:
            self.flush("dirty:" + ",".join(sorted(self.dirty)))
        st_or_uv = self.uv if self.prim["FST"] else (self.stq[0], self.stq[1])
        vtx = Vertex(
            x=x, y=y, z=z, f=f,
            u=st_or_uv[0], v=st_or_uv[1], q=self.stq[2],
            r=self.rgba[0], g=self.rgba[1], b=self.rgba[2], a=self.rgba[3],
            adc=adc,
        )
        self.pending.append(vtx)

        need = PRIM_VERTS.get(self.prim["PRIM"], 0)
        if need == 0:
            self.pending = []
            return
        if len(self.pending) < need:
            return
        if adc:
            # XYZ3/XYZF3 (or the packed ADC bit) suppresses the drawing kick.
            self._trim_pending(need)
            return
        if not self.queue:
            self._snapshot()
        self.queue.extend(self.pending[-need:])
        self._trim_pending(need)

    def _trim_pending(self, need: int) -> None:
        p = self.prim["PRIM"]
        if p in (2, 4):  # line strip / triangle strip: slide window
            self.pending = self.pending[-(need - 1):]
        elif p == 5:  # triangle fan: keep first + last
            self.pending = [self.pending[0], self.pending[-1]]
        else:
            self.pending = []


# --------------------------------------------------------------------------------------
# GIF packet walker
# --------------------------------------------------------------------------------------


def process_transfer(sm: GSStateMachine, buf: bytes, start: int, length: int,
                     stats: dict[str, int]) -> None:
    pos = start
    end = start + length
    while pos + 16 <= end:
        lo, hi = struct.unpack_from("<QQ", buf, pos)
        pos += 16
        nloop = _bits(lo, 0, 15)
        eop = _bits(lo, 15, 1)
        pre = _bits(lo, 46, 1)
        prim = _bits(lo, 47, 11)
        flg = _bits(lo, 58, 2)
        nreg = _bits(lo, 60, 4)
        if nreg == 0:
            nreg = 16
        stats["giftags"] += 1

        if pre:
            sm.write_reg(REG_PRIM, prim)

        if nloop == 0:
            if eop:
                continue
            continue

        if flg == GIF_FLG_PACKED:
            regs = [(hi >> (4 * i)) & 0xF for i in range(16)][:nreg]
            need = nloop * nreg * 16
            if pos + need > end:
                need = ((end - pos) // 16) * 16
            i = 0
            count = need // 16
            while i < count:
                r = regs[i % nreg]
                qlo, qhi = struct.unpack_from("<QQ", buf, pos + i * 16)
                _apply_packed(sm, r, qlo, qhi)
                i += 1
            pos += count * 16
        elif flg == GIF_FLG_REGLIST:
            regs = [(hi >> (4 * i)) & 0xF for i in range(16)][:nreg]
            total = nloop * nreg
            words = (total + 1) // 2 * 2  # padded to a whole qword
            need = words * 8
            if pos + need > end:
                need = (end - pos) // 8 * 8
                words = need // 8
            for i in range(min(total, words)):
                val = struct.unpack_from("<Q", buf, pos + i * 8)[0]
                sm.write_reg(regs[i % nreg], val)
            pos += words * 8
        else:  # IMAGE / IMAGE2 -- raw local-memory transfer payload, skipped
            need = nloop * 16
            if pos + need > end:
                need = ((end - pos) // 16) * 16
            sm.image_qwords += need // 16
            stats["image_qwords"] += need // 16
            pos += need


def _apply_packed(sm: GSStateMachine, r: int, lo: int, hi: int) -> None:
    """PACKED-mode field extraction (GS User's Manual PACKED descriptors)."""
    if r == REG_PRIM:
        sm.write_reg(REG_PRIM, _bits(lo, 0, 11))
    elif r == REG_RGBAQ:
        sm.rgba = (_bits(lo, 0, 8), _bits(lo, 32, 8), _bits(hi, 0, 8), _bits(hi, 32, 8))
        # Q comes from the internal Q register, last written by ST.
    elif r == REG_ST:
        sm.stq = (_f32(_bits(lo, 0, 32)), _f32(_bits(lo, 32, 32)), _f32(_bits(hi, 0, 32)))
    elif r == REG_UV:
        sm.uv = (_bits(lo, 0, 14) / 16.0, _bits(lo, 32, 14) / 16.0)
    elif r == REG_XYZF2:
        adc = _bits(hi, 47, 1)
        sm.kick(_bits(lo, 0, 16), _bits(lo, 32, 16), _bits(hi, 4, 24), _bits(hi, 36, 8), adc)
    elif r == REG_XYZ2:
        adc = _bits(hi, 47, 1)
        sm.kick(_bits(lo, 0, 16), _bits(lo, 32, 16), _bits(hi, 0, 32), sm.fog, adc)
    elif r == REG_XYZF3:
        sm.kick(_bits(lo, 0, 16), _bits(lo, 32, 16), _bits(hi, 4, 24), _bits(hi, 36, 8), 1)
    elif r == REG_XYZ3:
        sm.kick(_bits(lo, 0, 16), _bits(lo, 32, 16), _bits(hi, 0, 32), sm.fog, 1)
    elif r == REG_FOG:
        sm.fog = _bits(hi, 36, 8)
    elif r == REG_AD:
        addr = _bits(hi, 0, 8)
        sm.write_reg(addr, lo)
    elif r == REG_NOP:
        pass
    else:
        # TEX0_1..CLAMP_2, XYZF3, XYZ3 in PACKED mode use the 64-bit value in lo.
        sm.write_reg(r, lo)


# --------------------------------------------------------------------------------------
# Analysis / serialisation
# --------------------------------------------------------------------------------------


def xyoffset_of(value: int) -> tuple[float, float]:
    return _bits(value, 0, 16) / 16.0, _bits(value, 32, 16) / 16.0


def draw_to_json(d: DrawCall, include_vertices: bool, max_vertices: int) -> dict[str, Any]:
    ofx, ofy = xyoffset_of(d.ctx.xyoffset)
    xs = [v.x / 16.0 - ofx for v in d.vertices]
    ys = [v.y / 16.0 - ofy for v in d.vertices]
    tex0 = decode_tex0(d.ctx.tex0)
    rec: dict[str, Any] = {
        "index": d.index,
        "packet_index": d.packet_index,
        "vsync_index": d.vsync_index,
        "break_reason": d.break_reason,
        "prim": PRIM_NAMES.get(d.prim["PRIM"], "?"),
        "prim_flags": d.prim,
        "vertex_count": len(d.vertices),
        "context": d.ctx_index,
        "xyoffset": {"OFX": ofx, "OFY": ofy},
        "screen_bbox": {
            "xmin": min(xs), "xmax": max(xs), "ymin": min(ys), "ymax": max(ys),
        },
        "screen_centroid": {"x": sum(xs) / len(xs), "y": sum(ys) / len(ys)},
        "tex0": tex0,
        "texa": f"0x{d.texa:016x}",
        "frame": decode_frame(d.ctx.frame),
        "scissor": decode_scissor(d.ctx.scissor),
        "test": decode_test(d.ctx.test),
        "alpha": decode_alpha(d.ctx.alpha) if d.prim["ABE"] else None,
        "z_range": {"min": min(v.z for v in d.vertices), "max": max(v.z for v in d.vertices)},
        "rgba_first": [d.vertices[0].r, d.vertices[0].g, d.vertices[0].b, d.vertices[0].a],
    }
    if include_vertices:
        n = len(d.vertices) if max_vertices <= 0 else min(len(d.vertices), max_vertices)
        rec["vertices_truncated"] = n < len(d.vertices)
        rec["vertices"] = [
            {
                "x": round(v.x / 16.0 - ofx, 4),
                "y": round(v.y / 16.0 - ofy, 4),
                "z": v.z,
                "u": round(v.u, 5),
                "v": round(v.v, 5),
                "q": round(v.q, 6),
                "rgba": [v.r, v.g, v.b, v.a],
            }
            for v in d.vertices[:n]
        ]
    return rec


def inspect(path: Path, include_vertices: bool, max_vertices: int,
            max_draws: int) -> dict[str, Any]:
    buf, decomp = load_dump_bytes(path)
    container = parse_container(buf)

    sm = GSStateMachine()
    stats = {"giftags": 0, "image_qwords": 0}
    packet_counts = {"transfer": 0, "vsync": 0, "readfifo2": 0, "registers": 0}
    transfer_bytes_by_path: dict[str, int] = {}
    vsync_draw_marks: list[int] = []

    for pkt in container.packets:
        sm.packet_index = pkt.index
        if pkt.kind == PACKET_TRANSFER:
            packet_counts["transfer"] += 1
            name = TRANSFER_PATH_NAMES.get(pkt.path or 4, "?")
            transfer_bytes_by_path[name] = transfer_bytes_by_path.get(name, 0) + pkt.length
            process_transfer(sm, container.data, pkt.offset, pkt.length, stats)
        elif pkt.kind == PACKET_VSYNC:
            packet_counts["vsync"] += 1
            sm.flush("vsync")
            vsync_draw_marks.append(len(sm.draws))
            sm.vsync_index += 1
        elif pkt.kind == PACKET_READFIFO2:
            packet_counts["readfifo2"] += 1
            sm.flush("readfifo2")
        elif pkt.kind == PACKET_REGISTERS:
            packet_counts["registers"] += 1
    sm.flush("eof")

    draws = sm.draws
    prim_hist: dict[str, int] = {}
    for d in draws:
        k = PRIM_NAMES.get(d.prim["PRIM"], "?")
        prim_hist[k] = prim_hist.get(k, 0) + 1

    emitted = draws if max_draws <= 0 else draws[:max_draws]
    return {
        "tool": "gs_dump_inspect",
        "tool_version": 1,
        "source_name": path.name,
        "source_bytes": path.stat().st_size,
        "decompressed_bytes": len(buf),
        "decompression": decomp,
        "container": {
            "crc": f"0x{container.crc:08x}",
            "serial": container.serial,
            "state_version": container.state_version,
            "state_size": container.state_size,
            "screenshot": {
                "width": container.screenshot_width,
                "height": container.screenshot_height,
                "bytes": container.screenshot_size,
            },
            "packet_count": len(container.packets),
            "packet_counts": packet_counts,
            "transfer_bytes_by_path": dict(sorted(transfer_bytes_by_path.items())),
        },
        "priv_regs": decode_priv_regs(container.regs_data),
        "gif": {
            "giftags": stats["giftags"],
            "image_qwords": stats["image_qwords"],
            "unknown_regs": {f"0x{k:02x}": v for k, v in sorted(sm.unknown_regs.items())},
        },
        "draw_call_count": len(draws),
        "draws_emitted": len(emitted),
        "prim_histogram": dict(sorted(prim_hist.items())),
        "vsync_draw_marks": vsync_draw_marks,
        "draws": [draw_to_json(d, include_vertices, max_vertices) for d in emitted],
    }


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dump", type=Path, help="path to a .gs / .gs.xz / .gs.zst dump")
    ap.add_argument("out", type=Path, help="path to write JSON to ('-' for stdout)")
    ap.add_argument("--no-vertices", action="store_true",
                    help="omit per-vertex arrays (bounding boxes are always emitted)")
    ap.add_argument("--max-vertices", type=int, default=0,
                    help="cap vertices emitted per draw (0 = no cap)")
    ap.add_argument("--max-draws", type=int, default=0,
                    help="cap draws emitted (0 = no cap); counts are still full")
    args = ap.parse_args(argv)

    result = inspect(args.dump, not args.no_vertices, args.max_vertices, args.max_draws)
    text = json.dumps(result, indent=1, sort_keys=False)
    if str(args.out) == "-":
        sys.stdout.write(text)
    else:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    sys.stderr.write(
        f"{args.dump.name}: decomp={result['decompression']} "
        f"packets={result['container']['packet_count']} "
        f"draws={result['draw_call_count']} prims={result['prim_histogram']}\n"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
