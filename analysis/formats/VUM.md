# VUM material catalog and render-payload boundary contract

## Scope and evidence state

This document records aggregate structural contracts independently reproduced across the owned
NTSC-U corpus. It contains no retail payload bytes, names, paths, code, or executable
instructions. Native adapters expose the proven material/name relationship plus a separate
retail-only passive render-payload descriptor. Geometry packets, vertex attributes, topology,
transforms, material parameters, texture binding, and the optional trailing region remain
unassigned.

State: **confirmed** for all 7,036 observed VUM spans.

## Header and primary boundaries

All integers are little-endian and offsets are relative to the VUM span. Every span starts with
`VUMS`, has a 92-byte header, and is 16-byte aligned.

| Header offset | Confirmed structural role |
| ---: | --- |
| `0x14` | number of NUL-terminated names in the preamble |
| `0x18` | number of fixed 92-byte `MTRL` records |
| `0x50` | end of the name preamble and start of the fixed record table |
| `0x54` | end of the fixed record table |
| `0x58` | end of the primary payload |

The three endpoints are ordered. The first two are four-byte aligned and the primary endpoint is
16-byte aligned. The fixed table relation is exact for the complete corpus:

```text
endpoint_0x54 - endpoint_0x50 == word_0x18 * 92
```

The primary endpoint equals the input span in 6,989 files. Forty-seven files have an additional
nonzero region, so `0x58` is not treated as a universal file length. The header words at `0x04`
through `0x1C` other than the two counts above, and the three homogeneous float vectors at
`0x20` through `0x4F`, remain opaque to the canonical adapter.

## Name preamble and fixed records

The region from byte 92 through the `0x50` endpoint has this confirmed grammar:

```text
u32 payload_boundary_a       # absolute, 16-byte aligned
u32 payload_boundary_b       # absolute, 16-byte aligned
u32 zero
u32 zero
u32 zero
ASCII/NUL name region
```

Both payload boundaries are ordered, begin no earlier than the end of the fixed record table, and
do not exceed the primary endpoint. The name region ends in NUL; every non-NUL byte is printable
ASCII; and its nonempty NUL-separated string count exactly equals the word at `0x14`.

The corpus contains 38,793 names and 38,899 fixed records. Every record starts with `MTRL`.
Within each 92-byte record, bytes `0x10` through `0x37` are zero. Words `0x38`, `0x3C`, and `0x40`
are dense name-reference slots; `0x58` declares that exactly one, two, or three are active. Every
active reference is in range, and every inactive reference uses `0xFFFFFFFF`. The corresponding
words at `0x48`, `0x4C`, and `0x50` use a small correlated code family, but its meaning remains
unassigned and it is omitted from canonical output. Words `0x44` and `0x54` are always
`0xFFFFFFFF`. Across the corpus, the 38,899 records contain 42,631 active name references.

The two scalar words at `0x08` and `0x0C` are finite floats but remain opaque. In particular, the
native adapter does not label a referenced name as a texture, shader, material layer, or external
asset.

## Post-material metadata records

Let `n = (word_0x0C - 1) / 2` and `m = word_0x10`; the first word is odd in every file. The region
from the fixed material-table end through payload boundary A contains exactly `2n + m` records of
16 bytes followed by zero to the next 16-byte boundary. The aggregate corpus contains 220,943 such
records in three structurally distinct families:

- 91,460 P records contain three aligned, strictly in-range references into the final payload;
- 91,460 Q records contain one reference into the middle payload and one into the final payload;
  and
- 38,023 T records contain one forward reference to a Q record.

Per file there are exactly `n` P, `n` Q, and `m` T records. T records form one contiguous block;
removing that block leaves exact `Q,P,Q,P,...` ordering. Q middle-payload references start at
payload boundary A, increase strictly, and partition that region into subspans of 16, 256, 480, or
704 bytes. Every T record targets a distinct forward Q record, target order is strictly
increasing, and no Q is targeted more than once.

