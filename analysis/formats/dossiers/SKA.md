# .SKA — Evidence Dossier

## 1. Identity

`.SKA` is a game-asset suffix found inside the retail HOG archive family. At the evidence
level the tracked corpus supports, it is a **counted 32-bit-word binary envelope**: a fixed
112-byte header followed by a computed logical extent, version word 3 in every observed
instance. No animation, skeleton, timing, channel, transform, bone, or compression semantics
are established. The related `.SKAS` suffix (a bounded CRLF text envelope, only 2 instances)
is a structurally distinct family and is not assumed to relate to `.SKA` beyond sharing a
name prefix.

## 2. Occurrence evidence

| Metric | Count | Source |
| --- | ---: | --- |
| Recursive-in-HOG occurrences (all nesting depths) | 213 | `analysis/formats/asset-fingerprints.json` (`ska.count`) |
| Top-level-HOG member-suffix occurrences | 212 | `analysis/formats/hog-validation.json` (`.ska: 212`) |
| Whole-disc occurrences (loose, non-HOG files) | 1 | `analysis/manifests/disc-summary.json` (`.ska: 1`) |
| Top-level HOG containers carrying `.ska`-suffixed members | `GAMEDATA/COMMON/SKA.HOG` (entry_count 157) + `GAMEDATA/COMMON/SKALEVEL.HOG` (entry_count 55) = 212 | `analysis/formats/hog-validation.json` |

The 212 top-level count plus the 1 whole-disc loose file accounts for 213 of the 213
recursive-in-HOG occurrences reported by the fingerprinter; the fingerprinter's 213 figure is
the aggregate structural-scan count over the full recursive HOG walk (`tools/fingerprint_assets.py`),
independently corroborated by `ASSET-RECON.md`'s "SKA | 213 |" corpus-results row and by
evidence-ledger entries E-0026/E-0027 (both state "213").

## 3. Confirmed facts (mechanically citable)

