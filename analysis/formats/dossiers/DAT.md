# .dat family dossier

## 1. Identity

`.dat` is a whole-disc-only suffix in the owner NTSC-U corpus: it never occurs as a HOG member
(top-level or recursively nested) and is attested only at the top level of the disc image itself.
No tracked source assigns it a decoder, a grammar, or a semantic role. At the evidence level this
document can defend, `.dat` is an **unclassified whole-disc file family**: the tracked evidence
does not establish that it is a PS2 system/OS file, and it also does not establish that it is a
HOG-adjacent game asset in the same sense as the HOG-member families (`.tdx`, `.vum`, `.col`,
etc.). It is therefore documented as a full family dossier rather than collapsed into the
`system_file_out_of_scope` stub, because the aggregate evidence gathered below (distinct from the
disc's confirmed system-file extensions) argues against — but does not conclusively rule out — a
PS2 system-file classification.

## 2. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive, inside any HOG (nested spans) | 0 | `analysis/formats/asset-fingerprints.json` (`formats`/`scan.extensions` keys enumerate only `.bin .bnk .bon .col .fnt .gui .gun .hog .ie .lpd .par .prn .pss .scc .ska .skas .skel .skf .skl .skm .so .sub .tdx .txt .vag .vpk .vum`; `.dat` is absent) |
| Top-level, inside any HOG | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts; `.dat` is absent from the enumerated suffixes) |
| Whole-disc | 20 | `analysis/manifests/disc-summary.json` (`extensions` map: `".dat": 20`), cross-checked against `analysis/manifests/disc-files.jsonl` (20 matching `path` entries, aggregated without reproducing individual rows) |

