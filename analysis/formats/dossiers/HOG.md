# .HOG Dossier

## 1. Identity

`.HOG` is Syphon Filter: The Omega Strain's container/archive format: a flat directory of
NUL-terminated-name entries pointing at contiguous payload spans within the same file. It is
used both as a top-level game archive (e.g. `DATA.HOG`, `LOADING.HOG`) and, recursively, as a
nested-archive envelope inside other `.HOG` files. This identity level (container/archive,
directory-of-named-spans) is mechanically established by the validated parse in
`tools/validate_hogs.py`, `tools/hog.py`, and the independent native parser in
`native/src/archive/hog_archive.cpp` / `native/include/omega/archive/hog_archive.h`. The
semantics of the header's first field (`tag`) and the interpretation of any given entry's
payload bytes are not established and are kept out of scope here.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
| --- | ---: | --- |
| Recursive occurrences inside HOG containers (`.hog` suffix, any depth) | 6,677 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".hog"]`, `scan.nested_hog_candidates`) |
| Top-level HOG archives (disc-root scope used by the validator) | 273 | `analysis/formats/asset-fingerprints.json` (`scan.top_level_hog_files`, `scan.top_level_hog_valid`) |
| Whole-disc file count with `.hog` suffix | 273 | `analysis/manifests/disc-summary.json` (`".hog": 273`) |
| Total directory entries across the 273 top-level archives | 32,351 | `analysis/formats/asset-fingerprints.json` (`scan.top_level_hog_entries`); corroborated by `scan.depth["0"]`: 32,351 |
| Nested HOG candidates found while scanning entries at depth 1 | 6,677 | `analysis/formats/asset-fingerprints.json` (`scan.nested_hog_candidates`) |

The whole-disc and recursive-in-HOG counts for the bare `.hog` suffix agree (273 top-level,
6,677 nested), which is consistent with `.hog` files existing only as top-level archives and as
nested archives inside other archives, per `analysis/manifests/disc-files.jsonl` file listing
scope and `analysis/formats/asset-fingerprints.json` scan scope.

## 3. Confirmed facts

Each row is mechanically citable from the named tracked file.

| # | Fact | Tracked source |
| --- | --- | --- |
| 1 | Header layout is 5 little-endian u32 fields at fixed offsets: `tag` (`0x00`), `count` (`0x04`), `offsets_offset` (`0x08`), `names_offset` (`0x0C`), `data_offset` (`0x10`); `offsets_offset` is `0x14` in the validated set. | `analysis/formats/HOG.md` ("Layout" table); struct mirror in `native/include/omega/archive/hog_archive.h` (`HogHeader`) |
| 2 | At `offsets_offset` sits an array of `count + 1` u32 offsets relative to `data_offset`; offset 0 must be 0; offsets must be monotonic; the final offset is the archive's logical payload end. | `analysis/formats/HOG.md`; enforced in `tools/fingerprint_assets.py` (`parse_hog_span`: "HOG payload offsets are not monotonic from zero") and `native/src/archive/hog_archive.cpp` |
| 3 | `names_offset = offsets_offset + 4 * (count + 1)`; between `names_offset` and `data_offset` sits a sequence of `count` NUL-terminated ASCII names followed by alignment padding. | `analysis/formats/HOG.md` |
| 4 | Entry `i`'s payload span is `[data_offset + offsets[i], data_offset + offsets[i+1])`. | `analysis/formats/HOG.md` |
| 5 | A top-level HOG must end exactly at its logical payload end (no trailing bytes accepted); a nested/embedded HOG occupies a parent-directory span and may have an all-zero tail after the logical end, but any nonzero tail byte is rejected. | `analysis/formats/HOG.md`; `native/include/omega/archive/hog_archive.h` (`HogIndex::OpenRange` doc comment: "Only an all-zero tail... is accepted"); `tools/fingerprint_assets.py` (`parse_hog_span`: "HOG trailing span is not zero padding") |
| 6 | The full directory-interpretation grammar (header, offset table, name table, payload spans, top-level vs. nested tail rule) validates with zero structural errors across all 273 top-level archives and 32,351 entries, and separately across all 6,677 nested/embedded HOG spans (501 exact-span, 6,176 zero-padded). | Ledger `E-0005`, `E-0011`, `E-0015` in `analysis/evidence/ledger.jsonl`; `analysis/formats/hog-validation.json`; `analysis/formats/asset-fingerprints.json` |
| 7 | An independent, second implementation — the native C++ `HogIndex`/`HogArchive` classes — reproduces the same validation result (273 archives, 32,351 entries) without loading payload bytes into memory, and separately validates all 6,677 nested spans. | Ledger `E-0007`, `E-0015`; `native/src/archive/hog_archive.cpp`; `native/apps/omega_tool/main.cpp` (`hog-verify-tree`, `hog-verify-nested-tree` commands) |
| 8 | The native decoder performs checked (overflow-safe) 64-bit arithmetic for offset/size addition and multiplication before any range use, and enforces upper bounds: `kMaximumDirectorySize` = 64 MiB, `kMaximumEntryCount` = 2^20, `kMaximumNameLength` = 4096 bytes, header size `kHeaderSize` = `0x14`. | `native/src/archive/hog_archive.cpp` (constants and `CheckedAdd`/`CheckedMultiply` helpers) |
| 9 | Extraction from a parsed HOG directory is path-safety-checked: it refuses absolute paths, `.`/`..` path components, empty components, and any destination path that resolves outside the requested output root. | `analysis/formats/HOG.md` ("Reproduce" section, closing note); corroborated by `tools/hog.py` (extraction implementation) |
| 10 | The native test suite (`native/tests/hog_archive_tests.cpp`) includes a case asserting that overflowing file ranges are rejected before any seek is attempted, and constructs/writes a synthetic malformed nested-HOG byte buffer to exercise the OpenRange validation path. | `native/tests/hog_archive_tests.cpp` (test names: "overflowing file ranges are rejected before seeking"; synthetic "dirty_nested_bytes" write) |
| 11 | The native archive/test sources are registered in the build: `native/src/archive/hog_archive.cpp` is a compiled source and `native/tests/hog_archive_tests.cpp` is a registered test target. | `CMakeLists.txt` lines listing both paths |
| 12 | The header's first field (`tag`) is explicitly documented as unidentified: "Varies by archive; checksum/tag algorithm is not yet identified." | `analysis/formats/HOG.md` (Layout table, `tag` row) |
| 13 | The directory-interpretation proof is explicitly scoped: "It does not yet prove what the first word means or the semantics of any embedded asset payload." | `analysis/formats/HOG.md` (end of "Layout" section) |

## 4. Aggregate-only facts

No semantic interpretation is attached to any of these; they are corpus-wide counts/ranges only.

- Recursive `.hog`-suffix occurrence count in the fingerprint corpus: 6,677 (`analysis/formats/asset-fingerprints.json`, `scan.extensions[".hog"]`).
- Of those 6,677 nested-HOG candidates: all 6,677 parse as structurally valid nested directories (`nested_hog_valid`); 501 have an exact span with no trailing bytes (`nested_hog_exact_span`); 6,176 have a nonzero all-zero-padded tail (`nested_hog_zero_padded`); the zero-padding byte-length range observed across the corpus is `[16, 2032]` bytes (`nested_hog_padding_bytes_range`).
- Depth histogram of scanned asset spans: depth `-1` → 5 spans; depth `0` (top-level HOG entries) → 32,351 spans; depth `1` (entries found while descending into nested HOGs) → 20,925 spans (`analysis/formats/asset-fingerprints.json`, `scan.depth`).
- Total `asset_spans_scanned` across the corpus: 53,281 (`analysis/formats/asset-fingerprints.json`, `scan.asset_spans_scanned`).
- Top-level HOG aggregate: 273 files, 273 valid, 273 with exact span (no top-level trailing bytes observed), 32,351 total directory entries, zero recorded errors (`top_level_hog_errors: []`) (`analysis/formats/asset-fingerprints.json`).
- In a per-region example bucket (`minsk`), one instance container aggregate is recorded as: `DATA.HOG` → 301 entries, all 301 sector-aligned and zero-padded as nested spans, paired 1:1 with `.col`+`.vum` nested-extension patterns; `MAPTEX.HOG` → 2 entries with TDX version-word value `5`; `MAPVUM.HOG` → 5 entries, valid as nested spans but not sector-aligned or zero-padded (`analysis/formats/asset-fingerprints.json`, `minsk.containers`).
- Corpus-wide suffix histogram of what the scanner finds while descending into HOGs, aggregated across the whole disc (counts only, no per-file mapping): `.tdx` 15,248, `.vag` 8,665, `.vum` 7,036, `.col` 7,036, `.skm` 4,219, `.gun` 624, `.lpd` 862, `.par` 679, `.skl` 1,261, `.pss` 54, `.bnk` 77, `.gui` 77, `.bon` 156, `.vpk` 85, `.ska` 213, `.sub` 42, `.skf` 26, `.so` 139, `.skas` 2, `.skel` 4, `.bin` 12, `.fnt` 3, `.txt` 3, `.prn` 1, `.scc` 1 (`analysis/formats/asset-fingerprints.json`, `scan.extensions`).
- `standard_compression_magic_hits`: 0 across `standard_compression_spans_checked`: 46,604 — an aggregate observation only, attached to no specific member (`analysis/formats/asset-fingerprints.json`, `scan`).

## 5. Hypotheses

Explicitly labeled; none are asserted as fact.

- **H1 — `tag` is an archive-identity value rather than a fixed magic.** One example value is
  published in `analysis/formats/HOG.md`, while the grammar says the field varies. A fixed aggregate
  distinct-count measurement could test this without publishing values or per-archive rows.
- **H2 — nested-HOG zero tails reflect an alignment policy rather than payload grammar.** The
  tracked padding range is `[16, 2032]`, but the whole-disc manifest does not establish a sector
  size for each parent. Test only explicitly stated modulus candidates and report counts.
- **H3 — the 6,176:501 zero-padded-to-exact-span ratio for nested HOGs correlates with which top-level container class holds them** (e.g., `DATA.HOG`-family containers tend to be sector-aligned/zero-padded per the `minsk` example bucket, while smaller single-purpose containers like `MAPVUM.HOG` tend to be exact-span). *Confirming/refuting observation:* extend the per-container aggregate breakdown already emitted for the `minsk` example bucket in `asset-fingerprints.json` to every top-level container class disc-wide, and check whether the exact/zero-padded split clusters by container-name pattern — this is an aggregate count grouped by generic container name, already an established practice in the tracked JSON.

## 6. Missing observations

- **No published aggregate breakdown of the `tag` field's value distribution.** One example value
  appears in the tracked format note; no corpus-wide distinct count exists. Publish only the count
  of distinct values, not min/max values or a raw histogram.
- **No aggregate correlation between nested-HOG padding size and parent sector geometry.** The padding range `[16, 2032]` is reported corpus-wide but not broken down by alignment modulus. *Privacy-safe collection:* add an aggregate histogram of `padding_bytes mod 2048` (or the disc's actual sector size) to `analysis/formats/asset-fingerprints.json`.
- **No adversarial/resource-boundary test targeting the *top-level* `HogArchive::Open` path specifically.** The cited test evidence (`native/tests/hog_archive_tests.cpp`) demonstrates an overflowing-range rejection and a synthetic malformed *nested* buffer; tracked evidence does not show an equivalent adversarial case for the top-level `Open()` entry point (e.g., a crafted top-level file with a non-zero trailing byte, an entry count at or beyond `kMaximumEntryCount`, or a name-table length at `kMaximumNameLength`). *Privacy-safe collection:* extend the existing test file with synthetic (non-owner-derived) byte buffers exercising `kMaximumDirectorySize`, `kMaximumEntryCount`, and `kMaximumNameLength` boundaries for both `HogIndex::Open` and `HogArchive::Open`.
- **No fuzz/property-based test registration is visible in the tracked CMake test list beyond the fixed unit-test file.** *Privacy-safe collection:* a CMake-registered fuzzer or property-based test target seeded only with synthetic buffers (never owner-corpus payloads) covering the header/offset-table invariants already stated in `analysis/formats/HOG.md`.

## 7. Decoder/tooling status

**Classification: `canonical_decoder`**

Justification:
- Native structural parser + typed API: `native/include/omega/archive/hog_archive.h` declares `HogHeader`, `HogEntry`, `HogFileRange`, `HogIndex` (bounded-read index, `Open`/`OpenRange`, `find`), and `HogArchive` (full archive object). Implementation in `native/src/archive/hog_archive.cpp` performs checked arithmetic, bounded reads, and directory validation (see Confirmed #8).
- Registered in the build: `native/src/archive/hog_archive.cpp` is a compiled source in `CMakeLists.txt`; `native/tests/hog_archive_tests.cpp` is a registered test target in the same file.
- Exercised by a CLI: `native/apps/omega_tool/main.cpp` exposes `hog-info`, `hog-verify-tree`, and `hog-verify-nested-tree` commands built directly on the `HogIndex`/`HogArchive` API.
- Cross-validated by an independent second implementation: the Python reference in `tools/hog.py` / `tools/validate_hogs.py` and the native C++ implementation both validate the identical corpus counts (Confirmed #6, #7; ledger `E-0005` and `E-0007`).
- Supports safe extraction, not just structural indexing: path-safety-checked extraction is documented in `analysis/formats/HOG.md` and implemented in `tools/hog.py`.
- Gap noted for completeness (does not change the classification, since directory-structure decoding is fully proven): the adversarial/boundary test coverage for the top-level `HogArchive::Open` path specifically is not shown in tracked evidence to the same depth as the nested-range path (see Missing observation #3). Payload-level (entry-content) semantics remain entirely un-decoded — the decoder's proven scope is the container directory/span structure, not any entry's payload format.

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. No semantic speculation about payload roles.

1. **Highest priority — close the top-level adversarial-test gap.** Add synthetic (non-owner-derived) byte-buffer test cases to `native/tests/hog_archive_tests.cpp` for `HogArchive::Open`/`HogIndex::Open` mirroring the existing nested-range malformed-buffer and overflow tests: (a) entry count at/over `kMaximumEntryCount`, (b) directory size at/over `kMaximumDirectorySize`, (c) a name exceeding `kMaximumNameLength`, (d) a top-level file with one nonzero trailing byte after the logical end (must be rejected, unlike the nested-tail case). Register any new test binary target already covered by the existing CMake test entry (no new registration needed if added to the same file).
2. **Publish an aggregate `tag`-field distribution** by extending `tools/validate_hogs.py` or `tools/fingerprint_assets.py` to emit a count of distinct `tag` values (or a bucketed histogram) across the 273 top-level and 6,677 nested archives into the existing JSON outputs, to test Hypothesis H1 without publishing any per-file tag-to-name mapping.
3. **Publish an aggregate padding-vs-sector-modulus histogram** for the `nested_hog_padding_bytes_range` observation, to test Hypothesis H2, written as a new aggregate field in `analysis/formats/asset-fingerprints.json` (bucketed counts only, no per-file rows).
4. **Extend the per-container aggregate breakdown (currently only shown for the `minsk` example bucket) to every top-level container name disc-wide**, to test Hypothesis H3 — an aggregate table keyed by generic container name (e.g., `DATA.HOG`, `MAPVUM.HOG`) with entry/exact-span/zero-padded counts, following the schema already present in `asset-fingerprints.json` for `minsk`.
5. **Add a CMake-registered fuzz or property-based test target** seeded only with synthetic buffers, asserting the header/offset-table invariants already documented in `analysis/formats/HOG.md` (monotonic offsets from zero, table-boundary containment, all-zero-tail-only for nested spans) hold under randomized malformed input — strengthens the `canonical_decoder` classification's robustness evidence without touching owner payload data.
6. **Do not attempt payload-format decoding under this work order.** Entry-content interpretation (e.g., what a `.col`/`.vum`/`.tdx` entry inside a HOG means) is out of scope for this dossier and belongs to those families' own dossiers; extending HOG tooling should stop at the container/directory boundary.
