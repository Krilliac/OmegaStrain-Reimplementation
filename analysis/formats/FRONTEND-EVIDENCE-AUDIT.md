# Front-end asset evidence audit: `.gui`, `.fnt`, `.ie`, and HOG containers

## Purpose and method

This document audits the complete tracked repository evidence for the front-end
asset suffixes `.gui`, `.fnt`, and `.ie`, and for the HOG containers that hold
them. It exists to keep any future front-end tooling or decoder honest: every
statement below is placed in exactly one of four tiers so that structural fact,
aggregate coincidence, hypothesis, and absent evidence are never conflated.

Only tracked repository content was consulted. No private input, disc image,
extracted asset, emulator, or ignored analysis output was read. Claims cite the
exact tracked file (repository-relative path) that mechanically supports them.
Disc-internal paths, archive tags, member names, offsets, and payload bytes are
deliberately not reproduced here, because they could identify private inputs;
where a tracked file contains such rows, this audit cites the file and describes
only its aggregate shape.

Tier definitions:

- **Tier 1 - Confirmed facts.** Mechanically verifiable by reading a tracked
  file. Re-derivable by anyone with the same tracked tree.
- **Tier 2 - Aggregate-only facts.** True of tracked aggregate outputs (counts,
  histograms, cross-counts) and honest arithmetic over them, but carrying no
  semantic interpretation.
- **Tier 3 - Hypotheses.** Plausible but unproven. Each states what tracked or
  future evidence would confirm or refute it.
- **Tier 4 - Missing observations.** Evidence that does not exist in the tracked
  tree, and what privacy-safe collection would produce it.

---

## Tier 1 - Confirmed facts

Each row is verifiable by reading the cited tracked file.

| # | Confirmed fact | Tracked evidence |
| --- | --- | --- |
| C1 | The recursive asset fingerprint scan records member-suffix occurrence counts of `.gui` = 77, `.fnt` = 3, and `.ie` = 79 (field `scan.extensions`). | `analysis/formats/asset-fingerprints.json` |
| C2 | The 273-archive top-level HOG validation records direct member-suffix counts of `.gui` = 21, `.fnt` = 3, and `.ie` = 23 (field `entry_extensions`), with `total_entries` = 32,351 and zero structural errors. | `analysis/formats/hog-validation.json` |
| C3 | `.gui`, `.fnt`, and `.ie` are member-name suffixes inside HOG containers, not standalone disc files: the disc-file extension histogram lists none of them, and the per-file manifest (keys `path`, `sha256`, `size`; 448 rows) contains no `.gui`/`.fnt`/`.ie` entry. | `analysis/manifests/disc-summary.json`; `analysis/manifests/disc-files.jsonl` |
| C4 | The fingerprint scanner has no format handler for `.gui`, `.fnt`, or `.ie`; `FORMAT_HANDLERS` covers only `.tdx`, `.ska`, `.skas`, `.skm`, `.skl`, `.vag`, `.lpd`, `.par`, `.col`, `.vum`, `.vpk`. Per-span size (`span_bytes`) and any header/extent field are recorded only inside a per-format aggregate, i.e. only for handled suffixes. | `tools/fingerprint_assets.py` |
| C5 | The bounded front-end HOG topology scanner (schema_version 2) has a frozen approved vocabulary of `.col`, `.gui`, `.hog`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, `.vum`; `.gui` maps to its own category `gui`, which echoes the suffix only and asserts no menu role, layout, lookup, or render semantics. `.fnt` and `.ie` remain unapproved and collapse into the single `other` count. The scanner's JSON is aggregate-only and emits no member names, sizes tied to a member, offsets, or raw suffixes. | `tools/measure_frontend_hog_topology.py`; `analysis/formats/FRONTEND-TOPOLOGY.md` |
| C6 | No evidence-ledger entry decodes, structurally envelopes, or assigns semantics to `.gui`, `.fnt`, or `.ie`. The ledger's canonical/passive format work covers HOG, POP, SO, TDX, SKM, SKL, SKA, SKAS, VAG, LPD, PAR, COL, VUM, and VPK only. | `analysis/evidence/ledger.jsonl` |
| C7 | The tracked HOG container layout is proven across 273 top-level archives and 32,351 entries: little-endian header (`tag`, `count`, `offsets_offset` = `0x14`, `names_offset` = `offsets_offset + 4*(count+1)`, `data_offset`), a monotonic-from-zero offset array, and NUL-terminated ASCII member names. It does not prove what any member payload (including a `.gui`/`.fnt`/`.ie` member) contains. | `analysis/formats/HOG.md`; `analysis/formats/hog-validation.json` |
| C8 | The static executable trace establishes a level-loader archive chain (`LOADING.HOG`, `DATA.HOG`, weapon/NPC archives) but names no `.gui`/`.fnt`/`.ie` consumer, no front-end archive load, and no front-end asset parser. | `analysis/elf/argument-loader.md`; `analysis/elf/loader-hints.md` |
| C9 | The tracked native "front end" (`FrontEndState`/`FrontEndView`, `kFrontEndLabelCells`, "project-font cells") is a project-owned synthetic menu explicitly documented as not a retail input, layout, font, or asset claim. It is not evidence about the retail `.fnt`/`.gui`/`.ie` formats. | `native/apps/openomega/front_end.h` |
| C10 | The sanitized front-end trace contract (`omega-frontend-trace-v1`) defines an anonymous `resource_class_totals` bucket named `font`; the contract states these carry no names, paths, formats, or bindings and make no claim about retail behavior. The token `font` here is a design placeholder, not `.fnt` evidence. | `docs/05-Frontend-Trace-Contract.md` |
| C11 | A bounded size-only HOG-member collector now has a frozen path-free schema and generated privacy/resource tests for default `.gui/.fnt/.ie` measurements, with optional allowlisted `.bnk/.gun`. Its implementation is evidence about the collector only; no owner-corpus output is tracked. | `tools/measure_member_structural_fingerprint.py`; `analysis/formats/MEMBER-STRUCTURAL-FINGERPRINT.md` |

