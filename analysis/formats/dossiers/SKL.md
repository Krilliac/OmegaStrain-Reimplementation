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
| Mixed structural-fingerprinter candidates (direct depth −1 plus recursive HOG members) | 1,261 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".skl"]` and `formats.skl.count`; `tools/fingerprint_assets.py` (`scan_disc`) |
| Recursive HOG-member occurrences at all archive depths | 1,259 | Mixed total minus the 2 directly scanned filesystem entries; `tools/fingerprint_assets.py` (`scan_disc`) |
| Top-level HOG member suffix | 636 | `analysis/formats/hog-validation.json` → `entry_extensions[".skl"]` |
| Whole-disc filesystem entries, also scanned directly at depth −1 because `.skl` is handled | 2 | `analysis/manifests/disc-summary.json` → `extensions[".skl"]`; `tools/fingerprint_assets.py` (`scan_disc`) |

The 1,261 total is deliberately mixed: `scan_disc` recursively scans HOG members and then directly
scans handled filesystem entries at depth −1. Subtracting the two direct `.skl` candidates yields
1,259 recursive HOG members. Against the 636 direct top-level HOG members, that leaves 623 members
at deeper HOG depth. Structural facts over 1,261 candidates include the two direct files and must
not be labeled HOG-only.

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

- Mixed-candidate extension histogram places `.skl` at 1,261 occurrences among the full scanned
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


1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
