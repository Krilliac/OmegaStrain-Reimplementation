# TDX texture storage

## Scope

This contract covers version-5 TDX block, transfer-plane, and palette storage. The native adapter
returns fully owned bytes in source order. It does not claim display-ready pixels, execute console
instructions, or retain retail offsets and input views.

The following remain deliberately unassigned: block purpose, mip/slice/frame meaning, texel row
order, swizzle, four-bit nibble order, indexed palette permutation, channel names, color space,
premultiplication, alpha scaling, sampler state, and GPU upload layout.

## Header and counted extent

The 64-byte prefix begins with these confirmed little-endian values:

```text
u16 version
u16 flags
u16 width
u16 height
u16 sample_bit_depth
u16 sample_format_code
u16 width_unit_word
u16 storage_unit_word
...
u16 block_count                 # +0x22
u16 primary_plane_count         # +0x24
u16 palette_plane_count         # +0x26
...
u32 block_stride                # +0x38
```

Every counted block has the same stride. The complete region is therefore
`64 + block_count * block_stride`, not `64 + block_stride`. This correction makes 24 formerly
opaque tails ordinary additional blocks. The physical corpus has 11,277 exact counted extents,
3,909 all-zero tails, no nonzero tails, and 62 spans ending inside the last primary plane.

The sample families are 4-bit indexed (`0x14`), 8-bit indexed (`0x13`), packed 24-bit (`0x01`),
and packed 32-bit (`0x00`). These header sample families are distinct from the transfer-element
format of each stored plane. In particular, indexed textures can carry storage rectangles whose
transfer elements are packed 32-bit words; those bytes must not be mistaken for RGBA pixels.

## Block storage

Each block begins with bounded relative tables for one to four primary objects and either one
palette object for indexed samples or none for direct samples. A transfer object is 0x60 bytes.
Only the independently established rectangle dimensions, transfer-element family, and data target
survive normalization.

For the complete corpus family:

- every object and data target is bounded by its block;
- all 17,898 physically complete primary planes exactly match their encoded rectangle size;
- all primary targets are unique, strictly ordered, and fill the meaningful suffix of a block;
- all 15,129 physically complete indexed blocks have one source-order four-byte palette plane;
- four-bit palettes contain 16 entries and an all-zero remainder in their 1,024-byte slot;
- eight-bit palettes contain 256 entries and fill that slot; and
- direct blocks contain no palette plane.

`TextureStorageIR` preserves top-level sample encoding, block order, plane order, transfer-element
encoding, rectangle dimensions, primary bytes, and four source bytes per palette entry. The fourth
source byte is copied unchanged, including observed values above 128. No RGBA naming or alpha
conversion is part of this decoder.

## Proven implicit-zero suffix family

Sixty-two TDX spans end 16, 32, 64, or 256 bytes before their sole primary plane ends. An
aggregate-only duplicate-prefix proof reduces them to 16 unique prefixes. Every prefix has a
complete corpus twin with an identical supplied prefix and an all-zero missing suffix; there are
no unmatched prefixes and no nonzero-suffix twins.

The semantic adapter therefore normalizes only the exact observed single-block/single-plane
signatures. All tables, objects, and any indexed palette must be complete; only the final primary
plane may cross the input end; its calculated end must equal the declared stride; and the shortage
must match the exact observed header, stride, base, object, data-target, transfer-rectangle,
palette, and aligned-size allowlist. The decoder synthesizes exactly those 4,112 proven zero bytes.
Every other short form is rejected. Reconstruction provenance stays in diagnostic aggregate counts
rather than renderer-neutral IR.

Reproduce the privacy-safe proof without emitting paths, names, hashes, or payload bytes:

```powershell
python -B tools/prove_tdx_zero_suffix.py private/extracted-disc
```

## Native validation

The aggregate verifier streams one asset at a time and discards each decoded value after updating
sanitized counters:

```powershell
build/msvc/Debug/omega_tool.exe asset-metadata-verify-tree private/extracted-disc
```

The target semantic baseline is 15,248 textures, 15,442 blocks, 17,960 primary planes, 15,190
palette blocks, 252 direct blocks, 1,510,240 palette entries, 62 implicit-zero textures, and 4,112
implicit-zero bytes, with zero errors. Exact owned primary-byte totals are recorded after the native
corpus run.
