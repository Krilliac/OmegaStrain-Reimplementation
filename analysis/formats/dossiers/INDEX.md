# Omega Strain Asset-Dossier Set — Master Index

## What this is

This directory holds one per-suffix asset dossier for every file-extension family observed in
the owned NTSC-U disc corpus of *Omega Strain*. Each `<SUFFIX>.md` records, at the evidence level
this repository can actually defend, what is known about that family: its occurrence counts, its
mechanically-citable Confirmed facts, its aggregate-only structural observations, its explicitly
labeled Hypotheses, its Missing observations, its decoder/tooling classification, and a ranked
Codex work order. This file is the roll-up: a coverage table across all families and a single
cross-family work queue.

## Ground rules these dossiers obey (clean-room)

- **Tracked-evidence-only.** Every Confirmed fact is citable from a Git-tracked file in this
  repository (`analysis/formats/*`, `analysis/manifests/*`, `analysis/evidence/ledger.jsonl`,
  `tools/*`, `native/*`, `CMakeLists.txt`). Nothing is read from `private/`, `runtime/`,
  `third_party/`, `downloads/`, the D: drive, or any untracked/ignored content.
- **Path-free and aggregate-only.** No absolute paths, no private-input paths, no per-file rows,
  no member/file names beyond generic container names already present in tracked docs
  (e.g. `DATA.HOG`, `LOADING.HOG`), no payload bytes, no hashes or byte offsets tied to an
  individual private input. Aggregate counts and published format-grammar constants that already
  live in tracked `analysis/formats/*` are the only quantitative detail carried here.
- **No invented semantics.** A claim is one of: **Confirmed** (mechanically citable from a tracked
  file), **Aggregate-only** (a counting/structural fact), an explicitly labeled **Hypothesis**, or
  a **Missing-observation**. Families the tracked evidence does not establish are kept explicitly
  UNKNOWN. A plausible-but-invented decoder or semantic is treated as a regression; conservatism is
  the correct posture.

## Decoder-status vocabulary

| Status | Meaning |
| --- | --- |
| `canonical_decoder` | A tracked native decoder exists, is build/test-registered, and produces canonical output validated against the corpus. |
| `structural_envelope_only` | A tracked decoder/descriptor validates the container envelope (counts, extents, versioning) but assigns no payload semantics. |
| `passive_descriptor_only` | A tracked descriptor/validator reads and checks structure but emits no decoded asset; no canonical asset output is produced. |
| `aggregate_scanner_only` | Only the generic recursive scanner counts the suffix; no dedicated structural handler, grammar doc, decoder, or ledger claim exists. |
| `unknown` | Occurrence is counted but no structural class has been established either way; not yet even a passive structural aggregate. |
| `system_file_out_of_scope` | Tracked evidence classifies it as a PS2 system/boot/network-support file, not a game-asset grammar; no decoder work is warranted. |

## Coverage table

47 families total. Grouped by decoder status.

### canonical_decoder (8)

| Suffix | Decoder status | Confirmed | Hypotheses | Dossier |
| --- | --- | ---: | ---: | --- |
| .so | canonical_decoder | 21 | 4 | [SO.md](SO.md) |
| .col | canonical_decoder | 18 | 3 | [COL.md](COL.md) |
| .vum | canonical_decoder | 17 | 4 | [VUM.md](VUM.md) |
| .tdx | canonical_decoder | 16 | 5 | [TDX.md](TDX.md) |
| .vag | canonical_decoder | 15 | 4 | [VAG.md](VAG.md) |
| .pop | canonical_decoder | 14 | 4 | [POP.md](POP.md) |
| .hog | canonical_decoder | 13 | 3 | [HOG.md](HOG.md) |
| .par | canonical_decoder | 10 | 3 | [PAR.md](PAR.md) |

### structural_envelope_only (5)

| Suffix | Decoder status | Confirmed | Hypotheses | Dossier |
| --- | --- | ---: | ---: | --- |
| .lpd | structural_envelope_only | 16 | 4 | [LPD.md](LPD.md) |
| .vpk | structural_envelope_only | 15 | 5 | [VPK.md](VPK.md) |
| .ska | structural_envelope_only | 15 | 3 | [SKA.md](SKA.md) |
| .skas | structural_envelope_only | 14 | 3 | [SKAS.md](SKAS.md) |
| .skm | structural_envelope_only | 13 | 3 | [SKM.md](SKM.md) |

