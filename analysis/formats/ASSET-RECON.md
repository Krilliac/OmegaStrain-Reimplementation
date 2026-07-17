# Omega Strain asset-format reconnaissance

## Scope and method

This is aggregate structural research over the owner's NTSC-U `SCUS-97264` disc extraction.
No texture, mesh, audio, script, or other proprietary payload is copied into this document or
the accompanying JSON. `tools/fingerprint_assets.py` seeks directly into the private files and
emits only counts, header fields, validation results, and cross-file relationships.

The formats below are passive asset data and may be decoded by independently
written native importers. Any executable-side MIPS tracing mentioned here is an
offline research technique only; no retail instructions or executable script
cells enter the shipping runtime.

The scanner visited all 273 top-level HOGs, their 32,351 directory entries, and every one of the
6,677 embedded `.HOG` entries. All 6,677 embedded candidates parsed as the same HOG directory
layout. Of these, 6,176 end before their containing span and use 16-2,032 bytes of zero padding;
501 occupy their span exactly. A runtime HOG reader therefore must receive the containing span
length and accept a validated all-zero tail instead of requiring the final payload offset to be
the physical end of the span.

## Corpus results

| Format | Validated entries | Confirmed structural result |
| --- | ---: | --- |
| TDX | 15,248 | Every header begins with little-endian version word 5. |
| SKM | 4,219 | Every file satisfies the chunk/qword size formula below; version byte is 3. |
| SKL | 1,261 | Every file is ASCII line-oriented data. |
| VAG | 8,665 | Every file has a conventional `VAGp` header and 16-byte-aligned ADPCM payload. |
| LPD | 862 | Every file satisfies the 22-word count-table formula below. |
| PAR | 679 | Every file is zero-padded ASCII with a version comment on line one. |
| COL | 7,036 | Every file starts `COL`, reports a 48-byte header, and is 16-byte aligned. |
| VUM | 7,036 | Every file starts `VUMS` and is 16-byte aligned. |
| VPK | 85 | Every file has the custom little-endian `VPK ` FourCC and a 2,048-byte header field. |
| POP | 18 | All 18 scanned level terrain sequences parse and land exactly on `GOB:`. |

## First MINSK scene: confirmed dependency chain

The minimum useful scene path is now concrete:

1. `DATA.POP` contains 299 `TER:` records. Each record is a little-endian kind word, an index
   word, and an ASCII NUL-terminated name padded to four bytes. Parsing all 299 lands exactly at
   the next `GOB:` section. Alignment bytes are skipped, not assumed to be zero:
   149 of the 299 MINSK records and 4,144 of 5,351 records corpus-wide contain
   at least one nonzero alignment byte.
2. All 299 terrain names resolve to `DATA.HOG` members. `DATA.HOG` contains 301 members total, so
   two cell containers are not referenced by the terrain list.
3. All 301 members are valid embedded HOGs, all sector-padded, and every one contains exactly one
   `.COL` plus one `.VUM`. All 301 COLs use header version byte 5; all 301 VUM size fields equal
   their containing asset span.
4. `MAPVUM.HOG` contains five exact-span embedded HOGs, again one COL plus one VUM each. All five
   use COL version byte 5 and exact VUM size fields.
5. `MAPTEX.HOG` contains two version-5 TDX textures.

The scanner finds these aligned tag markers in the MINSK POP, in order. Only
the `TER:` record body is decoded; the later markers are structural leads, not
yet confirmed section boundaries:

```text
TER GOB SND ACL INL NPC WPN PLR SKY PNT DIR ENV NOD GEN GRP BOX FIR CAM INV BUG
```

Across all 18 level POPs, the same terrain parser accepts 5,351 records. Every terrain name has a
matching `DATA.HOG` member, while the HOG directories contain 44 additional cell members. This
cross-level result makes the MINSK relationship much stronger than a one-file guess.

The first canonical native asset value is therefore a `LevelManifestIR`, not guessed geometry.
Its stateless retail adapter resolves each POP terrain record to an owned `DATA.HOG` source
entry while storing the common archive locator once and preserving the two uninterpreted numeric
fields. Geometry, placement, visibility, and transforms remain absent until their later sections
are independently established.

The only standalone `.MAP` on the disc is 1,138 bytes of ASCII whose contents
resemble a character-code mapping rather than geometry. Current evidence points
to the POP plus nested COL/VUM chain as the world/map geometry path.

## Per-format findings

### TDX texture

The common 16-byte prefix is eight little-endian words:

```text
u16 version, flags, width, height, bits_per_pixel, gs_psm, planes, storage_units
```

All 15,248 files report version 5. The observed bit-depth/PSM pairs are 4/`0x14` (9,879),
8/`0x13` (5,172), 24/`0x01` (68), and 32/`0x00` (129), matching the expected PS2 GS indexed and
direct-color storage modes. For 13,472 files, `storage_units * 256` equals
`width * height * bits_per_pixel / 8`.

The word at offset `0x38`, plus the 64-byte prefix, reaches either the exact asset end or a zero
tail in 15,162 files. It exceeds the directory span in 62 files and is followed by additional
nonzero data in 24. Treat it as a primary-block size field, not a universally authoritative file
length. Palette layout, GS swizzle, mip/animation blocks, and the 86 exceptions still need a
dedicated decoder.