| # | Fact | Tracked source |
| --- | --- | --- |
| 1 | `fingerprint_ska()` reads a 112-byte header and unpacks 5 little-endian u32 words via `struct.unpack_from("<5I", header)`: `version_word`, `word_0x04`, `word_0x08`, `_word_0x0c` (discarded/unnamed), `word_0x10`. | `tools/fingerprint_assets.py` (lines ~234-269, function `fingerprint_ska`) |
| 2 | The fingerprinter only proceeds past validation when `version_word == 3`, `word_0x04 != 0`, `word_0x08 != 0`, and `word_0x10 ∈ {0, 1}`; otherwise it tags the span `unsupported_version_word`, `zero_counted_word`, or `unsupported_observed_word_0x10` and stops. | `tools/fingerprint_assets.py` |
| 3 | Computed logical extent formula: `logical_size = 112 + 4 * word_0x08 * (word_0x04 + (1 if word_0x10 == 0 else 0))`. | `tools/fingerprint_assets.py`; restated identically in `analysis/formats/ASSET-RECON.md` §"SKA and SKAS structural envelopes" |
| 4 | All 213 observed spans report `version_word = 3` (aggregate `version_word: {"3": 213}`); all 213 are 16-byte aligned (`span_16_byte_aligned: 213`). | `analysis/formats/asset-fingerprints.json` (`ska` block) |
| 5 | Of 213 computed extents, 158 are exact matches to their containing span and 55 have an all-zero tail (`computed_counted_word_span_exact: 158`, `computed_counted_word_span_zero_padded: 55`); no span has a nonzero tail or an extent exceeding its physical boundary. | `analysis/formats/asset-fingerprints.json`; `analysis/formats/ASSET-RECON.md` |
| 6 | Observed `word_0x08` values across the corpus: 56 (16 spans), 88 (192 spans), 92 (5 spans). Observed `word_0x10` values: 0 (140 spans), 1 (73 spans). Observed `word_0x04` range: 1-357. | `analysis/formats/asset-fingerprints.json` (`ska.observed_word_0x08`, `ska.observed_word_0x10`, `ska.observed_word_0x04_range`) |
| 7 | Ledger entry **E-0026** (state: confirmed) records the same 213-span, 112-byte-prefix, counted-word extent proof (158 exact / 55 zero-padded) and explicitly disclaims animation/timing/transform/channel/compression/label/value/relationship semantics. | `analysis/evidence/ledger.jsonl` id `E-0026` |
| 8 | Ledger entry **E-0027** (state: confirmed) records that the native `SkaContainerDescriptor` independently validates all 213 owned-corpus spans with zero errors (158 exact, 55 zero-padded, 2,180,832 aggregate logical bytes) and retains no payload bytes or semantics; SKAS is explicitly kept separate. | `analysis/evidence/ledger.jsonl` id `E-0027`; `native/include/omega/retail/ska_container_descriptor.h`; `native/src/retail/ska_container_descriptor.cpp`; `native/apps/omega_tool/asset_commands.cpp` |
| 9 | `SkaContainerDescriptor` (native struct) retains exactly four scalar fields plus an `ObservedExtent`: `format_version`, `observed_word_0x04`, `observed_word_0x08`, `observed_word_0x10`, `logical_extent{observed_bytes, input_bytes, relation}`. Its header comment states it "retains no input bytes and assigns no animation, timing, channel, transform, bone, compression, or payload semantics," and explicitly states SKAS "is a separate text family and is intentionally outside this API." | `native/include/omega/retail/ska_container_descriptor.h` |
| 10 | `InspectSkaContainer()` is declared `[[nodiscard]]`, takes `std::span<const std::byte>` plus `asset::DecodeLimits`, and returns `asset::DecodeResult<SkaContainerDescriptor>` — a passive, allocation-bounded decode contract. | `native/include/omega/retail/ska_container_descriptor.h` |
| 11 | The CLI tool `omega_tool asset-metadata-verify-tree` recognizes the `.ska` extension (`InputKind::Ska`), calls `retail::InspectSkaContainer`, and aggregates `stats.ska.candidates/valid/errors`, `stats.ska_extents.{exact,zero_padded_tail,nonzero_tail,exceeds_input}`, and `stats.ska_structural.logical_bytes`. | `native/apps/omega_tool/asset_commands.cpp` (lines ~185-186, 428-432, 488-489, 617-629, 917-919) |
| 12 | A dedicated native unit test target `native/tests/ska_container_descriptor_tests.cpp` exists, is compiled into `omega_core_tests`, and is linked against `omega_retail_formats`. Its test harness builds synthetic spans from a `SkaSpec{word_0x04, word_0x08, word_0x10, version}` and a `MakeSka()` helper that reproduces the exact 112 + counted-word formula, plus an optional appended zero-tail length parameter. | `CMakeLists.txt` (line 1402, target `omega_core_tests`); `native/tests/ska_container_descriptor_tests.cpp` |
| 13 | `analysis/formats/ASSET-RECON.md` corpus-results table lists SKA at 213 validated entries with confirmed result "Every span satisfies the neutral counted-word extent below; version word is 3." | `analysis/formats/ASSET-RECON.md` |
| 14 | The `SKA.HOG` top-level container has `entry_count: 157`, `data_offset: 4784`, `tag: 0x40245D11`; the `SKALEVEL.HOG` top-level container has `entry_count: 55`, `data_offset: 2048`, `tag: 0x40245D20`. (157 + 55 = 212, matching the top-level `.ska` suffix count.) | `analysis/formats/hog-validation.json` |
| 15 | No `SKA.md` grammar document exists in `analysis/formats/`; the only published per-format `.md` file for this family is `analysis/formats/SKAS.md` (for the distinct SKAS text envelope), and `.ska` grammar is documented only inline within `ASSET-RECON.md`. | `analysis/formats/` directory listing (glob `SKA*.md` matches only `SKAS.md`) |

## 4. Aggregate-only facts

- `span_bytes_range`: 464-125,776 bytes across all 213 spans (`asset-fingerprints.json` `ska.span_bytes_range`, identical to `computed_logical_bytes_range`).
- `computed_counted_word_span_padding_bytes_range`: 16-2,000 bytes of zero padding observed among the 55 zero-padded spans.
- `observed_word_0x08_0x10_pair` joint distribution: `56/0`: 16, `88/0`: 122, `88/1`: 70, `92/0`: 2, `92/1`: 3 (sums to 213).
- Aggregate native-decoder logical-byte total across the full owned corpus: 2,180,832 bytes (E-0027; `asset_commands.cpp` `stats.ska_structural.logical_bytes` accumulator).
- All 213 spans are 16-byte aligned at the container level; this is an alignment observation only, not a claimed record-size rule.
- No compression-magic signature (gzip/ZIP/bzip2/XZ/7zip/LZ4-frame/Zstandard/RNC1/RNC2/LZSS/LZ77/Yaz0) was found among the 46,603 non-HOG asset spans scanned corpus-wide, which includes `.ska` members; this rules out those specific whole-file wrapper signatures for this family but says nothing about internal/headerless encoding (`ASSET-RECON.md` §"Wrappers and compression check").

