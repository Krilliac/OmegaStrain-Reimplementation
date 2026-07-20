# .lpd Format Dossier

## 1. Identity

`.lpd` is a HOG-embedded, counted-envelope binary asset family: every observed member begins with
a little-endian 32-bit word count of 22, followed by 21 little-endian per-track entry counts, then
that many four-byte opaque entries per track in source order, and an optional all-zero physical
tail. This is the full extent of what tracked evidence establishes. No track role, sample type,
timing unit, interpolation rule, pose, animation, or audio relationship is confirmed; the informal
"dialogue/lip data" label used in prose (`analysis/formats/ASSET-RECON.md`) is an unconfirmed
naming hint, not a decoded semantic, and is treated below as a Hypothesis only.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive-in-HOG (all nesting depths) | 862 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".lpd"]`) |
| Top-level-HOG member suffix | 862 | `analysis/formats/hog-validation.json` (`entry_extensions[".lpd"]`) |
| Whole-disc file histogram | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.lpd` key present) |

The recursive and top-level counts are identical (862 = 862), consistent with `.lpd` members
occurring only as direct HOG members in the current tracked inventory. That inventory contains no
whole-disc `.lpd` filesystem entry; this establishes no universal placement rule for other
releases or corpora.

## 3. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| 1 | All 862 validated `.lpd` entries begin with little-endian word count 22 and satisfy `logical_bytes = 22*4 + 4*sum(header_words[1:22])`. | `analysis/formats/ASSET-RECON.md` ("LPD dialogue/lip data" section) |
| 2 | The corpus-level aggregate row for LPD reports 862 validated entries and "Every file satisfies the 22-word count-table formula below." | `analysis/formats/ASSET-RECON.md` (Corpus results table) |
| 3 | Of the 862 files, exactly 1 lands exactly on the computed logical extent; 861 have an all-zero tail. | `analysis/formats/ASSET-RECON.md` |
| 4 | Observed all-zero tails range from 8 through 1,932 bytes across the corpus. | `analysis/formats/ASSET-RECON.md`, `analysis/formats/LPD.md` |
| 5 | The tracked aggregate spans physical sizes from 2,048 through 4,096 bytes. | `analysis/formats/LPD.md` |
| 6 | In the same HOG directory, 852 of 862 LPD basenames have a same-named VAG companion (basename co-occurrence only; no confirmed structural link). | `analysis/formats/ASSET-RECON.md` |
| 7 | `tools/fingerprint_assets.py` is registered as a FORMAT_HANDLER for `lpd` and performs the structural scan producing the above aggregate. | `tools/fingerprint_assets.py` |
| 8 | A native fully-owned canonical IR type `omega::asset::LpdEnvelopeIR` exists, holding exactly `kLpdSourceTrackCount = 21` `LpdTrackIR` objects, each an owned `std::vector<std::array<std::byte,4>>` of source-order opaque entries; no field carries semantic meaning. | `native/include/omega/asset/lpd_envelope_ir.h` |
| 9 | A native decoder `omega::retail::DecodeLpdEnvelope(std::span<const std::byte>, DecodeLimits)` is declared, described as stateless/reentrant and callable from any worker thread. | `native/include/omega/retail/lpd_envelope_decoder.h` |
| 10 | Fixed decoder constants: `kLpdHeaderWordCount=22`, `kLpdHeaderBytes=88`, `kLpdMaximumInputBytes=4096`, `kLpdMaximumZeroTailBytes=1932`, derived `kLpdMaximumEntryCount=1002` (`= (4096-88)/4`), `kLpdMaximumDecodedItems=1024` (`= 1 + 21 + 1002`), `kLpdMaximumLogicalOutputBytes = sizeof(LpdEnvelopeIR) + 4008`. | `native/include/omega/retail/lpd_envelope_decoder.h` |
| 11 | The decoder implementation file exists at 217 lines and the test file at 352 lines, both registered as CMake build/test targets. | `native/src/retail/lpd_envelope_decoder.cpp`, `native/tests/lpd_envelope_decoder_tests.cpp`, `CMakeLists.txt` (lines defining `omega_lpd_envelope_decoder_tests`, linking `omega_retail_formats`, `add_test`, `TIMEOUT 5`) |
| 12 | `CMakeLists.txt` lists `native/src/retail/lpd_envelope_decoder.cpp` as a build source and registers `omega_lpd_envelope_decoder_tests` as a CTest target with a 5-second timeout. | `CMakeLists.txt` |
| 13 | Ledger entry E-0091 (state: confirmed) documents the same decoder/IR: it "adds a stateless reentrant worker-thread LPD counted-envelope decoder and fully owned canonical IR," ties the 4096-byte physical and 1932-byte tail values to "unraiseable project security ceilings," and states the adapter "assigns no track, scalar, timing, interpolation, pose, or VAG relationship" and "is not composed into content, startup, audio, animation, or playback." | `analysis/evidence/ledger.jsonl` (E-0091) |
| 14 | E-0091's own evidence list cites exactly: `CMakeLists.txt`, `native/include/omega/asset/lpd_envelope_ir.h`, `native/include/omega/retail/lpd_envelope_decoder.h`, `native/src/retail/lpd_envelope_decoder.cpp`, `native/tests/lpd_envelope_decoder_tests.cpp`, `analysis/formats/LPD.md`, `analysis/formats/ASSET-RECON.md`, `analysis/formats/asset-fingerprints.json`, plus repo docs. | `analysis/evidence/ledger.jsonl` (E-0091) |
| 15 | E-0091 records synthetic-only test coverage: source-order grouping, opaque byte preservation, empty/maximum envelopes, ownership, repeat determinism, exact/bounded tails, earliest dirty-tail offsets, wrong word count, truncated header/payload prefixes, hostile/over-hard-limit counts, unraiseable fixed limits, exact/one-below caller limits, zero scratch/depth, and injected first/later allocation failures — and states focused MSVC Debug/Release builds and CTest passed, plus a runtime-OFF full Debug integration build with CTest 37/37, with "publication and exact-main validation" still pending at time of that entry. | `analysis/evidence/ledger.jsonl` (E-0091) |
| 16 | `analysis/formats/LPD.md` ("Status" section) states the native adapter "does not assign a track role, scalar type, timing unit, sample rate, interpolation rule, pose, animation, audio relationship, or playback behavior." | `analysis/formats/LPD.md` |

