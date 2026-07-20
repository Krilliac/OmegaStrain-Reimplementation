# .prn family dossier

## 1. Identity

`.prn` is a HOG-member-only suffix in the owner NTSC-U corpus: it occurs exactly once as a
top-level HOG archive member and exactly once in the recursive (all-depth) asset-span scan, with
zero whole-disc occurrences. No tracked source assigns it a decoder, a grammar, a payload
interpretation, or a semantic role. At the evidence level this document can defend, `.prn` is an
**unclassified, singleton, in-scope game-asset family**: it is HOG-adjacent (so it is treated as a
game asset rather than a system file, per the task's in-scope criterion), but no tracked tool has
ever opened its bytes structurally, and no published grammar or ledger entry exists for it. This
document does not infer a role (e.g., "print", "profile", or any other name-shaped guess) — the
literal suffix text is not treated as evidence of function.

## 2. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive, inside any HOG (all nested spans, any depth) | 1 | `analysis/formats/asset-fingerprints.json` (`scan.extensions` map: `".prn": 1`) |
| Top-level, inside any HOG | 1 | `analysis/formats/hog-validation.json` (`entry_extensions` map: `".prn": 1`, out of `total_entries: 32351` across `archive_count: 273` archives, `error_count: 0`) |
| Whole-disc | 0 | `analysis/manifests/disc-summary.json` (`extensions` map: `.prn` is absent — a full-file grep of `disc-summary.json` and `disc-files.jsonl` for `prn` returns no match) |

The recursive count (1) and the top-level count (1) are consistent with a single HOG member that
is not itself further nested inside another HOG (i.e., the one occurrence is a direct member of
some top-level archive, not a member reached only via multi-level nesting) — this is an aggregate
count-equality observation, not a claim about which archive or directory holds it (no path is
cited, per the clean-room path-free rule).

No `analysis/evidence/ledger.jsonl` entry (`E-0001`..) references `.prn`; a full-file grep of the
ledger for `prn` returned no confirmed or rejected claim for this suffix.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Citation |
|---|---|---|
| 1 | `.prn` is absent from `FORMAT_HANDLERS` — the dict mapping suffixes to structural fingerprint handlers only contains `.tdx .ska .skas .skm .skl .vag .lpd .par .col .vum .vpk` | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict, ~line 499) |
| 2 | `.prn` produces no `formats["prn"]` (or `.prn`-keyed) aggregate bucket in `asset-fingerprints.json` — the top-level `formats` object's keys are exactly `col lpd par ska skas skl skm tdx vag vpk vum`, none of which is `prn` — because the scanner only opens a structural handler for extensions present in `FORMAT_HANDLERS` (row 1); the single recorded `.prn` observation exists only as a raw suffix tally in `scan.extensions`, with no header bytes, magic, or size aggregate attached | `analysis/formats/asset-fingerprints.json` (`formats` object keys; `scan.extensions` map) |
| 3 | No `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h` header, and no `native/src/**/*.cpp` file, names or references a `.prn`-suffixed decoder, descriptor, or grammar; a name/suffix grep across those trees returns no match for `prn` as a format identifier | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp` |
| 4 | `CMakeLists.txt` registers no target, source file, or test for a `.prn` decoder (no `prn`-named source is compiled or listed) | `CMakeLists.txt` |
| 5 | `analysis/evidence/ledger.jsonl` contains no confirmed or rejected entry whose `claim` or `evidence` references `.prn` (full-file search) | `analysis/evidence/ledger.jsonl` |
| 6 | No `analysis/formats/*.md` published grammar document exists for `.prn`; a suffix-scoped grep of every file in `analysis/formats/*.md` returns no grammar or structural description for this family (the only appearances of the literal string `.prn` in that directory are inside other families' dossiers/docs enumerating the full known-extension list, e.g. as a member of the alphabetical suffix list in `DAT.md`'s occurrence table, not as content about `.prn` itself) | `analysis/formats/*.md` |
| 7 | The recursive scan (`scan.extensions`) and the top-level HOG validation (`entry_extensions`) both independently report the same count of exactly `1` for `.prn`, and the whole-disc manifest (`disc-summary.json`) reports `.prn` as entirely absent (count 0, not merely low) | `analysis/formats/asset-fingerprints.json` (`scan.extensions`), `analysis/formats/hog-validation.json` (`entry_extensions`), `analysis/manifests/disc-summary.json` (`extensions`) |

## 4. Aggregate-only facts

- The scan that produced the `.prn` count is the same recursive pass documented for other
  families: `scan.asset_spans_scanned` covers 53,281 total asset spans across nesting depths `-1`
  (5 spans), `0` (32,351 spans), `1` (20,925 spans); this depth histogram is global, not broken out
  per extension, so it cannot be attributed to `.prn` specifically beyond "some subset of these
  depths contains the one `.prn` span" (`analysis/formats/asset-fingerprints.json`, `scan.depth`).
- `analysis/formats/asset-fingerprints.json`'s `scan` block reports `standard_compression_magic_hits: 0`
  across `standard_compression_spans_checked: 46,604` non-HOG asset spans (gzip/zip/bzip2/xz/7zip
  signature check). This check is applied to every non-`.hog` span the scanner visits, which would
  include the single `.prn` span; no `compression_hit_extension` entry for `.prn` is recorded, so
  the aggregate evidence is that the one `.prn` span did **not** match a standard compression magic
  — but no other magic-byte or structural fact was recorded for it, because `.prn` has no
  `FORMAT_HANDLERS` entry (§3 row 1) and so receives no size, alignment, or version-word aggregate.
- Among the full set of HOG-member suffixes enumerated in `entry_extensions`/`scan.extensions`
  (`.bin .bnk .bon .col .fnt .gui .gun .hog .ie .lpd .par .prn .pss .scc .ska .skas .skel .skf .skl
  .skm .so .sub .tdx .txt .vag .vpk .vum`), `.prn` shares its count of exactly `1` with `.scc`
  (also `1` in both `scan.extensions` and `entry_extensions`) — this is a coincidental aggregate
  count match between two otherwise-unrelated, unhandled singleton suffixes, not evidence of a
  shared format or relationship.
- `.prn`'s recursive-scan depth is not separately reported; no aggregate source attributes it to a
  specific nesting depth, region, or the `minsk` per-region example bucket (`analysis/formats/
  asset-fingerprints.json`'s `minsk.containers` object enumerates only `DATA.HOG`, `MAPTEX.HOG`, and
  `MAPVUM.HOG` container aggregates, none of which lists a `.prn` nested-extension pattern).

No aggregate source reports header magic, size, alignment, or internal structure for `.prn`,
because no tracked scanner or decoder has ever opened the one `.prn` span structurally beyond the
generic non-HOG compression-magic check noted above (see §3, rows 1–2).

## 5. Hypotheses

Each is explicitly labeled and unconfirmed. None is asserted as fact anywhere else in this
document. No hypothesis infers meaning from the suffix's literal text.

- **H1 — Singleton/incidental asset, not a recurring format class.** A count of exactly 1 (both
  recursively and at the top level, with 0 whole-disc) is far below the hundreds-to-thousands
  counts of the confirmed structural families (`.tdx` 15,248; `.vag` 8,665; `.skm` 4,219; `.col`
  7,036; per `analysis/formats/asset-fingerprints.json`'s `formats`/`scan.extensions` maps — not to
  be confused with the unrelated top-level-HOG member count of `11,166` for `.tdx` reported in
  `hog-validation.json`'s `entry_extensions` map, which counts a different scope), and is
  in the same low-count tier as other unhandled singleton suffixes (`.scc` 1, `.fnt` 3, per the same
  maps). This is suggestive of a one-off or debug/tooling artifact rather than a systematically
  used per-asset format, but a single data point cannot establish this.
  *Confirms if:* a privacy-safe re-run of the fingerprinting pass against additional owned
  discs/editions under the same tracked pipeline continues to show a count of exactly 1 (or 0) for
  `.prn`, i.e., the suffix does not scale with corpus size the way `.tdx`/`.vag`/`.skm` do.
  *Refutes if:* a broader corpus scan shows `.prn` scaling with content volume (e.g., one per
  level or per texture), which would argue for a real per-asset format instead.
- **H2 — Structurally opaque to the existing generic checks.** The one `.prn` span passed through
  the scanner's non-HOG standard-compression-magic check (§4) with no hit, meaning it is not a
  gzip/zip/bzip2/xz/7zip-wrapped payload by that specific check, but nothing else about its byte
  layout has ever been inspected.
  *Confirms if:* a bounded, aggregate-only header-byte scan (first N bytes, magic/alignment only,
  no payload reproduction) run against the single `.prn` span reports ASCII/text-ratio or
  magic-byte characteristics consistent with a specific known container shape (e.g., matching the
  header shape already published for `.par`, `.lpd`, or another handled family in this same corpus).
  *Refutes if:* the scan reports a byte pattern inconsistent with every already-published grammar
  in `analysis/formats/*.md`, i.e., `.prn` is genuinely `unknown`/sui generis within this corpus.
- **H3 — Not a compressed wrapper.** Directly supported by the `standard_compression_magic_hits: 0`
  aggregate (§4), but stated here as a hypothesis rather than confirmed fact because the check only
  covers the five listed standard signatures and does not rule out a proprietary/PS2-specific
  compression scheme.
  *Confirms if:* a broader, aggregate-only magic-byte sweep (checking for any additional
  proprietary compression signatures already documented elsewhere in this repo, if any) also
  returns no hit for the single `.prn` span.
  *Refutes if:* such a sweep finds a proprietary compression magic at the start of the span.

## 6. Missing observations

- **No structural scanner has ever been run against `.prn`.** `tools/fingerprint_assets.py`'s
  `FORMAT_HANDLERS` dict has no `.prn` entry (§3 row 1), so `asset-fingerprints.json` carries zero
  structural data for this family beyond the bare suffix tally and the generic non-HOG compression
  check. *Privacy-safe collection:* add a bounded, read-only `.prn` handler to
  `tools/fingerprint_assets.py` that reports only aggregate leading-byte histogram, size, and any
  ASCII/text-ratio observation for the one occurrence — mirroring the existing per-format handlers
  (`fingerprint_tdx`, `fingerprint_col`, etc.) — and re-run the existing fingerprinting pass,
  publishing only the aggregate output (no payload bytes).
- **No published grammar document exists.** Unlike `.pop`, `.col`, `.vum`, etc., there is no
  `analysis/formats/PRN.md` (or equivalent) describing byte layout. *Privacy-safe collection:* once
  §6 row 1's aggregate scan establishes whether the single `.prn` span has a recognizable header
  shape, promote the result to a grammar doc following the `POP.md`/`VUM.md` template, citing only
  aggregate/structural constants (a single-observation "grammar" would need to be explicitly
  flagged as n=1 and provisional).
- **No native descriptor or decoder exists**, so there is no CMake target, no test target, and no
  adversarial/resource-boundary coverage to note a gap in (§3 rows 3–4) — the gap is total, not
  partial.
- **No ledger entry (`E-####`) has ever evaluated a claim about `.prn`.** *Privacy-safe collection:*
  file a new ledger entry once §6 row 1's scan produces a citable aggregate fact, following the
  existing claim/evidence/check schema.
- **No cross-region/cross-edition comparison exists.** All counts in this dossier derive from the
  single NTSC-U owner disc represented by `disc-summary.json`/`disc-files.jsonl` and the HOG
  archives fingerprinted in `hog-validation.json`/`asset-fingerprints.json`. Whether other
  regions/editions carry zero, one, or more `.prn` occurrences is unknown, and with a base count of
  1 this family is especially sensitive to sample size. *Privacy-safe collection:* if additional
  owned discs are ever fingerprinted under the same tracked pipeline, add their `.prn` aggregate
  counts to this dossier's §2/§4 tables without introducing any new per-file detail.
- **The archive and directory context of the single occurrence is not documented in this dossier**
  (deliberately, per the path-free rule) — no tracked aggregate source (e.g., a `minsk`-style
  per-region container bucket) currently reports which archive or region's example bucket contains
  it, so even a privacy-safe "which archive family" fact is not currently available. *Privacy-safe
  collection:* if the `minsk`-style per-region example-bucket aggregation in `asset-fingerprints.json`
  is ever extended to enumerate all nested-extension patterns per container (not just the
  currently-listed `DATA.HOG`/`MAPTEX.HOG`/`MAPVUM.HOG` examples), check whether `.prn` appears in
  that expanded aggregate.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`** — and even that is understated, because the *existing*
aggregate scanner (`tools/fingerprint_assets.py`) does not currently bucket `.prn` at all (§3 rows
1–2); the only aggregate evidence available today is the bare suffix tally recorded by the
recursive scan's generic `scan.extensions` counter and the top-level HOG validator's
`entry_extensions` counter, plus the incidental fact that the one span did not match a standard
compression magic during the scanner's blanket non-HOG check. There is no `canonical_decoder`, no
`structural_envelope_only` decoder, and no `passive_descriptor_only` descriptor for `.prn` anywhere
in `native/include/omega/retail/`, `native/include/omega/asset/`, `native/src/`, or
`CMakeLists.txt` (§3 rows 3–4). Consequently there is no adversarial or resource-boundary test
suite to report a gap in — the gap is the complete absence of any decoder, not a partially-tested
one.

## 8. Codex work order

Ranked, privacy-safe, concrete next steps. None presumes a semantic role; each step either builds
tooling against the owner corpus or verifies an existing aggregate. Given the n=1 sample, every
step below should explicitly report and preserve that sample-size caveat rather than generalizing
from a single instance.

1. **Highest priority — add a bounded `.prn` structural handler to `tools/fingerprint_assets.py`**
   (mirroring the existing `fingerprint_tdx`/`fingerprint_col`/etc. handlers) that reports only:
   leading-byte histogram (e.g., first 4–16 bytes bucketed, not reproduced), size, and an
   ASCII/text-ratio aggregate for the single occurrence — all as aggregate output, never a payload
   dump. Register it in `FORMAT_HANDLERS` and re-run the existing fingerprinting pass against the
   owner corpus. Publish only the aggregate JSON, following the existing `asset-fingerprints.json`
   schema, and explicitly label the result as n=1.
2. Extend the handler (or a follow-up bounded scanner script) to run the single `.prn` span's
   leading bytes against the header shapes already published for the corpus's confirmed-grammar
   families (`analysis/formats/*.md`) and report only a pass/fail aggregate match — this directly
   confirms or refutes H2 without exposing any payload.
3. Once step 1 produces a citable aggregate (magic present/absent, size, text-ratio), file a new
   `analysis/evidence/ledger.jsonl` entry (`E-00xx`) recording the confirmed aggregate claim,
   following the existing claim/evidence/check schema.
4. Do **not** write an `analysis/formats/PRN.md` grammar document until step 1 produces a citable
   structural aggregate; a grammar doc written from a bare occurrence count would be unsupported
   speculation and a regression under the clean-room rules.
5. If additional owned discs/editions are ever added to the tracked pipeline, re-run this dossier's
   §2 occurrence-evidence table against them first, before any further tooling investment — because
   at n=1, learning whether `.prn` is corpus-wide-rare or disc-specific is higher-value than
   building a decoder for a single unexamined span.
6. Only after steps 1–5 establish a confirmed, citable structural pattern across more than one
   observation: scope and build a `structural_envelope_only` native descriptor (own bytes, bound
   sizes, reject malformed input, assign no semantics) under `native/include/omega/asset/`, with a
   corresponding CMake target and test file, following the pattern already established for
   `.vpk`'s wrapper-envelope descriptor. Do not attempt a `canonical_decoder` until the envelope
   stage is proven and reviewed.
7. Re-run this dossier's §2 occurrence-evidence table whenever `asset-fingerprints.json`,
   `hog-validation.json`, or `disc-summary.json` are regenerated, to catch drift (e.g., a future
   corpus addition changing the count away from 1/1/0).
