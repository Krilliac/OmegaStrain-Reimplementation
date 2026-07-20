# .gui — Format Dossier

## 1. Identity

`.gui` is a HOG-archive member suffix observed by the project's tracked
recursive-scan and top-level-validation tooling. At the evidence level this
dossier can defend, `.gui` is **an observed, frequently-occurring container
member suffix with no published grammar, no structural decoder, and no
confirmed internal layout**. It is one of a small named set of "front-end"
suffixes (`.gui`, `.fnt`, `.ie`) that a dedicated tracked collector script
singles out for *future* aggregate size measurement, precisely because no
size/byte/structure evidence has been committed for them yet. No semantic
role (menu definition, widget layout, dialog table, or otherwise) is
established by any tracked source — any such reading would be an invented
semantic and is explicitly out of scope.

## 2. Occurrence evidence

| Count | Value | Tracked source |
|---|---|---|
| Recursive-in-HOG suffix count | 77 | `analysis/formats/asset-fingerprints.json`, `scan.extensions[".gui"]` |
| Top-level-HOG member-suffix count | 21 | `analysis/formats/hog-validation.json`, `entry_extensions[".gui"]` |
| Whole-disc occurrence count | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.gui`-suffixed path in the whole-disc file histogram) |

The recursive count (77) is taken over the full recursive HOG containment
tree (top-level HOGs plus nested HOGs discovered inside them); the
top-level count (21) is taken only over entries listed directly in each
top-level HOG's own entry table. The two counts are not directly comparable
row-for-row; both are aggregate totals only, with no per-archive or
per-member breakdown reproduced here.

Because the whole-disc count is 0, `.gui` never appears as a bare file on
the disc image outside of HOG containment — it is exclusively an in-archive
member format, which is why this dossier treats it as an in-scope,
full-dossier game-asset format per the task's occurrence gate.

(Note: a top-level PS2 directory named `NETGUI` appears in
`analysis/manifests/disc-summary.json` / `disc-files.jsonl`. That is a
directory name, not a `.gui`-suffixed file, and its contents (`.RGB`,
`.BIN`, `.IRX`, `.ELF`, `.TM2`, `.PF` members of the PS2 network boot
stack) are unrelated to the `.gui` suffix family; it is not evidence about
`.gui` and is not used anywhere below.)

## 3. Confirmed facts

Each row below is mechanically citable from a named tracked file.

| # | Fact | Tracked source |
|---|---|---|
| C1 | The suffix `.gui` is recognized and counted by the recursive asset-fingerprint scanner as a plain extension bucket (no structural handler attached). | `analysis/formats/asset-fingerprints.json` (`scan.extensions`); `tools/fingerprint_assets.py` (FORMAT_HANDLERS registers structural handlers only for `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk` — `.gui` is absent from that registration) |
| C2 | The suffix `.gui` is recognized and counted by the top-level HOG-validation script as a top-level member-suffix bucket. | `analysis/formats/hog-validation.json` (`entry_extensions`) |
| C3 | `.gui` is one of exactly three suffixes (`.fnt`, `.gui`, `.ie`) explicitly named as the frozen "front-end" vocabulary of a dedicated, not-yet-run aggregate size-fingerprint collector. The tool's own docstring states it exists "to produce the member-size structure that the front-end evidence audit records as missing for `.gui`/`.fnt`/`.ie`" and that only a run against the owner corpus (a separate, private step outside this dossier) would yield real evidence. | `tools/measure_member_structural_fingerprint.py` (module docstring; `FROZEN_SUFFIXES = (".fnt", ".gui", ".ie")`) |
| C4 | `.gui` is absent from the "approved public suffixes" list tracked by the front-end HOG-topology measurement tool/doc (`.col`, `.hog`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, `.vum`); unapproved suffixes including `.gui` fall into that tool's undifferentiated `other` bucket. | `analysis/formats/FRONTEND-TOPOLOGY.md` (§ Aggregate contract); `tools/measure_frontend_hog_topology.py` |
| C5 | No native decoder, descriptor, header, or CMake target exists anywhere in the tracked native source tree that names or handles `.gui`, GUI-format parsing, or a "Gui"-typed asset. A repo-wide search of `native/include`, `native/src`, and `CMakeLists.txt` for GUI-related identifiers returns no `.gui`-format hit (the sole unrelated match, `run_capture_replay.h`, concerns input-capture replay, not the asset format). | `native/include/`, `native/src/`, `CMakeLists.txt` (absence confirmed by search) |
| C6 | No entry in the evidence ledger (`E-0001`…) makes any confirmed or rejected claim about `.gui` specifically; a literal search for `.gui` in the ledger returns no match. | `analysis/evidence/ledger.jsonl` |

## 4. Aggregate-only facts

These are tracked aggregate counts with no semantic interpretation attached.

- Recursive-in-HOG member count for `.gui` across the whole recursively-scanned HOG containment tree: **77** (`analysis/formats/asset-fingerprints.json`, `scan.extensions[".gui"]`). This sits in the same aggregate table as `.fnt` (3), `.ie` (79), `.skas` (2), `.skel` (4), `.skf` (26), and other low/mid-frequency suffixes — i.e. `.gui` is a low-frequency suffix relative to high-volume asset families such as `.tdx` (15248), `.skm` (4219), or `.col` (7036) in that same table.
- Top-level-HOG member count for `.gui` across all validated top-level archives: **21** (`analysis/formats/hog-validation.json`, `entry_extensions[".gui"]`), again alongside `.fnt` (3) in the same top-level table.
- No size range, minimum/maximum payload size, distinct-size count, or size-GCD is present in any tracked JSON for `.gui`. The one tracked tool designed to produce that aggregate (`tools/measure_member_structural_fingerprint.py`) defines the schema fields (`count`, `size_min`, `size_max`, `size_distinct`, `size_gcd`) but no corresponding output document for `.gui` exists in the tracked tree — this is an aggregate schema, not aggregate data, for this family.
- No alignment, bucket-membership (the seven fixed member-size buckets defined by `FRONTEND-TOPOLOGY.md`), category tag, or sibling-basename pairing count is available for `.gui`, because that tool's approved-category vocabulary excludes `.gui` (see C4) and routes it into the undifferentiated `other` bucket, which the tool's own contract states carries no per-suffix breakdown.

## 5. Hypotheses

Explicitly labeled; none of these are asserted as fact anywhere above.

- **H1 — Front-end/UI association.** The suffix token "gui" and its grouping alongside `.fnt` (font) and `.ie` in the frozen front-end vocabulary of `tools/measure_member_structural_fingerprint.py` suggest a front-end/menu-adjacent role. *Confirming observation:* a privacy-safe run of `tools/measure_member_structural_fingerprint.py` against the owner corpus, publishing only the aggregate size fingerprint (count/min/max/distinct/gcd) for `.gui`, cross-referenced against known front-end HOG container names already documented in tracked *.md files — a tight, small, uniform size cluster co-located with `.fnt`/`.ie` members would support the grouping; a wide, `.tdx`/`.skm`-like size spread would refute it. *Refuting observation:* the same run showing `.gui` sizes/patterns statistically indistinguishable from an unrelated bulk-data family.
- **H2 — Fixed or near-fixed record structure.** Because `.gui` is a discrete, low-count suffix (77 recursive / 21 top-level) rather than a high-volume streaming-asset family, it may carry a small, fixed-size or low-variance record structure (as opposed to a variable-length blob format). *Confirming observation:* the `size_distinct` and `size_gcd` fields from a run of `measure_member_structural_fingerprint.py` — a low `size_distinct` count and a nontrivial `size_gcd` would support a fixed/near-fixed record; a high `size_distinct` with `size_gcd` of 1 would refute it.
- **H3 — Common header prefix analogous to other formats.** Several already-documented formats in this repo (e.g. per `analysis/formats/ASSET-RECON.md`) share a common leading multi-word little-endian prefix convention. `.gui` may or may not follow an analogous convention. *Confirming observation:* a privacy-safe, path-free structural probe (built the same way as the existing FORMAT_HANDLERS in `tools/fingerprint_assets.py`) that reports only aggregate first-bytes statistics (e.g., how many `.gui` members share a common leading N-word pattern, with no payload bytes or per-file rows reproduced) run against the owner corpus. *Refuting observation:* the same aggregate probe showing no dominant leading pattern across the corpus.

## 6. Missing observations

Tracked evidence that does not exist, and the privacy-safe collection that would produce it:

- **No committed size-fingerprint document for `.gui`.** `tools/measure_member_structural_fingerprint.py` defines the exact schema (count, size_min, size_max, size_distinct, size_gcd) but has apparently not been run against the owner corpus, or its output was not committed. *Collection:* run it against a private, gitignored input directory and commit only its JSON aggregate document under `analysis/output/` (the tool itself guarantees no path, member name, hash, offset, payload byte, or per-file row is emitted).
- **No structural/header probe for `.gui`.** Unlike `tdx`/`ska`/`skas`/`skm`/`skl`/`vag`/`lpd`/`par`/`col`/`vum`/`vpk`, there is no `FORMAT_HANDLERS` entry in `tools/fingerprint_assets.py` for `.gui`, so no header-magic, field-layout, or structural-validity aggregate exists. *Collection:* author a new bounded, path-free structural handler (mirroring the existing ones) that reports only aggregate header/field statistics for `.gui`, run it against the owner corpus, and commit only the aggregate output.
- **No category/topology placement for `.gui`.** `tools/measure_frontend_hog_topology.py` / `FRONTEND-TOPOLOGY.md` explicitly excludes `.gui` from its approved-suffix vocabulary, so no depth, nesting, size-bucket, or sibling-basename-pairing data exists for it. *Collection:* if warranted after H1/H2 evidence, extend the approved-suffix vocabulary (a deliberate, documented schema change per the tool's own comment) and re-run against the owner corpus.
- **No ledger entry.** No `E-####` entry in `analysis/evidence/ledger.jsonl` makes any confirmed or rejected claim naming `.gui`. *Collection:* once any of the above aggregate collectors produce owner-corpus results, record a new ledger entry citing the exact tracked tool/output pair, following the ledger's existing evidence-citation convention.
- **No native decoder/descriptor and no test coverage.** There is no header, source file, or CMake test target anywhere in `native/` for `.gui`. *Collection:* out of scope until an aggregate size/structure signal (from the missing observations above) exists to justify one — building a decoder ahead of that evidence would be the exact "plausible invented decoder" regression this dossier is required to avoid.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- `.gui` is counted by two tracked aggregate scanners: the recursive asset-fingerprint scan (`tools/fingerprint_assets.py`, via `analysis/formats/asset-fingerprints.json`) and the top-level HOG-validation scan (`analysis/formats/hog-validation.json`). Neither attaches a structural handler to `.gui` — `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` registers structural decoders only for `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`, and `.gui` is not among them.
- A second, dedicated aggregate collector (`tools/measure_member_structural_fingerprint.py`) is registered in tracked source specifically for `.gui` (alongside `.fnt`/`.ie`) but its schema fields are unpopulated in the tracked tree (see § 6) — the tool exists and is presumably buildable/testable, but no owner-corpus run's output is committed.
- No `native/include/omega/**` header, no `native/src/**` source file, and no CMake target in `CMakeLists.txt` reference `.gui`, a GUI asset type, or GUI parsing (confirmed by repo-wide search — see C5). There is therefore no `canonical_decoder`, no `structural_envelope_only` handler, and no `passive_descriptor_only` type for `.gui` — only the two generic, suffix-agnostic aggregate scanners above see it at all.
- No adversarial/resource-boundary test exists for `.gui` specifically, because no decoder/descriptor exists to test. `tools/measure_member_structural_fingerprint.py` reuses the bounded, resource-limited, reparse/symlink/case-fold-safe HOG traversal core shared with `measure_frontend_hog_topology.py` (per its own docstring), so *if and when* it is run, the traversal safety properties are inherited — but that has not been exercised against `.gui` payload content specifically, only against the archive-container layer.

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. No menu-role or other semantic speculation is included.

