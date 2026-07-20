# .pss — Format Dossier

## 1. Identity

`.pss` is a recurring game-asset suffix found inside the title's HOG archive
containers. At the current evidence level it is an **uncharacterized
suffix**: it is enumerated by the recursive asset scanner and by the
top-level HOG member-suffix table as a distinct extension bucket, but no
tracked source defines a header magic, field grammar, container role, or
semantic meaning for it. No stronger identity claim is supportable from
tracked evidence. (Note: other PS2-era titles are sometimes reported
elsewhere to use a `.pss`-suffixed streaming/movie container, but that is
external, untracked knowledge — it is not established by any file in this
repository and is treated strictly as an unconfirmed Hypothesis below, never
as identity.)

## 2. Occurrence evidence

| Metric | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG occurrences | 54 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".pss"]`) |
| Top-level-HOG member count | 54 | `analysis/formats/hog-validation.json` (`entry_extensions[".pss"]`) |
| Whole-disc occurrences | 0 | `analysis/manifests/disc-summary.json` (no `.pss` key in `extensions`), `analysis/manifests/disc-files.jsonl` (0 matching rows on direct grep) |

The recursive-in-HOG count and the top-level-HOG-member count are identical
(54 = 54), which is consistent with (but does not by itself prove) all
`.pss` members residing at one nesting depth inside HOG containers rather
than being split across nested HOGs. `asset-fingerprints.json` records
aggregate nesting depth only as a global histogram (`scan.depth`: depths
`-1`, `0`, `1`), not per-extension, so a per-extension depth breakdown is not
available from tracked evidence (see Missing observations, section 6).

The whole-disc manifest showing 0 `.pss` occurrences means every `.pss`
instance the tooling has recorded comes from inside a HOG container — none
appear as loose top-level disc files.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source citation |
|---|---|---|
| C-1 | `.pss` is not a key in `FORMAT_HANDLERS` in the fingerprinting tool; no structural handler exists for it | `tools/fingerprint_assets.py`, lines 499–511 (dict lists only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`) |
| C-2 | `.pss` spans are only ever routed through the generic per-extension counter (`scan.count("extensions", extension)`), the generic per-depth counter, and the generic compression-magic check; no format-specific parsing branch is reached for it | `tools/fingerprint_assets.py`, `scan_asset()`, lines 514–538 |
| C-3 | `asset-fingerprints.json`'s `formats` object (the per-format structural-aggregate section) has no `pss` key — only `col`, `lpd`, `par`, `ska`, `skas`, `skl`, `skm`, `tdx`, `vag`, `vpk`, `vum` are present | `analysis/formats/asset-fingerprints.json` (`formats` top-level key) |
| C-4 | No published grammar document exists for `.pss` among the tracked `analysis/formats/*.md` files (`ASSET-RECON.md`, `COL.md`, `FRONTEND-TOPOLOGY.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md` — none mention `.pss`) | Directory listing of `analysis/formats/*.md`; grep of all files in that directory for `pss`/`PSS` returns no matches |
| C-5 | No entry for `.pss` exists in the evidence ledger | `analysis/evidence/ledger.jsonl` (grep for `pss`/`PSS` returns no matches) |
| C-6 | No native decoder, descriptor header, source file, or CMake/test registration references `.pss` or a "pss"-named asset type anywhere under `native/` | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp` (recursive grep for `pss`/`PSS` returns no matches) |
| C-7 | `CMakeLists.txt` registers no target, source file, or test naming or referencing `.pss` | `CMakeLists.txt` (grep for `pss`/`PSS` returns no matches) |
| C-8 | `hog-validation.json` records the top-level-HOG member-suffix count for `.pss` as exactly 54, with no further structural detail | `analysis/formats/hog-validation.json` (`entry_extensions[".pss"]`) |
| C-9 | `asset-fingerprints.json`'s global scan reports `standard_compression_magic_hits: 0` across `standard_compression_spans_checked: 46,604` non-`.hog` spans; `.pss` spans (54 of the total scanned spans, since `.pss` ≠ `.hog`) are included in that checked population, and no `compression_hit_extension` breakdown attributes any hit to `.pss` | `analysis/formats/asset-fingerprints.json` (`scan.standard_compression_magic_hits`, `scan.standard_compression_spans_checked`, `scan.compression_hit_extension` — key absent/empty) |

## 4. Aggregate-only facts

| # | Fact | Tracked source citation |
|---|---|---|
| A-1 | `.pss` appears in the same global extension histogram as 25 other suffixes (`.bin`, `.bnk`, `.bon`, `.col`, `.fnt`, `.gui`, `.gun`, `.hog`, `.ie`, `.lpd`, `.par`, `.prn`, `.scc`, `.ska`, `.skas`, `.skel`, `.skf`, `.skl`, `.skm`, `.so`, `.sub`, `.tdx`, `.txt`, `.vag`, `.vpk`, `.vum`); its count (54) is larger than `.bin` (12), `.fnt` (3), `.txt` (3), `.scc`/`.prn` (1 each), `.skas` (2), `.skel` (4), `.skf` (26), and `.sub` (42), but smaller than `.bnk`/`.gui` (77 each), `.ie` (79), `.vpk` (85), `.bon` (156), and every other listed suffix up through `.tdx` (15,248) and `.hog` (6,677) | `analysis/formats/asset-fingerprints.json` (`scan.extensions`) |
| A-2 | The scan that produced this count covered 53,281 total asset spans across nesting depths `-1` (5 spans), `0` (32,351 spans), `1` (20,925 spans) — but this depth histogram is global, not broken out per extension, so it cannot be attributed to `.pss` specifically beyond "some subset of these depths" | `analysis/formats/asset-fingerprints.json` (`scan.asset_spans_scanned`, `scan.depth`) |
| A-3 | The dataset's declared scope is "aggregate structural fingerprints only; no proprietary payloads exported" (schema_version 3) — i.e., the entire `.pss` count of 54 is itself an aggregate figure, not a per-file listing, consistent with clean-room constraints | `analysis/formats/asset-fingerprints.json` (`scope`, `schema_version`) |
| A-4 | No standard-compression magic hit is attributed in aggregate to `.pss` specifically; the global `standard_compression_magic_hits` count is 0 across 46,604 checked spans, and no `compression_hit_extension` breakdown names `.pss` (same global fact as C-9, restated here as an aggregate rather than a per-format confirmed fact) | `analysis/formats/asset-fingerprints.json` (`scan.standard_compression_magic_hits`, `scan.standard_compression_spans_checked`) |
| A-5 | One top-level HOG container in the tracked validation data (a media-region archive whose recorded `entry_count` is 96) is the only top-level archive in that same tracked table with an entry count in the same order of magnitude as the 54-member `.pss` bucket; no tracked source states that this archive's entries are `.pss` members or attributes any of the 54 occurrences to it — the archive's `entry_count` field and the `.pss` suffix count are two independent aggregate numbers from the same JSON file, and no per-member breakdown exists to connect them | `analysis/formats/hog-validation.json` (`archives[].entry_count`, `entry_extensions[".pss"]`) |

No size-range, alignment, or byte-level aggregate specific to `.pss` exists in
tracked evidence — the fingerprinting tool only computes such aggregates
(e.g. `span_2048_byte_aligned`) inside format-specific handler functions, and
`.pss` has none (see C-1, C-2).

## 5. Hypotheses

All items below are explicitly labeled hypotheses. None are asserted as
fact. Each lists the privacy-safe, aggregate-only observation that would
confirm or refute it without violating clean-room rules.

- **H-1: `.pss` is a streaming/media container rather than a static-geometry or metadata asset (naming-pattern guess only, not evidence).**
  This is suggested only by the bare suffix string; the tracked corpus
  contains no header dump, magic literal, or field layout for `.pss`, and no
  tracked source connects it to any playback, video, or audio subsystem.
  This hypothesis must not be treated as identity (see section 1).
  *Confirms/refutes:* Add a structural handler for `.pss` to
  `tools/fingerprint_assets.py` that inspects only the first N bytes of each
  span for a magic/tag pattern and emits an aggregate hit-count (e.g. "N of
  54 `.pss` spans begin with tag X"). A magic pattern matching a documented,
  publicly specified streaming-container signature would support H-1; an
  aggregate hit rate of 0, or a distinct/unrelated magic distribution, would
  refute it. No payload bytes would be published — only a tag/frequency
  count.

- **H-2: `.pss` members are single-depth (depth 0 or 1) HOG members, not present inside nested HOGs.**
  Suggested only by the recursive-in-HOG count (54) exactly equaling the
  top-level-HOG-member count (54); this equality is consistent with, but does
  not prove, a single-depth distribution.
  *Confirms/refutes:* Emit a per-extension depth breakdown (a `scan.depth`
  histogram keyed by extension rather than only globally) in
  `asset-fingerprints.json`. If `.pss`'s depth histogram shows 100% mass at
  one depth, H-2 is confirmed; any split across depths refutes it.

- **H-3: `.pss` members are large binary payloads rather than small metadata records.**
  Suggested only by position in the count table relative to other families —
  not a size observation, since no size aggregate for `.pss` exists (see §4
  closing note). This is speculative pattern-matching on suffix alone.
  *Confirms/refutes:* Extend the fingerprinter to compute an aggregate size
  histogram (min/max/mean/bucketed distribution) for `.pss` spans, identical
  in shape to the aggregates already published for handled formats, and add
  it to `asset-fingerprints.json`. A wide/large-size distribution would
  support H-3; a tight small-size cluster would refute it.

## 6. Missing observations

- **No header/magic-byte sample for `.pss`.** Tracked evidence contains zero
  bytes of `.pss` content — no header dump, no magic literal, no field
  layout. *Privacy-safe collection:* a structural-handler function (in the
  style of the existing `fingerprint_*` functions in
  `tools/fingerprint_assets.py`) that reads only the first 16–32 bytes of
  each `.pss` span, buckets them into an aggregate tag/magic-frequency table,
  and stores counts only (no payload bytes) — matching the existing
  `formats.*` aggregate pattern already in `asset-fingerprints.json`.

- **No size distribution for `.pss`.** The current pipeline only computes
  size aggregates inside per-extension handlers, which `.pss` lacks.
  *Privacy-safe collection:* add `.pss` to `FORMAT_HANDLERS` with a minimal
  handler that records aggregate size buckets and alignment flags (e.g.
  `span_2048_byte_aligned`), following the pattern already used for `.col`,
  `.lpd`, etc.

- **No per-extension nesting-depth breakdown.** `scan.depth` in
  `asset-fingerprints.json` is a global histogram across all extensions
  combined, so depth cannot currently be attributed to `.pss` alone.
  *Privacy-safe collection:* extend `scan_asset()` to count depth per
  extension (`scan.count(f"depth_{extension}", depth)` or equivalent),
  publishing only the resulting aggregate counts.

- **No evidence-ledger entry.** `.pss` has never been the subject of a
  confirmed or rejected claim in `analysis/evidence/ledger.jsonl`.
  *Privacy-safe collection:* once a structural handler exists and produces an
  aggregate observation (magic distribution, size distribution), file a
  ledger entry (`E-####`) citing that aggregate as the evidence, so future
  dossier revisions have a citable claim ID instead of a fresh ad hoc
  re-derivation.

