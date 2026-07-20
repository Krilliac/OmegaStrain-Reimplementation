# .FNT — Format Dossier

## 1. Identity

`.fnt` is a low-population suffix observed exclusively as a member type inside a
top-level `.HOG` archive (a font-related container). At the evidence level this
dossier can defend, `.fnt` is: **an in-scope game-asset suffix with a non-zero
in-archive occurrence count and NO tracked structural decoder, descriptor, or
grammar document.** Nothing about internal layout, encoding, glyph semantics,
or byte-level structure is established by tracked evidence. The name suggests
"font" by convention only — that reading is a Hypothesis (see §5), not a
Confirmed fact.

## 2. Occurrence evidence

| Metric | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG occurrences (suffix inventory across the full nested-HOG scan) | 3 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".fnt"]` |
| Top-level-HOG occurrences (member-suffix counts inside top-level HOG directories only) | 3 | `analysis/formats/hog-validation.json` → `entry_extensions[".fnt"]` |
| Whole-disc occurrences (files with this suffix present loose on the disc, outside any HOG) | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.fnt` keys present) |

The recursive-in-HOG and top-level-HOG counts are equal (3 = 3), which is
consistent with all `.fnt` members living at the top level of their containing
HOG(s) with none nested inside a secondary HOG — but this equality is an
aggregate coincidence check, not a confirmed structural claim about any
individual container.