### passive_descriptor_only (2)

| Suffix | Decoder status | Confirmed | Hypotheses | Dossier |
| --- | --- | ---: | ---: | --- |
| .skl | passive_descriptor_only | 16 | 5 | [SKL.md](SKL.md) |
| .cnf | passive_descriptor_only | 12 | 3 | [CNF.md](CNF.md) |

### aggregate_scanner_only (17)

| Suffix | Decoder status | Confirmed | Hypotheses | Dossier |
| --- | --- | ---: | ---: | --- |
| .bnk | aggregate_scanner_only | 9 | 2 | [BNK.md](BNK.md) |
| .pss | aggregate_scanner_only | 9 | 3 | [PSS.md](PSS.md) |
| .skf | aggregate_scanner_only | 9 | 3 | [SKF.md](SKF.md) |
| .fnt | aggregate_scanner_only | 8 | 3 | [FNT.md](FNT.md) |
| .gun | aggregate_scanner_only | 8 | 3 | [GUN.md](GUN.md) |
| .ie | aggregate_scanner_only | 8 | 3 | [IE.md](IE.md) |
| .scc | aggregate_scanner_only | 8 | 3 | [SCC.md](SCC.md) |
| .skel | aggregate_scanner_only | 8 | 3 | [SKEL.md](SKEL.md) |
| .txt | aggregate_scanner_only | 8 | 3 | [TXT.md](TXT.md) |
| .wdb | aggregate_scanner_only | 8 | 3 | [WDB.md](WDB.md) |
| .bon | aggregate_scanner_only | 7 | 3 | [BON.md](BON.md) |
| .map | aggregate_scanner_only | 7 | 3 | [MAP.md](MAP.md) |
| .prn | aggregate_scanner_only | 7 | 3 | [PRN_.md](PRN_.md) (named with a trailing `_` because `PRN` is a reserved Windows device name) |
| .bin | aggregate_scanner_only | 6 | 3 | [BIN.md](BIN.md) |
| .dat | aggregate_scanner_only | 6 | 3 | [DAT.md](DAT.md) |
| .gui | aggregate_scanner_only | 6 | 3 | [GUI.md](GUI.md) |
| .bd | aggregate_scanner_only | 0 | 3 | [BD.md](BD.md) |

### unknown (4)

| Suffix | Decoder status | Confirmed | Hypotheses | Dossier |
| --- | --- | ---: | ---: | --- |
| .tbl | unknown | 10 | 2 | [TBL.md](TBL.md) |
| .icn | unknown | 8 | 3 | [ICN.md](ICN.md) |
| .hd | unknown | 6 | 3 | [HD.md](HD.md) |
| .sub | unknown | 7 | 3 | [SUB.md](SUB.md) |

### system_file_out_of_scope (11)

| Suffix | Decoder status | Confirmed | Hypotheses | Dossier |
| --- | --- | ---: | ---: | --- |
| .log | system_file_out_of_scope | 7 | 2 | [LOG.md](LOG.md) |
| .ini | system_file_out_of_scope | 7 | 2 | [INI.md](INI.md) |
| .pf | system_file_out_of_scope | 7 | 3 | [PF.md](PF.md) |
| .rgb | system_file_out_of_scope | 7 | 2 | [RGB.md](RGB.md) |
| .tm2 | system_file_out_of_scope | 7 | 2 | [TM2.md](TM2.md) |
| .sys | system_file_out_of_scope | 6 | 3 | [SYS.md](SYS.md) |
| .elf | system_file_out_of_scope | 5 | 2 | [ELF.md](ELF.md) |
| .ico | system_file_out_of_scope | 5 | 2 | [ICO.md](ICO.md) |
| .irx | system_file_out_of_scope | 5 | 2 | [IRX.md](IRX.md) |
| .64 | system_file_out_of_scope | 4 | 2 | [64.md](64.md) |
| .img | system_file_out_of_scope | 2 | 2 | [IMG.md](IMG.md) |

## Cross-family Codex work queue (ranked)

