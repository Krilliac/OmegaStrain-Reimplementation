# .MAP — Dossier

**Identity (evidence-level):** A whole-disc-only suffix with exactly one tracked instance: a 1,138-byte file whose entire content decodes as ASCII. `tools/fingerprint_assets.py` runs a dedicated aggregate pass over every `*.MAP` file (`direct_map_summary()`), but `.map` has no entry in `FORMAT_HANDLERS` — no structural, per-field decoder exists. One published tracked document (`analysis/formats/ASSET-RECON.md`) offers a hedged, explicitly non-committal characterization of the single file's content ("resemble a character-code mapping") and separately argues the *actual* level-geometry pipeline is POP + nested COL/VUM, not `.map`. Whether this suffix is a PS2 system/OS artifact (e.g., a toolchain-emitted linker/symbol map) or a game data asset is **not established** by tracked evidence — this dossier does not assert either, and per the clean-room rules the exact on-disc path/filename backing the single occurrence is withheld (only the aggregate counts and the one hedged doc statement are cited).

## 1. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory; `.map` has no key under `formats`) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts; no `.map` key) |
| Whole-disc (anywhere on the extracted disc tree) | 1 | `analysis/manifests/disc-summary.json`, key `extensions[".map"] = 1`; corroborated by exactly one matching row in `analysis/manifests/disc-files.jsonl` |

`.map` is therefore a whole-disc-only suffix: it exists as a top-level filesystem entry on the disc image but never as packed content inside any `.hog` container, recursive or top-level. Per the clean-room rules, the specific path, filename, size, and hash of that one `disc-files.jsonl` row are per-file/private-input detail and are withheld from this dossier; only the aggregate figures from `disc-summary.json` and `asset-fingerprints.json` are cited below.

**Naming-collision note:** `tools/fingerprint_assets.py`'s `minsk_container_summary()` references per-level HOG container files named with a `MAP` prefix (e.g., a `MAPVUM.HOG` / `MAPTEX.HOG` pattern under the MINSK level). Those are `.hog` archive containers, not members of the `.map` suffix family being profiled here, and are not counted in any of the rows above. They are noted only to avoid conflating an unrelated "MAP"-prefixed container name with this suffix.

## 2. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| 1 | `tools/fingerprint_assets.py` defines `direct_map_summary(disc_root)`, which globs `disc_root.rglob("*.MAP")` and computes an aggregate-only record: `file_count`, `file_bytes` (min/max range via the shared `Aggregate.observe`), and an `ascii_text` / `non_ascii` split per file (via `data.decode("ascii")` success/failure) — no filenames or content are retained in the output. | `tools/fingerprint_assets.py`, function `direct_map_summary` |
| 2 | `FORMAT_HANDLERS` (the dict mapping suffix → structural fingerprint function used by `scan_asset()`) contains only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`. `.map` is **not** a key, so no per-field/header structural decode is ever run on it. | `tools/fingerprint_assets.py`, `FORMAT_HANDLERS` dict |
| 3 | `analysis/formats/asset-fingerprints.json` carries a top-level `direct_map_files` aggregate block: `{"ascii_text": 1, "file_bytes_range": [1138, 1138], "file_count": 1}`. | `analysis/formats/asset-fingerprints.json`, key `direct_map_files` |
| 4 | `analysis/manifests/disc-summary.json` records `extensions[".map"] = 1` out of `file_count: 448` whole-disc files. | `analysis/manifests/disc-summary.json` |
| 5 | `analysis/formats/ASSET-RECON.md` states, verbatim: "The only standalone `.MAP` on the disc is 1,138 bytes of ASCII whose contents resemble a character-code mapping rather than geometry. Current evidence points to the POP plus nested COL/VUM chain as the world/map geometry path." This is cited as a Confirmed fact only in the sense that the tracked document makes this hedged statement — the underlying semantic claim ("character-code mapping") is explicitly non-committal in the source ("resemble") and is treated as inherited Hypothesis material in §4, not as a decoded semantic. | `analysis/formats/ASSET-RECON.md`, line ~76 |
| 6 | No entry in `analysis/evidence/ledger.jsonl` decodes `.map` or asserts a structural/semantic claim about it; the one hit for the substring "map" is `E-0011` (confirmed), whose claim text includes the phrase "map-associated data" in a list of aggregate asset-family coverage — an aggregate label, not a `.map`-suffix decode or claim. | `analysis/evidence/ledger.jsonl`, entry `E-0011` |
| 7 | No file under `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, or `native/src/**/*.cpp` defines a `.map`-specific type, descriptor, or decoder; the only "MAP"-adjacent hits in `native/` are unrelated `MAPTEX.HOG` container-name literals in a texture-store test fixture, not a `.map`-suffix decoder. `CMakeLists.txt` registers no `.map`-related source or test target. | `native/tests/level_texture_store_tests.cpp` (unrelated hit); `CMakeLists.txt` (absence) |

