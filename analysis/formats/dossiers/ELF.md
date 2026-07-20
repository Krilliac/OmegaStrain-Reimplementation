# .elf — Dossier

## 1. Identity

`.elf` (Executable and Linkable Format) is a generic, standard executable/
object-file container — not a game-specific data-asset grammar. Tracked
evidence in this repository (the sibling `.64` dossier, ledger `E-0002`/
`E-0004`) already establishes that this project's PS2 boot binary
(`SCUS_972.64`) is itself an ELF32 little-endian MIPS image despite its
disc-ID-derived extension. The four `.elf`-suffixed files found on the
tracked disc manifest sit in a distinct top-level container from the
game-asset containers (`GAMEDATA`, `ZMEDIA`) and from every HOG archive.
This dossier is therefore a **stub classification**, not a full asset
dossier: no decoder work is warranted because standard ELF executables are
out of scope for the asset-format fingerprinting pipeline, mirroring the
`.64` precedent.

## 2. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive-in-HOG | 0 | `analysis/formats/asset-fingerprints.json` (`scan.extensions` has no `.elf` key; `formats` handler keys have no `elf` entry) |
| Top-level-HOG | 0 | `analysis/formats/hog-validation.json` (`entry_extensions` has no `.elf` key) |
| Whole-disc | 4 | `analysis/manifests/disc-summary.json` (`"extensions": {".elf": 4, ...}`) |

The four whole-disc occurrences are enumerated by path/sha256/size in
`analysis/manifests/disc-files.jsonl`; all four share a common top-level
directory container that also appears, as an aggregate byte total, in
`analysis/manifests/disc-summary.json`'s `top_level_bytes` map (that
container name is a generic top-level disc directory already present in
the tracked manifest, analogous to `GAMEDATA` and `ZMEDIA`, and is
distinct from both of those). The `.elf` family never appears inside
`DATA.HOG`, `LOADING.HOG`, or any other tracked HOG archive — it exists
solely as a small cluster of top-level disc files, the same
occurrence shape as the `.64` boot executable.

## 3. Confirmed facts

| # | Claim | Tracked citation |
|---|---|---|
| 1 | Disc-wide inventory records exactly four `.elf`-suffixed files. | `analysis/manifests/disc-summary.json` (`"extensions": {".elf": 4}`) |
| 2 | All four `.elf` files are enumerated with individual path/sha256/size in the whole-disc manifest. | `analysis/manifests/disc-files.jsonl` |
| 3 | No `.elf` member occurs inside any tracked HOG archive (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json` |
| 4 | No fingerprint handler, native decoder header, or descriptor treats `.elf` as a registered asset format. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS`/`formats` keys omit `elf`), `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `CMakeLists.txt` — confirmed absent by inspection during this dossier's authoring |
| 5 | This project's own tracked ELF-inspection tooling and ledger already confirm, for a *different* disc file (`SCUS_972.64`), that the ELF container is the standard executable/object format (ELF32 little-endian MIPS, five program headers, eleven sections) — corroborating that the `.elf` suffix denotes the same standard executable container class, not a bespoke asset grammar. | Ledger `E-0004` (evidence: `analysis/elf/SCUS_97264.metadata.json`, `tools/inspect_ps2_elf.py`); see also `analysis/formats/dossiers/64.md` |

No ledger entry (`E-####`/`R-####`) in `analysis/evidence/ledger.jsonl`
names any of the four `.elf` files directly; the entries above are cited
only for the general ELF-container fact they establish about this
codebase's evidence, not as direct evidence about the four disc files.

## 4. Aggregate-only facts

- The whole-disc extension histogram places `.elf` at count 4, in the same
  small-cluster band as `.tdx` (2), `.skl` (2), `.sys` (2), and `.pf` (3) —
  i.e. a minor top-level file class, far smaller than the bulk asset
  classes (`.hog`: 273, `.pop`: 18, `.tm2`: 16). Source:
  `analysis/manifests/disc-summary.json`.
