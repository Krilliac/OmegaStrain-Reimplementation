# .CNF — PS2 System/IOP Configuration Text File

**One-line identity:** `.cnf` is the PlayStation 2 system-configuration text-file suffix — most
notably `SYSTEM.CNF`, the standard PS2 boot descriptor that every retail disc carries at its root
to tell the BIOS which executable (`BOOT2 = ...`) to load. On this tracked disc layout, `.cnf` also
labels several small root-level text files outside any HOG archive. It is **not** a game/HOG asset
format — no `.cnf` occurrence exists inside any tracked archive.

## 1. Occurrence evidence

| Scope | Count | Source |
|---|---|---|
| Recursive-in-HOG (inside archive payloads, any nesting depth) | 0 | task-provided tracked count (consistent with §3/§4 below — no `.cnf` key appears anywhere in `analysis/formats/asset-fingerprints.json`, which enumerates every handled and unhandled suffix seen recursively) |
| Top-level-HOG (member names of top-level `.HOG` archives) | 0 | task-provided tracked count (consistent with `analysis/formats/hog-validation.json`, whose per-archive `entry_count`/tag records cover only `.HOG` containers and list no `.cnf` members) |
| Whole-disc (anywhere on the disc image, any container) | 8 | `analysis/manifests/disc-summary.json` — top-level extension histogram entry `".cnf": 8` |

`analysis/manifests/disc-summary.json` additionally breaks one specific whole-disc file out of that
histogram by its literal name — `"SYSTEM.CNF": 58` (a 58-byte file) — because `SYSTEM.CNF` is a
disc-root, single-instance, standardized PS2 filename (not an arbitrary game-content member name),
and that same name is independently named in a tracked native-source path (§3). The other 7 of the
8 whole-disc `.cnf` occurrences are not individually named in any tracked aggregate source and are
not enumerated here (per the path-free/no-per-file-row constraint on this dossier); `disc-files.jsonl`
per-file rows exist for them but are out of scope for citation in this document beyond the aggregate
count already given.

