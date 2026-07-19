# TDX texture storage

## Scope

This contract covers version-5 TDX block, transfer-plane, and palette storage. The native adapter
returns fully owned bytes in source order. It does not claim display-ready pixels, execute console
instructions, or retain retail offsets and input views.

The following remain deliberately unassigned: block purpose, mip/slice/frame meaning, texel row
order, swizzle, four-bit nibble order, indexed palette permutation, channel names, color space,
premultiplication, alpha scaling, sampler state, and GPU upload layout.

## Header and counted extent

The 64-byte prefix begins with these confirmed little-endian values:

```text
u16 version
u16 flags
u16 width
u16 height
u16 sample_bit_depth
u16 sample_format_code
u16 width_unit_word
u16 storage_unit_word
...
u16 block_count                 # +0x22
u16 primary_plane_count         # +0x24
u16 palette_plane_count         # +0x26
...
u32 block_stride                # +0x38
```

Every counted block has the same stride. The complete region is therefore
`64 + block_count * block_stride`, not `64 + block_stride`. This correction makes 24 formerly
opaque tails ordinary additional blocks. The physical corpus has 11,277 exact counted extents,
3,909 all-zero tails, no nonzero tails, and 62 spans ending inside the last primary plane.
Block stride is not uniformly 16-byte aligned: 23 occurrences use stride 2,376 and carry an
eight-byte zero container tail.

The sample families are 4-bit indexed (`0x14`), 8-bit indexed (`0x13`), packed 24-bit (`0x01`),
and packed 32-bit (`0x00`). These header sample families are distinct from the transfer-element
format of each stored plane. In particular, indexed textures can carry storage rectangles whose
transfer elements are packed 32-bit words; those bytes must not be mistaken for RGBA pixels.

## Block storage

Each block begins with bounded relative tables for one to four primary objects and either one
palette object for indexed samples or none for direct samples. A transfer object is 0x60 bytes.
Only the independently established rectangle dimensions, transfer-element family, and data target
survive normalization.

For the complete corpus family:

- every object and data target is bounded by its block;
- all 17,898 physically complete primary planes exactly match their encoded rectangle size;
- all primary targets are unique, strictly ordered, and fill the meaningful suffix of a block;
- all 15,129 physically complete indexed blocks have one source-order four-byte palette plane;
- four-bit palettes contain 16 entries and an all-zero remainder in their 1,024-byte slot;
- eight-bit palettes contain 256 entries and fill that slot; and
- direct blocks contain no palette plane.

`TextureStorageIR` preserves top-level sample encoding, block order, plane order, transfer-element
encoding, rectangle dimensions, primary bytes, and four source bytes per palette entry. The fourth
source byte is copied unchanged, including observed values above 128. No RGBA naming or alpha
conversion is part of this decoder.

## Proven implicit-zero suffix family

Sixty-two TDX spans end 16, 32, 64, or 256 bytes before their sole primary plane ends. An
aggregate-only duplicate-prefix proof reduces them to 16 unique prefixes. Every prefix has a
complete corpus twin with an identical supplied prefix and an all-zero missing suffix; there are
no unmatched prefixes and no nonzero-suffix twins.

The semantic adapter therefore normalizes only the exact observed single-block/single-plane
signatures. All tables, objects, and any indexed palette must be complete; only the final primary
plane may cross the input end; its calculated end must equal the declared stride; and the shortage
must match the exact observed header, stride, base, object, data-target, transfer-rectangle,
palette, and aligned-size allowlist. The decoder synthesizes exactly those 4,112 proven zero bytes.
Every other short form is rejected. Reconstruction provenance stays in diagnostic aggregate counts
rather than renderer-neutral IR.

Reproduce the privacy-safe proof without emitting paths, names, hashes, or payload bytes:

```powershell
python -B tools/prove_tdx_zero_suffix.py private/extracted-disc
```

## Native measured decode usage

`DecodeTdxTextureStorageMeasured` returns the same owned `TextureStorageIR` as the compatibility
decoder together with the exact standalone operation-budget counters used by that decode.
`decoded_items` counts the texture root, blocks, primary planes, present palette objects, and
palette entries. `logical_output_bytes` counts the compiled-ABI root, block, and plane objects,
owned plane payloads, and one four-byte array per palette entry. These are logical budget values,
not allocator, vector-capacity, resident-memory, or process-memory measurements.

