# Command Center mission-select ring geometry

Status: **measured** from owner-private PCSX2 GS dumps (2026-07-25). Marker screen positions,
marker count, per-marker quad extents, ring-band extent, and the highlighted-marker discriminator
are proven. Ring *parameterisation* (a single ellipse with uniform angular spacing) is **disproven**.
As of the savestate sweep below, **rotation is also disproven**: two ring states with *different*
selected markers place all 22 markers at bit-identical screen positions, so the ring does not
rotate and selection is a per-marker alpha/texture swap over a fixed layout. Node-to-mission
mapping, ordering rule, and the number of markers one L1/R1 press advances remain unproven.

This document contains derived geometry only: screen coordinates, quad sizes, counts and angles.
No dump bytes, texture payloads, framebuffer contents, or screenshot pixels are reproduced here or
in version control. The `.gs.zst` captures, their `.png` companions, and the capture manifest live
under the git-ignored `analysis/DrawDump/` tree.

## Method

`tools/gs_dump_inspect.py` decompresses a PCSX2 GS dump and walks it into structured JSON. The
container and packet grammar are transcribed from the PCSX2 sources that define them, not from any
third-party description:

* `pcsx2/GS/GSDump.h` — `GSDumpHeader` (`#pragma pack(4)`, nine `u32`) and the format comment.
* `pcsx2/GS/GSDump.cpp` — `GSDumpBase::AddHeader` / `Transfer` / `ReadFIFO` / `VSync`, which define
  what the writer emits.
* `pcsx2/GS/GSLzma.cpp` — `GSDumpFile::ReadFile`, which defines how the reader consumes it,
  including the "drop a trailing short packet" rule.
* `pcsx2/GS/GSState.cpp` — `ApplyPRIM`, `GIFRegHandlerPRMODE`, `CheckFlushes`, used for the
  draw-call batching rule below.

Container, as implemented and confirmed against every capture in the corpus:

```text
u32   0xFFFFFFFF                fake CRC signalling a new-style header
u32   header_size               sizeof(GSDumpHeader) + serial bytes + screenshot bytes
u8[]  header_size               GSDumpHeader, then the serial text, then a BGRA preview image
u8[]  header.state_size         the real freeze/state blob
u8[]  8192                      GSPrivRegSet
      packets until EOF:
        id 0 Transfer   : u8 path, u32 size, size bytes of GIF data
        id 1 VSync      : u8 field
        id 2 ReadFIFO2  : u32 size
        id 3 Registers  : 8192 bytes
```

GIF data is decoded per GIFtag `NLOOP/EOP/PRE/PRIM/FLG/NREG/REGS`, with PACKED, REGLIST and IMAGE
modes handled separately. IMAGE payloads (texture uploads) are counted and skipped, never decoded.
Vertex kicks come from `XYZ2`/`XYZF2` (and the non-drawing `XYZ3`/`XYZF3`, plus the PACKED `ADC`
bit at bit 111). Screen positions are computed as `X/16 − OFX/16` and `Y/16 − OFY/16` in floating
point from the 12.4 fixed-point vertex registers and the active context's `XYOFFSET`, so the
sub-pixel fraction survives and every coordinate below is in framebuffer pixels.

Draw-call batching mirrors `GSState::CheckFlushes()`: queued primitives are emitted as one draw
when a vertex kick occurs while any drawing-environment register (the `DIRTY_REG_*` set) differs
from its value at the previous flush. Context-indexed registers only dirty the state when their
index matches the context `PRIM` currently selects, and `PRMODE` writes are ignored while
`PRMODECONT.AC == 1`. This reproduces the emulator's own notion of "a draw" rather than inventing
one.

Decompression path actually used: the Python `zstandard` module (0.25.0) via `stream_reader`.
`GSDumpZst` (`GSDump.cpp`) compresses with `ZSTD_e_continue` and a single closing `ZSTD_e_end`, so
the frame header carries no decompressed size and a one-shot `ZstdDecompressor.decompress()` fails
with *"could not determine content size in frame header"* — verified. `read_across_frames=True` is
set defensively; the captures in this corpus are single-frame (both settings yield the same
17,771,597 bytes for `…003922`). The tool falls back to a `zstd` CLI if the module is absent (no
CLI is present on this machine), and handles `.xz` through stdlib `lzma` with concatenated-stream
handling.

`tools/measure_mission_ring_state.py` builds on that parser and does the ring extraction and the
across-capture comparison, so the two are never re-implemented by hand. Its rules, all of which
restate what the single-capture measurement above already established:

* a *ring quad* is a six-vertex `triangle_strip` with `TME=1` and `ABE=1`;
* among ring quads at least `--band-min-width` (120) px wide, the *band* is the most frequently
  repeated bounding box; a wide quad joins it only if all four corners sit within 8 px of that
  modal box. That admits the band's own per-draw jitter (up to 6.94 px on one edge in this corpus)
  and rejects the backdrop quads, whose closest edge to the modal box is 68.062 px away — more than
  eight times the tolerance;
* the *ring* is the single uninterrupted run of ring quads that contains the band draws, grown
  outwards from them until the neighbouring draw is not a ring quad;
* remaining quads inside that run are *marker* quads, clustered by centre at an 8 px radius, each
  cluster reported at the centre of its smallest quad;
* a quad is *opaque* when all six vertices carry alpha 127, and `opaque_area_ratio` is the largest
  opaque quad's area over the cluster's smallest quad's area — the doc's own
  highlight discriminator, expressed as a number.

Reproduce:

```text
python -B tools/gs_dump_inspect.py <dump.gs.zst> <out.json>
python -B tools/gs_dump_inspect.py <dump.gs.zst> <out.json> --no-vertices   # summary only
python -B tools/measure_mission_ring_state.py <dump-or-dir> [...] --json <out.json>
python -B tools/measure_mission_ring_state.py <dump-or-dir> --nodes         # per-node table
```

Output is deterministic: two runs over the same dump produce byte-identical JSON
(SHA-256 `cae4dc68…1afb1d` for the `…003922` capture, verified twice).

Run over the three original mission-select captures, `measure_mission_ring_state.py` reproduces the
single-capture measurement below exactly and independently: 22 clusters, 40 band quads in 9 distinct
boxes with the modal box occurring 18 times, 100 marker quads, marker 2 as the sole highlight at
`opaque_area_ratio` 3.082, and the same marker-0 frame-0 positions. That is a cross-check of the
tables in this document, not a new claim.

## Corpus and parser coverage

Every capture cohort in the private tree parses with no unknown packet types and no unknown GS
register addresses other than `0x7f`, which is not a defined GS register and is used by the title
as A+D padding.

| Capture (timestamp suffix) | Cohort | Packets | Frames | Draw calls | Primitives |
| --- | --- | ---: | ---: | ---: | --- |
| `…003922` | command-center-mission-select | 2,565 | 4 | 672 | 636 tri-strip, 36 sprite |
| `…003931` | command-center-mission-select | 2,564 | 4 | 672 | 636 tri-strip, 36 sprite |
| `…003940` | command-center-mission-select | 1,924 | 3 | 504 | 477 tri-strip, 27 sprite |
| `…001330` | character-creation-menu | 1,348 | 4 | 980 | 924 tri-strip, 56 sprite |
| `…001359` | character-creation-menu | 1,013 | 4 | 729 | 687 tri-strip, 42 sprite |
| `…001403` | character-creation-menu | 1,348 | 4 | 980 | 924 tri-strip, 56 sprite |
| `…002058` | offline-online-play-select | 300 | 4 | 104 | 80 tri-strip, 24 sprite |
| `…002433` | command-center-personnel | 2,536 | 4 | 668 | 636 tri-strip, 32 sprite |
| `…004300` | equipment-modify | 850 | 4 | 602 | 554 tri-strip, 48 sprite |
| `…004540` | carthage-first-gameplay | 7,300 | 4 | 1,910 | 1,625 tri-strip, 259 sprite, 26 tri-fan |

The gameplay cohort exercises triangle fans, which no menu cohort produces, so the primitive
assembly is not tuned to the mission-select screen.

### Savestate sweep (2026-07-25)

A later headless capture pass (`-gsdumpframes` / `-gsdumpstop` / `-gsdumpdir`) took exactly one GS
dump from every owner savestate, giving a screen-per-slot corpus. Screens are identified from each
dump's own companion PNG together with its draw structure; the counts are `gs_dump_inspect` output.

| Slot | Screen | Packets | Draws | Primitives | Ring? |
| --- | --- | ---: | ---: | --- | --- |
| slot04 | Play Select (`PLAY OFF-LINE` selected) | 300 | 104 | 80 tri-strip, 24 sprite | no |
| slot05 | Play Select (`PLAY OFF-LINE` selected) | 300 | 104 | 80 tri-strip, 24 sprite | no |
| slot06 | Command Center — Personnel, no sub-menu | 2,536 | 668 | 636 tri-strip, 32 sprite | **yes** |
| slot07 | Command Center — Personnel, sub-menu list open | 2,820 | 808 | 740 tri-strip, 68 sprite | **yes** |
| slot08 | Command Center — mission select | 2,564 | 672 | 636 tri-strip, 36 sprite | **yes** |
| slot09 | Equipment Modify | 850 | 598 | 550 tri-strip, 48 sprite | no |
| slot10 | in-mission gameplay | 7,300 | 1,908 | 1,620 tri-strip, 262 sprite, 26 tri-fan | no |