## 4. Aggregate-only facts

No semantic interpretation is applied to any of the following; these are corpus-level counts and
published grammar constants only.

- Corpus size: 862 validated `.lpd` inputs (`analysis/formats/asset-fingerprints.json`,
  `analysis/formats/ASSET-RECON.md`).
- Physical size range across the corpus: 2,048–4,096 bytes (`analysis/formats/LPD.md`).
- Header size is fixed at 88 bytes (22 little-endian 32-bit words) for all 862 inputs
  (`analysis/formats/LPD.md`, `analysis/formats/ASSET-RECON.md`).
- Tail-padding distribution: 1 of 862 files has zero tail bytes (exact logical extent); 861 of 862
  have a non-empty all-zero tail; observed tail lengths span 8–1,932 bytes
  (`analysis/formats/ASSET-RECON.md`, `analysis/formats/LPD.md`).
- Cross-format basename co-occurrence: 852/862 (~98.8%) of LPD basenames have a same-directory,
  same-basename VAG file in the same containing HOG (`analysis/formats/ASSET-RECON.md`). This is a
  directory/basename adjacency count only — no byte-level or structural link between the two
  formats has been observed or is claimed.
- Derived (not observed) hard ceilings published from the fixed physical/header constants:
  maximum entries per file = 1,002; maximum decoded items = 1,024; maximum logical output =
  `sizeof(LpdEnvelopeIR) + 4,008` bytes (`analysis/formats/LPD.md`,
  `native/include/omega/retail/lpd_envelope_decoder.h`). These are algebraic derivations of the
  4,096-byte and 1,932-byte aggregate maxima, not independently observed corpus facts.

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

**Classification: `structural_envelope_only`**

Justification: a native decoder exists (`native/src/retail/lpd_envelope_decoder.cpp`,
`native/include/omega/retail/lpd_envelope_decoder.h`) that parses the full fixed 22-word header,
all 21 per-track counts, and every four-byte entry into an owned `LpdEnvelopeIR`
(`native/include/omega/asset/lpd_envelope_ir.h`) with deterministic resource accounting and typed
bounded-decode error handling. This goes beyond a passive descriptor (it fully materializes the
counted structure) but stops at the structural/envelope level — per E-0091 and `LPD.md`, it
explicitly assigns no track role, scalar type, timing, interpolation, pose, animation, or audio
relationship, so it is not a semantic "canonical_decoder" for the asset's meaning.

- Build/test registration: `CMakeLists.txt` builds `native/src/retail/lpd_envelope_decoder.cpp`
  into `omega_retail_formats` and registers `omega_lpd_envelope_decoder_tests`
  (`native/tests/lpd_envelope_decoder_tests.cpp`) as a CTest target with a 5-second timeout.
- Test coverage per ledger E-0091 (self-reported, synthetic-only): source-order grouping, opaque
  byte preservation, empty/maximum envelopes, ownership, determinism, exact/bounded tails,
  earliest-dirty-tail offsets, wrong word count, truncated header/payload prefixes, hostile and
  over-hard-limit counts, unraiseable fixed limits, exact/one-below caller limits, zero
  scratch/depth, and injected allocation failures (first and later).
- Publication is complete. The remaining evidence gap is an owner-corpus metadata-only validation,
  not missing synthetic adversarial coverage or uncertain merge state.

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