The aggregate topology scanner does not execute this native API or observe C++ object sizes.
Composed runtime Open/Load usage is measured separately by the native `LevelTextureStore` verifier
documented below; structural Python proxies must not be substituted for those runtime values.

## Common-archive containment result

The bounded aggregate scanner walks each runtime level's complete common DATA.HOG graph, including
direct members and every recursively contained HOG, then records the POP-designated cell locators
again as manifest occurrences. Across all 18 levels, it accepts 5,351 manifest cell occurrences and
5,413 scanned archive-directory occurrences, including 44 deeper-container occurrences, with zero
malformed inputs, normalized collisions, or other errors. It finds zero normalized `.TDX`-suffixed
members; all 5,351 manifest cell occurrences are likewise zero-TDX within that extension-bounded
scope. Independent structural maxima are 48,271,360 Open input bytes, 2,745 archive entries plus
locator items, and one archive edge. They are not asserted to co-occur and are not native ABI
budgets.

This is narrow negative containment evidence only. It does not exclude texture data embedded under
another member suffix or representation, identify an owner, connect any texture to a cell, name,
material, mesh, or draw, or justify recursive texture discovery elsewhere. The scanner's fixed
schema explicitly excludes sibling texture containers so its result cannot be misread as a
whole-level texture inventory.

```powershell
python -B tools/measure_level_tdx_topology.py private/extracted-disc
```

## Explicit sibling-container result

A separate bounded scanner measures only two explicit sibling container classes present beside
each runtime level's common files. It treats each as one exact top-level HOG, validates the complete
normalized directory before filtering, and inspects direct TDX members only. Across all 18 levels
it accepts all 36 containers and all 5,801 directory members with zero errors, malformed textures,
normalized collisions, or non-TDX entries. The primary class contributes 5,765 direct TDX
occurrences and the map class contributes 36. Independent single-container maxima are 728 entries,
3,073,600 bytes, and 1,456 structural Open directory-plus-locator items at archive depth zero; they
are not asserted to co-occur and are not native ABI budgets.

This establishes direct containment in the two scanned sibling classes, not retail ownership,
necessity, priority, or a material, cell, mesh, name, draw, placement, visibility, mip, or render
binding. The native store's level-scoped locator inventory is an explicit engineering contract, not
a claim that the scan recovered a retail ownership relationship. Nested containers are deliberately
not traversed, class order conveys no priority, and the 5,801 occurrences are a level-scope inventory
rather than a whole-disc ownership claim.

```powershell
python -B tools/measure_level_texture_container_topology.py private/extracted-disc
```

## Cross-format exact-name candidate experiment

The separate VUM-name experiment uses exactly the two direct normalized `.TDX` locator classes
measured above. Each locator occurrence is class-qualified, so the same complete normalized member
string in both containers would contribute two locator occurrences and an ambiguous cross-class
candidate rather than a priority decision.

Only exact equality of complete normalized member strings, restricted to VUM names ending `.TDX`,
is tested. No basename, stem, extensionless, suffix-substitution, nested-container, fuzzy, or other
alias relationship is included. The confirmed rerun reaches zero locator occurrences uniquely,
zero only ambiguously, and leaves all 5,801 unreached because all 34,267 accepted VUM name
occurrences lack terminal `.TDX` and never enter the lookup branch. This is narrow lexical coverage,
not retail ownership, material binding, source priority, or runtime lookup evidence. Detailed
name/reference classification and privacy constraints are documented in `analysis/formats/VUM.md`.

## Display-layout hypothesis scoring

`score_tdx_layout_hypotheses.py` is a bounded, aggregate-only experiment for the remaining indexed
display-layout questions. It stratifies every primary plane by both header bit depth and that
plane's transfer-element code. For direct indexed transfer rectangles, it compares only these
explicit source-order hypotheses:

- four-bit `0x14`: low-nibble-first versus high-nibble-first, with an identity palette; and
- eight-bit `0x13`: identity palette lookup versus the bit-3/bit-4 lookup permutation.