---

## Tier 2 - Aggregate-only facts

Honest arithmetic and cross-counts over tracked aggregates. No semantics.

| # | Aggregate fact | Basis (tracked) |
| --- | --- | --- |
| A1 | The two scans agree at depth 0: `hog-validation.json` `total_entries` (32,351) equals the `asset-fingerprints.json` `scan.depth` bucket `"0"` (32,351). This establishes that `entry_extensions` counts the direct (depth-0) members of the 273 top-level HOGs, while `scan.extensions` counts all spans at all depths. | `analysis/formats/hog-validation.json`; `analysis/formats/asset-fingerprints.json` |
| A2 | Subtracting depth-0 from recursive totals (C1 minus C2): 56 `.gui` and 56 `.ie` occurrences are members of embedded/nested HOGs (depth >= 1), while all 3 `.fnt` occurrences are depth-0 members of top-level HOGs and none appear in nested HOGs. Combined with C3, every `.gui`/`.fnt`/`.ie` occurrence is a HOG member. This is a count relationship only; it assigns no role, archive, or meaning. | `analysis/formats/asset-fingerprints.json`; `analysis/formats/hog-validation.json` |
| A3 | Across the 46,604 non-HOG spans whose first bytes the scanner checked (`standard_compression_spans_checked`), `standard_compression_magic_hits` = 0. Because `.gui`/`.fnt`/`.ie` members are non-HOG spans, none of them begins with any of the checked whole-file magics (gzip, zip, bzip2, xz, 7zip, lz4-frame, zstd, RNC1/2, LZSS, LZ77, Yaz0). This rules out those specific whole-file wrappers only; it does not rule out internal or headerless compression. | `analysis/formats/asset-fingerprints.json`; `tools/fingerprint_assets.py` |
| A4 | `hog-validation.json` records, per archive, only a logical path, entry count, and tag. It contains no per-archive suffix breakdown, so the disc-wide `.gui`/`.fnt`/`.ie` counts cannot be attributed to any particular archive from tracked data. | `analysis/formats/hog-validation.json` |
| A5 | No tracked aggregate reports a per-suffix depth distribution, size bucket, alignment class, or header-field value for `.gui`, `.fnt`, or `.ie`. The only tracked per-span facts for these suffixes are: the suffix itself, inclusion in the global depth histogram, and inclusion in the global compression check (A3). | `analysis/formats/asset-fingerprints.json`; `tools/fingerprint_assets.py` |

---

## Tier 3 - Hypotheses (unproven)

Each is plausible but unsupported by tracked structural evidence. None may be
treated as fact or wired into any tool or decoder.

| # | Hypothesis | Confirm / refute with |
| --- | --- | --- |
| H1 | `.gui`, `.fnt`, and `.ie` are front-end / menu presentation formats (layout, font, and interface-element data respectively). | Refuted or confirmed only by a privacy-safe aggregate of their internal structure plus a behavioral oracle showing a front-end consumer reads them. No tracked evidence currently bears on it; suffix spelling is not evidence. |
| H2 | The 3 `.fnt` members reside in a single "fonts" front-end archive. | A per-archive suffix breakdown (not currently produced) mapping the `.fnt` members to an archive. Tracked archive names are merely suggestive (A4); attribution from a name is plausibility, not proof. |
| H3 | `.gui`/`.fnt`/`.ie` members share a common header or FourCC the way TDX/VAG/COL/VUM do. | Extending a bounded scanner to read the leading words of these members and reporting whether a constant magic/version appears across the corpus. Until then, unknown. |
| H4 | The nested-HOG `.gui`/`.ie` members (A2) are language- or locale-partitioned front-end content. | A privacy-safe measurement of which container depths and sibling suffix sets they co-occur with; not derivable from current aggregates. |
| H5 | The anonymous `font` resource class in the trace contract (C10) and any future `.fnt` decode describe the same underlying asset. | A behavioral trace that both passes `omega-frontend-trace-v1` and, separately, a structural `.fnt` aggregate; correlating them is out of scope of any current tracked evidence. |

