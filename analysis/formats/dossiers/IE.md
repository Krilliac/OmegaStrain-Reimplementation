# `.ie` — Format Dossier

## 1. Identity

`.ie` is a HOG-archive member suffix tracked only as a generic, undifferentiated
extension bucket by two aggregate-counting tools. It has **no published
grammar, no structural decoder, and no confirmed internal layout**. It is one
of a small named set of "front-end" suffixes (`.gui`, `.fnt`, `.ie`) that a
dedicated tracked collector script singles out for *future* aggregate size
measurement, precisely because no size/byte/structure evidence has been
committed for it yet. No semantic meaning (menu role, dialog data, script,
text, or anything else) can be asserted from tracked evidence — the family is
UNKNOWN at the semantic level.

## 2. Occurrence evidence

| Metric | Count | Tracked source |
|---|---|---|
| Recursive-in-HOG occurrences (whole containment tree, all nesting depths) | 79 | `analysis/formats/asset-fingerprints.json` (`scan.extensions[".ie"]`) |
| Top-level-HOG occurrences (top-level archives only) | 23 | `analysis/formats/hog-validation.json` (`entry_extensions[".ie"]`) |
| Whole-disc occurrences (outside any HOG) | 0 | `analysis/manifests/disc-summary.json` (extension histogram; `.ie` absent) |

The gap between 79 recursive and 23 top-level confirms most `.ie` members live
inside nested/child HOG archives rather than top-level ones, consistent with
the general nested-HOG structure documented for the corpus, but this dossier
draws no further inference from that gap.

## 3. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Tracked source citation |
|---|---|---|
| C1 | The suffix `.ie` is recognized and counted by the recursive asset-fingerprint scanner as a plain extension bucket (no structural handler attached). | `analysis/formats/asset-fingerprints.json` (`scan.extensions`); `tools/fingerprint_assets.py` (`FORMAT_HANDLERS` registers structural handlers only for `tdx`, `ska`, `skas`, `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk` — `.ie` is absent from that registration) |
| C2 | The suffix `.ie` is recognized and counted by the top-level HOG-validation script as a top-level member-suffix bucket. | `analysis/formats/hog-validation.json` (`entry_extensions`) |
| C3 | `tools/fingerprint_assets.py` builds its recursive-scan extension set from `target_extensions = set(FORMAT_HANDLERS)` for the disc-root direct-map pass, and `.ie` is not in that set — i.e. the tool's own structural-handler registry mechanically excludes `.ie` from any payload interpretation. | `tools/fingerprint_assets.py` (`FORMAT_HANDLERS`, `target_extensions`) |
| C4 | `.ie` is absent from the "frozen public extension vocabulary" list tracked by the front-end HOG-topology measurement tool (`.col`, `.hog`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, `.vum`); suffixes outside that frozen vocabulary, including `.ie`, fall into that tool's undifferentiated `other` bucket. | `tools/measure_frontend_hog_topology.py` (module docstring, "frozen public extension vocabulary" list) |
| C5 | A second, dedicated tracked collector, `tools/measure_member_structural_fingerprint.py`, defines a frozen public constant `FROZEN_SUFFIXES = (".fnt", ".gui", ".ie")` naming `.ie` as one of exactly three front-end suffixes for which the evidence audit records no size/byte/alignment data, and states explicitly that it is "an evidence collector, not a decoder" that assigns "NO field, layout, alignment, role, or format semantics." | `tools/measure_member_structural_fingerprint.py` (module docstring; `FROZEN_SUFFIXES` constant, line ~75) |
| C6 | No `native/include/omega/**` header, no `native/src/**` source file, and no CMake target in `CMakeLists.txt` references `.ie`, an "IE" asset type, or any decoder/descriptor named for this suffix (confirmed by repo-wide word-boundary search for standalone `ie`/`IE` tokens across `native/` and `CMakeLists.txt`, which returned zero matches). | `native/include/omega/**`, `native/src/**`, `CMakeLists.txt` (absence confirmed by search) |
| C7 | No entry in the evidence ledger references `.ie` (confirmed by grep for `.ie` / `"ie"` across the ledger, which returned zero matches) — no claim about this suffix has yet passed the ledger's confirmed/rejected process. | `analysis/evidence/ledger.jsonl` (absence confirmed by search) |
| C8 | No committed output document exists anywhere in the tracked tree from `tools/measure_member_structural_fingerprint.py` (no per-suffix `count`/`size_min`/`size_max`/`size_distinct`/`size_gcd` JSON for `.ie` is present under `analysis/`) — the collector's schema is defined in tracked source but has not been run against any corpus and its output committed. | `tools/measure_member_structural_fingerprint.py` (schema definition); absence confirmed by search of `analysis/formats/*.json` and `analysis/formats/*.md` |

