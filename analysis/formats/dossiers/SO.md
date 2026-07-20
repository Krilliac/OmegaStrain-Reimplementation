# SO Dossier

## 1. Identity

`.so` is an in-scope game-asset suffix that occurs nested inside HOG archives across the owned
NTSC-U disc corpus. At the evidence level this repository can defend, `.SO` is **not** an ELF
shared object despite the extension: every tested module begins with the 32-bit value `8` (a
code-array offset), not the ELF magic `7F 45 4C 46`. It is a custom little-endian container
holding a leading code-cell array followed by a literal pool and four reflected-schema tables
(types, enums, globals, callables) that describe a proprietary script-VM module. The repository
draws no conclusion beyond that structural description: opcode semantics, the runtime's type-ID
registry, and the gameplay role of any given module are explicitly left unassigned by the tracked
grammar doc, and the project has a standing decision to never execute `.SO` cells inside the
shipping reimplementation.

## 2. Occurrence evidence

| Metric | Value | Source |
| --- | ---: | --- |
| Recursive-in-HOG occurrences | 139 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".so"]`) |
| Top-level-HOG occurrences | 139 | `analysis/formats/hog-validation.json` (`entry_extensions[".so"]`) |
| Whole-disc occurrences | 0 | `analysis/manifests/disc-summary.json` (`extensions` has no `.so` key) and `analysis/manifests/disc-files.jsonl` (zero matching rows) |

Recursive-in-HOG and top-level-HOG counts are identical (139 = 139), consistent with every
observed `.SO` member sitting exactly one container level deep (directly inside a top-level HOG
archive) rather than inside a further-nested HOG. `.SO` never appears loose at the whole-disc
level.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| 1 | `.SO` files are not ELF: every tested module opens with the 32-bit little-endian value `8` (a code-array offset), not the ELF magic `7F 45 4C 46` | `analysis/formats/SO.md` ("Result") |
| 2 | `.so` is absent from `FORMAT_HANDLERS` in `tools/fingerprint_assets.py` (only `col, lpd, par, ska, skas, skl, skm, tdx, vag, vpk, vum` have structural handlers there) — the recursive aggregate scanner counts `.so` occurrences but does not structurally decode them | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict, line ~499) |
| 3 | `.so` has no corresponding key under `formats` in `analysis/formats/asset-fingerprints.json` (only the eleven handled-suffix keys listed above appear there); the only `.so` aggregate present is the raw extension count under `scan.extensions` | `analysis/formats/asset-fingerprints.json` |
| 4 | A dedicated, non-`fingerprint_assets.py` tool `tools/inspect_so.py` exists, reads `.SO` modules directly out of `SCRIPTS.HOG`-class archives via `hog.py`'s `parse_hog`, and emits structural metadata only (never extracts or copies a payload) | `tools/inspect_so.py` (module docstring, `Reader` class) |
| 5 | Running the inspector against the owner corpus parsed all 118 modules from 19 level archives to exact EOF with 0 parse errors and 0 trailing bytes, producing `analysis/formats/so-validation.json` | `analysis/formats/SO.md` ("Result"); `analysis/formats/so-validation.json` |
| 6 | Aggregate structural totals across those 118 modules: 9,275,184 serialized bytes; 402,694 32-bit code cells; 6,909 literal-pool entries; 9,250 type records with 3,681 members; 4,359 enum records with 174 locally declared values; 24,928 global-symbol records including 4,074 local definitions; 42,804 inline global-initializer cells; 79,845 callable records including 4,950 local definitions; 140,322 callable parameter-type references | `analysis/formats/SO.md` ("Result") |
| 7 | The confirmed module grammar: `u32 code_offset` (confirmed `8` in all 118 modules) + `u32 code_cell_count` + code cells, then `u32 literal_count` + length-prefixed literal strings, then `u32 type_count` + type records, then `u32 enum_count` + enum records, then `u32 globals_reserved` (confirmed `0` in all modules) + `u32 global_count` + global records, then `u32 callable_count` + callable records, then EOF | `analysis/formats/SO.md` ("Serialization grammar") |
| 8 | `LPString` encoding is `u32 byte_length_including_NUL` + that many bytes + zero-padding to a 4-byte boundary | `analysis/formats/SO.md` ("Serialization grammar") |
| 9 | Type record layout: name, `u32 external_flag` (observed values 0 or 1), base-type name (zero-length when absent), `u32 member_count`, then per member a name and a `u32 member_type_id` | `analysis/formats/SO.md` ("Serialization grammar") |
| 10 | Enum record layout: name, `u32 external_flag`; when `external_flag == 0`, a `u32 value_count` followed by name/value pairs | `analysis/formats/SO.md` ("Serialization grammar") |
| 11 | Global record layout: name, `field_0`, `external_flag`, `u32 ordinal` (confirmed to exactly equal the table index), `u32 initializer_cell_count`, then that many initializer cells | `analysis/formats/SO.md` ("Serialization grammar") |
| 12 | Callable record layout: name, `field_0`, `external_flag`, `u32 ordinal` (confirmed to exactly equal the table index), `label_id`, `entry_cell`, five further `u32` fields (`unknown_5`..`unknown_9`), `u32 parameter_count`, then that many parameter-type-ID words | `analysis/formats/SO.md` ("Serialization grammar") |
| 13 | Cross-module invariants confirmed for the `external_flag`: flag-1 globals never carry initializer cells while flag-0 globals may; flag-1 callables always have `entry_cell == 0`; flag-0 callables always have a nonzero entry and are strictly ordered by that entry; all 4,950 flag-0 callable entries point inside that module's own leading code array | `analysis/formats/SO.md` ("Imports, definitions, and code entry points") |
| 14 | For every one of the 4,950 local callables, the code cell immediately before `entry_cell` has `high16 == callable.label_id` and `low16 == 0x003B`, confirmed across all records | `analysis/formats/SO.md` ("Imports, definitions, and code entry points") |
| 15 | For local callables, `field_0` takes exactly three observed values across all 4,950 records: `0x00000004` (4,746 records), `0x00000002` (163 records), `0x00000029` (41 records) | `analysis/formats/SO.md` ("Imports, definitions, and code entry points") |
| 16 | Across all 79,845 callable records, the five trailing fields' non-default (`!= 0` and `!= 0xFFFFFFFF`) counts are, in order, `0, 1624, 18, 956, 381` | `analysis/formats/SO.md` ("Imports, definitions, and code entry points") |
| 17 | Module-to-module schema variance is confirmed and bounded: type count ranges 78–80 per module, enum count ranges 35–40, at most one locally declared type and at most two locally declared enums appear per module, external-only type tables hash to 8 distinct values and external-only enum tables hash to 14 distinct values across the corpus | `analysis/formats/SO.md` ("Shared versus module-local schema") |
| 18 | Ledger entry E-0010 (confirmed): across 118 retail `.SO` modules the custom little-endian container parser reaches exact EOF, validates 4,950 strictly ordered in-bounds local callable entries, and finds no ELF modules; evidence cites `analysis/formats/SO.md`, `analysis/formats/so-validation.json`, `tools/inspect_so.py`; reproduction command is `python -B tools/inspect_so.py private/extracted-disc --json analysis/formats/so-validation.json` | `analysis/evidence/ledger.jsonl` (E-0010) |
| 19 | `.SO` has no native decoder, descriptor, or CMake/CTest registration anywhere in the tracked native tree: `native/include/omega/retail/*.h` and `native/src/**/*.cpp` contain no SO/script-VM decoder, and `CMakeLists.txt` lists compiled sources/tests only for `container_descriptors`, `pop_post_terrain_hypothesis_descriptor`, `ska/skm/skl_container_descriptor`, and `vum_render_payload_descriptor` — none reference `inspect_so.py` or any `.SO`-specific type | `CMakeLists.txt`; direct listing of `native/include/omega/retail/`, `native/src/retail/` |
| 20 | The only native-tree references to `.SO`/`.so` are as a generic filler filename in two generic container tests — `native/tests/hog_archive_tests.cpp` (`"B.SO"` used to test generic HOG entry lookup) and `native/tests/virtual_file_system_tests.cpp` (`"TEST.SO"` / `"GAMEDATA/MINSK/SCRIPTS/TEST.SO"` used to test generic case-insensitive VFS path lookup and byte-limited reads) — neither test asserts anything about `.SO` internal structure | `native/tests/hog_archive_tests.cpp`; `native/tests/virtual_file_system_tests.cpp` |
| 21 | 19 top-level `SCRIPTS.HOG` archives are present in the tracked HOG archive listing, with a combined `entry_count` of 119 across them | `analysis/formats/hog-validation.json` (`archives`, filtered to `path` containing `SCRIPTS.HOG`) |

## 4. Aggregate-only facts

No semantic interpretation attached — counts and structural observations only.

- `.SO` is the 10th most common HOG entry extension by top-level count among the 15 extensions with nonzero counts in the tracked corpus: `.tdx` 11166, `.vag` 8665, `.hog` 6677, `.skm` 2808, `.lpd` 862, `.par` 679, `.skl` 636, `.ska` 212, `.bon` 156, `.so` 139, `.vpk` 85, `.bnk` 77, `.pss` 54, `.sub` 42, `.skf` 26 (`hog-validation.json`, `entry_extensions`).
- The 19 tracked `SCRIPTS.HOG` archives sum to 119 total entries, while the corpus-wide `.so` extension count is 139 (both `hog-validation.json`). The 20-entry gap and the one-entry gap versus the inspector's "118 modules" figure are unexplained by any tracked source — see Section 6.
- Per-module average size implied by the 118-module aggregate is roughly 78.6 KB (9,275,184 bytes / 118 modules), reported here as a simple arithmetic derivation of two already-published aggregate figures, not an independently measured per-file statistic.
- Locally declared content is a small fraction of each module's reflected schema: 4,074 of 24,928 global records (~16.3%) and 4,950 of 79,845 callable records (~6.2%) are local (flag-0) rather than external (flag-1) (`SO.md`, arithmetic over the published totals).
- Type/enum table sizes cluster tightly (78–80 types, 35–40 enums per module) against a much larger literal pool (6,909 entries across 118 modules, ~58.6 per module on average) and code-cell array (402,694 cells across 118 modules, ~3,413 per module on average) (`SO.md`, "Result" and "Shared versus module-local schema", arithmetic over published totals).

## 5. Hypotheses


No new hypothesis is promoted here. The established evidence above remains the claim ceiling, and
this dossier authorizes no owner-corpus measurement recipe. Before any future measurement is
implemented, a separate reviewed contract must predeclare its fixed public schema, fixed minimum
cohort threshold, bounded execution and typed failures, and project-generated privacy tests.

An authorized report may contain only fixed anonymous corpus-wide totals for cohorts meeting that
threshold. Smaller cohorts must collapse to one typed suppression result. The report must not emit
raw values, signatures, payloads, owner-derived strings, paths, file, container, or archive names,
suffix-derived labels, per-file, per-container, or per-archive rows, or cross-tabulations keyed by
raw fields.

## 6. Missing observations


Unresolved structural, semantic, consumer, and validation questions remain missing observations.
This section deliberately defines no executable collection recipe. Closing any gap requires the
separately reviewed contract and suppression policy stated above; absent that contract, the gap
remains UNKNOWN.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`** in the authoritative native/tool coverage matrix.