No `analysis/evidence/ledger.jsonl` entry (`E-0001`..) references `.dat`; a full-file grep of the
ledger for `.dat`/`dat`/`strings` returned no confirmed or rejected claim for this suffix.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Citation |
|---|---|---|
| 1 | `.dat` is absent from `FORMAT_HANDLERS` — the dict mapping suffixes to structural fingerprint handlers only contains `.tdx .ska .skas .skm .skl .vag .lpd .par .col .vum .vpk` | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict, ~line 499) |
| 2 | `.dat` produces no `formats["dat"]` aggregate bucket in `asset-fingerprints.json`, because the scanner only opens a structural handler for extensions present in `FORMAT_HANDLERS`, and separately `.dat` cannot appear via `scan.extensions` inside a HOG scan because it never occurs as a HOG member | `tools/fingerprint_assets.py` (`scan_asset`/`FORMAT_HANDLERS`), `analysis/formats/asset-fingerprints.json` (`formats` object keys) |
| 3 | No `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h` header and no `native/src/**/*.cpp` file names or references a `.dat`-suffixed decoder, descriptor, or grammar; a name/suffix grep across those trees returns no match for `dat` as a format identifier | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp` |
| 4 | `CMakeLists.txt` registers no target, source file, or test for a `.dat` decoder (no `dat`-named source is compiled or listed) | `CMakeLists.txt` |
| 5 | The disc's confirmed PS2-system-adjacent extensions (`.elf` 4, `.irx` 46, `.cnf` 8, `.sys` 2, `.bd`/`.hd` 1 each) are distinct top-level extension buckets from `.dat` in the same manifest — `.dat` is counted separately and is not merged with or aliased to any of those buckets | `analysis/manifests/disc-summary.json` (`extensions` map) |
| 6 | `analysis/evidence/ledger.jsonl` contains no confirmed or rejected entry whose `claim` or `evidence` references `.dat` | `analysis/evidence/ledger.jsonl` (full-file search) |

## 4. Aggregate-only facts

Aggregate statistics derived from `analysis/manifests/disc-files.jsonl` for the 20 whole-disc
`.dat` occurrences, computed without reproducing any individual path, hash, or per-file row:

- Size range: minimum 6,624 bytes, maximum 126,320 bytes; total bytes across all 20 occurrences:
  459,056 bytes.
- Directory distribution: the 20 occurrences are spread across 19 distinct top-level directories
  already present in tracked manifests (the per-level `GAMEDATA/<LEVEL>` directories enumerated in
  `analysis/formats/hog-validation.json`); 18 of those directories contain exactly one occurrence
  each, and 1 directory contains exactly two occurrences. No directory contains more than two.
- `analysis/formats/asset-fingerprints.json`'s `scan` block reports 0 `standard_compression_magic_hits`
  across 46,604 checked non-HOG asset spans (gzip/zip/bzip2/xz/7zip signatures); this sweep covers
  HOG-internal spans and does not claim to have checked whole-disc `.dat` files specifically, since
  `.dat` never appears as a HOG-internal span (see §2). No tracked source reports a magic-byte or
  compression check run directly against the whole-disc `.dat` files.
- `.pop` (18 whole-disc occurrences, per `analysis/manifests/disc-summary.json`) is the closest
  whole-disc-count neighbor to `.dat` (20) in the same manifest, but `.pop` has a published grammar
  (`analysis/formats/POP.md`) and a native decoder, whereas `.dat` has neither — the count
  proximity is a coincidental aggregate fact, not evidence of a shared format or relationship.

No aggregate source reports header magic, alignment constants, or internal structure for `.dat`,
because no tracked scanner or decoder has ever opened a `.dat` file structurally (see §3, rows 1–2).

## 5. Hypotheses

Each is explicitly labeled and unconfirmed. None is asserted as fact anywhere else in this
document.

- **H1 — Not a PS2 system/OS file.** The disc's manifest already carries separate, small,
  low-count extension buckets for PS2-system-adjacent files (`.elf`, `.irx`, `.cnf`, `.sys`,
  `.bd`/`.hd`, per §3 row 5), and `.dat` is not merged into or aliased with any of them. This is
  suggestive but not conclusive.
  *Confirms if:* a privacy-safe structural scan of the 20 `.dat` files finds no ELF/IRX magic, no
  PS2 `SYSTEM.CNF`-style key=value boot-config text, and no icon/save-file header magic (PS2 save
  icons and `SYSTEM.CNF` are the usual `.dat`-adjacent PS2 system artifacts on other titles).
  *Refutes if:* such a scan finds one of those markers in some or all of the 20 files.
- **H2 — Per-level data resource, one instance per game level plus a shared/common location.**
  The directory-distribution aggregate in §4 (18 single-occurrence directories, 1 two-occurrence
  directory, spread across the same per-level directory set already documented for HOG archives)
  is consistent with a resource that is generated or shipped once per playable level, with one
  directory holding a second, related variant.
  *Confirms if:* a privacy-safe per-directory occurrence-count re-run against the full corpus
  (not just this NTSC-U disc) shows the same one-or-two-per-level pattern holding across regions/
  editions.
  *Refutes if:* other editions show a different per-directory occurrence pattern (e.g., zero,
  three, or a non-level-aligned directory set).
- **H3 — Structurally uniform within the family.** Not established either way; no tracked source
  has opened any `.dat` file's header bytes.
  *Confirms if:* a bounded, aggregate-only header-byte scan (first N bytes, magic/alignment only,
  no payload reproduction) run across all 20 occurrences reports a shared magic or common leading
  structure.
  *Refutes if:* the scan reports divergent leading bytes across occurrences (i.e., `.dat` is a
  generic container suffix reused for unrelated content, as `.dat` often is on PS2 titles).

## 6. Missing observations

- **No structural scanner has ever been run against `.dat`.** `tools/fingerprint_assets.py`'s
  `FORMAT_HANDLERS` dict has no `.dat` entry (§3 row 1), so `asset-fingerprints.json` carries zero
  aggregate structural data (no magic bytes, no size histogram beyond the whole-disc manifest, no
  alignment observations) for this family. *Privacy-safe collection:* add a bounded, read-only
  `.dat` handler to `tools/fingerprint_assets.py` that reports only aggregate magic-byte
  histograms, size buckets, and alignment stats — mirroring the existing HOG-member handlers — and
  re-run the existing fingerprinting pass against the owner corpus, publishing only the aggregate
  output (no payload bytes, no per-file rows).
- **No published grammar document exists.** Unlike `.pop`, `.col`, `.vum`, etc., there is no
  `analysis/formats/DAT.md` (or equivalent) describing byte layout. *Privacy-safe collection:* once
  §6 row 1's aggregate scan establishes whether the family shares a common header, promote the
  result to a grammar doc following the `POP.md`/`VUM.md` template, citing only aggregate/structural
  constants.
- **No native descriptor or decoder exists**, so there is no CMake target, no test target, and no
  adversarial/resource-boundary coverage to note a gap in (§3 rows 3–4) — the gap is total, not
  partial.
- **No ledger entry (`E-####`) has ever evaluated a claim about `.dat`.** *Privacy-safe collection:*
  file a new ledger entry once §6 row 1's scan produces a citable aggregate fact, following the
  existing `E-00xx` claim/evidence/check schema.
