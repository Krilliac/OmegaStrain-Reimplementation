# .bon — Format Dossier

## 1. Identity

`.bon` is a recurring game-asset suffix found inside the title's HOG archive
containers. At the current evidence level it is an **uncharacterized
suffix**: it is enumerated by the recursive asset scanner as a distinct
extension bucket, but no tracked source defines a header magic, field
grammar, or semantic role for it. No stronger identity claim is supportable
from tracked evidence.

## 2. Occurrence evidence

| Metric | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG occurrences | 156 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".bon"]`) |
| Top-level-HOG member count | 156 | `analysis/formats/hog-validation.json` (`.bon: 156`) |
| Whole-disc occurrences | 0 | `analysis/manifests/disc-summary.json`, `analysis/manifests/disc-files.jsonl` (no `.bon` key/rows; confirmed by direct grep of both files) |

The recursive-in-HOG count and the top-level-HOG count are identical (156 =
156), which is consistent with (but does not by itself prove) all `.bon`
members residing at one nesting depth inside HOG containers rather than being
split across nested HOGs. `asset-fingerprints.json` records aggregate nesting
depth only as a global histogram (`scan.depth`: depths `-1`, `0`, `1`), not
per-extension, so a per-extension depth breakdown is not available from
tracked evidence (see Missing observations, section 6).

The whole-disc manifest showing 0 `.bon` occurrences means every `.bon`
instance the tooling has recorded comes from inside a HOG container — none
appear as loose top-level disc files.

## 3. Confirmed facts

| # | Fact | Tracked source citation |
|---|---|---|
| C-1 | `.bon` is not a key in `FORMAT_HANDLERS` in the fingerprinting tool; no structural handler exists for it | `tools/fingerprint_assets.py`, lines 499–511 (dict lists only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`) |
| C-2 | `.bon` spans are only ever routed through the generic per-extension counter (`scan.count("extensions", extension)`) and the generic compression-magic check; no format-specific parsing branch is reached for it | `tools/fingerprint_assets.py`, `scan_asset()`, lines 514–538 |
| C-3 | `asset-fingerprints.json`'s `formats` object (the per-format structural-aggregate section) has no `bon` key — only `col`, `lpd`, `par`, `ska`, `skas`, `skl`, `skm`, `tdx`, `vag`, `vpk`, `vum` are present | `analysis/formats/asset-fingerprints.json` (`formats` top-level key) |
| C-4 | No published grammar document exists for `.bon` among the tracked `analysis/formats/*.md` files (`ASSET-RECON.md`, `COL.md`, `FRONTEND-TOPOLOGY.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md` — none mention `.bon`) | Directory listing of `analysis/formats/*.md`; grep of `HOG.md` and `ASSET-RECON.md` for `.bon` returns no matches |
| C-5 | No entry for `.bon` exists in the evidence ledger | `analysis/evidence/ledger.jsonl` (grep for `bon`/`BON` returns no matches) |
| C-6 | No native decoder, descriptor header, source file, or CMake/test registration references `.bon` or a "bon"-named asset type. The only "bon"-substring hits in `native/` are `native/include/omega/retail/ska_container_descriptor.h` and `native/tests/skl_container_descriptor_tests.cpp`, both matching the unrelated `BONENOSCALE` magic literal used by the SKA/SKL skeleton-container decoders, not a `.bon` file format | `native/include/omega/retail/ska_container_descriptor.h`; `native/tests/skl_container_descriptor_tests.cpp`; `tools/fingerprint_assets.py` line 312–313 (`BONENOSCALE` check inside `fingerprint_skl`) |
| C-7 | `hog-validation.json` records the top-level-HOG member-suffix count for `.bon` as exactly 156, with no further structural detail | `analysis/formats/hog-validation.json` (`".bon": 156`) |

## 4. Aggregate-only facts

| # | Fact | Tracked source citation |
|---|---|---|
| A-1 | `.bon` appears in the same global extension histogram as 26 other suffixes (`.bin`, `.bnk`, `.col`, `.fnt`, `.gui`, `.gun`, `.hog`, `.ie`, `.lpd`, `.par`, `.prn`, `.pss`, `.scc`, `.ska`, `.skas`, `.skel`, `.skf`, `.skl`, `.skm`, `.so`, `.sub`, `.tdx`, `.txt`, `.vag`, `.vpk`, `.vum`); its count (156) is smaller than `.gun` (624), `.lpd` (862), `.par` (679), `.skl` (1261), `.skm` (4219), `.col`/`.vum` (7036 each), `.vag` (8665), `.tdx` (15248), `.hog` (6677), but larger than `.bin` (12), `.fnt` (3), `.txt` (3), `.scc`/`.prn` (1 each), `.skas` (2), `.skel` (4), `.skf` (26), `.sub` (42), `.ie` (79), `.bnk`/`.gui` (77 each), `.vpk` (85) | `analysis/formats/asset-fingerprints.json` (`scan.extensions`) |
| A-2 | The scan that produced this count covered 53,281 total asset spans across nesting depths `-1` (5 spans), `0` (32,351 spans), `1` (20,925 spans) — but this depth histogram is global, not broken out per extension, so it cannot be attributed to `.bon` specifically beyond "some subset of these depths" | `analysis/formats/asset-fingerprints.json` (`scan.asset_spans_scanned`, `scan.depth`) |
| A-3 | The dataset's declared scope is "aggregate structural fingerprints only; no proprietary payloads exported" (schema_version 3) — i.e., the entire `.bon` count of 156 is itself an aggregate figure, not a per-file listing, consistent with clean-room constraints | `analysis/formats/asset-fingerprints.json` (`scope`, `schema_version`) |
| A-4 | No standard-compression magic hit is attributed in aggregate to `.bon` specifically; the global `standard_compression_magic_hits` count is 0 across 46,604 checked spans, and no `compression_hit_extension` breakdown names `.bon` | `analysis/formats/asset-fingerprints.json` (`scan.standard_compression_magic_hits`, `scan.standard_compression_spans_checked`) |

No size-range, alignment, or byte-level aggregate specific to `.bon` exists in
tracked evidence — the fingerprinting tool only computes such aggregates
(e.g. `span_2048_byte_aligned`) inside format-specific handler functions, and
`.bon` has none (see C-1, C-2).

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

**Classification: `aggregate_scanner_only`**

`.bon` spans are visited by the generic scanning loop in
`tools/fingerprint_assets.py::scan_asset()` (lines 514–538) — every asset
span, regardless of extension, passes through the top-level-count and
compression-magic checks — but `.bon` is absent from `FORMAT_HANDLERS`
(lines 499–511), so no format-specific aggregate (header layout, magic,
size distribution, alignment) is ever computed for it. It appears solely as
a generic suffix count in `scan.extensions` and the top-level HOG
member-suffix table.

There is no native decoder, descriptor, or struct for `.bon` anywhere under
`native/` — the only "bon"-substring matches in that tree
(`native/include/omega/retail/ska_container_descriptor.h`,
`native/tests/skl_container_descriptor_tests.cpp`) are unrelated hits on the
`BONENOSCALE` magic literal consumed by the existing SKA/SKL skeleton
decoders, not a `.bon`-file decoder. `CMakeLists.txt` registers no
`.bon`-related source or test target (no `.bon`-referencing target exists to
check against, since no `.bon` source file exists to register).

There is consequently no adversarial or resource-boundary test to gap-check
— test coverage is a null set for a format with no decoder. This is not a
regression or an oversight to flag against existing coverage; it simply
means `.bon` has not yet entered the decoder pipeline at all.

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
