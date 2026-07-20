# .skm — Format Dossier

## 1. Identity

`.skm` is a HOG-archived, FourCC-less container with a compact leading chunk table. At the evidence
tier this repository can defend, `.skm` is a **structurally-validated container family** ("SKM
mesh" per `analysis/formats/ASSET-RECON.md`) — every observed instance satisfies a self-consistent
chunk/qword size formula and carries a fixed version byte. The chunk payloads' vertex/VIF semantics
and any bone-weighting role are **not** decoded; no mesh, skeleton, or rendering meaning is claimed
here.

## 2. Occurrence evidence

| Scope | Count | Source |
| --- | ---: | --- |
| Recursive, inside nested HOGs | 4,219 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".skm"]` and `formats.skm.count` |
| Top-level HOG member suffix | 2,808 | `analysis/formats/hog-validation.json` → `entry_extensions[".skm"]` |
| Whole-disc (outside HOG archives) | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.skm` key present) |

The recursive count (4,219) exceeds the top-level count (2,808), consistent with `.skm` members
also occurring inside nested HOGs (HOGs within HOGs), per the aggregate-scan methodology documented
for other families in `analysis/formats/HOG.md`. In the current tracked inventory, the whole-disc
filesystem-entry count is 0; this establishes no universal rule for other versions or corpora.

## 3. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Citation |
| --- | --- | --- |
| C1 | `.skm` has no FourCC/magic; its first two bytes are `u8 chunk_count` then `u8 version` | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `native/src/retail/skm_container_descriptor.cpp` (`ReadU8(bytes,0)`, `ReadU8(bytes,1)`) |
| C2 | Version byte is fixed at `3` across the entire observed corpus (4,219/4,219) | `analysis/formats/asset-fingerprints.json` → `formats.skm.version_byte: {"3": 4219}`; enforced as `kFormatVersion = 3` in `native/src/retail/skm_container_descriptor.cpp` |
| C3 | Header/chunk-table layout: `header_size = align16(2 + 2*chunk_count)`, then a 2-byte record per chunk (`u8 qword_count`, `u8 secondary_count`) | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `tools/fingerprint_assets.py::fingerprint_skm`; `native/src/retail/skm_container_descriptor.cpp` (`kFixedHeaderBytes=2`, `kChunkRecordBytes=2`, `kContainerAlignment=16`) |
| C4 | Logical asset size formula: `asset_size = header_size + 16 * sum(qword_count)` | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `tools/fingerprint_assets.py::fingerprint_skm` (`logical_size = header_size + 16 * sum(qword_counts)`); `native/src/retail/skm_container_descriptor.cpp` (payload region = `qword_count * kContainerAlignment`, accumulated as `payload_offset`) |
| C5 | The formula holds for all 4,219 observed files (100%); of those, 2,732 occupy their directory span exactly and 1,487 have an all-zero tail | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `analysis/formats/asset-fingerprints.json` → `formats.skm.computed_qword_layout_span_exact: 2732`, `computed_qword_layout_span_zero_padded: 1487` |
| C6 | E-0011 (ledger, confirmed) states the passive fingerprint corpus identifies a stable structural family for SKM (among others) without publishing payloads | `analysis/evidence/ledger.jsonl` id `E-0011` |
| C7 | A native structural descriptor `omega::retail::SkmContainerDescriptor` / `InspectSkmContainer(...)` exists, enforcing: fixed 2-byte header, chunk-count range [1,61], per-chunk qword-count range [4,55], per-chunk secondary-count range [1,30], 16-byte payload/container alignment, format-version must equal 3, and (when the physical span exceeds the computed logical extent) a strict all-zero tail-padding check | `native/include/omega/retail/skm_container_descriptor.h`; `native/src/retail/skm_container_descriptor.cpp` |
| C8 | The descriptor is explicitly documented as passive/structural: "Passive structural inspection only. The descriptor retains no input span and assigns no semantics to chunk payloads or the observed secondary count." | `native/include/omega/retail/skm_container_descriptor.h` (doc comment on `InspectSkmContainer`) |
| C9 | The decoder is registered in the build (source + test) and consumed by the CLI tool's aggregate SKM structural stats (`SKM chunk count`, `SKM qword count`, `SKM logical byte count`) and pass/error counters | `CMakeLists.txt` lines referencing `native/src/retail/skm_container_descriptor.cpp` and `native/tests/skm_container_descriptor_tests.cpp`; `native/apps/omega_tool/asset_commands.cpp` (`InspectSkmContainer` call site, `SkmStructuralStats`, JSON stat emission) |
| C10 | The decoder's arithmetic is overflow-guarded end-to-end (checked `Multiply`/`Add`/`AlignUp` helpers) before any offset or size is used to index the input buffer | `native/src/retail/skm_container_descriptor.cpp` (`Multiply`, `Add`, `AlignUp` helpers and their call sites) |
| C11 | The published aggregate ranges match the decoder's enforced ranges exactly: chunk_count_range [1,61], qwords_per_chunk_range [4,55], secondary_count_per_chunk_range [1,30] | `analysis/formats/asset-fingerprints.json` → `formats.skm.chunk_count_range/qwords_per_chunk_range/secondary_count_per_chunk_range`; `native/src/retail/skm_container_descriptor.cpp` (`kMinimumChunkCount=1`, `kMaximumChunkCount=61`, `kMinimumQwordCount=4`, `kMaximumQwordCount=55`, `kMinimumSecondaryCount=1`, `kMaximumSecondaryCount=30`) |
| C12 | `.skm` is listed among the approved public suffixes with fixed corpus counts and among the fixed "mesh" category counts | `analysis/formats/FRONTEND-TOPOLOGY.md` (approved-suffix and category-count lists) |
| C13 | `.skm` is explicitly disclaimed as having no established relationship to SKA/SKL/actor/animation/runtime semantics, and no established lookup/rendering/gameplay behavior, in the SKAS scope-boundary doc | `analysis/formats/SKAS.md` (scope-exclusion list naming SKM) |

