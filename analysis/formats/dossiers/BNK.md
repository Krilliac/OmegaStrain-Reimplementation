# .bnk Format Dossier

## 1. Identity

`.bnk` is a game-asset suffix that occurs exclusively as a member inside `.HOG`
archives in the tracked Syphon Filter: The Omega Strain corpus. At the current
evidence level it is an **unidentified container/data suffix**: no tracked
grammar document, native decoder, or ledger entry establishes its internal
structure, byte order, magic, or purpose. No semantic label (audio bank,
script bank, etc.) is asserted here — that would be an invented semantic and
is out of scope per the clean-room rules.

## 2. Occurrence evidence

| Metric | Count | Tracked source |
|---|---|---|
| Recursive occurrences inside HOG archives (all depths) | 77 | `analysis/formats/asset-fingerprints.json` -> `scan.extensions[".bnk"]` |
| Top-level HOG member-suffix count | 77 | `analysis/formats/hog-validation.json` -> `entry_extensions[".bnk"]` |
| Whole-disc occurrences (outside any HOG) | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.bnk` rows; grep of the JSONL returns zero matches) |

The recursive-in-HOG figure and the top-level-HOG figure are identical (77 =
77), which is consistent with all `.bnk` members living at the top level of
their containing archives with none reached only through nested-HOG descent,
but this equality is an arithmetic observation, not a confirmed structural
claim (no per-archive nesting-depth breakdown for `.bnk` specifically is
published in either tracked file).

## 3. Confirmed facts

Mechanically citable facts about `.bnk` in this tracked repository:

| # | Fact | Tracked source |
|---|---|---|
| C1 | `.bnk` appears in the HOG top-level `entry_extensions` aggregate with value `77`. | `analysis/formats/hog-validation.json` |
| C2 | `.bnk` appears in the whole-corpus `scan.extensions` aggregate (recursive scan across all HOG nesting depths, `depth` in `{-1,0,1}`) with value `77`. | `analysis/formats/asset-fingerprints.json` |
| C3 | `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict — the registry of extensions that receive structural parsing during the fingerprint scan — contains exactly `.tdx, .ska, .skas, .skm, .skl, .vag, .lpd, .par, .col, .vum, .vpk`. `.bnk` is not a key in this dict. | `tools/fingerprint_assets.py` (lines ~499–511, `FORMAT_HANDLERS = {...}`) |
| C4 | Because `.bnk` has no entry in `FORMAT_HANDLERS`, `scan_asset()` never calls a per-file handler for `.bnk` spans; only the extension count and the generic standard-compression-magic check (`compression_magic(head)` against the first bytes) are recorded for it, alongside every other unhandled suffix. | `tools/fingerprint_assets.py` (`scan_asset` function) |
| C5 | `analysis/formats/asset-fingerprints.json`'s `formats` object (the per-format structural-aggregate section, populated only for handled extensions) contains exactly the keys `col, lpd, par, ska, skas, skl, skm, tdx, vag, vpk`. `bnk` is absent. | `analysis/formats/asset-fingerprints.json` -> `formats` |
| C6 | No published grammar document (`analysis/formats/*.md`: `ASSET-RECON.md`, `COL.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md`) mentions `.bnk` (case-insensitive grep of all files returns zero matches). | `analysis/formats/*.md` (absence check) |
| C7 | No entry in `analysis/evidence/ledger.jsonl` references `.bnk` or `bnk` (case-insensitive grep returns zero matches). | `analysis/evidence/ledger.jsonl` (absence check) |
| C8 | No file under `native/include/omega/**`, `native/src/**`, or `CMakeLists.txt` references `bnk` in any casing (recursive case-insensitive grep across those trees returns zero matches). | `native/include/`, `native/src/`, `CMakeLists.txt` (absence check) |
| C9 | `.bnk` does not appear in `analysis/manifests/disc-summary.json` or `analysis/manifests/disc-files.jsonl`, i.e. it is never a whole-disc top-level file — it exists only as an archive member. | `analysis/manifests/disc-summary.json`, `analysis/manifests/disc-files.jsonl` |

## 4. Aggregate-only facts

