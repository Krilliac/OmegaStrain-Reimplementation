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

All items below are explicitly labeled hypotheses. None are asserted as
fact. Each lists the privacy-safe, aggregate-only observation that would
confirm or refute it without violating clean-room rules.

- **H-1: `.bon` denotes a bone/skeleton-adjacent asset family (name-similarity guess only).**
  The suffix visually resembles "bone" and the codebase already has
  bone/skeleton container formats (`.ska`, `.skl`, `.skas`, `.skm`,
  `BONENOSCALE` magic). This is a naming-pattern guess, not evidence — the
  tracked sources establish no connection between `.bon` and any bone/skeleton
  decoder.
  *Confirms/refutes:* Add a structural handler for `.bon` to
  `tools/fingerprint_assets.py` that inspects only the first N bytes of each
  span for a magic/tag pattern and emits an aggregate hit-count (e.g.
  "N of 156 `.bon` spans begin with tag X"), mirroring the existing
  `BONENOSCALE`-style checks. A nonzero aggregate hit rate for a
  skeleton-related magic would support H-1; an aggregate hit rate of 0, or a
  distinct/unrelated magic distribution, would refute it.

- **H-2: `.bon` members are uniformly small (config/metadata-scale) rather than large binary payloads.**
  Suggested only by its position in the suffix-frequency table relative to
  known small-file formats (analogous count magnitude to `.vpk` at 85 and
  `.skf` at 26, versus large binary families like `.tdx` at 15248). This is
  speculative pattern-matching on count alone, not a size observation.
  *Confirms/refutes:* Extend the fingerprinter to compute an aggregate size
  histogram (min/max/mean/bucketed distribution) for `.bon` spans, identical
  in shape to the aggregates already published for handled formats, and add
  it to `asset-fingerprints.json`. A tight small-size cluster would support
  H-2; a wide or large-size distribution would refute it.

- **H-3: All 156 `.bon` occurrences are single-depth (depth 0 or 1) HOG members, not present inside nested HOGs.**
  Suggested only by recursive-in-HOG count (156) exactly equaling top-level-HOG
  count (156); this equality is consistent with, but does not prove, a
  single-depth distribution.
  *Confirms/refutes:* Emit a per-extension depth breakdown (a `scan.depth`
  histogram keyed by extension rather than only globally) in
  `asset-fingerprints.json`. If `.bon`'s depth histogram shows 100% mass at
  one depth, H-3 is confirmed; any split across depths refutes it.

## 6. Missing observations

- **No header/magic-byte sample for `.bon`.** Tracked evidence contains zero
  bytes of `.bon` content — no header dump, no magic literal, no field
  layout. *Privacy-safe collection:* a structural-handler function (in the
  style of the existing `fingerprint_*` functions in
  `tools/fingerprint_assets.py`) that reads only the first 16–32 bytes of
  each `.bon` span, buckets them into an aggregate tag/magic-frequency table,
  and stores counts only (no payload bytes) — matching the existing
  `formats.*` aggregate pattern already in `asset-fingerprints.json`.

- **No size distribution for `.bon`.** The current pipeline only computes size
  aggregates inside per-extension handlers, which `.bon` lacks. *Privacy-safe
  collection:* add `.bon` to `FORMAT_HANDLERS` with a minimal handler that
  records aggregate size buckets and alignment flags (e.g.
  `span_2048_byte_aligned`), following the pattern already used for `.col`,
  `.lpd`, etc.

- **No per-extension nesting-depth breakdown.** `scan.depth` in
  `asset-fingerprints.json` is a global histogram across all extensions
  combined, so depth cannot currently be attributed to `.bon` alone.
  *Privacy-safe collection:* extend `scan_asset()` to count depth per
  extension (`scan.count(f"depth_{extension}", depth)` or equivalent),
  publishing only the resulting aggregate counts.

- **No evidence-ledger entry.** `.bon` has never been the subject of a
  confirmed or rejected claim in `analysis/evidence/ledger.jsonl`.
  *Privacy-safe collection:* once a structural handler exists and produces
  an aggregate observation (magic distribution, size distribution), file a
  ledger entry (`E-####`) citing that aggregate as the evidence, so future
  dossier revisions have a citable claim ID instead of a fresh ad hoc
  re-derivation.

- **No sibling-adjacency evidence.** Tracked sources do not record which
  other suffixes typically co-occur in the same HOG alongside `.bon` members
  (e.g. whether `.bon` files travel with `.skl`/`.ska`/`.skm` sets). *Privacy-safe
  collection:* an aggregate co-occurrence count (e.g. "of the top-level HOGs
  containing at least one `.bon` member, N also contain at least one `.skl`
  member") computed and published as archive-level counts only — no member
  names or paths.

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

Ranked, concrete, privacy-safe. Each item operates only on tracked
infrastructure/aggregation code, never on private inputs directly, and each
output must remain an aggregate count.

1. **Add a `.bon` entry to `FORMAT_HANDLERS`** in
   `tools/fingerprint_assets.py` with a minimal `fingerprint_bon()` that
   reads only the first 16–32 bytes of each span and records an aggregate
   magic/tag-frequency table (mirroring the existing `BONENOSCALE`-detection
   pattern in `fingerprint_skl`) plus a size-bucket histogram and the
   standard `span_2048_byte_aligned`-style alignment flag. Output must land
   in `asset-fingerprints.json` under a new `formats.bon` key, aggregate
   counts only.
2. **Re-run the fingerprinting scan** against the tracked/owner corpus with
   the new handler and diff the regenerated `asset-fingerprints.json`
   against the current one — confirm the `.bon` count stays at 156 (sanity
   check that the new handler doesn't change span discovery) and capture the
   new `formats.bon` aggregate block.
3. **Add a per-extension depth breakdown** to `scan_asset()` so `.bon`'s
   nesting-depth distribution (H-3) can be evaluated from an aggregate count
   rather than inferred from the coincidental 156=156 equality.
4. **File an evidence-ledger entry** (next `E-####`) once step 1–2 produce a
   citable aggregate, recording the magic/size aggregate as the new
   Confirmed or Aggregate-only fact — this converts today's Hypotheses
   section into either Confirmed facts or explicitly Rejected claims on the
   next dossier revision.
5. **Only after** 1–4 land and show a stable, structured magic/size pattern,
   consider whether a native structural-envelope decoder is warranted
   (i.e., promote from `aggregate_scanner_only` toward
   `structural_envelope_only`). Do not skip ahead to writing a native
   decoder or descriptor header on the strength of the naming hypothesis
   (H-1) alone — that would be inventing semantics the evidence does not
   yet support.
