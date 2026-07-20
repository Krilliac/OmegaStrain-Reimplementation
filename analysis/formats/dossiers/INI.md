# .INI — System-File Stub

**One-line identity:** A single top-level disc file (generic container name only, already present in tracked manifests) that sits alongside the PS2 boot-configuration file and kernel-module directory rather than inside any HOG archive; the tracked evidence places it as PS2 disc/publishing metadata, not a game asset, so this dossier is scoped as `system_file_out_of_scope` rather than a full format grammar.

## 1. Scope classification

`system_file_out_of_scope`. Rationale below.

## 2. Occurrence evidence

| Metric | Count | Source |
|---|---|---|
| Recursive-in-HOG occurrences | 0 | `analysis/formats/asset-fingerprints.json` (no `.ini` key in the recursive suffix inventory) |
| Top-level-HOG member-suffix occurrences | 0 | `analysis/formats/hog-validation.json` (no `.ini`/`INI` entries among top-level HOG member suffixes) |
| Whole-disc occurrences | 1 | `analysis/manifests/disc-summary.json` (`"extensions": {".ini": 1, ...}`) and `analysis/manifests/disc-files.jsonl` (single matching row) |

The single whole-disc `.ini` file is recorded at disc top level in `analysis/manifests/disc-summary.json`'s `top_level_bytes` map with a size of 385 bytes, alongside these other top-level entries: a `.64` boot executable, `SYSTEM.CNF` (PS2 boot configuration, 58 bytes), `OVL_DNAS.BIN`, an `IRX` directory (kernel/IOP modules), `GAMEDATA` (the HOG-bearing tree), `NETGUI`, and `ZMEDIA`. No second `.ini` file exists anywhere else in the tracked manifests — this format never recurs inside `GAMEDATA` or any HOG container.

## 3. Confirmed facts

