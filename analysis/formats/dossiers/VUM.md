# .vum — Evidence Dossier

## 1. Identity

`.vum` is a game-asset container format found only nested inside HOG archives (magic
`VUMS`, 92-byte header, 16-byte-aligned span). Mechanically confirmed content is a
**material catalog**: a name preamble plus a fixed table of 92-byte `MTRL` records that
reference those names. Beyond the material catalog, the span carries three additional
bounded regions (post-material metadata records and two payload regions) whose structural
shape is confirmed but whose semantic role (geometry, rendering, or otherwise) is
explicitly **not established** by tracked evidence. There is no confirmed relationship
between `.vum` catalog names and any specific texture, shader, mesh, or gameplay object.

## 2. Occurrence evidence

| Metric | Count | Tracked source |
| --- | ---: | --- |
| Recursive occurrences inside HOG archives | 7,036 | analysis/formats/asset-fingerprints.json (`formats.vum.count`, `scan.extensions[".vum"]`) |
| Top-level HOG member occurrences | 0 | analysis/formats/hog-validation.json |
| Whole-disc (non-nested) occurrences | 0 | analysis/manifests/disc-summary.json / analysis/manifests/disc-files.jsonl |
| `VUMS` magic match rate | 7,036 / 7,036 | analysis/formats/asset-fingerprints.json (`formats.vum.vums_magic`) |
| Per-level-container `.vum` presence (e.g. `MINSK` sample) | `DATA.HOG`: 301; `MAPVUM.HOG`: 5 | analysis/formats/asset-fingerprints.json (`minsk.containers.DATA.HOG.vum_count`, `minsk.containers.MAPVUM.HOG.vum_count`) |

Note: `MAPVUM.HOG` is a top-level HOG **container name** already present in tracked
per-level manifests (e.g. `GAMEDATA/<LEVEL>/MAPVUM.HOG`, 18 occurrences across levels per
`analysis/formats/hog-validation.json`); it is a container, not a `.vum` member, and is
listed here only because tracked evidence associates it with nested `.vum` members.

This family occurs only nested inside HOG archives (7,036 recursive, 0 top-level, 0
whole-disc), confirming it is an in-scope game-asset format warranting a full dossier.

## 3. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| C1 | Ledger entry E-0011 (state: confirmed) attests the passive asset-fingerprint corpus identifies a stable structural family for VUM (among others) across 6,677 nested HOGs, without publishing payloads. | analysis/evidence/ledger.jsonl |
| C2 | Every span begins with magic `VUMS`; header is 92 bytes; span is 16-byte aligned; all integers little-endian. | analysis/formats/VUM.md |
| C3 | Header word `0x14` = count of NUL-terminated names in the preamble; `0x18` = count of fixed 92-byte `MTRL` records; `0x50`/`0x54`/`0x58` are the ordered end-of-name-preamble, end-of-fixed-table, and end-of-primary-payload boundaries. | analysis/formats/VUM.md |
| C4 | Fixed-table relation holds exactly across the whole corpus: `endpoint_0x54 - endpoint_0x50 == word_0x18 * 92`. | analysis/formats/VUM.md |
| C5 | Primary endpoint (`0x58`) equals the input span length in 6,989 of 7,036 files; 47 files have an additional nonzero trailing region. | analysis/formats/VUM.md; analysis/formats/asset-fingerprints.json (`primary_boundary_span_exact`=6989, `primary_boundary_span_nonzero_tail`=47) |
| C6 | Corpus-wide totals: 38,793 names, 38,899 fixed `MTRL` records, 42,631 dense active name references, 220,943 post-material metadata records (91,460 P + 91,460 Q + 38,023 T), 134,122 middle-to-final references, 365,840 ordered Q/P final references. | analysis/formats/VUM.md |
| C7 | Every fixed record begins with magic `MTRL`; bytes `0x10`–`0x37` are zero; words `0x44` and `0x54` are always `0xFFFFFFFF`; active/inactive name-reference slots at `0x38/0x3C/0x40` are validated in-range or `0xFFFFFFFF`. | analysis/formats/VUM.md |
| C8 | Middle-payload Q partitions are exactly one of four sizes (16, 256, 480, 704 bytes), each fitting `32 + 224 * group` for group 0–3, with observed counts 48,798 / 35,408 / 3,556 / 3,698 respectively. | analysis/formats/VUM.md |
| C9 | Span byte-length range across the corpus is [240, 580112]; all 7,036 spans are 16-byte aligned; `observed_word_0x1c` and `variant_word` aggregate histograms are recorded. | analysis/formats/asset-fingerprints.json (`formats.vum`) |
| C10 | A canonical adapter `DecodeVumMaterialCatalog` (plus a measured variant `DecodeVumMaterialCatalogMeasured`) exists in `omega::retail`, returning an owned `asset::MaterialCatalogIR` (strings + per-record dense name indices) with no retained input span. | native/include/omega/retail/vum_material_catalog_decoder.h; native/src/retail/vum_material_catalog_decoder.cpp |
| C11 | A separate, explicitly retail-only passive descriptor `InspectVumRenderPayload` exists, returning `VumRenderPayloadDescriptor`/`VumRenderPayloadPairDescriptor`, documented to expose no packet word, opcode, register, renderer object, vertex, index, or material assignment, and to remain confined to `omega::retail` (must not cross into renderer/simulation code). | native/include/omega/retail/vum_render_payload_descriptor.h; native/src/retail/vum_render_payload_descriptor.cpp |
| C12 | Both decoders are registered in the build and covered by dedicated test files. | CMakeLists.txt lines 106–108 (sources), 1407–1408 (tests: `vum_material_catalog_decoder_tests.cpp`, `vum_render_payload_descriptor_tests.cpp`) |
| C13 | `fingerprint_vum` is a registered structural handler for suffix `.vum` (checks `VUMS` magic) in the fingerprinting tool; `.vum` is also cross-checked per-container against its declared header size field for `DATA.HOG` and `MAPVUM.HOG`. | tools/fingerprint_assets.py lines 461–469, 509, 661–698 |
| C14 | The render-geometry gate found the flat-position hypothesis fails decisively (0 of 884,718 middle-region 16-byte candidates homogeneous), no tested final-region fixed-stride family covers the corpus, and the paired COL mesh is not a substitute (only 2/4,320 nonempty COL/VUM sibling pairs have equal bounds; only 105/949,762 COL vertices recur in any paired final payload). | analysis/formats/VUM.md |
| C15 | A native aggregate verifier (`omega_tool.exe asset-metadata-verify-tree`) and a level-composition verifier (`omega_tool.exe level-material-catalogs-verify-tree`) both exist and are documented to emit only sanitized aggregate counts, with a stated zero-error baseline of 7,036 catalogs / 18 levels / 5,351 manifest-referenced catalogs. | analysis/formats/VUM.md; native/apps/omega_tool/asset_commands.cpp |
| C16 | A bounded, privacy-safe consumer-read trace gate (`tools/validate_vum_read_trace.py`) exists and has passed twice with byte-identical/reproduced aggregate results confined to the already-opaque header-vector block; it explicitly does not establish runtime semantics. | analysis/formats/VUM.md |
| C17 | A full normalized-string-equality experiment against direct `.TDX` locators found **zero** matches under exact full-string comparison, and a follow-up one-terminal-extension-elision variant found matches only after stripping one syntactic extension (34,267 names / 34,589 material records), explicitly not proving retail lookup or texture binding. | analysis/formats/VUM.md |

