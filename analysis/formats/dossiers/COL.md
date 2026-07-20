# COL Dossier

## 1. Identity

`.col` is an in-scope game-asset suffix that occurs only nested inside HOG archives across the
owned NTSC-U disc corpus. At the evidence level this repository can defend, COL is a counted
node/leaf/triangle/vertex spatial-partition span with a confirmed 48-byte header and four
counted, densely-packed record tables. Its native adapter treats it as passive collision/spatial
geometry; the repository draws no conclusion beyond "spatial acceleration structure with
triangle-mesh leaves" because collision response, triangle winding, coordinate-space, and
material meaning are explicitly left unassigned by the tracked grammar doc.

## 2. Occurrence evidence

| Metric | Value | Source |
| --- | ---: | --- |
| Recursive-in-HOG occurrences | 7036 | `analysis/formats/asset-fingerprints.json` (`formats.col`) |
| Top-level-HOG occurrences | 0 | `analysis/formats/hog-validation.json` |
| Whole-disc occurrences | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` |

COL never appears as a top-level HOG member and never appears loose at the whole-disc level; every
observed instance is nested inside a HOG member (task-brief framing), consistent with COL being
carried inside per-cell/per-level container HOGs rather than top-level archives.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| 1 | `col` format family entry exists in the passive fingerprint corpus with count 7036 (`col_prefix`, `count`, `counted_table_formulas_match`, `described_tables_span_nonzero_tail`, `observed_table_ends_ordered_aligned_and_bounded` = 7036) | `analysis/formats/asset-fingerprints.json` (`formats.col`) |
| 2 | `header_size_48` = 7036 (every span's header size field equals 48) | `analysis/formats/asset-fingerprints.json` (`formats.col.header_size_48`) |
| 3 | `span_16_byte_aligned` = 7036 (every span's table-end offset is 16-byte aligned) | `analysis/formats/asset-fingerprints.json` (`formats.col.span_16_byte_aligned`) |
| 4 | `span_bytes_range` = [128, 229680] across the 7036 spans | `analysis/formats/asset-fingerprints.json` (`formats.col.span_bytes_range`) |
| 5 | `version_byte` distribution: value 3 occurs 3 times, value 5 occurs 7033 times (sums to 7036) | `analysis/formats/asset-fingerprints.json` (`formats.col.version_byte`) |
| 6 | `first_count_word` distribution: value 0 occurs 2678 times, value 1 occurs 3223 times, plus a long tail up to 105 (full histogram present, summing to 7036) | `analysis/formats/asset-fingerprints.json` (`formats.col.first_count_word`) |
| 7 | Header is 48 bytes; `u32@0x08 == 48` in every span. Four counted tables follow: node table (header count field `0x04`, header-end `0x10`, record size 64 bytes: two bounds vectors + eight child offsets), leaf table (count field `0x0C`, header-end `0x18`, record size 48 bytes: two bounds vectors + reference count/offset + two zero words), triangle table (count field `0x14`, header-end `0x20`, record size 16 bytes: three u16 vertex indices at `+0x04/+0x06/+0x08`), vertex table (count field `0x1C`, header-end `0x24`, record size 16 bytes: finite x/y/z + homogeneous w=1) | `analysis/formats/COL.md` ("Counted layout" table) |
| 8 | The endpoint at `0x2C` equals `align16(vertex_end + 4 * triangle_count)`; the intervening region is leaf-addressed unsigned 32-bit triangle references plus zero alignment padding; the word at `0x28` and all bytes after `0x2C` are opaque and omitted from canonical output | `analysis/formats/COL.md` ("Counted layout") |
| 9 | Spatial-graph invariants confirmed across all spans: nonzero child words identify exact node/leaf record starts, zero is an empty slot, root is node zero when nodes exist else leaf zero, every non-root record has exactly one parent, every record is reachable, no cycles; maximum observed root edge depth is 8 | `analysis/formats/COL.md` ("Spatial and geometry invariants") |
| 10 | Leaf bounds exactly equal the extrema of vertices used by referenced triangles (no tolerance needed); leaf reference ranges form an exact permutation of triangle indices, each triangle belongs to exactly one leaf; first three triangle indices are distinct, have no high bit, and address existing vertices; primitive bytes at `+0x00`, `+0x0A`, `+0x0C` are non-index opaque fields | `analysis/formats/COL.md` ("Spatial and geometry invariants") |
| 11 | 2716 spans contain the exact sole-node empty sentinel (one node, no leaves/triangles/vertices, inverse-maximum-float bounds, no children), normalized to an empty mesh by the adapter rather than publishing sentinel floats; 2678 spans use a direct leaf root | `analysis/formats/COL.md` ("Spatial and geometry invariants") |
| 12 | Ledger entry E-0018 (confirmed): independent passive native descriptor corpus validates 7036 COL spans (alongside 7036 VUM and 15248 TDX) with zero errors, reproducing every aggregate extent class without decoding payload semantics | `analysis/evidence/ledger.jsonl` (E-0018); evidence cites `native/src/retail/container_descriptors.cpp`, `native/apps/omega_tool/asset_commands.cpp` |
| 13 | Ledger entry E-0020 (confirmed): the bounded native COL adapter decodes all 7036 owned-corpus spans into canonical spatial meshes with zero errors — 23913 source nodes, 99193 leaves, 1327714 triangles, 949762 vertices, 2716 normalized empty meshes, 2678 direct leaf roots, maximum edge depth 8 | `analysis/evidence/ledger.jsonl` (E-0020); evidence cites `analysis/formats/COL.md`, `native/include/omega/asset/spatial_mesh_ir.h`, `native/src/retail/col_spatial_mesh_decoder.cpp`, `native/apps/omega_tool/asset_commands.cpp` |
| 14 | Ledger entry E-0021 (confirmed): native content service resolves all 5351 manifest cells across all 18 levels into 5351 owned spatial meshes with zero errors — 20203 canonical nodes, 93356 leaves, 889640 vertices, 1239980 triangles/references, 2137 normalized empty meshes; native startup independently preserves 5351/5351 manifest-spatial cardinality | `analysis/evidence/ledger.jsonl` (E-0021); evidence cites `analysis/formats/COL.md`, `native/include/omega/asset/level_spatial_ir.h`, `native/src/content/game_data_service.cpp`, `native/src/runtime/content_startup.cpp`, `native/apps/omega_tool/pop_commands.cpp`, `tools/probe_native_levels.py` |
| 15 | Ledger entry E-0025 (confirmed): native startup builds a bounded source-order wireframe contact sheet from canonical LevelSpatialIR (not manifest metadata); synthetic integration proves changed COL topology changes the owned RGBA8 pixels | `analysis/evidence/ledger.jsonl` (E-0025); evidence cites `analysis/formats/COL.md`, `native/include/omega/runtime/spatial_debug_image.h`, `native/src/runtime/spatial_debug_image.cpp`, `native/src/runtime/content_startup.cpp`, `native/tests/spatial_debug_image_tests.cpp`, `native/tests/game_data_service_tests.cpp` |
| 16 | `GameDataService::LoadLevelSpatial` resolves the manifest's normalized DATA.HOG source, any nested container chain, and each cell HOG before requiring exactly one COL member, returning one owned mesh per manifest cell; archive/source depth and spatial edge depth compose under a default maximum of nine, admitting the observed eight-edge COL maximum beneath one cell-HOG edge | `analysis/formats/COL.md` ("Canonical adapter") |
| 17 | Native decoder header/source pair exists: `native/include/omega/retail/col_spatial_mesh_decoder.h`, `native/src/retail/col_spatial_mesh_decoder.cpp`; canonical IR header `native/include/omega/asset/spatial_mesh_ir.h` and `native/include/omega/asset/level_spatial_ir.h` | Direct file listing under `native/include/omega/retail/`, `native/src/retail/`, `native/include/omega/asset/` |
| 18 | `native/src/retail/col_spatial_mesh_decoder.cpp` is registered as a compiled source in the build, and `native/tests/col_spatial_mesh_decoder_tests.cpp` is registered as a test target | `CMakeLists.txt` (lines listing `native/src/retail/col_spatial_mesh_decoder.cpp` and `native/tests/col_spatial_mesh_decoder_tests.cpp`) |

## 4. Aggregate-only facts

No semantic interpretation attached — counts and structural observations only.

- Span byte-size range across all 7036 spans: 128 to 229680 bytes (`asset-fingerprints.json`, `formats.col.span_bytes_range`).
- Version-byte split: 7033 spans carry version 5, 3 spans carry version 3 (`asset-fingerprints.json`, `formats.col.version_byte`); `COL.md` states the confirmed state applies to "all 7,036 observed COL spans (7,033 version 5 and three version 3)".
- `first_count_word` (the node-table count) distribution spans from 0 (2678 spans) through 1 (3223 spans) down a long tail to isolated large values up to 105 — full histogram recorded in `asset-fingerprints.json`.
- Every span's table-end offsets are 16-byte aligned and every span's header-size field is exactly 48 (100% of 7036, i.e. no exceptions observed).
- Native decode aggregate totals across the full 7036-span corpus (`COL.md`, "Aggregate native validation," attributed to `omega_tool.exe asset-metadata-verify-tree`): 23,913 source nodes, 21,197 canonical nodes after empty normalization, 99,193 leaves, 1,327,714 triangles, 949,762 vertices, 1,327,714 leaf triangle references, 2,716 empty meshes, 2,678 direct leaf roots, maximum edge depth 8.
- Level-scoped native decode aggregate totals across all 18 levels / 5,351 manifest cells (`COL.md`, same section, attributed to `omega_tool.exe level-spatial-verify-tree`): 5,351 meshes, 20,203 canonical nodes, 93,356 leaves, 889,640 vertices, 1,239,980 triangles and leaf references, 2,137 normalized empty meshes, zero errors.
- These two aggregate totals are drawn from different scopes (all 7036 raw corpus spans vs. the 5351 manifest-referenced cells actually reachable through level content), so their counts differ; both are Confirmed aggregates, not contradictory.

## 5. Hypotheses

Each is explicitly labeled and untested against the tracked evidence; each states the privacy-safe
observation that would confirm or refute it.

- **H1 — COL is per-cell collision/physics geometry, one member per manifest cell.** The tracked
  grammar doc treats COL as "spatial geometry" and the content-service loader requires exactly one
  COL member per resolved cell HOG, but no tracked source assigns a gameplay role (collision vs.
  visibility vs. pathing) to the structure. Confirming/refuting observation: a privacy-safe survey
  correlating COL edge-depth/leaf-count distributions against POP candidate-record placement
  aggregates (already-published `POP.md` / E-0032 style aggregates) for the same manifest cells,
  looking for a structural correlation (e.g., mesh triangle density vs. record density) without
  reading any private payload semantics.
- **H2 — The four opaque/unassigned fields (word at `+0x28`, primitive bytes `+0x00/+0x0A/+0x0C`,
  and the sole-node sentinel's exact float bit pattern) encode a material or surface-type tag.**
  No tracked source assigns meaning to these fields; they are explicitly called "opaque" in
  `COL.md`. Confirming/refuting observation: an aggregate bit-pattern/entropy profile of these
  fields across the full 7036-span corpus (in the style of the POP `profile_pop_candidate_record_shapes.py`
  aggregate profiler) — reporting value-distribution shape only, no per-file rows — would show
  whether the field looks like a small enum (low-entropy, few distinct values) consistent with a
  material/surface tag, versus a derived numeric quantity.
- **H3 — Version 3 spans (3 of 7036) represent an earlier-build or degenerate-content variant
  rather than a structurally distinct grammar.** Only version 3 and version 5 are observed, with
  version 3 vastly outnumbered. No tracked source states what differs, if anything, in the
  version-3 header/table layout beyond the shared 48-byte header and counted-table formulas
  already confirmed to hold for all 7036 spans. Confirming/refuting observation: an aggregate-only
  diff of the confirmed structural invariants (node/leaf/triangle/vertex counts, edge depth, empty
  sentinel presence) segmented by version_byte==3 vs version_byte==5, reported as bucketed
  aggregates only.

## 6. Missing observations

- No tracked source records a COL-to-owning-container relationship beyond "resolved via the
  manifest's normalized DATA.HOG source, nested container chain, and cell HOG" (`COL.md`); no
  tracked aggregate states which top-level HOG kinds (per `disc-summary.json` archive listing,
  e.g. DATA.HOG vs OBJECTS.HOG) COL spans are nested under, or in what proportion. A privacy-safe
  collection: extend the existing recursive fingerprint scan (`tools/fingerprint_assets.py`) to
  emit an aggregate histogram of COL-count-per-immediate-container-suffix (e.g. "N COL spans found
  nested under DATA.HOG-class containers, M under OBJECTS.HOG-class containers") without naming
  any specific member or file path.
- No tracked source records cross-format co-location aggregates (e.g., how many manifest cells
  have both a COL and a POP/VUM member, versus COL-only cells). A privacy-safe collection: an
  aggregate cardinality report from the already-existing `GameDataService`/manifest traversal
  (same style as E-0021/E-0033) counting cells-with-COL vs cells-with-COL-and-VUM vs cells-with-
  COL-and-POP, published as totals only.
- No tracked source runs an adversarial/fuzz/resource-boundary test against the COL decoder beyond
  what `native/tests/col_spatial_mesh_decoder_tests.cpp` already covers per `COL.md`'s claim of
  "malformed numerics/references/topology, and exact/one-below resource budgets" — the dossier
  cannot independently verify the adversarial-test *content* without reading the test file, which
  is in scope (tracked, non-private) but was not opened for this dossier; the ledger does not carry
  a dedicated E-#### entry naming exact adversarial-case counts for COL the way E-0093 does for
  SKAS. Privacy-safe collection: read/summarize `native/tests/col_spatial_mesh_decoder_tests.cpp`
  (tracked source, no private data) and, if warranted, add a ledger entry enumerating the exact
  adversarial case classes and pass counts, matching the granularity already given for SKAS
  (E-0093) and the texture debug image adapter (E-0066).
- No tracked source states whether the COL decoder enforces distinct upper bounds on node/leaf/
  triangle/vertex counts independent of overall span-byte-size limits (i.e., whether a
  maliciously large count field in an otherwise short span is rejected before allocation). Privacy-
  safe collection: inspect the decoder's `DecodeLimits`-style preflight logic (already described in
  `COL.md` as "preflights caller-provided input, item, logical-output, and scratch limits") and
  record the exact numeric ceilings as a Confirmed fact once read, or as a dedicated ledger entry.

## 7. Decoder/tooling status

**Classification: `canonical_decoder`**

- `native/include/omega/retail/col_spatial_mesh_decoder.h` and `native/src/retail/col_spatial_mesh_decoder.cpp`
  exist and implement `DecodeColSpatialMesh`, returning owned `SpatialMeshIR` (vertices, triangles,
  packed leaf triangle-reference permutation, nodes, leaves) per `analysis/formats/COL.md`
  ("Canonical adapter").
- Canonical IR types are declared in `native/include/omega/asset/spatial_mesh_ir.h` (per-span mesh)
  and `native/include/omega/asset/level_spatial_ir.h` (per-manifest-cell aggregate, consumed by
  `native/src/content/game_data_service.cpp`).
- Build registration: `native/src/retail/col_spatial_mesh_decoder.cpp` is listed as a compiled
  source in `CMakeLists.txt`; its test unit `native/tests/col_spatial_mesh_decoder_tests.cpp` is
  registered as a CMake/CTest target in the same file.
- Aggregate validation is cross-referenced from three confirmed ledger entries (E-0018, E-0020,
  E-0021) plus a fourth (E-0025) for a downstream diagnostic consumer (`spatial_debug_image`), all
  reporting zero decode errors across the full 7036-span corpus and the full 5351-cell manifest
  traversal.
- Gap: unlike SKAS (E-0093) and the texture-storage debug adapter (E-0066), there is no dedicated
  ledger entry for COL that enumerates exact adversarial/boundary test-case classes and pass counts
  for `col_spatial_mesh_decoder_tests.cpp`; `COL.md` only asserts in prose that "synthetic
  regressions cover both supported versions, direct and node roots, empty normalization, input
  ownership, opaque-field immunity, malformed numerics/references/topology, and exact/one-below
  resource budgets" without a machine-checkable enumeration in the ledger. This is a documentation-
  completeness gap, not a decoder-existence gap — the decoder and its build/test wiring are
  Confirmed above.

## 8. Codex work order

Ranked, privacy-safe, concrete next steps. No menu-role, layout, or gameplay-semantic speculation.

1. **(Highest priority) Backfill a dedicated ledger entry for the COL decoder's adversarial/
   boundary test suite**, matching the granularity of E-0093 (SKAS) and E-0066 (texture debug
   image): read `native/tests/col_spatial_mesh_decoder_tests.cpp` (tracked, non-private) and record
   exact case-class names and pass/fail counts (version boundary, root-shape boundary, empty-
   sentinel normalization, malformed-numeric rejection, malformed-reference rejection, malformed-
   topology rejection, exact/one-below resource-limit boundaries) as a new `E-####` entry in
   `analysis/evidence/ledger.jsonl`, with a `check` command reproducing the CTest run.
