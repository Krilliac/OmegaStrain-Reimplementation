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
occurring only inside HOG containers and not at additional nesting levels beyond what the top-level
scan already captures. The 0 whole-disc count confirms `.lpd` never appears as a bare filesystem
entry outside a HOG — it is exclusively an in-archive asset suffix.

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

Each is explicitly unconfirmed. No hypothesis here is treated as fact anywhere else in this
document.

- **H1 — "Dialogue/lip" role.** `ASSET-RECON.md` prose speculates the LPD family is
  "dialogue lip/pose curves" based on (a) the per-track counted-envelope shape and (b) the 852/862
  VAG basename adjacency. *Confirms/refutes with:* a privacy-safe structural cross-check that
  aligns each LPD track's entry count against its companion VAG's decoded PCM sample/frame count
  (aggregate ratio statistics only, no payload) — a stable numeric relationship (e.g., entry count
  tracking audio duration) would support H1; no correlation across the 852-pair aggregate would
  refute it.
- **H2 — 21 tracks correspond to a fixed facial/bone rig dimension.** The fixed count of 21
  per-file tracks (identical across all 862 files) suggests a fixed output cardinality (e.g., a
  rig with 21 channels) rather than a variable list. *Confirms/refutes with:* checking whether any
  companion format (SKL/SKA skeleton data already in the tracked corpus) exposes a bone/joint count
  of 21 in its own aggregate; a match in the tracked SKL/SKA aggregates would support H2, a
  mismatch or absence of any 21-valued field would weaken it.
- **H3 — Four-byte entries are a single scalar (float32 or fixed-point), not a struct.** The IR
  currently stores entries as opaque 4-byte arrays; no field-splitting has been attempted.
  *Confirms/refutes with:* an aggregate-only statistical scan (value-range histogram, IEEE-754
  validity ratio) over all decoded entries across the 862-file corpus — a high proportion of
  entries that parse as plausible finite float32 values in a narrow, non-degenerate range would
  support a float32-scalar hypothesis; a bimodal or ASCII-like byte distribution would refute it.
- **H4 — The single "exact" file (zero tail) is a truncated/minimal or otherwise distinguished
  case rather than a representative example.** *Confirms/refutes with:* an aggregate check of
  whether that file's total entry count (sum of the 21 counts) is an outlier (e.g., near-zero or
  near-maximum) relative to the corpus distribution — reported as an aggregate percentile only, no
  filename or path.

## 6. Missing observations

- No published per-track entry-count distribution (min/mean/max per track index, 0–20) exists in
  any tracked source. `analysis/formats/LPD.md` and `ASSET-RECON.md` report only the aggregate
  formula and overall size/tail statistics, not a per-track breakdown. *Privacy-safe collection:*
  extend `tools/fingerprint_assets.py`'s LPD handler to emit, per track index 0–20, aggregate
  min/mean/max/zero-count entry counts across the 862-file corpus into
  `analysis/formats/asset-fingerprints.json` — no payload bytes, counts only.
- No aggregate statistical profile (byte-value histogram, float32-validity ratio, integer-range
  check) of the opaque 4-byte entries exists in tracked sources — the decoder deliberately treats
  them as opaque, and no separate research pass has profiled them. *Privacy-safe collection:* a new
  read-only aggregate-scanner pass (following the existing `fingerprint_assets.py` model of
  emitting only counts/ratios, never payload bytes) that reports, corpus-wide, what fraction of
  4-byte entries are valid finite IEEE-754 float32 vs. small non-negative integers vs. neither.
- No cross-reference has been run correlating LPD track/entry counts against the corresponding
  paired VAG's decoded audio duration (frame count from the VAG decoder, ledger E-0090). This would
  test H1 without touching any payload content. *Privacy-safe collection:* an aggregate-only join
  (basename match already established at 852/862) producing only a scatter of (LPD total entry
  count, VAG frame count) pairs summarized as a correlation coefficient — no filenames, no bytes.
- No adversarial/fuzzing test-gap audit result is recorded for the LPD decoder beyond what E-0091
  self-reports (synthetic malformed/truncated/hostile-count cases). No independent verification run
  (e.g., a fresh CTest execution log, or a coverage report) is present in the tracked evidence for
  this specific decoder beyond the ledger's self-description. *Privacy-safe collection:* run
  `ctest -R omega_lpd_envelope_decoder_tests` and `ctest` full-suite locally and record pass/fail
  counts plus any new coverage gaps (e.g., simultaneous multi-track overflow, non-zero-tail
  rejection at exactly 1,933 bytes) as a new ledger entry — all synthetic, no owner-corpus access
  needed.
- Publication is confirmed by current repository history: the LPD implementation landed on main at
  commit `9e8bdde`. E-0091's pending language is a historical validation boundary, not current merge
  status. A fresh owner-corpus verification remains a separate, unclaimed result.

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

Ranked, privacy-safe, concrete next steps. None require reading private inputs beyond running the
existing read-only aggregate scanner against the owner corpus (already the established, permitted
pattern for `tools/fingerprint_assets.py`).

1. **Run a metadata-only owner-corpus verification if needed.** Publication is already confirmed;
   report only aggregate accept/reject and typed-error counts.
2. **Extend `tools/fingerprint_assets.py`'s LPD handler to emit per-track (0–20) aggregate entry-count
   statistics** (min/mean/max/zero-count) into `asset-fingerprints.json`, closing the "Missing
   observations" gap on per-track distribution without touching payload bytes.
3. **Add an aggregate-only 4-byte-entry value-profile pass** (float32-validity ratio, integer-range
   check, byte-histogram) run read-only over the owner corpus, emitting only corpus-wide ratios to
   a new or extended JSON aggregate — directly tests Hypothesis H3 (scalar type) without exposing
   any entry value.
4. **Run an aggregate-only LPD/VAG basename-paired correlation** (entry-count sum vs. paired VAG
   frame count, from the already-existing VAG decoder in `native/src/retail/`), reporting only a
   correlation coefficient across the 852 paired basenames — tests Hypothesis H1 without any
   filename, path, or payload leaving the aggregate boundary.
5. **Run the existing test suite and record a fresh pass/fail count** (`ctest -R
   omega_lpd_envelope_decoder_tests`, then the full suite) and append the result as a new ledger
   entry, to give this dossier's "Decoder/tooling status" section a currently-dated confirmation
   rather than relying solely on E-0091's self-report.
6. **Do not** attempt to assign track roles, timing units, or pose/animation semantics to the LPD
   IR until an item 3 or 4 result produces a citable aggregate signal; absent that, keep the
   decoder's non-claims in `LPD.md` unchanged — inventing semantics here is an explicit regression
   per project rules.
