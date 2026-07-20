# VAG — Dossier

## 1. Identity

`.vag` is the mono PlayStation-ADPCM audio container found throughout the tracked HOG corpus. At
the evidence level this dossier can defend: it is a fixed 48-byte big-endian `VAGp` header followed
by a declared, 16-byte-frame-aligned PS-ADPCM data span. A stateless, reentrant decoder exists in
the project's `omega_retail_formats` library that turns an accepted envelope into owned mono
PCM16 samples; it assigns no playback, container-selection, or cross-format (e.g. LPD) semantics.

## 2. Occurrence evidence

| Metric | Value | Tracked source |
| --- | ---: | --- |
| Recursive-in-HOG occurrences | 8,665 | `analysis/formats/asset-fingerprints.json` (`/formats/vag/count`, `/scan/extensions/.vag`) |
| Top-level-HOG member count | 8,665 | `analysis/formats/hog-validation.json` (`extensions`/`.vag`) |
| Whole-disc occurrences | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` |

The format is exclusively packed inside HOG archives (never present as a whole-disc top-level
file), matching the recursive-in-HOG and top-level-HOG counts being identical (8,665 = 8,665).

## 3. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| C1 | The fingerprinter's `.vag` handler (`fingerprint_vag`) reads a 48-byte header, checks the 4-byte magic against `b"VAGp"`, and unpacks four big-endian 32-bit fields at offset 4: version, reserved, `data_size`, `sample_rate`. | `tools/fingerprint_assets.py` (`fingerprint_vag`, lines ~360-378) |
| C2 | All 8,665 fingerprinted entries carry the `VAGp` magic (`vagp_magic: 8665`, no `bad_magic` count present); the reserved big-endian word is `0x00000000` for all 8,665. | `analysis/formats/asset-fingerprints.json` (`/formats/vag`) |
| C3 | Declared version values split 8,497 × `0x00000000`, 166 × `0x00000004`, 2 × `0x00000020` across the full 8,665-entry population. | `analysis/formats/asset-fingerprints.json` (`/formats/vag/version`) |
| C4 | Declared sample rate is `22050` for all 8,665 entries (single bucket, no other value present). | `analysis/formats/asset-fingerprints.json` (`/formats/vag/sample_rate_hz`) |
| C5 | All 8,665 entries have a declared ADPCM data-byte count that is a multiple of 16 (`adpcm_data_16_byte_aligned: 8665`, equal to `count`). | `analysis/formats/asset-fingerprints.json` (`/formats/vag`) |
| C6 | Of 8,665 entries, 53 have the physical span end exactly at `48 + data_size` (`declared_data_span_exact`), and 8,612 carry a zero-padded tail beyond that point (`declared_data_span_zero_padded`), with the observed tail-length range 16–2,032 bytes. | `analysis/formats/asset-fingerprints.json` (`/formats/vag/declared_data_span_*`) |
| C7 | The published grammar doc restates the same header layout (`char[4] magic="VAGp"`; `be32 version`; `be32 reserved=0`; `be32 ADPCM data bytes`; `be32 sample rate=22050`; ADPCM frames starting at `0x30`) and the same 53/8,612 exact/padded and 8,497/166/2 version split. | `analysis/formats/ASSET-RECON.md` (VAG audio section) |
| C8 | A native decoder exists: `omega::retail::DecodeVagAdpcm(std::span<const std::byte>, asset::DecodeLimits)` is declared to accept the "independently documented mono VAG envelope and PS-ADPCM frames" and decode to canonical owned PCM16, explicitly not applying end/repeat/loop/resample/mix/playback policy. | `native/include/omega/retail/vag_adpcm_decoder.h` |
| C9 | The decoder implementation and its dedicated test translation unit are registered as source/test files in the build. | `native/src/retail/vag_adpcm_decoder.cpp`; `native/tests/vag_adpcm_decoder_tests.cpp`; `CMakeLists.txt` (source list line ~104; test target `omega_vag_adpcm_decoder_tests`, lines ~565-588) |
| C10 | A dedicated CTest target `omega_vag_adpcm_decoder_tests` is registered with a 10-second timeout. | `CMakeLists.txt` (`add_test(NAME omega_vag_adpcm_decoder_tests ...)`, `set_tests_properties(... TIMEOUT 10)`) |
| C11 | The published transform doc (`VAG.md`) specifies the accepted envelope (48-byte header, versions `0`/`4`/`0x20`, reserved zero, rate 22,050 Hz, frame-aligned data, 0 or 16–2,032-byte zero tail), the 5 predictor coefficient pairs, shift range 0–12, nibble order/sign, and the fixed-ceiling ADPCM/output ceilings (4 MiB declared data, 32 MiB logical output) enforced by the decoder. | `analysis/formats/VAG.md` |
| C12 | Ledger entry E-0090 records the VAG adapter as confirmed, including build/test verification (warning-free focused MSVC Debug/Release builds, focused CTest 1/1 in both configurations, warning-free full Debug integration build with CTest 41/41) and enumerates exactly what is *not* claimed (no runtime lookup, no title-specific marker meaning, no SDL publication, no playback/mixing policy, no retail behavioral comparison). | `analysis/evidence/ledger.jsonl` (`E-0090`) |
| C13 | Ledger entry E-0011 records the passive asset-fingerprint corpus (covering VAG among other families) as confirmed, tied to `tools/fingerprint_assets.py` and its check command. | `analysis/evidence/ledger.jsonl` (`E-0011`) |
| C14 | `.lpd` companion-detection logic in the fingerprinter counts LPD basenames that share a directory with a `.vag` name: 852 `.lpd` entries have a same-directory `.vag` companion, 10 do not. | `tools/fingerprint_assets.py` (`fingerprint` orchestration, `formats[".lpd"].add("same_directory_vag_companions"...)`); `analysis/formats/asset-fingerprints.json` (`/formats/lpd/same_directory_vag_companions`, `/formats/lpd/without_same_directory_vag_companion`) |
| C15 | The ASSET-RECON doc restates the LPD/VAG companion counts (852 of 862 LPD basenames have a VAG companion in the same HOG directory) and explicitly labels the pairing's meaning as an unconfirmed structural observation ("strongly suggest," not decoded). | `analysis/formats/ASSET-RECON.md` (LPD dialogue/lip data section) |

## 4. Aggregate-only facts

No semantic interpretation attached — counts and ranges only, all from
`analysis/formats/asset-fingerprints.json` unless noted.

- Physical span size across the 8,665-entry population ranges 4,096–929,792 bytes (`span_bytes_range`).
- Zero-padded tail length, where present, ranges 16–2,032 bytes (`declared_data_span_padding_bytes_range`).
- Exact-span vs. zero-padded-span split: 53 vs. 8,612 (0.6% exact, 99.4% padded).
- Version-value distribution: 8,497 (`0x0`) / 166 (`0x4`) / 2 (`0x20`) — a 98.1% / 1.9% / 0.02% split.
- Reserved word is uniformly `0x00000000` (8,665/8,665) — no observed exception.
- Sample-rate field is uniformly `22050` (8,665/8,665) — no observed exception.
- ADPCM-data-byte-count field is 16-byte-aligned for 8,665/8,665 entries (100%).
- The declared-data ceiling adopted by the native decoder (4 MiB) is stated in `VAG.md` to be
  "more than four times the observed largest complete VAG span of 929,792 bytes" — i.e. the ceiling
  is derived from, and safely exceeds, the aggregate maximum rather than being an invented number.
- LPD/VAG directory co-occurrence: 852 same-directory pairs out of 862 total LPD entries (98.8%),
  10 LPD entries without a same-directory VAG name (1.2%) — a structural correlation only, not a
  decoded reference/lookup mechanism.

## 5. Hypotheses

Each is explicitly unconfirmed; each states the privacy-safe collection that would move it to
Confirmed or Rejected.

- **H1 — LPD/VAG basename pairing is a real cross-reference used by the retail engine (e.g. lip-sync
  driven by dialogue audio).** ASSET-RECON already flags this as suggestive but undecoded (C15).
  Confirming observation: a tracked, privacy-safe structural analysis of retail code paths, executable
  strings, or script/table text (already in-tree, not extracted from disc images) that names an
  LPD-to-VAG lookup rule, OR a documented format spec (already published, non-owner) describing the
  same pairing convention for this engine family. Absent such a source, this remains speculative and
  must not be encoded into any decoder.
- **H2 — The three observed version values (`0`, `4`, `0x20`) correspond to distinct VAG sub-variants
  (e.g. mono vs. interleaved, or tool-version markers) rather than being an unstructured version
  counter.** Confirming observation: a tracked published spec or engine source excerpt that assigns
  meaning to VAG version constants; absent that, the field remains an opaque accepted-but-uninterpreted
  value in the decoder (per C11/`VAG.md`).
- **H3 — The 28-byte "opaque header bytes" region (offset `0x14`–`0x2F`) carries loop points, channel
  count, or other PS-ADPCM metadata (as in some retail VAG variants documented in the broader PS2
  ecosystem).** Confirming observation: an aggregate byte-value/entropy breakdown of that specific
  byte range across the full 8,665-entry population (not a single file), produced by extending
  `fingerprint_assets.py` to bucket those 28 bytes, checked in as a new aggregate field — without
  ever printing per-file values.
- **H4 — The non-16-byte-aligned zero-tail lengths (16–2,032 bytes) reflect padding to a fixed
  container-record boundary (e.g. sector or bucket alignment) inside the owning HOG archive, rather
  than being VAG-format padding.** Confirming observation: an aggregate cross-tabulation of VAG tail
  length against enclosing-HOG entry alignment/stride, computed and published as an aggregate field
  (no per-file rows), to see whether tail length is a deterministic function of container alignment
  rather than of the VAG payload itself.

## 6. Missing observations

- No published byte-level breakdown of the 28 "opaque" header bytes (offset `0x14`–`0x2F`) exists in
  any tracked `*.md` or `asset-fingerprints.json` field — only "ignored" per `VAG.md`. A privacy-safe
  collection step would extend `fingerprint_vag` to emit aggregate per-offset statistics (e.g.
  all-zero rate, distinct-value counts) over the full corpus, not any single file's bytes.
- No tracked evidence establishes a decoded relationship between VAG entries and LPD entries beyond
  same-directory basename co-occurrence counts (C14/C15). No lookup-table, index, or script-level
  reference has been mechanically confirmed.
- No tracked evidence covers adversarial/hostile VAG inputs (malformed magic, oversized declared
  data, truncated headers) at the *aggregate corpus* level — the native decoder's adversarial test
  coverage (per `VAG.md`/E-0090) is against project-authored synthetic fixtures only, not against a
  scan of the real corpus for boundary-violating entries. A privacy-safe collection step: extend the
  fingerprinter to count corpus entries that would be *rejected* by the decoder's exact acceptance
  rules (e.g. version not in {0,4,0x20}, tail length outside 0 or 16–2,032, unaligned data span) and
  publish that as a new aggregate bucket — this would show whether the decoder's envelope already
  covers 100% of the tracked corpus or diverges from some entries.
- No tracked evidence records per-archive (HOG-file) distribution of `.vag` counts (e.g. how many
  VAG entries per SNDVAG.HOG-style archive across the full 273-archive population) — `hog-validation.json`
  gives only the aggregate top-level suffix count (8,665), not a per-archive-type breakdown. A
  privacy-safe collection step: aggregate `.vag` member counts grouped by archive *type* (generic
  container name, not path), already partially visible in the archive list's `entry_count` field but
  not broken out by contained suffix.
- No tracked evidence connects VAG's PCM16 output to any runtime audio backend, mixer, or SDL
  integration — this is explicitly non-claimed in `VAG.md` and E-0090, and no other tracked file
  changes that.

## 7. Decoder/tooling status

**Classification: canonical_decoder**

- Native decoder: `omega::retail::DecodeVagAdpcm` declared in
  `native/include/omega/retail/vag_adpcm_decoder.h`, implemented in
  `native/src/retail/vag_adpcm_decoder.cpp`. It performs full envelope validation (magic, version
  set, reserved word, frame alignment, tail-length rule) and the standard 5-predictor / 13-shift
  PS-ADPCM sample transform, producing an owned `omega::asset::MonoPcm16IR` value — this exceeds a
  structural-envelope-only or passive-descriptor classification because it produces decoded sample
  data, not just structural spans.
- Build/test registration: source file listed in `CMakeLists.txt` (~line 104); dedicated test binary
  target `omega_vag_adpcm_decoder_tests` (`native/tests/vag_adpcm_decoder_tests.cpp`) built and
  registered as CTest `omega_vag_adpcm_decoder_tests` with a 10-second timeout
  (`CMakeLists.txt` ~lines 565-588).
- Verification evidence (ledger E-0090): warning-free focused MSVC Debug and Release builds; focused
  CTest 1/1 passed in both configurations; warning-free full Debug integration build with CTest
  41/41 passed. That ledger wording predates publication; the implementation is present on current
  main at commit `a6d3ee5`. Owner-corpus coverage remains a separate unclaimed result.
- Owner-corpus coverage gap: per `VAG.md` and E-0090, the decoder's malformed/truncated/
  oversized/limit-boundary coverage is against **project-authored synthetic fixtures**, not against
  the real tracked corpus. There is no tracked evidence of a corpus-wide "would this decoder accept
  every one of the 8,665 real entries" sweep (see Missing observations, item 3) — this is the concrete
  gap between "decoder proven correct on synthetic vectors" and "decoder proven to cover the full
  owner corpus."
- Explicit non-claims (per `VAG.md` and E-0090, both tracked): no playback, looping, resampling,
  mixing, streaming, caching, SDL upload, container/track selection, title-specific sound-role
  assignment, or LPD-association semantics are implemented or claimed.

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. Some require an authorized private owner-corpus rerun;
only fixed aggregate output may be reviewed for publication, with owner paths and bytes remaining
outside version control.

1. **Highest priority — corpus-coverage sweep.** Extend `tools/fingerprint_assets.py`'s
   `fingerprint_vag` to additionally count, in aggregate only, how many of the 8,665 real corpus
   entries would be accepted vs. rejected by `DecodeVagAdpcm`'s exact acceptance rules (version ∈
   {0,4,0x20}; reserved=0; rate=22050; data-size % 16 == 0; tail ∈ {0} ∪ [16,2032]). Publish the
   resulting aggregate bucket in `asset-fingerprints.json` and note it in `VAG.md`/`ASSET-RECON.md`.
   This closes the owner-corpus coverage gap noted in Section 7 without touching any
   per-file identity.
2. Do not publish raw opaque-header values or distinct byte patterns. Any future probe must use a
   predeclared, fixed classification whose output cannot reconstruct source bytes.
3. Add an aggregate cross-tabulation of VAG zero-tail length against the alignment/stride of its
   enclosing HOG entry (using already-tracked `hog-validation.json` archive metadata) to test
   Hypothesis H4 — publish only bucketed counts (e.g. "N entries with tail length T at container
   stride S"), never per-file rows.
4. Run the existing registered CTest target (`omega_vag_adpcm_decoder_tests`) during the next
   proportionate native verification pass; do not describe publication itself as pending.
5. If H1 (LPD/VAG cross-reference) is ever to be pursued, restrict investigation strictly to other
   already-tracked, non-owner sources (published engine documentation, in-repo script/table text) —
   do not attempt to infer the relationship from corpus byte content, and do not encode any lookup
   rule into a decoder until a tracked source names it explicitly.