2. Extend `tools/fingerprint_assets.py` (or a new small aggregate tool alongside it) to emit a
   COL-count-per-immediate-container-suffix histogram (e.g., counts under DATA.HOG-class vs.
   OBJECTS.HOG-class containers) as an aggregate addition to `asset-fingerprints.json`, closing the
   Section 6 gap on container co-location without naming any specific archive or member.
3. Add a cross-format co-location aggregate pass over the existing `GameDataService` manifest
   traversal (reusing the machinery behind E-0021/E-0033) reporting cell counts by COL/POP/VUM
   membership combination as plain totals, to test Hypothesis H1 structurally without inferring
   gameplay role.
4. Run (or re-run and re-verify) `build/msvc/Debug/omega_tool.exe asset-metadata-verify-tree
   private/extracted-disc` and `level-spatial-verify-tree private/extracted-disc` against the owner
   corpus to reconfirm the E-0018/E-0020/E-0021 aggregate totals are still reproduced with zero
   errors after any decoder change, before merging.
5. Add an aggregate bit-pattern/entropy profile of the four currently-opaque COL fields (`+0x28`
   word, primitive bytes `+0x00/+0x0A/+0x0C`, sentinel bounds bit pattern) using a POP-profiler-
   style tool, to gather evidence toward or against Hypothesis H2 — publish only the aggregate
   value-distribution shape, never per-file rows or byte offsets tied to an individual private
   input.
6. Segment the existing structural-invariant aggregates (node/leaf/triangle/vertex counts, edge
   depth, empty-sentinel rate) by `version_byte` (3 vs. 5) and publish as a small aggregate table in
   `COL.md`, to test Hypothesis H3.
