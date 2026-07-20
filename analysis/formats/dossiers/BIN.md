# .bin — Format Dossier

## 1. Identity

`.bin` is a recursive-suffix bucket recorded by the asset scanner and by the
top-level HOG-member census. At the current evidence level it is an
**unclassified generic-suffix bucket**: no tracked source defines a header
magic, field grammar, or semantic role for it, and no native decoder or
descriptor exists for it. Its true nature (single homogeneous format vs. a
grab-bag of unrelated binary blobs sharing the literal suffix `.bin`) is
**UNKNOWN** and must not be guessed.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (all archive depths, scanner's asset-span walk) | 12 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".bin"]`) |
| Top-level-HOG member census (depth-0 HOG directories only) | 12 | `analysis/formats/hog-validation.json` (`entry_extensions[".bin"]`) |
| Whole-disc filesystem-entry histogram | 22 | `analysis/manifests/disc-summary.json` (`extensions[".bin"]`) |

The recursive-in-HOG figure equalling the top-level-HOG figure (12 = 12) is
consistent with all observed `.bin` HOG members living at archive depth 0,
i.e. none of them were found nested inside a child HOG during the scanner's
recursive walk — but this dossier does not assert that as a separate
"Confirmed" fact beyond the two source counts themselves, since no source
states it as an explicit invariant.

The whole-disc manifest and the HOG-member scans count different populations.
`disc-summary.json` counts filesystem entries whose own names end in `.bin`;
it does not expand a `.hog` filesystem entry into its internal member rows.
Conversely, the recursive and top-level HOG counts describe directory members
inside HOG payloads, not filesystem rows in the whole-disc manifest. The values
22 and 12 therefore cannot be subtracted, combined, or described as a
superset/subset relationship, and neither population establishes asset role.

## 3. Confirmed facts

| # | Fact | Tracked source |
|---|---|---|
| C1 | `.bin` is not a key in `FORMAT_HANDLERS` in the asset fingerprinter — the handled suffixes are exactly `tdx, ska, skas, skm, skl, vag, lpd, par, col, vum, vpk`. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict, lines ~499–511) |
| C2 | Because `.bin` has no entry in `FORMAT_HANDLERS`, `scan_asset()` records only the aggregate extension count for `.bin` spans and applies no format-specific structural parse to them. | `tools/fingerprint_assets.py` (`scan_asset` handler-dispatch logic) |
| C3 | `analysis/formats/asset-fingerprints.json`'s `formats` object (the per-suffix aggregate-detail section) contains exactly the keys `col, lpd, par, ska, skas, skl, skm, tdx, vag, vpk, vum` — `.bin` has no per-suffix aggregate-detail entry at all, only the raw scan-count. | `analysis/formats/asset-fingerprints.json` (`formats` object keys) |
| C4 | No published grammar document among `analysis/formats/*.md` (`ASSET-RECON.md`, `COL.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md`, `FRONTEND-TOPOLOGY.md`) mentions the `.bin` suffix. | `analysis/formats/*.md` (absence confirmed by direct search) |
| C5 | `analysis/evidence/ledger.jsonl` contains no entry (confirmed or rejected) referencing the `.bin` suffix. | `analysis/evidence/ledger.jsonl` (absence confirmed by direct search) |
| C6 | No native header or source file under `native/include/omega/` or `native/src/` and no rule in `CMakeLists.txt` references the `.bin` suffix, a `.bin`-named decoder/descriptor type, or test registration for it. | `native/include/omega/**/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` (absence confirmed by direct search) |

## 4. Aggregate-only facts

- **Recursive-in-HOG scan count:** 12 asset spans with extension `.bin` were observed across the fingerprinter's recursive HOG walk (`analysis/formats/asset-fingerprints.json`, `scan.extensions[".bin"]`). Because `.bin` is not in `FORMAT_HANDLERS`, the separate direct-file pass at depth −1 does not add `.bin` candidates to this count.
- **Top-level-HOG member count:** 12 depth-0 HOG-member entries carry the `.bin` suffix, out of 32,351 total top-level entries in `analysis/formats/hog-validation.json` (`entry_extensions[".bin"]` for the 12; `total_entries` for the 32,351).
- **Whole-disc filesystem-entry count:** 22 filesystem entries on the disc image carry the `.bin` suffix, per `analysis/manifests/disc-summary.json` (`extensions[".bin"]`). This count contains no HOG-internal member rows.
- **Whole-disc size range (aggregate, computed from the tracked per-entry size field in `analysis/manifests/disc-files.jsonl`, no paths/hashes/names disclosed):** across the 22 whole-disc `.bin` entries, sizes range from 6,064 bytes to 658,432 bytes, with most entries clustered in the ~16 KB–37 KB band and one outlier at 131,072 bytes (two occurrences) and one outlier at 658,432 bytes. This is a raw byte-size aggregate only — no semantic interpretation of what those size bands represent is offered.
- No compression-magic hits were recorded anywhere in the scan (`standard_compression_magic_hits: 0` over 46,604 spans checked) — this is a global scanner statistic, not `.bin`-specific, but it means no tracked evidence suggests any `.bin` span (or any other span) opens with a recognized compression magic.
- The scanner's `formats` per-suffix aggregate-detail section — which for handled suffixes carries fields such as size ranges, bucket histograms, and alignment/padding observations — has no such section for `.bin`. There is therefore no tracked size-range, bucket, or alignment aggregate for the **in-HOG** `.bin` population specifically (only the raw count exists for that population); the size-range aggregate above is drawn from the separate whole-disc manifest, not from the fingerprinter's per-suffix detail.

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

**Classification: `aggregate_scanner_only`.**

- The only tracked-code touchpoint for `.bin` is the generic scan-count path in `tools/fingerprint_assets.py`'s `scan_asset()` function, which increments `scan.extensions[".bin"]` for every span whose suffix is `.bin` (case-insensitively) — the same generic counting path every unhandled suffix goes through. `.bin` is conspicuously absent from `FORMAT_HANDLERS` (`tools/fingerprint_assets.py`, ~line 499), which is the dict that gates all suffix-specific structural handlers (`tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`).
- There is **no native decoder**: no header in `native/include/omega/retail/` or `native/include/omega/asset/`, no source file in `native/src/**`, and no `CMakeLists.txt` target or test registration references `.bin` or a `.bin`-flavored type name.
- There is **no descriptor**, passive or otherwise: the "passive descriptor" pattern used elsewhere in this codebase (e.g. the VUM passive render-payload inspector cited in the ledger) has no `.bin` counterpart in any tracked source.
- **Adversarial/resource-boundary test gap:** since no decoder or descriptor exists, there is by definition no adversarial or resource-boundary test coverage for `.bin` anywhere in the tracked test tree. This is a total gap, not a partial one.

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
