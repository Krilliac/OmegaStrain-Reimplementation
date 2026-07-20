# .TDX dossier — texture storage container

## 1. Identity

`.TDX` is a version-5, 64-byte-prefixed game-asset container that stores one or more fixed-stride
"blocks," each holding one to four primary transfer-element planes and (for indexed sample
families) exactly one 1,024-byte palette plane, in source order. It occurs as an in-scope,
high-volume game-asset format inside the tracked HOG archive corpus. At the evidence level this
dossier can defend, `.TDX` is a **structural texture-storage container**: a decoded, owned,
byte-preserving intermediate representation (`TextureStorageIR`) exists and is native-verified
against the full owned corpus, but no display-ready pixel, channel, alpha, color-space, swizzle,
mip/slice/frame, or GPU-upload semantic is established anywhere in tracked evidence. Several named
candidate/diagnostic projections (Packed24, Indexed8-candidate) exist as explicitly-labeled,
policy-gated experiments, not confirmed decoders of retail meaning.

## 2. Occurrence evidence

| Count | Scope | Attributed tracked source |
|---|---|---|
| 15,248 | Recursive occurrences inside HOG archives | `analysis/formats/asset-fingerprints.json` (`/formats/tdx/count`, `/scan/extensions/.tdx`) |
| 11,166 | Top-level HOG member-suffix count | `analysis/formats/hog-validation.json` (`/entry_extensions/.tdx`) |
| 2 | Whole-disc file histogram | `analysis/manifests/disc-summary.json` (`/extensions/.tdx`) |

