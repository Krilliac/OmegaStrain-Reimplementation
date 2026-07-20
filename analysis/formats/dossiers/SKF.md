# `.skf` — Format Dossier

## 1. Identity

`.skf` is a HOG-archive member suffix tracked only as a generic,
undifferentiated extension bucket by two aggregate-counting tools. It has
**no published grammar, no structural decoder, no native/CMake presence, no
ledger entry, and no dedicated aggregate-fingerprint collector schema** (unlike
`.ie`/`.gui`/`.fnt`, which at least have a frozen-suffix collector defined for
future measurement, `.skf` has none). No semantic meaning (skeleton-adjacent
data implied by the `sk`-prefix mnemonic it happens to share with `.ska`/
`.skas`/`.skl`/`.skm`, or anything else) can be asserted from tracked
evidence — the family is UNKNOWN at the semantic level.

## 2. Occurrence evidence

| Metric | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG occurrences (whole containment tree, all nesting depths) | 26 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".skf"]`) |
| Top-level-HOG occurrences (top-level archives only) | 26 | `analysis/formats/hog-validation.json` (`entry_extensions[".skf"]`) |
| Whole-disc occurrences (outside any HOG) | 0 | `analysis/manifests/disc-summary.json` (extension histogram; `.skf` absent) |

The recursive count (26) exactly equals the top-level count (26) — a zero gap.
This is stated here only as an arithmetic fact about the two tracked counters;
no inference about nesting depth, archive role, or load order is drawn beyond
that fact (see §5 H1 for the explicitly-labeled hypothesis this observation
could feed).

## 3. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Tracked source citation |
|---|---|---|
| C1 | The suffix `.skf` is recognized and counted by the recursive asset-fingerprint scanner as a plain extension bucket (no structural handler attached). | `analysis/formats/asset-fingerprints.json` (`scan.extensions`); `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` registers structural handlers only for `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk` — `.skf` is absent from that registration) |
| C2 | The suffix `.skf` is recognized and counted by the top-level HOG-validation script as a top-level member-suffix bucket. | `analysis/formats/hog-validation.json` (`entry_extensions`) |
| C3 | `tools/fingerprint_assets.py` builds its recursive-scan extension set from `target_extensions = set(FORMAT_HANDLERS)` for the disc-root direct-map pass, and `.skf` is not in that set — i.e. the tool's own structural-handler registry mechanically excludes `.skf` from any payload interpretation. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS`, `target_extensions`) |
| C4 | `.skf` is absent from the "approved public suffixes" category map tracked by the front-end HOG-topology measurement tool (`APPROVED_EXTENSION_CATEGORIES` lists `.col`, `.hog`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, `.vum` only); unapproved suffixes including `.skf` fall into that tool's undifferentiated `other` category. | `tools/measure_frontend_hog_topology.py` (`APPROVED_EXTENSION_CATEGORIES`, lines ~30-45) |
| C5 | `.skf` is absent from the `FROZEN_SUFFIXES` constant of the dedicated member-structural-fingerprint collector, which currently names only `.fnt`, `.gui`, `.ie` as suffixes with a defined-but-not-yet-run size/byte aggregate schema — no such schema is defined for `.skf` at all, tracked or otherwise. | `tools/measure_member_structural_fingerprint.py` (`FROZEN_SUFFIXES = (".fnt", ".gui", ".ie")`, line 75) |
| C6 | No `native/include/omega/**` header, no `native/src/**` source file, and no CMake target in `CMakeLists.txt` references `.skf`, an "SKF" asset type, or any decoder/descriptor named for this suffix (confirmed by repo-wide search for `skf`/`SKF` across `native/` and `CMakeLists.txt`, which returned zero matches). | `native/include/omega/**`, `native/src/**`, `CMakeLists.txt` (absence confirmed by search) |
| C7 | No entry in the evidence ledger references `.skf` (confirmed by grep for `skf`/`SKF` across the ledger, which returned zero matches) — no claim about this suffix has yet passed the ledger's confirmed/rejected process. | `analysis/evidence/ledger.jsonl` (absence confirmed by search) |
| C8 | No published grammar document (`analysis/formats/*.md`) exists for `.skf` anywhere in the tracked tree (confirmed by search across all `analysis/formats/*.md` files, which returned zero matches for `skf`/`SKF`, including in the sibling dossiers `IE.md`, `HOG.md`, `SKAS.md`, and the topology docs `ASSET-RECON.md`/`FRONTEND-TOPOLOGY.md`). | `analysis/formats/*.md` (absence confirmed by search) |
| C9 | The recursive-scan tally (`scan.extensions[".skf"] = 26`) and the top-level HOG-validation tally (`entry_extensions[".skf"] = 26`) are numerically identical, both mechanically read from their respective tracked JSON files. | `analysis/formats/asset-fingerprints.json` (`scan.extensions`); `analysis/formats/hog-validation.json` (`entry_extensions`) |

## 4. Aggregate-only facts

Tracked aggregate counts with no semantic interpretation attached.