## 3. Aggregate-only facts

- With n=1 whole-disc occurrence and n=0 HOG occurrences, the only real "distribution" is the single-file datum reported in `asset-fingerprints.json`'s `direct_map_files` block: `file_count=1`, `file_bytes_range=[1138,1138]` (a degenerate single-point range), `ascii_text=1` / `non_ascii=0` (the entire file content is valid ASCII per Python's strict `ascii` codec).
- `disc-summary.json`'s whole-disc extension histogram places `.map` alongside other single-count extensions (`.64`, `.bd`, `.hd`, `.icn`, `.ini`, `.log`, `.sys`, `.tbl`, `.wdb`) and multi-count game-data extensions (`.hog`:273, `.pop`:18, `.tm2`:16, `.irx`:46, etc.) in the same 448-file whole-disc inventory. This is a generic co-occurrence observation; no semantic or grouping relationship between `.map` and any other listed suffix is asserted.
- The `direct_map_summary()` scan is disc-wide (`disc_root.rglob("*.MAP")`) and independent of the top-level-directory byte totals published in `disc-summary.json`'s `top_level_bytes`; that JSON records byte totals per top-level bucket but does not associate any specific extension with any specific bucket, so no tracked source lets this dossier attribute the single `.map` file to a named top-level directory without consulting the withheld per-file path.

## 4. Hypotheses

All statements below are explicitly labeled hypotheses, not facts. None are asserted in the dossier body above.

- **H1 — Character/text-encoding mapping table.** Echoing `ASSET-RECON.md`'s hedged "resemble a character-code mapping" language, the file could be a font-glyph map, subtitle/text charset table, or localization codepage table used by the game's text-rendering pipeline.
  - *Privacy-safe confirmation/refutation:* extend `direct_map_summary()` (or add a sibling passive descriptor) to emit aggregate token-shape statistics over the ASCII content — line count, average line length, delimiter-character histogram (e.g., `=`, `:`, `,`), character-class histogram — without emitting the raw text or filename. A structure consistent with a repeated `<code>maps-to<glyph/index>` pattern would move this toward Confirmed; a mismatch would weaken it without fully refuting it (n=1).
- **H2 — PS2 toolchain/system artifact (e.g., a linker/symbol map file).** `.map` is a common suffix for build-toolchain-emitted linker/symbol map files (address-to-symbol tables) on many platforms including PS2 SDKs, and such files are plain ASCII, consistent with the observed `ascii_text=1` result.
  - *Privacy-safe confirmation/refutation:* the same aggregate token-shape descriptor proposed in H1 could also test for hex-address-shaped tokens per line (an aggregate count/ratio, not the tokens themselves). A high ratio of hex-address-like tokens would support H2 over H1; a low ratio would weaken H2. Neither H1 nor H2 can be adjudicated with the currently tracked evidence (only size + ascii/non-ascii split exist today).
- **H3 — Not the world/level-map geometry format (noted as a disfavored hypothesis).** `ASSET-RECON.md` already argues the level-geometry pipeline runs through POP plus nested COL/VUM containers, explicitly distinguishing that path from this standalone `.map` file. Treating `.map` as "the" level/world-geometry format would contradict this tracked analysis; it is recorded here only to be explicitly ruled out as a live hypothesis, not pursued further.

## 5. Missing observations

