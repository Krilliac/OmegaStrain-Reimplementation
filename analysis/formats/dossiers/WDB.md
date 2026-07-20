# `.wdb` — Dossier

## 1. Identity

`.wdb` is a suffix that occurs exactly once across the tracked whole-disc file inventory and
zero times inside any `.HOG` archive (recursive or top-level). At the evidence level currently
defensible: **an unhandled, unparsed, whole-disc-only game-data file.** The tracked whole-disc
manifest's path field for this single entry places it under the disc's `GAMEDATA` top-level
directory — one of the published top-level buckets already enumerated in
`analysis/manifests/disc-summary.json` (`top_level_bytes`), alongside `IRX`, `NETGUI`,
`OVL_DNAS.BIN`, `SCUS_972.64`, `SFO_GAME.INI`, `SYSTEM.CNF`, and `ZMEDIA` — rather than under any
of the disc's OS/boot-metadata top-level entries (`SYSTEM.CNF` boot config, `SCUS_972.64` boot
ELF, `SFO_GAME.INI` title metadata, `OVL_DNAS.BIN`, or the `IRX` IOP-module directory). On that
basis this dossier classifies `.wdb` as an **in-scope game-data asset, not a PS2 system/OS file**,
and proceeds with a full dossier rather than a system-file stub. Per the clean-room rules, the
specific path, filename, size, and hash of that one manifest row are per-file/private-input
detail and are withheld here; only the aggregate count and top-level-bucket attribution are
cited. No tracked source defines a `.wdb` header magic, internal grammar, decoder, or specific
semantic role (weapon table or otherwise — any name-derived inference is a Hypothesis only, never
a Confirmed fact). Family classification is **UNKNOWN**.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive, inside `.HOG` archives (any depth) | 0 | `analysis/formats/asset-fingerprints.json` → `scan.extensions` (no `.wdb` key present) |
| Top-level HOG member suffix | 0 | `analysis/formats/hog-validation.json` (no `.wdb` key present) |
| Whole-disc (top-level disc file tree) | 1 | `analysis/manifests/disc-summary.json` → `extensions[".wdb"] = 1`; corroborated by exactly one matching row in `analysis/manifests/disc-files.jsonl` |

`.wdb` is therefore a whole-disc-only suffix: it exists as a filesystem entry on the extracted
disc image but never as packed content inside any `.hog` container, recursive or top-level. This
is the same occurrence pattern as `.bd`, `.hd`, `.icn`, `.log`, and `.map` (each also
whole-disc=1, HOG-count=0) documented in their respective sibling dossiers.

## 3. Confirmed facts

