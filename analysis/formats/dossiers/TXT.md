# .txt — Format Dossier

## 1. Identity

`.txt` is a recursive-suffix bucket recorded by the asset scanner, and it is
also one of the thirteen suffixes explicitly named in the frozen output
vocabulary of the front-end HOG-topology measurement tool, where it is
mapped to the category label `"text"`. At the current evidence level it is
an **unclassified generic-suffix bucket for game-asset purposes**: no
tracked source defines a header magic, field grammar, or semantic role for
in-HOG `.txt` members, and no native decoder or descriptor exists for the
literal `.txt` suffix. Its true nature (a single homogeneous in-game text
format vs. a grab-bag of unrelated plain-text files sharing the suffix
`.txt`) is **UNKNOWN** and must not be guessed.

Two tracked source files (`native/src/retail/par_text_envelope_decoder.cpp`,
`native/src/retail/skas_text_envelope_decoder.cpp`) implement "text-envelope"
decoders, but these decode the `.par` and `.skas` suffix families
respectively — files whose *content* happens to be structured as
seven-bit-ASCII/CRLF text, not files whose *suffix* is literally `.txt`. This
dossier keeps that distinction explicit: no tracked decoder targets the
`.txt` suffix itself.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (all archive depths, scanner's asset-span walk) | 3 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".txt"]`) |
| Top-level-HOG member census (depth-0 HOG directories only) | 0 | `analysis/formats/hog-validation.json` (`entry_extensions`, `.txt` key absent) |
| Whole-disc filesystem-entry histogram | 8 | `analysis/manifests/disc-summary.json` (`extensions[".txt"]`) |

The recursive-in-HOG count (3) exceeding the top-level-HOG count (0) is
consistent with all 3 observed `.txt` HOG members living at a nested archive
depth (depth ≥ 1, i.e. inside a HOG that is itself embedded in another HOG),
since `hog-validation.json`'s `entry_extensions` census — which counts only
depth-0 (top-level) HOG members — records zero `.txt` entries while the
scanner's full recursive walk records 3. This dossier does not assert the
depth-1 placement as a separately sourced "Confirmed" fact beyond noting
that the two counts are compatible with it; no tracked source states the
per-member depth explicitly for `.txt`.

The whole-disc manifest and HOG-member scans count different populations.
`disc-summary.json` counts filesystem entries whose own names end in `.txt`;
it does not expand a `.hog` filesystem entry into its internal members. The
three recursive HOG members are archive-directory spans and are not rows in
that manifest. The values 8 and 3 therefore cannot be subtracted, combined,
or described as a superset/subset relationship, and neither count establishes
asset role or placement beyond its own scope.

## 3. Confirmed facts

| # | Fact | Tracked source |
|---|---|---|
| C1 | `.txt` is not a key in `FORMAT_HANDLERS` in the asset fingerprinter — the handled suffixes are exactly `tdx, ska, skas, skm, skl, vag, lpd, par, col, vum, vpk`. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict) |
| C2 | Because `.txt` has no entry in `FORMAT_HANDLERS`, `scan_asset()` records only the aggregate extension count for `.txt` spans and applies no format-specific structural parse to them. | `tools/fingerprint_assets.py` (`scan_asset` handler-dispatch logic) |
| C3 | `analysis/formats/asset-fingerprints.json`'s `formats` object (the per-suffix aggregate-detail section) contains exactly the keys `col, lpd, par, ska, skas, skl, skm, tdx, vag, vpk, vum` — `.txt` has no per-suffix aggregate-detail entry at all, only the raw scan-count. | `analysis/formats/asset-fingerprints.json` (`formats` object keys) |
| C4 | `analysis/formats/hog-validation.json`'s `entry_extensions` object (the top-level-HOG member census) has no `.txt` key, i.e. the observed top-level-HOG member count for `.txt` is exactly 0. | `analysis/formats/hog-validation.json` (`entry_extensions` object keys) |
| C5 | `analysis/formats/FRONTEND-TOPOLOGY.md` explicitly names `.txt` as one of the thirteen "approved public suffixes" tracked by `tools/measure_frontend_hog_topology.py`, and that tool's source maps `.txt` to the fixed category label `"text"` in its `APPROVED_EXTENSION_CATEGORIES` dictionary. This is a declared measurement-vocabulary fact only — no result JSON from a run of that tool is checked into the repository, so no actual `.txt` topology count/bucket value is available from it. | `analysis/formats/FRONTEND-TOPOLOGY.md` (approved-suffix list); `tools/measure_frontend_hog_topology.py` (`APPROVED_EXTENSION_CATEGORIES[".txt"] == "text"`) |
| C6 | No published grammar document among `analysis/formats/*.md` (`ASSET-RECON.md`, `COL.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md`) other than `FRONTEND-TOPOLOGY.md`'s vocabulary listing mentions the `.txt` suffix. | `analysis/formats/*.md` (absence confirmed by direct search) |
| C7 | `analysis/evidence/ledger.jsonl` contains no entry (confirmed or rejected) making a structural or semantic claim about the `.txt` game-asset suffix. | `analysis/evidence/ledger.jsonl` (direct search for a `.txt` format-family claim) |
| C8 | No header or source file under `native/include/omega/` or `native/src/`, and no rule in `CMakeLists.txt`, defines a decoder/descriptor or registers a build/test target for the literal `.txt` suffix. The only tracked source files with "text" in their name (`par_text_envelope_decoder.*`, `skas_text_envelope_decoder.*`, and their test files) decode the `.par` and `.skas` suffix families, not files suffixed `.txt`. | `native/include/omega/**/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` (direct search) |