### SKM mesh

SKM has no FourCC. Its compact leading table is nevertheless fully self-consistent:

```text
u8 chunk_count
u8 version                         # always 3 in this corpus
repeat chunk_count:
    u8 qword_count
    u8 secondary_count             # semantic name not yet proven
header_size = align16(2 + 2 * chunk_count)
asset_size = header_size + 16 * sum(qword_count)
```

All 4,219 files satisfy that formula: 2,732 occupy their directory span exactly and 1,487 have an
all-zero tail. Chunk counts range from 1 to 61, qword counts from 4 to 55, and secondary counts
from 1 to 30. The table is loader-ready, but the qwords' VIF/vertex semantics and bone weighting
are not yet decoded.

### SKL skeleton/loadout list

All 1,261 SKLs are ASCII. There are 1,243 CRLF files and 18 CR-only files, so the parser must
support both. Files contain 10-60 lines; 1,212 begin with the common `BONENOSCALE` profile and 49
use other profile labels. The evidence supports a line-oriented skeleton/loadout reference list,
not yet a decoded transform hierarchy. Strip only trailing NUL padding; do not assume CRLF.

### VAG audio

All 8,665 entries match the conventional big-endian `VAGp` header layout:

```text
0x00  char[4] magic = "VAGp"
0x04  be32 version
0x08  be32 reserved = 0
0x0C  be32 ADPCM data bytes
0x10  be32 sample rate = 22050
...
0x30  ADPCM frames
```

Every declared data length is a multiple of the 16-byte PS ADPCM frame size. Fifty-three assets
end exactly after `48 + data_bytes`; 8,612 use 16-2,032 bytes of zero padding. Version values are
0 (8,497), 4 (166), and `0x20` (2). This is the most immediately reusable non-container decoder.

### LPD dialogue/lip data

Every LPD starts with little-endian word count 22. The remaining 21 header words are per-track
counts, and all 862 files satisfy:

```text
header_bytes = 22 * 4
logical_bytes = header_bytes + 4 * sum(header_words[1:22])
```

One file is exact; 861 have an all-zero tail. In the same HOG directory, 852 of 862 LPD basenames
have a VAG companion. The structure and pairing strongly suggest dialogue lip/pose curves, but the
meaning of the 21 tracks and each four-byte sample is still inferred rather than decoded.

### PAR particle parameters

All 679 PAR entries are ASCII, CRLF, NUL-padded, and begin with a numeric `;version` comment.
Observed versions range from 1.3 through 2.1. A tolerant text parser that strips trailing NULs,
separates values from semicolon comments, and dispatches by declared version is sufficient for
initial ingestion. Field semantics and compatibility defaults still require implementation.

### COL and VUM geometry

All 7,036 COL files begin `COL`; 7,033 use format byte 5 and three Tokyo map models use byte 3.
The little-endian word at offset `0x08` is 48 in every file, and every span is 16-byte aligned.
The likely collision semantics are inferred from the extension and pairing, while the magic,
version distribution, header size, and alignment are confirmed.

All 7,036 VUM files begin `VUMS`. The word at offset `0x58` equals the directory span in 6,989
files. Forty-seven files have additional nonzero data after that boundary, so this is a primary
section boundary rather than a universal file length. All 306 MINSK world/map VUMs are in the
exact group. Material tables, VU packet boundaries, vertex attributes, indices, and coordinate
conversion remain the critical first-scene reverse-engineering target.

### Wrappers and compression check

All 85 `.VPK` music entries begin with the custom little-endian `VPK ` FourCC, declare 2,048 at
offset `0x08`, and have 2,048-byte-aligned spans. This proves a sector-oriented wrapper, not its
codec; streamed PS ADPCM is plausible but remains unconfirmed.

The scanner checked the first bytes of 46,603 non-HOG asset spans for gzip, ZIP, bzip2, XZ, 7zip,
LZ4-frame, Zstandard, RNC1/2, LZSS, LZ77, and Yaz0 magic. It found zero. This rules out obvious
whole-file wrappers with those signatures, not internal compression or headerless codecs.

## Loader priority

For the first visible MINSK scene, implement in this order:

1. Span-aware nested HOG reading with verified zero-tail acceptance.
2. POP `TER:` parsing and name-to-`DATA.HOG` resolution.
3. COL/VUM headers, then VUM material and geometry packet decoding.
4. TDX version-5 texture upload, starting with 8-bit `0x13`, then 4-bit `0x14` plus CLUT/swizzle.
5. POP visibility/placement sections needed to assemble and cull cells.
6. SKM/SKL for characters and weapons.
7. PAR effects, then VAG audio and LPD lip curves.
8. VPK music after its payload codec is identified.

This ordering deliberately puts scene geometry and textures ahead of actors, effects, and audio.
The next high-value static/dynamic trace is the executable's `VUMS` consumer, especially reads of
the offset-`0x58` boundary and material-table offsets.

## Reproduce

```powershell
python -B .\tools\fingerprint_assets.py `
  .\private\extracted-disc `
  .\analysis\formats\asset-fingerprints.json
```

The JSON is deterministic for the same corpus and contains aggregate metadata only.