These three counts come from three independent tracked scans (per-format structural fingerprinter,
top-level HOG suffix validator, and whole-disc file histogram) and are not asserted to describe the
same denominator; recursive-in-HOG, top-level-HOG, and whole-disc are different scopes by
construction.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| 1 | The corpus-observed `version` field for `.TDX` spans is uniformly `5` (15,248 of 15,248). | `analysis/formats/asset-fingerprints.json` (`/formats/tdx/version`) |
| 2 | The header is a 64-byte little-endian prefix with fields `u16 version, flags, width, height, sample_bit_depth, sample_format_code, width_unit_word, storage_unit_word, ... u16 block_count (+0x22), primary_plane_count (+0x24), palette_plane_count (+0x26), ... u32 block_stride (+0x38)`. | `analysis/formats/TDX.md` |
| 3 | The counted region formula is `64 + block_count * block_stride` (not `64 + block_stride`); this correction reclassifies 24 formerly opaque tails as ordinary additional blocks. | `analysis/formats/TDX.md` |
| 4 | Observed sample-family codes are `0x14` (4-bit indexed), `0x13` (8-bit indexed), `0x01` (packed 24-bit), `0x00` (packed 32-bit). | `analysis/formats/TDX.md`; `analysis/formats/asset-fingerprints.json` (`/formats/tdx/observed_format_code`) |
| 5 | A transfer object within a block is 0x60 bytes; every object and data target is bounded by its block; all primary targets are unique, strictly ordered, and fill the meaningful suffix of a block. | `analysis/formats/TDX.md` |
| 6 | Four-bit-indexed palettes hold 16 entries plus an all-zero remainder in a 1,024-byte slot; eight-bit-indexed palettes hold 256 entries filling that slot; direct (non-indexed) blocks carry no palette plane. | `analysis/formats/TDX.md` |
| 7 | A native semantic decoder exists (`TextureStorageIR` / `DecodeTdxTextureStorageMeasured`) that preserves top-level sample encoding, block order, plane order, transfer-element encoding, rectangle dimensions, primary bytes, and four raw source bytes per palette entry (including observed values above 128), performing no RGBA naming or alpha conversion. | `native/include/omega/asset/texture_storage_ir.h`; `native/src/retail/tdx_texture_storage_decoder.cpp`; `analysis/formats/TDX.md` |
| 8 | E-0022 (ledger, confirmed): the native TDX adapter decodes all 15,248 owned-corpus spans with zero errors into 15,442 blocks, 17,960 primary planes, 285,521,272 owned primary bytes, 15,190 palette blocks, 252 direct blocks, 1,510,240 palette entries; 62 textures normalize exactly 4,112 duplicate-proven implicit-zero suffix bytes. | `analysis/evidence/ledger.jsonl` (E-0022); `native/apps/omega_tool/asset_commands.cpp`; `tools/prove_tdx_zero_suffix.py` |
| 9 | E-0018 (ledger, confirmed): an independent passive native structural descriptor corpus validates all 15,248 TDX spans with zero errors, without decoding payload semantics. | `analysis/evidence/ledger.jsonl` (E-0018); `native/src/retail/container_descriptors.cpp`; `native/apps/omega_tool/asset_commands.cpp` |
| 10 | E-0029 (ledger, confirmed): a bounded, explicitly-labeled aggregate scorer (`score_tdx_layout_hypotheses.py`) accepts all 15,248 spans and scores 2,176 direct indexed planes; the tool and ledger entry are explicit that this "nominates hypotheses only" and does not confirm nibble/palette/channel/swizzle/mip/display semantics. | `analysis/evidence/ledger.jsonl` (E-0029); `analysis/formats/TDX.md`; `tools/score_tdx_layout_hypotheses.py` |
| 11 | E-0038 (ledger, confirmed): the native `LevelTextureStore` composed Open/Load verifier accepts all 18 owned-corpus runtime levels, 36 explicit sibling texture-container sources, and 5,801 level-inventory texture occurrences with zero errors, loading 5,913 blocks / 7,603 planes / 615,232 palette entries / 29,562,280 owned bytes; it establishes no display, material, cell, mesh, draw, placement, visibility, or rendering semantic. | `analysis/evidence/ledger.jsonl` (E-0038); `native/include/omega/content/level_texture_store.h`; `native/src/content/level_texture_store.cpp` |
| 12 | E-0043 (ledger, confirmed): a native asynchronous `AssetService` v0 sits above `LevelTextureStore` with a fixed slot pool, generation-checked handles, and Ready/Failed release; two verifier passes are byte-identical over 18 levels / 36 sources / 5,801 occurrences with zero errors. It performs no VUM-name/material lookup or binding. | `analysis/evidence/ledger.jsonl` (E-0043); `native/include/omega/runtime/asset_service.h`; `native/src/runtime/asset_service.cpp` |
| 13 | E-0036/E-0037 (ledger, confirmed): bounded aggregate scans find **zero** normalized `.TDX`-suffixed members inside the common runtime-level `DATA.HOG` archive graph (18 levels, 5,351 manifest cell occurrences, 5,413 scanned archive-directory occurrences), while a separate scan of two explicit sibling texture-container classes finds 5,801 direct `.TDX` members across the same 18 levels (36 exact containers, zero errors). | `analysis/evidence/ledger.jsonl` (E-0036, E-0037); `analysis/formats/TDX.md`; `tools/measure_level_tdx_topology.py`; `tools/measure_level_texture_container_topology.py` |
| 14 | E-0066/E-0078/E-0087 (ledger, confirmed): named metadata-only diagnostic image adapters (`texture_storage_topology_debug_image`, `packed24_transfer_debug_image`, `tdx_indexed8_candidate_debug_image`) exist, are CMake-registered, and are explicitly documented as establishing no channel/alpha/swizzle/display/material/geometry semantic; the Indexed8 candidate adapter requires the caller to supply an explicit non-default policy for every unresolved transformation rather than assuming one. | `analysis/evidence/ledger.jsonl` (E-0066, E-0078, E-0087); `CMakeLists.txt`; `native/include/omega/runtime/texture_storage_topology_debug_image.h`; `native/include/omega/runtime/packed24_transfer_debug_image.h`; `native/include/omega/runtime/tdx_indexed8_candidate_debug_image.h` |
| 15 | `.TDX` has a dedicated structural fingerprint handler (`fingerprint_tdx`) registered in the format-handler table, and a dedicated per-suffix version-word tally (`tdx_version_words`) is computed for at least one named sibling container class (`MAPTEX.HOG`, showing version `5` for both observed members). | `tools/fingerprint_assets.py` (lines ~171, ~500, ~700–704); `analysis/formats/asset-fingerprints.json` (`/minsk/containers/MAPTEX.HOG/tdx_version_words`) |
| 16 | The native TDX decoder source, its unit tests, and CMake test registration exist as named files. | `native/src/retail/tdx_texture_storage_decoder.cpp`; `native/tests/tdx_texture_storage_decoder_tests.cpp`; `CMakeLists.txt` (line 103, 1405) |

