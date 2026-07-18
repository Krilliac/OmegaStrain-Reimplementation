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
build/msvc/Debug/omega_tool.exe asset-metadata-verify-tree private/extracted-disc
```

The confirmed baseline is 7,036 catalogs, 38,793 owned names, 38,899 material records, 42,631
dense name references, 91,460 passive payload pairs, 38,023 normalized T targets, 134,122
middle-to-final references, 365,840 ordered Q/P final references, and zero errors. The same pass
validates all 220,943 P/Q/T metadata records without retaining payload bytes or opaque words.

Synthetic regressions cover ownership, opaque-field immunity, nonzero trailing data, truncation,
boundary order/alignment, count/extent contradictions, string grammar and limits, fixed-record
magic/reserved bytes, dense name-reference families and bounds, P/Q/T metadata relationships,
midstream target-block normalization, duplicate/decreasing targets, middle-span families,
combined reference ordering, final suffix size, the empty family, hostile counts, and
exact/one-below input, item, and output budgets.
