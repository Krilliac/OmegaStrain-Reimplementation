# Decoder coverage matrix

## Scope and method

This document is derived mechanically from tracked repository source: the suffix inventory in
`analysis/formats/asset-fingerprints.json` (produced by `tools/fingerprint_assets.py`), the
narrower archive-level inventory in `analysis/formats/hog-validation.json` (produced by
`tools/validate_hogs.py`), the whole-disc file tally in `analysis/manifests/disc-summary.json`
(produced by `tools/generate_manifest.py`), the frozen category map in
`tools/measure_frontend_hog_topology.py`, every header under `native/include/omega/retail/` and
`native/include/omega/asset/`, every corresponding file under `native/src/`, and `CMakeLists.txt`.
No claim below rests on memory, plausibility, or naming intuition; every row cites the
repo-relative tracked file(s) it comes from. Where a suffix has no citable tooling or decoder
beyond a raw count, it is marked `unknown` rather than inferred.

Categories (exactly one per family, with layering noted in Section 3 for families that have more
than one native code path):

- **canonical decoder** — a native `Decode*` function owns a canonical `asset::*IR` result type
  (from `native/include/omega/asset/`), routed through `asset::DecodeResult`/`asset::DecodeLimits`
  (`native/include/omega/asset/decode.h`) or an equivalent typed result, with a focused test.
- **structural envelope only** — a native `Decode*` function exists and is bounded/tested, but its
  own doc comment states no semantic meaning is assigned to the decoded fields (an "envelope" or
  "wrapper" result, not a canonical semantic IR).
- **passive descriptor only** — a native `Inspect*` function exists and is bounded/tested, but it
  never claims to "decode" the payload; it returns a `*Descriptor` struct of observed extents and
  raw header words only.
- **aggregate scanner only** — no native code exists; only Python tooling purpose-built to
  inventory game asset formats (`tools/fingerprint_assets.py`, `tools/validate_hogs.py`,
  `tools/measure_frontend_hog_topology.py`, `tools/inspect_so.py`) observes the suffix, at most as
  a raw count or a generic container-topology bucket.
- **unknown** — observed only in the generic whole-disc file manifest
  (`analysis/manifests/disc-summary.json`) and/or a scanner's aspirational vocabulary map, with
  zero occurrences in either asset-directed archive inventory
  (`asset-fingerprints.json`/`hog-validation.json`). This is also the mandatory bucket for `.PF`
  and `.TM2` per the campaign's hard rules, regardless of what any manifest tool counts.

No native decoder, envelope, or descriptor in this tree asserts retail menu role, lookup rule,
layout, timing, rendering, audio, owner-corpus coverage, or PCSX2 equivalence. Those remain false
unless separately demonstrated, independent of the classification below.

## 1. Suffix classification matrix

