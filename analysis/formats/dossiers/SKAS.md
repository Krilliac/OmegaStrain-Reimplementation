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


1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
