# .PAR — Evidence Dossier

## 1. Identity

`.PAR` is a HOG-archived, all-ASCII, CRLF-terminated **text envelope** whose first logical line
carries a numeric version token followed by the literal marker `;version`. Tracked evidence
establishes only this outer text-envelope shape (bounds, line endings, the marker line, and
zero-padding to a fixed physical size). No key/value grammar, field, path, asset-name, particle,
rendering, timing, or gameplay semantics is established for the body content — those remain
explicitly out of scope per the published nonclaims (see §3, §5, §6).

## 2. Occurrence evidence

| Metric | Value | Tracked source |
|---|---|---|
| Recursive-in-HOG count | 679 | `analysis/formats/asset-fingerprints.json` (`formats.par.count`) |
| Top-level-HOG count | 679 | `analysis/formats/hog-validation.json` |
| Whole-disc count | 0 | `analysis/manifests/disc-summary.json` / `disc-files.jsonl` |
| Container observed | `GAMEDATA/COMMON/PAR.HOG` (`entry_count: 870`, `data_offset: 20480`) | `analysis/formats/hog-validation.json` |
| Corpus scope cited elsewhere | "6677 nested HOGs" aggregate corpus, PAR named as one of the covered families | `analysis/evidence/ledger.jsonl` E-0011 |

All three counts (recursive-in-HOG, top-level-HOG, whole-disc) match the counts supplied in the
task instructions and are corroborated by the tracked JSON/JSONL sources above. The format occurs
only inside HOG archives (679 vs. 0 on the whole disc), confirming it is an in-archive game-asset
format rather than a loose disc file.

## 3. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| C1 | The aggregate fingerprint corpus contains 679 PAR spans; all 679 are 7-bit ASCII (`ascii: 679`), all use CRLF exclusively with no lone CR/LF (`crlf_only: 679`), and all end in trailing NUL padding (`zero_padded: 679`). | `analysis/formats/asset-fingerprints.json` (`formats.par`) |
| C2 | Physical span sizes across the corpus range from 2,048 to 4,096 bytes (`span_bytes_range: [2048, 4096]`); the trailing zero-padding within a span ranges from 1 to 2,040 bytes (`padding_bytes_range: [1, 2040]`). | `analysis/formats/asset-fingerprints.json` (`formats.par`) |
| C3 | Every one of the 679 spans has a first logical line matching the marker pattern (`version_comment_first_line: 679`), captured via the fixed regex `^([0-9]+\.[0-9]+)\s*;version(?:\r\n|\n)`. | `tools/fingerprint_assets.py` (`VERSION_LINE`, `fingerprint_par`) |
| C4 | The scanner records the exact captured numeric token verbatim (no normalization); exactly eight distinct token spellings are observed in the corpus, with these counts: `1.300000`=10, `1.400000`=1, `1.500000`=7, `1.700000`=2, `1.800000`=222, `1.900000`=100, `2.000000`=187, `2.100000`=150 (sums to 679). | `analysis/formats/asset-fingerprints.json` (`formats.par.declared_version`) |
| C5 | No `1.600000` token is observed in the corpus. | `analysis/formats/asset-fingerprints.json` (`formats.par.declared_version`); confirmed narratively in `analysis/formats/PAR.md` |
| C6 | A native decoder exists: `omega::retail::DecodeParTextEnvelope(std::span<const std::byte>, asset::DecodeLimits)`, declared in `native/include/omega/retail/par_text_envelope_decoder.h`, implemented in `native/src/retail/par_text_envelope_decoder.cpp`. It is documented as stateless and reentrant, accepts only the observed 2,048–4,096-byte physical range, requires 1–2,040 bytes of all-zero trailing padding, validates 7-bit ASCII/CRLF-only text, and recognizes only the eight exact tokens in C4. | `native/include/omega/retail/par_text_envelope_decoder.h`; `analysis/formats/PAR.md` |
| C7 | The decoder's owned IR type is `omega::asset::ParTextEnvelopeIR`, declared in `native/include/omega/asset/par_text_envelope_ir.h`. Per the published contract it retains the exact logical text and original token spelling, source-order opaque line ranges, terminator size, a fixed declared-version enum, and the omitted-padding-byte count; it does not rewrite, trim, split, or normalize body content. | `native/include/omega/asset/par_text_envelope_ir.h`; `analysis/formats/PAR.md` |
| C8 | The decoder has a dedicated unit-test binary registered in the build: `native/tests/par_text_envelope_decoder_tests.cpp`, built as CMake target `omega_par_text_envelope_tests`, linked against `omega_retail_formats`, and registered as CTest `omega_par_text_envelope_tests` with a 5-second timeout. | `CMakeLists.txt` (lines defining `omega_par_text_envelope_tests` and its `add_test`) |
| C9 | The ledger records a confirmed claim (E-0092) that the decoder passed focused MSVC Debug and Release unit/CTest runs (1/1 each), a full runtime-off Debug CTest run (37/37), and full static-validation gates (dependency gate, tooling tests, formatting, public-tree gate) prior to the final mainline rebase; it explicitly states final combined C++ integration, owner-corpus validation, and runtime integration remain unclaimed. | `analysis/evidence/ledger.jsonl` (E-0092) |
| C10 | `.par` is registered in the format-handler dispatch table (`FORMAT_HANDLERS[".par"] = fingerprint_par`), i.e. it has a structural fingerprint handler distinct from formats with no handler. | `tools/fingerprint_assets.py` (line mapping `".par"` to `fingerprint_par`) |

