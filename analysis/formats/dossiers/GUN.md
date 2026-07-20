# `.gun` — Dossier

## 1. Identity

`.gun` is a suffix that occurs 624 times among files nested inside `.HOG` archives on the
tracked corpus. At the evidence level currently defensible, that is the entirety of its
identity: **an unhandled, unparsed member suffix found only inside HOG containers.** No tracked
source establishes a header magic, internal grammar, decoder, or semantic role (weapon data,
config, or otherwise — the name itself must not be treated as informative). Family classification
is **UNKNOWN**.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive, inside `.HOG` archives | 624 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".gun"]` |
| Top-level HOG member suffix | 0 | `analysis/formats/hog-validation.json` (no `.gun` key present) |
| Whole-disc (top-level disc files) | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.gun` entries) |

Because the recursive-in-HOG count is nonzero, `.gun` is an in-scope game-asset format per the
project's scoping rule, despite having zero direct top-level presence.

## 3. Confirmed facts

| # | Fact | Tracked citation |
| --- | --- | --- |
| C1 | `.gun` is counted 624 times under `scan.extensions` during the recursive asset-span scan. | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".gun"] = 624`) |
| C2 | `.gun` is **not** a key in `FORMAT_HANDLERS`, the dict mapping extensions to structural fingerprint functions (`.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk` only). | `tools/fingerprint_assets.py` (lines ~499-511, `FORMAT_HANDLERS` dict) |
| C3 | Because `.gun` has no handler, `scan_asset()` records only the generic extension tally and the generic compression-magic check for every `.gun` span; no per-format aggregate object is created for it (`.gun` is absent from the top-level `formats` object, which only holds keys for the eleven handled extensions). | `tools/fingerprint_assets.py` (`scan_asset()`, and `analysis/formats/asset-fingerprints.json` → `formats` object keys) |
| C4 | `.gun` does not appear in the published corpus-results table (which enumerates TDX, SKM, SKL, SKA, SKAS, VAG, LPD, PAR, COL, VUM, VPK, POP only) or anywhere else in `ASSET-RECON.md`. | `analysis/formats/ASSET-RECON.md` (§ "Corpus results") |
| C5 | `.gun` has zero occurrences in any published per-format grammar doc (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, `LPD.md`, `PAR.md`, `SKAS.md`, `VAG.md`, `VPK.md`, `SO.md`, `FRONTEND-TOPOLOGY.md`). | `analysis/formats/*.md` (grep, zero hits) |
| C6 | `.gun` has zero occurrences in the evidence ledger (no `E-####` entry mentions it). | `analysis/evidence/ledger.jsonl` (grep, zero hits) |
| C7 | `.gun` has zero occurrences anywhere in tracked native code, headers, or the top-level `CMakeLists.txt` — no decoder class, struct, descriptor, or build-system registration exists for it. | `native/include/**`, `native/src/**`, `CMakeLists.txt` (grep, zero hits) |
| C8 | Across the recursive scan, `standard_compression_magic_hits = 0` out of `standard_compression_spans_checked = 46,604` non-`.hog` spans — i.e., no span of any unhandled extension, `.gun` included, exhibited a recognized standard-compression magic header. | `analysis/formats/asset-fingerprints.json` (`scan.standard_compression_magic_hits`, `scan.standard_compression_spans_checked`) |

## 4. Aggregate-only facts

- The only quantitative fact the tracked aggregates establish for `.gun` specifically is the
  bare occurrence count: **624** recursive-in-HOG spans (`asset-fingerprints.json` →
  `scan.extensions[".gun"]`). No size range, alignment observation, byte-offset bucket, or
  header-byte tally is broken out per-extension for unhandled suffixes — the scanner's per-span
  compression-magic check (`standard_compression_magic_hits`) is aggregated corpus-wide (all
  46,604 checked spans, all unhandled extensions pooled together), not per-suffix, so it cannot
  be attributed to `.gun` alone beyond noting the corpus-wide hit count was zero.
- No size, count, or alignment statistic in `asset-fingerprints.json`, `hog-validation.json`, or
  any published `*.md` is broken out for `.gun`. Any number beyond the 624 count above would be
  an invented aggregate, not a tracked one.

## 5. Hypotheses

All statements below are explicitly unconfirmed hypotheses, not facts. Each lists the
privacy-safe, aggregate-only observation that would confirm or refute it without exposing any
private payload, path, or per-file identifier.

- **H1 — `.gun` is a fixed-header binary format like the handled asset suffixes (TDX/VAG/COL/etc.).**
  Confirm/refute: extend `tools/fingerprint_assets.py` with a passive `.gun` fingerprint function
  that reads only the first N bytes of each of the 624 spans and reports aggregate stats (byte-0..3
  magic-value histogram, size-modulo-alignment histogram, min/max/mean size). If a single
  consistent magic or size formula emerges across all 624 spans, H1 is confirmed at
  Aggregate-only tier; if bytes are inconsistent/text-like, H1 is refuted.
- **H2 — `.gun` files are exclusively HOG-nested (never appear as top-level disc files or top-level HOG members) because they are always packed inside a per-cell/per-level container.**
  Confirm/refute: this is already partially supported by the occurrence evidence (top-level-HOG=0,
  whole-disc=0, recursive-in-HOG=624) but the *reason* is unconfirmed. A depth histogram already
  collected by the scanner (`scan.depth`) could be cross-tabulated with extension in a future
  scanner revision to show `.gun`'s depth distribows without extracting payloads.
- **H3 — the suffix name reflects weapon-related content.**
  This is a plausibility inference from the string "gun" and MUST NOT be treated as established.
  No tracked source parses, decodes, or names the internal structure of any `.gun` file. Confirming
  or refuting this would require a semantic decoder backed by cross-referenced executable-side
  evidence (e.g., a MIPS string/table cross-reference showing the runtime opening `.gun` members in
  a weapon-loading code path), which does not currently exist in tracked evidence. Until such a
  decoder and citation exist, this hypothesis stays unconfirmed and should not appear in any
  Confirmed section.

## 6. Missing observations

- No per-extension structural fingerprint function exists for `.gun` in `tools/fingerprint_assets.py`,
  so no header-magic, alignment, or size-formula aggregate has ever been collected for it. Privacy-safe
  collection: add a `fingerprint_gun()` handler following the pattern of the eleven existing handlers
  (`fingerprint_tdx`, `fingerprint_col`, etc.) that reads only structural header fields and emits
  counts/aggregates into a new `formats[".gun"]` bucket — never payload bytes.
- No published grammar doc (`analysis/formats/GUN.md` as a *format* doc, distinct from this dossier)
  exists. Privacy-safe collection: once a structural handler produces stable aggregate results across
  all 624 spans, promote the confirmed structural facts into a `GUN.md` grammar doc alongside
  `TDX.md`/`COL.md`/etc., following the existing "Confirmed structural result" table format in
  `ASSET-RECON.md`.
- No evidence-ledger entry (`E-####`) exists for `.gun`. Privacy-safe collection: once a structural
  claim is mechanically verified against the aggregate scan output, add a ledger entry citing the
  specific scanner run and aggregate field, per the ledger's existing confirmed/rejected-claim format.
- No depth or containing-archive-name aggregate is currently cross-tabulated by extension. Privacy-safe
  collection: extend the scanner to bucket `scan.extensions` counts by `scan.depth` (already collected
  separately) so a depth-vs-extension aggregate table can be produced without any per-file listing.
- No cross-reference against retail executable-side string/table evidence exists for `.gun` in any
  tracked file. Any such cross-reference, if pursued, must remain an offline research technique
  producing only aggregate/structural citations (per the constraint already stated in `ASSET-RECON.md`
  §"Scope and method") — no retail instructions or executable script cells may enter tracked docs or
  the runtime.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- `.gun` spans are visited by the generic recursive asset scanner (`tools/fingerprint_assets.py`,
  `scan_asset()`) and contribute to the generic `scan.extensions` tally and the generic
  corpus-wide compression-magic check. This is the full extent of tooling that touches `.gun`.
- There is no structural-envelope handler, no passive descriptor, and no canonical decoder:
  `.gun` is absent from `FORMAT_HANDLERS` in `tools/fingerprint_assets.py`, absent from every
  header under `native/include/omega/retail/*.h` and `native/include/omega/asset/*.h`, absent
  from every source file under `native/src/**/*.cpp`, and absent from any target/source-list
  registration in the top-level `CMakeLists.txt`.
- No test (adversarial, resource-boundary, or otherwise) references `.gun` in any tracked test
  registration, because no decoder or descriptor exists to test. This is a complete tooling gap,
  not a narrower "coverage gap in an existing decoder."

## 8. Codex work order

Ranked, privacy-safe, concrete next steps — no semantic speculation, no menu-role assignment:

1. **Highest priority — add a passive structural fingerprint handler for `.gun`.** Extend
   `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` with a `fingerprint_gun()` function mirroring
   the existing eleven handlers: read only header-region bytes from each of the 624 spans, emit
   aggregate counts (byte-offset 0-15 value histogram, size distribution, alignment-modulo
   buckets) into a new `formats[".gun"]` object. Re-run the scanner against the owner's tracked
   corpus and inspect only the emitted JSON aggregates (never raw payload).
2. **Cross-tabulate depth and containing-archive-type for `.gun`.** Using the same scan pass,
   extend `scan.extensions` counting to also bucket by `scan.depth`, so it can be established
   (in aggregate) whether all 624 `.gun` occurrences share a single nesting depth or vary — this
   is a pure aggregate question already partially answerable from existing scanner infrastructure.
3. **If step 1 yields a consistent magic/size formula across all 624 spans**, promote the result
   into a new `analysis/formats/GUN.md` grammar doc using the same "Confirmed structural result"
   table format as `TDX.md`/`COL.md`, and add a corresponding `E-####` entry to
   `analysis/evidence/ledger.jsonl` citing the specific aggregate field(s) that support the claim.
4. **If step 1 yields inconsistent or ASCII/text-like bytes**, do not force a fabricated binary
   grammar — instead record the negative/mixed result as an Aggregate-only fact (e.g., "N of 624
   spans begin with printable ASCII; M of 624 do not") and stop there until further structural
   signal exists.
5. **Do not attempt semantic (weapon/config/etc.) labeling at any point in this pipeline.** Any
   future decoder work must stay at the structural-envelope tier (sizes, counts, magic bytes,
   alignment) until an independent, citable, privacy-safe source (e.g., a published format
   reference or a mechanically-verified cross-reference already meeting the project's clean-room
   rules) establishes semantics. Until then, keep `.gun` classified `aggregate_scanner_only` (or
   `structural_envelope_only` once step 1 lands) and keep the family UNKNOWN.