## 2. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| 1 | The engine's `GameDataService::Open` treats `SYSTEM.CNF` as a **required** file at the mounted game-data root; a missing `SYSTEM.CNF` produces `GameDataErrorCode::MissingRequiredFile` with message `"game-data root is missing SYSTEM.CNF"`. | `native/src/content/game_data_service.cpp` (Open(), the `impl->files.Contains("SYSTEM.CNF")` check) |
| 2 | `SYSTEM.CNF` bytes are read through a caller-configured cap, `GameDataServiceConfig::maximum_system_config_bytes`, defaulted to `4096`. | `native/include/omega/content/game_data_service.h` (field `maximum_system_config_bytes = 4096`); `native/src/content/game_data_service.cpp` (`impl->files.Read("SYSTEM.CNF", impl->config.maximum_system_config_bytes)`) |
| 3 | `ValidateSystemConfig()` rejects an empty byte span with `"SYSTEM.CNF is empty"`. | `native/src/content/game_data_service.cpp`, function `ValidateSystemConfig` |
| 4 | `ValidateSystemConfig()` rejects any byte outside `{0x09 tab, 0x0D CR, 0x0A LF} ∪ [0x20,0x7E]` (printable ASCII plus tab/CR/LF) with `"SYSTEM.CNF contains non-ASCII data"` — i.e. the format is treated as strict 7-bit ASCII text. | `native/src/content/game_data_service.cpp`, function `ValidateSystemConfig` |
| 5 | The file is parsed as newline-delimited `KEY = VALUE` lines; each line is trimmed of leading/trailing space/tab/CR before the `=` split. | `native/src/content/game_data_service.cpp`, function `ValidateSystemConfig` (loop over `text.find('\n', cursor)`, `TrimAsciiWhitespace`) |
| 6 | The key `BOOT2` (case-insensitive) is the one recognized key; a line whose key case-insensitively equals `BOOT2` sets the boot value. A second such line triggers `"SYSTEM.CNF contains duplicate BOOT2 entries"`. Absence of any `BOOT2` line triggers `"SYSTEM.CNF has no BOOT2 entry"`. | `native/src/content/game_data_service.cpp`, function `ValidateSystemConfig` |
| 7 | The accepted `BOOT2` value, compared case-insensitively, is the single literal `CDROM0:\SCUS_972.64;1` (constant `kExpectedBootValue`); any other value yields `"unsupported retail build: expected NTSC-U SCUS-97264"`. | `native/src/content/game_data_service.cpp` (`constexpr std::string_view kExpectedBootValue = "CDROM0:\\SCUS_972.64;1";` and its use in `ValidateSystemConfig`) |
| 8 | After `SYSTEM.CNF` validation passes, `Open()` separately requires the boot executable named by constant `kExpectedBootExecutable = "SCUS_972.64"` to exist at the game-data root, independent of the `.CNF` text itself. | `native/src/content/game_data_service.cpp` |
| 9 | `native/tests/game_data_service_tests.cpp` exercises: (a) a wrong `BOOT2` build value (`SLES_000.00`) is rejected; (b) a correct `BOOT2` value with the boot executable absent still fails (`"a matching BOOT2 line cannot mask a missing boot executable"`); (c) removing the synthetic `SYSTEM.CNF` reproduces the stable missing-file error. | `native/tests/game_data_service_tests.cpp` |
| 10 | `native/tests/game_data_service_tests.cpp` and `CMakeLists.txt` register `native/src/content/game_data_service.cpp` and `native/tests/game_data_service_tests.cpp` as build/test targets, i.e. this validator is compiled and unit-tested, not dead code. | `CMakeLists.txt` (lines listing both paths) |
| 11 | Evidence ledger entry `E-0072` (confirmed) independently corroborates the exact missing-file error string `"content startup [missing-required-file]: game-data root is missing SYSTEM.CNF"` surfaced through the process-level diagnostic adapter, built and CTest-verified against a CMake-created empty synthetic root (no private/owner/disc-image input). | `analysis/evidence/ledger.jsonl`, entry `E-0072` |
| 12 | No suffix-handler table entry exists for `.cnf`: `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` covers only tdx/ska/skas/skm/skl/vag/lpd/par/col/vum/vpk, and `analysis/formats/asset-fingerprints.json`'s per-format aggregate block likewise has no `.cnf`/`cnf` key. | `tools/fingerprint_assets.py`; `analysis/formats/asset-fingerprints.json` |

## 3. Aggregate-only facts

- Whole-disc `.cnf` extension count: **8** (`analysis/manifests/disc-summary.json` top-level histogram). No semantic split of these 8 beyond the one named exception below.
- One of the 8 is independently broken out by the standardized name `SYSTEM.CNF` at **58 bytes** (`analysis/manifests/disc-summary.json`). This is an aggregate/whole-disc-histogram fact, not a per-file row lookup.
- `.cnf` never appears as a recursive-in-HOG or top-level-HOG member suffix in `analysis/formats/hog-validation.json` or `analysis/formats/asset-fingerprints.json` — i.e., at the aggregate level, this family is disjoint from every archived-asset suffix population those two sources enumerate (col, tdx, vag, skm, skl, vum, etc.).
- The runtime default resource boundary for reading any file the engine treats as a `.CNF`-family config (as opposed to a generic archive member) is 4096 bytes (`maximum_system_config_bytes` default, §2 row 2) — an aggregate configuration constant, not a measurement of a private input's actual size.

## 4. Hypotheses

1. **H1 — The other 7 whole-disc `.cnf` files are the same PS2 `SYSTEM.CNF`-style key/value text format, used for smaller subsystem or network configuration, not for boot selection.**
   Confirming/refuting observation (privacy-safe): re-run the disc-histogram tool with a suffix-scoped **content-shape aggregate** — e.g., "of the N whole-disc `.cnf` files, how many are ASCII-only, how many contain a line matching `KEY\s*=\s*VALUE`, min/max size" — emitted as a count/range only (no path, no per-file row, no payload bytes) into a new or extended tracked JSON. If the aggregate shows all 8 are ASCII key=value text under a few hundred bytes, H1 is supported; if any are binary or large, H1 is refuted for that subset.

