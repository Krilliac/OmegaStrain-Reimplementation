# Command Center mission-select ring geometry

Status: **measured** from three owner-private PCSX2 GS dumps of the `command-center-mission-select`
cohort (2026-07-25). Marker screen positions, marker count, per-marker quad extents, ring-band
extent, and the highlighted-marker discriminator are proven. Ring *parameterisation* (a single
ellipse with uniform angular spacing) is **disproven** by the same data. Node-to-mission mapping,
ordering rule, and rotation step remain unproven.

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

Reproduce:

```text
python -B tools/gs_dump_inspect.py <dump.gs.zst> <out.json>
python -B tools/gs_dump_inspect.py <dump.gs.zst> <out.json> --no-vertices   # summary only
```

Output is deterministic: two runs over the same dump produce byte-identical JSON
(SHA-256 `cae4dc68…1afb1d` for the `…003922` capture, verified twice).

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
`…003931` frame 0, so the motion is cyclic rather than monotone. Total observed excursion is under
5 px.

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

**No rotation step could be measured.** The carousel did not move between the three captures, so
the cohort provides no evidence about the rotation increment. Recovering it requires new captures
taken across an L1/R1 press.

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
  the count of *selectable* entries may be anywhere from 18 to 22.
* **What drives ordering.** Emission order is monotone in ring angle, but the captures cannot show
  whether that order is a stored mission table order, a depth sort, or a re-sorted view of a fixed
  list.
* **The rotation step.** Not observable: all three captures show an identical ring state. No
  capture spans an L1/R1 input.
* **Whether the layout is geographic.** The band texture carries continental lettering and the
  marker spacing is irregular, which is consistent with map positions, but nothing in the geometry
  proves a projection or a coordinate system. The marker positions should be treated as an opaque
  fixed table until a capture of a different mission set shows whether they move.
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
