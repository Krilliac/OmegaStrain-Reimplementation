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

**Classification: `canonical_decoder`**

Justification:
- Native structural parser + typed API: `native/include/omega/archive/hog_archive.h` declares `HogHeader`, `HogEntry`, `HogFileRange`, `HogIndex` (bounded-read index, `Open`/`OpenRange`, `find`), and `HogArchive` (full archive object). Implementation in `native/src/archive/hog_archive.cpp` performs checked arithmetic, bounded reads, and directory validation (see Confirmed #8).
- Registered in the build: `native/src/archive/hog_archive.cpp` is a compiled source in `CMakeLists.txt`; `native/tests/hog_archive_tests.cpp` is a registered test target in the same file.
- Exercised by a CLI: `native/apps/omega_tool/main.cpp` exposes `hog-info`, `hog-verify-tree`, and `hog-verify-nested-tree` commands built directly on the `HogIndex`/`HogArchive` API.
- Cross-validated by an independent second implementation: the Python reference in `tools/hog.py` / `tools/validate_hogs.py` and the native C++ implementation both validate the identical corpus counts (Confirmed #6, #7; ledger `E-0005` and `E-0007`).
- Supports safe extraction, not just structural indexing: path-safety-checked extraction is documented in `analysis/formats/HOG.md` and implemented in `tools/hog.py`.
- Gap noted for completeness (does not change the classification, since directory-structure decoding is fully proven): the adversarial/boundary test coverage for the top-level `HogArchive::Open` path specifically is not shown in tracked evidence to the same depth as the nested-range path (see Missing observation #3). Payload-level (entry-content) semantics remain entirely un-decoded — the decoder's proven scope is the container directory/span structure, not any entry's payload format.

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
