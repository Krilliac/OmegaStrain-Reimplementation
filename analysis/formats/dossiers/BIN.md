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
| Whole-disc file histogram (all disc files, HOG or not) | 22 | `analysis/manifests/disc-summary.json` (`extensions[".bin"]`) |

The recursive-in-HOG figure equalling the top-level-HOG figure (12 = 12) is
consistent with all observed `.bin` HOG members living at archive depth 0,
i.e. none of them were found nested inside a child HOG during the scanner's
recursive walk — but this dossier does not assert that as a separate
"Confirmed" fact beyond the two source counts themselves, since no source
states it as an explicit invariant.

The whole-disc count (22) is larger than the in-HOG count (12) because the
whole-disc histogram in `analysis/manifests/disc-summary.json` /
`analysis/manifests/disc-files.jsonl` counts every `.bin`-suffixed file on
the disc image, including files that live outside any HOG container (e.g.
platform/boot-loader-adjacent files at the disc root, which are out of scope
for this game-asset dossier per the task's system-file carve-out). The
in-HOG counts (12) are the game-asset-relevant subset.

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

- **Recursive-in-HOG scan count:** 12 asset spans with extension `.bin` were observed across the fingerprinter's full recursive HOG walk (`analysis/formats/asset-fingerprints.json`, `scan.extensions[".bin"]`). This walk scanned 53,281 total asset spans across two nesting depths (0 and 1, plus 5 spans at depth −1 per the scanner's depth accounting) — `.bin` is a minor fraction of the total.
- **Top-level-HOG member count:** 12 depth-0 HOG-member entries carry the `.bin` suffix, out of 32,351 total top-level entries in `analysis/formats/hog-validation.json` (`entry_extensions[".bin"]` for the 12; `total_entries` for the 32,351).
- **Whole-disc count:** 22 files on the disc image (HOG-internal and non-HOG combined) carry the `.bin` suffix, per `analysis/manifests/disc-summary.json` (`extensions[".bin"]`).
- **Whole-disc size range (aggregate, computed from the tracked per-entry size field in `analysis/manifests/disc-files.jsonl`, no paths/hashes/names disclosed):** across the 22 whole-disc `.bin` entries, sizes range from 6,064 bytes to 658,432 bytes, with most entries clustered in the ~16 KB–37 KB band and one outlier at 131,072 bytes (two occurrences) and one outlier at 658,432 bytes. This is a raw byte-size aggregate only — no semantic interpretation of what those size bands represent is offered.
- No compression-magic hits were recorded anywhere in the scan (`standard_compression_magic_hits: 0` over 46,604 spans checked) — this is a global scanner statistic, not `.bin`-specific, but it means no tracked evidence suggests any `.bin` span (or any other span) opens with a recognized compression magic.
- The scanner's `formats` per-suffix aggregate-detail section — which for handled suffixes carries fields such as size ranges, bucket histograms, and alignment/padding observations — has no such section for `.bin`. There is therefore no tracked size-range, bucket, or alignment aggregate for the **in-HOG** `.bin` population specifically (only the raw count exists for that population); the size-range aggregate above is drawn from the separate whole-disc manifest, not from the fingerprinter's per-suffix detail.

## 5. Hypotheses (explicitly labeled — none confirmed)

- **H1 — Heterogeneous bucket hypothesis.** The `.bin` suffix may aggregate multiple unrelated binary blob types (e.g. small lookup/config tables of one kind mixed with larger data blobs of another), given the wide size spread in the whole-disc aggregate (6,064 to 658,432 bytes) and the small population (12 in-HOG). *Confirming/refuting observation (privacy-safe):* extend `tools/fingerprint_assets.py` with a passive header-byte scanner for `.bin` (recording the first N bytes, size, and a coarse magic/entropy signature per span, aggregated only — no payload export) and check whether the 12 in-HOG spans cluster into distinguishable sub-populations by header signature and size band.
- **H2 — System/engine-support hypothesis.** Some or all of the whole-disc `.bin` population (22, a superset of the in-HOG 12) may be platform/engine-support files rather than game assets, given that whole-disc `.bin` count exceeds the in-HOG count by 10. *Confirming/refuting observation:* cross-tabulate the whole-disc `.bin` entries against HOG-membership (already partially answerable from `analysis/manifests/disc-files.jsonl` plus `analysis/formats/hog-validation.json` without exposing individual paths) to report an aggregate count of "`.bin` files inside a tracked top-level HOG" vs. "`.bin` files elsewhere on disc," refining the 12-vs-22 split already known into a stated, cited difference rather than an inferred one.
- **H3 — No shared grammar hypothesis (null hypothesis).** It is equally possible the 12 in-HOG `.bin` spans share no common structure at all and are simply files whose original authors used the generic `.bin` suffix for unrelated single-purpose data. *Confirming/refuting observation:* the same passive header/size aggregate proposed in H1 — if it shows no cluster structure (uniformly random-looking headers, no shared magic, no size quantization), that is evidence for H3 over H1.