Two further auto-captures sit at the root of the same capture tree (`frame_000005`,
`frame_000020`); both are mission select and match slot08's counts exactly (2,564 packets,
672 draws, 636 tri-strip + 36 sprite). Five ring-bearing captures are therefore available, three of
them from Command Center sub-screens that are *not* mission select.

`measure_mission_ring_state.py` finds zero marker clusters in slot04, slot05, slot09 and slot10:
the ring is absent from those screens, not merely undetected — none of them contains the band quad
at all.

**slot07 identified.** It matched no previously recorded cohort. It is the Command Center Personnel
screen with its sub-menu list open. The evidence:

* Its ring is byte-for-byte the slot06 ring (see below), so it is a Command Center ring screen.
* Its header text run is *identical* to slot06's — draw 192, 18 sprite vertices, bbox
  `(433.938, 36.375) – (540.688, 58.750)`; slot06 emits the same 18-vertex run with the same bbox
  at draw 157. Sprite text is two vertices per glyph, so both headers are 9 glyphs. slot08's header
  is instead two runs, draw 156 (34 vertices, `(361.938, 30.750) – (529.688, 46.625)`) and draw 158
  (28 vertices, `(361.938, 49.438) – (508.438, 65.312)`), i.e. 17 and 14 glyphs on two lines.