## 5. Hypotheses (explicitly labeled — none decoded, none confirmed)

- **H-SKA-1 (bone/skeleton animation channel table).** The word-count-and-stride shape (`word_0x04` × `word_0x08`, gated by `word_0x10`) is consistent in *shape* with a per-channel or per-frame keyframe table, given the file suffix's conventional association with skeletal animation in similar-era titles. Privacy-safe confirming observation: cross-tabulating `word_0x04`/`word_0x08`/`word_0x10` against the SKL "skeleton/loadout reference list" bone-count or profile-label distribution (both are aggregate, already-tracked artifacts) to test whether `word_0x08` values (56/88/92) correlate with SKL bone-count buckets. A refuting observation would be no stable correlation across the corpus.
- **H-SKA-2 (SKA/SKAS relationship).** The shared name prefix invites a hypothesis that SKAS text spans describe or index SKA binary spans (e.g., a label/metadata sidecar). Tracked evidence explicitly withholds this: `ASSET-RECON.md` states SKAS "labels, values, relationships, and any association with SKA are not yet established," and the native descriptor header states SKAS "is intentionally outside this API." Confirming observation: with only 2 SKAS instances against 213 SKA instances, no meaningful cross-reference is currently possible from tracked aggregates; a privacy-safe basename/co-location cross-tabulation (analogous to the LPD/VAG basename-pairing check already done in `ASSET-RECON.md`) would be the way to test this without exposing payload content.
- **H-SKA-3 (`word_0x10` as a compression/format-variant flag).** Its restriction to exactly `{0, 1}` and its role in the extent formula (adding one unit to the word count when 0) is consistent with a boolean mode flag (e.g., "has extra trailer row" vs. not) rather than a compression flag. Confirming/refuting observation: none available from aggregate counts alone; would require decoding actual counted-word content, which is out of scope for this dossier and the current descriptor.

## 6. Missing observations

- No published `SKA.md` grammar document exists (unlike `SKAS.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`); the grammar currently lives only as a subsection of `ASSET-RECON.md`. A privacy-safe collection step: promote that subsection into a standalone `analysis/formats/SKA.md` mirroring the `SKAS.md` structure (aggregate-only, no payload bytes), so downstream tooling and Codex work orders have a single per-format citation target.
- `native/tests/ska_container_descriptor_tests.cpp` already exercises every rejection branch implemented in `fingerprint_assets.py`'s `fingerprint_ska()`: non-3 version word (`CheckUnsupportedWord(0, ...)` over `{2U, 4U}`), zero counted-word rejection (`CheckUnsupportedWord` over `word_0x04 ∈ {0U, 358U, 0xFFFFFFFFU}` and `word_0x08 ∈ {0U, 55U, 57U, 87U, 89U, 91U, 93U}`), unsupported `word_0x10` rejection (`CheckUnsupportedWord` over `{2U, 0xFFFFFFFFU}`), and header-too-short/truncation rejection (the loop asserting every prefix shorter than the full envelope is classified `Truncated`). Extent-exceeds-input is likewise covered by the nonzero-trailing-byte `Malformed` checks. No coverage gap against the Python fingerprinter's rejection branches was found.
- No aggregate cross-tabulation exists between `.ska` container-hog membership (`SKA.HOG` vs. `SKALEVEL.HOG`) and any of the observed word buckets (`word_0x08` ∈ {56,88,92}, `word_0x10` ∈ {0,1}). Producing this split (which HOG contributes which bucket counts) would be a privacy-safe aggregate-only extension of `tools/fingerprint_assets.py` (bucket the existing per-span stats by containing top-level HOG path) and might sharpen or refute H-SKA-1.
- No decoded-content research (VIF/vertex-style tracing, as done for VUM/COL) has been attempted against `.ska`'s counted-word region; `ASSET-RECON.md`'s loader-priority list places "SKM/SKL for characters and weapons" as priority 6 but does not mention SKA at all, meaning it has not yet been scheduled into the loader-priority research plan.

