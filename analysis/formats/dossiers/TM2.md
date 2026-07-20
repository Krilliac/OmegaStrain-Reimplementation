# .tm2 — Dossier (stub classification)

## 1. Identity

`.tm2` is a whole-disc-only suffix appearing exactly 16 times in the
tracked disc inventory. It never occurs inside any `.hog` archive.
Tracked evidence places all 16 occurrences at the top level of the disc
image inside the `NETGUI` directory — a directory the repository's own
`.GUI`, `.PF`, and `.LOG` dossiers already characterize, from tracked
manifests, as the PS2 network-boot stack (its other established members
being `.RGB`/`.BIN`/`.IRX`/`.ELF`/`.PF` files, none of which are
game-content asset formats reached by the HOG-recursive asset pipeline).
On that basis this dossier classifies `.tm2` as a **PS2 system/
network-boot-stack support file** (specifically, texture/image resource
data consumed by the network-configuration GUI, not by the game's
HOG-based asset-loading system), and is therefore written as a **concise
stub** per the task's occurrence-gate instruction rather than a full
asset-format dossier.

Note on published context: "TM2"/"TIM2" is a name publicly associated
with a Sony PS2 SDK texture-image container in general PS2 development
literature. No tracked file in this repository defines, cites, or
verifies that grammar for the specific bytes on this disc, so no field
layout, header magic, or pixel-format claim is made here — that would be
an invented semantic per the clean-room rules. This dossier only records
what the tracked manifests and tooling actually establish: occurrence
counts, containment, and absence of any registered decoder.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts) |
| Whole-disc (anywhere on the extracted disc tree) | 16 | `analysis/manifests/disc-summary.json`, key `extensions[".tm2"] = 16`; corroborated by exactly 16 matching rows in `analysis/manifests/disc-files.jsonl` |

Per clean-room rules (and matching this repository's own `.BD`/`.LOG`
dossier precedent), the specific leaf filenames, sub-directory path
segments, sizes, and hashes of those 16 `disc-files.jsonl` rows are
per-file/private-input detail and are withheld here. Only the top-level
container (`NETGUI`) — already a generic, previously-published container
name per the tracked `.GUI`/`.PF`/`.LOG` dossiers and `disc-summary.json`'s
`top_level_bytes` map — is cited.

## 3. Confirmed facts

