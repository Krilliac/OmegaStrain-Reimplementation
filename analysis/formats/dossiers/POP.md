# .POP Dossier

## 1. Identity

`.POP` is a per-level game-data container in Syphon Filter: The Omega Strain, co-located with a
level's other `GAMEDATA/<LEVEL>/` containers (e.g. `DATA.HOG`, `MAPTEX.HOG`, `MAPVUM.HOG`). It is
**not** a PS2 system/OS artifact (not an IOP module, boot config, or save icon) — the evidence below
shows it is a passively-decoded game format with a validated terrain-reference prefix and an
uninterpreted remainder. It is therefore given a full dossier rather than a system-file stub.

## 2. Occurrence evidence

| Scope | Count | Source |
| --- | ---: | --- |
| Recursive-in-HOG (nested member suffix) | 0 | `analysis/formats/asset-fingerprints.json` (`scan.extensions` has no `.pop` key) |
| Top-level-HOG (HOG directory-entry suffix) | 0 | `analysis/formats/hog-validation.json` (no `.pop`/`POP` entries) |
| Whole-disc files | 18 | `analysis/manifests/disc-summary.json` (`".pop": 18`) and `analysis/manifests/disc-files.jsonl` (18 `GAMEDATA/<LEVEL>/DATA.POP` rows) |

`.POP` appears exclusively as one whole-disc file per level (`DATA.POP`), never as a HOG member —
consistent with the task brief's stated recursive/top-level-HOG counts of 0 and whole-disc count of
18.

## 3. Confirmed facts

