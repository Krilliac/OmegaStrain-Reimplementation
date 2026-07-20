# .skl — Evidence Dossier

## 1. Identity

`.skl` is a game-asset suffix found nested inside `.HOG` archives (e.g. `SKL.HOG` per-level
containers) in the Omega Strain retail corpus. At the evidence level this dossier can defend,
`.skl` is **ASCII, line-oriented, CR- or CRLF-delimited text** organized as a bounded list of
short tokenized records — the tracked evidence describes it as a "skeleton/loadout reference
list," not a decoded transform hierarchy, binary skeleton, or animation format. No byte in any
`.skl` record has been assigned a semantic role (bone name, joint index, weight, parent link,
loadout slot, etc.) by any tracked source.

## 2. Occurrence evidence

| Scope | Count | Source |
| --- | ---: | --- |
| Recursive, inside nested HOGs | 1,261 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".skl"]` and `formats.skl.count` |
| Top-level HOG member suffix | 636 | `analysis/formats/hog-validation.json` → `entry_extensions[".skl"]` |
| Whole-disc (loose files outside any HOG) | 2 | `analysis/manifests/disc-summary.json` → `extensions[".skl"]` |

Note the recursive count (1,261) and the top-level-HOG count (636) are drawn from two different
tallies in the pipeline (all nested-HOG members vs. top-level-archive-only members) and are not
expected to match; both are reproduced here exactly as the tracked JSON reports them.

## 3. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Source |
| --- | --- | --- |
| C1 | All 1,261 scanned `.skl` spans decode as ASCII (`fingerprint_skl` asserts `content.decode("ascii")`; non-ASCII spans are tallied separately and none were observed as ASCII-decode failures per the aggregate). | `tools/fingerprint_assets.py` (`fingerprint_skl`, lines ~298-320); `analysis/formats/asset-fingerprints.json` → `formats.skl.ascii = 1261` |
| C2 | Line-ending family is either CRLF (1,243 files) or bare-CR (18 files); no LF-only or mixed-ending file was observed among the 1,261. | `analysis/formats/asset-fingerprints.json` → `formats.skl.crlf_only = 1243`, `formats.skl.cr_only = 18`; `analysis/formats/ASSET-RECON.md` line 129 |
| C3 | Line count per file ranges 10–60. | `analysis/formats/asset-fingerprints.json` → `formats.skl.line_count_range = [10, 60]` |
| C4 | 1,212 of 1,261 files begin with the literal ASCII marker `BONENOSCALE`; the remaining 49 use a different first-line profile label. | `tools/fingerprint_assets.py` (`fingerprint_skl`: `text.startswith("BONENOSCALE")`); `analysis/formats/asset-fingerprints.json` → `formats.skl.starts_with_bonenoscale = 1212`; `analysis/formats/ASSET-RECON.md` line 130 |
| C5 | 1,099 of 1,261 files carry zero-byte padding after their logical text content (i.e. do not occupy their containing span exactly); 162 occupy their span exactly (`exact_text_span`). | `analysis/formats/asset-fingerprints.json` → `formats.skl.zero_padded = 1099`, `formats.skl.exact_text_span = 162` |
| C6 | Observed zero-padding length ranges 1–1,878 bytes; observed total span size ranges 176–2,048 bytes. | `analysis/formats/asset-fingerprints.json` → `formats.skl.padding_bytes_range = [1, 1878]`, `formats.skl.span_bytes_range = [176, 2048]` |
| C7 | The corpus summary table states: "Every file is ASCII line-oriented data," validated over 1,261 entries. | `analysis/formats/ASSET-RECON.md` line 28 |
| C8 | The published grammar note: files contain 10–60 lines; parser must accept both CRLF and CR-only delimiters; strip only trailing NUL padding and do not assume CRLF. | `analysis/formats/ASSET-RECON.md` lines 129–132 |
| C9 | A native passive descriptor `omega::retail::SklContainerDescriptor` / `InspectSklContainer` exists, builds a per-record `{line_region, token_region, terminator_region}` list plus a logical extent (`Exact` or `ZeroPaddedTail`), and explicitly documents that it "retains no input or token bytes and assigns no meaning to records from the observed marker family." | `native/include/omega/retail/skl_container_descriptor.h` |
| C10 | The decoder enforces a closed grammar derived from the aggregate observations: record count bounded to 10–60 (`kMinimumRecordCount=10`, `kMaximumRecordCount=60`), line length bounded to ≤29 bytes (`kMaximumLineBytes=29`), a single consistent line-ending family per file (rejects LF-only and rejects mixed CR/CRLF), tokens restricted to ASCII alphanumeric plus `_`, `-`, `.` with no embedded/leading spaces or control bytes, record 0 non-empty, record 0 with no dot, and exactly one dotted "marker" token which must appear at record index 3 and case-insensitively end in `.SKEL`. | `native/src/retail/skl_container_descriptor.cpp` (constants and `ValidateMarkerFamily`) |
| C11 | The decoder validates the zero-tail contract observed in C5/C6: it scans for the first NUL byte, treats everything from there to end-of-input as the padding region, and rejects any nonzero byte found after that boundary. | `native/src/retail/skl_container_descriptor.cpp` (`InspectSklContainer`, NUL-scan block) |
| C12 | The decoder enforces `DecodeLimits` (max input bytes, max items, max output bytes) and returns `LimitExceeded`/`Overflow`/`Malformed`/`UnsupportedVariant` typed errors rather than reading past a caller-supplied bound; overflow-checked `Add`/`Multiply` helpers guard record-count and output-size arithmetic. | `native/src/retail/skl_container_descriptor.cpp` |
| C13 | An adversarial unit-test suite (`SklContainerDescriptorFailureCount`) exercises: exact CRLF and CR-only decode with precise expected region offsets, an omitted final delimiter, an arbitrary zero-tail, an alternate first-line profile label, lowercase `.skel` marker acceptance, descriptor ownership independent of source-buffer lifetime, same-length token mutation stability, LF-only rejection, mixed CR/CRLF rejection, tab/control-character/path-separator/space-grammar violations, the 29-byte and 60-record ceilings, the 10-record floor, missing/moved/duplicated `.SKEL` marker rejection, an early-NUL-then-nonzero rejection, a dirty-tail-after-zero-boundary rejection, and exact-boundary success/one-below-failure pairs for `maximum_input_bytes`, `maximum_items`, and `maximum_output_bytes`. | `native/tests/skl_container_descriptor_tests.cpp` |
| C14 | The descriptor source and its test file are both registered in the build. | `CMakeLists.txt` lines 102 (`native/src/retail/skl_container_descriptor.cpp`) and 1404 (`native/tests/skl_container_descriptor_tests.cpp`) |
| C15 | A CLI tool path (`omega_tool`) consumes `InspectSklContainer` results into `SklStructuralStats` (record count, logical byte count, line-ending family count, unterminated-final-record count) as one of several asset kinds (`InputKind::Skl`) it can report on. | `native/apps/omega_tool/asset_commands.cpp` |
| C16 | Ledger entry E-0011 (confirmed) attests the passive fingerprint corpus "identifies stable structural families for TDX, SKM, SKL, VAG, LPD, PAR, COL, VUM, POP … without publishing payloads," citing the same fingerprint tool/JSON/doc used above. | `analysis/evidence/ledger.jsonl` (`E-0011`) |

## 4. Aggregate-only facts

No semantic interpretation attached — pure counts from the tracked aggregates.

- Recursive-scan extension histogram places `.skl` at 1,261 occurrences among the full scanned
  extension set (`.tdx` 15,248; `.vag` 8,665; `.col` 7,036; `.vum` 7,036; `.skm` 4,219; `.lpd`
  862; `.par` 679; `.ska` 213; `.vpk` 85; `.so` 139; `.skf` 26; `.skel` 4; `.skas` 2; etc.).
  Source: `analysis/formats/asset-fingerprints.json` → `scan.extensions`.
- Top-level-HOG member histogram places `.skl` at 636, alongside `.tdx` 11,166; `.vag` 8,665;
  `.skm` 2,808; `.lpd` 862; `.par` 679; `.ska` 212; `.vpk` 85; `.so` 139; `.skf` 26; `.skel` 4;
  `.skas` 2. Source: `analysis/formats/hog-validation.json` → `entry_extensions`.
- Whole-disc extension histogram places `.skl` at 2, alongside `.ska` 1, `.tdx` 2, `.pop` 18,
  `.tm2` 16, `.pf` 3 among 448 total files. Source: `analysis/manifests/disc-summary.json` →
  `extensions`.
- The fingerprint corpus's `scope` field states the whole exercise is "aggregate structural
  fingerprints only; no proprietary payloads exported." Source: `analysis/formats/
  asset-fingerprints.json` → `scope`.
- Two related but structurally distinct extension families exist in the same corpus and must
  not be conflated with `.skl`: `.skel` (4 occurrences) and `.skf` (26 occurrences). The tracked
  sources give `.skl` no defined relationship to `.skel`/`.skf` beyond the coincidence that the
  native decoder's own marker-token grammar independently requires a `.SKEL`-suffixed token to
  appear inside every `.skl` record at index 3 (Confirmed fact C10) — this is a fact about
  `.skl`'s internal grammar, not a claim about the separate `.skel`/`.skf` suffix families.

## 5. Hypotheses

Explicitly labeled; each includes the privacy-safe observation that would confirm or refute it.

- **H1 — "skeleton/loadout reference list" describes an indirection table, not raw skeleton
  data.** The `ASSET-RECON.md` prose (C7/C8) already frames `.skl` as a reference list rather
  than a transform hierarchy, but no tracked source states what the list is a reference *to*
  beyond the record-3 `.SKEL`-suffixed token. Confirm/refute: extend `fingerprint_assets.py` (or
  a successor aggregate-only tool) to report, in the aggregate JSON only, how many of the 1,261
  files' record-3 token collides with a corresponding `.skm`/`.ska`/`.skel`-family basename
  observed elsewhere in the same HOG — as a count, never as a per-file path list.
- **H2 — The 49 non-`BONENOSCALE` first-line labels form a small closed enumeration (e.g. a
  handful of alternate profile names), not 49 distinct one-off values.** Confirm/refute: add an
  aggregate-only histogram of distinct first-line values (label + count, no file identity) to
  the fingerprint JSON; if the 49 collapse into few distinct strings, that supports a bounded
  enum; if they are 49 distinct strings, H2 is refuted.
- **H3 — Record indices 1, 2, 4+ (the non-marker, non-profile lines) name bones/joints in a
  fixed or level-specific skeleton.** No tracked source assigns semantic meaning to these
  tokens; the native decoder explicitly disclaims doing so (C9). Confirm/refute: an aggregate
  cross-tabulation of record-index-vs-token-string frequency (counts only, no file paths) that
  shows a small repeating vocabulary across files would support a bone-name hypothesis; high
  per-file uniqueness at every non-marker index would refute it.
- **H4 — The 636 vs. 1,261 count discrepancy (top-level-HOG members vs. recursive scan) is
  explained entirely by nested-HOG-only `.skl` members (i.e., most `.skl` files live inside
  second-level HOGs, not as direct top-level HOG members).** Confirm/refute: add an aggregate
  depth breakdown for `.skl` specifically (depth 0 vs. depth 1, mirroring the existing
  `scan.depth` histogram) to the fingerprint JSON; if depth-1 count ≈ 1,261 − 636, H4 is
  confirmed.
- **H5 — The 176–2,048-byte span range and 1–1,878-byte padding range correlate with per-level
  record count (larger record counts → larger allocated span, e.g. fixed power-of-two or
  16/32-byte-quantized bucket sizes).** Confirm/refute: add an aggregate-only scatter/histogram
  of `(record_count, span_bytes)` pairs (counts only) to the fingerprint JSON; a small number of
  distinct span "buckets" correlated with line-count ranges would support quantized allocation;
  a continuous spread would refute it.

## 6. Missing observations

What tracked evidence does not exist, and the privacy-safe collection that would produce it.

- No tracked source reports the distinct *set* of non-`BONENOSCALE` first-line profile labels
  (only the count, 49). **Collection:** an aggregate-only frequency table (label string → count)
  emitted into the fingerprint JSON — labels are short ASCII tokens already exposed as format
  grammar, not private payload identity, so this stays within the aggregate-only rule as long as
  no file path/hash is attached to a row.
- No tracked source reports which HOG *families* (by generic container name, e.g. `SKL.HOG` vs.
  a nested nested-HOG) contribute the 636 top-level vs. 1,261 recursive counts. **Collection:**
  extend the existing per-archive tally in `hog-validation.json`'s style (already grouped by
  generic container name, not per-owner-file) to add a `.skl`-specific nested-vs-top-level
  breakdown.
- No tracked source establishes any cross-format linkage between a `.skl` file's record-3
  `.SKEL`-suffixed token and any other tracked family (`.skm`, `.ska`, `.skel`, `.skf`).
  **Collection:** an aggregate-only "token resolves within same container" boolean/count,
  computed and reported as a single corpus-wide percentage, never as a resolved path.
- No tracked source reports whether the 18 CR-only files cluster in specific top-level archives
  or are spread evenly. **Collection:** an aggregate count of CR-only occurrences bucketed by
  generic archive-name pattern (e.g. `SKL.HOG` vs. other container names already public in
  `HOG.md`), not by individual disc path.
- No adversarial fuzz/mutation-count metric (e.g. how many of a corpus of synthetic malformed
  inputs the decoder correctly rejects) has been run against the owner corpus itself — the
  existing adversarial coverage (C13) is entirely synthetic/hand-built, not corpus-driven.
  **Collection:** run the existing `InspectSklContainer` decoder against all 1,261 tracked
  `.skl` spans from the owner's private extraction (locally, never publishing payload) and
  report only the aggregate accept/reject count and, for any rejects, the `DecodeErrorCode`
  histogram — this validates the decoder's C10 grammar against real data without exporting
  content.

## 7. Decoder/tooling status

**Classification: `passive_descriptor_only`**

- `omega::retail::InspectSklContainer` (declared `native/include/omega/retail/
  skl_container_descriptor.h`, implemented `native/src/retail/skl_container_descriptor.cpp`) is
  registered in the build at `CMakeLists.txt:102`, with its adversarial test file
  `native/tests/skl_container_descriptor_tests.cpp` registered at `CMakeLists.txt:1404`, and is
  consumed by the CLI tool at `native/apps/omega_tool/asset_commands.cpp`.
- It goes beyond a bare structural envelope (which would only compute a logical extent): it
  fully parses the record grammar into per-record `{line, token, terminator}` byte ranges and
  validates a closed set of grammar rules (record-count bounds, line-length bound, single
  line-ending family, token character set, marker-position rule) derived directly from the
  aggregate observations in Section 4. This justifies `passive_descriptor_only` rather than
  `structural_envelope_only`.
- It explicitly stops short of `canonical_decoder`: the header comment states the descriptor
  "retains no input or token bytes and assigns no meaning to records from the observed marker
  family" — i.e., it does not resolve, name, or interpret what any record token *means*
  (no bone/joint semantics, no loadout-slot semantics, no hierarchy).
- Adversarial/resource-boundary coverage is present and substantial (C13: malformed grammar,
  mixed line endings, boundary values for all three `DecodeLimits` fields, buffer-lifetime
  independence). The one gap is that this coverage is entirely synthetic (`MakeSkl`/`MakeRecords`
  helpers) — no tracked evidence shows the decoder has been run against the real owner corpus of
  1,261 files (see Missing observation "no adversarial fuzz/mutation-count metric… against the
  owner corpus").

## 8. Codex work order

Ranked, privacy-safe, concrete. No menu-role or semantic speculation.

1. **(Highest priority) Corpus-validate the existing decoder.** Run `InspectSklContainer` over
   all 1,261 tracked `.skl` spans in the owner's private extraction (locally; never publish
   payload bytes) and report only: accept/reject counts, and for any rejects, the
   `DecodeErrorCode` histogram plus which specific grammar rule (record-count bound, line-length
   bound, marker-position rule, etc.) fired. This is the fastest way to discover whether C10's
   grammar (derived from 1,261 aggregate observations already) is complete, or whether real data
   exercises a path the synthetic test suite (C13) does not cover.
2. Extend `tools/fingerprint_assets.py`'s `fingerprint_skl` to emit the H2 first-line-label
   frequency table (label string → count, no file identity) into `asset-fingerprints.json`, to
   turn H2 into a Confirmed/Aggregate-only fact.
3. Extend the same tool to add a `.skl`-specific depth-0-vs-depth-1 breakdown (mirroring
   `scan.depth`) to resolve H4 (the 636-vs-1,261 discrepancy).
4. Add an aggregate `(record_count, span_bytes)` pair histogram to test H5's quantized-allocation
   hypothesis, reusing the existing `Aggregate.observe` pattern already in `fingerprint_assets.py`.
5. If step 1 and step 2 both hold cleanly (decoder never rejects real data; first-line labels
   collapse into a small enum), promote the "skeleton/loadout reference list" framing from
   Section 1 to a corpus-attested claim in a follow-up `ASSET-RECON.md` revision and log a new
   ledger entry citing the corpus-validation run — do not add semantic names for individual
   record tokens (bone names, loadout slots) without a mechanically citable source establishing
   them; that remains out of scope for this family per the clean-room rules.
6. Only after 1–5: investigate H1/H3 (cross-format token resolution to `.skm`/`.ska`/`.skel`)
   via the aggregate-only "token resolves within same container" percentage described in
   Section 6 — this is lower priority because it requires new cross-format aggregate tooling
   rather than extending the existing single-format scanner.
