# .IRX — Dossier (stub: system_file_out_of_scope)

**Identity (evidence-level):** A whole-disc-only suffix (46 occurrences, 0 inside any `.hog` container at any depth) whose instances sit exclusively under two top-level disc directories — `IRX` and `NETGUI` — that are structurally distinct from the game-content directories (`GAMEDATA`, `ZMEDIA`) which host the `.hog` archives covered by the rest of this dossier family. This directory-level placement, alongside other top-level boot/system entries (`OVL_DNAS.BIN`, `SCUS_972.64`, `SFO_GAME.INI`, `SYSTEM.CNF`) in the same tracked manifest, is the basis for treating `.irx` as a PS2 system/IOP-module family rather than a game asset, and for scoping this dossier as a stub per task instructions rather than a full grammar dossier.

## 1. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts) |
| Whole-disc (anywhere on the extracted disc tree) | 46 | `analysis/manifests/disc-summary.json`, key `extensions["\.irx"] = 46`; corroborated by 46 matching rows in `analysis/manifests/disc-files.jsonl` |

`.irx` is therefore a whole-disc-only suffix that never appears packed inside any `.hog` container, recursive or top-level — the same occurrence pattern as other confirmed PS2 system/boot artifacts already dossiered in this family (e.g. `.bd`), but at a much higher whole-disc count (46 vs. 1).

Aggregate positional breakdown (computed by grouping the 46 matching `disc-files.jsonl` rows by their top-level directory component, with no filename, path-suffix, size, or hash disclosed per row): 25 occurrences sit under the top-level directory `IRX`, and 21 sit under the top-level directory `NETGUI`. Both are top-level entries already named in `disc-summary.json`'s `top_level_bytes` block (`"IRX": 1415431` bytes; `NETGUI` present with its own aggregate byte total) alongside the disc's other named top-level containers (`GAMEDATA`, `ZMEDIA`, `OVL_DNAS.BIN`, `SCUS_972.64`, `SFO_GAME.INI`, `SYSTEM.CNF`). No filenames within either directory are disclosed here, consistent with the clean-room rule against per-file/member names beyond generic container names already present in tracked docs.

## 2. Confirmed facts

None beyond the occurrence counts and directory-grouping aggregate in §1. Checked and absent:

- `tools/fingerprint_assets.py` — `FORMAT_HANDLERS` contains only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`. `.irx` is **not** a key — no structural handler exists.
- `analysis/formats/*.md` — no published grammar document exists for `.irx` (directory contains `ASSET-RECON.md`, `COL.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SKAS.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md`, `FRONTEND-TOPOLOGY.md`; none mention `.irx`).
- `analysis/evidence/ledger.jsonl` — no `E-####` entry's claim text concerns `.irx`.
- `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, `native/src/**/*.cpp` — no `.irx`-related type, descriptor, decoder, or comment found by targeted search.
- `CMakeLists.txt` — no `.irx`-related source, test, or target registration.

No tracked source defines an `.irx` header magic, grammar, field layout, or semantic role (e.g. "IOP module," "boot loader," "network stack component"). Any such label would be drawn from general PS2-devkit convention, not from this repo's tracked evidence, and is therefore withheld from Confirmed status.

## 3. Aggregate-only facts

- `analysis/manifests/disc-summary.json`: whole-disc extension histogram records `.irx: 46` out of `file_count: 448`, the second-highest multi-count game/system extension after `.hog` (273) in the same table (followed by `.bin` 22, `.dat` 20, `.pop` 18, `.tm2` 16).
- `analysis/manifests/disc-files.jsonl`: aggregate size statistics across the 46 whole-disc `.irx` rows — minimum 3,123 bytes, maximum 171,884 bytes, mean ≈36,675 bytes. No individual size is attributed to a named file.
- Directory-grouping aggregate (computed from the same 46 rows, no filenames disclosed): 25 rows under top-level directory `IRX`, 21 rows under top-level directory `NETGUI`. Zero rows under `GAMEDATA` or `ZMEDIA`, the two top-level directories that host the `.hog` archives analyzed elsewhere in this dossier family.
- `analysis/formats/asset-fingerprints.json`: no per-format aggregate block exists for `.irx` (only `FORMAT_HANDLERS` suffixes receive a structural aggregate section). This confirms `.irx` is scan-visible (counted) but never structurally opened by the fingerprinting tool.
- `tools/fingerprint_assets.py`, `scan_asset()`: the generic extension tally and `compression_magic(head)` sniff apply to `.irx` the same as to every other unhandled suffix — a passive count/compression-sniff, not a format-specific decode.

## 4. Hypotheses

- **H1 — PS2 IOP-module system family.** `.irx` denotes IOP (I/O processor) relocatable executable modules, a standard PS2 SDK file family, consistent with: (a) zero HOG-packed occurrences, (b) exclusive placement under two top-level directories (`IRX`, `NETGUI`) that sit alongside other boot/system top-level entries (`OVL_DNAS.BIN`, `SCUS_972.64`, `SYSTEM.CNF`) rather than the content directories (`GAMEDATA`, `ZMEDIA`), and (c) a size range (≈3 KB–172 KB) consistent with small loadable modules rather than bulk game assets. This is the basis for this dossier's `system_file_out_of_scope` classification, but the specific semantic label "IOP module" is not itself sourced from any tracked repo document — it is general, publicly documented PS2-devkit terminology, not evidence mined from this codebase.
  - *Privacy-safe confirmation/refutation:* add a passive aggregate descriptor (per §5/§6) that reports only aggregate header-byte histograms across the 46 instances; if a consistent magic/header pattern matching the publicly documented ELF-like IOP-module header shape emerges in aggregate (without per-file disclosure), that would move this from Hypothesis toward Aggregate-only-confirmed. No such check has been run in the tracked corpus to date.
  - **NETGUI-grouped instances (21 of 46) may represent a distinct sub-role** (e.g. network-stack IOP modules) from the `IRX`-grouped instances (25 of 46), given the directory-name split — but this repo's tracked evidence establishes only the directory-grouping count, not any functional distinction. This is a sub-hypothesis of H1.

## 5. Missing observations

- **No structural handler for `.irx` exists** in `tools/fingerprint_assets.py`. No header fields, magic bytes, or internal layout have been mechanically extracted or aggregated for this suffix.
- **No published grammar doc** covers `.irx`, so even a generic PS2 IOP-module header shape has not been cross-checked against this corpus's instances in any tracked document.
- **No ledger entry** records a confirmed or rejected claim about `.irx`.
- **No native decoder/descriptor or CMake/test registration** references `.irx`.
- No aggregate magic-byte histogram, alignment-bucket, or compression-hit breakdown specific to `.irx` has been computed and stored in `analysis/formats/asset-fingerprints.json` — only the raw count and directory-grouping aggregate derived ad hoc for this dossier exist.

## 6. Decoder/tooling status

**Classification: `system_file_out_of_scope`**

- Justification: per the task framing, a suffix with (a) 0 recursive/top-level HOG occurrences, (b) exclusive placement in top-level directories separate from the game-content directories, and (c) a size profile consistent with small loadable system modules is scoped as a PS2 system/OS file family, out of scope for this project's game-asset decoder work. This mirrors the disc's other top-level system/boot entries (`OVL_DNAS.BIN`, `SCUS_972.64`, `SYSTEM.CNF`, `SFO_GAME.INI`) which are likewise not game-asset formats requiring a `native/` decoder.
- Also independently satisfies `aggregate_scanner_only` at the tooling level (§3): the only mechanical processing `.irx` receives today is the generic extension-histogram tally and compression-magic sniff in `scan_asset()`; no `FORMAT_HANDLERS` entry, no `native/` decoder or descriptor type, and no CMake/CTest target references it.
- No adversarial or resource-boundary test exists for `.irx` because no decoder exists to test, and none is expected given the system-file scoping above.

## 7. Codex work order

Ranked, privacy-safe, no semantic speculation beyond §4's labeled hypotheses:

1. **Confirm the `system_file_out_of_scope` scoping decision in project docs** (e.g. a one-line note in `analysis/formats/ASSET-RECON.md` or an equivalent index) so future dossier passes don't re-open `.irx` as a candidate game-asset format without cause.
2. **Optionally add a passive aggregate-only descriptor for `.irx`** in `tools/fingerprint_assets.py` (head-byte histogram bucket, size bucket, 2048-byte-alignment flag, compression-magic hit/no-hit, and the `IRX`-vs-`NETGUI` directory-grouping split already used in §3) if a future pass wants to test H1's magic-pattern prediction — emit aggregates only, never per-file identifiers. This is optional given the out-of-scope classification, and lower priority than the game-asset families still short a decoder.
3. **Do not** build a `native/` decoder or `FORMAT_HANDLERS` entry for `.irx` under the current scoping; doing so would be effort spent on a system-file family outside this project's game-asset reimplementation goal.
4. **Do not** attempt to open, name, or hash-fingerprint any individual `.irx` instance beyond the aggregate counts/statistics already in `disc-summary.json`/`disc-files.jsonl`; do not access `private/`, `runtime/`, or `third_party/` in pursuit of additional samples.
5. **If a later, differently-sourced tracked corpus is added,** re-run the directory-grouping and size-range aggregate in §3 against the combined corpus to see whether the `IRX`/`NETGUI` split and size profile hold, strengthening or weakening H1 without any per-file disclosure.