2. **H2 — `.cnf` files under disc directories other than the root are IOP/network-stack configuration (e.g. driver or protocol parameters) rather than boot descriptors, since only one `.cnf` (`SYSTEM.CNF`) plays the boot role that `game_data_service.cpp` checks for.**
   Confirming/refuting observation: an aggregate report of directory-depth (root vs. non-root) counts for the 8 `.cnf` files, with no filenames — if all 7 non-`SYSTEM.CNF` occurrences sit below the disc root while `SYSTEM.CNF` sits at the root, that positional aggregate is consistent with H2 (still not proof of semantic role — would remain a Hypothesis unless a tracked native decoder or grammar doc for that sub-family appears).

3. **H3 — No non-boot `.cnf` file is ever read by any tracked native decoder; only `SYSTEM.CNF` has runtime-side meaning in this codebase.**
   Confirming/refuting observation: a repo-wide, path-free grep-count of `.cnf`-adjacent identifiers (`CNF`, `SYSTEM.CNF`, or config-key literals) across `native/` — already partially done for this dossier (§2 rows 1–11 found only `SYSTEM.CNF`); a full sweep turning up zero additional `.cnf`-branded code paths would confirm H3, any new hit would refute it and require a follow-up dossier note.

## 5. Missing observations

- **No tracked structural handler or grammar doc for `.cnf` as a general family.** `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` and `analysis/formats/*.md` cover col/hog/lpd/par/pop/skas/so/tdx/vag/vpk/vum but not `.cnf`. Privacy-safe collection: extend `fingerprint_assets.py` with a *structural-only* `.cnf` handler that classifies ASCII-vs-binary and `KEY=VALUE` line-shape as an aggregate (line count, key-name histogram with no values), emitted into `asset-fingerprints.json` the same way existing handled formats are — never printing key values, which could carry configuration content specific to the owner's disc.
- **No aggregate breakdown of the 7 non-`SYSTEM.CNF` whole-disc `.cnf` files.** `disc-summary.json` only names `SYSTEM.CNF` specially; the remaining 7 are folded into the bare `".cnf": 8` histogram entry. Privacy-safe collection: add an aggregate-only sub-count (e.g., "n at disc root, n below root", "size min/max/median") to `disc-summary.json` or a sibling manifest, computed the same way the existing whole-disc histogram is computed — no filenames, no hashes, no payload.
- **No adversarial/resource-boundary test coverage for three of the four failure branches `ValidateSystemConfig()` implements.** `native/tests/game_data_service_tests.cpp` exercises the missing-file case and the wrong-`BOOT2`-value case, but grep of that test file shows no case constructing an empty `SYSTEM.CNF`, a non-ASCII-byte `SYSTEM.CNF`, a duplicate-`BOOT2` `SYSTEM.CNF`, or a `SYSTEM.CNF` at/over the 4096-byte `maximum_system_config_bytes` cap. Privacy-safe collection: add four CMake-synthesized-fixture unit tests (empty, one non-ASCII byte, duplicate `BOOT2` lines, byte length at/above the cap) mirroring the existing missing-file/wrong-value test pattern in that same file — no owner/disc input required.
- **No tracked evidence on whether any of the 7 non-`SYSTEM.CNF` `.cnf` files are themselves referenced by name anywhere in `native/` source (e.g., a network-config loader).** The `native/` grep in this session found only `SYSTEM.CNF`; a dedicated follow-up grep sweep restricted to `native/` and `CMakeLists.txt` for any other `.CNF`-suffixed literal would close this gap without touching disc/private content.

## 6. Decoder/tooling status

**Classification: `passive_descriptor_only`** (for `SYSTEM.CNF` specifically) with the rest of the `.cnf` whole-disc population **`unknown`**.