Each Q/P pair contributes four strictly increasing references into the final payload, in the
observed order Q fourth word, then P first, third, and fourth words. The ordering continues across
adjacent pairs, so all 365,840 such references are unique within their source asset. The final
reference leaves exactly 4, 8, 12, or 16 bytes before the primary endpoint. These are validated as
passive references and relationships only; their rendering and target meanings remain unassigned.

## Middle- and final-payload evidence

Across all 91,460 Q partitions, middle-payload span sizes are distributed as follows:

| Bytes | Count | Structural group value |
| ---: | ---: | ---: |
| 16 | 48,798 | compact family |
| 256 | 35,408 | 1 |
| 480 | 3,556 | 2 |
| 704 | 3,698 | 3 |

Every grouped size follows `32 + 224 * group`, for group values one through three. In every
nonempty file, the set of those group values exactly equals the set of active-name counts in its
MTRL records. This is a file-level structural correlation only; no per-pair material binding has
been established.

Compact spans contain one final-payload reference at relative byte 4. Grouped spans contain two
at relative bytes 116 and 244. All 134,122 are 16-byte aligned, strictly inside the final payload,
unique within their file, and disjoint from the four Q/P references. The complete per-pair order
is `compact-reference, Q, P0, P2, P3` for compact spans and
`grouped-reference-0, Q, P0, grouped-reference-1, P2, P3` for grouped spans. This combined order
continues strictly across pairs, and its first reference is always 16 bytes into the final region.

Q/P references partition the remainder into variable-width sections, but their contents remain
unassigned. Q span size does not determine the matching final-region envelope, and Q/P opaque
words do not form simple material-index ranges.

## Canonical adapter

`DecodeVumMaterialCatalog` returns an owned `MaterialCatalogIR`: source-order strings plus the one
to three dense, source-order name indices for each source-order material record. It retains no
input span, retail byte offset, opaque record word, render packet, console instruction, or
renderer/GPU object. Replacing
or destroying the source bytes after decoding cannot affect the result, and the stateless decoder
is reentrant on worker threads.

The decoder debits the full input span once, validates all counted arithmetic and boundaries, and
preflights the exact logical output and root/name/material item counts before allocation. Each
name is independently bounded by `maximum_string_bytes`. Parsing needs no dynamic scratch
storage. Unsupported reserved layouts and characters fail closed.

## Passive render-payload descriptor

`InspectVumRenderPayload` returns an owned retail-only `VumRenderPayloadDescriptor`. It preserves
the three bounded regions, source-order Q/P pairs, each pair's middle-payload byte count and
neutral group count, all middle-to-final references, the four Q/P final-region-relative
references, and source-order T targets normalized to pair ordinals.

It retains no input span or payload byte and exposes no packet word, opcode, register,
microprogram, renderer object, vertex, index, triangle, draw, or material assignment. The
descriptor remains under `omega::retail`; renderer and simulation code must not include it. It is
evidence scaffolding for the next clean-room proof, not canonical asset IR. The inspector
preflights source records, normalized relationships, output vectors, and arithmetic before
allocation and needs zero dynamic scratch.

## Privacy-safe consumer-read trace evidence gate

`tools/validate_vum_read_trace.py` is the acceptance gate for any future dynamic claim about the
retail VUM consumer. The first bounded capture pair now passes that gate: one complete 120-frame
pair selected one runtime copy, strict validation accepted both validated reports, and the repeat
was byte-identical. Each accepted report contains two EE-read aggregate rows, two anonymous-site
rows, and zero VIF1 chunk rows. These counts confirm only a deterministic bounded consumer-read
observation; they do not identify a consumed structure or assign runtime meaning.

