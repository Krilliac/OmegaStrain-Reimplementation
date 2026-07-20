# .txt — Format Dossier

## 1. Identity

`.txt` is a recursive-suffix bucket recorded by the asset scanner, and it is
also one of the thirteen suffixes explicitly named in the frozen output
vocabulary of the front-end HOG-topology measurement tool, where it is
mapped to the category label `"text"`. At the current evidence level it is
an **unclassified generic-suffix bucket for game-asset purposes**: no
tracked source defines a header magic, field grammar, or semantic role for
in-HOG `.txt` members, and no native decoder or descriptor exists for the
literal `.txt` suffix. Its true nature (a single homogeneous in-game text
format vs. a grab-bag of unrelated plain-text files sharing the suffix
`.txt`) is **UNKNOWN** and must not be guessed.

Two tracked source files (`native/src/retail/par_text_envelope_decoder.cpp`,
`native/src/retail/skas_text_envelope_decoder.cpp`) implement "text-envelope"
decoders, but these decode the `.par` and `.skas` suffix families
respectively — files whose *content* happens to be structured as
seven-bit-ASCII/CRLF text, not files whose *suffix* is literally `.txt`. This
dossier keeps that distinction explicit: no tracked decoder targets the
`.txt` suffix itself.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG (all archive depths, scanner's asset-span walk) | 3 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".txt"]`) |
| Top-level-HOG member census (depth-0 HOG directories only) | 0 | `analysis/formats/hog-validation.json` (`entry_extensions`, `.txt` key absent) |
| Whole-disc file histogram (all disc files, HOG or not) | 8 | `analysis/manifests/disc-summary.json` (`extensions[".txt"]`) |

The recursive-in-HOG count (3) exceeding the top-level-HOG count (0) is
consistent with all 3 observed `.txt` HOG members living at a nested archive
depth (depth ≥ 1, i.e. inside a HOG that is itself embedded in another HOG),
since `hog-validation.json`'s `entry_extensions` census — which counts only
depth-0 (top-level) HOG members — records zero `.txt` entries while the
scanner's full recursive walk records 3. This dossier does not assert the
depth-1 placement as a separately sourced "Confirmed" fact beyond noting
that the two counts are compatible with it; no tracked source states the
per-member depth explicitly for `.txt`.

The whole-disc count (8) is larger than the in-HOG count (3) because the
whole-disc histogram in `analysis/manifests/disc-summary.json` /
`analysis/manifests/disc-files.jsonl` counts every `.txt`-suffixed file on
the disc image, including files that live outside any HOG container. One
such non-HOG `.txt` file is independently attested in
`analysis/evidence/ledger.jsonl` (entry E-0079, discussing a distributed
license text), which is a generic open-source-license/build-artifact file
and out of scope for this game-asset dossier per the task's system-file
carve-out. The in-HOG count (3) is the game-asset-relevant subset; the
remaining 5 of the 8 whole-disc `.txt` files are of unknown disc placement
(HOG-adjacent vs. root-level) beyond what is stated here, since no tracked
source cross-tabulates whole-disc `.txt` files by HOG membership.

## 3. Confirmed facts

| # | Fact | Tracked source |
|---|---|---|
| C1 | `.txt` is not a key in `FORMAT_HANDLERS` in the asset fingerprinter — the handled suffixes are exactly `tdx, ska, skas, skm, skl, vag, lpd, par, col, vum, vpk`. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` dict) |
| C2 | Because `.txt` has no entry in `FORMAT_HANDLERS`, `scan_asset()` records only the aggregate extension count for `.txt` spans and applies no format-specific structural parse to them. | `tools/fingerprint_assets.py` (`scan_asset` handler-dispatch logic) |
| C3 | `analysis/formats/asset-fingerprints.json`'s `formats` object (the per-suffix aggregate-detail section) contains exactly the keys `col, lpd, par, ska, skas, skl, skm, tdx, vag, vpk, vum` — `.txt` has no per-suffix aggregate-detail entry at all, only the raw scan-count. | `analysis/formats/asset-fingerprints.json` (`formats` object keys) |
| C4 | `analysis/formats/hog-validation.json`'s `entry_extensions` object (the top-level-HOG member census) has no `.txt` key, i.e. the observed top-level-HOG member count for `.txt` is exactly 0. | `analysis/formats/hog-validation.json` (`entry_extensions` object keys) |
| C5 | `analysis/formats/FRONTEND-TOPOLOGY.md` explicitly names `.txt` as one of the thirteen "approved public suffixes" tracked by `tools/measure_frontend_hog_topology.py`, and that tool's source maps `.txt` to the fixed category label `"text"` in its `APPROVED_EXTENSION_CATEGORIES` dictionary. This is a declared measurement-vocabulary fact only — no result JSON from a run of that tool is checked into the repository, so no actual `.txt` topology count/bucket value is available from it. | `analysis/formats/FRONTEND-TOPOLOGY.md` (approved-suffix list); `tools/measure_frontend_hog_topology.py` (`APPROVED_EXTENSION_CATEGORIES[".txt"] == "text"`) |
| C6 | No published grammar document among `analysis/formats/*.md` (`ASSET-RECON.md`, `COL.md`, `HOG.md`, `LPD.md`, `PAR.md`, `POP.md`, `SO.md`, `TDX.md`, `VAG.md`, `VPK.md`, `VUM.md`) other than `FRONTEND-TOPOLOGY.md`'s vocabulary listing mentions the `.txt` suffix. | `analysis/formats/*.md` (absence confirmed by direct search) |
| C7 | `analysis/evidence/ledger.jsonl` contains no entry (confirmed or rejected) making a structural or semantic claim about the `.txt` game-asset suffix. Every literal `.txt` substring match in the ledger (34 lines) resolves to either the build file `CMakeLists.txt` or the license file `LICENSES/SDL3.txt` (ledger entry E-0079), neither of which is a claim about the `.txt` game-asset family. | `analysis/evidence/ledger.jsonl` (direct search of every line containing `.txt`) |
| C8 | No header or source file under `native/include/omega/` or `native/src/`, and no rule in `CMakeLists.txt`, defines a decoder/descriptor or registers a build/test target for the literal `.txt` suffix. The only tracked source files with "text" in their name (`par_text_envelope_decoder.*`, `skas_text_envelope_decoder.*`, and their test files) decode the `.par` and `.skas` suffix families, not files suffixed `.txt`. | `native/include/omega/**/*.h`, `native/src/**/*.cpp`, `CMakeLists.txt` (direct search) |

## 4. Aggregate-only facts

- **Recursive-in-HOG scan count:** 3 asset spans with extension `.txt` were observed across the fingerprinter's full recursive HOG walk (`analysis/formats/asset-fingerprints.json`, `scan.extensions[".txt"]`).
- **Top-level-HOG member count:** 0 depth-0 HOG-member entries carry the `.txt` suffix (`analysis/formats/hog-validation.json`, `entry_extensions`).
- **Whole-disc count:** 8 files on the disc image (HOG-internal and non-HOG combined) carry the `.txt` suffix, per `analysis/manifests/disc-summary.json` (`extensions[".txt"]`), out of 448 total whole-disc files.
- **Whole-disc size range (aggregate, computed from the tracked per-entry size field in `analysis/manifests/disc-files.jsonl`, no paths/hashes/names disclosed):** across the 8 whole-disc `.txt` entries, sizes are 44, 44, 130, 176, 780, 974, 3,528, and 5,755 bytes — i.e. a range of 44 to 5,755 bytes, with a visible cluster of very small entries (44–180 bytes, 4 of 8) and a smaller cluster of larger entries (780–5,755 bytes, 4 of 8). This is a raw byte-size aggregate over the whole-disc population (which includes the non-game-asset license/build file noted in Section 2), not a size aggregate isolated to the 3 in-HOG spans specifically — no tracked source publishes a size aggregate scoped to only the in-HOG `.txt` population.
- The scanner's `formats` per-suffix aggregate-detail section — which for handled suffixes carries fields such as size ranges, bucket histograms, and alignment/padding observations — has no such section for `.txt`. There is therefore no tracked size-range, bucket, or alignment aggregate for the **in-HOG** `.txt` population specifically.
- `analysis/formats/FRONTEND-TOPOLOGY.md` states that its measurement tool tracks `.txt` under the category `"text"` alongside fixed member-size buckets (seven fixed buckets, overall and per category) and same-basename sibling-pair counts against the other twelve approved suffixes — but because no result JSON from an actual run of `tools/measure_frontend_hog_topology.py` is checked into the tracked tree, none of those bucket/pair values are available as an aggregate fact for `.txt` here; only the tool's frozen vocabulary (that `.txt` is tracked and categorized as `"text"`) is confirmed.

## 5. Hypotheses (explicitly labeled — none confirmed)

- **H1 — Front-end/UI text-asset hypothesis.** Given that the front-end HOG-topology tool (`tools/measure_frontend_hog_topology.py`) explicitly includes `.txt` in its "approved public suffixes" list — a list otherwise composed of established engine-asset suffixes (`.col`, `.hog`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.vag`, `.vum`) — the in-HOG `.txt` population (3 spans) may be front-end/menu-adjacent plain-text data (e.g. string tables, localization, or configuration-like text) rather than an unrelated generic bucket. *Confirming/refuting observation (privacy-safe):* run `tools/measure_frontend_hog_topology.py` against the owned corpus and check whether the 3 in-HOG `.txt` spans co-occur (same-basename sibling pairs) with any of the other twelve approved suffixes, and whether they cluster in the front-end-relevant archive depths/size buckets the tool already reports in aggregate — with no path, basename, or payload disclosed.
- **H2 — Heterogeneous/incidental-text hypothesis (null hypothesis).** The 3 in-HOG `.txt` spans may simply be incidental development or debug text files packed into HOGs with no shared grammar, given the small population size (3) and the total absence of any structural handler or grammar doc. *Confirming/refuting observation:* add a passive, aggregate-only header/size/character-class profiler for `.txt` spans (first-N-bytes signature histogram, printable-ASCII-vs-binary classification, size distribution) to `tools/fingerprint_assets.py`, scoped to the 3 in-HOG spans only; a lack of any shared signature or size quantization across only 3 spans would support H2 over H1, though the small sample size limits how strongly either hypothesis can be confirmed.
- **H3 — Non-asset build/license-artifact hypothesis (for the whole-disc superset only).** Some of the 5 whole-disc `.txt` files that are not in-HOG members may be non-game-asset artifacts (the ledger already attests one such case, a distributed third-party license text at E-0079, albeit for a `LICENSES/SDL3.txt` file that is itself a repository/build artifact rather than a disc-image file — the ledger entry does not establish that any of the 8 whole-disc `.txt` *disc* entries are license files). *Confirming/refuting observation:* cross-tabulate the whole-disc `.txt` entries against HOG membership using only `analysis/manifests/disc-files.jsonl` and `analysis/formats/hog-validation.json`/`asset-fingerprints.json` (all already tracked, aggregate-safe if reported as counts) to report how many of the 8 whole-disc `.txt` files are inside a tracked HOG vs. outside all HOGs — this directly reconciles the 3-vs-8 gap already visible in Section 2 into a stated, cited split.