## 4. Aggregate-only facts

No semantic interpretation attached; these are corpus-wide numeric aggregates only.

- Recursive occurrence count: 4,219 (`analysis/formats/asset-fingerprints.json` → `scan.extensions[".skm"]`).
- Chunk-count range across the corpus: 1–61 (`formats.skm.chunk_count_range`).
- Qwords-per-chunk range: 4–55 (`formats.skm.qwords_per_chunk_range`).
- Secondary-count-per-chunk range: 1–30 (`formats.skm.secondary_count_per_chunk_range`).
- Span-bytes range across the corpus: 128–40,960 bytes (`formats.skm.span_bytes_range`).
- Computed qword-layout span: exact match in 2,732 files, zero-padded tail in 1,487 files, out of 4,219 total (`formats.skm.computed_qword_layout_span_exact`, `computed_qword_layout_span_zero_padded`).
- Zero-padding-byte-count range (for the 1,487 zero-padded files): 16–2,032 bytes (`formats.skm.computed_qword_layout_span_padding_bytes_range`).
- Version byte distribution: 100% (4,219/4,219) carry version byte 3 (`formats.skm.version_byte`).
- Alignment observation: the fixed header, per-chunk payload regions, and (per the native decoder) the overall physical span are all 16-byte aligned; the fingerprint script's `align_up(2 + 2*chunk_count, 16)` and the decoder's `kContainerAlignment = 16` agree (`tools/fingerprint_assets.py`; `native/src/retail/skm_container_descriptor.cpp`).

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

**Classification: `passive_descriptor_only`**

Justification:
- A native decoder exists — `omega::retail::InspectSkmContainer` in
  `native/include/omega/retail/skm_container_descriptor.h` and
  `native/src/retail/skm_container_descriptor.cpp` — that parses the chunk table, validates all
  observed range constraints (chunk count, qword count, secondary count, version byte), computes the
  chunk-table/payload byte regions and overall logical extent, and verifies zero-padded tails when
  present.
- It is explicitly self-documented as producing **no payload semantics**: "The descriptor retains no
  input span and assigns no semantics to chunk payloads or the observed secondary count"
  (`skm_container_descriptor.h` doc comment). The authoritative coverage matrix therefore calls it
  `passive_descriptor_only`: the typed descriptor validates observed regions and bounds but produces
  no mesh, vertex, bone, or other canonical asset output.
- Build/test registration: both the source and a dedicated unit test file are registered in
  `CMakeLists.txt` (lines referencing `native/src/retail/skm_container_descriptor.cpp` and
  `native/tests/skm_container_descriptor_tests.cpp`).
- Test coverage observed in `native/tests/skm_container_descriptor_tests.cpp`: a synthetic
  `.skm` builder (`MakeSkm`) generating well-formed fixtures, plus explicit checks against
  `DecodeLimits` (`asset::DecodeErrorCode::LimitExceeded`) at three distinct limit thresholds (lines
  ~242–272) — i.e., resource-boundary behavior (oversized input, item-count limit, output-size limit)
  is exercised. This is a genuine adversarial/resource-boundary test, not merely a happy-path test.
  The same test file covers every fixed-header, chunk-table, aligned-header, and payload truncation
  stage; wrong versions; out-of-range chunk/qword/secondary counts; non-aligned spans; dirty tails;
  exact and one-below input, item, and output limits; and zero scratch/depth behavior.
- Tool integration: `native/apps/omega_tool/asset_commands.cpp` calls `InspectSkmContainer` over the
  aggregate corpus and emits pass/fail counts, extent-relation counts (`exact`/`zero_padded_tail`/
  `nonzero_tail`/`exceeds_input`), and semantic-counter aggregates (`SKM chunk count`, `SKM qword
  count`, `SKM logical byte count`) as JSON — an aggregate-scanner consumer layered on top of the
  passive descriptor, consistent with the classification above.

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