An independently guarded second ranked trial reproduced those aggregates exactly: one capture with
four accepted aggregate rows, split evenly between EE-read and anonymous-site rows, zero VIF1 rows,
and zero aggregate-count deltas. Across both trials, the EE-read rows remain confined to the
already-opaque homogeneous header-vector block. No accepted row reaches the scalar/count words,
endpoint words, record preamble, name/material records, metadata, payload, VIF, or tail. The
reproduced result is therefore header-only evidence; it does not rule out copied buffers or
consumption outside the bounded observation window and does not establish runtime semantics.

A publishable trace report may expose only sanitized aggregates:

- VUM-relative read offsets, widths, and counts;
- anonymous instrumentation-site ranges and counts, without site identities;
- VIF source-relative ranges and output counts; and
- lifecycle status needed to prove that capture began and ended cleanly.

The report schema forbids program counters; absolute, process, or emulated-RAM addresses; CRCs or
hashes; source paths or names; payload bytes or decoded payload data; and instructions, opcodes,
registers, or other executable detail. Fields outside the allow-list above are invalid rather than
silently ignored.

Validation receives the expected VUM span size privately; that size is a bound, not a publishable
asset identifier. The validator enforces the exact schema, accepted lifecycle status, strict
in-span offset and width arithmetic, deterministic ordering, unique keyed rows, and agreement
between detail counts and their summaries. It can also compare a second report byte-for-byte, so
two captures from the same save state must be identical before determinism is claimed.

The isolated runner also passed its post-run containment audit: no retained runtime copy,
executable surface, reparse point, owner-input copy, or emulator/build process remained.

The next pass is evidence-driven interpretation rather than capture repair. Treat the accepted
pair as a structural baseline only. Add controlled, independently repeatable pairs that change one
research condition at a time, compare only sanitized relative ranges and counts, and promote a
relationship only after cross-capture stability and independent corroboration. Keep sites
anonymous and bounds private. Do not infer geometry, topology, vertex, material, packet, draw,
placement, visibility, or gameplay semantics from row counts, offsets, widths, or the absence of
VIF1 chunks.

## Render geometry gate

The two payload boundaries split the post-record primary region into three bounded, nonnegative,
16-byte-aligned sections. The metadata-record region now has the exact contract above, but no
all-corpus header-only formula determines the middle or final payload byte length. Whole-region
float and fixed-stride tests also do not support interpreting either payload as a flat vertex table.

The flat-position hypothesis fails decisively: none of 884,718 middle-region 16-byte candidates
has a homogeneous position word. No tested final-region fixed-stride family covers the corpus;
the best 16-byte candidate reaches exact header extrema in only 5,411 of 6,517 nonempty files.
The paired COL mesh is not a substitute either: only two of 4,320 nonempty COL/VUM siblings have
equal bounds, and only 105 of 949,762 COL vertices recur in any paired final payload.

A future native render adapter will emit only owned positions and triangle indices after packet
topology and position semantics are independently proven. It will not expose or execute MIPS,
VU, VIF, packet-command, register, opcode, or microprogram representations. Unknown packet
families will fail as unsupported instead of entering canonical IR.

## Native validation

The aggregate verifier streams one asset at a time, discards each decoded catalog after updating
sanitized counters, and never prints source paths or names:

```powershell
build/msvc/products/sdk/Debug/omega_tool.exe asset-metadata-verify-tree private/extracted-disc
```

The level-composition verifier separately exercises `GameDataService` manifest-to-cell traversal
and its shared per-level decode budget. It emits only level, cell, catalog, name, material,
reference, and error counts; diagnostics contain no level codes, paths, member names, hashes,
payloads, or inferred roles:

```powershell
build/msvc/products/sdk/Debug/omega_tool.exe level-material-catalogs-verify-tree private/extracted-disc
```

The confirmed baseline is 7,036 catalogs, 38,793 owned names, 38,899 material records, 42,631
dense name references, 91,460 passive payload pairs, 38,023 normalized T targets, 134,122
middle-to-final references, 365,840 ordered Q/P final references, and zero errors. The same pass
validates all 220,943 P/Q/T metadata records without retaining payload bytes or opaque words.
The separate level-composition pass accepts all 18 levels and all 5,351 manifest-referenced
catalogs with zero errors, preserving manifest order and producing 34,267 owned names, 34,589
material records, and 37,893 dense name references. Those totals confirm only bounded service
orchestration and ownership; they assign no name role, asset binding, placement, or render meaning.