## 6. Missing observations

- **No per-suffix structural aggregate for `.bin`.** Unlike the eleven handled suffixes, `analysis/formats/asset-fingerprints.json`'s `formats` object carries no size-range, bucket-count, or alignment aggregate for `.bin`. *Privacy-safe collection:* add a `fingerprint_bin` aggregate-only handler to `tools/fingerprint_assets.py` (mirroring the existing per-suffix handlers) that records size distribution, first-bytes signature histogram, and alignment/padding observations across all owned-corpus `.bin` spans, with no payload or per-file export — then re-run the existing fingerprinting pass.
- **No ledger entry.** `analysis/evidence/ledger.jsonl` has never had a claim proposed or checked for `.bin`. *Privacy-safe collection:* once an aggregate-only handler (above) exists and has been run, file a ledger claim describing only the aggregate statistical facts it produces (counts, size bands, signature-cluster counts) — explicitly not any inferred semantic role — and mark it `confirmed` only for the mechanical aggregate, not for any interpretation.
- **No native decoder, descriptor, or CMake/test registration.** There is no header, source file, or build/test target anywhere in `native/` or `CMakeLists.txt` that even names `.bin`. *Privacy-safe collection:* this is a straightforward absence, not something to "collect" — it simply means Codex work here starts from zero (see Section 8) and any first decoder attempt must begin from the aggregate-only header/size profiling in Section 6's first bullet before any structural or semantic claim is attempted.
- **No published grammar doc.** No `analysis/formats/*.md` document exists for `.bin`. *Privacy-safe collection:* a `BIN.md` grammar doc should only be authored once the aggregate-only handler above has produced enough structural signal (e.g. a shared magic word or consistent header layout across the 12 in-HOG spans) to state a Confirmed grammar constant — until then, no such doc should be written, to avoid fabricating structure that isn't there.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`.**

- The only tracked-code touchpoint for `.bin` is the generic scan-count path in `tools/fingerprint_assets.py`'s `scan_asset()` function, which increments `scan.extensions[".bin"]` for every span whose suffix is `.bin` (case-insensitively) — the same generic counting path every unhandled suffix goes through. `.bin` is conspicuously absent from `FORMAT_HANDLERS` (`tools/fingerprint_assets.py`, ~line 499), which is the dict that gates all suffix-specific structural handlers (`tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`).
- There is **no native decoder**: no header in `native/include/omega/retail/` or `native/include/omega/asset/`, no source file in `native/src/**`, and no `CMakeLists.txt` target or test registration references `.bin` or a `.bin`-flavored type name.
- There is **no descriptor**, passive or otherwise: the "passive descriptor" pattern used elsewhere in this codebase (e.g. the VUM passive render-payload inspector cited in the ledger) has no `.bin` counterpart in any tracked source.
- **Adversarial/resource-boundary test gap:** since no decoder or descriptor exists, there is by definition no adversarial or resource-boundary test coverage for `.bin` anywhere in the tracked test tree. This is a total gap, not a partial one.

## 8. Codex work order (ranked, privacy-safe)

1. **Highest priority — build an aggregate-only `.bin` structural profiler.** Add a `fingerprint_bin` handler to `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` that, for every `.bin` span in the owned corpus, records (aggregate-only, no payload/path export): total size histogram, first-N-bytes signature histogram (e.g. first 4/8/16 bytes as hex, counted not stored per-file), and byte-alignment/padding observations relative to the enclosing HOG's directory-entry granularity — mirroring the style already used for `col`/`vum`/etc. Re-run the fingerprinter and refresh `analysis/formats/asset-fingerprints.json`. This is the prerequisite for every later step.
2. **Cross-tabulate whole-disc vs. in-HOG membership.** Using only `analysis/manifests/disc-files.jsonl` and `analysis/formats/hog-validation.json` (both already tracked, both aggregate-safe if reported as counts), produce and record a single aggregate number: how many of the 22 whole-disc `.bin` files fall inside a tracked top-level HOG vs. outside all HOGs. This directly resolves H2 without exposing any path or name.
3. **If step 1 shows a shared magic/header signature across a majority of the 12 in-HOG spans:** propose a ledger claim (E-#### under `analysis/evidence/ledger.jsonl`) stating only the mechanical aggregate fact (e.g. "N of 12 `.bin` spans share first-4-byte signature X; size range Y–Z"), evidenced by the new fingerprinter output — no semantic role, no field names, no invented purpose.
4. **If and only if step 3 produces a Confirmed structural grammar constant:** author `analysis/formats/BIN.md` following the house style of the existing per-format docs (`TDX.md`, `COL.md`, etc.), scoped strictly to what the aggregate evidence supports; keep any remaining ambiguity explicitly labeled Hypothesis or UNKNOWN rather than filled in by inference.
5. **Do not** attempt a native decoder or CMake/test registration before steps 1–4 land — writing a decoder ahead of structural evidence would be exactly the "plausible invented decoder" regression this dossier is required to avoid.