- **No passive descriptor extracts internal structure** (line count, token/delimiter shape, character-class histogram) for `.map` — the existing `direct_map_summary()` reports only `file_count`, a size range, and an ascii/non-ascii split. This is the single biggest gap blocking H1/H2 adjudication in §4.
- **No additional same-suffix samples exist in the tracked corpus** (whole-disc count = 1, HOG counts = 0), so no genuine size/content distribution can be built from the current pipeline output; any future aggregate beyond the single-point range in §3 requires either a differently-sourced disc/region dump processed through the same privacy-safe pipeline, or a widened passive descriptor run against the existing single sample.
- **No ledger entry** (`analysis/evidence/ledger.jsonl`) records a confirmed or rejected claim about `.map`.
- **No native decoder, descriptor type, or CMake/CTest registration** references the `.map` suffix family (the only "MAP"-prefixed native hits are unrelated `.hog` container-name literals, see §1 naming-collision note).
- **No cross-reference has been attempted against other published tracked grammar docs** (`HOG.md`, `TDX.md`, `COL.md`, `VUM.md`, `POP.md`, etc.) to see whether any of them independently reference or explain the `.map` file's role; the only tracked mention found is the single hedged sentence in `ASSET-RECON.md` cited in §2.

## 6. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- Justification: `tools/fingerprint_assets.py` runs a dedicated aggregate pass (`direct_map_summary()`) over every `.map` file on the disc, producing the `direct_map_files` block in `asset-fingerprints.json` (§2 fact 1, §3). This is more than the pure generic-histogram tally some other unhandled suffixes receive (it adds an ASCII/non-ASCII classification specific to `.map`), but it still performs no field-level, header-level, or record-level structural extraction.
- It is **not** `passive_descriptor_only` because a passive descriptor (in this repo's convention, as used for `.tdx`/`.col`/`.vum` etc. via `FORMAT_HANDLERS`) parses declared internal structure (header words, record counts, section tags); `direct_map_summary()` does none of that — it only classifies the whole file as ASCII/non-ASCII and records its size.
- It is **not** `structural_envelope_only` or `canonical_decoder`: `.map` has no `FORMAT_HANDLERS` entry (§2 fact 2), and no `native/` header, descriptor type, or CMake/CTest target references the suffix (§2 fact 7).
- It is **not** `system_file_out_of_scope`: the sole tracked doc mention (`ASSET-RECON.md`, §2 fact 5) explicitly hedges rather than classifying the file as a PS2 system/OS artifact, and no other tracked source asserts a system-file role (see §4 H2, which remains an open hypothesis, not a finding).
- No adversarial or resource-boundary test exists for `.map` because no structural decoder exists to test against; this is a decoder-absence gap, not a hardening gap in an existing decoder.

## 7. Codex work order

Ranked, privacy-safe, no semantic speculation:

1. **Extend `direct_map_summary()` in `tools/fingerprint_assets.py` to emit aggregate token-shape statistics** for the ASCII content — line count, average/line-length distribution, delimiter-character histogram, and a hex-address-token ratio — while continuing to emit zero filenames, paths, or raw text. This is the single highest-priority action: it is the only proposed change that could move either H1 (character/text-encoding map) or H2 (toolchain linker/symbol map) from Hypothesis toward Confirmed using only the already-tracked, privacy-safe pipeline.
2. **Re-run the extended scanner against the existing tracked corpus** (no new inputs) and record the resulting aggregate JSON into `analysis/formats/asset-fingerprints.json`'s `direct_map_files` block, so future dossier revisions have real structural aggregates instead of only size + ascii/non-ascii.
3. **Grep the full set of published `analysis/formats/*.md` docs and `README.md` once more** (case-insensitive, whole-word `map`) to positively confirm no other tracked document already classifies this suffix's role beyond the single `ASSET-RECON.md` sentence found in this pass.
4. **If step 1's token-shape aggregate surfaces a pattern matching a grammar already published in a tracked `*.md` doc** (this repo's own docs, not external plausibility), promote the matching hypothesis in §4 to Confirmed with that citation; otherwise leave both H1 and H2 open.
5. **Do not** attempt to open, print, name, or hash-fingerprint the single private `.map` instance beyond what `disc-summary.json` / `asset-fingerprints.json` already record in aggregate; do not access `private/`, `runtime/`, `third_party/` (including `third_party/pcsx2`), or `downloads/` in pursuit of a second `.map` sample.