## 4. Aggregate-only facts

- **Recursive-in-HOG scan count:** 3 asset spans with extension `.txt` were observed across the fingerprinter's full recursive HOG walk (`analysis/formats/asset-fingerprints.json`, `scan.extensions[".txt"]`).
- **Top-level-HOG member count:** 0 depth-0 HOG-member entries carry the `.txt` suffix (`analysis/formats/hog-validation.json`, `entry_extensions`).
- **Whole-disc filesystem-entry count:** 8 filesystem entries on the disc image carry the `.txt` suffix, per `analysis/manifests/disc-summary.json` (`extensions[".txt"]`), out of 448 total filesystem entries. This count contains no HOG-internal member rows.
- **Whole-disc size range (aggregate, computed from the tracked per-entry size field in `analysis/manifests/disc-files.jsonl`, no paths/hashes/names disclosed):** across the 8 whole-disc `.txt` filesystem entries, sizes are 44, 44, 130, 176, 780, 974, 3,528, and 5,755 bytes — a range of 44 to 5,755 bytes. This separate filesystem-entry aggregate is not a size aggregate for the 3 in-HOG members; no tracked source publishes the latter.
- The scanner's `formats` per-suffix aggregate-detail section — which for handled suffixes carries fields such as size ranges, bucket histograms, and alignment/padding observations — has no such section for `.txt`. There is therefore no tracked size-range, bucket, or alignment aggregate for the **in-HOG** `.txt` population specifically.
- `analysis/formats/FRONTEND-TOPOLOGY.md` states that its measurement tool tracks `.txt` under the category `"text"` alongside fixed member-size buckets (seven fixed buckets, overall and per category) and same-basename sibling-pair counts against the other twelve approved suffixes — but because no result JSON from an actual run of `tools/measure_frontend_hog_topology.py` is checked into the tracked tree, none of those bucket/pair values are available as an aggregate fact for `.txt` here; only the tool's frozen vocabulary (that `.txt` is tracked and categorized as `"text"`) is confirmed.

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

- The only tracked-code touchpoint for `.txt` in the asset fingerprinter is the generic scan-count path in `tools/fingerprint_assets.py`'s `scan_asset()` function, which increments `scan.extensions[".txt"]` for every span whose suffix is `.txt` (case-insensitively) — the same generic counting path every unhandled suffix goes through. `.txt` is conspicuously absent from `FORMAT_HANDLERS`, which is the dict that gates all suffix-specific structural handlers (`tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`).
- A second tracked tool, `tools/measure_frontend_hog_topology.py`, declares `.txt` as one of its thirteen approved, categorized suffixes (category `"text"`) — but this is a container-topology measurement over member *suffix and size*, explicitly documented as never inspecting or interpreting member content, and no result JSON from a run of it is checked into the repository. This does not raise the classification above `aggregate_scanner_only`: it is a second aggregate-counting touchpoint, not a structural or content-level decoder.
- There is **no native decoder**: no header in `native/include/omega/retail/` or `native/include/omega/asset/`, no source file in `native/src/**`, and no `CMakeLists.txt` target or test registration references the `.txt` suffix or a `.txt`-flavored type name. The tracked `par_text_envelope_decoder` and `skas_text_envelope_decoder` sources/tests decode the `.par` and `.skas` suffix families respectively (files whose payload happens to be text-structured) and must not be conflated with a `.txt`-suffix decoder.
- There is **no descriptor**, passive or otherwise, for the literal `.txt` suffix in any tracked source.
- **Adversarial/resource-boundary test gap:** since no decoder or descriptor exists for `.txt`, there is by definition no adversarial or resource-boundary test coverage for it anywhere in the tracked test tree. This is a total gap, not a partial one.

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