- Recursive-in-HOG member count for `.skf` across the whole recursively-scanned
  HOG containment tree: **26** (`analysis/formats/asset-fingerprints.json`,
  `scan.extensions[".skf"]`). In that same table it sits among comparably
  low-frequency suffixes — `.fnt` (3), `.gui` (77), `.ie` (79), `.skas` (2),
  `.skel` (4) — versus high-volume families such as `.tdx` (15248), `.vag`
  (8665), `.col`/`.vum` (7036 each), `.skm` (4219).
- Top-level-HOG member count for `.skf` across all validated top-level
  archives: **26** (`analysis/formats/hog-validation.json`,
  `entry_extensions[".skf"]`) — identical to the recursive figure above.
- The recursive scan that produced the 26 figure covered 53,281 total asset
  spans across nesting depths `-1` (5 spans), `0` (32,351 spans), `1` (20,925
  spans) — this depth histogram is global across all extensions, not broken
  out per suffix, so it cannot be attributed to `.skf` specifically beyond
  "some subset of these depths." (`analysis/formats/asset-fingerprints.json`,
  `scan.asset_spans_scanned`, `scan.depth`)
- The top-level HOG-validation dataset separately reports `archive_count`,
  `total_entries`, `valid_count`, and `error_count` at the whole-corpus level
  (not broken out per suffix), and a `zero`-length `errors` array; none of
  these figures can be attributed to `.skf` members specifically beyond "some
  subset of the validated top-level entries." (`analysis/formats/hog-validation.json`)
- No size range, minimum/maximum payload size, distinct-size count, or
  size-GCD is present in any tracked JSON for `.skf`. No tracked tool defines
  even a not-yet-run schema for this figure (contrast with `.ie`/`.gui`/`.fnt`,
  which at least have a defined-but-unrun schema in
  `tools/measure_member_structural_fingerprint.py`) — for `.skf` there is no
  aggregate schema and no aggregate data.
- The dataset's declared scope is "aggregate structural fingerprints only; no
  proprietary payloads exported" (schema_version 3) — i.e. the entire `.skf`
  count of 26 is itself an aggregate figure, not a per-file listing, consistent
  with clean-room constraints. (`analysis/formats/asset-fingerprints.json`,
  `scope`, `schema_version`)

## 5. Hypotheses

Explicitly labeled; none of these are asserted as fact anywhere above.

- **H1 — "All `.skf` members live at the top level of the containment tree
  (no nesting)."** The recursive count (26) and top-level count (26) are
  numerically equal (§2, §3 C9), which is consistent with — but does not by
  itself mechanically prove for every individual member — zero nested-HOG
  occurrence, since the two counters could in principle be produced by
  different, non-overlapping sets of members that happen to sum/tally to the
  same total. *Privacy-safe observation that would confirm or refute*: extend
  a tracked, path-free tool to report, per suffix, a depth histogram (analogous
  to the corpus-wide `scan.depth` histogram already present) so that `.skf`'s
  own depth distribution — not just its raw count — is directly comparable
  across the two datasets without revealing any private filename.
- **H2 — "Naming-mnemonic kinship with `.ska`/`.skas`/`.skl`/`.skm`
  (skeleton/animation family)."** `.skf` shares the `sk`-prefix with four
  suffixes that do have structural handlers and published grammar docs
  (`.ska`, `.skas`, `.skl`, `.skm`), which is a naming-pattern observation
  worth flagging — but no tracked tool, grammar doc, or ledger entry groups
  `.skf` with that family, and the mnemonic alone establishes nothing about
  content. *Privacy-safe observation that would confirm or refute*: an
  aggregate size-structure comparison (count/min/max/distinct-size/size-GCD)
  for `.skf` run by a new or extended tracked collector, compared against the
  already-published aggregate figures for `.ska`/`.skas`/`.skl`/`.skm` in
  their respective `analysis/formats/*.md` docs, would be an aggregate-only
  signal for or against this naming-driven hypothesis — never proof of shared
  semantics.
- **H3 — "Low-cardinality, one-per-context file population."** The count of
  26 is small relative to per-asset families (`.tdx`, `.skm`, `.vag`, `.col`,
  `.vum`), consistent with — but not established by — a low-cardinality
  population (e.g. one-per-level, one-per-character, or one-per-archive
  rather than one-per-mesh). *Privacy-safe observation that would confirm or
  refute*: an aggregate co-occurrence count (e.g. how many `.skf` members
  share a normalized basename/stem with a `.hog` container at the same
  nesting depth, analogous to the "same-basename groups/pairs" aggregate
  already defined in `tools/measure_frontend_hog_topology.py`) extended to
  include `.skf` in its counted-pair vocabulary — reporting only counts, never
  filenames.

## 6. Missing observations

Tracked evidence that does not exist, and the privacy-safe collection that
would produce it:

- **Payload size structure.** No `count`/min/max/distinct-size/size-GCD
  aggregate exists for `.skf`, and unlike `.ie`/`.gui`/`.fnt` there is not even
  a defined-but-unrun schema for it. Collection: extend
  `tools/measure_member_structural_fingerprint.py`'s `FROZEN_SUFFIXES` tuple to
  include `.skf` (or add a sibling tool following the same aggregate-only,
  path-free contract), run it against the owner corpus, and commit the JSON
  output under `analysis/formats/`.