- **No cross-region/cross-edition comparison exists.** All counts in this dossier derive from the
  single NTSC-U owner disc represented by `disc-summary.json`/`disc-files.jsonl`. Whether other
  regions/editions carry the same 20-file, per-level pattern is unknown. *Privacy-safe collection:*
  if additional owned discs are ever fingerprinted under the same tracked pipeline, add their
  `.dat` aggregate counts to this dossier's §2/§4 tables without introducing any new per-file
  detail.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`** — and even that is understated, because the *existing*
aggregate scanner (`tools/fingerprint_assets.py`) does not currently bucket `.dat` at all (§3 rows
1–2); the only aggregate evidence available today comes from the whole-disc manifest tooling
(`analysis/manifests/disc-summary.json`, `disc-files.jsonl`), which records occurrence counts and
sizes but performs no structural/content inspection of `.dat` files. There is no
`canonical_decoder`, no `structural_envelope_only` decoder, and no `passive_descriptor_only`
descriptor for `.dat` anywhere in `native/include/omega/retail/`, `native/include/omega/asset/`,
`native/src/`, or `CMakeLists.txt` (§3 rows 3–4). Consequently there is no adversarial or
resource-boundary test suite to report a gap in — the gap is the complete absence of any decoder,
not a partially-tested one.

## 8. Codex work order

Ranked, privacy-safe, concrete next steps. None presumes a semantic role; each step either builds
tooling against the owner corpus or verifies an existing aggregate.

1. **Highest priority — add a bounded `.dat` structural handler to `tools/fingerprint_assets.py`**
   (mirroring the existing `fingerprint_tdx`/`fingerprint_col`/etc. handlers) that reports only:
   leading-byte histogram (e.g., first 4–16 bytes bucketed, not reproduced), overall size
   histogram, and any ASCII/text-ratio aggregate (proportion of printable bytes) — all as
   aggregate counts, never per-file. Register it in `FORMAT_HANDLERS` and re-run the existing
   fingerprinting pass against the owner corpus's whole-disc tree (not just HOG-internal spans,
   which the current pass is scoped to). Publish only the aggregate JSON, following the existing
   `asset-fingerprints.json` schema.
2. Extend the handler (or a follow-up bounded scanner script, mirroring
   `tools/scan_pop_post_terrain.py`'s pattern) to check specifically for the known PS2 system-file
   magic/shape markers relevant to H1 (ELF magic, IRX/module header shape, `SYSTEM.CNF`-style
   key=value ASCII text, PS2 save-icon header magic) and report only pass/fail aggregate counts
   across the 20 occurrences — this directly confirms or refutes H1 without exposing any payload.
3. Once step 1 produces a citable aggregate (shared magic present or absent), file a new
   `analysis/evidence/ledger.jsonl` entry (`E-00xx`) recording the confirmed aggregate claim,
   following the existing claim/evidence/check schema used by `E-0094` and neighbors.
4. If step 1/2 establish a shared, well-formed header, promote the result to a
   `analysis/formats/DAT.md` grammar document using the `POP.md` template (validated scope,
   observed layout, native contract section left `TODO` until a decoder is actually built) —
   do not write this document before the aggregate evidence exists.
5. Only after steps 1–4 produce a confirmed, citable grammar: scope and build a
   `structural_envelope_only` native descriptor (own bytes, bound sizes, reject malformed input,
   assign no semantics) under `native/include/omega/asset/`, with a corresponding CMake target and
   test file, following the pattern already established for `.vpk`'s wrapper-envelope descriptor
   (`E-0094`). Do not attempt a `canonical_decoder` until the envelope stage is proven and reviewed.
6. Re-run this dossier's §2 occurrence-evidence table whenever `asset-fingerprints.json`,
   `hog-validation.json`, or `disc-summary.json` are regenerated, to catch drift (e.g., a future
   corpus addition changing the whole-disc count away from 20).
