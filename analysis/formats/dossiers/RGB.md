# .rgb — Format Dossier (System-File Stub)

## 1. Identity

`.rgb` is a suffix observed **only** in the tracked whole-disc file
histogram, never inside a HOG archive. At the evidence level this dossier
can defend, `.rgb` is **a whole-disc-only file suffix, co-located exclusively
with a cluster of PS2 boot/IOP-module suffixes under a single non-game
top-level container, with no HOG presence, no structural handler, no native
decoder, and no ledger entry**. Aggregate container composition (§3, C7)
places it outside the game's asset vocabulary entirely. Per the task's
system-file gate, this stub classifies `.rgb` as
**`system_file_out_of_scope`** rather than authoring a full asset dossier.
No image/pixel-format semantic, palette role, or menu/background role is
asserted anywhere below — any such reading would be an invented semantic
and is out of scope.

## 2. Occurrence evidence

| Count | Value | Tracked source |
|---|---|---|
| Recursive-in-HOG suffix count | 0 | `analysis/formats/asset-fingerprints.json` (`scan.extensions`) — no `.rgb` key present |
| Top-level-HOG member-suffix count | 0 | `analysis/formats/hog-validation.json` (`entry_extensions`) — no `.rgb` key present |
| Whole-disc occurrence count | 3 | `analysis/manifests/disc-summary.json` (`extensions[".rgb"]` = 3), corroborated by `analysis/manifests/disc-files.jsonl` |

All 3 whole-disc occurrences sit under the top-level container `NETGUI`, a
bucket key already present in `analysis/manifests/disc-summary.json`
(`top_level_bytes.NETGUI` = 30306038 bytes), sibling to the disc's other
top-level containers (`GAMEDATA`, `IRX`, `ZMEDIA`, plus root-level
`OVL_DNAS.BIN`, `SCUS_972.64`, `SFO_GAME.INI`, `SYSTEM.CNF`). `.rgb` never
appears inside a HOG archive (recursive and top-level counts are both 0),
so it is exclusively a whole-disc, non-archived file — which is what
triggers the system-file classification check for this suffix.

## 3. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| C1 | `.rgb` is absent from the recursive HOG-containment scan's extension table. | `analysis/formats/asset-fingerprints.json` (`scan.extensions`) |
| C2 | `.rgb` is absent from the top-level HOG-validation extension table. | `analysis/formats/hog-validation.json` (`entry_extensions`) |
| C3 | `.rgb` is absent from `FORMAT_HANDLERS` — the only suffixes with a registered structural handler are `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`. | `tools/fingerprint_assets.py` |
| C4 | No header, source file, or CMake target anywhere in the tracked native tree names `.rgb`, an "Rgb"-typed asset, or RGB-file parsing. A repo-wide search of `native/include`, `native/src`, and `CMakeLists.txt` for `.rgb`/`RGB`-file handling returns no hit; the only `RGB`-adjacent identifiers found (`RGBA8`/`RGBA` in `analysis/formats/COL.md`, `analysis/formats/TDX.md`, and native debug-image source) describe internal owned pixel-buffer formats produced by unrelated debug tooling (e.g. the wireframe contact-sheet image), not the `.rgb` file suffix. | `native/include/`, `native/src/`, `CMakeLists.txt` (absence confirmed by search); `analysis/formats/COL.md`; `analysis/formats/TDX.md` |
| C5 | No entry in the evidence ledger names `.rgb` specifically; the only `RGB`-adjacent ledger entries (e.g. `E-0025`, `E-0044`) concern an unrelated native debug-image RGBA8 pixel buffer, not the `.rgb` file suffix or its content. | `analysis/evidence/ledger.jsonl` |
| C6 | The 3 whole-disc `.rgb` occurrences are located under the top-level container `NETGUI`, distinct from the game's primary asset container `GAMEDATA`. | `analysis/manifests/disc-summary.json` (`top_level_bytes`); `analysis/manifests/disc-files.jsonl` |
| C7 | The full suffix composition of the `NETGUI` container (aggregate extension counts over its member paths, no per-file rows) is: `.BD`×1, `.BIN`×21, `.CNF`×1, `.ELF`×4, `.HD`×1, `.ICO`×1, `.IMG`×1, `.IRX`×21, `.LOG`×1, `.PF`×3, `.RGB`×3, `.SCC`×6, `.SYS`×1, `.TM2`×16, `.TXT`×1. None of the game's primary HOG-scanned asset-format suffixes (`.col`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.tdx`, `.vag`, `.vum`, `.hog`, etc.) appear anywhere in this container. | `analysis/manifests/disc-files.jsonl` (aggregate count over `NETGUI/*` paths) |

## 4. Aggregate-only facts

Tracked aggregates only, no semantic interpretation attached.

- Whole-disc `.rgb` file count: **3**. (`analysis/manifests/disc-summary.json`)
- Aggregate size multiset for the 3 `.rgb` files (unordered, not tied to any individual file/path): **{860160, 983040, 1146880} bytes**. Minimum = 860160, maximum = 1146880, sum = 2990080. (`analysis/manifests/disc-files.jsonl`)
- The `NETGUI` container's own aggregate byte total is 30306038 bytes across 448-file-histogram-scoped disc contents; `.rgb` accounts for 2990080 of those bytes (≈9.9% of the container by byte count, 3 of its ~83 member files by the extension tally in C7). (`analysis/manifests/disc-summary.json`, `disc-files.jsonl`)
- `.rgb`'s co-located extensions in the same container (`.BIN`, `.CNF`, `.ELF`, `.IRX`, `.SYS`) are extensions independently associated elsewhere on this disc with PS2 boot/IOP-module content: `.IRX` (IOP relocatable executable modules) and `.CNF` (boot configuration, cf. root-level `SYSTEM.CNF`) are disc-wide top-level extension buckets in `analysis/manifests/disc-summary.json`; `.ELF` is the PS2 executable extension (cf. root-level `SCUS_972.64`, a PS2 boot-executable-style top-level entry). No tracked source supplies IRX/ELF/CNF *semantics* beyond their bare extension identity — this bullet reports co-occurrence counts only, not file roles.