| # | Fact | Tracked citation |
| --- | --- | --- |
| C1 | `.wdb` is counted exactly once under `extensions[".wdb"]` in the whole-disc manifest summary. | `analysis/manifests/disc-summary.json` (`extensions[".wdb"] = 1`) |
| C2 | Exactly one row in the whole-disc file manifest carries a `.WDB`-suffixed path, and that row's path field places it under the disc's `GAMEDATA` top-level directory (not under `IRX`, `SYSTEM.CNF`, `SFO_GAME.INI`, `OVL_DNAS.BIN`, or `SCUS_972.64`). | `analysis/manifests/disc-files.jsonl` (one matching row); top-level bucket names corroborated by `analysis/manifests/disc-summary.json` → `top_level_bytes` keys |
| C3 | `.wdb` is **not** a key in `FORMAT_HANDLERS`, the dict mapping extensions to structural fingerprint functions (`.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk` only). | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict) |
| C4 | Because `.wdb` has no handler, the recursive scanner never visits it (it has zero recursive-in-HOG occurrences per C-evidence above) and no per-format aggregate object exists for it in the `formats` object of the fingerprint output. | `analysis/formats/asset-fingerprints.json` (`formats` object keys; absence of `.wdb`) |
| C5 | `.wdb` does not appear in the published corpus-results table in `ASSET-RECON.md` (which enumerates TDX, SKM, SKL, SKA, SKAS, VAG, LPD, PAR, COL, VUM, VPK, POP only), nor anywhere else in that document. | `analysis/formats/ASSET-RECON.md` (grep, zero hits) |
| C6 | `.wdb` has zero occurrences in any other published per-format grammar doc (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, `LPD.md`, `PAR.md`, `SKAS.md`, `VAG.md`, `VPK.md`, `SO.md`, `FRONTEND-TOPOLOGY.md`). | `analysis/formats/*.md` (grep, zero hits) |
| C7 | `.wdb` has zero occurrences in the evidence ledger (no `E-####` entry mentions it). | `analysis/evidence/ledger.jsonl` (grep, zero hits) |
| C8 | `.wdb` has zero occurrences anywhere in tracked native code, headers, or the top-level `CMakeLists.txt` — no decoder class, struct, descriptor, or build-system registration exists for it. | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` (grep, zero hits) |

## 4. Aggregate-only facts

- The only quantitative facts the tracked aggregates establish for `.wdb` specifically are: bare
  occurrence count = **1** (whole-disc only), and top-level-directory bucket = `GAMEDATA` (one of
  eight top-level buckets enumerated in `disc-summary.json`). No size, hash, alignment, or
  header-byte value for that single occurrence is disclosed in this dossier, consistent with the
  clean-room per-file-withholding rule already applied in the `.bd`/`.hd`/`.icn` sibling dossiers.
- Corpus-wide, `standard_compression_magic_hits = 0` out of `standard_compression_spans_checked =
  46,604` non-`.hog` spans — i.e., no span of any unhandled extension across the whole recursive
  scan (a pool that does not include this file, since it is whole-disc-only and outside the
  recursive-in-HOG scan's traversal root) exhibited a recognized standard-compression magic
  header. This is a corpus-wide, not `.wdb`-specific, aggregate and is cited only for context.
- No size, count, or alignment statistic in `asset-fingerprints.json`, `hog-validation.json`, or
  any published `*.md` is broken out for `.wdb`. Any number beyond the bare count of 1 and the
  `GAMEDATA` bucket attribution above would be an invented aggregate, not a tracked one.

## 5. Hypotheses

All statements below are explicitly unconfirmed hypotheses, not facts. Each lists the
privacy-safe, aggregate-only observation that would confirm or refute it without exposing any
private payload, path, filename, size, or hash.

- **H1 — `.wdb` is a fixed-header binary table format (a "world/weapon database" style flat
  record table), analogous in spirit to the handled structural-envelope suffixes (TDX/COL/etc.).**
  Confirm/refute: add a passive `.wdb` fingerprint routine to `tools/fingerprint_assets.py` that
  reads only the first N header bytes of the single tracked span and reports structural
  observations (byte-0..3 magic-value, presence/absence of a repeating fixed-stride record
  pattern, size-modulo-alignment). Because there is only one tracked occurrence, any "aggregate"
  here is degenerate (n=1) and must be labeled as such, not generalized.
- **H2 — `.wdb` is exclusively whole-disc (never HOG-packed) because it is loaded directly by the
  executable at boot/level-load time rather than streamed from an archive.**
  Confirm/refute: this is already partially supported by the occurrence evidence
  (recursive-in-HOG=0, top-level-HOG=0, whole-disc=1), but the *reason* is unconfirmed. A
  cross-reference against the retail loader's file-table strings (already-tracked, path-free
  aggregate evidence only, per `analysis/elf/argument-loader.md`'s existing method) could
  establish whether the loader references this suffix directly; no such cross-reference currently
  exists in tracked evidence.
- **H3 — the suffix name reflects a "weapon database" or generic "world database" table role.**
  This is a plausibility inference from the string "wdb" and the `GAMEDATA` top-level bucket
  attribution, and MUST NOT be treated as established. No tracked source parses, decodes, or names
  the internal structure of this file. Confirming or refuting this would require a semantic
  decoder backed by independently cross-referenced, privacy-safe executable-side evidence (e.g., a
  string/table cross-reference showing the runtime opening `.wdb`-suffixed files in a specific
  named subsystem), which does not currently exist in tracked evidence. Until such a decoder and
  citation exist, this hypothesis stays unconfirmed and must not appear in any Confirmed section.

## 6. Missing observations

- No per-extension structural fingerprint function exists for `.wdb` in
  `tools/fingerprint_assets.py`, so no header-magic, record-stride, or size observation has ever
  been collected for it. Privacy-safe collection: add a `fingerprint_wdb()` handler mirroring the
  existing eleven handlers that reads only structural header fields from the single tracked span
  and emits an n=1 observation into a new `formats[".wdb"]` bucket — never payload bytes, and
  explicitly labeled as a single-sample observation rather than a corpus aggregate.
- No published grammar doc (`analysis/formats/WDB.md` as a *format* doc, distinct from this
  dossier) exists. Privacy-safe collection: once a structural handler produces a stable structural
  observation for the one tracked span, promote any confirmed structural fact into a `WDB.md`
  grammar doc alongside `TDX.md`/`COL.md`/etc., explicitly flagging the n=1 sample-size caveat.
- No evidence-ledger entry (`E-####`) exists for `.wdb`. Privacy-safe collection: once a structural
  claim is mechanically verified against the aggregate scan output, add a ledger entry citing the
  specific scanner run and aggregate field.
