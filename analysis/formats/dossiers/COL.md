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


No new hypothesis is promoted here. The established evidence above remains the claim ceiling, and
this dossier authorizes no owner-corpus measurement recipe. Before any future measurement is
implemented, a separate reviewed contract must predeclare its fixed public schema, fixed minimum
cohort threshold, bounded execution and typed failures, and project-generated privacy tests.

An authorized report may contain only fixed anonymous corpus-wide totals for cohorts meeting that
threshold. Smaller cohorts must collapse to one typed suppression result. The report must not emit
raw values, signatures, payloads, owner-derived strings, paths, file, container, or archive names,
suffix-derived labels, per-file, per-container, or per-archive rows, or cross-tabulations keyed by
raw fields.

## 6. Missing observations


Unresolved structural, semantic, consumer, and validation questions remain missing observations.
This section deliberately defines no executable collection recipe. Closing any gap requires the
separately reviewed contract and suppression policy stated above; absent that contract, the gap
remains UNKNOWN.

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


1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