* The extra 35 draws in its frame (202 against slot06's 167) include a vertical column of sprite
  text runs at draws 159, 163, 167, 171, 175, 179, 183 and 187, spanning y 169.812 to y 319.188,
  each centred within 0.41 px of x = 323 — a menu list drawn over the ring. A ninth run is batched
  into draw 191, whose 28 vertices and bbox `(74.000, 73.688) – (384.188, 337.812)` merge the
  `Agent:`/`Rank:` label block with a further column entry.
* Its footer differs by one text run: a 16-vertex (8-glyph) run at
  `(120.000, 405.062) – (192.000, 419.062)` that slot06 and slot08 do not emit. On screen slot06
  and slot08 show the `L1`/`R1` rotate hints there and slot07 shows a d-pad `Navigate` hint.

So slot07 is the same sub-screen as slot06 one level deeper, and the ring is drawn behind it.

All mission-select captures render exactly **168 draw calls per frame**, identical frame to frame.
Display state from `GSPrivRegSet`: `DISP0.DISPLAY` gives a **640 × 448** visible area
(`DW=2559, MAGH=3, DH=447, MAGV=0`), `DISPFB.FBW=10`, `PSM=PSMCT32`. The companion PNGs are
1245 × 934 window captures; the framebuffer maps onto them by the plain linear scale
`W/640 = 1.94531`, `H/448 = 2.08482` (see cross-check below).

## PROVEN — what the captures establish

### The ring band is a screen-aligned textured quad, not geometry

The elliptical track, the tick marks and the "AMERICA" lettering visible on screen are *texture
art*. Geometrically the band is a six-vertex axis-aligned triangle-strip quad sampling a
256 × 256 `PSMT8` texture, re-drawn **40 times per frame** (9 distinct bounding boxes; the modal
one occurs 18 times):

```text
modal band quad   (170.812, 97.438) - (489.688, 387.500)
                  centre (330.250, 242.469)   size 318.876 x 290.062
band bbox spread across all 40 draws: x0 170.375..170.812, y0 95.625..97.438,
                                      x1 488.625..489.688, y1 387.500..389.625
```

No ellipse appears in the vertex stream. Any reimplementation that tries to build the ring from
geometry is modelling the wrong thing; the ring is a blended texture stack.

### 22 markers, at fixed screen positions

The ring occupies draws 16–155 of each 168-draw frame: 40 band quads plus 100 marker quads that
resolve into 22 positionally distinct clusters (a quad belongs to an existing cluster if its centre
is within 8 px). Positions below are the centre of each cluster's smallest quad, in framebuffer
pixels, listed in draw order (capture `…003922`, frame 0 — all four frames are identical for
markers 1–21):

| # | first draw | marker quad (x0, y0) – (x1, y1) | centre | marker W×H | largest quad W×H | draws |
| ---: | ---: | --- | --- | ---: | ---: | ---: |
| 0 | 16 | 490.312, 161.688 – 533.312, 199.188 | 511.812, 180.438 | 43.00 × 37.50 | 43.00 × 37.50 | 1 |
| 1 | 17 | 422.938, 347.500 – 452.750, 374.500 | 437.844, 361.000 | 29.81 × 27.00 | 54.69 × 52.19 | 5 |
| 2 | 24 | 347.938, 348.812 – 377.750, 375.750 | 362.844, 362.281 | 29.81 × 26.94 | 52.31 × 47.31 | 5 |
| 3 | 31 | 282.875, 361.000 – 312.500, 387.812 | 297.688, 374.406 | 29.62 × 26.81 | 53.25 × 48.19 | 5 |
| 4 | 38 | 214.188, 358.688 – 243.812, 385.500 | 229.000, 372.094 | 29.62 × 26.81 | 51.56 × 49.12 | 5 |
| 5 | 45 | 189.062, 318.625 – 215.000, 342.812 | 202.031, 330.719 | 25.94 × 24.19 | 52.31 × 47.38 | 5 |
| 6 | 52 | 126.875, 311.562 – 156.688, 338.562 | 141.781, 325.062 | 29.81 × 27.00 | 52.31 × 47.31 | 5 |
| 7 | 59 | 108.812, 266.438 – 138.625, 293.438 | 123.719, 279.938 | 29.81 × 27.00 | 52.31 × 47.38 | 5 |
| 8 | 66 | 154.375, 235.938 – 182.750, 259.750 | 168.562, 247.844 | 28.38 × 23.81 | 52.31 × 47.31 | 5 |
| 9 | 73 | 85.250, 205.688 – 113.000, 231.562 | 99.125, 218.625 | 27.75 × 25.88 | 52.31 × 47.31 | 5 |
| 10 | 80 | 146.875, 176.500 – 174.625, 202.438 | 160.750, 189.469 | 27.75 × 25.94 | 52.31 × 47.31 | 5 |
| 11 | 87 | 161.625, 123.625 – 190.875, 150.125 | 176.250, 136.875 | 29.25 × 26.50 | 52.31 × 47.31 | 5 |
| 12 | 94 | 239.938, 144.188 – 266.250, 168.750 | 253.094, 156.469 | 26.31 × 24.56 | 52.31 × 47.31 | 5 |
| 13 | 101 | 244.688, 88.688 – 272.438, 114.562 | 258.562, 101.625 | 27.75 × 25.88 | 52.31 × 47.38 | 5 |
| 14 | 108 | 300.438, 97.875 – 330.250, 124.812 | 315.344, 111.344 | 29.81 × 26.94 | 52.31 × 47.38 | 5 |
| 15 | 115 | 356.438, 92.375 – 386.250, 119.312 | 371.344, 105.844 | 29.81 × 26.94 | 52.31 × 47.31 | 5 |
| 16 | 122 | 419.688, 72.625 – 447.500, 98.562 | 433.594, 85.594 | 27.81 × 25.94 | 51.56 × 48.12 | 5 |
| 17 | 129 | 412.562, 130.125 – 438.125, 154.000 | 425.344, 142.062 | 25.56 × 23.88 | 52.31 × 47.31 | 5 |
| 18 | 136 | 464.312, 121.188 – 494.125, 148.188 | 479.219, 134.688 | 29.81 × 27.00 | 52.31 × 47.31 | 5 |
| 19 | 143 | 496.500, 147.062 – 533.562, 181.688 | 515.031, 164.375 | 37.06 × 34.62 | 47.81 × 44.62 | 4 |
| 20 | 149 | 486.125, 246.250 – 540.188, 291.812 | 513.156, 269.031 | 54.06 × 45.56 | 54.94 × 46.19 | 3 |
| 21 | 153 | 443.000, 287.562 – 495.750, 333.125 | 469.375, 310.344 | 52.75 × 45.56 | 53.50 × 46.19 | 2 |

Markers 1–18 are the plain dots. Markers 19, 20 and 21 use larger quads and no 32 × 32 dot texture;
they are the three distinctly shaped icons visible on the right of the ring. Marker 0 is the only
animated element (below).

### Per-marker draw structure

Markers 1–18 are each exactly five six-vertex quads. Ranked by area, their sizes across all
eighteen markers are stable:

```text
rank 0   W 25.56..29.81   H 23.81..27.00     the dot
rank 1   W 25.56..29.81   H 23.81..27.00     the same dot again, identical position
rank 2   W 37.81..41.25   H 34.25..38.06
rank 3   W 48.81..53.06   H 44.19..48.06
rank 4   W 51.56..54.69   H 47.31..52.19     the outer halo
```

Emission order within a group is not fixed (some markers emit the halo first, some the dot). For
each of markers 1–18 a band quad is re-emitted between that marker's two dot draws.

Every marker emits exactly one *opaque* pass (vertex alpha 127 on all six vertices); the rest are
emitted at vertex alpha 0. Every one of the 140 ring draws shares identical state:
`PRIM=triangle_strip`, `IIP=1`, `TME=1`, `FST=0` (STQ texture coordinates), `ABE=1`, `AA1=0`,
`FGE=0`, context 0; `TEX0` `TFX=Modulate`, `TCC=RGBA`, `PSMT8` or `PSMT4` at 32 × 32, 64 × 64 or
256 × 256; blend `ALPHA` A=0, B=1, C=0, D=1, FIX=128, i.e. `(Cs − Cd)·As + Cd`; `TEST`
`ATE=1 ATST=6 AREF=64 AFAIL=1 ZTE=1 ZTST=2`; `FRAME` `FBP=0 FBW=10 PSMCT32 FBMSK=0xFF000000`.

### Marker emission order is ring order

Taking polar angles about the best-fit ellipse centre (angle 0 = +x, increasing towards +y, i.e.
clockwise on screen), the 21 static markers are emitted in strictly monotone angular order:

```text
67.42  85.28 103.22 121.49 139.03 153.64 173.86 186.51 199.55 215.11 234.50
246.26 257.66 271.39 287.24 300.28 310.57 322.10 339.53  27.12  48.57
```

The sequence wraps exactly once and closes back onto the first marker, so the 22 markers form one
closed loop drawn clockwise starting just right of bottom-centre. The animated marker 0 (angle
345.8°) is emitted before the loop begins.

### The highlighted marker

Marker 2, centred at **(362.844, 362.281)**, is the selected one. Two independent facts establish
it:

* It is the only marker whose opaque pass is *not* the small dot. Markers 1 and 3–18 emit their
  ~26–30 × 24–27 dot at alpha 127; markers 19–21 emit their ~44–54 × 41–46 icon. Marker 2 emits
  **two** ~52.31 × 47.31 quads at alpha 127 (a halo plus a distinct icon texture) and leaves both
  of its 29.81 × 26.94 dot draws at alpha 0.
* In the companion screenshot that exact position is the large ringed emblem containing a figure
  icon (verified visually against the capture's own PNG).

Its extent as drawn is the pair of 52.31 × 47.31 quads spanning
`(336.688, 338.625) – (389.000, 385.938)`.

Marker 2 is **not** at the lowest point of the ring. The lowest marker is marker 3 at
`(297.688, 374.406)`. Marker 2 sits 34.7 px right of the fitted ellipse centre's x and 125 px below
its y.

### The selection moves; the markers do not

This is the observation the original three captures could not make, and it settles the interaction
model. Across the five ring-bearing captures — two Personnel captures (slot06, slot07) and three
mission-select captures (slot08, `frame_000005`, `frame_000020`) — the ring's geometry is not merely
similar, it is *the same emission, draw for draw*:

| Property | Value, identical in all five captures |
| --- | --- |
| ring draw run | draws 13–155 of the first frame; the ring's own 140 draws are 16–155, preceded by three backdrop quads at draws 13–15 (706.31 × 511.44, 503.12 × 469.62 and 725.69 × 444.69) that the tool's contiguity rule sweeps into the run and its band/marker width rules then discard |
| band quads | 40, at draw indices 21, 23, 28, 30, 35, … 148, 152, 155 |
| band bounding boxes | all 40 agree to **0.0000 px**; 9 distinct boxes, modal `(170.812, 97.438) – (489.688, 387.500)` occurring 18× |
| marker quads | 100, resolving to 22 clusters |
| markers 1–21 | centres, bounding boxes, smallest- and largest-quad sizes and per-marker draw-index lists all agree to **0.0000 px** / exactly |

Only marker 0, the animated one, differs — as it does between frames of a single capture.

Against that fixed backdrop, the *entire* difference in the ring between a Personnel capture and a
mission-select capture is **four draws' worth of vertex alpha**, and nothing else:

| Marker | Centre | Draw | Quad | Personnel (slot06, slot07) | Mission select (slot08, `…000005`, `…000020`) |
| ---: | --- | ---: | --- | --- | --- |
| 2 | 362.844, 362.281 | 24 | 52.312 × 47.312 | alpha 0 | **alpha 127** |
| 2 | 362.844, 362.281 | 25 | 52.312 × 47.312 | alpha 0 | **alpha 127** |
| 21 | 469.375, 310.344 | 153 | 53.500 × 46.188 | **alpha 127** | alpha 0 |
| 21 | 469.375, 310.344 | 154 | 52.750 × 45.562 | alpha 0 | **alpha 127** |

Every other one of the 100 marker quads carries the same alpha in all five captures. Reading that
table with the discriminator above:

* On mission select, marker 2 is highlighted — `opaque_area_ratio` 3.082, against 1.39 for the
  next-highest marker (marker 19). This reproduces the original cohort's result.
* On Personnel, **no** marker satisfies the discriminator: marker 2 emits no opaque pass at all,
  all five of its quads at alpha 0. Instead marker 21 swaps *which* of its two ≈53 × 46 quads is
  opaque — draw 153 rather than draw 154. Both its quads are large, so this swap does not move
  `opaque_area_ratio` above 1.03 and the primary discriminator cannot see it; it is nonetheless the
  only other alpha difference anywhere in the ring.
* The two quads marker 21 swaps between are separate textures, `TBP0` 12618 (draw 153) and 13230
  (draw 154) in both slot06 and slot08. Only which one is painted changes; the addresses do not.
  (In slot07 the same two draws carry 11480 and 11500 — a texture-cache difference, per the `TBP0`
  caveat under UNPROVEN, not a different image.)
* Screenshot corroboration, checked visually against each capture's own PNG: on the Personnel
  captures the marker-21 icon renders bright and the marker-2 emblem is absent; on the mission-select
  captures the marker-2 emblem renders bright and the marker-21 icon renders dim.

So two ring states that differ in which marker is selected place all 22 markers, and the band, at
bit-identical screen positions. **The ring does not rotate.** Selection is expressed entirely by a
per-marker alpha and texture swap at a fixed screen position.

The header text corroborates that the selected marker is what the header names: slot06 and slot07
emit one 18-vertex (9-glyph) header run while marker 21 is lit; slot08 emits two runs of 34 and 28
vertices (17 and 14 glyphs) while marker 2 is lit. On screen those read `Personnel` and
`Carthage, Michigan` / `Quarantine Zone`.

Markers 21 and 2 are two steps apart in ring order (21 → 1 → 2, wrapping), at ring angles 48.57°
and 85.28° from the table above. The captures do **not** show how many presses produced that
difference — see UNPROVEN.

The original `command-center-personnel` capture `…002433` shows the same signature independently:
marker 2 with no opaque pass and marker 21 painting its 53.500 × 46.188 quad. That capture yields
21 clusters rather than 22 because its animated marker 0 (draw 16) happened to pass within the 8 px
cluster radius of marker 19 and merged with it; the merge is visible in the cluster's draw-index
list `[16, 143, 144, 145, 146]` and is a limitation of the clustering, not a change in the ring.

### An animated marker exists and is not part of the static loop

Marker 0 is the only element whose position changes. It carries no opaque pass at all (alpha 0 on
every vertex) and its quad is 43.00 × 37.50, larger than any plain dot. Its trajectory:

```text
…003922 frames 0-3: (511.812, 180.438) (511.625, 181.188) (511.438, 182.000) (511.219, 182.938)
…003931 frames 0-3: (512.000, 179.688) (511.875, 180.281) (511.688, 180.969) (511.562, 181.562)
…003940 frames 0-2: (512.344, 178.312) (512.188, 179.000) (512.000, 179.688)
```

Motion is smooth and sub-pixel-quantised (12.4 fixed point), roughly −0.12…−0.22 px/frame in x and
+0.59…+0.94 px/frame in y within a capture, and the value at `…003940` frame 2 exactly equals
`…003931` frame 0, so the motion is cyclic rather than monotone.

The savestate sweep extends this well beyond the ~5 px seen across the original three captures.
Marker 0 is always draw 16, always alpha 0, and always a 43.00–43.06 × 37.44–37.50 quad, but its
position ranges widely:

```text
frame_000005 (mission select) : (501.188, 223.812)
frame_000020 (mission select) : (498.125, 236.375)
slot08       (mission select) : (500.594, 226.344)
slot06       (Personnel)      : (494.312, 251.938)
slot07       (Personnel)      : (383.406, 107.375)
```

The slot07 position is 182.205 px from slot06's and 166.992 px from slot08's, on the opposite arc
of the band. So marker 0 traverses the ring rather than jittering about one point; the small
excursion recorded from the original three captures is an artefact of those captures being seconds
apart. Its motion is independent of the marker layout, which does not move at all across the same
five captures.

### Screenshot cross-check

For each of the three captures, every predicted marker centre was mapped into the capture's own
1245 × 934 PNG by `x·(1245/640)`, `y·(934/448)` and compared with the luminance centroid of the
bright blob inside an ±11 px window.

* **22 of 22** markers found a bright blob in all three captures.
* Solving a free affine scale from the 18 plain-dot markers recovers `sx = 1.94442` against the
  nominal `1.94531` (0.05 % off) and `sy = 2.08192` against `2.08482` (0.14 % off), confirming the
  mapping is a plain linear scale with no letterbox.
* Residual scatter about that affine fit is **0.177 px in x and 0.275 px in y**, in framebuffer
  units — sub-pixel.
* Using the nominal scale directly, markers 1–18 show a constant bias of `(+0.65, +0.95)`
  framebuffer px with standard deviation `(0.185, 0.311)`. The bias is a property of the
  blob-centroid estimator against the quad centre, not of the extraction; the marker-to-marker
  agreement is what matters and it is a fifth of a pixel.
* The only large disagreement is marker 0, at `(−2.48, −4.81)` px, exactly as expected for an
  element that is still moving between the dumped frame and the screenshot instant.

Whole-capture RMS offsets: 1.65 px (`…003922`), 1.62 px (`…003931`), 1.53 px (`…003940`), dominated
by marker 0 in each case.

### The three captures show the same ring state

Frame 0 of `…003931` and `…003940` reproduce all 21 static marker positions from `…003922` to
**0.0000 px** — bit-identical 12.4 fixed-point values. Only marker 0 differs (0.773 px and
2.190 px respectively), consistent with its animation. All three captures also show the same
mission-name text lines and the same highlighted marker.

No rotation step could be measured **from this cohort**: all three captures show one selection
state. That gap is closed by the savestate sweep — see "The selection moves; the markers do not"
above and the DISPROVEN entry below.

### The ring is shared Command Center furniture

The same ring is drawn on at least three different Command Center sub-screens — Personnel (slot06),
Personnel with its sub-menu open (slot07), and mission select (slot08) — from the same 140-draw
emission at the same draw indices with the same geometry, to 0.0000 px. It is therefore a single
piece of screen furniture that the Command Center draws regardless of which entry is selected, not
per-screen content. slot07 additionally shows it drawn *behind* an overlaid menu list, unchanged.

It is absent from Play Select (slot04, slot05), Equipment Modify (slot09) and gameplay (slot10):
those frames contain no band quad and yield zero marker clusters.

## DISPROVEN — the rotating carousel

Rotation would require the marker positions to change when the selection changes. They do not.
Across five captures spanning two different selected markers and three different Command Center
sub-screens, all 21 static markers and all 40 band quads agree to **0.0000 px** — identical 12.4
fixed-point values, at identical draw indices, in identical sizes. The only differences anywhere in
the ring are four vertex-alpha values on markers 2 and 21, plus the independently animated marker 0.

There is consequently no rotation increment to model. L1/R1 changes *which* marker is selected over
a static, map-like layout; it does not turn the ring. A reimplementation that stores the 22 marker
positions as a fixed table and moves a selection index over it is structurally correct.

Two limits on this result, stated plainly. It rests on captures of *different* selection states, not
on a recording spanning an input, so it disproves rotation as the mechanism without measuring what
one press does. And the two observed states differ in sub-screen as well as in selected marker; what
is proven is that neither change moves a marker.

## DISPROVEN — the uniform-ellipse carousel model

The markers are **not** evenly spaced on a common ellipse. Fitting a general conic (SVD, algebraic
fit, normalised coordinates):

| Marker subset | centre | semi-axes | tilt | radial residual sd | max deviation |
| --- | --- | --- | ---: | ---: | ---: |
| all 22 | (326.64, 237.20) | 207.32, 131.79 | −7.89° | 24.01 px | 50.82 px |
| 21 static (excl. marker 0) | (328.10, 236.95) | 209.28, 131.57 | −8.01° | 24.73 px | 50.54 px |
| the 15 outermost only | (321.33, 235.31) | 217.50, 139.46 | −6.43° | 13.84 px | 31.33 px |

Tilt is the major-axis angle in screen coordinates with +y pointing down, so a negative value means
the major axis rises to the right. Radial residual is the standard deviation of each marker's
normalised elliptical radius, converted to pixels along the semi-major axis.

Normalised radii of the 21 static markers span **0.748 to 1.190** — a ±22 % spread on a ~209 px
semi-major axis. Even after discarding the six innermost markers, the remainder still misses a
common ellipse by up to 31 px. Angular gaps between consecutive markers range from **10.29° to
47.59°** (mean 17.06°, sd 7.68°) against `360/21 = 17.14°`; the outer-15 subset ranges from 12.64°
to 45.45°. Fitting a projective model over all 22 markers (DLT homography from a unit circle with
exactly uniform 360/22 spacing, phase offset searched over the full step) leaves a **38.2 px RMS**
reprojection error, so the layout is not a perspective-projected circle with equally spaced nodes
either.

Marker quad sizes correlate with radius: the four smallest dots (25.56 × 23.88 through
28.38 × 23.81) belong to the four markers with the smallest normalised radii (0.748–0.873). Total
size variation across the 18 plain dots is only 25.56–29.81 px wide, so whatever depth cue exists
is weak.

The screen art supports the same reading: the ring background carries curved continental lettering
("AMERICA" arcs across the top of the band), so the marker layout is best explained as fixed
map-like positions, not a uniform carousel. This contradicts the working assumption that the
mission items are evenly spaced ring nodes, and it also contradicts the older assumption that they
are text labels placed by GUI widget rectangles — they are neither.

## Screen furniture in the same frame

Useful anchors extracted from the same 168-draw frame, framebuffer pixels:

| Draw | Role (from the screenshot) | bbox |
| ---: | --- | --- |
| 8 | top header bar | (−2.188, 3.062) – (664.562, 82.438) |
| 11 | "COMMAND CENTER" title | (22.000, 35.438) – (253.750, 57.812) |
| 12, 157 | "Agent:" / "Rank:" label block | (22.000, 72.750) – (90.500, 114.750) |
| 156 | mission name, line 1 | (361.938, 30.750) – (529.688, 46.625) |
| 158 | mission name, line 2 | (361.938, 49.438) – (508.438, 65.312) |
| 165–167 | L1 / R1 rotate hints | (44.438, 389.938) – (175.250, 434.500) |
| 159–161 | triangle glyph + "Previous" | (261.000, 375.188) – (426.875, 445.438) |
| 162–164 | cross glyph + "Select" | (451.000, 377.000) – (576.750, 444.812) |

The two mission-name lines share one 256 × 256 glyph atlas and are emitted as `PRIM=sprite` runs
(34 and 28 vertices), which is the title's text path; the ring markers never use sprites.

## UNPROVEN

* **Whether 22 equals the mission count.** The captures prove 22 markers are drawn. They do not
  establish that every marker is a selectable mission, nor that every mission has a marker. Three
  markers (19–21) render differently, and one (marker 0) is animated and carries no opaque pass, so
  the count of *selectable* entries may be anywhere from 18 to 22. The savestate sweep narrows the
  question from one side: marker 21 is selectable — it is the selected entry in slot06 and slot07 —
  and while it is selected the header reads `Personnel`, so at least one selectable marker is not a
  mission. 22 is therefore an upper bound on the mission count, not the count itself.
* **What drives ordering.** Emission order is monotone in ring angle, but the captures cannot show
  whether that order is a stored mission table order, a depth sort, or a re-sorted view of a fixed
  list.
* **How far one L1/R1 press moves the selection.** The rotation *step* is no longer an open
  question — there is no rotation (see DISPROVEN). What is still unmeasured is the selection
  increment: the two observed states differ by two positions in ring order (marker 21 → marker 2,
  wrapping through marker 1), but the captures are independent savestates, so nothing shows whether
  that was one press, two presses, or a jump. Measuring it needs two captures known to be separated
  by exactly one press.
* **Whether the selection order is the emission order.** Markers 21 and 2 are two apart in emission
  order, which is consistent with L1/R1 walking that order, but two captures cannot distinguish a
  ring walk from an independent selection sequence.
* **Whether the layout is geographic.** The band texture carries continental lettering and the
  marker spacing is irregular, which is consistent with map positions, but nothing in the geometry
  proves a projection or a coordinate system. The savestate sweep shows the positions do not move
  with the selection or the sub-screen, which supports treating them as a fixed table; whether that
  table is the same for a save with different mission progress is still untested.
* **What marker 0 represents.** Its motion, its lack of an opaque pass, and its position near
  marker 19 are all measured; its meaning is not.
* **Texture identity.** Markers reference different `TEX0.TBP0` addresses (for example the plain
  dot is `TBP0=11766`/`11460` for markers 3–18 but `15614` for markers 1–2 in `…003922`, and the
  addresses differ between captures). Equal addresses within one capture mean the same texture
  cache slot; different addresses do **not** prove different images. Texel payloads were
  deliberately not decoded.
* **The 3D interpretation.** All coordinates here are post-transform screen positions. Nothing in a
  GS dump exposes the model or view transform that produced them.

## Provenance

Authored 2026-07-25 from the `command-center-mission-select` cohort
(`…003922`, `…003931`, `…003940`) plus six captures from five other cohorts used only to show the
parser generalises. Format definitions read from the PCSX2 working tree at
`pcsx2/GS/{GSDump.h,GSDump.cpp,GSLzma.cpp,GSState.cpp,GSRegs.h}`. Every number in this document is
reproducible from `tools/gs_dump_inspect.py` output; nothing is estimated by eye.

Extended 2026-07-25 with the savestate sweep — one headless GS dump per owner savestate (slot04
through slot10) plus two further auto-captures from the same tree, giving five ring-bearing
captures across three Command Center sub-screens. That evidence disproves rotation, identifies
slot07, and establishes the slot-to-screen map. Its numbers come from
`tools/measure_mission_ring_state.py`, which drives `tools/gs_dump_inspect.py` and is covered by
`tools/tests/test_measure_mission_ring_state.py`; screen identifications were checked against each
capture's own companion PNG. The captures, their PNGs and the tool's JSON output are owner-private
and stay under the git-ignored `analysis/DrawDump/` tree.
