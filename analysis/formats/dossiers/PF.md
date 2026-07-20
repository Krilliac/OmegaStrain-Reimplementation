# .PF — System-File Stub

**One-line identity:** A rare, whole-disc-only suffix (3 instances, zero
HOG occurrences) co-located under a top-level disc directory alongside
IOP-module, boot-executable, and boot-icon file types; tracked evidence
places it in the PS2 boot/network-support portion of the disc rather than
the HOG-based game-asset pipeline, so this dossier is scoped as
`system_file_out_of_scope` rather than a full format grammar.

## 1. Scope classification

`system_file_out_of_scope`. Rationale below.

## 2. Occurrence evidence

| Metric | Count | Source |
|---|---|---|
| Recursive-in-HOG occurrences | 0 | `analysis/formats/asset-fingerprints.json` (no `.pf`/`pf` key in the recursive suffix inventory or the per-format `formats` table) |
| Top-level-HOG member-suffix occurrences | 0 | `analysis/formats/hog-validation.json` (no `.pf` entry among top-level HOG member suffixes) |
| Whole-disc occurrences | 3 | `analysis/manifests/disc-summary.json` (`"extensions": {".pf": 3, ...}`) and `analysis/manifests/disc-files.jsonl` (three matching rows) |

The three whole-disc `.pf` files are all recorded under the same top-level
disc directory named in `analysis/manifests/disc-summary.json`'s
`top_level_bytes` map — a directory whose aggregate byte total there is
30,306,038 bytes and which also contains, per that same top-level map and
per the pre-existing note in `analysis/formats/dossiers/GUI.md` (§2), files
of type `.RGB`, `.BIN`, `.IRX`, `.ELF`, `.TM2`, and `.ICO` — i.e. a PS2
IOP-module / boot-executable / icon mix, not a HOG-bearing asset tree.
Individual `.pf` filenames are withheld from this dossier per the
path-free / no-member-names policy; only the generic container directory
name already published in `analysis/manifests/disc-summary.json` and
`analysis/formats/dossiers/GUI.md` is used above.

One additional literal-substring hit for `PF` appears in
`analysis/formats/hog-headers.jsonl` (line 110, inside the ASCII dump of a
`MAPVUM.HOG` header), but this is a false positive: it is two characters
inside unrelated binary/text bytes of a `.HOG` header (surrounded by
`MODEL_A01.HOG` text), not a `.pf`-suffixed member entry, and it is
excluded from the counts above.

## 3. Confirmed facts