## 7. Decoder/tooling status: **passive_descriptor_only**

- **Native descriptor exists** and is registered: `native/include/omega/retail/ska_container_descriptor.h` declares `SkaContainerDescriptor` and `InspectSkaContainer()`; `native/src/retail/ska_container_descriptor.cpp` is compiled into the `omega_retail_formats` library (`CMakeLists.txt` line ~99); the CLI tool `native/apps/omega_tool/asset_commands.cpp` wires it into `InputKind::Ska` classification and `asset-metadata-verify-tree` reporting.
- **Test registration exists**: `native/tests/ska_container_descriptor_tests.cpp` is built into `omega_core_tests` (`CMakeLists.txt` line 1402) and linked against `omega_retail_formats`.
- The authoritative coverage matrix classifies this API `passive_descriptor_only`: it retains four
  observed header words plus an extent relation, never decodes the counted-word payload, and assigns
  no animation, timing, channel, transform, bone, or compression semantics.
- **Adversarial/resource-boundary coverage confirmed**: beyond the well-formed `SkaSpec`/`MakeSka` construction path (plus its optional zero-tail parameter), the test harness explicitly exercises every rejection branch implemented in the Python fingerprinter — bad version word and zero counted-word values via `CheckUnsupportedWord`, unsupported `word_0x10` values via the same helper, undersized/truncated headers via the all-prefixes-truncated loop, and extent-exceeds-physical-span via the nonzero-trailing-byte `Malformed` checks. No coverage gap against the Python fingerprinter's rejection branches was found.

## 8. Codex work order (ranked, privacy-safe, no semantic speculation)

1. **Resolved — no gap found.** Re-inspection of `native/tests/ska_container_descriptor_tests.cpp` confirms every rejection branch already implemented in `tools/fingerprint_assets.py::fingerprint_ska()` — non-3 version word, zero `word_0x04`/`word_0x08`, `word_0x10` outside `{0,1}`, span shorter than 112 bytes, and computed extent exceeding the physical span — already has an explicit synthetic-byte-buffer assertion (`CheckUnsupportedWord`, the all-prefixes-truncated loop, and the nonzero-tail `Malformed` checks). No new rejection-path unit tests are needed; this item is closed.
2. Promote the SKA grammar currently embedded in `analysis/formats/ASSET-RECON.md` §"SKA and SKAS structural envelopes" into a standalone `analysis/formats/SKA.md`, mirroring the structure of `analysis/formats/SKAS.md` (aggregate-only; no payload bytes; explicit non-claims section). This gives future dossiers and tooling a single canonical citation target instead of a subsection of a larger recon document.
3. Extend `tools/fingerprint_assets.py`'s `ska` aggregate to bucket the existing per-span statistics (`word_0x04`, `word_0x08`, `word_0x10`, exact/zero-padded classification) by containing top-level HOG path (`SKA.HOG` vs `SKALEVEL.HOG`), and re-run against the owner corpus to regenerate `analysis/formats/asset-fingerprints.json`. This is a privacy-safe aggregate-only change (adds one more grouping key to output already limited to counts) and would let H-SKA-1 be tested or refuted without any new payload exposure.
4. Re-run `build/msvc/Debug/omega_tool.exe asset-metadata-verify-tree private/extracted-disc` after step 1's test additions land, and confirm the reported `stats.ska.valid == 213`, `stats.ska.errors == 0`, and `stats.ska_structural.logical_bytes == 2,180,832` still match the E-0027 ledger baseline exactly — a regression here would indicate the new rejection tests exposed a live bug rather than just adding coverage.
5. Do **not** attempt to decode the counted-word payload region, assign bone/channel/animation semantics, or establish an SKA/SKAS relationship until a dedicated, ledger-tracked research pass explicitly targets that question with its own privacy-safe evidence plan (per §5's confirming/refuting observations) — this dossier's `passive_descriptor_only` classification is not a green light to add semantic decoding without a new evidence-ledger entry.