## 4. Aggregate-only facts

Purely quantitative, non-semantic, sourced from `analysis/formats/asset-fingerprints.json`
(`/formats/tdx`) and `analysis/formats/TDX.md`. No row here assigns meaning to a value.

- Sample bit-depth distribution: 4-bit = 9,879; 8-bit = 5,172; 24-bit = 68; 32-bit = 129 (total 15,248).
- Bit-depth/format-code pairing is exact and one-to-one in the corpus: `4/0x14`=9,879, `8/0x13`=5,172, `24/0x01`=68, `32/0x00`=129.
- `block_count` distribution is dominated by 1 (15,224 of 15,248); remaining values (2,3,4,6,8,10,11,19) are rare tails.
- `block_stride_range` = [352, 525,728]; stride is **not** uniformly 16-byte aligned — 23 occurrences use stride 2,376 with an eight-byte zero container tail.
- `span_bytes_range` = [416, 526,336].
- Counted-block-span reconciliation: 11,277 exact matches, 3,909 all-zero-padded tails, 62 spans ending inside the last primary plane (padding-byte range [8, 1,824]), zero nonzero tails.
- `palette_plane_count`: 0 for 197 spans, 1 for 15,051 spans.
- `primary_plane_count`: 1 → 13,921; 2 → 582; 3 → 299; 4 → 446.
- Dimension histogram spans 8x8 (92) up to 1024x512 (401), with 64x64 (5,572) and 32x32 (4,476) the largest buckets.
- `width_unit_formula_match` = 15,248 of 15,248 (all spans); `storage_word_matches_area_bit_formula` = 13,472 of 15,248, with per-bit-depth mismatch counts of 68 (24-bit, 100% mismatch), 1,348 (4-bit), and 360 (8-bit).
- Implicit-zero-suffix family: 62 spans, reduced to 16 unique prefixes by an aggregate-only duplicate-prefix proof, each with a complete corpus twin (all-zero missing suffix, no unmatched prefixes, no nonzero-suffix twins); 4,112 total synthesized zero bytes.
- Layout-hypothesis scoring aggregates (content-dependent adjacency deltas, not semantic proof): 2,176 direct indexed planes scored; 4-bit `0x14` low-nibble-first delta sum 93,917,821 (1,765 wins/139 losses/110 ties) versus high-nibble-first 109,066,377; 8-bit `0x13` bit-3/bit-4 permutation delta sum 554,076 (145 wins/7 losses/10 ties) versus identity 658,936.
- Common-archive containment aggregate (E-0036): 18 levels, 5,351 manifest cell occurrences, 5,413 archive-directory occurrences, 44 deeper-container occurrences, zero normalized `.TDX` members found in that scope.
- Sibling-container aggregate (E-0037/E-0038): 36 exact containers, 5,801 direct `.TDX` member occurrences (5,765 from one class, 36 from the other); native `LevelTextureStore` load totals 5,913 blocks / 7,603 planes / 615,232 palette entries / 27,101,352 plane bytes / 2,460,928 palette bytes / 29,562,280 owned bytes across the same 18 levels.
- Independent (not-asserted-co-occurring) native Open/Load field maxima: Open input/items/logical-output/depth/scratch = 3,076,944 / 1,460 / 111,014 / 0 / 71,467; Load = 3,139,344 / 5,169 / 333,232 / 0 / 65,595.

## 5. Hypotheses

Each is explicitly labeled as unconfirmed in tracked sources; each row states the privacy-safe
observation that would confirm or refute it.

