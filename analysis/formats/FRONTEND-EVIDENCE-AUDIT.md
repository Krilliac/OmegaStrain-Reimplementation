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
| C5 | The bounded front-end HOG topology scanner (schema_version 3) has a frozen approved vocabulary of `.col`, `.gui`, `.hog`, `.ie`, `.pop`, `.ska`, `.skas`, `.skl`, `.skm`, `.so`, `.tbl`, `.tdx`, `.txt`, `.vag`, `.vum`; `.gui` and `.ie` map to neutral categories that echo their suffixes only and assert no menu role, layout, lookup, render, or binding semantics. The fixed sibling-pair vocabulary now includes `.gui+.ie` and aggregates matches across visited HOG directories without emitting the contributing archive, depth, basename, or member. `.fnt` remains unapproved and collapses into `other`. The scanner's JSON is aggregate-only and emits no member names, sizes tied to a member, offsets, or raw suffixes. | `tools/measure_frontend_hog_topology.py`; `analysis/formats/FRONTEND-TOPOLOGY.md` |
| C6 | Three native `Inspect*` APIs now implement bounded passive prefix/envelope hypotheses. Their code and generated fixtures confirm the project-defined accept/reject boundaries only; no tracked source records the retail provenance of the exact FNT constants, GUI tag/offsets, or IE offsets. None retains source bytes or assigns font, glyph, texture, widget, node, layout, lookup, render, menu, or recursive-payload semantics. | `native/include/omega/retail/fnt_envelope_descriptor.h`; `native/include/omega/retail/gui_envelope_descriptor.h`; `native/include/omega/retail/ie_envelope_descriptor.h`; matching files under `native/src/retail/` and `native/tests/` |
| C7 | The tracked HOG container layout is proven across 273 top-level archives and 32,351 entries: little-endian header (`tag`, `count`, `offsets_offset` = `0x14`, `names_offset` = `offsets_offset + 4*(count+1)`, `data_offset`), a monotonic-from-zero offset array, and NUL-terminated ASCII member names. It does not prove what any member payload (including a `.gui`/`.fnt`/`.ie` member) contains. | `analysis/formats/HOG.md`; `analysis/formats/hog-validation.json` |
| C8 | The static executable trace establishes a level-loader archive chain (`LOADING.HOG`, `DATA.HOG`, weapon/NPC archives) but names no `.gui`/`.fnt`/`.ie` consumer, no front-end archive load, and no front-end asset parser. | `analysis/elf/argument-loader.md`; `analysis/elf/loader-hints.md` |
| C9 | The tracked native "front end" (`FrontEndState`/`FrontEndView`, `kFrontEndLabelCells`, "project-font cells") is a project-owned synthetic menu explicitly documented as not a retail input, layout, font, or asset claim. It is not evidence about the retail `.fnt`/`.gui`/`.ie` formats. | `native/apps/openomega/front_end.h` |
| C10 | The sanitized front-end trace contract (`omega-frontend-trace-v1`) defines an anonymous `resource_class_totals` bucket named `font`; the contract states these carry no names, paths, formats, or bindings and make no claim about retail behavior. The token `font` here is a design placeholder, not `.fnt` evidence. | `docs/05-Frontend-Trace-Contract.md` |
| C11 | A bounded size-only HOG-member collector now has a frozen path-free schema and generated privacy/resource tests for default `.gui/.fnt/.ie` measurements, with optional allowlisted `.bnk/.gun`. Its implementation is evidence about the collector only; no owner-corpus output is tracked. | `tools/measure_member_structural_fingerprint.py`; `analysis/formats/MEMBER-STRUCTURAL-FINGERPRINT.md` |
| C12 | Each passive front-end descriptor has a generated-fixture-focused test executable covering its accepted prefix, truncation/malformed boundaries, hard and caller limits, deterministic results, unaligned input, and path-free typed failures. These tests validate the implemented boundary, not owner-corpus coverage or retail semantics. | `native/tests/fnt_envelope_descriptor_tests.cpp`; `native/tests/gui_envelope_descriptor_tests.cpp`; `native/tests/ie_envelope_descriptor_tests.cpp`; `CMakeLists.txt` |

---

## Tier 2 - Aggregate-only facts

Honest arithmetic and cross-counts over tracked aggregates. No semantics.

