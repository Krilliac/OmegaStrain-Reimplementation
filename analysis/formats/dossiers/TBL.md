# .TBL — Dossier

**Identity (evidence-level):** A whole-disc-only suffix with exactly one tracked instance. It is not a PS2 system/OS artifact by any tracked evidence (no boot-config, IOP-module, or save-icon association is asserted anywhere in the tracked corpus), so this is written as a full dossier rather than a `system_file_out_of_scope` stub. The suffix is a member of the fixed, publicly-committed extension vocabulary of the front-end recursive-HOG-topology measurement tool (`tools/measure_frontend_hog_topology.py`), which pre-assigns it the internal category label `"table"` alongside twelve other approved suffixes. That category label is a name the tool's own tracked source code assigns to the suffix bucket — it is cited here as a fact about the tool's fixed vocabulary, not as a confirmed statement about the internal byte-level structure or role of the one real `.tbl` file on the disc, which no tracked scanner has ever structurally decoded. Per the clean-room rules, the exact on-disc path/filename backing the single whole-disc occurrence is withheld; only aggregate counts and already-published tracked-doc/tracked-code statements are cited below.

## 1. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (inside any `.hog` archive, any depth) | 0 | `analysis/formats/asset-fingerprints.json` (recursive suffix inventory; `.tbl` has no key under `formats`) |
| Top-level-in-HOG (direct members of a top-level `.hog`) | 0 | `analysis/formats/hog-validation.json` (top-level HOG member-suffix counts; no `.tbl` key) |
| Whole-disc (anywhere on the extracted disc tree) | 1 | `analysis/manifests/disc-summary.json`, key `extensions[".tbl"] = 1`; corroborated by exactly one matching row in `analysis/manifests/disc-files.jsonl` |

`.tbl` is therefore a whole-disc-only suffix: it exists as a top-level filesystem entry on the disc image but has never been observed as packed content inside any `.hog` container, recursive or top-level, in the tracked corpus. Per the clean-room rules, the specific path, filename, size, and hash of that one `disc-files.jsonl` row are per-file/private-input detail and are withheld from this dossier.

## 2. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| 1 | `tools/measure_frontend_hog_topology.py` defines `APPROVED_EXTENSION_CATEGORIES`, a fixed dict mapping suffix → category label used by its aggregate topology scanner. `.tbl` is one of its thirteen keys, mapped to the category label `"table"` (`APPROVED_EXTENSION_CATEGORIES[".tbl"] = "table"`). | `tools/measure_frontend_hog_topology.py`, `APPROVED_EXTENSION_CATEGORIES` dict |
| 2 | `analysis/formats/FRONTEND-TOPOLOGY.md` publicly documents that its aggregate contract reports "fixed counts for the approved public suffixes `.col`, `.hog`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, and `.vum`" and "fixed category counts for animation, audio, collision, container, material, mesh, scene, script, skeleton, table, text, and texture" — confirming `.tbl`/`"table"` is part of the tool's frozen public vocabulary. | `analysis/formats/FRONTEND-TOPOLOGY.md` (§ Aggregate contract) |
| 3 | `tools/tests/test_measure_frontend_hog_topology.py` exercises the `.tbl` → `"table"` mapping only against synthetic, in-memory fixture names (e.g. a fabricated `*.TBL` member paired with a `category` assertion of `b"table"`, and a synthetic same-basename `.tbl`+`.txt` pair-total assertion). No owner/private input is used by these tests. | `tools/tests/test_measure_frontend_hog_topology.py` |
| 4 | The ledger entry for this tool, `E-0086`, states explicitly that "the public E-0086 claim does not include an owner-corpus scan" and that no owner/private/disc-image/executable/PCSX2 input was accessed in producing it — so the tool's `.tbl` classification has never been run against the real disc corpus in any tracked artifact. | `analysis/evidence/ledger.jsonl`, entry `E-0086` |
| 5 | `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict (the suffix → structural-fingerprint-function registry) contains only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`. `.tbl` is **not** a key, so no per-field/header structural decode is ever run on it. | `tools/fingerprint_assets.py`, `FORMAT_HANDLERS` dict |
| 6 | `tools/fingerprint_assets.py` contains no `.tbl`-specific aggregate function analogous to `direct_map_summary()` (the dedicated ASCII/non-ASCII, size-range pass it runs over every `.MAP` file). A targeted search of the file for `tbl` (case-insensitive) returns zero matches. | `tools/fingerprint_assets.py` |
| 7 | `tools/generate_manifest.py` (the tool that produces `disc-summary.json`/`disc-files.jsonl`) computes only a generic, suffix-agnostic extension histogram (`Counter` over every file extension) plus per-file `path`/`sha256`/`size` rows; it contains no `.tbl`-specific branch or classification logic. | `tools/generate_manifest.py` |
| 8 | No file under `native/include/omega/retail/*.h`, `native/include/omega/asset/*.h`, or `native/src/**/*.cpp` contains the literal `TBL` (case-insensitive); a targeted search of `native/` returns zero matches. `CMakeLists.txt` registers no `.tbl`-related source or test target (a targeted case-insensitive search of `CMakeLists.txt` for `tbl` returns zero matches). | `native/` (absence); `CMakeLists.txt` (absence) |
| 9 | `analysis/formats/ASSET-RECON.md` contains no mention of `.tbl`/`TBL` (a targeted case-insensitive search returns zero matches). | `analysis/formats/ASSET-RECON.md` |
| 10 | No entry in `analysis/evidence/ledger.jsonl` makes any confirmed or rejected claim specifically about `.tbl` file content, structure, or role; the only ledger entry that even names `.tbl` is `E-0086`, and that entry's claim is scoped to the topology tool's fixed vocabulary and explicitly disclaims any owner-corpus scan (see fact 4). | `analysis/evidence/ledger.jsonl` |

