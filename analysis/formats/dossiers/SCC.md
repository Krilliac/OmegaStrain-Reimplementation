# .scc family dossier

## 1. Identity

`.scc` is an attested suffix at three tracked scopes in the owner NTSC-U corpus: once as a
recursively-scanned asset span somewhere in the HOG hierarchy, once as a direct top-level HOG
member, and six times as a whole-disc top-level file. No tracked source assigns `.scc` a decoder,
a published grammar, a header magic, or any semantic role. At the evidence level this document can
defend, `.scc` is an **unclassified, extremely low-count asset-suffix family**: it is not a
game-asset format with any structural handler, native decoder, or grammar document, and the
tracked evidence does not establish what its single HOG-embedded instance contains or whether it
relates in any way to the six whole-disc instances of the same suffix. Per the task's occurrence
threshold, its presence inside HOG archives (recursive-in-HOG ≥ 1) places it in scope for a full
evidence-tiered dossier rather than a stub.

## 2. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive, inside any HOG (all depths scanned) | 1 | `analysis/formats/asset-fingerprints.json` (`scan.extensions` map: `".scc": 1`) |
| Top-level, inside any HOG (direct top-level HOG member) | 1 | `analysis/formats/hog-validation.json` (`entry_extensions` map: `".scc": 1`) |
| Whole-disc | 6 | `analysis/manifests/disc-summary.json` (`extensions` map: `".scc": 6`), cross-checked against `analysis/manifests/disc-files.jsonl` (6 matching `path` entries, aggregated below without reproducing individual paths, hashes, or per-file rows) |

No `analysis/evidence/ledger.jsonl` entry (`E-0001`..) references `.scc`; a full-file search of the
ledger for `scc` returned no confirmed or rejected claim for this suffix. No
`analysis/formats/*.md` grammar document is dedicated to `.scc` — the suffix appears only inside
other dossiers' cross-histogram enumerations (e.g. `BON.md`, `DAT.md`, `HOG.md`), each citing it
purely as one entry in a shared extension-count list, never as a described format.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Citation |
|---|---|---|
| 1 | `.scc` is absent from `FORMAT_HANDLERS` — the dict mapping suffixes to structural fingerprint handlers only contains `.tdx .ska .skas .skm .skl .vag .lpd .par .col .vum .vpk` | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict, ~line 499) |
| 2 | The recursive scanner (`scan_asset`) nonetheless counts every asset span's extension unconditionally via `scan.count("extensions", extension)`, independent of whether a structural handler exists for that extension — this is why `.scc` appears in `scan.extensions` with count 1 despite having no handler | `tools/fingerprint_assets.py` (`scan_asset` function, `scan.count("extensions", extension)` line, ~line 523) |
| 3 | `.scc` produces no `formats["scc"]` (or equivalent) aggregate bucket anywhere in `asset-fingerprints.json`'s `formats` object — only extension/depth counters exist for it, because no structural handler is registered (see row 1) | `analysis/formats/asset-fingerprints.json` (`formats` object keys) |
| 4 | The recursive-scan total for `.scc` (1, `scan.extensions`) equals the top-level-HOG-member total for `.scc` (1, `hog-validation.json` `entry_extensions`). Since the recursive scan counts spans at every depth including top-level, and the two totals are equal, the single `.scc` occurrence found by the recursive scan is the same one counted as a direct top-level HOG member — i.e., no additional `.scc` instance exists strictly inside a nested/embedded HOG | `analysis/formats/asset-fingerprints.json` (`scan.extensions`), `analysis/formats/hog-validation.json` (`entry_extensions`) |
| 5 | No `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h` header, and no `native/src/**/*.cpp` file, names or references a `.scc`-suffixed decoder, descriptor, or grammar; a suffix-targeted search across those trees returns no match for `scc` as a format identifier | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp` |
| 6 | `CMakeLists.txt` registers no target, source file, or test for a `.scc` decoder (no `scc`-named source is compiled or listed) | `CMakeLists.txt` |
| 7 | `analysis/evidence/ledger.jsonl` contains no confirmed or rejected entry whose `claim` or `evidence` references `.scc` | `analysis/evidence/ledger.jsonl` (full-file search) |
| 8 | `hog-validation.json` reports `error_count: 0` for the full top-level HOG validation pass (273 archives, all listed structurally valid), meaning the single top-level `.scc` member was counted as a normal, non-error directory entry, not flagged as malformed | `analysis/formats/hog-validation.json` (`error_count`, `errors`) |

## 4. Aggregate-only facts

Aggregate statistics derived from `analysis/manifests/disc-files.jsonl` for the 6 whole-disc `.scc`
occurrences, computed without reproducing any individual path, hash, or per-file row:

- Size range: minimum 80 bytes, maximum 256 bytes. Total bytes across all 6 whole-disc occurrences:
  1,024 bytes.
- All 6 whole-disc `.scc` occurrences are small enough (≤256 bytes each) that none could contain a
  structurally meaningful instance of any of the corpus's known game-asset grammars (e.g. `COL`'s
  documented 48-byte header plus payload, `VUM`'s `VUMS` header, `TDX`'s versioned header) at any
  scale comparable to the HOG-member asset families described in `ASSET-RECON.md` — this is a size
  observation only, not a content classification.
- Within the recursive corpus-wide extension histogram published in `HOG.md` (25 suffixes,
  `.tdx` 15,248 down to `.prn`/`.scc` 1 each), `.scc` and `.prn` are tied for the single lowest
  occurrence count among all HOG-embedded asset suffixes the scanner recognizes — both are rarer
  by this count than every registered `FORMAT_HANDLERS` suffix (smallest of which, `.skas`, occurs
  2 times).
- `hog-validation.json` records `archive_count: 273` top-level HOGs and `total_entries` matching
  the `depth: 0` count (32,351) in `asset-fingerprints.json`'s `scan.depth` map; the single
  top-level `.scc` member is therefore 1 of 32,351 direct top-level HOG entries, with no tracked
  source indicating which of the 273 top-level HOG archives contains it (per-archive suffix
  breakdowns are not published — see §6).
- No aggregate source reports header magic, alignment constants, leading-byte histograms, or
  ASCII/text-ratio statistics for `.scc` at any scope (HOG-embedded or whole-disc), because no
  tracked scanner or decoder has ever opened a `.scc` file structurally (see §3, rows 1–3).

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

**Classification: `aggregate_scanner_only`.** The only tracked evidence-producing mechanism that
touches `.scc` at all is the generic, handler-independent extension/depth counter built into
`tools/fingerprint_assets.py`'s `scan_asset` function (§3 row 2) and the equivalent generic
suffix-counting pass behind `hog-validation.json`'s `entry_extensions` map — neither performs any
structural inspection, header check, or content classification of `.scc` data; both merely
increment a count keyed by the literal suffix string. There is no `canonical_decoder`, no
`structural_envelope_only` decoder, and no `passive_descriptor_only` descriptor for `.scc` anywhere
in `native/include/omega/retail/`, `native/include/omega/asset/`, `native/src/`, or
`CMakeLists.txt` (§3 rows 5–6). Consequently there is no adversarial or resource-boundary test
suite to report a gap in — the gap is the complete absence of any decoder or descriptor, not a
partially-tested one.

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