- Justification for `passive_descriptor_only` rather than `canonical_decoder`: `ValidateSystemConfig()` (`native/src/content/game_data_service.cpp`) does parse structure — ASCII-range check, newline-delimited `KEY = VALUE` splitting, case-insensitive `BOOT2` key match, exact-value comparison against one hardcoded literal — but it is a narrow existence/shape/value *gate* used only to decide whether `GameDataService::Open()` accepts a mounted root, not a general decoder that extracts or exposes arbitrary `SYSTEM.CNF` key/value content as a reusable data structure. It recognizes exactly one key (`BOOT2`) and rejects everything else implicitly by never reading it. This falls short of `canonical_decoder` (would require exposing the full key/value set generically) and above `structural_envelope_only`/`aggregate_scanner_only` (it does inspect and reject on content, not merely count/bucket).
- Registration: both `native/src/content/game_data_service.cpp` and `native/tests/game_data_service_tests.cpp` are listed in `CMakeLists.txt`, so the validator builds and its tests run in the tracked test suite.
- Corroboration: ledger entry `E-0072` (`analysis/evidence/ledger.jsonl`) independently confirms the missing-`SYSTEM.CNF` error path end-to-end through a process-level diagnostic, built against a CMake-synthesized empty root — not a private disc image.
- **Test gap (adversarial/resource-boundary):** as detailed in §5, the empty-file, non-ASCII-byte, duplicate-`BOOT2`, and over-the-4096-byte-cap branches of `ValidateSystemConfig()` have no located unit test in `native/tests/game_data_service_tests.cpp`. This is a coverage gap in an otherwise-tested decoder, not a decoder-absence.
- For the other 7 whole-disc `.cnf` occurrences: **`unknown`**. No tracked handler, grammar doc, or native source references any `.cnf` filename other than `SYSTEM.CNF`. Nothing in tracked evidence supports classifying them as `structural_envelope_only` or higher — they remain unclassified rather than assumed identical to `SYSTEM.CNF`.

## 7. Codex work order

Ranked, privacy-safe, no semantic speculation beyond what §2–§6 already establish:

1. **Close the `ValidateSystemConfig()` test gap.** Add four unit tests to `native/tests/game_data_service_tests.cpp` (CMake-synthesized fixtures, no owner input) for: empty `SYSTEM.CNF`; a byte outside the tab/CR/LF/printable-ASCII range; duplicate `BOOT2` lines; a payload at/over `maximum_system_config_bytes` (4096). Assert each yields the specific error string already present in `ValidateSystemConfig()`. This directly targets the gap identified in §5/§6.
2. **Add an aggregate-only `.cnf` shape scan to `tools/fingerprint_assets.py`.** A new handler (or a lightweight pre-pass, since `FORMAT_HANDLERS` currently has no `.cnf` entry) that classifies each `.cnf` occurrence as ASCII-text-vs-binary and counts `KEY=VALUE`-shaped lines, emitting only counts/ranges into `asset-fingerprints.json` — never a key's value, never a filename beyond the existing `SYSTEM.CNF` special case. This would let H1 (§4) be confirmed or refuted without any new privacy exposure.
3. **Extend `analysis/manifests/disc-summary.json` (or a sibling manifest) with a root-vs-non-root positional aggregate for `.cnf`.** A single "N at root / N below root" count pair, computed the same way the existing extension histogram is computed, to test H2 (§4) without naming any of the 7 non-`SYSTEM.CNF` files.
4. **Run a dedicated, path-free grep sweep of `native/` and `CMakeLists.txt` for any `.CNF`-suffixed literal other than `SYSTEM.CNF`.** This would either confirm H3 (§4) — that no other `.cnf` file has runtime meaning in this codebase — or surface a second in-scope decoder to fold into a future revision of this dossier.
5. **Do not build a general `.cnf` decoder/grammar doc yet.** Tracked evidence supports a narrow `SYSTEM.CNF` gate only; a full `.cnf`-family grammar (analogous to `analysis/formats/COL.md`, `TDX.md`, etc.) would require the aggregate evidence from items 2–4 above first, to avoid inventing semantics for the 7 unclassified occurrences.
