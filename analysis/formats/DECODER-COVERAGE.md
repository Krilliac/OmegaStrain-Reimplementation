# Decoder coverage matrix

## Scope and method

This document is derived mechanically from tracked repository source: the suffix inventory in
`analysis/formats/asset-fingerprints.json` (produced by `tools/fingerprint_assets.py`), the
narrower archive-level inventory in `analysis/formats/hog-validation.json` (produced by
`tools/validate_hogs.py`), the whole-disc file tally in `analysis/manifests/disc-summary.json`
(produced by `tools/generate_manifest.py`), the frozen category map in
`tools/measure_frontend_hog_topology.py`, every header under `native/include/omega/retail/`,
`native/include/omega/asset/`, and `native/include/omega/media/`, every corresponding file under
`native/src/`, the opening-movie presentation boundary, and `CMakeLists.txt`.
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
  never claims to "decode" the payload; it returns a `*Descriptor` struct containing only neutral
  structural summaries such as observed extents, counts, or raw header words.
- **aggregate scanner only** — no native code exists; only Python tooling purpose-built to
  inventory game asset formats (`tools/fingerprint_assets.py`, `tools/validate_hogs.py`,
  `tools/measure_frontend_hog_topology.py`, `tools/inspect_so.py`) observes the suffix, at most as
  a raw count or a generic container-topology bucket.
- **unknown** — observed only in the generic whole-disc file manifest
  (`analysis/manifests/disc-summary.json`) and/or a scanner's aspirational vocabulary map, with
  zero occurrences in either asset-directed archive inventory
  (`asset-fingerprints.json`/`hog-validation.json`). This is also the mandatory bucket for `.PF`
  and `.TM2` per the campaign's hard rules, regardless of what any manifest tool counts.

No retail-format decoder, envelope, or descriptor in this matrix asserts menu role, lookup rule,
layout, rendering behavior, owner-corpus coverage, or PCSX2 equivalence. The separate opening-movie
path presents one bounded MPEG-PS/H.262/PCM shape through project-owned video, audio, and clock
boundaries; that application capability does not establish suffix-wide retail behavior or general
audio/video parity.

## 1. Suffix classification matrix