1. **Four-bit indexed nibble order is low-nibble-first.** Basis: `score_tdx_layout_hypotheses.py` shows lower aggregate local-adjacency delta for low-nibble-first across 2,014 planes (E-0029). This is a content-smoothness heuristic, not a bitstream proof. *Confirming/refuting observation:* a privacy-safe behavioral cross-check — e.g., comparing the scorer's ranking stability under an independently generated synthetic 4-bit fixture with a known ground-truth nibble order, or any tracked emulator/behavioral trace that fixes nibble order without touching private disc bytes — is currently absent from tracked evidence.
2. **Eight-bit indexed CLUT lookup uses the bit-3/bit-4 permutation, not identity.** Basis: same scorer, 162 planes, lower delta for the permuted lookup (E-0029). *Confirming/refuting observation:* an aggregate-only comparison against a second, independent content-coherence metric (e.g., a different neighbor-distance kernel) over the same 162-plane population, reported only in aggregate, would either corroborate or contradict the ranking without adding semantic claims.
3. **Packed-32 (`0x00`) storage rectangles on indexed textures represent pre-swizzled GPU-upload layout rather than display texel order.** Basis: `TDX.md` states these should not be scored as display coherence and are deliberately excluded from the layout-hypothesis scorer. *Confirming/refuting observation:* no tracked source establishes swizzle rule or upload target; a privacy-safe collection would be an aggregate-only count of how many `0x00`-transfer planes recur across near-duplicate dimension buckets (a proxy for shared-swizzle-table reuse) without reproducing any payload bytes.
4. **VUM material-catalog names ending `.TDX` (after exact or single-extension-elision normalization) identify the primary or map sibling-container `.TDX` members.** Basis: E-0041/E-0042 run two lexical-coverage experiments; both explicitly disclaim that this establishes retail lookup, ownership, or binding — they report only exact-string/one-extension-elision candidate coverage (5,690 of 5,801 locator occurrences reached only via elision; 111 unreached; zero reached via exact equality alone). *Confirming/refuting observation:* a further privacy-safe aggregate experiment — e.g., counting whether elision-reached candidates cluster by container class or by catalog position, reported only in aggregate — is not yet in tracked evidence; genuine confirmation would require behavioral (emulator/native runtime lookup trace) evidence, which is explicitly out of scope for this format family per the task instructions.
5. **The `24/0x01` bit-depth/format pairing (packed 24-bit) always mismatches the area-bit storage formula (68 of 68, 100%) because it uses a distinct storage-word convention from the 4/8-bit indexed families.** Basis: aggregate mismatch counts in `asset-fingerprints.json` (`storage_formula_mismatch_by_bit_depth`). *Confirming/refuting observation:* an aggregate-only breakdown of the 24-bit population by `width_unit_word`/`storage_unit_word` values (already present as header fields) reported as a bucketed count, without exposing per-file rows, would show whether a single alternate formula fits all 68 uniformly or whether the family is itself heterogeneous.

## 6. Missing observations

Tracked evidence does not exist for the following; each entry states a privacy-safe collection that
would close the gap without violating clean-room rules.