All rows below are mechanically citable from a named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| C1 | The corpus has exactly 18 `.POP` files, one per level directory, each paired with a `DATA.HOG` in the same directory. | `analysis/manifests/disc-files.jsonl` |
| C2 | A structural aggregate scanner (`scan_pop_files`/`parse_pop_terrain`) parses a leading terrain-reference section: `u32` word `70`, then ASCII tag `"TER:"`, then a `u32` record count, then that many `{u32 kind, u32 index, NUL-terminated ASCII name, alignment padding to 4 bytes}` records. | `tools/fingerprint_assets.py` (function `parse_pop_terrain`, `scan_pop_files`) |
| C3 | All 18 corpus `.POP` files parse this prefix with zero `terrain_parse_failures`; total `terrain_record_count` = 5,351. | `analysis/formats/asset-fingerprints.json` (`population_files.terrain_parse_successes`=18, `terrain_record_count`=5351) |
| C4 | Every one of the 18 files is immediately followed by ASCII tag `"GOB:"` right after the terrain records (`gob_follows_terrain_records`=18). | `analysis/formats/asset-fingerprints.json` (`population_files.gob_follows_terrain_records`) |
| C5 | 4,144 of the 5,351 terrain records have nonzero alignment/padding bytes after the name (i.e., padding is not guaranteed zero). | `analysis/formats/asset-fingerprints.json` (`population_files.terrain_records_with_nonzero_alignment_bytes`) |
| C6 | Every terrain record name has a case-insensitive basename match against the same level's `DATA.HOG` directory (`terrain_names_missing_from_data_hog`=0); the HOG directories additionally contain 44 members not referenced by any terrain name. | `analysis/formats/asset-fingerprints.json` (`population_files.terrain_names_matching_data_hog`, `terrain_names_missing_from_data_hog`, `data_hog_names_not_in_terrain`) |
| C7 | The MINSK level's `.POP` file was inspected in more detail: 675,312 bytes, 299 terrain records with `kind` values limited to {12, 13}, next tag after terrain is `GOB:`, and 20 later four-byte-aligned literal tags occur in one fixed observed order (`TER:`, `GOB:`, `SND:`, `ACL:`, `INL:`, `NPC:`, `WPN:`, `PLR:`, `SKY:`, `PNT:`, `DIR:`, `ENV:`, `NOD:`, `GEN:`, `GRP:`, `BOX:`, `FIR:`, `CAM:`, `INV:`, `BUG:`). | `analysis/formats/asset-fingerprints.json` (`minsk.pop`) |
| C8 | A published format-grammar document exists describing the terrain-prefix layout (header word 70, `"TER:"` tag, count, per-record kind/index/name/alignment, terminating `"GOB:"`). | `analysis/formats/POP.md` ("Observed layout" section) |
| C9 | A native structural decoder `omega::asset::PopTerrainIndex` exists that accepts a caller-owned byte span, bounds record count/name length, rejects truncation/empty/non-ASCII names/a missing `GOB:` boundary, and returns owned record names plus a nonzero-alignment count, without interpreting the alignment bytes. | `native/include/omega/asset/pop_terrain_index.h`, `native/src/asset/pop_terrain_index.cpp` |
| C10 | A second native decoder `omega::retail::DecodePopLevelManifest` resolves every terrain name (case-insensitively, by basename stem) against the matching `DATA.HOG` directory into an owned `LevelManifestIR`, rejecting missing/unsafe/duplicate references and applying cumulative resource limits; it preserves only the `kind`/`index` numeric fields without assigning them semantics. | `native/include/omega/retail/pop_level_manifest_decoder.h`, `native/src/retail/pop_level_manifest_decoder.cpp` |
| C11 | A third native component `omega::retail::InspectPopPostTerrainHypotheses` (with descriptor type `PopPostTerrainHypothesisDescriptor`) exists as a passive, retail-only inspector that checks the already-published 19-literal aligned-marker order and five marker-relative "+4 word / fixed stride" arithmetic extents, without decoding sections or assigning field semantics. | `native/include/omega/retail/pop_post_terrain_hypothesis_descriptor.h`, `native/src/retail/pop_post_terrain_hypothesis_descriptor.cpp` |
| C12 | These native components are registered in the build and covered by dedicated test binaries: `pop_terrain_index.cpp`, `pop_level_manifest_decoder.cpp`, `pop_post_terrain_hypothesis_descriptor.cpp`, `pop_commands.cpp`, `pop_post_terrain_commands.cpp` are compiled sources, and `pop_terrain_index_tests.cpp`, `pop_level_manifest_decoder_tests.cpp`, `pop_post_terrain_hypothesis_descriptor_tests.cpp`, `pop_post_terrain_commands_tests.cpp` are registered CTest targets (including `omega_tool_rejects_empty_pop_tree` and `omega_tool_rejects_empty_pop_post_terrain_hypotheses_tree` for the empty-corpus/adversarial-boundary case). | `CMakeLists.txt` |
| C13 | A CLI surface (`omega_tool pop-verify-tree`, `omega_tool pop-post-terrain-hypotheses-verify-tree`, `omega_tool level-manifest-verify-tree`) drives these decoders over a corpus and reports fixed-schema, path-free aggregates. | `native/apps/omega_tool/pop_commands.cpp`, `native/apps/omega_tool/pop_post_terrain_commands.cpp`, `analysis/formats/POP.md` ("Reproduce" section) |
| C14 | Evidence-ledger entries record independent confirmed runs of these components against the full owned corpus: E-0011 (fingerprint corpus covers POP among named families), E-0016 (native terrain-prefix parser validates all 18 files / 5,351 records, 4,144 nonzero-alignment, zero errors), E-0017 (native level-manifest adapter resolves all 5,351 references across 18 levels with zero errors), E-0028 (bounded post-terrain literal scanner: 342 aligned hits, 19 per file, one shared order, semantics disclaimed), E-0030/E-0031 (bounded arithmetic layout-hypothesis scorer: five marker/stride tuples fit all nonzero and zero occurrences, semantics disclaimed), E-0032 (bounded record-shape profiler: 8,019 candidate records / 105,985 column observations, semantics disclaimed), E-0040 (native passive post-terrain hypothesis descriptor accepts all 18, zero errors, semantics disclaimed). | `analysis/evidence/ledger.jsonl` (entries E-0011, E-0016, E-0017, E-0028, E-0030, E-0031, E-0032, E-0040) |

## 4. Aggregate-only facts

No semantic interpretation attached — counts/ranges only.

