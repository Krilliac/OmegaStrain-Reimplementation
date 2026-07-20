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
| Mixed structural-fingerprinter candidates (direct depth −1 plus recursive HOG members) | 213 | `analysis/formats/asset-fingerprints.json` (`ska.count`); `tools/fingerprint_assets.py` (`scan_disc`) |
| Recursive HOG-member occurrences (all nesting depths) | 212 | Mixed total minus the 1 directly scanned filesystem entry; `tools/fingerprint_assets.py` (`scan_disc`) |
| Top-level-HOG member-suffix occurrences | 212 | `analysis/formats/hog-validation.json` (`.ska: 212`) |
| Whole-disc filesystem entries, also scanned directly at depth −1 because `.ska` is handled | 1 | `analysis/manifests/disc-summary.json` (`.ska: 1`); `tools/fingerprint_assets.py` (`scan_disc`) |
| Top-level HOG containers carrying `.ska`-suffixed members | `GAMEDATA/COMMON/SKA.HOG` (entry_count 157) + `GAMEDATA/COMMON/SKALEVEL.HOG` (entry_count 55) = 212 | `analysis/formats/hog-validation.json` |

The fingerprinter's 213 figure is a mixed structural-candidate total, not a recursive-HOG count.
`scan_disc` recursively scans HOGs and then directly scans handled loose files at depth −1. Thus
the 212 top-level HOG members plus 1 direct filesystem candidate account for all 213 structural
candidates, and no nested `.ska` member is implied. The 213-candidate total is independently
corroborated by `ASSET-RECON.md` and evidence-ledger entries E-0026/E-0027.

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

- `span_bytes_range`: 464-125,776 bytes across all 213 mixed structural candidates (`asset-fingerprints.json` `ska.span_bytes_range`, identical to `computed_logical_bytes_range`).
- `computed_counted_word_span_padding_bytes_range`: 16-2,000 bytes of zero padding observed among the 55 zero-padded spans.
- `observed_word_0x08_0x10_pair` joint distribution: `56/0`: 16, `88/0`: 122, `88/1`: 70, `92/0`: 2, `92/1`: 3 (sums to 213).
- Aggregate native-decoder logical-byte total across the full owned corpus: 2,180,832 bytes (E-0027; `asset_commands.cpp` `stats.ska_structural.logical_bytes` accumulator).
- All 213 mixed structural candidates are 16-byte aligned; this is an input-size observation only, not a claimed HOG-only or record-size rule.
- No compression-magic signature (gzip/ZIP/bzip2/XZ/7zip/LZ4-frame/Zstandard/RNC1/RNC2/LZSS/LZ77/Yaz0) was found among the 46,603 non-HOG asset spans scanned corpus-wide, which includes `.ska` members; this rules out those specific whole-file wrapper signatures for this family but says nothing about internal/headerless encoding (`ASSET-RECON.md` §"Wrappers and compression check").

## 5. Hypotheses (explicitly labeled — none decoded, none confirmed)

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

## 7. Decoder/tooling status: **passive_descriptor_only**

- **Native descriptor exists** and is registered: `native/include/omega/retail/ska_container_descriptor.h` declares `SkaContainerDescriptor` and `InspectSkaContainer()`; `native/src/retail/ska_container_descriptor.cpp` is compiled into the `omega_retail_formats` library (`CMakeLists.txt` line ~99); the CLI tool `native/apps/omega_tool/asset_commands.cpp` wires it into `InputKind::Ska` classification and `asset-metadata-verify-tree` reporting.
- **Test registration exists**: `native/tests/ska_container_descriptor_tests.cpp` is built into `omega_core_tests` (`CMakeLists.txt` line 1402) and linked against `omega_retail_formats`.
- The authoritative coverage matrix classifies this API `passive_descriptor_only`: it retains four
  observed header words plus an extent relation, never decodes the counted-word payload, and assigns
  no animation, timing, channel, transform, bone, or compression semantics.
- **Adversarial/resource-boundary coverage confirmed**: beyond the well-formed `SkaSpec`/`MakeSka` construction path (plus its optional zero-tail parameter), the test harness explicitly exercises every rejection branch implemented in the Python fingerprinter — bad version word and zero counted-word values via `CheckUnsupportedWord`, unsupported `word_0x10` values via the same helper, undersized/truncated headers via the all-prefixes-truncated loop, and extent-exceeds-physical-span via the nonzero-trailing-byte `Malformed` checks. No coverage gap against the Python fingerprinter's rejection branches was found.

## 8. Codex work order (ranked, privacy-safe, no semantic speculation)

1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