## 3. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| C1 | The suffix `.fnt` appears exactly 3 times in the recursive-in-HOG suffix inventory produced by the asset-fingerprint scan. | `analysis/formats/asset-fingerprints.json`, `scan.extensions` |
| C2 | The suffix `.fnt` appears exactly 3 times in the top-level-HOG member-suffix count. | `analysis/formats/hog-validation.json`, `entry_extensions` |
| C3 | `.fnt` is **not** a key in `FORMAT_HANDLERS` in the fingerprinting tool — no structural handler function is registered for it. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS = {...}` block, listing `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk` only) |
| C4 | Because whole-disc scanning restricts its target extensions to `set(FORMAT_HANDLERS)`, `.fnt` is structurally excluded from the whole-disc scan pass. The whole-disc count of 0 therefore reflects "not scanned for" rather than a confirmed absence from the disc image. | `tools/fingerprint_assets.py` (`target_extensions = set(FORMAT_HANDLERS)` in the whole-disc scan function) |
| C5 | No native C++ header or source file under `native/include/omega/**` or `native/src/**` implements a decoder, descriptor, or parser keyed to the `.fnt` suffix or a `.fnt`-specific magic/tag. A text match for "Font"/"FNT" in `native/` resolves only to an unrelated, self-contained ASCII glyph-table helper (`IsSupportedProjectFontAscii`) used by the project's own console/HUD text rendering — it does not read, parse, or reference any `.fnt` game asset. | `native/apps/openomega/front_end.cpp` |
| C6 | No published format-grammar document exists for `.fnt`. The `analysis/formats/*.md` set covers `ASSET-RECON.md`, `COL.md`, `FRONTEND-TOPOLOGY.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md` — none of these mention `.fnt`. | `analysis/formats/*.md` (directory listing; grep for `.fnt`/`FNT` across `ASSET-RECON.md` and `HOG.md` returns no matches) |
| C7 | No ledger entry (`E-####`) in the evidence ledger references `.fnt`. | `analysis/evidence/ledger.jsonl` (grep for `fnt`, case-insensitive, returns no matches) |
| C8 | A tracked top-level-HOG header record exists for a font-related container archive (generic container name pattern consistent with the `DATA.HOG`/`LOADING.HOG` naming convention already used in tracked docs) whose directory-region ASCII bytes include a `.FNT`-suffixed entry name, confirming at least one `.fnt` member is directly enumerable in that container's own HOG directory structure. No member name, offset, or payload byte is reproduced here per the path-free/aggregate-only constraint; only the suffix match itself is asserted. | `analysis/formats/hog-headers.jsonl` |

## 4. Aggregate-only facts

- Population is small and stable across both aggregation levels: exactly 3
  recursive-in-HOG and exactly 3 top-level-HOG, with 0 loose-on-disc. This is
  consistent with `.fnt` being a rare, top-level-only member type rather than
  a bulk/high-cardinality asset class such as `.tdx` (15,248 recursive) or
  `.vag` (8,665 recursive) in the same scan. (`analysis/formats/asset-fingerprints.json`,
  `analysis/formats/hog-validation.json`)
- `.fnt` has no entry under `formats` in `asset-fingerprints.json` (that block
  only contains the eleven suffixes with registered handlers). No aggregate
  size range, alignment observation, or bucket histogram exists for `.fnt`
  anywhere in tracked docs — the only aggregate available is the bare
  occurrence count itself. (`analysis/formats/asset-fingerprints.json`)
- No header-magic, tag, or size field is aggregated for `.fnt` in
  `hog-headers.jsonl` beyond the single container-level record noted in C8,
  which describes the *containing* HOG's directory bytes, not the `.fnt`
  member's own internal structure. (`analysis/formats/hog-headers.jsonl`)

## 5. Hypotheses

All items below are explicitly labeled hypotheses. None is asserted as fact anywhere else in this dossier.

- **H1 — Name-implied role.** The suffix conventionally suggests a font/glyph
  or bitmap-font-metrics asset, by analogy to common `.fnt` usage in other
  engines of this era. *Confirm/refute privacy-safely by:* running the existing
  fingerprinting tool's header-preview mode (as already used for other
  suffixes in `hog-headers.jsonl`) against `.fnt` members and checking whether
  the leading bytes resolve to an ASCII/structured table consistent with
  glyph-metrics records (e.g., regular fixed-size record stride implying a
  per-character table) versus some other layout — without reproducing payload
  bytes or per-file rows in this doc.
- **H2 — Fixed small file count reflects one font family per title, not per
  member.** The count of exactly 3 could mean 3 distinct font assets (e.g.
  different weights/sizes) rather than 3 fragments of one font. *Confirm/refute
  by:* extending `asset-fingerprints.json`'s aggregate stage to report a size
  histogram (min/max/mean) and container-name bucket count for `.fnt`
  analogous to what already exists for handled suffixes — an aggregate, not a
  per-file, addition.
- **H3 — Container is font-dedicated.** The tracked header record (C8) implies
  at least one HOG container is a dedicated font container (naming pattern
  parallel to other single-purpose containers already documented, e.g.
  `SNDVAG.HOG` for audio). *Confirm/refute by:* an aggregate report of how many
  distinct top-level HOG containers hold at least one `.fnt` member, and
  whether `.fnt` co-occurs with any other suffix inside those same containers
  — again, container-name and count level only, no member names.

## 6. Missing observations

- **No structural handler ⇒ no header/magic/size aggregate.** Because `.fnt`
  is absent from `FORMAT_HANDLERS` (`tools/fingerprint_assets.py`), the pipeline
  never opens `.fnt` payloads to extract a magic number, version field, record
  stride, or size distribution. *Privacy-safe collection:* add a
  `fingerprint_fnt` handler that only ever emits aggregate counters (byte-size
  buckets, leading-magic tally, alignment-padding tally) into the existing
  `Aggregate` accumulator pattern already used by `fingerprint_col`/
  `fingerprint_vum`/etc. — never per-file rows, matching the existing schema's
  `"scope": "aggregate structural fingerprints only"` contract.
- **No whole-disc pass.** `.fnt` is excluded from `target_extensions` in the
  whole-disc scan (`set(FORMAT_HANDLERS)`), so the whole-disc count of 0 is a
  scanning artifact, not a confirmed disc-wide absence. *Privacy-safe
  collection:* extend the whole-disc target-extension set to include suffixes
  with no handler yet (bare existence counting only, no content read) so the
  whole-disc column becomes a true observation instead of a methodological
  gap.
- **No ledger entries.** Zero `E-####` claims reference `.fnt`; nothing has
  been formally proposed, confirmed, or rejected for this family. *Privacy-safe
  collection:* file an `E-####` ledger entry once any aggregate observation
  from §4/§6 lands, so the claim trail is auditable.
- **No grammar document.** No `analysis/formats/FNT.md` grammar doc exists
  (contrast with `TDX.md`, `COL.md`, `VUM.md`, etc., which document published
  structural constants). *Privacy-safe collection:* only warranted once a
  structural handler (above) produces enough aggregate structure (consistent
  magic/stride/version fields across the 3 known instances) to write from —
  writing one now would require inventing semantics, which is prohibited.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- `.fnt` is counted by the generic recursive-HOG suffix inventory (which
  tallies every member suffix regardless of type) and by the top-level HOG
  validator's suffix histogram — both are pure counting passes with no
  content interpretation. (`tools/fingerprint_assets.py`, `analysis/formats/asset-fingerprints.json`,
  `analysis/formats/hog-validation.json`)
- There is no structural envelope reader, no passive descriptor, and no
  canonical decoder: `.fnt` is not in `FORMAT_HANDLERS`, has no native
  header/source file, and has no CMake target or test registration anywhere
  in the tracked tree. A repo-wide search of `native/` for `Font`/`FNT` finds
  only an unrelated in-engine ASCII-glyph helper (`front_end.cpp`,
  `IsSupportedProjectFontAscii`) used for the project's own text rendering —
  not a `.fnt` asset reader — so it does not change this classification.
- **Test/registration gap:** since there is no handler, there is necessarily
  no adversarial or resource-boundary test (malformed-header, truncated-file,
  oversized-record fuzzing) covering `.fnt`. This gap is total, not partial.

## 8. Codex work order

Ranked, privacy-safe, no semantic speculation:

1. **Add an aggregate-only `fingerprint_fnt` handler** to
   `tools/fingerprint_assets.py`, registered in `FORMAT_HANDLERS`, modeled on
   the existing `fingerprint_col`/`fingerprint_vum` pattern: emit only
   size-bucket histograms, leading-magic/tag tallies, and alignment-padding
   counts into the `Aggregate` accumulator — no per-file rows, no payload
   bytes, no member names. This directly closes the §6 "no structural
   handler" gap and is the single highest-priority action, since every other
   Hypothesis in §5 depends on it.
2. **Widen the whole-disc `target_extensions` set** in the same tool to
   include unhandled suffixes (existence-only counting) so the whole-disc=0
   figure becomes a real observation instead of a scan-configuration
   artifact, and re-run the whole-disc pass to refresh `disc-summary.json`.
3. **Re-run the full aggregate scan** (`analysis/formats/asset-fingerprints.json`,
   `analysis/formats/hog-validation.json`) after (1)+(2) land, and diff the
   `.fnt` aggregate block against this dossier's §4 to see whether the 3/3/0
   counts and any new size/magic aggregate are stable.
3a. Cross-check whether the header record noted in §3 C8 (font-container
   directory bytes) is one of the 3 top-level-HOG `.fnt` occurrences or
   distinct from them, using only the existing aggregate/container-name level
   of detail already in `hog-headers.jsonl` — do not open the private
   container itself outside the existing tracked-doc record.
4. **Only after (1)-(3) produce a stable aggregate structure**, draft
   `analysis/formats/FNT.md` following the same grammar-constant style as
   `TDX.md`/`COL.md`/`VUM.md`, and file a corresponding `E-####` ledger entry
   in `analysis/evidence/ledger.jsonl` for whatever becomes Confirmed. Do not
   write this document from name-plausibility alone — every constant in it
   must trace to the new aggregate handler's output.
5. **Do not** attempt to infer glyph/character-table semantics, encoding, or
   rendering roles at any stage above without a mechanically citable
   structural regularity (e.g., a consistent fixed-size record count matching
   a fixed character-set size) surviving across all 3 known instances in
   aggregate form; absent that, keep `.fnt` semantics UNKNOWN per the
   clean-room conservatism rule.