- Whole-disc `.POP` file size range across the 18 files: 58,208 to 919,360 bytes (`analysis/formats/asset-fingerprints.json`, `population_files.file_bytes_range`; corroborated per-file in `analysis/manifests/disc-files.jsonl`, e.g. 58,208–919,360 bytes observed across the listed `DATA.POP` rows).
- Aggregate `terrain_kind` value histogram across all 5,351 records spans integer keys {2–19, 21–26} with per-value counts ranging from 53 to 914 occurrences (`analysis/formats/asset-fingerprints.json`, `population_files.terrain_kind`). No meaning is assigned to these integers.
- The bounded post-terrain literal-marker scan finds exactly 342 aligned candidate-tag hits corpus-wide: 19 distinct literal tags, each occurring exactly once per file, in one shared ordered sequence across all 18 files (`analysis/formats/POP.md`, "Post-TER marker envelope"; ledger E-0028).
- The bounded arithmetic layout-hypothesis scorer tested 1,443 bounded nonzero candidate count words across 342 marker spans; five marker-relative "+4 word / fixed stride" tuples exactly fit every nonzero occurrence tested (`INL:`/36 bytes 18/18, `PNT:`/88 bytes 17/17, `DIR:`/44 bytes 18/18, `ENV:`/76 bytes 18/18, `INV:`/84 bytes 16/16), with the remaining zero-word cases matching the same formula's predicted empty extent (`analysis/formats/POP.md`, "Candidate layout hypothesis scoring"; ledger E-0030/E-0031).
- The bounded record-shape profiler classified 8,019 candidate records and 105,985 four-byte column observations across the five candidates, reporting only zero/nonzero and IEEE-754 finite/nonfinite bit-pattern counts and distinct-pattern cardinality — no field types assigned (`analysis/formats/POP.md`, "Candidate record-shape profiling"; ledger E-0032).
- Native passive-descriptor run over the owned corpus: 18 candidates discovered, 18 accepted, 0 rejected, independent accepted-only maxima input/items/logical-output/string/scratch = 919,360 / 1 / 168 / 26 / 80,036 (logical accounting fields, not memory measurements) (`analysis/formats/POP.md`, "Native passive hypothesis descriptor"; ledger E-0040).

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

**Classification: `canonical_decoder`** for the terrain-reference prefix (header word 70 → `"TER:"` → count → records → `"GOB:"`) and its level-manifest resolution layer; the post-`GOB:` remainder is explicitly `structural_envelope_only` (hypothesis-scored, not decoded) and must not be conflated with the confirmed prefix.

- Canonical decoder: `omega::asset::PopTerrainIndex` (`native/include/omega/asset/pop_terrain_index.h`, `native/src/asset/pop_terrain_index.cpp`) — bounds-checked, rejects malformed/truncated input, validated against all 18 corpus files with zero errors (ledger E-0016). Registered in `CMakeLists.txt` and covered by `native/tests/pop_terrain_index_tests.cpp`.
- Canonical decoder (dependency layer): `omega::retail::DecodePopLevelManifest` (`native/include/omega/retail/pop_level_manifest_decoder.h`, `native/src/retail/pop_level_manifest_decoder.cpp`) — resolves all 5,351 terrain references across 18 levels with zero errors (ledger E-0017). Registered in `CMakeLists.txt`, tested by `native/tests/pop_level_manifest_decoder_tests.cpp`.
- Passive descriptor (not a decoder): `omega::retail::InspectPopPostTerrainHypotheses` / `PopPostTerrainHypothesisDescriptor` (`native/include/omega/retail/pop_post_terrain_hypothesis_descriptor.h`, `native/src/retail/pop_post_terrain_hypothesis_descriptor.cpp`) — checks only the already-published arithmetic hypothesis envelope for the post-terrain bytes; explicitly does not assign section, count, record, or field semantics (`analysis/formats/POP.md`). Registered in `CMakeLists.txt`, tested by `native/tests/pop_post_terrain_hypothesis_descriptor_tests.cpp` and `native/tests/pop_post_terrain_commands_tests.cpp`.
- Aggregate scanner (Python, no native decoder): `tools/fingerprint_assets.py` (`parse_pop_terrain`/`scan_pop_files`), `tools/scan_pop_post_terrain.py`, `tools/score_pop_section_layout_hypotheses.py`, `tools/profile_pop_candidate_record_shapes.py` — all aggregate-only, each with a dedicated test module under `tools/tests/`.
- Adversarial/resource-boundary test coverage: an empty-corpus rejection path exists for both the terrain-prefix and post-terrain-hypothesis CLI verifiers (`CMakeLists.txt` targets `omega_tool_rejects_empty_pop_tree` and `omega_tool_rejects_empty_pop_post_terrain_hypotheses_tree`). No tracked test exercises truncated/mutated real-corpus-shaped `.POP` bytes beyond the empty case — this is the test-gap noted in Section 6.

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