| # | Claim | Tracked citation |
|---|---|---|
| 1 | Disc-wide inventory records exactly 16 `.tm2`-suffixed files. | `analysis/manifests/disc-summary.json` (`extensions[".tm2"] = 16`), `analysis/manifests/disc-files.jsonl` |
| 2 | No `.tm2` member occurs inside any tracked HOG archive (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json` |
| 3 | All 16 `.tm2` rows in `analysis/manifests/disc-files.jsonl` sit under the disc's top-level `NETGUI` directory — the same directory the repo's own `.GUI` dossier documents (from these same tracked manifests) as containing PS2 network-boot-stack members `.RGB`/`.BIN`/`.IRX`/`.ELF`/`.TM2`/`.PF`, none of which is a game-content asset family. | `analysis/manifests/disc-files.jsonl`; `analysis/formats/dossiers/GUI.md` (§2 note); `analysis/manifests/disc-summary.json` (`top_level_bytes.NETGUI`) |
| 4 | `.tm2` has no entry in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict (registered keys: `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`) — no structural handler is registered for this suffix. | `tools/fingerprint_assets.py` |
| 5 | No `analysis/formats/*.md` published grammar document (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, `SO.md`, `LPD.md`, `PAR.md`, `VAG.md`, `VPK.md`, `SKAS.md`, `ASSET-RECON.md`, `FRONTEND-TOPOLOGY.md`) mentions `.tm2`/`TIM2`, except for the incidental container-inventory notes in `GUI.md`, `PF.md`, and `LOG.md` that list `.TM2` only as a co-resident member of the `NETGUI` directory (not as a format grammar). | direct inspection, `analysis/formats/*.md` |
| 6 | No `analysis/evidence/ledger.jsonl` entry names `.tm2` or a `TIM2`/`TM2` container format, header, or decoder. | `analysis/evidence/ledger.jsonl` |
| 7 | No `native/include/omega/retail/*.h` or `native/include/omega/asset/*.h` decoder/descriptor type references `.tm2`/`TIM2`, and no `CMakeLists.txt` target registers a `.tm2` source, decoder, or test. | `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `CMakeLists.txt` |

## 4. Aggregate-only facts

- `analysis/manifests/disc-summary.json`'s whole-disc extension histogram
  records `.tm2` at count 16 out of `file_count: 448` total whole-disc
  files (`total_bytes: 3,455,648,408`) — grouped, in this repository's
  other dossiers (`ELF.md`, `IMG.md`, `MAP.md`), alongside the disc's
  other multi-count non-`.hog` extensions (`.pop`:18, `.irx`:46) rather
  than the single-count top-level extensions (`.bd`, `.hd`, `.icn`,
  `.ini`, `.log`, `.map`, `.tbl`, `.wdb`) or the count-2 extension
  (`.sys`:2).
- Aggregate size statistics across the 16 whole-disc `.tm2` rows in
  `analysis/manifests/disc-files.jsonl`: minimum 8,320 bytes, maximum
  246,848 bytes, mean ≈57,200 bytes. No individual size is attributed to
  a named file.
- Among the 16 rows, at least one pair shares an identical (size, hash)
  pair — i.e. two of the sixteen `.tm2` entries are byte-identical
  content — reported here strictly as an aggregate duplicate-count
  observation with no filename, path, or hash value disclosed.
- `disc-summary.json`'s `top_level_bytes.NETGUI` records only the
  directory's aggregate byte total (30,306,038 bytes across all of
  `NETGUI`'s members combined, including the `.RGB`/`.BIN`/`.IRX`/`.ELF`/
  `.PF` co-resident members); no per-extension byte subtotal for `.tm2`
  specifically is published anywhere in tracked docs, so the 16 `.tm2`
  files' combined byte total (computable only by summing withheld
  per-file rows) is not independently stated in any tracked aggregate.
- `analysis/formats/asset-fingerprints.json` contains no per-format
  structural aggregate block for `.tm2` (such blocks exist only for the
  `FORMAT_HANDLERS`-listed suffixes named in §3 row 4), confirming `.tm2`
  is scan-visible (counted into the generic whole-disc extension
  histogram) but never structurally opened by the fingerprinting tool.

## 5. Hypotheses

- **H1 — PS2 SDK texture-image resource for the network-configuration
  GUI.** The 16 `.tm2` files could be texture/image data consumed by the
  `NETGUI` network-boot-stack overlay (on-screen keyboard, dialog
  backgrounds, button graphics used during network setup), rather than
  by the game's own HOG-based rendering/asset pipeline.
  *Confirmation/refutation:* a privacy-safe passive descriptor (see §6)
  that reports only aggregate structural signals (head-byte magic
  classification if a consistent magic exists across all 16 instances,
  size-bucket distribution, 16/64/2048-byte alignment flags) without
  emitting per-file identifiers; a consistent non-zero magic shared
  across all 16 instances, distinct from any `FORMAT_HANDLERS`-registered
  magic, would be consistent with H1 but would not by itself establish
  field-level grammar.
- **H2 — Shared-content variants across localizations.** The presence of
  at least one duplicate (size, hash) pair among the 16 files (§4) could
  indicate that some `.tm2` instances are per-locale variants of a shared
  graphic (e.g. a language-independent button image reused verbatim
  across locale-specific subsets), consistent with a network-menu
  localization scheme.
  *Confirmation/refutation:* an aggregate duplicate-cluster count (how
  many of the 16 rows collapse into how many distinct hash groups) would
  either strengthen H2 (many duplicates → strong localization-reuse
  signal) or weaken it (all 16 hashes distinct beyond the one observed
  pair → weak signal), without disclosing any filename or path.

## 6. Missing observations

- **No structural or passive descriptor for `.tm2` exists** in
  `tools/fingerprint_assets.py` — no head-byte/magic classification, size
  bucket, or alignment determination has ever been mechanically extracted
  or aggregated for this suffix.
- **No published grammar doc** covers `.tm2`, and none is expected if the
  system-file classification in §1 holds.
- **No ledger entry** records a confirmed or rejected claim about the
  tracked disc's `.tm2` files.
- **No per-extension byte subtotal** for `.tm2` within `NETGUI` exists in
  any tracked aggregate (only the whole-directory total is published);
  producing one would require a privacy-safe aggregation step (sum of
  withheld per-file sizes) rather than a new tracked source, since the 16
  individual sizes are already present in `disc-files.jsonl` but are not
  yet rolled into a stated `.tm2` subtotal anywhere.
- **No duplicate-cluster count** (distinct-hash count among the 16 rows)
  is published anywhere in tracked docs; only the fact that at least one
  duplicate pair exists (§4) has been derived here from the raw
  manifest rows.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`**

Justification (one paragraph): the tracked evidence mechanically
establishes that all 16 `.tm2` occurrences live at the top level of the
`NETGUI` directory, which the repository's own tracked `.GUI`, `.PF`, and
`.LOG` dossiers already document (from `disc-summary.json`/
`disc-files.jsonl`) as the PS2 network-boot-stack container holding
`.RGB`/`.BIN`/`.IRX`/`.ELF`/`.TM2`/`.PF` system files rather than
game-content assets; `.tm2` has zero occurrences inside any HOG archive
(recursive or top-level), is absent from every published
`analysis/formats/*.md` grammar document (beyond incidental
container-inventory mentions), has no entry in
`tools/fingerprint_assets.py`'s `FORMAT_HANDLERS`, and no
`native/include/omega/retail/*.h` or `native/include/omega/asset/*.h`
decoder/descriptor or `CMakeLists.txt` registration treats it as a
decodable/registered asset format. Building an asset decoder for `.tm2`
would therefore be unjustified scope creep for what the tracked,
directory-level evidence indicates is a system/network-boot-stack
support artifact (likely PS2-SDK-provided texture data for the network
setup GUI, per the publicly-known "TIM2" name — an unverified hypothesis,
not a tracked-file claim), not a class of game data asset reached by
OpenOmega's HOG-based asset pipeline.

## 8. Codex work order

Ranked, privacy-safe, no semantic speculation:

1. **(Highest priority) No new decoder or fingerprint-handler work for
   `.tm2`.** Do not add `.tm2` to `tools/fingerprint_assets.py`
   `FORMAT_HANDLERS` or to any `native/include/omega/asset/*.h` descriptor
   set — tracked directory-level evidence classifies this as a system/
   network-boot-stack support file outside the HOG-based asset pipeline
   this project targets, and adding decoder machinery for it would be
   unjustified scope creep with no supporting grammar or in-scope
   containment.
2. **Add a passive, aggregate-only descriptor** for `.tm2` in
   `tools/fingerprint_assets.py` (mirroring the existing `FORMAT_HANDLERS`
   registration pattern, but emitting only aggregate counters: head-byte
   magic classification shared/not-shared across the 16 instances, size
   bucket, alignment flag, compression-magic hit/no-hit, and a
   duplicate-hash-cluster count — never per-file paths, names, or hash
   values). This would let H1/H2 in §5 move toward Confirmed/Refuted
   without exposing any private input.
3. **Run the extended scanner against the existing tracked corpus** (no
   new inputs) and record the resulting aggregate JSON block into
   `analysis/formats/asset-fingerprints.json`, including the `.tm2`
   per-extension byte subtotal within `NETGUI` and the duplicate-cluster
   count, so a future revision of this dossier has real aggregate facts
   instead of the raw min/max/mean derived ad hoc here.
4. **Re-run the existing recursive/top-level HOG suffix scans** after any
   future disc re-extraction to confirm `.tm2`'s recursive-in-HOG and
   top-level-HOG counts remain 0; record the result as a new ledger entry
   rather than assuming permanence.
5. **Do not** attempt to open, name, or hash-fingerprint any individual
   private `.tm2` instance beyond what `disc-summary.json`/
   `disc-files.jsonl` already record in aggregate; do not access
   `private/`, `runtime/`, or `third_party/` (including
   `third_party/pcsx2`) in pursuit of a TIM2-format reference decoder or
   a second corroborating disc sample.