Synthetic regressions cover ownership, opaque-field immunity, nonzero trailing data, truncation,
boundary order/alignment, count/extent contradictions, string grammar and limits, fixed-record
magic/reserved bytes, dense name-reference families and bounds, P/Q/T metadata relationships,
midstream target-block normalization, duplicate/decreasing targets, middle-span families,
combined reference ordering, final suffix size, the empty family, hostile counts, and
exact/one-below input, item, and output budgets.

## Full normalized member-name equality restricted to `.TDX`

`tools/measure_level_material_texture_name_candidates.py` is a bounded analysis-only comparison
between VUM name occurrences and the two explicit level-scoped direct TDX locator classes. Its
accepted population has exact aggregate level, cell, catalog, name, material, reference, container,
and locator cardinality parity with the native `GameDataService` and `LevelTextureStore` verifiers.
That is validation-scope parity only: the Python experiment does not execute a native lookup and
does not establish that retail code performs one.

The sole eligible branch compares complete normalized member strings, after the existing bounded
ASCII case and separator normalization, and requires the VUM string to end exactly in `.TDX`.
The experiment does not compare basenames or stems, add or remove an extension, substitute a suffix,
fuzzy-match, search nested containers, or test any alias family. An unsafe name is
`invalid_member_candidate`; a valid name without terminal `.TDX` is `non_tdx_suffix`; and
`unmatched` means only that an eligible full-string exact candidate was absent. None of those
classes excludes a relationship through an untested alias or representation.

A complete normalized string present in exactly one locator class would be classified
`unique_primary` or `unique_map`; presence in both would be `ambiguous_cross_class`, with no class
priority. These are candidate classes, not asset identity or binding. Dense MTRL references merely
inherit the class of the referenced name occurrence, and overlapping material-record flags summarize
those inherited classes without assigning a texture to a material.

The confirmed byte-identical rerun reports:

| Class | VUM name occurrences | Dense name-reference occurrences |
| --- | ---: | ---: |
| Invalid normalized member candidate | 0 | 0 |
| Non-`.TDX` terminal suffix | 34,267 | 37,893 |
| Eligible full-string exact candidate unmatched | 0 | 0 |
| Unique primary-class candidate | 0 | 0 |
| Unique map-class candidate | 0 | 0 |
| Ambiguous across both classes | 0 | 0 |

All 34,589 material records have at least one ineligible reference; the all-unique, any-unique,
any-unmatched, and any-ambiguous flags are all zero. All 5,801 class-qualified locator occurrences
are unreached; none is reached uniquely or only ambiguously. The zero `unmatched` count does not
mean that names matched: no name was eligible to enter that branch because every accepted name
lacked terminal `.TDX`.

The run discovers and atomically scans all 18 levels, traverses 5,351 manifest cell occurrences,
validates 5,351 VUM catalog occurrences with 34,267 name occurrences, 34,589 material records, and
37,893 dense references, and compares against 5,801 class-qualified locator occurrences from 36
exact sibling containers, with zero errors. Independent maxima are 140 names per catalog, 140
materials per catalog, 142 dense references per catalog, 730 locators per level, and zero candidate
locators per name.

This exact-equality negative result does not prove that catalog names are texture names, that a
material record binds a texture, that retail code performs this lookup, that either container class
has priority, or that a locator binds a cell, mesh, draw, placement, visibility, or renderer
resource. It does not exclude implicit extensions, extension removal, aliases, basenames, stems,
indirection, alternate representations, or other lookup mechanisms, and it does not justify runtime
integration. The fixed report emits no paths, archive/member/catalog names, hashes, offsets, payload
bytes, per-level rows, locator identities, or inferred semantics. Private strings exist only in
bounded per-level working sets and are discarded before the next level.