| # | Fact | Tracked source |
|---|---|---|
| 1 | Exactly three `.pf`-suffixed files exist on the whole disc; zero occur inside HOG archives (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json`, `analysis/manifests/disc-summary.json` |
| 2 | All three `.pf` files sit under the same top-level, non-`GAMEDATA` disc directory that also holds `.RGB`, `.BIN`, `.IRX`, `.ELF`, `.TM2`, and `.ICO` files — i.e. outside the HOG-containment tree entirely. | `analysis/manifests/disc-summary.json` (`top_level_bytes`); `analysis/formats/dossiers/GUI.md` (§2 note) |
| 3 | No entry for `.pf` exists in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict (registered keys: `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`) — no structural handler is registered for this suffix. | `tools/fingerprint_assets.py` (lines ~499–511) |
| 4 | No `.pf`-related grammar or claim exists in any published `analysis/formats/*.md` document; the only tracked-doc mention of `PF` is the parenthetical container-inventory note in `GUI.md` §2, which explicitly states that note is "not evidence about `.gui`" and does not assert any `.pf` grammar or semantics. | `analysis/formats/*.md` (grep across all format docs) |
| 5 | No `analysis/evidence/ledger.jsonl` entry (confirmed or rejected) references this suffix. | `analysis/evidence/ledger.jsonl` |
| 6 | No native decoder, descriptor, or CMake/test registration references `.pf`/`PF` parsing anywhere in `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp`, or `CMakeLists.txt`. | `native/include/**`, `native/src/**`, `CMakeLists.txt` (absence confirmed by search) |
| 7 | The one literal `PF` substring hit outside disc-summary data (`analysis/formats/hog-headers.jsonl` line 110) occurs inside a `MAPVUM.HOG` header ASCII dump, not as a `.pf` file entry, and was checked and ruled a false positive. | `analysis/formats/hog-headers.jsonl` (line 110) |

## 4. Aggregate-only facts

- The disc-wide extension histogram in `analysis/manifests/disc-summary.json` records `.pf: 3` out of `file_count: 448` total disc files and `total_bytes: 3,455,648,408` total disc bytes — a rare suffix, statistically negligible against the whole-disc corpus.
- Per-file sizes recorded in `analysis/manifests/disc-files.jsonl` for the three `.pf` rows span roughly 637 KB to 4.25 MB (three distinct sizes, no two identical) — reported here only as an aggregate min/max range, with no per-file row, filename, or hash reproduced per the path-free policy.
- All three sizes are well below the top-level directory's own aggregate byte total (30,306,038 bytes) recorded in `disc-summary.json`'s `top_level_bytes` map, meaning `.pf` files account for a partial (not dominant) share of that directory's bytes; the remainder belongs to the sibling `.RGB`/`.BIN`/`.IRX`/`.ELF`/`.TM2`/`.ICO`/etc. files already inventoried by that same directory's tracked extension counts.
- No size-bucket distribution, alignment observation, or magic-byte record exists for `.pf` in `analysis/formats/asset-fingerprints.json`, because that tool's structural scan only ever runs over HOG-tree content and registered `FORMAT_HANDLERS` extensions — `.pf` is picked up by neither, so no structural aggregate beyond the three raw sizes above is available.

## 5. Hypotheses

- **H1 (PS2 network/boot-support resource, non-game-asset):** Given its exclusive co-location with IOP-module (`.IRX`), boot-executable (`.ELF`/`.BIN`), and boot-icon (`.ICO`) files under the same non-`GAMEDATA` top-level directory (per Confirmed-fact 2), `.pf` is PS2 platform/network-boot support data (e.g., a font/profile/parameter resource consumed by the network-boot stack or its front-end) rather than a title-runtime game asset consumed by the OpenOmega engine's HOG-based asset pipeline. *Confirm/refute:* a privacy-safe, aggregate structural probe (leading-magic-bytes classification, or an ASCII/UTF-8-vs-binary flag, reported only as an aggregate count across the three files — never per-file) run against the owner corpus and recorded in a new `analysis/formats/*.md` entry, without publishing any payload bytes.
- **H2 (single-directory, non-recurring artifact):** Because occurrence is exactly 3, all within one non-`GAMEDATA` directory, and 0 inside every HOG, `.pf` is scoped to that directory's boot/network subsystem rather than a reusable in-game asset family (unlike `.tdx`/`.vum`/`.pop`, which recur by the dozens to hundreds inside `GAMEDATA` and do have registered handlers). *Confirm/refute:* re-run the same recursive/top-level suffix inventory against any additional owner-supplied disc image (if one becomes tracked) to see whether `.pf` ever appears under `GAMEDATA` or inside a HOG; a positive hit there would weaken H2.
- **H3 (per-instance size variation reflects distinct payload roles or locales):** The three `.pf` files carry three distinct sizes (see §4), which could reflect either three functionally distinct resources or three size-varying instances of the same resource type (e.g., differently sized regional/localization variants) within the same support directory. *Confirm/refute:* a privacy-safe aggregate content-similarity check (e.g., whether leading N bytes share a common magic/header pattern across the three files, reported only as a boolean/aggregate match count) would distinguish "same format, different payload size" from "different formats sharing a suffix" without exposing payload bytes or filenames.

## 6. Missing observations

- No aggregate magic-byte, header-structure, or content-classification record exists for any of the three `.pf` files in any tracked source — only suffix, count, top-level directory placement, and raw sizes are recorded. Producing a privacy-safe collection (leading-bytes-present boolean, or a generic binary/text classification flag, emitted in aggregate only) would let a future dossier revision move H1/H3 from Hypothesis to Aggregate-only or Confirmed.
- No tracked source enumerates the full sibling-file set of the top-level directory beyond the extension-count aggregate already in `analysis/manifests/disc-summary.json`. A directory-scoped listing (extensions + counts + sizes only, already the shape of that same summary) would let H1 (co-location with boot/network files) be corroborated at finer grain without exposing individual filenames.
- No tracked source confirms whether `.pf` files recur on other regional/disc variants or only on the single tracked disc image; the current corpus reflects one disc image only. *Privacy-safe collection:* re-run the existing whole-disc manifest generator against any additional tracked disc image and diff the `.pf` occurrence count.
- No native or third-party PS2 SDK documentation is checked into this repository (per clean-room rules, none may be), so no external corroboration of any platform-standard resource-format convention is admissible evidence here — any such claim would remain Hypothesis, not Confirmed.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`.**

Justification: the tracked evidence places all three `.pf` files at the
disc top level, inside a directory whose only other tracked contents are
IOP kernel-module (`.IRX`), boot-executable (`.ELF`/`.BIN`), boot-icon
(`.ICO`), and raw-image (`.RGB`/`.TM2`) files — none of which are
HOG/game-asset containers, and none of which are reached by the
HOG-recursive pipeline that `tools/fingerprint_assets.py` and the native
`omega/asset` decoders target. Corroborating the classification:

- `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` registers structural handlers only for `tdx/ska/skas/skm/skl/vag/lpd/par/col/vum/vpk` — no `.pf` entry exists, meaning the project's own fingerprinting tool does not treat this suffix as a game-asset family worth structurally decoding, and its recursive/HOG-only scan scope means `.pf` is never even visited by that tool (occurrence 0 there is a scope artifact, not evidence of absence elsewhere).
- No file under `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, or `native/src/**/*.cpp` declares or implements any `.pf`-related descriptor, decoder, or parser, and `CMakeLists.txt` registers no corresponding source or test target.
- Occurrence is a small, fixed, directory-scoped set (count 3, all in one non-`GAMEDATA` directory) with zero recurrence inside any HOG archive, matching the profile of platform/network-boot support data rather than a reusable engine asset format (contrast with `.tdx`/`.vum`/`.pop`, which recur across dozens to hundreds of HOG members and do have registered handlers).

No adversarial or resource-boundary test gap applies, since no decoder
exists to test.

## 8. Codex work order

Ranked, privacy-safe, no menu-role or semantic speculation:

1. **Highest priority — aggregate structural probe.** Extend the existing whole-disc manifest tooling (the same class of tool that produced `analysis/manifests/disc-summary.json`) to emit, for the three `.pf` files as a group, a privacy-safe aggregate content classification (leading-magic-bytes-present boolean, or ASCII/UTF-8-vs-binary flag per file, reported only as an aggregate tally — e.g. "2 of 3 share a common leading 4-byte pattern") and record the result as a new row in `analysis/formats/*.md` — no payload bytes, no per-file names beyond the already-published generic container directory name.
2. **Sibling-directory aggregate listing.** Extend the same manifest tooling to emit an extension+count+size-bucket breakdown scoped to the top-level directory that holds the `.pf` files (mirroring the shape already in `disc-summary.json`'s `top_level_bytes`), to corroborate or refute H1's "boot/network support directory" reading at finer grain, still without naming individual files.
3. **Cross-corpus recurrence check.** If any additional owner-tracked disc image, patch, or update image becomes available under the existing manifest pipeline, re-run the recursive/top-level suffix inventory to determine whether `.pf` ever recurs inside `GAMEDATA` or a HOG, or on other disc variants; record the updated occurrence counts here regardless of outcome.
4. **Ledger entry.** Once step 1 or 2 produces a citable aggregate fact, add a confirmed/rejected entry to `analysis/evidence/ledger.jsonl` for the `.pf` classification decision itself (system/boot-support file vs. game asset), so future dossier passes have a stable citation instead of re-deriving this stub's reasoning each time.
5. **Do not** add a `FORMAT_HANDLERS` entry, native decoder, or descriptor for `.pf` unless steps 1–3 establish it is consumed by the game engine's asset pipeline rather than being platform/network-boot support data — building a decoder ahead of that evidence would be an invented-semantics regression.