- Separately, `tools/inspect_so.py` fully and deterministically recovers the tracked `.SO` container grammar —
  leading code-cell array, literal pool, type table, enum table, global table, and callable table
  — down to named fields for every record kind, reaching exact EOF with 0 parse errors and 0
  trailing bytes across all 118 tracked modules (`analysis/formats/SO.md`, `so-validation.json`,
  ledger E-0010). This is materially deeper than a boundary-only (`structural_envelope_only`)
  parse: it recovers named type/enum/global/callable records, not merely span/table extents.
- This decoder is **Python-only and standalone** — it is not one of the `FORMAT_HANDLERS` entries
  in `tools/fingerprint_assets.py` (that aggregate scanner has no `.so` handler at all), and it has
  **no native counterpart**: no header/source pair exists under `native/include/omega/retail/` or
  `native/src/retail/`, and `CMakeLists.txt` registers no compiled source, descriptor, or CTest
  target for `.SO`. The only native-tree mentions of `.SO`/`.so` are generic filler filenames in
  two unrelated container tests (`hog_archive_tests.cpp`, `virtual_file_system_tests.cpp`) that
  exercise generic HOG/VFS lookup, not `.SO`-specific decoding.
- This absence of native integration is a deliberate project stance, not a gap: `SO.md` states
  explicitly that "the shipping reimplementation will never interpret, translate, recompile, or
  dispatch retail `.SO` cells," and frames the format as "an offline research input, not a
  runtime format." The offline parser is important research evidence, but it does not satisfy the
  repository's native `canonical_decoder` definition and has no focused Python test. The coverage
  label therefore remains `aggregate_scanner_only`; this does not diminish the separately recorded
  corpus result.
- Gap: unlike SKAS (E-0093) and the texture-storage debug adapter (E-0066), there is no dedicated
  adversarial/boundary test suite or ledger entry for `tools/inspect_so.py` — the sole tracked
  validation is one deterministic pass over the real corpus (E-0010). See Section 6.

## 8. Codex work order


1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