```powershell
python -B tools/measure_level_material_texture_name_candidates.py private/extracted-disc
```

## Exact-first one-terminal-extension candidate family

The scanner's separate `one-terminal-extension` family is an additive schema-version-2 experiment.
The default `exact-terminal-tdx` family and its schema-version-1 output remain unchanged and are
guarded byte-for-byte for both success and sanitized configuration failure. The new family retains
the same bounded manifest-scoped VUM population and the same two direct normalized `.TDX` locator
classes; it does not admit non-`.TDX` container members or traverse another container.

Both complete strings are safely normalized before comparison. Full normalized equality is tested
first and, when present, retains exact provenance. Otherwise the policy independently removes at
most one syntactic extension from the final component of the VUM string and the direct `.TDX`
locator string, then performs one more exact full-string comparison. A syntactic extension requires
a dot after the first character of the final component and at least one character after that dot.
Consequently `.HIDDEN`, `NAME.`, and an extensionless component remain unchanged, while `A.B.C`
becomes `A.B`. Directory components are never removed, and no second extension is stripped.

Candidate results retain the original class-qualified locator, not the extension-elided comparison
key. Exact candidates take precedence over extension-elided candidates. A valid normalized name
with no candidate after the one permitted transformation is
`unmatched_after_one_terminal_extension`; unsafe normalization remains
`invalid_member_candidate`. Unique and ambiguous statuses separately record exact and
extension-elided provenance without assigning either container class a priority.

The two byte-identical confirmed passes report:

| Class | VUM name occurrences | Dense name-reference occurrences |
| --- | ---: | ---: |
| Invalid normalized member candidate | 0 | 0 |
| Unmatched after one terminal extension | 0 | 0 |
| Exact unique primary-class candidate | 0 | 0 |
| Exact unique map-class candidate | 0 | 0 |
| Exact ambiguous cross-class candidate | 0 | 0 |
| Extension-elided unique primary-class candidate | 34,267 | 37,893 |
| Extension-elided unique map-class candidate | 0 | 0 |
| Extension-elided ambiguous cross-class candidate | 0 | 0 |

All 34,589 material records have all references unique, have at least one unique candidate, and have
at least one extension-elided candidate. The any-unmatched, any-ambiguous, and any-ineligible flags
are zero. These flags only inherit lexical classes from dense MTRL-to-name indices; they do not
assign a texture to a material record.

Coverage is counted over original class-qualified direct `.TDX` locator occurrences. None is reached
only through full-string exact equality, 5,690 are reached only through extension elision, none is
reached through both routes, and 111 remain unreached. Repeated candidate names do not multiply an
original locator occurrence. The run atomically scans all 18 levels with zero errors and retains the
same 5,351 manifest cells, 5,351 VUM catalogs, 34,267 names, 34,589 material records, 37,893 dense
references, 36 direct containers, and 5,801 original locator occurrences as the exact family.
Independent maxima are 140 names per catalog, 140 materials per catalog, 142 dense references per
catalog, 730 locators per level, and one candidate locator per name.

This result does not establish that one-terminal-extension removal is a retail alias rule. Retail
name lookup, retail material-record consumption, and retail extension elision remain unobserved. It
does not prove that catalog names are texture names, that material records bind textures, that either
container class has priority, that a locator binds a cell, mesh, draw, placement, visibility, or
renderer resource, or that runtime integration is justified. It also does not test basename, stem,
substring, fuzzy, suffix-family, repeated-extension, indirection, alternate-representation, or other
alias behavior. The fixed report emits no paths, archive/member/catalog names, hashes, offsets,
payload bytes, per-level rows, transformed keys, locator identities, or inferred semantics. Private
strings remain bounded per-level working data and are discarded before the next level.

```powershell
python -B tools/measure_level_material_texture_name_candidates.py private/extracted-disc `
  --candidate-family one-terminal-extension
```
