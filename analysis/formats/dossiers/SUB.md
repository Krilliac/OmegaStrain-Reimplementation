# .sub — Format Dossier

## 1. Identity

`.sub` is a game-asset suffix that occurs exclusively as an in-archive member
name inside `HOG` container files in the owned corpus. No `.sub` member has
been observed at the top level of the whole-disc filesystem. Beyond the
suffix itself and its occurrence pattern, tracked evidence establishes no
container grammar, header magic, or semantic role for this format — it has
no structural fingerprint handler and no native decoder. At the evidence
level this dossier can defend, `.sub` is an **unclassified in-HOG asset
suffix**: its internal layout, purpose, and relationship to any other family
are UNKNOWN.

## 2. Occurrence evidence

| Scope | Count | Tracked source |
|---|---|---|
| Recursive, inside HOG archives | 42 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".sub"]` (line ~618) |
| Top-level HOG member-suffix tally | 42 | `analysis/formats/hog-validation.json` → `entry_extensions[".sub"]` (line ~1663) |
| Whole-disc (outside any HOG) | 0 | `analysis/manifests/disc-summary.json` → `extensions` map has no `.sub` key |

The recursive-in-HOG count (42) and the top-level-HOG member tally (42) are
identical, and no `.sub` entries appear on the whole disc outside HOG
containers. This is consistent with `.sub` being an asset packed only inside
HOG archives, with no top-level appearance and no evidence (from these two
counters alone) of it also occurring nested inside a HOG-inside-HOG span —
tracked sources do not break the 42 down by nesting depth, so that question
is a Missing observation (Section 6).

## 3. Confirmed facts

| # | Fact | Tracked source |
|---|---|---|
| C1 | `.sub` appears 42 times in the recursive suffix inventory computed by `tools/fingerprint_assets.py` over the owned HOG corpus. | `analysis/formats/asset-fingerprints.json`, `scan.extensions[".sub"]` |
| C2 | `.sub` appears 42 times in the top-level-HOG member-suffix tally. | `analysis/formats/hog-validation.json`, `entry_extensions[".sub"]` |
| C3 | `.sub` produces zero entries in the whole-disc file histogram (`disc-summary.json` `extensions` map enumerates every whole-disc suffix present and `.sub` is absent). | `analysis/manifests/disc-summary.json` |
| C4 | `.sub` is **not** a key in `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` dict (which lists `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk` only). Consequently the tool records only its generic suffix count for `.sub`, never a structural aggregate (size buckets, header fields, etc.) — there is no `formats.sub` object in `asset-fingerprints.json`'s `formats` map (that map's only keys are `col, lpd, par, ska, skas, skl, skm, tdx, vag, vpk, vum`). | `tools/fingerprint_assets.py` lines ~499–511; `analysis/formats/asset-fingerprints.json` `formats` map |
| C5 | No native decoder, descriptor, or CMake/test registration for `.sub` exists in the tracked native tree: a suffix search over `native/include/omega/**` and `native/src/**` for a `.sub`-format literal returns no matches (the few case-insensitive `sub` string hits in `native/include/omega/runtime/frame_scheduler.h`, `native/include/omega/runtime/job_service.h`, and `native/include/omega/compat/ps2_memory_card_filesystem.h` are unrelated substrings, e.g. "subsystem"/"subscribe" — none reference a `.sub` asset format). | `native/include/omega/**`, `native/src/**` (grep, no hits); `CMakeLists.txt` (no `.sub` reference) |
| C6 | No entry in the evidence ledger (`E-0001`…) makes any claim naming `.sub` or the `.sub` suffix specifically. | `analysis/evidence/ledger.jsonl` (grep for `.sub`/`sub` format field, no hits) |
| C7 | No published format-grammar doc (`analysis/formats/*.md`) currently documents `.sub` — the existing docs cover `HOG`, `COL`, `LPD`, `PAR`, `POP`, `SKAS`, `SO`, `TDX`, `VAG`, `VPK`, `VUM`, plus `ASSET-RECON.md` and `FRONTEND-TOPOLOGY.md`, none of which mention `.sub`. | `analysis/formats/*.md` (directory listing + grep, no hits) |

## 4. Aggregate-only facts

| Fact | Tracked source |
|---|---|
| The recursive `.sub` count (42) sits in a suffix table alongside other HOG-packed suffixes with counts spanning single digits (`.prn`: 1, `.scc`: 1, `.fnt`: 3) up to tens of thousands (`.tdx`: 15248, `.vag`: 8665) — 42 places `.sub` among the lower-frequency suffixes in the recursive inventory, comparable in order of magnitude to `.skas` (2), `.skel` (4), `.skf` (26), rather than to the high-volume mesh/texture/audio suffixes. | `analysis/formats/asset-fingerprints.json` `scan.extensions` |
| No size-range, alignment, magic-byte, or bucket aggregate exists for `.sub` specifically, because it has no entry in the `formats` map (see C4) and no structural handler ran against it. All aggregate facts available for `.sub` are limited to the bare occurrence counts in Section 2/Table above — no byte-level aggregate (min/max size, header field distribution, etc.) has been computed. | `analysis/formats/asset-fingerprints.json` (absence of a `sub` key under `formats`) |

## 5. Hypotheses

All items below are explicitly labeled hypotheses. None is asserted as fact, and none should be treated as informing a decoder.

- **H1 — Subtitle/caption or auxiliary-stream asset.** The suffix `.sub` is conventionally associated with subtitle streams in many media formats generally. *Privacy-safe confirming/refuting observation:* running the existing structural aggregate scanner (`tools/fingerprint_assets.py`) with a new handler that reports only aggregate size-distribution and header-byte statistics (no payload bytes, no per-file rows) for the 42 `.sub` members; if sizes cluster tightly and small (consistent with text/caption streams) that is weak support, while a wide size spread or large sizes would refute it. This has NOT been run — Section 4 shows no aggregate currently exists.
- **H2 — Sidecar/companion file to another asset family co-resident in the same HOG.** Because `.sub` never appears at the whole-disc top level and always inside HOG archives at a count (42) that is a plausible companion-multiplier of some other in-HOG suffix, it may be a per-asset auxiliary file paired 1:1 or N:1 with members of another family. *Privacy-safe confirming/refuting observation:* an aggregate-only cross-tabulation (counts only, no names) of how many HOG containers hold at least one `.sub` member versus how many hold none, and the aggregate count of `.sub` members per container that has any — without naming containers or files.
- **H3 — Generic/placeholder or leftover authoring-tool suffix with no runtime role.** Given the complete absence of a native decoder, CMake registration, or structural handler after multiple passes of format-recon work covering higher-volume suffixes, `.sub` may be an asset class the shipped game engine reads through a shared/generic loader rather than a dedicated one (e.g., consumed as an opaque blob by whatever loads its parent family), or may be unused at runtime. *Privacy-safe confirming/refuting observation:* a symbol/string search over any tracked disassembly or reverse-engineering notes (if such tracked docs exist) for a `.sub`-suffix string constant; none is currently known to exist in the tracked tree (see Section 6).

## 6. Missing observations

- **No structural handler has ever been run against `.sub`.** `tools/fingerprint_assets.py`'s `FORMAT_HANDLERS` omits `.sub`, so no header-magic, size-bucket, or alignment aggregate has been produced. *Privacy-safe collection:* add a `fingerprint_sub` aggregate-only handler (size min/max/mean, first-N-byte histogram or magic-byte frequency, alignment/padding stats) to `FORMAT_HANDLERS` and rerun the existing recursive scanner — output format already enforces aggregate-only, no-payload-export scope (see `scope` field in `asset-fingerprints.json`).
- **No nesting-depth breakdown exists for the 42 recursive occurrences.** It is unknown whether all 42 are one level deep (directly inside a top-level HOG) or whether some are nested inside a HOG-inside-HOG span. *Privacy-safe collection:* extend the scanner's aggregate output to bucket `.sub` occurrence counts by nesting depth (an integer histogram, no paths), matching the pattern already used for `nested_hog_*` aggregate fields in `asset-fingerprints.json`.
- **No co-occurrence/sidecar aggregate exists.** Whether `.sub` members cluster with a specific sibling suffix inside the same container (supporting H2) is unmeasured. *Privacy-safe collection:* an aggregate-only per-container suffix-set tally (e.g., "N containers contain both `.sub` and `.X`") without naming any container or member.
- **No native/RE evidence search has been recorded for a `.sub` string constant.** No tracked ledger entry or format doc records having searched executable strings/symbols for `.sub`. *Privacy-safe collection:* a string-constant search over any tracked (non-third_party, non-runtime) disassembly notes, recorded as an aggregate hit/no-hit ledger entry (`E-####`) rather than raw disassembly excerpts.
- **No magic-byte/header sample has been taken.** Given `.sub`'s low frequency (42), a privacy-safe aggregate summary of just the first 4–16 bytes' byte-value histogram (not the bytes of any single member) across all 42 could establish whether they share a common magic/header at all, without exposing any per-file content.

## 7. Decoder/tooling status

**Classification: `unknown`**

Justification: `.sub` is tracked and in-scope (42 recursive / 42 top-level-HOG
occurrences, 0 whole-disc), so it is not `system_file_out_of_scope`. But no
tracked evidence shows any level of decode capability against it:

- No native decoder or descriptor exists (`native/include/omega/**`,
  `native/src/**`) — ruling out `canonical_decoder` and
  `passive_descriptor_only`.
- No CMake target or test registration references `.sub` in
  `CMakeLists.txt`.
- No structural/envelope handler exists in `tools/fingerprint_assets.py`'s
  `FORMAT_HANDLERS` (it is a bare suffix-count entry only) — ruling out
  `structural_envelope_only`.
- The only tracked tooling that touches `.sub` at all is the generic
  recursive suffix counter (`scan.extensions`) and the top-level HOG
  member-suffix tally (`entry_extensions`) in `hog-validation.json` — both
  produce a bare occurrence count, not a structural aggregate over the
  format's own bytes (no size/header/magic aggregate exists per Section 4).
  This falls short of `aggregate_scanner_only`, which in this project's
  usage denotes a format that at least has a per-format aggregate object
  (as `.col`, `.lpd`, `.par`, `.ska`, `.skas`, `.skm`, `.skl`, `.tdx`,
  `.vag`, `.vpk`, `.vum` do). `.sub` has no such object.

No adversarial or resource-boundary test gap can be assessed because there is
no decoder or handler to test.

## 8. Codex work order

Ranked, concrete, privacy-safe next steps. None below asserts a semantic
meaning for `.sub` — each is a mechanical collection or tooling step against
the aggregate scanner already in the tracked repo.

1. **(Highest priority) Add `.sub` to `FORMAT_HANDLERS` in `tools/fingerprint_assets.py` as an aggregate-only structural handler** (size min/max/mean/stddev, magic/first-N-byte value histogram, alignment/padding observations) mirroring the existing handlers for `.col`/`.lpd`/`.par`/etc., then rerun the recursive scanner over the owned corpus and merge the resulting `formats.sub` aggregate object into `asset-fingerprints.json`. This directly fills the Section 4/6 gap and is the same mechanism already proven safe (aggregate-only, `scope` field enforced) for every other handled suffix.
2. Extend the scanner to emit a nesting-depth histogram for `.sub` occurrences (bucketed integer counts, no names) to resolve whether all 42 are top-level-HOG members or some are nested, closing the Missing-observation in Section 6.
3. Add an aggregate-only per-container suffix-co-occurrence tally (counts of containers holding `.sub` alongside each other tracked suffix, no container names) to test Hypothesis H2 without violating the no-per-file-row / no-path rule.
4. Record the outcome of steps 1–3 as new ledger entries (`E-####`) in `analysis/evidence/ledger.jsonl`, each citing the specific `asset-fingerprints.json` field it confirms, so future dossier revisions can promote H1/H2/H3 out of Hypothesis status only on cited aggregate evidence.
5. If step 1's byte histogram shows a stable magic/header pattern, escalate to drafting a `SUB.md` grammar doc (structural fields only, still aggregate-derived) before considering any native decoder or descriptor work — do not write a native decoder until a structural grammar is aggregate-confirmed.
