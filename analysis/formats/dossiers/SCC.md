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

Each is explicitly labeled and unconfirmed. None is asserted as fact anywhere else in this
document.

- **H1 — The single HOG-embedded `.scc` instance is unrelated in content to the six whole-disc
  `.scc` instances.** The two occurrence scopes are structurally distinct in this corpus (one asset
  span living inside the HOG-archive tree the fingerprinting scanner walks; six files living at the
  top level of the disc image outside any HOG), and no tracked source links them.
  *Confirms if:* a bounded, aggregate-only leading-byte/magic comparison between the one
  HOG-embedded span and the six whole-disc files reports a shared header signature or size-formula
  match (reported only as "match count" / "mismatch count", never as reproduced bytes).
  *Refutes if:* such a comparison finds the HOG-embedded span structurally distinct from the
  whole-disc set (e.g. different leading bytes, non-comparable size class).
- **H2 — `.scc` is not a mesh/texture/audio/script game-asset format in the sense of the corpus's
  `FORMAT_HANDLERS` families.** Its occurrence count (1 HOG-embedded instance across 53,281 scanned
  asset spans) is far below every registered handler's minimum (`.skas` at 2), and no grammar
  document or decoder exists for it (§3, rows 1, 3, 5–6).
  *Confirms if:* a bounded aggregate scan of the HOG-embedded `.scc` span's header reports
  ASCII/text-dominant content or a non-binary marker shape (comparable to the plain-ASCII
  observation already published for `.par` in `PAR.md`), consistent with a non-payload auxiliary
  file rather than a binary asset.
  *Refutes if:* the scan reports a binary header shape structurally comparable to an existing
  asset family's documented grammar (e.g. `COL`, `VUM`, `TDX`).
- **H3 — The six whole-disc `.scc` occurrences share a common internal structure.** Their tight,
  small size range (80–256 bytes, §4) is consistent with instances of the same small record format
  repeated across the disc, though the range itself does not prove a shared layout.
  *Confirms if:* a bounded, aggregate-only header-byte scan across all 6 whole-disc occurrences
  reports a shared magic or common leading structure (reported as match/mismatch counts only).
  *Refutes if:* the scan reports divergent leading bytes across the 6 occurrences.

## 6. Missing observations

- **No structural scanner has ever been run against `.scc` at either scope.** `tools/fingerprint_assets.py`'s
  `FORMAT_HANDLERS` dict has no `.scc` entry (§3 row 1), so `asset-fingerprints.json` carries only a
  bare extension/depth count for the single HOG-embedded instance — zero header, magic, alignment,
  or text-ratio data. *Privacy-safe collection:* add a bounded, read-only `.scc` handler to
  `tools/fingerprint_assets.py` (mirroring the existing `fingerprint_par`/`fingerprint_col` handlers)
  that reports only aggregate leading-byte histograms, size, and printable-ASCII-ratio statistics —
  never per-file rows — and register it in `FORMAT_HANDLERS`.
- **No per-archive breakdown exists to identify which of the 273 top-level HOGs contains the
  single top-level `.scc` member.** `hog-validation.json`'s `archives` list records only
  `path`/`tag`/`entry_count`/`data_offset` per archive, not a per-archive suffix histogram.
  *Privacy-safe collection:* extend the HOG validation pass to additionally emit a per-archive
  extension-count map (aggregate counts only, keyed by the already-tracked top-level HOG path
  strings that are container names, not novel private paths) so a future dossier revision can
  state which top-level HOG contains the `.scc` member without exposing any new file-name detail.
- **No comparison has ever been run between the HOG-embedded `.scc` instance and the whole-disc
  `.scc` instances** (H1). *Privacy-safe collection:* the same bounded handler from the first row
  above, applied to both scopes, reporting only an aggregate match/mismatch verdict.