- The four `.elf` files sit inside one top-level disc container whose
  aggregate byte total (per `disc-summary.json`'s `top_level_bytes` map)
  is on the order of tens of megabytes — small relative to the two large
  asset containers (`GAMEDATA`, `ZMEDIA`, each hundreds of megabytes to
  low gigabytes) and much closer in scale to other small top-level system
  artifacts (e.g. the boot executable, the DNAS overlay, the system
  config file) than to the bulk asset containers. No further size
  breakdown per individual `.elf` file is published in any tracked
  aggregate beyond the per-file sizes already in `disc-files.jsonl`.
- `asset-fingerprints.json`'s recursive suffix inventory and `formats`
  handler-key list both omit `elf` entirely, meaning the fingerprinting
  pipeline has never observed nor registered this suffix as HOG-embedded
  content of any kind.

## 5. Hypotheses

- **H1**: The four `.elf` files are locale/platform variants of a single
  secondary executable (e.g. a network-setup or configuration front-end)
  bundled alongside the main game disc, rather than four unrelated
  programs. *Confirmation/refutation*: a privacy-safe aggregate
  comparison of the four files' sizes and a structural ELF-header pass
  (program-header count, section count, entry point — the same aggregate
  fields ledger `E-0004` already publishes for `SCUS_972.64`) run through
  `tools/inspect_ps2_elf.py` against each of the four, with results
  recorded as counts only (no section names, no code bytes), would
  confirm near-identical structural shape (supporting H1) or reveal
  materially different program-header/section counts (refuting it).
- **H2**: No `.elf`-suffixed file will ever appear inside a HOG archive in
  a hypothetical future re-extraction, because — like `.64` — the
  extension denotes an executable container fundamentally incompatible
  with the HOG asset-archive grammar rather than a general asset-type
  suffix that could be repacked. *Confirmation/refutation*: re-running the
  existing `asset-fingerprints.json` / `hog-validation.json` scan tooling
  against the owner corpus and checking that recursive-in-HOG and
  top-level-HOG `.elf` counts remain 0 would support H2; a nonzero count
  in either would refute it.

## 6. Missing observations

- No tracked evidence establishes the top-level container's *purpose*
  (e.g. whether it is a network-configuration utility, a demo launcher,
  or something else) — only its name-as-aggregate-key and byte total are
  published. A privacy-safe collection would run a structural (not
  semantic) ELF pass over each of the four files — entry point, program-
  header count, section count, endianness/machine fields only — and
  publish those counts in a new aggregate doc, without naming individual
  files or exposing code/string bytes.
- No `analysis/formats/*.md` grammar document (`HOG.md`, `TDX.md`,
  `COL.md`, `VUM.md`, `POP.md`, `SO.md`, `LPD.md`, `PAR.md`, `VAG.md`,
  `VPK.md`, `SKAS.md`, `ASSET-RECON.md`, `FRONTEND-TOPOLOGY.md`) mentions
  `.elf`. This is expected for a standard executable container and
  confirms no asset-format author believed `.elf` belonged to their
  grammar.
- No ledger entry (`E-####`/`R-####`) names any of the four `.elf` files
  directly, unlike `SCUS_972.64` which has three (`E-0002`, `E-0004`,
  `E-0006`) plus loader-trace entries. A privacy-safe collection would add
  a ledger entry recording only the aggregate ELF-header fields (per
  H1's confirmation path above) for the four files as a group.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`**

Justification: ELF is a standard, well-defined executable/object-file
container format (the same class this repository's own tracked evidence,
ledger `E-0004`, already identifies for the disc's main boot binary), not
a bespoke game-asset grammar. The four `.elf` files have zero occurrences
inside any HOG archive (recursive or top-level), are not referenced by
name in any published asset-format grammar document, and no entry in
`tools/fingerprint_assets.py`'s `FORMAT_HANDLERS`, no header in
`native/include/omega/retail/*.h` or `native/include/omega/asset/*.h`,
and no `CMakeLists.txt` target treats `.elf` as a decodable/registered
asset format. This mirrors the `.64` dossier's precedent exactly: the
project's existing ELF-inspection tooling (`tools/inspect_ps2_elf.py`)
already covers the standard-format structural level (program headers,
sections, entry point) as part of startup/loader research, not the asset
fingerprinting pipeline, so treating these four files as executables
rather than game-data assets carries no risk of invented semantics — it
follows directly from the ELF format's own well-known structural
definition, the same definition already applied to `SCUS_972.64`
elsewhere in this repository's tracked evidence.

## 8. Codex work order

1. **(Highest priority) No new asset decoder or fingerprint-handler work
   for `.elf`.** Do not add `.elf` to `tools/fingerprint_assets.py`
   `FORMAT_HANDLERS` or to any `native/include/omega/asset/*.h`
   descriptor set — the tracked evidence classifies this suffix as a
   standard executable container, and adding asset-decoder machinery for
   it would be unjustified scope creep with no supporting grammar.
2. Run `tools/inspect_ps2_elf.py` (already used for `SCUS_972.64` per
   ledger `E-0004`) against the four `.elf` files as a group and publish
   only the resulting aggregate structural counts (program-header count,
   section count, entry point, endianness/machine fields — no section
   names, no code/string bytes) as a new ledger entry, to test H1 and
   convert it from Hypothesis to Confirmed or Rejected.
3. Re-run the existing recursive/top-level HOG suffix scans
   (`tools/fingerprint_assets.py`, whatever produces
   `hog-validation.json`) after any future disc re-extraction to confirm
   H2 (no `.elf` ever appears inside a HOG archive) still holds; record
   the result as a new ledger entry rather than assuming permanence.
4. If a future dossier author wants to characterize the top-level
   container's role, do so only via aggregate, privacy-safe means (e.g.
   publishing the container's declared byte total and file count, both
   already present in `disc-summary.json`) — do not infer or state a
   functional role (network setup, demo, etc.) without a mechanically
   citable tracked source, per the clean-room conservatism rule.
