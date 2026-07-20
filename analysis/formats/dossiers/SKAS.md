# .skas — Dossier

## 1. Identity

`.skas` is a distinct game-asset suffix found only inside HOG archives. At the evidence level
this dossier can defend, it is a **bounded printable-ASCII, CRLF-delimited text envelope** with a
fixed line/blank-line/colon-line shape — kept structurally separate from `.ska` (the counted
32-bit-word binary animation-family envelope) because the aggregate corpus contains only two
`.skas` candidates and no evidence connects the two suffixes. No label/value grammar, no
relationship to `.ska`/`.skl`/`.skm`, and no animation, skeleton, actor, or gameplay semantics are
established for `.skas`.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive, inside HOG archives | 2 | `analysis/formats/asset-fingerprints.json` (`extensions_by_family... "skas": {"count": 2, ...}` and the recursive suffix histogram entry `".skas": 2`) |
| Top-level HOG member suffix | 2 | `analysis/formats/hog-validation.json` (`entry_extensions` block, `".skas": 2`) |
| Whole-disc file suffix | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` — no `.skas`-suffixed path appears in the whole-disc histogram or per-file manifest; the only disc-level hit for the string "SKAS" is the container `GAMEDATA/COMMON/SKAS.HOG` itself (a HOG archive name, not a `.skas` member), per `analysis/manifests/disc-files.jsonl` line recording `path: GAMEDATA/COMMON/SKAS.HOG`. |

This family occurs only nested inside HOG archives (both tracked recursive and top-level HOG
counts agree at 2), never as a bare top-level disc file, so it is treated as an in-scope
game-asset format per the task's own framing.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| C1 | The public fingerprint corpus records exactly 2 `.skas` candidates, both ASCII, both CRLF-only, both ending with CRLF. | `analysis/formats/asset-fingerprints.json` (`"skas": {"ascii": 2, "count": 2, "crlf_only": 2, "ends_with_crlf": 2, ...}`) |
| C2 | Aggregate ranges for the 2 candidates: content byte range 5,129–5,155 bytes; padding byte range 1–3 bytes; line count range 72–72 (fixed); blank-line count range 5–5 (fixed). | `analysis/formats/asset-fingerprints.json` (`skas` block: `content_bytes_range`, `padding_bytes_range`, `line_count_range`, `blank_line_count_range`) |
| C3 | The published grammar doc states the closed envelope: physical span 5,132–5,156 bytes; logical text 5,129–5,155 bytes; 1–3 trailing zero-padding bytes; logical bytes are printable ASCII `0x20`–`0x7e` plus paired CRLF; exactly 72 CRLF-terminated lines including exactly 5 empty lines; exactly 67 lines contain exactly one colon. | `analysis/formats/SKAS.md` §"Evidence boundary" |
| C4 | `ASSET-RECON.md` corroborates the same closed envelope (72 lines, 5 blank, 67 single-colon, 1–3 trailing NUL bytes, 5,132–5,156-byte physical spans) and states explicitly that "labels, values, relationships, and any association with SKA are not yet established" and "No native SKAS descriptor is implied by the SKA implementation." | `analysis/formats/ASSET-RECON.md` §"SKA and SKAS structural envelopes" |
| C5 | Ledger entry E-0026 (confirmed) states the two SKAS candidates "satisfy a bounded printable-ASCII CRLF text envelope" with "No animation, timing, transform, channel, compression, label, value, or relationship semantics" assigned. | `analysis/evidence/ledger.jsonl` id `E-0026` |
| C6 | Ledger entry E-0027 (confirmed) states the fixed-output retail-only SKA descriptor "retains no payload bytes and assigns no actor, skeleton, timing, channel, transform, compression, or animation semantics; SKAS remains separate." | `analysis/evidence/ledger.jsonl` id `E-0027` |
| C7 | Ledger entry E-0093 (confirmed) documents the addition of a stateless, reentrant `omega_retail_formats` SKAS structural-text adapter and a canonical owned `SkasTextEnvelopeIR`, explicitly "without connecting it to content or runtime systems," and enumerates the exact fixed acceptance envelope (5,132–5,156-byte physical span; 5,129–5,155-byte logical text; 1–3 trailing zero bytes; printable ASCII 0x20–0x7e plus paired CRLF; final CRLF; exactly 72 lines; exactly 5 empty lines; exactly 67 single-colon lines). | `analysis/evidence/ledger.jsonl` id `E-0093` |
| C8 | `tools/fingerprint_assets.py` defines `fingerprint_skas(file, span, stats)` — "Record only bounded text-shape aggregates for the distinct SKAS family" — registered in the format-handler dispatch table under key `".skas"`. | `tools/fingerprint_assets.py` (function `fingerprint_skas`, dispatch entry `".skas": fingerprint_skas`) |
| C9 | A canonical intermediate representation exists: `omega::asset::SkasTextEnvelopeIR` (owning `logical_text` plus a `std::vector<SkasOpaqueTextLineIR>` of opaque byte ranges per line) and `SkasOpaqueTextLineIR` (a byte range into `logical_text` excluding its terminator). | `native/include/omega/asset/skas_text_envelope_ir.h` |
| C10 | A native decoder function `omega::retail::DecodeSkasTextEnvelope(std::span<const std::byte> bytes, asset::DecodeLimits limits = DefaultSkasDecodeLimits())` exists, together with named fixed constants mirroring the aggregate envelope: `kSkasMinimumPhysicalBytes=5132`, `kSkasMaximumPhysicalBytes=5156`, `kSkasMinimumLogicalTextBytes=5129`, `kSkasMaximumLogicalTextBytes=5155`, `kSkasMinimumZeroPaddingBytes=1`, `kSkasMaximumZeroPaddingBytes=3`, `kSkasLineCount=72`, `kSkasBlankLineCount=5`, `kSkasSingleColonLineCount=67`, `kSkasMaximumDecodedItems = 1 + kSkasLineCount`. | `native/include/omega/retail/skas_text_envelope_decoder.h` |
| C11 | The decoder's format-scoped default (`DefaultSkasDecodeLimits`) sets `maximum_string_bytes` to `kSkasMaximumLogicalTextBytes` (5,155), tightening the shared default string budget rather than widening any fixed ceiling. | `native/include/omega/retail/skas_text_envelope_decoder.h` (function `DefaultSkasDecodeLimits`) |
| C12 | The decoder implementation and its test file are registered as first-class native build/test targets: source `native/src/retail/skas_text_envelope_decoder.cpp` is compiled into the build; test executable `omega_skas_text_envelope_decoder_tests` (from `native/tests/skas_text_envelope_decoder_tests.cpp`) is linked against `omega_retail_formats`, registered via `add_test`, with a 10-second CTest timeout. | `CMakeLists.txt` (lines listing `native/src/retail/skas_text_envelope_decoder.cpp`, the `omega_skas_text_envelope_decoder_tests` target, `add_test`, `set_tests_properties ... TIMEOUT 10`) |
| C13 | No `.skas`/`SKAS` reference exists in `native/apps/omega_tool/asset_commands.cpp` — the CLI/runtime asset-metadata tool that does implement `.ska` structural recording (`SkaStructuralStats`, `RecordSkaStructure`) has no corresponding SKAS entry point. | `native/apps/omega_tool/asset_commands.cpp` (absence of any `Skas`/`SKAS`/`skas` symbol, contrasted with the present `SkaStructuralStats` / `RecordSkaStructure` symbols) |
| C14 | Top-level and recursive suffix counts agree exactly (2 and 2), while the `.ska` counts differ between the two views (213 recursive vs. 212 top-level in `hog-validation.json`), which is a tracked aggregate fact, not an interpretation. | `analysis/formats/asset-fingerprints.json` (`".ska": 213` in the recursive histogram) vs. `analysis/formats/hog-validation.json` (`".ska": 212` in `entry_extensions`) |

## 4. Aggregate-only facts

No semantic interpretation is applied to any of these — they are tracked aggregate measurements
only.

- Sample size: exactly 2 `.skas` candidates in the entire tracked corpus (`asset-fingerprints.json`, `hog-validation.json`).
- Content/logical-text byte range across the 2 candidates: 5,129–5,155 bytes (`asset-fingerprints.json` `content_bytes_range`).
- Physical span range across the 2 candidates: 5,132–5,156 bytes (`SKAS.md`, `ASSET-RECON.md`).
- Trailing zero-padding byte range: 1–3 bytes (`asset-fingerprints.json` `padding_bytes_range`; `SKAS.md`).
- Line-count is fixed at exactly 72 for both candidates (`line_count_range: [72, 72]`).
- Blank-line count is fixed at exactly 5 for both candidates (`blank_line_count_range: [5, 5]`).
- Single-colon line count is fixed at exactly 67 for both candidates (`SKAS.md`, `ASSET-RECON.md`).
- Both candidates are pure ASCII, CRLF-only, and end with a final CRLF (`asset-fingerprints.json`: `ascii: 2`, `crlf_only: 2`, `ends_with_crlf: 2`).
- The neighboring `.ska` family (213 recursive / 212 top-level occurrences) is a separately proven counted 32-bit-word binary envelope (version word 3, 16-byte aligned, 112-byte header) — an aggregate fact about a *different* suffix, cited here only to explain why `.skas` is kept structurally separate (`ASSET-RECON.md`).
- `.skas` occurrences are HOG-nested only; the whole-disc top-level histogram records zero bare `.skas` files (`disc-summary.json` / `disc-files.jsonl`).

## 5. Hypotheses

Each is explicitly labeled speculative and paired with the privacy-safe aggregate observation
that would confirm or refute it, using only the owner's already-extracted corpus (no new disc
access, no payload disclosure).

- **H1 — `.skas` is a per-actor or per-skeleton sidecar text manifest referencing `.ska`/`.skl` assets by name.**
  Confirm/refute signal: an aggregate check of whether the 67 single-colon lines' *left-of-colon*
  token lengths and character-class distribution (not values) cluster into a small fixed set of
  label-shaped strings across both candidates, reported only as counts/lengths, never as text.
  `analysis/formats/SKAS.md` explicitly states the decoder "deliberately does not split a line at
  its colon," so no observation currently supports or refutes this.
- **H2 — The 5 blank lines are section separators dividing the 72-line body into a fixed number of logical sections.**
  Confirm/refute signal: an aggregate positional-index check (line index only, not content) of
  where the 5 blank lines fall in each of the 2 candidates — if both candidates' blank-line index
  sets are identical, that is a citable structural regularity; if they differ, the hypothesis is
  refuted. No such positional aggregate is currently recorded in any tracked source, so this
  remains untested.
- **H3 — `.skas` files are configuration/metadata for the SKA animation pipeline rather than an unrelated system entirely.**
  Confirm/refute signal: the sample size (2) is too small and the tracked evidence base
  (`ASSET-RECON.md`) explicitly disclaims any SKA-association. Confirming would require either (a)
  a larger owner-corpus sample of `.skas` candidates showing a stable count-scaling relationship
  with `.ska`/`.skl` counts, or (b) an aggregate archive-level co-occurrence count (see §6) showing
  `.skas` consistently co-resides with a fixed ratio of `.ska`/`.skl` members. The `.skl` family's
  `BONENOSCALE` profile note in `ASSET-RECON.md` concerns `.skl` only and must not be conflated
  with `.skas`.

## 6. Missing observations

- **No positional/index-level statistics for blank-line or colon-line placement** exist in any
  tracked source (only counts: 5 blank, 67 single-colon, out of 72 total). Collection: extend
  `tools/fingerprint_assets.py`'s `fingerprint_skas` handler to additionally record, per candidate,
  the sorted line-index list of blank lines and of single-colon lines (indices only, never line
  text), aggregated across the corpus, and re-run against the owner's already-extracted tree —
  this stays path-free and payload-free.
- **No left-of-colon / right-of-colon token-shape statistics** (lengths, character classes) exist.
  Collection: extend the same handler to record aggregate token-length histograms for the substring
  before the first colon and after it on each single-colon line, without ever retaining or emitting
  the substrings themselves.
- **No cross-suffix co-occurrence data** (e.g., whether a given HOG that contains a `.skas` member
  also contains a specific count of `.ska`/`.skl`/`.skm` members) is recorded. Collection: extend
  `tools/fingerprint_assets.py`'s HOG-walk to emit an aggregate per-container suffix-count vector
  specifically keyed to the archives that contain `.skas`, reporting counts only, no member names.
  The tracked archive name `SKAS.HOG` (already present in `analysis/manifests/disc-files.jsonl`,
  `GAMEDATA/COMMON/SKAS.HOG`) is a generic container name already published and not a private path,
  but the members inside any HOG are out of scope to enumerate individually per the clean-room
  rules — only aggregate counts may be reported.
- **No adversarial/boundary CTest evidence beyond E-0093's own ledger summary is separately tracked**
  as an independent artifact — the ledger entry states adversarial coverage (both size boundaries,
  padding boundaries, printable and line-ending rejection classes, 71/73-line shapes, wrong
  blank/colon counts, colon-edge opacity, limit boundaries, tighter shared string default, hard-limit
  rejection, ownership, determinism, zero scratch/depth, allocation failures) but no separate
  aggregate test-count artifact is cited beyond the ledger's prose. Collection: none required
  beyond what exists unless independent verification is desired — re-running the registered
  `omega_skas_text_envelope_decoder_tests` CTest target (`CMakeLists.txt`) against the current tree
  would reproduce/refresh that evidence.

## 7. Decoder/tooling status

**Classification: `structural_envelope_only`**

Rationale: a real, registered, tested native decoder exists (`omega::retail::DecodeSkasTextEnvelope`
in `native/include/omega/retail/skas_text_envelope_decoder.h` / `native/src/retail/skas_text_envelope_decoder.cpp`,
built and CTest-registered per `CMakeLists.txt`, producing the owned `omega::asset::SkasTextEnvelopeIR`
in `native/include/omega/asset/skas_text_envelope_ir.h`). It strictly validates and losslessly
retains the exact logical text and opaque per-line byte ranges within the aggregate-proven bounded
text envelope (C10–C12 above). It is not classified `canonical_decoder` because, by its own
explicit non-claims (`analysis/formats/SKAS.md` §"Non-claims"; ledger `E-0093`), it assigns **no**
label/value grammar, no key/value split at the colon, and no relationship to `.ska`/`.skl`/`.skm`
or any runtime/gameplay meaning — it decodes *shape*, not *semantics*, and is explicitly not wired
into `native/apps/omega_tool/asset_commands.cpp` (C13) or any content/runtime system (ledger
`E-0093`: "without connecting it to content or runtime systems"). This is a stronger claim than
`passive_descriptor_only` (which would imply a fixed-size result struct with no owned variable
content) because the IR *does* own variable-length text and per-line ranges — hence
`structural_envelope_only` rather than `passive_descriptor_only` or `aggregate_scanner_only`
(the aggregate scanner, `tools/fingerprint_assets.py`'s `fingerprint_skas`, is a separate,
earlier-tier artifact that only records counts, per C8, and does not by itself justify a higher
classification).

Gap: the CLI/tool integration (`omega_tool asset-metadata-verify-tree`) that exists for `.ska`
(`RecordSkaStructure` / `SkaStructuralStats` in `asset_commands.cpp`) has no `.skas` counterpart
(C13) — the decoder exists as a library-level, test-covered capability but is not exposed as a
corpus-wide verification command the way `.ska` is.

## 8. Codex work order

Ranked, concrete, privacy-safe. Every item operates on the owner's already-extracted tracked
corpus or its aggregate scanners/decoders — no new disc access, no payload disclosure, no
semantic invention.

1. **Highest priority — wire `DecodeSkasTextEnvelope` into `omega_tool asset-metadata-verify-tree`.**
   Add a `.skas` branch to `native/apps/omega_tool/asset_commands.cpp` mirroring the existing
   `RecordSkaStructure`/`SkaStructuralStats` pattern for `.ska`: run the decoder across every
   `.skas` candidate in the owner corpus, and emit only sanitized aggregate counters (accept/reject
   count, physical/logical byte ranges observed, padding-byte range, blank/colon-line counts) —
   exactly the shape already used for `.ska`. This closes the one concrete integration gap
   identified in §7 and gives independent, reproducible corpus-wide confirmation of the two-file
   envelope claim already in the ledger, at negligible risk since the decoder is already built
   and tested.
2. Extend `tools/fingerprint_assets.py`'s `fingerprint_skas` handler to additionally emit, per
   candidate, the blank-line and single-colon-line *index sets* (positions only) as an aggregate
   list, to directly test Hypothesis H2 (structural regularity of separator placement) without
   touching content.
3. Extend the same handler to emit aggregate token-length/character-class histograms for the
   substrings on either side of each single-colon line's colon (never the substrings themselves),
   to gather evidence toward or against Hypothesis H1 (label/value shape) while staying strictly
   within the "counts, not text" boundary already established for this family.
4. Add an archive-level co-occurrence aggregate to the HOG-validation pass (`hog-validation.json`
   generation) that reports, for each container-name bucket already tracked (e.g. the `SKAS.HOG`
   name already present in `disc-files.jsonl`), the counts of co-resident `.ska`/`.skl`/`.skm`
   suffixes — counts only, no member enumeration — to gather evidence toward or against
   Hypothesis H3 without violating the no-per-file-row rule.
5. Re-run the registered `omega_skas_text_envelope_decoder_tests` CTest target as part of any
   future full-suite validation pass to keep the E-0093 adversarial-coverage claim continuously
   verified as the tree changes, rather than treating it as a one-time historical result.
6. Do **not** attempt to split colon-bearing lines into label/value pairs or to assert any
   `.ska`/`.skas` relationship until one of the aggregate collections above (items 2–4) produces a
   citable structural signal; doing so before evidence exists would be exactly the kind of invented
   semantic the clean-room rules forbid.
