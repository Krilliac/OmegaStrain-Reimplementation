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

- **H1 — Version token tracks an authoring-tool or engine build version, not per-asset semantics.**
  Privacy-safe confirming/refuting observation: a tracked aggregate correlating declared-version
  token with independent structural metadata already published for other formats that co-occur in
  the same HOG (e.g. an aggregate cross-tabulation of PAR version token against COL/VUM header
  version bytes for spans sharing a container), produced without exposing per-file identity.
- **H2 — The two-bucket span-size range (2,048 / 4,096) reflects two fixed on-disk allocation
  quanta rather than a continuum.** Privacy-safe confirming/refuting observation: an aggregate
  histogram (already-aggregate, no per-file rows) of span sizes at intermediate values between
  2,048 and 4,096 in `asset-fingerprints.json`; if such a histogram were added and showed mass only
  at the two endpoints, this would confirm a bucketed-allocation hypothesis; a smooth spread would
  refute it. This dossier does not have that histogram today — it has only the range.
- **H3 — The body text after the version marker line encodes a further line-oriented grammar
  (e.g. one logical record per line).** Privacy-safe confirming/refuting observation: an aggregate
  count of logical-line counts per span (min/max/mean, no content) published alongside the existing
  `version_comment_first_line` counter; the decoder's IR already tracks "source-order opaque line
  ranges" (C7) but no tracked source publishes an aggregate over that count today.

## 6. Missing observations

- No tracked source publishes a per-bucket or full histogram of span sizes between 2,048 and 4,096
  — only the range endpoints are recorded. A privacy-safe collection: extend
  `tools/fingerprint_assets.py`'s `fingerprint_par` to call `stats.observe`/`stats.count` on a
  bucketed span-size histogram (aggregate counts only, no per-file identifiers) and re-run against
  the owner corpus.
- No tracked source publishes logical line-count statistics for the body after the marker line (see
  H3). A privacy-safe collection: add an aggregate-only line-count observation to `fingerprint_par`
  (e.g. `stats.observe("logical_line_count", n)`), re-run, and publish the resulting min/max/mean
  into `asset-fingerprints.json`.
- No tracked source reconciles the PAR.HOG `entry_count: 870` against the 679 fingerprinted `.par`
  spans (§4). A privacy-safe collection: an aggregate breakdown of PAR.HOG member-suffix counts
  (suffix → count, no names) analogous to what `hog-validation.json` already does at the top level,
  applied specifically to `GAMEDATA/COMMON/PAR.HOG`'s member list.
- No tracked source runs the native `DecodeParTextEnvelope` decoder against the owner asset corpus
  (only generated fixtures per E-0092). A privacy-safe collection: run the existing
  `omega_par_text_envelope_tests` binary's decode path (or a small aggregate-reporting CLI wrapper)
  over the real PAR spans and publish only pass/fail/error-code aggregate counts — no payload bytes,
  no per-file rows — into a new or extended JSON aggregate file.
- No tracked source documents an adversarial/resource-boundary sweep of the decoder against
  malformed real-world inputs at scale (E-0092 states tests "use generated text only"; see §7).

## 7. Decoder/tooling status

**Classification: `canonical_decoder`**

- Native decoder: `omega::retail::DecodeParTextEnvelope`, declared in
  `native/include/omega/retail/par_text_envelope_decoder.h`, implemented in
  `native/src/retail/par_text_envelope_decoder.cpp`. It parses the full observed structural
  envelope (physical bounds, padding, ASCII/CRLF validation, and the eight-token version-marker
  grammar) into an owned IR type, rather than merely describing or aggregating it.
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
  pre-mainline-rebase.
- **Adversarial/resource-boundary test gap (explicit, per E-0092):** "Tests use generated text
  only" (also stated verbatim in `analysis/formats/PAR.md` §Nonclaims) — the decoder has not been
  exercised against the real owner PAR corpus. E-0092 also explicitly states "Owner-corpus
  validation, runtime integration, publication, and exact-main validation remain unclaimed" and "A
  final local C++ rebuild was intentionally not run; protected publication CI owns combined
  LPD-plus-PAR integration." This is a real, tracked gap between "decoder exists and is unit-tested
  on synthetic fixtures" and "decoder has been run against the actual asset population."

## 8. Codex work order (ranked, privacy-safe)

1. **Highest priority:** Run the existing, already-built `DecodeParTextEnvelope` decoder (via
   `omega_par_text_envelope_tests` or a thin aggregate-reporting CLI extension) against the real
   owner PAR corpus (all 679 tracked spans) and record only aggregate pass/fail/typed-error-code
   counts — no payload bytes, no per-file identifiers — closing the "generated fixtures only" gap
   noted in E-0092 and §7.
2. Extend `tools/fingerprint_assets.py::fingerprint_par` to emit a bucketed span-size histogram and
   a logical-line-count aggregate (min/max/mean), re-run over the owner corpus, and publish the
   result into `analysis/formats/asset-fingerprints.json`; this would let H2 and H3 be confirmed or
   refuted without adding any per-file detail.
3. Produce an aggregate member-suffix breakdown specifically for the `GAMEDATA/COMMON/PAR.HOG`
   container (suffix → count only, mirroring the existing top-level `hog-validation.json`
   methodology) to reconcile the 870-entry vs. 679-fingerprint discrepancy noted in §4.
4. Confirm (or update) whether `omega_retail_formats` / the mainline rebase referenced by E-0092 as
   pending ("Final local C++ rebuild was intentionally not run") has since landed, and if so, run
   the full CTest suite once more and record a fresh ledger entry — this is process verification,
   not new semantic work, and requires no new private-input access.
5. Only after 1–4 are exhausted: if a future tracked source (e.g. a decompiled retail parser for the
   consumer of `.par`) surfaces genuine grammar beyond the version-marker line, extend
   `ParTextEnvelopeIR`/`DecodeParTextEnvelope` accordingly — but do not speculate ahead of that
   evidence; the current nonclaims boundary (no key/value grammar, no field/path/particle semantics)
   must hold until such a tracked source exists.