## 6. Missing observations

- **No per-suffix structural aggregate for `.txt`.** Unlike the eleven handled suffixes, `analysis/formats/asset-fingerprints.json`'s `formats` object carries no size-range, bucket-count, or alignment aggregate for `.txt`. *Privacy-safe collection:* add a `fingerprint_txt` aggregate-only handler to `tools/fingerprint_assets.py` (mirroring the existing per-suffix handlers) that records size distribution, first-bytes signature histogram, and a coarse printable-ASCII-vs-binary classification across all owned-corpus in-HOG `.txt` spans, with no payload or per-file export — then re-run the existing fingerprinting pass.
- **No executed front-end-topology measurement.** `tools/measure_frontend_hog_topology.py` already declares `.txt` as an approved, categorized suffix, but no result JSON from an actual run against the owned corpus is checked into the tracked tree. *Privacy-safe collection:* run the tool per its own documented aggregate-only contract (`analysis/formats/FRONTEND-TOPOLOGY.md`) and check the resulting schema-version-1 JSON into `analysis/` so the `.txt` category counts, size buckets, and same-basename sibling-pair counts become citable aggregate facts rather than an unexercised vocabulary declaration.
- **No ledger entry.** `analysis/evidence/ledger.jsonl` has never had a claim proposed or checked for the `.txt` game-asset suffix (the only `.txt` substring hits are `CMakeLists.txt` and the unrelated `LICENSES/SDL3.txt` license reference). *Privacy-safe collection:* once an aggregate-only handler and/or the front-end-topology run above exist and have produced output, file a ledger claim describing only the mechanical aggregate facts (counts, size bands, signature-cluster counts) — explicitly not any inferred semantic role — and mark it `confirmed` only for the mechanical aggregate.
- **No native decoder, descriptor, or CMake/test registration for the `.txt` suffix.** There is no header, source file, or build/test target anywhere in `native/` or `CMakeLists.txt` that treats the literal `.txt` suffix as a decodable format (the existing `par_text_envelope_decoder` / `skas_text_envelope_decoder` pair decodes `.par`/`.skas` content, not `.txt`-suffixed files). *Privacy-safe collection:* this is a straightforward absence; any first decoder attempt must begin from the aggregate-only header/size profiling above before any structural or semantic claim is attempted.
- **No whole-disc-vs-in-HOG cross-tabulation.** No tracked source states how many of the 8 whole-disc `.txt` files are inside vs. outside a tracked HOG, leaving the 3-vs-8 gap unresolved beyond the compatibility note in Section 2. *Privacy-safe collection:* produce and record the single aggregate split described in Hypothesis H3 above.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`.**

- The only tracked-code touchpoint for `.txt` in the asset fingerprinter is the generic scan-count path in `tools/fingerprint_assets.py`'s `scan_asset()` function, which increments `scan.extensions[".txt"]` for every span whose suffix is `.txt` (case-insensitively) — the same generic counting path every unhandled suffix goes through. `.txt` is conspicuously absent from `FORMAT_HANDLERS`, which is the dict that gates all suffix-specific structural handlers (`tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`).
- A second tracked tool, `tools/measure_frontend_hog_topology.py`, declares `.txt` as one of its thirteen approved, categorized suffixes (category `"text"`) — but this is a container-topology measurement over member *suffix and size*, explicitly documented as never inspecting or interpreting member content, and no result JSON from a run of it is checked into the repository. This does not raise the classification above `aggregate_scanner_only`: it is a second aggregate-counting touchpoint, not a structural or content-level decoder.
- There is **no native decoder**: no header in `native/include/omega/retail/` or `native/include/omega/asset/`, no source file in `native/src/**`, and no `CMakeLists.txt` target or test registration references the `.txt` suffix or a `.txt`-flavored type name. The tracked `par_text_envelope_decoder` and `skas_text_envelope_decoder` sources/tests decode the `.par` and `.skas` suffix families respectively (files whose payload happens to be text-structured) and must not be conflated with a `.txt`-suffix decoder.
- There is **no descriptor**, passive or otherwise, for the literal `.txt` suffix in any tracked source.
- **Adversarial/resource-boundary test gap:** since no decoder or descriptor exists for `.txt`, there is by definition no adversarial or resource-boundary test coverage for it anywhere in the tracked test tree. This is a total gap, not a partial one.

## 8. Codex work order (ranked, privacy-safe)

1. **Highest priority — build an aggregate-only `.txt` structural profiler.** Add a `fingerprint_txt` handler to `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` that, for every in-HOG `.txt` span in the owned corpus, records (aggregate-only, no payload/path export): total size histogram (the population is only 3 spans, so exact per-span sizes as an unordered multiset are acceptable aggregate detail), a printable-ASCII-vs-binary classification, and a first-N-bytes signature histogram (counted, not stored per-file) — mirroring the style already used for `col`/`vum`/etc. Re-run the fingerprinter and refresh `analysis/formats/asset-fingerprints.json`.
2. **Run the existing front-end-topology tool and check in its output.** `tools/measure_frontend_hog_topology.py` already treats `.txt` as an approved, categorized suffix; running it against the owned corpus per its documented aggregate-only contract and checking in the resulting schema-version-1 JSON would upgrade Section 4's currently-unexercised vocabulary fact into real category/bucket/sibling-pair aggregates for `.txt` at essentially zero new engineering cost.
3. **Cross-tabulate whole-disc vs. in-HOG membership.** Using only `analysis/manifests/disc-files.jsonl` and `analysis/formats/hog-validation.json`/`asset-fingerprints.json` (all already tracked, aggregate-safe if reported as counts), produce and record a single aggregate number: how many of the 8 whole-disc `.txt` files fall inside a tracked HOG vs. outside all HOGs. This resolves Hypothesis H3 without exposing any path or name.
4. **If steps 1–2 show a shared signature/size quantization across the 3 in-HOG spans:** propose a ledger claim (next E-#### under `analysis/evidence/ledger.jsonl`) stating only the mechanical aggregate fact (e.g. "N of 3 in-HOG `.txt` spans are printable ASCII; size range X–Y; front-end-topology category `text` bucket Z") — no semantic role, no field names, no invented purpose.
5. **If and only if step 4 produces a Confirmed structural grammar constant:** author `analysis/formats/TXT.md` following the house style of the existing per-format docs (`TDX.md`, `COL.md`, `PAR.md`), scoped strictly to what the aggregate evidence supports; keep any remaining ambiguity explicitly labeled Hypothesis or UNKNOWN.
6. **Do not** attempt a native decoder or CMake/test registration before steps 1–5 land, and do not conflate the existing `par_text_envelope_decoder`/`skas_text_envelope_decoder` sources with `.txt`-suffix decoding — writing a `.txt` decoder ahead of structural evidence, or miscrediting the `.par`/`.skas` decoders as covering `.txt`, would each independently be the "plausible invented decoder" regression this dossier is required to avoid.