1. **Highest priority — run the existing size-fingerprint collector.** Execute `tools/measure_member_structural_fingerprint.py` against a private, gitignored owner-corpus directory and commit only its resulting JSON aggregate (count/size_min/size_max/size_distinct/size_gcd for `.gui`, `.fnt`, `.ie`) under `analysis/output/`. This is a tool that already exists in tracked source, is schema-frozen and path-free by construction, and directly resolves the § 6 "no committed size-fingerprint" gap and the H1/H2 hypotheses with zero new code.
2. **Author a bounded structural/header probe for `.gui`.** Following the pattern of the existing `FORMAT_HANDLERS` entries in `tools/fingerprint_assets.py`, write a new aggregate-only handler that reports header-magic/common-prefix statistics (count of members sharing a leading N-byte/word pattern, distinct-pattern counts) with no payload bytes, no per-file rows, and no path/member-name output — mirroring the existing handlers' contracts. Run it against the owner corpus; commit only the aggregate.
3. **Record a ledger entry** citing whichever of steps 1–2 actually runs, with the exact tracked tool path(s) and check command, following the existing `E-####` citation convention in `analysis/evidence/ledger.jsonl` — this is currently the missing link between "tool exists" and "evidence confirmed."
4. **Only if steps 1–2 produce a distinctive, non-generic signal** (e.g., a small `size_distinct`, a nontrivial `size_gcd`, or a dominant header pattern), consider extending `tools/measure_frontend_hog_topology.py`'s approved-suffix vocabulary to include `.gui` (a deliberate, documented schema change per that tool's own comment) so depth/bucket/sibling-pairing aggregates become available for it too.
5. **Do not build a native decoder, descriptor, or CMake target for `.gui` until steps 1–3 produce owner-corpus aggregate evidence.** Per the clean-room rules governing this dossier, a plausible invented decoder ahead of that evidence is a regression, not progress.