| Suffix | Class | Native/tool evidence | Test evidence |
|---|---|---|---|
| `.col` | canonical decoder (mixed — see §3) | `DecodeColSpatialMesh` → `asset::SpatialMeshIR` (`native/include/omega/retail/col_spatial_mesh_decoder.h`, `native/src/retail/col_spatial_mesh_decoder.cpp`, IR at `native/include/omega/asset/spatial_mesh_ir.h`); independent passive `InspectColContainer` (`native/include/omega/retail/container_descriptors.h`, `native/src/retail/container_descriptors.cpp`) | `native/tests/col_spatial_mesh_decoder_tests.cpp`, `native/tests/container_descriptor_tests.cpp` (both built into `omega_core_tests`) |
| `.hog` | canonical decoder | `HogArchive`/`HogIndex::Open`/`OpenRange`/`FromBytes`/`FromSpan`/`FromOwnedBytes` (`native/include/omega/archive/hog_archive.h`, `native/src/archive/hog_archive.cpp`) | `native/tests/hog_archive_tests.cpp` (`omega_core_tests`) |
| `.pop` | canonical decoder (mixed — see §3) | `DecodePopLevelManifest` → `asset::LevelManifestIR` (`native/include/omega/retail/pop_level_manifest_decoder.h`, `native/src/retail/pop_level_manifest_decoder.cpp`), built atop `asset::PopTerrainIndex::Parse` (`native/include/omega/asset/pop_terrain_index.h`, `native/src/asset/pop_terrain_index.cpp`) for the `TER:` prefix only; passive `InspectPopPostTerrainHypotheses` checks the published marker/extent hypotheses without assigning section semantics (`native/include/omega/retail/pop_post_terrain_hypothesis_descriptor.h`, `native/src/retail/pop_post_terrain_hypothesis_descriptor.cpp`) | `native/tests/pop_level_manifest_decoder_tests.cpp`, `native/tests/pop_terrain_index_tests.cpp`, `native/tests/pop_post_terrain_hypothesis_descriptor_tests.cpp` (all built into `omega_core_tests`) |
| `.tdx` | canonical decoder (mixed — see §3) | `DecodeTdxTextureStorage`/`DecodeTdxTextureStorageMeasured` → `asset::TextureStorageIR` (`native/include/omega/retail/tdx_texture_storage_decoder.h`, `native/src/retail/tdx_texture_storage_decoder.cpp`), bounded front-end `DecodeTdxFrontEnd`/`DecodeTdxScopedFrontEnd` → owned indexed-image IR (`native/include/omega/retail/frontend_tdx_decoder.h`, `native/src/retail/frontend_tdx_decoder.cpp`), built alongside passive `InspectTdxContainer` (`native/include/omega/retail/container_descriptors.h`, `native/src/retail/container_descriptors.cpp`) | `native/tests/tdx_texture_storage_decoder_tests.cpp`, `native/tests/frontend_tdx_decoder_tests.cpp`, `native/tests/container_descriptor_tests.cpp` |
| `.vag` | canonical decoder | `DecodeVagAdpcm` → `asset::MonoPcm16IR` (`native/include/omega/retail/vag_adpcm_decoder.h`, `native/src/retail/vag_adpcm_decoder.cpp`, IR at `native/include/omega/asset/audio_ir.h`) | `native/tests/vag_adpcm_decoder_tests.cpp` (own executable `omega_vag_adpcm_decoder_tests`) |
| `.vum` | canonical decoder (mixed — see §3) | `DecodeVumMaterialCatalog`/`…Measured` → `asset::MaterialCatalogIR` (`native/include/omega/retail/vum_material_catalog_decoder.h`, `native/src/retail/vum_material_catalog_decoder.cpp`, IR at `native/include/omega/asset/material_catalog_ir.h`); passive `InspectVumRenderPayload` (`native/include/omega/retail/vum_render_payload_descriptor.h`, `native/src/retail/vum_render_payload_descriptor.cpp`) and independent `InspectVumContainer` (`native/include/omega/retail/container_descriptors.h`, `native/src/retail/container_descriptors.cpp`) | `native/tests/vum_material_catalog_decoder_tests.cpp`, `native/tests/vum_render_payload_descriptor_tests.cpp`, `native/tests/container_descriptor_tests.cpp` (all built into `omega_core_tests`) |
| `.lpd` | structural envelope only | `DecodeLpdEnvelope` → `asset::LpdEnvelopeIR`, doc comment: "No meaning is assigned to tracks or four-byte entries" (`native/include/omega/retail/lpd_envelope_decoder.h`, `native/src/retail/lpd_envelope_decoder.cpp`) | `native/tests/lpd_envelope_decoder_tests.cpp` (own executable `omega_lpd_envelope_decoder_tests`) |
| `.par` | structural envelope only | `DecodeParTextEnvelope` → `asset::ParTextEnvelopeIR`, doc comment: "assigns no keys, values, comments, fields, paths, particle behavior" (`native/include/omega/retail/par_text_envelope_decoder.h`, `native/src/retail/par_text_envelope_decoder.cpp`) | `native/tests/par_text_envelope_decoder_tests.cpp` (own executable `omega_par_text_envelope_tests`) |
| `.skas` | structural envelope only | `DecodeSkasTextEnvelope` → `asset::SkasTextEnvelopeIR`, opaque-line pattern (`native/include/omega/retail/skas_text_envelope_decoder.h`, `native/src/retail/skas_text_envelope_decoder.cpp`) | `native/tests/skas_text_envelope_decoder_tests.cpp` (own executable `omega_skas_text_envelope_decoder_tests`) |
| `.vpk` | structural envelope only | `DecodeVpkWrapperEnvelope` → `VpkWrapperEnvelopeDescriptor`, doc comment: "Retail-only passive structure... never retained" (`native/include/omega/retail/vpk_wrapper_envelope_decoder.h`, `native/src/retail/vpk_wrapper_envelope_decoder.cpp`) | `native/tests/vpk_wrapper_envelope_decoder_tests.cpp` (own executable `omega_vpk_wrapper_envelope_decoder_tests`) |
| `.ska` | passive descriptor only | `InspectSkaContainer` → `SkaContainerDescriptor` recognizes the tracked observed header family and classifies the correlated counted-word extent as exact, zero-padded tail, nonzero tail, or beyond input without exposing payload bytes or treating the latter two relations as established retail variants (`native/include/omega/retail/ska_container_descriptor.h`, `native/src/retail/ska_container_descriptor.cpp`) | `native/tests/ska_container_descriptor_tests.cpp` (`omega_core_tests`; all four relations use project-generated fixtures, while tracked corpus evidence establishes only exact and zero-padded tail) |
| `.skl` | passive descriptor only | `InspectSklContainer` → `SklContainerDescriptor` (`native/include/omega/retail/skl_container_descriptor.h`, `native/src/retail/skl_container_descriptor.cpp`) | `native/tests/skl_container_descriptor_tests.cpp` (`omega_core_tests`) |
| `.skm` | passive descriptor only | `InspectSkmContainer` → `SkmContainerDescriptor` (`native/include/omega/retail/skm_container_descriptor.h`, `native/src/retail/skm_container_descriptor.cpp`) | `native/tests/skm_container_descriptor_tests.cpp` (`omega_core_tests`) |
| `.so` | passive descriptor only | `InspectSoModule` -> `SoModuleDescriptor` validates the tracked little-endian structural grammar through exact EOF while retaining bounded section ranges, counts, and neutral record summaries but no code cells, strings, or payload bytes (`native/include/omega/retail/so_module_descriptor.h`, `native/src/retail/so_module_descriptor.cpp`) | `native/tests/so_module_descriptor_tests.cpp` (own executable `omega_so_module_descriptor_tests`, registered in `CMakeLists.txt`) |
| `.tbl` | passive descriptor only | `InspectTblEnvelope` -> `TblEnvelopeDescriptor` performs the bounded fixed-stride zero-sentinel observation while retaining only payload size, sentinel offset, preceding nonzero-probe count, and opaque trailing-byte count; inter-probe and trailing bytes remain opaque and no lane, integer, endianness, record, lookup, or front-end semantics are assigned (`native/include/omega/retail/tbl_envelope_descriptor.h`, `native/src/retail/tbl_envelope_descriptor.cpp`) | `native/tests/tbl_envelope_descriptor_tests.cpp` (own executable `omega_tbl_envelope_descriptor_tests`, registered in `CMakeLists.txt`; project-generated fixtures only) |
| `.gui` | canonical decoder (mixed — see §3) | `DecodeGuiFrontend`/`DecodeGuiFrontendMeasured` → owned `asset::FrontendWidgetDocumentIR` for the bounded title/create/open-agent family (`native/include/omega/retail/frontend_document_decoder.h`, `native/src/retail/frontend_document_decoder.cpp`); independent passive `InspectGuiEnvelope` retains only the older opaque root boundary (`native/include/omega/retail/gui_envelope_descriptor.h`, `native/src/retail/gui_envelope_descriptor.cpp`) | `native/tests/frontend_document_decoder_tests.cpp`, `native/tests/gui_envelope_descriptor_tests.cpp` |
| `.fnt` | canonical decoder (mixed — see §3) | `DecodeFntV3` → owned `FntV3IR` for the documented version-3 family while rejecting unproven pair tables (`native/include/omega/retail/fnt_v3_decoder.h`, `native/src/retail/fnt_v3_decoder.cpp`); independent passive `InspectFntEnvelope` retains only prefix/range facts (`native/include/omega/retail/fnt_envelope_descriptor.h`, `native/src/retail/fnt_envelope_descriptor.cpp`) | `native/tests/fnt_v3_decoder_tests.cpp`, `native/tests/fnt_envelope_descriptor_tests.cpp` |
| `.ie` | canonical decoder (mixed — see §3) | `DecodeIeFrontend`/`DecodeIeFrontendMeasured` → owned `asset::FrontendVisualDocumentIR` for the bounded title/create/open-agent family (`native/include/omega/retail/frontend_document_decoder.h`, `native/src/retail/frontend_document_decoder.cpp`); independent passive `InspectIeEnvelope` retains only the older opaque root boundary (`native/include/omega/retail/ie_envelope_descriptor.h`, `native/src/retail/ie_envelope_descriptor.cpp`) | `native/tests/frontend_document_decoder_tests.cpp`, `native/tests/ie_envelope_descriptor_tests.cpp` |
| `.bin` | aggregate scanner only | Raw suffix count in `asset-fingerprints.json`/`hog-validation.json`; also present in `tools/check_public_tree.py`'s commit-blocklist (existence signal only, not structural evidence) | none |
| `.bnk` | aggregate scanner only | Raw suffix count only (`asset-fingerprints.json`, `hog-validation.json`) | none |
| `.bon` | aggregate scanner only | Raw suffix count only | none |
| `.gun` | aggregate scanner only | Raw suffix count only (`asset-fingerprints.json`; absent from `hog-validation.json`'s narrower top-level tally) | none |
| `.prn` | aggregate scanner only | Raw suffix count only | none |
| `.pss` | aggregate scanner only (suffix classification) | Raw suffix counts remain the only tracked suffix-wide corpus evidence. Separately, the native opening-movie path bounds and inspects MPEG-2 Program Stream/PES framing, builds a zero-copy MPEG video plan, inspects H.262 sequence facts, decodes video through Media Foundation on Windows, applies one project-defined provisional SShd/SSbd signed-PCM compatibility hypothesis, deinterleaves it into caller-owned samples, and presents it through bounded SDL video/audio queues. Those APIs accept an explicit external movie but do not independently prove the custom PCM field meanings or deinterleave semantics, that every retail `.pss` member uses the accepted shape, or that it behaves like the retail runtime (`native/include/omega/media/`, `native/apps/openomega/opening_movie_player.h`, `native/apps/openomega/sdl_audio_service.h`) | Suffix occurrence only; synthetic media safety and self-consistency coverage in `native/tests/mpeg_program_stream_descriptor_tests.cpp`, `mpeg_video_elementary_stream_tests.cpp`, `media_foundation_h262_decoder_tests.cpp`, `pss_pcm_audio_stream_tests.cpp`, and app/audio tests |
| `.scc` | aggregate scanner only | Raw suffix count only in both archive inventories, plus a separate whole-disc count in `disc-summary.json` | none |
| `.skel` | aggregate scanner only | Raw suffix count only | none |
| `.skf` | aggregate scanner only | Raw suffix count only | none |
| `.sub` | aggregate scanner only | Raw suffix count only | none |
| `.txt` | aggregate scanner only | Raw suffix count in the recursive `asset-fingerprints.json` inventory (3 spans), but no `.txt` key in top-level `hog-validation.json` `entry_extensions`; `disc-summary.json` separately records 8 whole-disc occurrences. Also listed in `measure_frontend_hog_topology.py`'s `APPROVED_EXTENSION_CATEGORIES` as category `text` (topology label only) | Topology-label coverage only, via `test_measure_frontend_hog_topology.py` |
| `.PF` (`.pf`) | unknown (hard-rule mandated) | Zero occurrences in either in-archive inventory; exactly 3 whole-disc occurrences in `analysis/manifests/disc-summary.json`, outside any HOG archive | none |
| `.TM2` (`.tm2`) | unknown (hard-rule mandated) | Zero occurrences in either in-archive inventory; exactly 16 whole-disc occurrences in `analysis/manifests/disc-summary.json`, outside any HOG archive | none |

Totals observed in this pass: **9 canonical decoder**, **4 structural envelope only**, **5 passive
descriptor only**, **11 aggregate scanner only**, **2 unknown** — 31 families.

## 2. CMake / test registration cross-check

Every matrix-listed native canonical/envelope/descriptor boundary has a matching implementation in
`native/src/retail/` (or `native/src/archive/`/`native/src/asset/`) registered in `CMakeLists.txt`,
plus focused coverage through its own executable/CTest pair or `omega_core_tests`. The FNT, GUI,
and IE canonical decoders and their independent passive descriptors each have registered focused tests. **No missing CMake
registration was found** for a matrix-listed native format boundary.

E-0113 adds a separate project-passive coverage consumer for those three descriptors. The
`frontend-envelope-coverage-verify-tree` command emits only deterministic aggregate acceptance and
typed-rejection counts from HOG members. Its schema version 1 is distinct from E-0110 topology
schema version 3, where `.fnt` remains `other`. No owner-corpus result is tracked, and this consumer
did not itself promote any family beyond `passive descriptor only` or supply UI/menu semantics.
The later bounded FNT/GUI/IE canonical decoders are separately tested implementations and remain
limited to their documented front-end families.

The separate `omega_media` target registers the generic MPEG-2 Program Stream inspector, video
range planning/H.262 inspection, Windows Media Foundation video adapter, and narrow PSS PCM stream
planner/deinterleaver. `openomega` composes those pieces with its opening-movie player and bounded
SDL presentation services. The PCM grammar remains a provisional compatibility hypothesis. Keeping
these pieces outside `omega_retail_formats` is deliberate: media presentation for one external
stream that passes that hypothesis is not a suffix-wide canonical asset decoder.

## 3. Mixed-layer families and mechanical inconsistencies (not fixed)

Seven suffixes have *more than one* native code path, with several distinct composition shapes:

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
- **`.pop`** — `DecodePopLevelManifest` builds on the canonical `PopTerrainIndex` prefix parser,
  while `InspectPopPostTerrainHypotheses` independently checks the already-published aligned-marker
  order and five neutral marker-relative extent formulas after that prefix. It assigns no section,
  count, record, placement, or gameplay semantics.
- **`.fnt`** — `DecodeFntV3` and `InspectFntEnvelope` are independent. The former owns the bounded
  version-3 glyph/atlas IR; the latter retains only the older prefix/range hypothesis.
- **`.gui`** and **`.ie`** — the paired canonical front-end document decoders share one
  implementation and produce owned widget/visual IR for the documented screen family. Their
  suffix-specific passive inspectors remain independent opaque-root observations.

Several "decoder built on / next to / sharing internals with descriptor" shapes exist for
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

1. **Keep the semantic front-end gate closed beyond the three passive prefix hypotheses.** FNT, GUI,
   and IE now have bounded, generated-fixture-tested `Inspect*` descriptors, but no tracked evidence
   records the retail provenance of their constants. Their opaque payload/root regions are
   intentionally not traversed. Do not turn neutral words or byte ranges into
   font, widget, node, layout, lookup, render, or menu fields without independent consumer evidence
   and a falsifiable deeper grammar.
2. **Use the size-only collector only as a coverage aid.**
   `tools/measure_member_structural_fingerprint.py` retains its frozen, bounded, path-free schema
   documented in `MEMBER-STRUCTURAL-FINGERPRINT.md`. A reviewed, sanitized owner-corpus result could
   measure cohort size regularity, but it cannot promote any passive descriptor to a semantic IR.
   Size GCD is a divisor of sizes, never an address-alignment claim.
3. **Optionally measure `.bnk/.gun`.** The same collector accepts these two suffixes only when
   explicitly requested. They remain `aggregate_scanner_only` until a sanitized owner result is
   tracked and still gain no audio, weapon, or menu semantics from their names.
4. **Treat sibling same-basename pairing only as aggregate topology.**
   `FRONTEND-TOPOLOGY.md`'s schema version 3 computes every unordered pair of approved suffixes that
   shares a normalized sibling basename. Its frozen vocabulary now includes `.gui` and `.ie`, so
   `.gui+.ie` and each suffix's pair keys with `.tdx`, `.vum`, and `.txt` are always present. Matches
   from root and nested HOG directories reduce to report-wide counts without archive, depth, member,
   or basename identity. No sanitized owner result is tracked, so actual co-location remains missing
   evidence; a nonzero count would still establish no binding or semantics. `.fnt` remains outside
   this pair vocabulary unless a separately reviewed topology question warrants another schema bump.
5. **Keep opening-movie capability separate from suffix-wide `.pss` parity.** The native path can
   present bounded H.262 video and one narrow block-interleaved PCM shape from an explicitly
   selected MPEG-PS input. Missing evidence includes accepted-shape coverage across retail members,
   alternate private-stream variants, subtitle/multi-stream behavior, seek policy, and exact retail
   A/V timing. Add support one demonstrated variant at a time; do not infer it from the suffix.
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

Consistent with the campaign's evidence bar: no row above proves retail menu role, lookup behavior,
field semantics, layout, render state, owner-corpus coverage, or PCSX2 behavioral equivalence for
any suffix, including the nine classified as "canonical decoder." Canonical-decoder status means a
native, tested, bounded parser owns a typed result for that suffix — nothing more. The opening-movie
composition additionally proves a project-owned presentation path for one accepted external
MPEG-PS/H.262/PCM shape; it does not prove general `.pss` compatibility, exact retail A/V timing,
alternate stream variants, subtitles, seeking, or non-Windows end-to-end playback.