- **Byte-level structural signature.** No magic-number, header-constant, or
  compression-magic check exists for `.skf` specifically (the compression-magic
  scan in `asset-fingerprints.json` — `standard_compression_magic_hits: 0` /
  `standard_compression_spans_checked: 46604` — is global across all scanned
  spans, not specific to `.skf`). Collection: extend the frozen-suffix
  collector (once `.skf` is added per the item above) to report a magic-byte
  histogram scoped to `.skf` spans only.
- **Per-suffix depth histogram.** No tracked tool currently reports the
  nesting-depth distribution broken out per suffix (only a corpus-wide
  `scan.depth` histogram exists). Collection: extend
  `tools/fingerprint_assets.py`'s scan aggregation to count depth per
  extension, which would directly test H1 (§5) without any path-level data.
- **Ledger entry.** No `E-####` ledger row exists for `.skf`. Collection: once
  an owner-corpus aggregate run for `.skf` completes, record a ledger entry
  citing the exact tracked tool path and command used, following the existing
  `E-####` citation convention in `analysis/evidence/ledger.jsonl`.
- **Naming-family size comparison.** No tracked aggregate compares `.skf`'s
  size structure against the `sk`-prefixed family (`.ska`, `.skas`, `.skl`,
  `.skm`) that does have published grammar and structural handlers.
  Collection: once the payload-size collection above exists for `.skf`, a
  small tracked aggregate table (counts and size-structure side by side)
  comparing it to the published figures in `analysis/formats/SKAS.md` and the
  handlers registered for `.ska`/`.skl`/`.skm` in `tools/fingerprint_assets.py`.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- `.skf` is counted by two generic, suffix-agnostic aggregate scanners
  (`tools/fingerprint_assets.py`'s extension tally and the top-level
  HOG-validation member-suffix tally) and is **not** named in the
  purpose-built (but not-yet-run) aggregate size-fingerprint collector's
  `FROZEN_SUFFIXES` list (`tools/measure_member_structural_fingerprint.py`),
  which currently covers only `.fnt`, `.gui`, `.ie` — `.skf` has strictly less
  tracked-tooling attention than that trio.
- No `canonical_decoder` exists: `.skf` is absent from
  `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` registry (the only
  structural handlers in the tracked tooling: `.tdx`, `.ska`, `.skas`, `.skm`,
  `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`).
- No `structural_envelope_only` or `passive_descriptor_only` type exists
  either: a repo-wide search of `native/include/omega/**`, `native/src/**`,
  and `CMakeLists.txt` for `skf`/`SKF` tokens returned zero matches — there is
  no native header, source file, or CMake target of any kind for this suffix.
- **Adversarial/resource-boundary test gap:** because no decoder, structural
  handler, or even a defined-but-unrun aggregate schema exists, there is
  nothing to adversarially test. The gap here is one step behind `.ie`'s: for
  `.ie` a schema is at least defined and awaiting a corpus run; for `.skf` no
  schema exists in tracked source at all, so the first required step is
  schema definition/extension, not test-gap remediation.

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. No menu-role or other semantic
speculation is included.

1. **Add `.skf` to `FROZEN_SUFFIXES` in `tools/measure_member_structural_fingerprint.py`
   (or add a sibling collector following the same aggregate-only, path-free
   contract), run it against the owner corpus, and commit the JSON output
   under `analysis/formats/`.** This is the single highest-priority action:
   `.skf` currently has no size/byte aggregate schema at all — a strictly
   larger gap than `.ie`/`.gui`/`.fnt`, which at least have a defined schema
   awaiting a run (§3 C5, §6).
2. **Extend `tools/fingerprint_assets.py`'s scan aggregation to report a
   per-suffix depth histogram** (or a narrowly-scoped equivalent), to test
   H1 (§5) — whether `.skf`'s recursive/top-level count equality reflects a
   genuine "no nested occurrence" pattern rather than coincidental tallies.
3. **Record a ledger entry** citing whichever of steps 1-2 actually runs, with
   the exact tracked tool path and check command, following the existing
   `E-####` citation convention in `analysis/evidence/ledger.jsonl` — this is
   currently the missing link between "tool exists" and "evidence confirmed"
   for `.skf`.
4. **After step 1, compare `.skf`'s size-structure aggregate against the
   published `.ska`/`.skas`/`.skl`/`.skm` figures** (§5 H2) purely as an
   aggregate signal — record the comparison as a Hypothesis-confirmation or
   -refutation in a follow-up ledger entry, not as a semantic conclusion.
5. **Do not** write a native decoder, header, or CMake target for `.skf` until
   step 1's owner-corpus aggregate run produces concrete size/byte structure
   evidence — inventing one now, before any structural evidence exists, would
   be a regression per the clean-room rules and per the existing tooling's own
   docstring convention ("a plausible invented decoder is a regression").