| Suffix | Class | Native/tool evidence | Test evidence |
|---|---|---|---|
| `.col` | canonical decoder | `DecodeColSpatialMesh` → `asset::SpatialMeshIR` (`native/include/omega/retail/col_spatial_mesh_decoder.h`, `native/src/retail/col_spatial_mesh_decoder.cpp`, IR at `native/include/omega/asset/spatial_mesh_ir.h`) | `native/tests/col_spatial_mesh_decoder_tests.cpp` (built into `omega_core_tests`, `CMakeLists.txt:1382`) |
| `.hog` | canonical decoder | `HogArchive`/`HogIndex::Open`/`OpenRange`/`FromBytes`/`FromSpan`/`FromOwnedBytes` (`native/include/omega/archive/hog_archive.h`, `native/src/archive/hog_archive.cpp`) | `native/tests/hog_archive_tests.cpp` (`omega_core_tests`, `CMakeLists.txt:1390`) |
| `.pop` | canonical decoder | `DecodePopLevelManifest` → `asset::LevelManifestIR` (`native/include/omega/retail/pop_level_manifest_decoder.h`, `native/src/retail/pop_level_manifest_decoder.cpp`), built atop `asset::PopTerrainIndex::Parse` (`native/include/omega/asset/pop_terrain_index.h`, `native/src/asset/pop_terrain_index.cpp`) for the `TER:` prefix only | `native/tests/pop_level_manifest_decoder_tests.cpp`, `native/tests/pop_terrain_index_tests.cpp` (both `omega_core_tests`, `CMakeLists.txt:1396,1398`) |
| `.tdx` | canonical decoder | `DecodeTdxTextureStorage`/`DecodeTdxTextureStorageMeasured` → `asset::TextureStorageIR` (`native/include/omega/retail/tdx_texture_storage_decoder.h`, `native/src/retail/tdx_texture_storage_decoder.cpp`) | `native/tests/tdx_texture_storage_decoder_tests.cpp` (`omega_core_tests`, `CMakeLists.txt:1405`) |
| `.vag` | canonical decoder | `DecodeVagAdpcm` → `asset::MonoPcm16IR` (`native/include/omega/retail/vag_adpcm_decoder.h`, `native/src/retail/vag_adpcm_decoder.cpp`, IR at `native/include/omega/asset/audio_ir.h`) | `native/tests/vag_adpcm_decoder_tests.cpp` (own executable `omega_vag_adpcm_decoder_tests`, `CMakeLists.txt:565-588`) |
| `.vum` | canonical decoder (mixed — see §3) | `DecodeVumMaterialCatalog`/`…Measured` → `asset::MaterialCatalogIR` (`native/include/omega/retail/vum_material_catalog_decoder.h`, `native/src/retail/vum_material_catalog_decoder.cpp`, IR at `native/include/omega/asset/material_catalog_ir.h`) | `native/tests/vum_material_catalog_decoder_tests.cpp` (`omega_core_tests`, `CMakeLists.txt:1407`) |
| `.lpd` | structural envelope only | `DecodeLpdEnvelope` → `asset::LpdEnvelopeIR`, doc comment: "No meaning is assigned to tracks or four-byte entries" (`native/include/omega/retail/lpd_envelope_decoder.h`, `native/src/retail/lpd_envelope_decoder.cpp`) | `native/tests/lpd_envelope_decoder_tests.cpp` (own executable `omega_lpd_envelope_decoder_tests`, `CMakeLists.txt:1425-1448`) |
| `.par` | structural envelope only | `DecodeParTextEnvelope` → `asset::ParTextEnvelopeIR`, doc comment: "assigns no keys, values, comments, fields, paths, particle behavior" (`native/include/omega/retail/par_text_envelope_decoder.h`) | `native/tests/par_text_envelope_decoder_tests.cpp` (own executable `omega_par_text_envelope_tests`, `CMakeLists.txt:745-768`) |
| `.skas` | structural envelope only | `DecodeSkasTextEnvelope` → `asset::SkasTextEnvelopeIR`, opaque-line pattern (`native/include/omega/retail/skas_text_envelope_decoder.h`) | `native/tests/skas_text_envelope_decoder_tests.cpp` (own executable `omega_skas_text_envelope_decoder_tests`, `CMakeLists.txt:590-614`) |
| `.vpk` | structural envelope only | `DecodeVpkWrapperEnvelope` → `VpkWrapperEnvelopeDescriptor`, doc comment: "Retail-only passive structure... never retained" (`native/include/omega/retail/vpk_wrapper_envelope_decoder.h`) | `native/tests/vpk_wrapper_envelope_decoder_tests.cpp` (own executable `omega_vpk_wrapper_envelope_decoder_tests`, `CMakeLists.txt:616-640`) |
| `.ska` | passive descriptor only | `InspectSkaContainer` → `SkaContainerDescriptor` (`native/include/omega/retail/ska_container_descriptor.h`, `native/src/retail/ska_container_descriptor.cpp`) | `native/tests/ska_container_descriptor_tests.cpp` (`omega_core_tests`, `CMakeLists.txt:1402`) |
| `.skl` | passive descriptor only | `InspectSklContainer` → `SklContainerDescriptor` (`native/include/omega/retail/skl_container_descriptor.h`, `native/src/retail/skl_container_descriptor.cpp`) | `native/tests/skl_container_descriptor_tests.cpp` (`omega_core_tests`, `CMakeLists.txt:1404`) |
| `.skm` | passive descriptor only | `InspectSkmContainer` → `SkmContainerDescriptor` (`native/include/omega/retail/skm_container_descriptor.h`, `native/src/retail/skm_container_descriptor.cpp`) | `native/tests/skm_container_descriptor_tests.cpp` (`omega_core_tests`, `CMakeLists.txt:1403`) |
| `.so` | aggregate scanner only | `tools/inspect_so.py` (custom little-endian VM-module structural grammar; output `analysis/formats/so-validation.json`); no native header under `native/include/omega/retail/` targets `.so` | none (Python-only; `tools/inspect_so.py` has no `tools/tests/test_inspect_so.py` found in this pass) |
| `.gui` | aggregate scanner only | Raw suffix count in `asset-fingerprints.json` (`scan.extensions[".gui"]`) and in `hog-validation.json` (`entry_extensions[".gui"]`); `tools/measure_frontend_hog_topology.py` currently maps `.gui` → category `gui` in `APPROVED_EXTENSION_CATEGORIES` (schema_version 2, observed in the tracked file at read time) — this is a container-topology label, not a structural field schema | `tools/tests/test_measure_frontend_hog_topology.py` covers the topology label only, not a `.gui` payload schema |
| `.fnt` | aggregate scanner only | Raw suffix count only, in both `asset-fingerprints.json` and `hog-validation.json`; `analysis/formats/FRONTEND-TOPOLOGY.md` explicitly documents it as deliberately left out of the approved vocabulary ("menu-adjacent-sounding suffixes such as `.fnt` and `.ie` deliberately remain in the `other` bucket") | none |
| `.ie` | aggregate scanner only | Same as `.fnt` — raw count only, explicitly named as excluded in `FRONTEND-TOPOLOGY.md` | none |
| `.bin` | aggregate scanner only | Raw suffix count in `asset-fingerprints.json`/`hog-validation.json`; also present in `tools/check_public_tree.py`'s commit-blocklist (existence signal only, not structural evidence) | none |
| `.bnk` | aggregate scanner only | Raw suffix count only (`asset-fingerprints.json`, `hog-validation.json`) | none |
| `.bon` | aggregate scanner only | Raw suffix count only | none |
| `.gun` | aggregate scanner only | Raw suffix count only (`asset-fingerprints.json`; absent from `hog-validation.json`'s narrower top-level tally) | none |
| `.prn` | aggregate scanner only | Raw suffix count only | none |
| `.pss` | aggregate scanner only | Raw suffix count only; also in `check_public_tree.py`'s blocklist | none |
| `.scc` | aggregate scanner only | Raw suffix count only in both archive inventories, plus a separate whole-disc count in `disc-summary.json` | none |
| `.skel` | aggregate scanner only | Raw suffix count only | none |
| `.skf` | aggregate scanner only | Raw suffix count only | none |
| `.sub` | aggregate scanner only | Raw suffix count only | none |
| `.txt` | aggregate scanner only | Raw suffix count in the recursive `asset-fingerprints.json` inventory (3 spans), but no `.txt` key in top-level `hog-validation.json` `entry_extensions`; `disc-summary.json` separately records 8 whole-disc occurrences. Also listed in `measure_frontend_hog_topology.py`'s `APPROVED_EXTENSION_CATEGORIES` as category `text` (topology label only) | Topology-label coverage only, via `test_measure_frontend_hog_topology.py` |
| `.tbl` | unknown | Zero occurrences in `asset-fingerprints.json` or `hog-validation.json`'s in-archive inventories; appears only in `measure_frontend_hog_topology.py`'s `APPROVED_EXTENSION_CATEGORIES` (category `table`) and as one whole-disc, outside-any-HOG occurrence in `analysis/manifests/disc-summary.json` | none |
| `.PF` (`.pf`) | unknown (hard-rule mandated) | Zero occurrences in either in-archive inventory; exactly 3 whole-disc occurrences in `analysis/manifests/disc-summary.json`, outside any HOG archive | none |
| `.TM2` (`.tm2`) | unknown (hard-rule mandated) | Zero occurrences in either in-archive inventory; exactly 16 whole-disc occurrences in `analysis/manifests/disc-summary.json`, outside any HOG archive | none |

Totals observed in this pass: **6 canonical decoder**, **4 structural envelope only**, **3 passive
descriptor only**, **15 aggregate scanner only**, **3 unknown** — 31 families.

## 2. CMake / test registration cross-check

Every native header listed above as canonical/envelope/descriptor has a matching `.cpp` under
`native/src/retail/` (or `native/src/archive/`, `native/src/asset/`) listed in `CMakeLists.txt`'s
source list (lines 40-41, 93-108), and a matching focused test file that is registered as either
its own `add_executable`/`add_test` pair or a member of `omega_core_tests`
(`CMakeLists.txt:1381-1409`). **No missing CMake registration was found** for any of the 13
retail-format headers or `hog_archive`/`pop_terrain_index`/`container_descriptors`.

## 3. Mixed-layer families and mechanical inconsistencies (not fixed)

Three suffixes have *more than one* native code path, and the three take three different
composition shapes:

- **`.tdx`** — `DecodeTdxTextureStorage` calls `InspectTdxContainer`
  (`native/include/omega/retail/container_descriptors.h`) internally as its first step
  (`native/src/retail/tdx_texture_storage_decoder.cpp:278`). The passive descriptor is a genuine
  foundation of the canonical decoder.
- **`.vum`** — `DecodeVumMaterialCatalog` (canonical) and `InspectVumRenderPayload` (passive,
  `native/include/omega/retail/vum_render_payload_descriptor.h`) both depend on a private shared
  helper, `native/src/retail/vum_layout_internal.h`/`.cpp`, but neither calls the other. A third,
  unrelated `InspectVumContainer` (`container_descriptors.h`) exists in parallel for the top-level
  container envelope.
- **`.col`** — `DecodeColSpatialMesh` and `InspectColContainer` (`container_descriptors.h`) are
  fully independent parses of the same bytes; `col_spatial_mesh_decoder.cpp` does not include
  `container_descriptors.h` at all.

Three different "decoder built on / next to / sharing internals with descriptor" shapes exist for
what looks like the same layering problem. This is a mechanical inconsistency, not a defect —
noted, not fixed.

Other mechanical inconsistencies found while cross-checking:

1. **Cited hot files that do not exist.** The campaign brief's hot-file list names
   `native/include/omega/asset/decode_result.h` and
   `native/include/omega/asset/decode_test_hooks.h`. Neither file exists in the tracked tree
   (`native/include/omega/asset/` contains only `decode.h`, `audio_ir.h`, `level_content_ir.h`,
   `level_ir.h`, `level_material_catalogs_ir.h`, `level_spatial_ir.h`, `lpd_envelope_ir.h`,
   `material_catalog_ir.h`, `par_text_envelope_ir.h`, `pop_terrain_index.h`,
   `skas_text_envelope_ir.h`, `source_locator.h`, `spatial_mesh_ir.h`, `texture_storage_ir.h`).
   The shared `DecodeLimits`/`DecodeErrorCode`/`DecodeError`/`DecodeResult` contract actually lives
   entirely inside `native/include/omega/asset/decode.h`. There is also no shared
   allocation-failure test-hook header anywhere under `native/`; each decoder test (for example
   `native/tests/lpd_envelope_decoder_tests.cpp`) defines its own local
   `ArmAllocationFailure`/`DisarmAllocationFailure` helpers rather than sharing one.
2. **Stale filename in the brief.** The brief cites
   `native/include/omega/retail/tdx_decoder.h`; the tracked file is
   `native/include/omega/retail/tdx_texture_storage_decoder.h`.
3. **Decode-result contract fragmentation.** Most retail decoders/descriptors return
   `asset::DecodeResult<T>` (`= std::expected<T, asset::DecodeError>` from `decode.h`), but
   `native/include/omega/archive/hog_archive.h`'s `HogArchive`/`HogIndex` factory functions return
   `std::expected<T, std::string>` (an untyped string error, no code/offset), and
   `native/include/omega/asset/pop_terrain_index.h`'s `PopTerrainIndex::Parse` returns
   `std::expected<T, PopTerrainParseError>` with its own separate error-code enum and its own
   `PopTerrainParseLimits` type rather than `asset::DecodeLimits`. Three different result/limit
   shapes coexist for what is described as one common contract.
4. **`Decode`/`Inspect` naming is not type-enforced.** `DecodeVpkWrapperEnvelope`
   (`vpk_wrapper_envelope_decoder.h`) is `Decode`-named but returns a passive
   `VpkWrapperEnvelopeDescriptor` whose own doc comment calls it a "passive structure" that
   retains no payload — the same shape the `Inspect`-named descriptors use elsewhere. Nothing
   besides documentation convention enforces the split between "envelope decoder" and "passive
   descriptor."
5. **Minor test-name drift.** `native/include/omega/retail/container_descriptors.h` (plural) is
   exercised by `native/tests/container_descriptor_tests.cpp` (singular). Registration is correct;
   the name just doesn't match the header.
6. **Scope mismatch between the two archive-level inventories.** `asset-fingerprints.json`
   (`tools/fingerprint_assets.py`) reports `.col`/`.vum` counts (7,036 each) that
   `hog-validation.json` (`tools/validate_hogs.py`) does not list at all in its
   `entry_extensions` map, even though both report the same `total_entries`/`top_level_hog_entries`
   value (32,351). `asset-fingerprints.json` additionally scans nested-archive spans
   (`asset_spans_scanned`: 53,281) where `.col`/`.vum` pairs actually live; `hog-validation.json`
   stops at the top level. This is an expected scope difference between the two tools, not a bug,
   but the two suffix tallies are not directly diffable without accounting for it.
7. **The most-decoded family is the worst-represented in the primary suffix inventory.** `.pop`
   has a full canonical decoder plus two dedicated tools (`tools/scan_pop_post_terrain.py`,
   `tools/probe_native_levels.py`), yet it does not appear in `asset-fingerprints.json`'s own
   `scan.extensions` suffix tally at all — the brief names that JSON as the starting inventory.
   Its only suffix-level corroboration is the generic whole-disc tally in
   `analysis/manifests/disc-summary.json` (18 occurrences) and the literal filename in
   `tools/probe_native_levels.py`.
8. **`.tbl` is a vocabulary slot with no observed member.** `tools/measure_frontend_hog_topology.py`
   freezes `.tbl` into `APPROVED_EXTENSION_CATEGORIES` (category `table`), and
   `analysis/formats/FRONTEND-TOPOLOGY.md` documents it as an approved public suffix, but it has
   zero occurrences in either in-archive inventory and exactly one whole-disc, outside-any-HOG
   occurrence. As tracked today, that scanner branch may never fire against a real archive member.

## 4. Ranked evidence-collection queue (menu-relevant families only)

Ranked by how directly current tracked evidence motivates the next step — not by speculation
about retail menu roles. Each entry names the missing evidence and the privacy-safe aggregate
collection that would produce it.

1. **Per-suffix structural fingerprint for `.gui`.** Tracked evidence: `.gui` already has a raw
   count in both archive inventories and its own topology category
   (`tools/measure_frontend_hog_topology.py`'s `APPROVED_EXTENSION_CATEGORIES`), but — unlike
   `.col`/`.lpd`/`.par`/`.ska`/`.skas`/`.skl`/`.skm`/`.tdx`/`.vag`/`.vpk`/`.vum` — it has no entry
   in `asset-fingerprints.json`'s per-suffix `formats` block (header-size consistency, span
   alignment, magic bytes). Missing evidence: whether `.gui` members share a fixed header size or
   bounded span family the way every other approved suffix already does. Collection: extend
   `tools/fingerprint_assets.py`'s existing bounded, aggregate-only per-suffix pass to `.gui`,
   exactly the pattern already proven for the eleven other families in that file.
2. **Per-suffix structural fingerprint for `.ie`.** Tracked evidence: 79 occurrences in
   `asset-fingerprints.json`, 23 in the narrower `hog-validation.json` — the largest sample among
   the suffixes `FRONTEND-TOPOLOGY.md` explicitly calls "menu-adjacent-sounding" and explicitly
   declines to promote for lack of evidence. Missing evidence: any structural regularity at all.
   Collection: same `fingerprint_assets.py` extension as (1); 79 members is large enough to test
   for a fixed-schema family the way `.skas` (2 members) already was.
3. **Per-suffix structural fingerprint for `.fnt`.** Tracked evidence: only 3 occurrences in
   either inventory — the smallest sample among the explicitly named menu-adjacent suffixes.
   Missing evidence: whether even a 3-member sample is internally consistent enough to call a
   candidate family. Collection: same extension as (1)/(2); ranked below `.gui`/`.ie` purely
   because n=3 cannot establish a family on its own, only a candidate observation.
4. **Sibling same-basename pairing once `.gui`/`.fnt`/`.ie` have fingerprints.**
   `FRONTEND-TOPOLOGY.md`'s frozen schema already computes "every unordered pair of approved
   suffixes that shares a normalized sibling basename," but only across the approved vocabulary.
   Missing evidence: whether `.gui`, `.fnt`, or `.ie` members ever co-locate (same basename) with
   `.tdx`, `.vum`, or `.txt` members — the most direct structural precursor to a font/texture/table
   co-location signal, still asserting zero semantics. Collection: once (1)-(3) establish that a
   suffix is structurally uniform enough to be worth promoting, reuse the existing pair-key
   machinery in `tools/measure_frontend_hog_topology.py` (Agent B/C's schema-versioned extension,
   not to be duplicated here).
5. **Per-suffix structural fingerprint for `.bnk` and `.gun`.** Tracked evidence: 77 and 624
   occurrences respectively (`asset-fingerprints.json`), both large samples, with zero structural
   evidence beyond the raw count and no menu-adjacent naming caveat recorded anywhere. Missing
   evidence: whether either is a uniform fixed-schema container at all — `.bnk`'s naming
   convention alone is not asserted as evidence of a "soundbank" role. Collection: same
   `fingerprint_assets.py` extension as (1); relevant to future menu-audio research only if a
   structural family emerges.
6. **Whether `.tbl` ever occurs inside a HOG archive.** Tracked evidence: `.tbl` is frozen into
   the topology scanner's approved vocabulary and category map, but has never been observed inside
   any archive in either archive-level inventory — only once, outside any archive, in the
   whole-disc tally. Missing evidence: does this suffix ever appear as an actual HOG member.
   Collection: no new tool needed — the next full `tools/validate_hogs.py`/
   `tools/fingerprint_assets.py` run against a directory tree that happens to contain a `.tbl`
   member (if one exists) would settle this; until then it should be flagged as an untested
   vocabulary slot rather than assumed populated.
7. **Confirm `.PF`/`.TM2` remain absent from every archive-level inventory.** Tracked evidence:
   both are true only at the whole-disc level (`disc-summary.json`, 3 and 16 occurrences
   respectively) and are explicitly named in the campaign's hard rules as families to keep
   `unknown` regardless. Missing evidence: nothing changes this without new tracked in-archive
   evidence. Collection: none proposed here — this entry exists only to record that the current
   `unknown` status is the correct, evidence-consistent answer, not an evidence gap to close.

## 5. What this document does not establish

Consistent with the campaign's evidence bar: no row above proves retail menu role, lookup
behavior, field semantics, layout, render state, timing, audio playback policy, owner-corpus
coverage, or PCSX2 behavioral equivalence for any suffix, including the six classified as
"canonical decoder." Canonical-decoder status means a native, tested, bounded parser owns a typed
result for that suffix — nothing more.
