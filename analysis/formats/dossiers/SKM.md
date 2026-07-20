# .skm — Format Dossier

## 1. Identity

`.skm` is a HOG-archived, FourCC-less container with a compact leading chunk table. At the evidence
tier this repository can defend, `.skm` is a **structurally-validated container family** ("SKM
mesh" per `analysis/formats/ASSET-RECON.md`) — every observed instance satisfies a self-consistent
chunk/qword size formula and carries a fixed version byte. The chunk payloads' vertex/VIF semantics
and any bone-weighting role are **not** decoded; no mesh, skeleton, or rendering meaning is claimed
here.

## 2. Occurrence evidence

| Scope | Count | Source |
| --- | ---: | --- |
| Recursive, inside nested HOGs | 4,219 | `analysis/formats/asset-fingerprints.json` → `scan.extensions[".skm"]` and `formats.skm.count` |
| Top-level HOG member suffix | 2,808 | `analysis/formats/hog-validation.json` → `entry_extensions[".skm"]` |
| Whole-disc (outside HOG archives) | 0 | `analysis/manifests/disc-summary.json` / `analysis/manifests/disc-files.jsonl` (no `.skm` key present) |

The recursive count (4,219) exceeds the top-level count (2,808), consistent with `.skm` members
also occurring inside nested HOGs (HOGs within HOGs), per the aggregate-scan methodology documented
for other families in `analysis/formats/HOG.md`. The whole-disc count of 0 confirms `.skm` is
exclusively a packed-archive asset, never a loose disc file.

## 3. Confirmed facts

Each row is mechanically citable from a named tracked file.

| # | Fact | Citation |
| --- | --- | --- |
| C1 | `.skm` has no FourCC/magic; its first two bytes are `u8 chunk_count` then `u8 version` | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `native/src/retail/skm_container_descriptor.cpp` (`ReadU8(bytes,0)`, `ReadU8(bytes,1)`) |
| C2 | Version byte is fixed at `3` across the entire observed corpus (4,219/4,219) | `analysis/formats/asset-fingerprints.json` → `formats.skm.version_byte: {"3": 4219}`; enforced as `kFormatVersion = 3` in `native/src/retail/skm_container_descriptor.cpp` |
| C3 | Header/chunk-table layout: `header_size = align16(2 + 2*chunk_count)`, then a 2-byte record per chunk (`u8 qword_count`, `u8 secondary_count`) | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `tools/fingerprint_assets.py::fingerprint_skm`; `native/src/retail/skm_container_descriptor.cpp` (`kFixedHeaderBytes=2`, `kChunkRecordBytes=2`, `kContainerAlignment=16`) |
| C4 | Logical asset size formula: `asset_size = header_size + 16 * sum(qword_count)` | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `tools/fingerprint_assets.py::fingerprint_skm` (`logical_size = header_size + 16 * sum(qword_counts)`); `native/src/retail/skm_container_descriptor.cpp` (payload region = `qword_count * kContainerAlignment`, accumulated as `payload_offset`) |
| C5 | The formula holds for all 4,219 observed files (100%); of those, 2,732 occupy their directory span exactly and 1,487 have an all-zero tail | `analysis/formats/ASSET-RECON.md` §"SKM mesh"; `analysis/formats/asset-fingerprints.json` → `formats.skm.computed_qword_layout_span_exact: 2732`, `computed_qword_layout_span_zero_padded: 1487` |
| C6 | E-0011 (ledger, confirmed) states the passive fingerprint corpus identifies a stable structural family for SKM (among others) without publishing payloads | `analysis/evidence/ledger.jsonl` id `E-0011` |
| C7 | A native structural descriptor `omega::retail::SkmContainerDescriptor` / `InspectSkmContainer(...)` exists, enforcing: fixed 2-byte header, chunk-count range [1,61], per-chunk qword-count range [4,55], per-chunk secondary-count range [1,30], 16-byte payload/container alignment, format-version must equal 3, and (when the physical span exceeds the computed logical extent) a strict all-zero tail-padding check | `native/include/omega/retail/skm_container_descriptor.h`; `native/src/retail/skm_container_descriptor.cpp` |
| C8 | The descriptor is explicitly documented as passive/structural: "Passive structural inspection only. The descriptor retains no input span and assigns no semantics to chunk payloads or the observed secondary count." | `native/include/omega/retail/skm_container_descriptor.h` (doc comment on `InspectSkmContainer`) |
| C9 | The decoder is registered in the build (source + test) and consumed by the CLI tool's aggregate SKM structural stats (`SKM chunk count`, `SKM qword count`, `SKM logical byte count`) and pass/error counters | `CMakeLists.txt` lines referencing `native/src/retail/skm_container_descriptor.cpp` and `native/tests/skm_container_descriptor_tests.cpp`; `native/apps/omega_tool/asset_commands.cpp` (`InspectSkmContainer` call site, `SkmStructuralStats`, JSON stat emission) |
| C10 | The decoder's arithmetic is overflow-guarded end-to-end (checked `Multiply`/`Add`/`AlignUp` helpers) before any offset or size is used to index the input buffer | `native/src/retail/skm_container_descriptor.cpp` (`Multiply`, `Add`, `AlignUp` helpers and their call sites) |
| C11 | The published aggregate ranges match the decoder's enforced ranges exactly: chunk_count_range [1,61], qwords_per_chunk_range [4,55], secondary_count_per_chunk_range [1,30] | `analysis/formats/asset-fingerprints.json` → `formats.skm.chunk_count_range/qwords_per_chunk_range/secondary_count_per_chunk_range`; `native/src/retail/skm_container_descriptor.cpp` (`kMinimumChunkCount=1`, `kMaximumChunkCount=61`, `kMinimumQwordCount=4`, `kMaximumQwordCount=55`, `kMinimumSecondaryCount=1`, `kMaximumSecondaryCount=30`) |
| C12 | `.skm` is listed among the approved public suffixes with fixed corpus counts and among the fixed "mesh" category counts | `analysis/formats/FRONTEND-TOPOLOGY.md` (approved-suffix and category-count lists) |
| C13 | `.skm` is explicitly disclaimed as having no established relationship to SKA/SKL/actor/animation/runtime semantics, and no established lookup/rendering/gameplay behavior, in the SKAS scope-boundary doc | `analysis/formats/SKAS.md` (scope-exclusion list naming SKM) |

## 4. Aggregate-only facts

No semantic interpretation attached; these are corpus-wide numeric aggregates only.

- Recursive occurrence count: 4,219 (`analysis/formats/asset-fingerprints.json` → `scan.extensions[".skm"]`).
- Chunk-count range across the corpus: 1–61 (`formats.skm.chunk_count_range`).
- Qwords-per-chunk range: 4–55 (`formats.skm.qwords_per_chunk_range`).
- Secondary-count-per-chunk range: 1–30 (`formats.skm.secondary_count_per_chunk_range`).
- Span-bytes range across the corpus: 128–40,960 bytes (`formats.skm.span_bytes_range`).
- Computed qword-layout span: exact match in 2,732 files, zero-padded tail in 1,487 files, out of 4,219 total (`formats.skm.computed_qword_layout_span_exact`, `computed_qword_layout_span_zero_padded`).
- Zero-padding-byte-count range (for the 1,487 zero-padded files): 16–2,032 bytes (`formats.skm.computed_qword_layout_span_padding_bytes_range`).
- Version byte distribution: 100% (4,219/4,219) carry version byte 3 (`formats.skm.version_byte`).
- Alignment observation: the fixed header, per-chunk payload regions, and (per the native decoder) the overall physical span are all 16-byte aligned; the fingerprint script's `align_up(2 + 2*chunk_count, 16)` and the decoder's `kContainerAlignment = 16` agree (`tools/fingerprint_assets.py`; `native/src/retail/skm_container_descriptor.cpp`).

## 5. Hypotheses

Each is explicitly labeled speculative and paired with a privacy-safe confirming/refuting observation.

- **H1 — "secondary_count" is a per-chunk vertex or primitive count.** The fingerprint script's
  variable name `vertex_counts` (assigned from the same field the decoder calls
  `observed_secondary_count`) suggests this reading, but `ASSET-RECON.md` explicitly flags it as
  "semantic name not yet proven," and the native descriptor's doc comment says it "assigns no
  semantics" to that field. **Confirms/refutes with:** an aggregate cross-tabulation of
  `secondary_count` against the qword_count of the same chunk and against known VIF/GS packet-size
  constants from a public, already-tracked format grammar (no payload bytes needed) — if a fixed
  linear relation (e.g., `qword_count ≈ f(secondary_count)`) holds across all 4,219 files without
  exception, that is aggregate corroboration; a single counted-exception in the aggregate scan
  refutes it.
- **H2 — the 16-byte qword-aligned chunk payloads carry PS2 VIFcode/VU microcode or GS packet
  data (a "mesh" in the PS2-graphics sense).** This is suggested only by the informal ASSET-RECON.md
  label "SKM mesh" and the qword (16-byte) granularity, which is the native PS2 VIF/DMA transfer
  unit size across the platform generally. No tracked file inspects payload bytes. **Confirms/refutes
  with:** an aggregate, payload-byte-position histogram (e.g., "byte 0 of each chunk payload takes
  value X in N% of chunks") computed by extending the existing passive fingerprint script — still
  no per-file rows or payload dumps, just corpus-wide value-frequency counts — checked against
  published PS2 VIFcode tag-byte value ranges already appearing in this repo's tracked grammar docs
  (if any exist) or flagged UNKNOWN if none do.
- **H3 — `.skm` files pair 1:1 or N:1 with `.skl` files for a given character/weapon (the
  "SKM/SKL for characters and weapons" grouping in the roadmap).** `ASSET-RECON.md`'s research
  roadmap groups SKM and SKL together as a future work item (item 6), but no tracked evidence
  establishes a pairing rule, naming convention, or archive-adjacency fact. **Confirms/refutes
  with:** an aggregate-only same-basename-group count (already a documented technique per
  `FRONTEND-TOPOLOGY.md`'s "same-basename groups/pairs" aggregate) restricted to `.skm`/`.skl`
  stems, reporting only counts of paired vs. unpaired basenames — no filenames disclosed beyond the
  generic extensions already public.

## 6. Missing observations

- **No FourCC/magic exists to corroborate independently** — the format's self-identification rests
  entirely on the version-byte-is-3 convention, which the corpus satisfies 100% of the time but
  which is not a strong discriminator on its own. Nothing further is missing here structurally; this
  is a property of the format, not a gap in current tooling.
- **No decoding of chunk-payload internals anywhere in the tracked evidence.** Neither the fingerprint
  script nor the native descriptor reads past the per-chunk qword_count/secondary_count byte pair;
  payload bytes are only ever range-validated (bounds/overflow) or, for the zero-padded tail, checked
  to be all-zero. The privacy-safe collection that would begin to close this gap is an extension of
  `tools/fingerprint_assets.py::fingerprint_skm` that adds aggregate-only, corpus-wide statistics over
  payload byte *positions* relative to chunk start (e.g., min/max/mode of byte 0, byte 1, ... of each
  16-byte qword slot, bucketed and counted across all 4,219 files) — this stays aggregate-only
  because it reports value-frequency histograms, never a specific file's raw bytes.
  Not run under this task (no evidence-collection commands were executed here; this doc reports what
  already exists in tracked files).
- **No cross-format correlation data (SKM↔SKL, SKM↔SKAS, SKM↔SKA) exists in any tracked file.** Only
  the roadmap notes an intended future grouping (`ASSET-RECON.md` item 6); no aggregate
  same-basename-pair counts for `.skm` specifically have been computed and published anywhere in the
  tracked corpus.
- **No adversarial/fuzz test asset exists for the byte-value distributions themselves** (see §7 for
  the decoder's existing boundary-condition test coverage, which is present but distinct from
  payload-content fuzzing).

## 7. Decoder/tooling status

**Classification: `passive_descriptor_only`**

Justification:
- A native decoder exists — `omega::retail::InspectSkmContainer` in
  `native/include/omega/retail/skm_container_descriptor.h` and
  `native/src/retail/skm_container_descriptor.cpp` — that parses the chunk table, validates all
  observed range constraints (chunk count, qword count, secondary count, version byte), computes the
  chunk-table/payload byte regions and overall logical extent, and verifies zero-padded tails when
  present.
- It is explicitly self-documented as producing **no payload semantics**: "The descriptor retains no
  input span and assigns no semantics to chunk payloads or the observed secondary count"
  (`skm_container_descriptor.h` doc comment). The authoritative coverage matrix therefore calls it
  `passive_descriptor_only`: the typed descriptor validates observed regions and bounds but produces
  no mesh, vertex, bone, or other canonical asset output.
- Build/test registration: both the source and a dedicated unit test file are registered in
  `CMakeLists.txt` (lines referencing `native/src/retail/skm_container_descriptor.cpp` and
  `native/tests/skm_container_descriptor_tests.cpp`).
- Test coverage observed in `native/tests/skm_container_descriptor_tests.cpp`: a synthetic
  `.skm` builder (`MakeSkm`) generating well-formed fixtures, plus explicit checks against
  `DecodeLimits` (`asset::DecodeErrorCode::LimitExceeded`) at three distinct limit thresholds (lines
  ~242–272) — i.e., resource-boundary behavior (oversized input, item-count limit, output-size limit)
  is exercised. This is a genuine adversarial/resource-boundary test, not merely a happy-path test.
  The same test file covers every fixed-header, chunk-table, aligned-header, and payload truncation
  stage; wrong versions; out-of-range chunk/qword/secondary counts; non-aligned spans; dirty tails;
  exact and one-below input, item, and output limits; and zero scratch/depth behavior.
- Tool integration: `native/apps/omega_tool/asset_commands.cpp` calls `InspectSkmContainer` over the
  aggregate corpus and emits pass/fail counts, extent-relation counts (`exact`/`zero_padded_tail`/
  `nonzero_tail`/`exceeds_input`), and semantic-counter aggregates (`SKM chunk count`, `SKM qword
  count`, `SKM logical byte count`) as JSON — an aggregate-scanner consumer layered on top of the
  passive descriptor, consistent with the classification above.

## 8. Codex work order

Ranked, concrete, privacy-safe. None of these require reading payload bytes, filenames, or per-file
identifiers beyond what is already public in tracked docs.

1. **Resolved — no listed test gap remains.** The tracked synthetic test file covers the non-limit
   and limit branches enumerated in §7. Add cases only when the implementation grows a genuinely new
   boundary.
2. Extend `tools/fingerprint_assets.py::fingerprint_skm` to emit an aggregate-only, corpus-wide
   frequency histogram of `secondary_count` conditioned on `qword_count` (bucketed counts, not
   per-file rows) to test Hypothesis H1 without publishing any payload bytes or file identities.
3. Extend the same aggregate-scan pass to compute a same-basename-stem pairing count between `.skm`
   and `.skl` members (count of stems present in both extensions vs. stems present in only one),
   surfaced as a single aggregate number in `analysis/formats/asset-fingerprints.json`, to test
   Hypothesis H3.
4. Run the existing `tools/fingerprint_assets.py` scan against the owner's already-authorized private
   corpus (per the existing `E-0011` check command pattern in `analysis/evidence/ledger.jsonl`) to
   regenerate `asset-fingerprints.json` after any script changes from items 2–3, and record any new
   confirmed claims as new ledger entries rather than editing existing `E-####` rows in place.
5. If items 2–3 produce a clean, exception-free aggregate pattern, promote the corresponding
   hypothesis to a Confirmed fact in this dossier (with the new ledger entry as citation) rather than
   inferring semantics from the pattern alone — a clean aggregate correlation is still not proof of
   meaning without an independent citable source (e.g., a public PS2 VIF/GS grammar reference already
   tracked elsewhere in this repo).
6. Do not attempt chunk-payload byte interpretation (VIF/VU opcode decoding, vertex extraction) until
   items 1–5 are complete and a public, already-tracked grammar reference for that byte layout is
   identified — per the CLEAN-ROOM RULES, inventing such a decoder from plausibility alone is a
   regression.