The score is the summed absolute delta across all four opaque source palette bytes between
horizontal and vertical neighbors; lower means only that one hypothesis produces smoother local
source-byte adjacency. It does not assign RGB or alpha channel names and is not a semantic
assignment. Each direct plane uses its own transfer-object dimensions, so later source-order planes
are not forced through the top-level dimensions or assigned a mip rank.

Indexed planes with transfer code `0x00` are counted in separate `4/0x00` and `8/0x00` families but
are deliberately not scored. Their object rectangle describes a packed 32-bit upload for visibly
pre-swizzled data, not display texel dimensions. Treating upload-order adjacency as display
coherence would produce a misleading comparison. Palette permutation and nibble order also remain
hypotheses until corroborated independently.

Aggregate delta sums naturally weight larger planes, so the report also includes per-plane wins
and ties. Proven implicit-zero reconstructions are counted separately because synthesized zeros can
affect adjacency. The metric is content-dependent and cannot by itself validate palette order,
nibble order, or the purpose of later planes.

The tool streams HOG and standalone TDX spans, rejects malformed or over-budget input, and emits no
paths, archive names, hashes, asset-specific dimensions, payload data, or samples:

```powershell
python -B tools/score_tdx_layout_hypotheses.py private/extracted-disc
```

The owned-corpus run accepts all 15,248 TDX spans with zero errors and scores 2,176 direct indexed
planes. For the 2,014 four-bit `0x14` planes, low-nibble-first has the lower aggregate delta
(93,917,821 versus 109,066,377), with 1,765 plane wins, 139 losses, and 110 ties. For the 162
eight-bit `0x13` planes, the bit-3/bit-4 palette permutation has the lower aggregate delta (554,076
versus 658,936), with 145 plane wins, 7 losses, and 10 ties. These are content-dependent coherence
results only. They nominate hypotheses for independent behavioral validation; they do not confirm
nibble order, palette order, channel meaning, swizzle, later-plane purpose, or display-ready pixels.

## Native validation

The aggregate verifier streams one asset at a time and discards each decoded value after updating
sanitized counters:

```powershell
build/msvc/Debug/omega_tool.exe asset-metadata-verify-tree private/extracted-disc
```

The confirmed semantic baseline is 15,248 textures, 15,442 blocks, 17,960 primary planes,
285,521,272 owned primary bytes, 15,190 palette blocks, 252 direct blocks, 1,510,240 palette entries,
62 implicit-zero textures, and 4,112 implicit-zero bytes, with zero errors.

The separate public-safe level-scoped verifier executes the native composed store API: one `Open`
per level followed by one `Load` for every published handle.

```powershell
build/msvc/Debug/omega_tool.exe level-texture-store-verify-tree private/extracted-disc
```

The confirmed run accepts all 18 levels, all 36 explicit texture sources, and 5,801 level-inventory
texture occurrences with zero errors. The loaded occurrence totals are 5,913 blocks, 7,603 planes,
615,232 palette entries, 27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 total owned
storage bytes.

Independent Open input/items/logical-output/depth/scratch maxima are
`3,076,944 / 1,460 / 111,014 / 0 / 71,467`; independent Load maxima are
`3,139,344 / 5,169 / 333,232 / 0 / 65,595`. Each field is maximized independently across its
operation class, so no single level, texture, or operation is asserted to exhibit a complete tuple.
These are deterministic logical API budgets rather than allocator or process-memory measurements.
They set internal defaults of 4 MiB input, 512 KiB logical output, 128 KiB scratch, 8,192 items,
4 KiB strings, and nesting depth one. Input, output, scratch, and items are independently rounded to
the next binary boundary above the larger Open/Load field maximum. Depth one is the smallest nonzero
headroom above measured depth zero while preserving bounded nested-source support; strings retain
the common 4 KiB safety cap. These defaults neither describe a co-occurring corpus tuple nor expose
runtime configuration or `--set` keys.
The occurrence totals are not a unique whole-disc asset count. The fixed report contains no paths,
names, hashes, offsets, payloads, per-level rows, identities, or bindings, and the native path assigns
no display pixels, channel order, nibble/palette policy, mip purpose, material relationship,
placement, visibility, draw, GPU-upload, or retail ownership semantics.