| # | Fact | Tracked source |
|---|---|---|
| 1 | Exactly one `.ini`-suffixed file exists on the whole disc; zero occur inside HOG archives (recursive or top-level). | `analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json` |
| 2 | The file's disc-manifest size is 385 bytes. | `analysis/manifests/disc-summary.json` (`top_level_bytes`), `analysis/manifests/disc-files.jsonl` |
| 3 | The file is a top-level disc entry, co-located with `SYSTEM.CNF` (PS2 boot configuration), the boot `.64` executable, `OVL_DNAS.BIN`, and the `IRX` module tree — not under `GAMEDATA` where every HOG archive lives. | `analysis/manifests/disc-summary.json` (`top_level_bytes` map) |
| 4 | No entry for `.ini` exists in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` — no structural handler is registered for this suffix. | `tools/fingerprint_assets.py` |
| 5 | No `.ini`-related grammar or claim exists in any published `analysis/formats/*.md` document (grep across all format docs returns no `.ini` hits outside unrelated substrings). | `analysis/formats/*.md` |
| 6 | No `analysis/evidence/ledger.jsonl` entry (confirmed or rejected) references this suffix. | `analysis/evidence/ledger.jsonl` |
| 7 | No native decoder, descriptor, or CMake/test registration references `.ini`/INI parsing anywhere in `native/include`, `native/src`, or `CMakeLists.txt`. | `native/include/**`, `native/src/**`, `CMakeLists.txt` |

## 4. Aggregate-only facts

- The disc-wide extension histogram in `analysis/manifests/disc-summary.json` records `.ini: 1` out of `file_count: 448` total disc files and `total_bytes: 3455648408` total disc bytes — a single-instance suffix, statistically negligible against the corpus.
- The file's 385-byte size is small relative to every other top-level disc entry except `SYSTEM.CNF` (58 bytes); both are far smaller than the boot executable, `OVL_DNAS.BIN`, or any HOG archive, consistent with (but not proof of) a short text/config payload.
- No size range, bucket distribution, or alignment observation is available beyond this single data point — aggregate statistics that normally come from `asset-fingerprints.json`'s per-format buckets do not exist for `.ini` because it was never picked up by the recursive HOG suffix inventory (occurrence count 0 there).

## 5. Hypotheses

- **H1 (system boot/publishing metadata):** The file is PS2 disc-authoring or publishing metadata (e.g., a build-tool-generated configuration or an ELF/SFO-adjacent descriptor), analogous in role to `SYSTEM.CNF`, and not consumed by the OpenOmega game engine at runtime. *Confirm/refute:* a privacy-safe, aggregate structural scan (magic-byte histogram at the container level, or a generic text/binary classifier count) run against the owner corpus and recorded in a new `analysis/formats/*.md` or `asset-fingerprints.json` entry, without publishing any payload bytes or the file's own content.
- **H2 (single-instance, non-recurring artifact):** Because occurrence count is exactly 1 disc-wide and 0 inside every HOG, the format is disc-tooling/publishing scaffolding rather than a reusable in-game asset family (unlike `.tdx`, `.vum`, etc., which recur by the hundreds inside `GAMEDATA`). *Confirm/refute:* re-run the same recursive/top-level suffix inventory against any additional owner-supplied disc image or patch/update image (if one becomes tracked) to see whether `.ini` recurs; a second observed instance would weaken H2.

## 6. Missing observations

- No aggregate byte-histogram or line/structure count for the file's *contents* exists in any tracked source — only its name, size, and top-level disc placement are recorded. Producing a privacy-safe collection (e.g., a generic "is this ASCII/UTF-8 text vs. binary" classifier flag, or a leading-magic-bytes check reported only as a boolean/aggregate, never as raw bytes) would let a future dossier revision move from Missing-observation to Aggregate-only without violating the path-free/no-payload-bytes constraint.
- No cross-reference exists confirming or ruling out whether `SYSTEM.CNF` (the confirmed PS2 boot config, also top-level) references this `.ini` file by name as a boot-time dependency. `SYSTEM.CNF`'s own contents are not published in any tracked source, so this cannot currently be checked without opening private/untracked material.
- No native or third-party PS2 SDK documentation is checked into this repository (per clean-room rules, none may be), so no external corroboration of "SFO"-style naming conventions is admissible evidence here — any such claim would be Hypothesis at best, not Confirmed.

## 7. Decoder/tooling status

**Classification: `system_file_out_of_scope`.**

Justification: the tracked evidence places this single `.ini` file at disc top level, grouped with the PS2 boot executable, `SYSTEM.CNF`, `OVL_DNAS.BIN`, and the `IRX` kernel-module tree — none of which are game-asset containers and none of which are reached by the HOG-recursive asset pipeline that `tools/fingerprint_assets.py` and the native `omega/asset` decoders target. Corroborating the classification:

- `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` registers structural handlers only for `tdx/ska/skas/skm/skl/vag/lpd/par/col/vum/vpk` — no `.ini` entry exists, meaning the project's own fingerprinting tool does not treat this suffix as a game-asset family worth structurally decoding.
- No file under `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, or `native/src/**/*.cpp` declares or implements any INI-related descriptor, decoder, or parser, and `CMakeLists.txt` registers no corresponding source or test target.
- Occurrence is a disc-root singleton (count 1) with zero recurrence inside any HOG archive, matching the profile of a per-disc build/publishing artifact rather than a reusable engine asset format (contrast with `.tdx`/`.vum`/`.pop`, which recur across dozens to hundreds of HOG members and do have registered handlers).

No adversarial or resource-boundary test gap applies, since no decoder exists to test.

## 8. Codex work order

Ranked, privacy-safe, no menu-role or semantic speculation:

1. **Highest priority — aggregate structural probe.** Extend the existing whole-disc manifest tooling (the same class of tool that produced `analysis/manifests/disc-summary.json`) to emit, for this one file only, a privacy-safe aggregate content classification (ASCII/UTF-8-text vs. binary flag, or a leading-magic-bytes-present boolean) and record the result as a new row in `analysis/formats/*.md` — no payload bytes, no per-line content, no path beyond the already-published generic container name.
2. **Cross-corpus recurrence check.** If any additional owner-tracked disc image, patch, or update image becomes available under the existing manifest pipeline, re-run the recursive/top-level suffix inventory (`asset-fingerprints.json`, `hog-validation.json` equivalents) to determine whether `.ini` ever recurs outside a single top-level instance; record the updated occurrence counts here regardless of outcome.
3. **Ledger entry.** Once (1) produces a citable aggregate fact, add a confirmed/rejected entry to `analysis/evidence/ledger.jsonl` for the `.ini` classification decision itself (system-file vs. asset), so future dossier passes have a stable citation instead of re-deriving this stub's reasoning each time.
4. **Do not** add a `FORMAT_HANDLERS` entry, native decoder, or descriptor for `.ini` unless step 1's aggregate probe or a future ledger entry establishes it is consumed by the game engine rather than being disc-authoring/publishing scaffolding — building a decoder ahead of that evidence would be an invented-semantics regression.