- **No archive-to-member-suffix mapping.** `hog-validation.json` records
  `entry_count` per top-level archive and a global `entry_extensions` suffix
  histogram, but no tracked source publishes a per-archive breakdown of
  which suffixes populate which archive — so A-5's numeric proximity between
  one archive's `entry_count` and the `.pss` total cannot be verified or
  ruled out as anything more than coincidence. *Privacy-safe collection:* add
  a per-archive suffix histogram (archive generic label → suffix → count) to
  `hog-validation.json`, aggregate-only, no member names.

- **No sibling-adjacency evidence.** Tracked sources do not record which
  other suffixes typically co-occur in the same HOG alongside `.pss` members.
  *Privacy-safe collection:* an aggregate co-occurrence count (e.g. "of the
  top-level HOGs containing at least one `.pss` member, N also contain at
  least one `.vag` member") computed and published as archive-level counts
  only — no member names or paths.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

`.pss` spans are visited by the generic scanning loop in
`tools/fingerprint_assets.py::scan_asset()` (lines 514–538) — every asset
span, regardless of extension, passes through the top-level-count,
per-depth-count, and compression-magic checks — but `.pss` is absent from
`FORMAT_HANDLERS` (lines 499–511), so no format-specific aggregate (header
layout, magic, size distribution, alignment) is ever computed for it. It
appears solely as a generic suffix count in `scan.extensions` and the
top-level HOG member-suffix table (`hog-validation.json`).

There is no native decoder, descriptor, or struct for `.pss` anywhere under
`native/include/omega/retail/`, `native/include/omega/asset/`, or
`native/src/` (a recursive grep for `pss`/`PSS` across those trees returns no
matches). `CMakeLists.txt` registers no `.pss`-related source or test
target.

There is consequently no adversarial or resource-boundary test to gap-check
— test coverage is a null set for a format with no decoder. This is not a
regression or an oversight to flag against existing coverage; it simply
means `.pss` has not yet entered the decoder pipeline at all.

## 8. Codex work order

Ranked, concrete, privacy-safe. Each item operates only on tracked
infrastructure/aggregation code, never on private inputs directly, and each
output must remain an aggregate count.

1. **Highest priority — add a `.pss` entry to `FORMAT_HANDLERS`** in
   `tools/fingerprint_assets.py` with a minimal `fingerprint_pss()` that reads
   only the first 16–32 bytes of each span and records an aggregate
   magic/tag-frequency table plus a size-bucket histogram and the standard
   `span_2048_byte_aligned`-style alignment flag. Output must land in
   `asset-fingerprints.json` under a new `formats.pss` key, aggregate counts
   only. This single step directly produces the evidence needed to evaluate
   H-1 and H-3 and closes the two highest-value Missing-observation gaps.
2. **Re-run the fingerprinting scan** against the tracked/owner corpus with
   the new handler and diff the regenerated `asset-fingerprints.json` against
   the current one — confirm the `.pss` count stays at 54 (sanity check that
   the new handler doesn't change span discovery) and capture the new
   `formats.pss` aggregate block.
3. **Add a per-extension depth breakdown** to `scan_asset()` so `.pss`'s
   nesting-depth distribution (H-2) can be evaluated from an aggregate count
   rather than inferred from the coincidental 54=54 equality.
4. **Add a per-archive suffix histogram** to `hog-validation.json` (archive
   generic label → suffix → count, no member names) so the A-5 numeric
   proximity observation can be verified or ruled out as coincidence rather
   than left as an unresolved aggregate curiosity.
5. **File an evidence-ledger entry** (next `E-####`) once steps 1–2 produce a
   citable aggregate, recording the magic/size aggregate as the new
   Confirmed or Aggregate-only fact — this converts today's Hypotheses
   section into either Confirmed facts or explicitly Rejected claims on the
   next dossier revision.
6. **Only after** 1–5 land and show a stable, structured magic/size pattern,
   consider whether a native structural-envelope decoder is warranted (i.e.,
   promote from `aggregate_scanner_only` toward `structural_envelope_only`).
   Do not skip ahead to writing a native decoder or descriptor header on the
   strength of the naming hypothesis (H-1) alone — that would be inventing
   semantics the evidence does not yet support.