Ranked by how directly tracked evidence motivates the step. Every item names a family and a
privacy-safe action (owner-corpus runs report only sanitized aggregate counters; synthetic tests
use generated byte buffers, never private disc bytes). No menu-role or gameplay-semantic
speculation is used to justify any item.

**Tier 1 — close owner-corpus validation gaps that a tracked ledger entry explicitly leaves open
(existing decoders proven only on synthetic fixtures).**

1. **.par** — Run the existing `DecodeParTextEnvelope` against the real owner PAR corpus (all 679
   tracked spans) and record only aggregate pass/fail/typed-error-code counts. Closes the
   "generated fixtures only" gap that ledger entry **E-0092** explicitly notes.
2. **.vpk** — Run the existing `DecodeVpkWrapperEnvelope` against the full owner corpus of 85
   tracked `.vpk` members and record only an aggregate pass/fail count plus a `DecodeErrorCode`
   histogram on failure. Closes the owner-corpus-validation gap that ledger entry **E-0094**
   explicitly leaves unclaimed.
3. **.lpd** — Confirm the merge/validation state of ledger entry **E-0091** (grep
   `analysis/evidence/ledger.jsonl` for a follow-up entry; check branch/CI status) before treating
   the LPD counted-envelope decoder as landed and trusted — E-0091 itself records publication and
   exact-main validation as pending.
4. **.skl** — Run the existing `InspectSklContainer` decoder over all 1,261 tracked `.skl` spans in
   the owner extraction, locally, and report only accept/reject counts and a `DecodeErrorCode`
   histogram, corpus-validating the grammar already inferred from aggregate observations.
5. **.vag** — Extend `fingerprint_vag` to add an aggregate-only accept/reject sweep of the real
   8,665-entry corpus against `DecodeVagAdpcm`'s exact acceptance rules (version set, reserved word,
   sample rate, 16-byte data alignment, tail-length rule), publishing only bucketed pass/fail
   counts. Closes the gap between synthetic-fixture-proven correctness and real-corpus coverage.
6. **.vum** — Extend `tools/validate_vum_read_trace.py` to capture additional bounded, sanitized
   consumer-read trace pairs targeting VUM-relative offsets in the metadata-record and payload
   regions (beyond the current header-only-confirmed window), reusing the exact allow-listed schema,
   to test the render-payload consumption hypothesis without exposing payload bytes.
7. **.skas** — Wire the existing, tested `DecodeSkasTextEnvelope` decoder into `omega_tool`
   `asset-metadata-verify-tree` (mirroring the `.ska` `RecordSkaStructure` pattern) to get
   independent, reproducible corpus-wide confirmation of the two-candidate text-envelope claim,
   emitting only sanitized aggregate counters.

**Tier 2 — add adversarial/boundary tests to existing decoders (synthetic buffers only) to remove
asymmetric or missing robustness evidence.**

8. **.hog** — Add synthetic boundary/adversarial byte-buffer tests for the top-level
   `HogArchive::Open` / `HogIndex::Open` path (max entry count, max directory size, max name length,
   trailing-nonzero-byte), mirroring the existing nested-range malformed-buffer coverage; closes the
   one gap keeping the top-level vs. nested-range robustness evidence asymmetric.
9. **.ska** — Add explicit rejection-path unit tests (bad version word, zero `word_0x04`/`word_0x08`,
   unsupported `word_0x10`, undersized header, extent-exceeds-input) using only synthetic buffers,
   closing the adversarial-test gap versus the Python fingerprinter's already-implemented rejection
   branches.
10. **.tdx** — Build and CMake-register a synthetic adversarial/fuzz suite for
    `tdx_texture_storage_decoder.cpp` (malformed headers, oversized `block_count`/`block_stride`,
    truncated files, corrupt `palette_plane_count`), exercising generated fixtures only.
11. **.skm** — Audit/complete unit-test coverage for all non-limit `DecodeErrorCode` branches of
    `InspectSkmContainer` (Unsupported/Truncated/Malformed/Overflow paths); only the three
    LimitExceeded cases are confirmed tested, the rest are implemented but untested individually.

**Tier 3 — mechanical verification / ledger-completeness on decoders already classified canonical.**