1. **No tracked ground-truth for channel order, alpha meaning, color space, or premultiplication.** No decoder, ledger entry, or `*.md` claims these. A privacy-safe collection would be a synthetic/generated (non-disc) fixture-based behavioral test comparable to the existing E-0066/E-0078/E-0087 synthetic fixtures, extended to probe a specific channel-order hypothesis against an independently-authored reference renderer — never against private disc bytes.
2. **No tracked evidence connects any `.TDX` occurrence to a cell, material, mesh, draw call, placement, or visibility state.** E-0033/E-0036/E-0037/E-0041/E-0042 are explicit that they establish containment or lexical-candidate coverage only. A privacy-safe collection would be additional aggregate-only cross-container candidate experiments (as already done for VUM names) restricted to counts, never exposing member names or offsets.
3. **No tracked evidence of mip/slice/frame purpose for `primary_plane_count` values of 2, 3, or 4.** The scorer explicitly does not assign a mip rank to later planes. A privacy-safe collection would be an aggregate-only positional statistic (e.g., whether later-plane dimensions are consistently a power-of-two fraction of the first plane's dimensions, reported as a bucketed count) computed from already-decoded header fields.
4. **No property-based fuzzer is registered.** The deterministic native suite already covers every
   short header prefix, invalid and duplicate references, unsupported encodings, malformed palette
   and plane geometry, padding/gap/overlap/tail failures, and exact/one-below resource limits. A
   future property test would be additive hardening, not a missing basic adversarial suite.
5. **No tracked whole-disc top-level `.TDX` member identity or count breakdown beyond the raw count of 2.** `disc-summary.json` gives only the aggregate; no `*.md` narrative interprets it. A privacy-safe collection would be an aggregate note in `analysis/manifests/disc-summary.json`'s companion documentation distinguishing these 2 from the 11,166 top-level-HOG occurrences (e.g., confirming they are or are not HOG containers themselves), computed purely from already-tracked manifest fields.

## 7. Decoder/tooling status

**Classification: `canonical_decoder`**

Justification (each clause cites its tracked source):

- A semantic, owned-byte-preserving decoder exists and is registered: `TextureStorageIR` (`native/include/omega/asset/texture_storage_ir.h`) is produced by `native/src/retail/tdx_texture_storage_decoder.cpp`, with a corresponding measured-decode entry point (`DecodeTdxTextureStorageMeasured`) documented in `analysis/formats/TDX.md`.
- CMake test registration exists: `native/tests/tdx_texture_storage_decoder_tests.cpp` is built and registered per `CMakeLists.txt` (line ~1405); related diagnostic-image test targets (`omega_tdx_indexed8_candidate_debug_image_tests`, `packed24_transfer_debug_image_tests`) are also registered (`CMakeLists.txt` lines ~1202–1226).
- Native corpus verification exists and passed with zero errors: E-0022 (full corpus, 15,248/15,248 spans), E-0038 (composed `LevelTextureStore` Open/Load, 18/18 levels, 5,801 occurrences), E-0043 (asynchronous `AssetService` lifecycle, byte-identical dual passes) — all in `analysis/evidence/ledger.jsonl`.
- A structural (pre-semantic) descriptor layer also exists independently: E-0018, `native/src/retail/container_descriptors.cpp`, confirming the corpus was validated at a structural level before the semantic adapter (E-0022) superseded that portion.
- This does **not** mean display-ready pixel decoding is confirmed. `TDX.md`, and every listed ledger entry from E-0022 through E-0087, explicitly and repeatedly disclaim channel naming, alpha conversion, swizzle, nibble/palette-permutation policy, mip/slice/frame purpose, color space, premultiplication, GPU upload layout, and any material/cell/mesh/draw/placement/visibility binding. The three "diagnostic image" adapters (E-0066, E-0078, E-0087) are metadata-topology or explicitly-policy-gated candidate visualizers, not confirmed display decoders — E-0087's own adapter requires the caller to supply a non-default policy for every unresolved transformation specifically because no default is justified by evidence.
- **Deterministic adversarial coverage exists:** `native/tests/tdx_texture_storage_decoder_tests.cpp`
  covers truncation, invalid/duplicate references, unsupported variants, malformed geometry,
  padding/gaps/overlap/tails, implicit-zero constraints, and exact/one-below limits. No separate
  property-based fuzzer is registered.

## 8. Codex work order

Ranked, concrete, privacy-safe. No item requires new access to private/, runtime/, third_party/,
downloads/, disc images, or any untracked content; all reference only the owner's already-tracked
corpus and tooling.

1. **Resolved for deterministic boundaries.** Keep the existing generated adversarial suite; add a
   property-based target only if its new invariant and bounded execution contract are specified.
2. **Extend the aggregate-only positional statistic for multi-plane textures** (Missing Observation 3): compute and publish, via a small addition to `tools/fingerprint_assets.py` or a new bounded scanner, whether plane 2/3/4 dimensions are power-of-two fractions of plane 1's dimensions, reported strictly as bucketed counts (no per-file rows). This narrows the mip/slice/frame-purpose unknown without inventing semantics.
3. **Re-run `score_tdx_layout_hypotheses.py` with a second, independent coherence metric** (e.g., a different neighbor kernel) over the same 2,176-plane population and publish the aggregate ranking alongside the existing E-0029 result, to corroborate or contradict Hypotheses 1 and 2 before any decoder policy is hardened as default.
4. **Bucket the 24-bit (`0x01`) `storage_formula_mismatch` population** (Hypothesis 5) by `width_unit_word`/`storage_unit_word` and publish as an aggregate table in `asset-fingerprints.json`'s companion doc, to determine whether a single alternate storage formula covers all 68 packed-24-bit spans.
5. **Document the 2 whole-disc top-level `.TDX` occurrences' relationship to the 11,166 top-level-HOG occurrences** (Missing Observation 5) using only fields already present in `analysis/manifests/disc-summary.json` / `disc-files.jsonl` — e.g., whether the 2 are themselves HOG containers — and add a one-line aggregate note to `TDX.md`'s Occurrence section.
6. **Do not** add any new "display-ready" or channel-semantic decoder, and do not promote any Section-5 hypothesis to Confirmed, until a corroborating second aggregate metric or an explicitly project-generated behavioral fixture (never private-disc-derived) exists per items 2–4 above.