- **No published grammar document exists.** Unlike `.pop`, `.col`, `.vum`, etc., there is no
  `analysis/formats/SCC.md` grammar doc (this dossier is deliberately an evidence/status document,
  not a grammar publication). *Privacy-safe collection:* once §6 row 1's aggregate scan establishes
  whether the family has any shared, well-formed header, promote the result to a grammar document
  following the `POP.md`/`PAR.md` template, citing only aggregate/structural constants.
- **No native descriptor or decoder exists**, so there is no CMake target, no test target, and no
  adversarial/resource-boundary coverage to note a gap in (§3 rows 5–6) — the gap is total, not
  partial.
- **No ledger entry (`E-####`) has ever evaluated a claim about `.scc`.** *Privacy-safe collection:*
  file a new ledger entry once §6 row 1's scan produces a citable aggregate fact, following the
  existing claim/evidence/check schema used by neighboring `E-00xx` entries.
- **No cross-region/cross-edition comparison exists.** All counts in this dossier derive from the
  single NTSC-U owner disc represented by the tracked manifests. Whether other regions/editions
  carry the same 1-HOG-embedded/6-whole-disc pattern is unknown. *Privacy-safe collection:* if
  additional owned discs are ever fingerprinted under the same tracked pipeline, add their `.scc`
  aggregate counts to this dossier's §2/§4 tables without introducing any new per-file detail.

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

Ranked, privacy-safe, concrete next steps. None presumes a semantic role; each step either builds
tooling against the owner corpus or verifies an existing aggregate.

1. **Highest priority — add a bounded `.scc` structural handler to `tools/fingerprint_assets.py`**
   (mirroring the existing `fingerprint_par`/`fingerprint_col` handlers) that reports, as pure
   aggregates only: a leading-byte histogram (first 4–16 bytes bucketed, never reproduced), the
   full size distribution, and a printable-ASCII-ratio statistic. Register it in `FORMAT_HANDLERS`
   and re-run the existing fingerprinting pass so the single HOG-embedded `.scc` span and the six
   whole-disc `.scc` files are both covered. Publish only the aggregate JSON, following the
   existing `asset-fingerprints.json` schema.
2. Using the handler from step 1, run the comparison described in H1/§6 between the one
   HOG-embedded instance and the six whole-disc instances, reporting only a match/mismatch verdict
   — this directly confirms or refutes whether the two occurrence scopes share any structural
   relationship, without exposing any payload or path.
3. Extend `hog-validation.json`'s archive-validation pass to emit a per-archive extension-count map
   (§6, second row) so a future revision of this dossier can state which top-level HOG (by its
   already-tracked container path string) holds the `.scc` member, still without introducing any
   new private file-name detail.
4. Once step 1 produces a citable aggregate (shared magic present or absent, ASCII-dominant or
   binary), file a new `analysis/evidence/ledger.jsonl` entry (`E-00xx`) recording the confirmed
   aggregate claim, following the existing claim/evidence/check schema.
5. If step 1 establishes a shared, well-formed header across some or all occurrences, promote the
   result to an `analysis/formats/SCC.md` grammar document using the `PAR.md`/`POP.md` template
   (validated scope, observed layout, native contract section left `TODO` until a decoder is
   actually built) — do not write this document before the aggregate evidence exists.
6. Only after steps 1–5 produce a confirmed, citable grammar: scope and build a
   `structural_envelope_only` native descriptor (own bytes, bound sizes, reject malformed input,
   assign no semantics) under `native/include/omega/asset/`, with a corresponding CMake target and
   test file, following the pattern already established for `.vpk`'s wrapper-envelope descriptor.
   Do not attempt a `canonical_decoder` until the envelope stage is proven and reviewed.
7. Re-run this dossier's §2 occurrence-evidence table whenever `asset-fingerprints.json`,
   `hog-validation.json`, or `disc-summary.json` are regenerated, to catch drift (e.g., a future
   corpus addition changing any of the three counts away from 1/1/6).