- The two published occurrence counts for `.bnk` (top-level-HOG `entry_extensions` and whole-corpus recursive `scan.extensions`) are both `77`, drawn from a corpus-wide HOG validation pass covering `archive_count = 273`, `total_entries = 32351`, `valid_count = 273`, `error_count = 0` (all 273 discovered HOG archives parsed as structurally valid; zero HOG-level errors). Source: `analysis/formats/hog-validation.json`.
- The whole-corpus recursive scan (`asset-fingerprints.json` -> `scan`) covers `asset_spans_scanned = 53281` spans across nesting `depth` buckets `{-1: 5, 0: 32351, 1: 20925}`; `.bnk`'s count of 77 is a sub-count of that total and is not broken down further by depth in the published aggregate.
- No size range, alignment, magic-byte, or padding aggregate exists for `.bnk` anywhere in tracked output, because it has no entry in the `formats` structural-aggregate object (see C5) and no handler ever executes `stats.add`/`stats.observe`/`stats.count` calls against `.bnk` spans (see C3, C4). The only aggregate signal that could apply to `.bnk` spans is the generic `standard_compression_magic_hits` / `compression_hit_extension` counters in `scan`, but the published aggregate does not break those out per extension in a form that isolates `.bnk` — so even a coincidental compression-magic hit rate for `.bnk` specifically is not observable from tracked output.
- `asset-fingerprints.json` declares `"scope": "aggregate structural fingerprints only; no proprietary payloads exported"` and `"schema_version": 3`, confirming the file is designed to be aggregate-only and that this dossier's inputs are within that declared scope.

## 5. Hypotheses

No hypothesis about `.bnk`'s internal structure, contents, or role is recorded
in tracked evidence. Per the clean-room rules, no semantic hypothesis (e.g.
about naming conventions or file relationships) is asserted here without a
privacy-safe observation path. The only hypothesis that can be stated without
inventing semantics is a purely structural one:

| # | Hypothesis | Privacy-safe observation that would confirm/refute it |
|---|---|---|
| H1 | All 77 `.bnk` members are direct (depth-0-relative-to-their-own-archive) children of their containing HOG, i.e. none of the 77 occurrences requires nested-HOG descent to reach. | Extend `tools/fingerprint_assets.py` (or a derivative aggregate-only script) to emit a per-extension breakdown of `scan.extensions` by the existing `depth` bucketing (`-1/0/1`) and re-run against the owned corpus. If `.bnk`'s count is concentrated entirely in one depth bucket consistent with top-level HOG membership, and still sums to 77 top-level per `hog-validation.json`, H1 is confirmed; a split across depths that doesn't reconcile would refute it. This requires no new decoder and emits only aggregate counts, so it stays privacy-safe. |
| H2 | `.bnk` span sizes follow some fixed-alignment boundary (e.g. 16-, 2048-, or sector-byte alignment), by analogy with the alignment checks already implemented for other handled formats (see `fingerprint_vpk`'s 2048-byte check). | Add a size-only aggregate handler for `.bnk` to `FORMAT_HANDLERS` that calls only `stats.observe("span_bytes", span.size)` and a small set of `stats.add("span_N_byte_aligned")` modulus checks (mirroring the existing handler patterns for other suffixes), then re-run the fingerprint scan and publish only the resulting aggregate (min/max/bucket counts), never per-file sizes or offsets. A skewed alignment distribution would support H2; a uniform/non-aligned distribution would refute it. |

No hypothesis is offered about what `.bnk` "is" (audio, script, animation-adjacent, etc.) because no tracked evidence — grammar doc, decoder, header magic, or ledger claim — exists to anchor such a claim, and inferring one from the suffix name alone would be a fabricated semantic and a regression under these rules.

## 6. Missing observations

- **No structural/magic-byte aggregate.** `tools/fingerprint_assets.py` has no `fingerprint_bnk` handler, so no header-magic, size-distribution, or alignment aggregate has ever been collected for `.bnk`, unlike the eleven suffixes in `FORMAT_HANDLERS`. Producing one requires only adding a size/magic-only aggregate handler (per H1/H2 above) and re-running the existing scanner — no new private-input access beyond what the scanner already touches, and output remains aggregate-only JSON.
- **No depth/nesting breakdown for `.bnk` specifically.** `scan.extensions` gives a flat total; it is not cross-tabulated with `scan.depth` per extension. A privacy-safe extension of the existing scan (aggregate counters keyed by `(extension, depth)`) would close this gap.
- **No grammar document.** There is no `analysis/formats/BNK.md` establishing header layout, magic, or entry structure — this dossier is explicitly not a substitute for one and does not attempt to originate one.
- **No native decoder or descriptor, and consequently no test registration.** `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, and `native/src/**/*.cpp` contain zero `.bnk`-related code, and `CMakeLists.txt` registers no `.bnk`-related target or test. There is therefore no adversarial/resource-boundary test to assess a gap in — the gap is the total absence of a decoder to test.
- **No ledger claim.** No `E-####` entry in `analysis/evidence/ledger.jsonl` has ever asserted or rejected anything about `.bnk`. Any future claim about this format must be entered into the ledger with a verifiable `check` command before being treated as confirmed.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

Justification:
- `.bnk` is enumerated by the whole-corpus scanner (`tools/fingerprint_assets.py`), which is why the occurrence counts in Section 2 exist at all — this is more than `unknown`, which would apply if the format were never touched by tooling.
- However, `.bnk` has **no entry in `FORMAT_HANDLERS`** (`tools/fingerprint_assets.py`, ~line 499), so it receives no per-file structural parsing, no magic-byte check, and no size/alignment aggregate beyond the flat suffix count and the generic compression-magic probe applied uniformly to all unhandled extensions.
- There is **no native decoder or descriptor** anywhere in `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, or `native/src/**/*.cpp` (recursive case-insensitive search returns zero hits), and **no CMake target or test registration** in `CMakeLists.txt` references `.bnk`. This rules out `canonical_decoder`, `structural_envelope_only`, and `passive_descriptor_only` — all three require at least a header/source file that the evidence does not show.
- Because tooling exists and does enumerate `.bnk` (ruling out `unknown`), and because that tooling's involvement is limited to aggregate counting rather than any structural or descriptive parse, `aggregate_scanner_only` is the exact match.
- Adversarial/resource-boundary test gap: not applicable to assess in the usual sense (fuzzing an existing decoder's bounds-checking) because there is no decoder to fuzz; the gap is upstream of that — a decoder must first be built (Section 8) before an adversarial-test gap can even be defined for `.bnk`.

## 8. Codex work order

Ranked, privacy-safe next steps, each producible from the existing owned corpus without exposing per-file identifiers, hashes, offsets, or payload bytes in any published artifact:

1. **(Highest priority) Add a size/alignment-only aggregate handler for `.bnk` to `tools/fingerprint_assets.py`.** Register it in `FORMAT_HANDLERS["​.bnk"]`, mirroring the existing minimal handlers (e.g. `fingerprint_vpk`'s pattern of `stats.add("count")`, `stats.observe("span_bytes", span.size)`, and modulus-based alignment flags) but WITHOUT asserting any magic-byte interpretation unless a fixed 4-byte prefix is empirically uniform across all 77 members. Re-run the scanner against the owned corpus and publish only the resulting aggregate (counts, size min/max/bucket histogram, alignment-flag counts) into `analysis/formats/asset-fingerprints.json`. This directly closes the Section 6 "no structural aggregate" gap and is the prerequisite for every later step.
2. **Cross-tabulate `scan.extensions` by `scan.depth` for `.bnk` (and ideally all suffixes) as a scanner enhancement**, to mechanically confirm or refute H1 (Section 5) — i.e., verify that the 77 top-level-HOG count and 77 recursive count are reconciled by depth, not just by coincidental equality of totals.
3. **If step 1 finds a uniform fixed-width prefix or repeating structural pattern across all 77 members**, promote that observation to a new `analysis/formats/BNK.md` grammar document stating only the mechanically-derived constants (byte offsets/widths/uniform magic if any) — explicitly marked as derived from aggregate scanning, with no semantic name/purpose attached unless a further independently-citable source (e.g. a public format spec already vetted for clean-room compatibility) is brought in through the project's normal provenance process.
4. **Only after steps 1–3 produce a stable grammar**, consider a `structural_envelope_only` native descriptor (header-only parse, no payload interpretation) under `native/include/omega/asset/`, registered in `CMakeLists.txt`, with an adversarial/resource-boundary test (truncated span, zero-length span, oversized declared-length field) added at that time — do not skip ahead to a decoder before the grammar step is evidenced.
5. **Log every finding from steps 1–3 as new `E-####` entries in `analysis/evidence/ledger.jsonl`** with a mechanical `check` command, so future dossier revisions for `.bnk` have ledger-citable facts rather than re-deriving them from raw JSON each time.