12. **.so** — Reconcile the 139 (corpus-wide `.so` count) / 119 (summed SCRIPTS.HOG entry_count) /
    118 (inspector-parsed module count) discrepancy by extending the HOG-walking tooling to emit an
    aggregate-only `.so`-count-by-container-suffix histogram plus a non-`.so`-sibling-entry count
    inside the 19 tracked SCRIPTS.HOG archives.
13. **.pop** — Extend the structural hypothesis scorer/profiler to the 12 remaining unscored
    post-GOB literal markers (`GOB:`, `SND:`, `ACL:`, `NPC:`, `SKY:`, `NOD:`, `GEN:`, `GRP:`,
    `BOX:`, `FIR:`, `CAM:`, `BUG:`), reporting only the same aggregate-only exact-match schema
    already used for the 5 scored markers.
14. **.col** — Backfill a dedicated ledger entry enumerating the
    `col_spatial_mesh_decoder_tests.cpp` adversarial/boundary test-case classes and pass counts,
    matching the granularity already given for SKAS (E-0093) and the texture-storage debug adapter
    (E-0066); COL currently asserts test coverage only in prose.

**Tier 4 — establish first-ever structural evidence for `unknown` families (aggregate-only probes,
no payload/name/hash export).**

15. **.hd / .icn / .sub / .tbl** — Extend `tools/fingerprint_assets.py` (or `direct_map_summary()`
    for text-shaped `.tbl`) with a bounded aggregate structural class (magic-byte bucket, size,
    ASCII ratio, alignment) for each, publishing results as new aggregate-only ledger/JSON entries.
    For `.icn` and `.hd` the resulting structural evidence — not general platform knowledge — is
    what should decide promotion to `system_file_out_of_scope` versus scoping a proper grammar doc.

**Tier 5 — add passive aggregate-only handlers for the 17 `aggregate_scanner_only` families.**

16. Add minimal, uniform `fingerprint_*` handlers (size/magic-class/alignment/ASCII-ratio
    histograms, no per-file rows or payload) to `FORMAT_HANDLERS` and re-run the scan to populate
    real `formats.*` aggregate blocks for: **.bnk, .pss, .skf, .fnt, .gun, .ie, .scc, .skel, .txt,
    .wdb, .bon, .map, .prn, .bin, .dat, .gui, .bd**. Several (`.gui`, `.ie`, `.skf`) can instead be
    served by the already-frozen, privacy-safe `tools/measure_member_structural_fingerprint.py`.
    These are prerequisites: every downstream hypothesis or grammar doc for these families depends
    on the aggregate block existing first. Explicitly label any n=1 result (e.g. `.wdb`, `.prn`).

**Non-actions (do not build ahead of evidence).**

17. For the 11 `system_file_out_of_scope` families (**.64, .elf, .ico, .img, .ini, .irx, .log, .pf,
    .rgb, .sys, .tm2**): do **not** add them to `FORMAT_HANDLERS`, native descriptors, or CMake
    targets. Where useful, record the out-of-scope scoping decision in the format-index docs
    (e.g. `ASSET-RECON.md`) so future passes don't re-open them as game-asset candidates without
    cause, and permit only bounded, filename/size-only sibling-listing or text-vs-binary aggregate
    probes to corroborate the classification.

## What remains unproven

Across the whole set, no dossier assigns gameplay, rendering, audio, spatial, or naming semantics
to any payload region — those interpretations remain Hypotheses everywhere. The `canonical_decoder`
and `structural_envelope_only` families have validated *structure* (counts, extents, envelope
grammar, container topology), not *meaning*; several of them (`.par`, `.vpk`, `.vum`, `.skas`,
`.skl`) are still proven only against synthetic fixtures or a header-only window and lack the
recorded owner-corpus sweep that Tier 1 would supply, and `.lpd` awaits confirmation that E-0091's
pending publication/validation actually landed. The four `unknown` families (`.hd`, `.icn`, `.sub`,
`.tbl`) have no established structural class at all — not even a passive aggregate — so their
system-file-vs-asset status is genuinely open. The 17 `aggregate_scanner_only` families are counted
but never structurally opened. And the `system_file_out_of_scope` calls rest on directory-level and
container-composition evidence rather than payload decoding, which is sufficient to decline decoder
work but is not a positive decode of those files. In short: structure is established where claimed;
semantics are not, and the tracked evidence deliberately stops short of inventing them.