## 3. Aggregate-only facts

- With n=1 whole-disc occurrence and n=0 HOG occurrences (recursive and top-level), no size distribution, byte-range, or content classification exists for `.tbl` in any tracked pipeline output — unlike `.map`, no dedicated aggregate scanner has ever been pointed at the one real `.tbl` file to record even a size range or an ASCII/non-ASCII split.
- `disc-summary.json`'s whole-disc extension histogram places `.tbl` alongside other single-count extensions (`.64`, `.bd`, `.hd`, `.icn`, `.ini`, `.log`, `.map`, `.sys`, `.wdb`) and multi-count game-data extensions (`.hog`:273, `.pop`:18, `.tm2`:16, `.irx`:46, etc.) in the same 448-file whole-disc inventory. This is a generic co-occurrence observation; no semantic or grouping relationship between `.tbl` and any other listed suffix is asserted.
- The front-end HOG-topology tool's fixed vocabulary independently groups `.tbl` under the category label `"table"` alongside the twelve other approved suffixes (`.col`→collision, `.hog`→container, `.pop`→scene, `.ska`/`.skas`→animation, `.skl`→skeleton, `.skm`→mesh, `.so`→script, `.tdx`→texture, `.txt`→text, `.vag`→audio, `.vum`→material). This category assignment is a fact about the tool's own tracked source code and its intended scope (front-end HOG containment topology), not an observed or decoded property of the disc's one real `.tbl` file, which the tool has never scanned (fact 4).

## 4. Hypotheses


No new hypothesis is promoted here. The established evidence above remains the claim ceiling, and
this dossier authorizes no owner-corpus measurement recipe. Before any future measurement is
implemented, a separate reviewed contract must predeclare its fixed public schema, fixed minimum
cohort threshold, bounded execution and typed failures, and project-generated privacy tests.

An authorized report may contain only fixed anonymous corpus-wide totals for cohorts meeting that
threshold. Smaller cohorts must collapse to one typed suppression result. The report must not emit
raw values, signatures, payloads, owner-derived strings, paths, file, container, or archive names,
suffix-derived labels, per-file, per-container, or per-archive rows, or cross-tabulations keyed by
raw fields.

## 5. Missing observations


Unresolved structural, semantic, consumer, and validation questions remain missing observations.
This section deliberately defines no executable collection recipe. Closing any gap requires the
separately reviewed contract and suppression policy stated above; absent that contract, the gap
remains UNKNOWN.

## 6. Decoder/tooling status

**Classification: `unknown`**

- Justification: no tracked artifact has ever produced even an aggregate size-range or content-shape observation about the one real `.tbl` file. This distinguishes `.tbl` from `.map`, which received a dedicated (if minimal) aggregate pass (`direct_map_summary()`) and therefore qualifies as `aggregate_scanner_only`. For `.tbl`, the only two tracked touchpoints are (a) `generate_manifest.py`'s fully generic, suffix-agnostic extension histogram, which treats `.tbl` no differently from any other extension on the disc, and (b) `measure_frontend_hog_topology.py`'s fixed vocabulary entry, which assigns a category label in source code but has never been run against owner-corpus data (per `E-0086`) and, independently, would report zero occurrences for `.tbl` even if run, since the real HOG corpus contains none (§1).
- It is **not** `aggregate_scanner_only`: that classification (as applied to `.map`) requires a suffix-specific aggregate pass that has actually characterized the real file's content; no such pass exists for `.tbl`.
- It is **not** `passive_descriptor_only`, `structural_envelope_only`, or `canonical_decoder`: `.tbl` has no `FORMAT_HANDLERS` entry (§2 fact 5), and no `native/` header, descriptor type, or CMake/CTest target references the suffix (§2 fact 8).
- It is **not** `system_file_out_of_scope`: no tracked source asserts or implies a PS2 system/OS role for this suffix; the only tracked characterization at all is the front-end-topology tool's category label `"table"`, which is a generic front-end-data label, not a system-file classification.
- No adversarial or resource-boundary test exists for `.tbl` specifically because no structural decoder or dedicated aggregate scanner exists to test against; the topology tool's own resource-boundary tests (entry/name/size/depth limits, malformed offsets, etc., per `analysis/formats/FRONTEND-TOPOLOGY.md` § Synthetic verification) exercise its general HOG-traversal machinery using synthetic `.tbl`-named fixtures among others, but that is boundary-hardening of the *traversal* code, not of any `.tbl`-specific content decoder.

## 7. Codex work order


1. Preserve the established facts, aggregates, decoder classification, and nonclaims above.
2. Before implementing or running any new owner-corpus measurement, land a separate reviewed
   contract that freezes its public schema, hard bounds, typed failures, deterministic behavior,
   synthetic privacy tests, and fixed minimum cohort threshold.
3. Permit only fixed anonymous corpus-wide totals for cohorts meeting that threshold.
4. Collapse every smaller cohort to one typed suppression result; do not publish a partial result.
5. Reject any contract or output containing raw values, signatures, payloads, owner-derived strings,
   paths, file, container, or archive names, suffix-derived labels, per-file, per-container, or
   per-archive rows, or cross-tabulations keyed by raw fields.