---

## Tier 4 - Missing observations

What the tracked tree does not contain, and the privacy-safe collection that
would produce it. All collection runs against ignored owner input and publishes
only fixed-schema aggregates, never member names, bytes, offsets, or paths.

| # | Missing observation | Privacy-safe collection that would produce it |
| --- | --- | --- |
| M1 | An owner-corpus size aggregate for `.gui`/`.fnt`/`.ie`: count, minimum, maximum, distinct-size count, and size GCD. | Run the implemented bounded collector privately and review only its fixed-schema output. The data does not exist in the tracked tree; size GCD is not address alignment. |
| M2 | Whether these members carry a constant leading magic/version word, and whether any predeclared header position is uniformly zero/nonzero across the corpus. | A bounded scanner reading only a fixed, small header window and reporting per-position zero/nonzero tallies and observed constant-word histograms - never the bytes themselves. |
| M3 | A per-archive suffix breakdown that would let `.gui`/`.fnt`/`.ie` counts be attributed to specific containers (needed to test H2/H4). | Extending the HOG validation aggregate to emit a per-archive category histogram (counts only, still no member names). |
| M4 | Any evidence that the retail front end (or its loader) reads a `.gui`/`.fnt`/`.ie` member: which archive is mounted, in what order, and which suffixes are touched. | A static loader trace of the front-end presentation path (the technique already used for the level loader in `analysis/elf/argument-loader.md`), reporting logical path templates and archive order only. |
| M5 | A behavioral oracle observation that a front-end action consumes one of these resources. | A sanitized `omega-frontend-trace-v1` capture (`docs/05-Frontend-Trace-Contract.md`) with a byte-identical repeat, publishing only anonymous aggregate resource-class touches. |

---

## Lane C gate verdict

**Verdict: NO.** The tracked evidence is not sufficient to freeze a
non-misleading fixed aggregate schema for a bounded
`measure_frontend_gui_envelopes.py` tool. A conservative no-decoder result is the
correct outcome here; a plausible invented envelope would be a regression per the
campaign brief.

Reasoning, mapped to the candidate aggregate dimensions the brief enumerates:

- **Candidate count** - *supported, but redundant.* Existence and occurrence
  counts of `.gui` (and `.fnt`/`.ie`) are tracked (C1, C2, A2). This is the only
  dimension with tracked grounding, and `.gui` is already reported as its own
  category by the front-end HOG topology scanner (C5, schema_version 2; Lane B
  promoted it out of `other`). A separate envelope tool adds nothing here.
- **Fixed size buckets** - *not supported.* No tracked file records any
  `.gui`/`.fnt`/`.ie` member size. `fingerprint_assets.py` records `span_bytes`
  only for handled suffixes, and there is no handler for these three (C4, A5).
  The topology scanner's size buckets for `other` are not published to any
  tracked output and mix all non-approved suffixes together (C5).
- **Alignment families** - *not supported.* Alignment requires member sizes or
  offsets, which are not tracked for these suffixes (A5).
- **Zero/nonzero classes at predeclared positions** - *not supported.* No
  tracked evidence reads any byte of a `.gui`/`.fnt`/`.ie` member; predeclaring a
  position would invent structure (A5, C4). The only byte-level fact is the
  aggregate negative that none begins with a listed compression magic (A3),
  which supports no positional schema.
- **Equality / monotonicity families** - *not supported.* These require reading
  multiple header words per member across the corpus; nothing tracked does so
  (A5, C4).

**What is missing to reach a YES.** The safe size-only collector now exists and is synthetically
verified, but no owner-corpus result is tracked. Even a uniform size family would not establish an
envelope grammar. A YES requires a sanitized result plus a falsifiable header/envelope observation,
generated malformed boundaries, and independent consumer evidence (M1, M2). The rejected
`measure_frontend_gui_envelopes.py` concept is distinct from the implemented size-only fingerprint
collector. Only after those gates should a native `GuiEnvelopeIR`/decoder be considered.

---

## Provenance

Prepared 2026-07-20 (Lane A) solely from tracked repository content on branch
`claude/frontend-asset-decoding`. Primary sources:
`analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json`,
`analysis/formats/HOG.md`, `analysis/formats/ASSET-RECON.md`,
`analysis/formats/FRONTEND-TOPOLOGY.md`, `analysis/manifests/disc-summary.json`,
`analysis/manifests/disc-files.jsonl`, `analysis/evidence/ledger.jsonl`,
`analysis/elf/argument-loader.md`, `analysis/elf/loader-hints.md`,
`tools/fingerprint_assets.py`, `tools/measure_frontend_hog_topology.py`,
`docs/05-Frontend-Trace-Contract.md`, and `native/apps/openomega/front_end.h`.
No private input, disc image, extracted asset, emulator, or ignored analysis
output was read. Not proven for `.gui`/`.fnt`/`.ie`: retail menu role, archive
attribution, lookup, field semantics, layout, state, timing, rendering, audio,
internal structure, owner-corpus structural coverage, and PCSX2 equivalence.