## 4. Aggregate-only facts

Tracked aggregate counts with no semantic interpretation attached.

- Recursive-in-HOG member count for `.ie` across the whole recursively-scanned
  HOG containment tree: **79** (`analysis/formats/asset-fingerprints.json`,
  `scan.extensions[".ie"]`). In that same table it sits among comparably
  low/mid-frequency suffixes — `.fnt` (3), `.gui` (77), `.skas` (2), `.skel`
  (4), `.skf` (26) — versus high-volume families such as `.tdx` (15248),
  `.skm` (4219), or `.col`/`.vum` (7036 each).
- Top-level-HOG member count for `.ie` across all validated top-level
  archives: **23** (`analysis/formats/hog-validation.json`,
  `entry_extensions[".ie"]`), alongside `.gui` (21) and `.fnt` (3) in the same
  top-level table.
- The recursive scan that produced the 79 figure covered 53,281 total asset
  spans across nesting depths `-1` (5 spans), `0` (32,351 spans), `1` (20,925
  spans) — this depth histogram is global across all extensions, not broken
  out per suffix, so it cannot be attributed to `.ie` specifically beyond
  "some subset of these depths." (`analysis/formats/asset-fingerprints.json`,
  `scan.asset_spans_scanned`, `scan.depth`)
- No size range, minimum/maximum payload size, distinct-size count, or
  size-GCD is present in any tracked JSON for `.ie`. The one tracked tool
  designed to produce that aggregate (`tools/measure_member_structural_fingerprint.py`)
  defines the schema fields (`count`, `minimum`, `maximum`, size-distinct,
  size-GCD) but no corresponding output document for `.ie` exists in the
  tracked tree — this is an aggregate schema, not aggregate data, for this
  family.
- The dataset's declared scope is "aggregate structural fingerprints only; no
  proprietary payloads exported" (schema_version 3) — i.e. the entire `.ie`
  count of 79 is itself an aggregate figure, not a per-file listing, consistent
  with clean-room constraints. (`analysis/formats/asset-fingerprints.json`,
  `scope`, `schema_version`)

## 5. Hypotheses

Explicitly labeled; none of these are asserted as fact anywhere above.

- **H1 — "Front-end/UI-adjacent grouping."** The tracked tooling groups `.ie`
  with `.gui` and `.fnt` under one frozen-suffix constant and one docstring
  phrase ("front-end HOG members"), which is itself a tracked, citable
  grouping decision — but the grouping is a tooling-vocabulary choice, not a
  confirmed semantic claim about what `.ie` files contain. *Privacy-safe
  observation that would confirm or refute*: running
  `tools/measure_member_structural_fingerprint.py` against the owner corpus
  and inspecting whether `.ie` payload sizes/structure cluster near `.gui`/`.fnt`
  (e.g. similar size distribution, similar GCD) versus resembling an unrelated
  family (e.g. `.tdx`-like size buckets) would be an aggregate-only signal,
  not proof of shared semantics, but it is the only privacy-safe next
  observation available.
- **H2 — "Small population, likely per-level or per-menu-instance data."**
  The 79 recursive / 23 top-level counts are small relative to per-asset
  families (`.tdx`, `.skm`, `.vag`), which is consistent with — but does not
  establish — a low-cardinality, one-per-context file population. *Privacy-safe
  observation that would confirm or refute*: an aggregate co-occurrence count
  (e.g. how many `.ie` members share a normalized basename/stem with a `.gui`
  or `.hog` container at the same nesting depth) computed by a tracked,
  path-free tool would either support or undercut a low-cardinality,
  per-context pattern without revealing any private filename.
- **H3 — "Text or script-like content."** No tracked evidence supports or
  refutes this; it is listed only because the suffix's mnemonic invites the
  guess and must be explicitly flagged as unconfirmed rather than silently
  assumed. *Privacy-safe observation that would confirm or refute*: an
  ASCII/printable-byte-ratio aggregate (analogous to the `direct_map_files`
  `ascii_text` field already in `asset-fingerprints.json` for other suffixes)
  computed by a new tracked, aggregate-only collector and run against the
  owner corpus — reporting only a ratio/histogram, never bytes or per-file
  rows — would be the correct next observation.

## 6. Missing observations

Tracked evidence that does not exist, and the privacy-safe collection that
would produce it:

- **Payload size structure.** No `count`/min/max/distinct-size/size-GCD
  aggregate exists for `.ie`. Collection: run
  `tools/measure_member_structural_fingerprint.py` against the owner corpus
  and commit its JSON output under `analysis/formats/`.