- No cross-reference against retail executable-side string/table evidence exists for `.wdb` in any
  tracked file. Any such cross-reference, if pursued, must remain an offline research technique
  producing only aggregate/structural citations (per the constraint already stated in
  `ASSET-RECON.md` §"Scope and method") — no retail instructions, disassembly excerpts, or
  executable script cells may enter tracked docs or the runtime.
- Because the tracked corpus contains exactly one `.wdb` instance, no statistically meaningful
  aggregate (size range, alignment distribution, magic-consistency-across-N) can ever be produced
  from this owner's corpus alone. Any future work must explicitly caveat single-sample results as
  such rather than presenting them as corpus-wide facts.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- The single `.wdb` occurrence is counted only by the generic whole-disc manifest summarizer
  (`analysis/manifests/disc-summary.json` / `disc-files.jsonl` generation) and is never visited by
  the recursive HOG-asset scanner (`tools/fingerprint_assets.py`'s `scan_asset()`), because that
  scanner's traversal root is HOG-archive content and `.wdb` has zero HOG occurrences. This is the
  full extent of tooling that touches `.wdb`.
- There is no structural-envelope handler, no passive descriptor, and no canonical decoder:
  `.wdb` is absent from `FORMAT_HANDLERS` in `tools/fingerprint_assets.py`, absent from every
  header under `native/include/omega/retail/*.h` and `native/include/omega/asset/*.h`, absent
  from every source file under `native/src/**/*.cpp`, and absent from any target/source-list
  registration in the top-level `CMakeLists.txt`.
- No test (adversarial, resource-boundary, or otherwise) references `.wdb` in any tracked test
  registration, because no decoder or descriptor exists to test. This is a complete tooling gap,
  not a narrower "coverage gap in an existing decoder."

## 8. Codex work order

Ranked, privacy-safe, concrete next steps — no semantic speculation, no menu-role assignment:

1. **Highest priority — add a passive structural fingerprint handler for `.wdb`.** Extend
   `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` with a `fingerprint_wdb()` function mirroring
   the existing eleven handlers: read only header-region bytes from the single tracked whole-disc
   span (note: this requires extending the scanner's traversal to also visit whole-disc files, not
   only HOG-nested content, since `.wdb` never appears inside a HOG), and emit a structural
   observation (byte-offset 0-15 value, apparent record stride if any, total size) into a new
   `formats[".wdb"]` object. Explicitly label the result as n=1 in any emitted JSON and downstream
   doc — never generalize a single sample into a corpus-wide claim.
2. **Do not fabricate a corpus-wide aggregate.** Because only one instance exists on the tracked
   disc, any "range," "distribution," or "bucket count" statistic for `.wdb` beyond the bare n=1
   observation from step 1 would be invented. Stop at the single structural observation.
3. **If step 1 yields a legible fixed-header/record-stride structure**, promote the result into a
   new `analysis/formats/WDB.md` grammar doc using the same "Confirmed structural result" table
   format as `TDX.md`/`COL.md`, explicitly flagging the single-sample caveat, and add a
   corresponding `E-####` entry to `analysis/evidence/ledger.jsonl` citing the specific aggregate
   field(s) that support the claim.
4. **If step 1 yields ASCII/text-like or otherwise inconsistent bytes**, record the negative result
   as an Aggregate-only fact (e.g., "the single tracked span does/does not begin with printable
   ASCII") and stop there until further structural signal exists — there is no path to a
   statistically supported binary grammar from a single sample.
5. **Do not attempt semantic (weapon-table/world-database/etc.) labeling at any point in this
   pipeline.** Any future decoder work must stay at the structural-envelope tier (size, byte
   values, apparent stride) until an independent, citable, privacy-safe source (e.g., a
   mechanically-verified executable-side cross-reference already meeting the project's clean-room
   rules) establishes semantics. Until then, keep `.wdb` classified `aggregate_scanner_only` (or
   `structural_envelope_only` once step 1 lands) and keep the family UNKNOWN.