## 4. Aggregate-only facts

These are corpus-wide statistics with no semantic interpretation attached.

- Span-size distribution is bimodal-bounded: minimum observed physical span 2,048 bytes, maximum
  4,096 bytes (`analysis/formats/asset-fingerprints.json`, `formats.par.span_bytes_range`). This is
  consistent with fixed-size allocation buckets (e.g. two adjacent power-of-two sizes), but the
  bucket boundary/count is not itself published — only the min/max of the observed range.
  `analysis/formats/PAR.md` explicitly declines to claim every integer size within the range occurs.
- Padding-byte counts range 1–2,040 across the corpus (`padding_bytes_range`), meaning every one of
  the 679 spans carries at least one trailing zero byte and at most 2,040.
  This is an aggregate min/max only — the ledger explicitly does not claim a distribution shape.
- Version-token histogram (C4) shows uneven weighting: `1.800000` (222) and `2.100000` (150) and
  `2.000000` (187) together account for 559 of 679 spans (~82%); the remaining six tokens are much
  rarer. No semantic meaning (e.g. release chronology, subsystem) is attached to this skew by any
  tracked source.
- The PAR.HOG top-level container entry lists `entry_count: 870` while the aggregate PAR fingerprint
  count is 679 — these are two different tracked measurements (top-level HOG directory entries vs.
  recursively fingerprinted `.par`-suffixed spans) and the gap (870 vs. 679) is not reconciled by any
  tracked source; treat the two numbers as independent aggregates, not as contradicting each other.

## 5. Hypotheses (explicitly labeled — none confirmed)


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

- Native decoder: `omega::retail::DecodeParTextEnvelope`, declared in
  `native/include/omega/retail/par_text_envelope_decoder.h`, implemented in
  `native/src/retail/par_text_envelope_decoder.cpp`. It parses the observed text envelope
  (physical bounds, padding, ASCII/CRLF validation, and the eight-token version-marker grammar)
  into an owned IR while explicitly assigning no key, value, field, path, or particle semantics.
- Owned IR: `omega::asset::ParTextEnvelopeIR` in `native/include/omega/asset/par_text_envelope_ir.h`.
- Build/test registration: CMake target `omega_par_text_envelope_tests`
  (`native/tests/par_text_envelope_decoder_tests.cpp`), linked against `omega_retail_formats`,
  registered via `add_test(NAME omega_par_text_envelope_tests ...)` with a 5-second CTest timeout
  (`CMakeLists.txt`).
- Ledger corroboration: E-0092 (`analysis/evidence/ledger.jsonl`) confirms the decoder's fixture
  coverage (every observed version token, whitespace/marker policy, CRLF vs. lone-newline
  rejection, ASCII boundary cases, malformed/unsupported tokens, padding and physical bounds,
  exact and one-below caller budgets, hard derived text/line maxima, hostile tails, ownership,
  determinism, zero scratch, allocation-failure injection) and passing focused + full CTest runs
  before publication.
- **Owner-corpus validation gap (explicit, per E-0092):** "Tests use generated text
  only" (also stated verbatim in `analysis/formats/PAR.md` §Nonclaims) — the decoder has not been
  exercised against the real owner PAR corpus. Publication is no longer pending: the PAR
  implementation is present on current main at commit `13db063`. Owner-corpus validation and
  runtime integration remain unclaimed. This is a real, tracked gap between "decoder exists and is
  unit-tested on synthetic fixtures" and "decoder has been run against the actual asset
  population."

## 8. Codex work order (ranked, privacy-safe)


1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