## 4. Aggregate-only facts

Aggregate counts/ranges with no semantic interpretation attached (already published in
`analysis/formats/VUM.md` and `analysis/formats/asset-fingerprints.json`):

- Span byte-length range: 240–580,112 bytes; 100% 16-byte aligned (7,036/7,036).
- `variant_word` histogram (an opaque header word) is multi-valued (0,1,2,3,8,9,10,11,17,19)
  with a dominant bucket at value 0 (3,406 of 7,036).
- `observed_word_0x1c` histogram is dominated by value 0 (6,989 of 7,036) with a long tail
  of small nonzero values (1,2,3,4,5,6,10,12,14,15,21).
- Names-per-catalog and materials-per-catalog maxima observed in the manifest-scoped
  population: 140 names/catalog, 140 materials/catalog, 142 dense references/catalog
  (independent maxima, not tied to a specific catalog identity).
- Middle-payload size-bucket distribution: 16B (48,798), 256B (35,408), 480B (3,556),
  704B (3,698) — aggregate counts only, no assigned meaning to the size classes beyond the
  structural-group correlation already stated in Confirmed fact C8.
- Per-container `.vum` presence counts for one sampled level container set (`DATA.HOG`:
  301; `MAPVUM.HOG`: 5) — aggregate per-container counts only, not member-level detail.

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

**Classification: `canonical_decoder`** (for the material-catalog portion of the format),
with an explicitly separate **`passive_descriptor_only`** component layered on top for the
render-payload regions.

- Canonical decoder: `omega::retail::DecodeVumMaterialCatalog` /
  `DecodeVumMaterialCatalogMeasured`, declared in
  `native/include/omega/retail/vum_material_catalog_decoder.h`, implemented in
  `native/src/retail/vum_material_catalog_decoder.cpp` (plus internal layout helpers in
  `native/src/retail/vum_layout_internal.{h,cpp}`), returning an owned
  `asset::MaterialCatalogIR` (see `native/include/omega/asset/level_material_catalogs_ir.h`).
  Registered in `CMakeLists.txt` (lines 106–108) and unit-tested in
  `native/tests/vum_material_catalog_decoder_tests.cpp` (registered at line 1407). VUM.md
  states the decoder validates all counted arithmetic/boundaries, preflights output/item
  counts before allocation, bounds each name by `maximum_string_bytes`, needs no dynamic
  scratch, and fails closed on unsupported layouts — i.e. it has documented
  adversarial/resource-boundary coverage ("hostile counts", "exact/one-below input, item,
  and output budgets" per VUM.md's synthetic-regression list).
- Passive descriptor: `omega::retail::InspectVumRenderPayload`, declared in
  `native/include/omega/retail/vum_render_payload_descriptor.h`, implemented in
  `native/src/retail/vum_render_payload_descriptor.cpp`, registered in `CMakeLists.txt`
  (line 108/1408) and tested in
  `native/tests/vum_render_payload_descriptor_tests.cpp`. This is explicitly documented
  (VUM.md, header comments) as retail-only structural scaffolding — not canonical asset IR
  — confined to `omega::retail` and forbidden from crossing into renderer/simulation code.
  It exposes no packet/opcode/register/renderer semantics.
- Gap: no decoder or descriptor of any kind covers the interpretation of the opaque
  header/record words (`0x20`–`0x4F`, `0x08`/`0x0C`, `0x48`/`0x4C`/`0x50`) or assigns any
  render/topology meaning to the middle/final payload contents — these remain explicitly
  unassigned per VUM.md's render-geometry gate, consistent with keeping the family
  conservatively scoped rather than fabricating a decoder the evidence does not support.
- Native validation entry points: `omega_tool.exe asset-metadata-verify-tree` and
  `omega_tool.exe level-material-catalogs-verify-tree` (native/apps/omega_tool/asset_commands.cpp),
  both documented in VUM.md as emitting sanitized aggregate counts only.

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