## 5. Hypotheses

Explicitly labeled; none are asserted as fact above.

- **H1 — `.rgb` is content belonging to a self-contained, non-game-asset network/boot utility stack, not a game asset processed by the OpenOmega asset pipeline.** This is the basis for the §7 classification. *Confirming observation:* the aggregate composition in C7/§4 already shows `.rgb` co-located exclusively with `.IRX`/`.ELF`/`.CNF`/`.SYS`/`.BIN` — a suffix set disjoint from every HOG-scanned game-asset suffix — and total absence from HOG archives, `FORMAT_HANDLERS`, and the native decoder tree, which is the strongest form of confirmation available from tracked, privacy-safe sources alone. *Refuting observation:* a future tracked artifact showing the OpenOmega runtime or asset pipeline actually opens/loads a `NETGUI`-container path or `.rgb`-suffixed content at runtime (no such reference exists anywhere in `native/` today, per C4).
- **H2 — the 3 `.rgb` files are raw uncompressed image buffers used by the network-utility UI (e.g. a background/palette pair) rather than any other binary structure.** This is suggested only by the "rgb" token itself, which is not evidence, and is explicitly NOT relied upon for the §7 classification. *Confirming observation:* a privacy-safe, path-free structural probe (mirroring existing `FORMAT_HANDLERS` conventions) reporting only aggregate size-divisibility/stride statistics for the 3 files (e.g., whether sizes are integer multiples of a small pixel-stride candidate) run against the owner corpus, publishing only the aggregate result. *Refuting observation:* the same probe showing no consistent stride/divisibility pattern across the 3 sizes.

## 6. Missing observations

Tracked evidence that does not exist, and the privacy-safe collection that would produce it:

- **No structural/header probe for `.rgb`.** No `FORMAT_HANDLERS` entry exists, so no header-magic or field-layout aggregate is available. *Collection:* if a future task deliberately revisits this classification, author a bounded, aggregate-only structural handler (mirroring existing ones) and run it against the owner corpus, committing only the aggregate.
- **No ledger entry.** No `E-####` entry names `.rgb`. *Collection:* if the §7 classification is formally adopted, record a ledger entry citing this dossier and the aggregate sources in §2–4.
- **No confirmation that the OpenOmega runtime ever loads `NETGUI`-container content.** Tracked native source is silent on this container entirely. *Collection:* a repo-wide, privacy-safe grep sweep (already partially performed for this dossier, see C4) re-run after any future native code additions, to catch a change in that status.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`**

- No structural handler (`tools/fingerprint_assets.py` `FORMAT_HANDLERS`), no passive descriptor, and no native decoder (`native/include/`, `native/src/`, `CMakeLists.txt`) exists for `.rgb` anywhere in tracked source (C3, C4).
- `.rgb` is never observed inside a HOG archive (recursive and top-level counts both 0, §2) — it is exclusively a whole-disc file, counted only by the generic whole-disc inventory/manifest generator that produced `analysis/manifests/disc-summary.json` and `disc-files.jsonl`. That generator is a suffix-agnostic file-histogram tool, not a format-aware scanner, so this family does not qualify as `aggregate_scanner_only` in the same sense as HOG-scanned suffixes.
- The aggregate container-composition evidence in C6/C7 (exclusive co-location with `.IRX`/`.ELF`/`.CNF`/`.SYS`/`.BIN` under a `NETGUI` top-level container disjoint from every game-asset suffix and from `GAMEDATA`) is the basis for this classification: it places `.rgb` in a non-game, boot/network-utility-adjacent top-level container rather than the game's asset corpus. This is an evidence-based judgment applied to Confirmed aggregate facts, not a claim any single tracked file states in prose — no tracked source explicitly labels `NETGUI` as a "PS2 network boot stack," and that residual gap is recorded as H1/§6.
- No adversarial/resource-boundary test exists for `.rgb` because no decoder/descriptor exists to test, and per this classification none is planned (§8).

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. No menu-role or other semantic speculation is included.

1. **Highest priority — do not build a decoder, descriptor, or CMake target for `.rgb`.** The aggregate container-composition evidence (C6/C7) places it outside the game-asset corpus this project decodes; building a decoder ahead of any tracked evidence that the OpenOmega runtime consumes `NETGUI`-container content would be exactly the "plausible invented decoder" regression the clean-room rules forbid.
2. **Record a ledger entry** capturing this dossier's classification and its evidence chain (C1–C7), following the ledger's existing `E-####` citation convention, so the `aggregate_scanner_only`-vs-`system_file_out_of_scope` distinction for this suffix is not re-litigated from scratch later.
3. **If the H1 refuting condition is ever met** (a future tracked artifact shows native code opening `NETGUI`/`.rgb` content), re-open this dossier as a full 8-section asset dossier rather than a stub, and re-classify decoder status accordingly.
4. **Low priority, only if explicitly requested** — run a bounded, aggregate-only size-stride probe (H2) against the owner corpus to settle whether the 3 files are pixel-buffer-shaped, purely for completeness; this does not change the §7 classification either way and should not block or precede items 1–2.