| # | Aggregate fact | Basis (tracked) |
| --- | --- | --- |
| A1 | The two scans agree at depth 0: `hog-validation.json` `total_entries` (32,351) equals the `asset-fingerprints.json` `scan.depth` bucket `"0"` (32,351). This establishes that `entry_extensions` counts the direct (depth-0) members of the 273 top-level HOGs, while `scan.extensions` counts all spans at all depths. | `analysis/formats/hog-validation.json`; `analysis/formats/asset-fingerprints.json` |
| A2 | Subtracting depth-0 from recursive totals (C1 minus C2): 56 `.gui` and 56 `.ie` occurrences are members of embedded/nested HOGs (depth >= 1), while all 3 `.fnt` occurrences are depth-0 members of top-level HOGs and none appear in nested HOGs. Combined with C3, every `.gui`/`.fnt`/`.ie` occurrence is a HOG member. This is a count relationship only; it assigns no role, archive, or meaning. | `analysis/formats/asset-fingerprints.json`; `analysis/formats/hog-validation.json` |
| A3 | Across the 46,604 non-HOG spans whose first bytes the scanner checked (`standard_compression_spans_checked`), `standard_compression_magic_hits` = 0. Because `.gui`/`.fnt`/`.ie` members are non-HOG spans, none of them begins with any of the checked whole-file magics (gzip, zip, bzip2, xz, 7zip, lz4-frame, zstd, RNC1/2, LZSS, LZ77, Yaz0). This rules out those specific whole-file wrappers only; it does not rule out internal or headerless compression. | `analysis/formats/asset-fingerprints.json`; `tools/fingerprint_assets.py` |
| A4 | `hog-validation.json` records, per archive, only a logical path, entry count, and tag. It contains no per-archive suffix breakdown, so the disc-wide `.gui`/`.fnt`/`.ie` counts cannot be attributed to any particular archive from tracked data. | `analysis/formats/hog-validation.json` |
| A5 | No tracked aggregate report publishes a per-suffix depth distribution, size bucket, alignment class, or header-field distribution for `.gui`, `.fnt`, or `.ie`. The native descriptors now freeze a few project-defined prefix hypotheses and opaque region boundaries, but those implementation facts are not retail evidence or a sanitized corpus distribution and do not establish how broadly the accepted families cover owner inputs. | `analysis/formats/asset-fingerprints.json`; `tools/fingerprint_assets.py`; the three native envelope-descriptor headers |

---

## Tier 3 - Hypotheses (unproven)


No new hypothesis is promoted here. The established evidence above remains the claim ceiling, and
this dossier authorizes no owner-corpus measurement recipe. Before any future measurement is
implemented, a separate reviewed contract must predeclare its fixed public schema, fixed minimum
cohort threshold, bounded execution and typed failures, and project-generated privacy tests.

An authorized report may contain only fixed anonymous corpus-wide totals for cohorts meeting that
threshold. Smaller cohorts must collapse to one typed suppression result. The report must not emit
raw values, signatures, payloads, owner-derived strings, paths, file, container, or archive names,
suffix-derived labels, per-file, per-container, or per-archive rows, or cross-tabulations keyed by
raw fields.

## Tier 4 - Missing observations


Unresolved structural, semantic, consumer, and validation questions remain missing observations.
This section deliberately defines no executable collection recipe. Closing any gap requires the
separately reviewed contract and suppression policy stated above; absent that contract, the gap
remains UNKNOWN.

## Lane C gate verdict

The original Lane C verdict remains **NO** for a free-standing aggregate
`measure_frontend_gui_envelopes.py` schema. The size-only collector still cannot justify arbitrary
header fields, alignment claims, equality families, or recursive structure, and no such broader
collector has been added.

The current native status is deliberately narrower and should be read as two separate gates:

- **Passive prefix/envelope implementation: YES, as a project-defined hypothesis only.** The
  generated-fixture tests make each implemented accept/reject boundary falsifiable and bounded, but
  no tracked evidence records the retail provenance of its constants. FNT reports a small prefix and
  opaque remainder; GUI and IE stop at fixed root boundaries and leave all root bytes opaque.
- **Semantic UI/font decoder: NO.** The descriptors do not establish resource identity, font or
  glyph data, widget/node trees, layout, lookup, rendering, menu state, timing, or consumer
  bindings. They are `Inspect*` boundaries returning neutral ranges and observations, not
  `GuiEnvelopeIR`, font IR, or a retail front-end loader.
- **Owner-corpus coverage: still unclaimed.** The tracked inventories establish counts, while the
  current generated fixtures establish code behavior. A reviewed sanitized coverage result and
  independent consumer evidence remain necessary before widening accepted variants or assigning
  meaning to any opaque region.

This split preserves the original conservative result: later code landed three small passive
hypothesis descriptors, but generated fixtures do not convert their constants into retail evidence,
make the earlier size-only proposal a semantic decoder, or authorize deeper parsing.

---

## Provenance

Prepared 2026-07-20 (Lane A) and refreshed after the passive front-end descriptors landed, solely
from tracked repository content. Primary sources:
`analysis/formats/asset-fingerprints.json`, `analysis/formats/hog-validation.json`,
`analysis/formats/HOG.md`, `analysis/formats/ASSET-RECON.md`,
`analysis/formats/FRONTEND-TOPOLOGY.md`, `analysis/manifests/disc-summary.json`,
`analysis/manifests/disc-files.jsonl`, `analysis/evidence/ledger.jsonl`,
`analysis/elf/argument-loader.md`, `analysis/elf/loader-hints.md`,
`tools/fingerprint_assets.py`, `tools/measure_frontend_hog_topology.py`,
`docs/05-Frontend-Trace-Contract.md`, `native/apps/openomega/front_end.h`, the FNT/GUI/IE descriptor
headers and implementations under `native/include/omega/retail/` and `native/src/retail/`, and
their generated-fixture tests under `native/tests/`.
No private input, disc image, extracted asset, emulator, or ignored analysis
output was read during this refresh. Not proven for `.gui`/`.fnt`/`.ie`: retail menu role, archive
attribution, lookup, field semantics, layout, state, timing, rendering, audio, structure beyond the
project-defined prefix hypotheses, retail provenance for those constants, owner-corpus structural
coverage, and PCSX2 equivalence.