- **Byte-level structural signature.** No magic-number, header-constant, or
  compression-magic check exists for `.ie` (the compression-magic scan in
  `asset-fingerprints.json` — `standard_compression_magic_hits: 0` /
  `standard_compression_spans_checked: 46604` — is global across all scanned
  spans, not specific to `.ie`). Collection: extend the frozen-suffix
  collector (or a new sibling tool following the same aggregate-only,
  path-free contract) to report a magic-byte histogram for `.ie` spans only.
- **Ledger entry.** No `E-####` ledger row exists for `.ie`. Collection: once
  an owner-corpus aggregate run for `.ie` completes, record a ledger entry
  citing the exact tracked tool path and command used, following the existing
  `E-####` citation convention in `analysis/evidence/ledger.jsonl`.
- **Ratio/spread against sibling suffixes `.gui`/`.fnt`.** No tracked
  aggregate directly compares `.ie` to its declared siblings (`.gui`, `.fnt`)
  beyond the raw recursive/top-level counts already listed in §4. Collection:
  a small tracked aggregate table (counts and size-structure side by side for
  the three `FROZEN_SUFFIXES`) run once §6's payload-size collection exists
  for all three.
- **Co-occurrence with container archives at the point of use.** No tracked
  aggregate reports which container types (`.hog`, `.gui`, nested archives)
  most frequently hold `.ie` members. Collection: a path-free co-occurrence
  counter analogous to the "same-basename groups/pairs" aggregate already
  defined in `tools/measure_frontend_hog_topology.py`, extended to include
  `.ie` in its counted-pair vocabulary.

## 7. Decoder/tooling status

**Classification: `aggregate_scanner_only`**

- `.ie` is counted by two generic, suffix-agnostic aggregate scanners
  (`tools/fingerprint_assets.py`'s extension tally and
  `tools/hog-validation` member-suffix tally) and is named in one
  purpose-built (but not-yet-run) aggregate size-fingerprint collector
  (`tools/measure_member_structural_fingerprint.py`, `FROZEN_SUFFIXES`).
- No `canonical_decoder` exists: `.ie` is absent from
  `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` registry (which holds the
  only structural handlers in the tracked tooling: `tdx`, `ska`, `skas`,
  `skm`, `skl`, `vag`, `lpd`, `par`, `col`, `vum`, `vpk`).
- No `structural_envelope_only` or `passive_descriptor_only` type exists
  either: a repo-wide word-boundary search of `native/include/omega/**`,
  `native/src/**`, and `CMakeLists.txt` for standalone `ie`/`IE` tokens
  returned zero matches — there is no native header, source file, or CMake
  target of any kind for this suffix.
- **Adversarial/resource-boundary test gap:** because no decoder or
  structural handler exists, there is nothing to adversarially test. The one
  relevant gap is that `tools/measure_member_structural_fingerprint.py` —
  which does exist in tracked source and does define a schema for `.ie` —
  has no committed run output and (per its own docstring) reuses the bounded
  traversal/resource-budget/error-vocabulary core from
  `tools/measure_frontend_hog_topology.py`; whether that shared safety core
  has been exercised specifically against `.ie`-bearing archives is unverified
  from tracked evidence alone.

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. No menu-role or other semantic
speculation is included.

1. **Run `tools/measure_member_structural_fingerprint.py` against the owner
   corpus** and commit its JSON output under `analysis/formats/` — this is
   the single highest-priority action: it is the one tracked tool already
   built and schema-defined specifically for `.ie` (jointly with `.gui`/
   `.fnt`), and its absence is the largest confirmed evidence gap in this
   dossier (§3 C8, §6).
2. **Record a ledger entry** citing whichever of step 1 actually runs, with
   the exact tracked tool path and check command, following the existing
   `E-####` citation convention in `analysis/evidence/ledger.jsonl` — this is
   currently the missing link between "tool exists" and "evidence confirmed"
   for `.ie`.
3. **Extend the frozen-suffix collector (or add a sibling tool) with a
   magic-byte / compression-magic histogram scoped to `.ie` spans**, following
   the same aggregate-only, path-free contract, to close the byte-level
   structural-signature gap identified in §6.
4. **Do not** write a native decoder, header, or CMake target for `.ie` until
   step 1's owner-corpus aggregate run produces concrete size/byte structure
   evidence — inventing one now, before any structural evidence exists, would
   be a regression per the clean-room rules and per this tool's own docstring
   ("a plausible invented decoder is a regression").
5. **After step 1, compare `.ie`'s size-structure aggregate against its
   declared siblings `.gui`/`.fnt`** (§5 H1) purely as an aggregate signal —
   record the comparison as a Hypothesis-confirmation or -refutation in a
   follow-up ledger entry, not as a semantic conclusion.
