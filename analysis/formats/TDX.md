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

## Cross-format exact-first one-terminal-extension candidate experiment

The additive one-terminal-extension family does not widen the locator inventory: it uses the same
5,801 direct normalized `.TDX` occurrences in the same two class-qualified containers, and direct
members with any other suffix remain outside the locator universe. Complete strings are normalized
first. If full-string equality has no candidate, at most one syntactic extension is removed from the
final component independently on each side before a second exact comparison. Directory components
are preserved, and basename, substring, fuzzy, repeated-extension, nested-container, and other alias
families remain excluded.

Candidate and coverage bookkeeping retains each original class-qualified `.TDX` locator rather than
the transformed comparison key. The two byte-identical confirmed passes reach 5,690 original locator
occurrences only through extension elision, reach none through exact equality or both routes, and
leave 111 unreached. All 34,267 accepted VUM name occurrences classify as extension-elided
unique-primary candidates; this assigns neither ownership nor priority to the primary class. It is
offline lexical coverage, not evidence that retail code removes extensions, consumes a material
reference, resolves an alias, or binds a texture to a material, cell, mesh, draw, placement,
visibility state, or renderer resource. Detailed status, material-flag, nonclaim, and privacy
constraints are documented in `analysis/formats/VUM.md`.

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

## Native asynchronous asset-service lifecycle

E-0043 adds `AssetService` v0 above the immutable `LevelTextureStore`. Creation allocates a fixed
reusable slot pool and publishes service-scoped generation handles. `Request` schedules only the
selected store handle's existing native `Load`; `Get` exposes the independently owned immutable
`TextureStorageIR` only after `Ready`, and `Release` explicitly recycles a `Ready` or `Failed` slot
while advancing its generation so every earlier handle remains stale. Queued or loading work cannot
be cancelled in v0. At generation exhaustion a slot retires instead of wrapping.

Accepted `JobService` callables retain a shared service implementation through their final return.
Service destruction stops new requests, waits for this service's accepted work rather than the
worker pool's global idle state, and expires the public identity after publication drains. `OmegaApp`
owns the service optionally only when startup owns a texture store, and member order destroys it
before both `JobService` and its non-owning `GameDataService`/`LevelTextureStore` dependencies.

The public-safe owned-tree verifier exercises the complete sequential lifecycle:

```powershell
build/msvc/Debug/omega_tool.exe asset-service-verify-tree private/extracted-disc
```

Two passes produce byte-identical schema-version-1 reports. Both accept 18/18 levels, 36 explicit
sources, and 5,801 texture occurrences with zero errors. Texture occurrences, requests, `Ready`
observations, successful `Get` calls, releases, stale-handle rejections, and zero-residual checks all
total 5,801. Loaded storage totals are 5,913 blocks, 7,603 planes, 615,232 palette entries,
27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 owned bytes. Independent maxima are
one active slot, one in-flight request, and 333,232 resident logical bytes.

The verifier deliberately uses one worker, one pending job, one service slot, one allowed in-flight
request, and a 524,288-byte resident-logical limit. Production defaults are 64 slots, 64 in-flight
requests, and 64 MiB resident logical output, with a hard maximum of 8,192 slots. These are synthetic
project bounds, not observed retail limits or user-facing settings. A clean MSVC build completed with
zero warnings and errors; the focused checks and full 18/18 CTest suite passed, as did 100 repeated
lifecycle-test runs. The unchanged E-0038 level-store verifier was also revalidated.

This service does not perform VUM-name or material lookup, alias resolution, material/texture/cell/
mesh/draw binding, display-pixel expansion, GPU upload, placement, visibility, or rendering. The
fixed aggregate report contains no paths, names, hashes, offsets, payloads, per-level rows,
identities, bindings, messages, or exception text.

## Packed24 transfer diagnostic projection

E-0078 introduces a renderer-neutral diagnostic utility over already-owned `TextureStorageIR`; it
does not change TDX decoding. The accepted shape is deliberately narrow: one nonzero top-level
rectangle with known `Packed24` sample encoding, exactly one block, exactly one plane, no palette,
one nonzero matching plane rectangle with known `Packed24` transfer-element encoding, and exactly
three source byte slots per rectangle element. Multi-block, multi-plane, palette-bearing, unknown,
or other known encodings fail closed rather than selecting or inferring a purpose.

The project diagnostic copies each consecutive group of three source byte slots into slots zero,
one, and two of a four-slot owned output group and writes the constant `0xff` into slot three. The
rectangle controls only deterministic output dimensions and cardinality. This is not a display
decoder: it establishes no channel names, row origin or row order, texel swizzle, color space,
premultiplication, alpha meaning or scaling, display-ready correctness, block/plane/mip/slice/frame
purpose, Packed32 or indexed expansion, nibble order, or palette permutation. No app, GPU upload,
renderer, AssetService, or E-0077 selection path consumes this utility in E-0078.

Source and output byte arithmetic are checked independently before cardinality and independent
synthetic 48 MiB/64 MiB limits. A generated 16x16 seed-`0x21` shape maps 768 source bytes to 1,024
owned output bytes with FNV-1a-64 `0x4abb645f50f5a325`; seed `0x61` maps to
`0x36590f25eee3ab25`. These are project diagnostic oracles only. Serialized local validation passed
focused/full MSVC, the direct unit plus 100/100 repeated runs, focused and 32/36/32 CTest,
runtime-off direct/focused checks with 28 registrations, dependency 168, tooling 209, and Python
compile-all. The staged public-tree gate checked 255 indexed text blobs; commit, DCO, publication,
and exact-main validation remain pending. No
private or owner files, D-drive content, disc image, executable, emulator, or PCSX2 input was used.

## Indexed8 display-candidate projection

E-0087 adds `BuildTdxIndexed8CandidateDebugImage`, a renderer-neutral diagnostic over already-owned
`TextureStorageIR`; it does not alter the TDX decoder or identify a retail display path. The
accepted shape is deliberately narrower than the complete Indexed8 storage family: one nonzero
top-level Indexed8 rectangle, exactly one block, exactly one nonzero matching `Packed8` plane, one
palette whose nonzero rectangle exactly covers its owned entries, exactly 256 palette entries, one
index byte per rectangle element, and no additional block, plane, or payload.

Every unresolved transformation is a required caller choice. Palette lookup is either identity or
the bit-3/bit-4 permutation already scored as an analysis candidate. Output slots zero through two
use one of the six permutations of palette source slots zero through two. Output slot three is
either the constant `0xff`, source slot three unchanged, or source slot three multiplied by two and
clamped to `0xff`. Source rows are either copied in linear top-to-bottom order or reversed as whole
linear rows. No intra-row or page swizzle candidate is implemented in this first direct-`Packed8`
slice; unsupported layout values fail closed instead of guessing.

The result is an independently owned four-slot `DebugImage`. Its field name does not promote the
source slots to channel names or the result to display-correct RGBA. Caller limits may tighten but
cannot exceed the project hard maxima of 16 MiB of index data plus the exact 1,024-byte palette and
64 MiB of output. Fixed typed diagnostics disclose no dimensions, payload, offset, source identity,
or exception text. Generated tests exercise all 256 indices, distinguishing palettes, both CLUT
candidates, all six source-slot permutations, all three alpha candidates, both whole-row
orientations, exact/one-below caller budgets, non-overridable hard maxima, malformed shapes,
determinism, nonmutation, and independent ownership. Serialized local validation passes focused
Debug and Release builds with zero warnings, direct Debug and Release units, 100/100 repeated Debug
runs, the full 34/34 Debug CTest suite, formatting, diff, the 174-file native dependency gate, all
212 tooling tests, Python compile-all, and the staged public-tree gate over 265 indexed text blobs.
Commit, DCO, publication, and exact-main validation remain pending.

This is hypothesis plumbing, not corroboration. It assigns no channel names, alpha meaning or
scale, row origin, swizzle, color space, premultiplication, filtering, UV, block/plane/mip/frame
purpose, material binding, menu use, GPU upload, retail rendering, gameplay, or emulator
equivalence. Only public project source and generated fixtures were used; no private or owner file,
proprietary input, D-drive content, disc image, retail executable, emulator, or PCSX2 input was
accessed.

## Indexed4 display-candidate projection

E-0121 records `BuildTdxIndexed4CandidateDebugImage`, a second renderer-neutral diagnostic over
already-owned `TextureStorageIR`. It is an independent sibling of the Indexed8 candidate above: it
does not alter the TDX decoder, does not extend the Indexed8 utility, and identifies no retail
display path. The accepted shape is a strict direct Indexed4/Packed4 slice and is deliberately
narrower than the complete Indexed4 storage family: one nonzero top-level `Indexed4` texture
rectangle, exactly one block, exactly one nonzero `Packed4` plane whose rectangle exactly equals the
texture rectangle, exactly one present palette whose nonzero rectangle exactly covers its owned
entries, exactly 16 four-source-slot palette entries, and a packed plane whose byte count is exactly
`(width * height + 1) / 2`. Any additional block, plane, palette cardinality, encoding, or payload
byte fails closed.

Every unresolved display choice remains an explicit caller-selected hypothesis across five axes,
supplied through a non-default-constructible policy: two packed nibble orders (low nibble first or
high nibble first within each source byte); palette lookup in source order only, so no permuted
Indexed4 palette candidate is asserted at all and the enumeration exists solely to keep the axis
explicit; all six permutations of palette source slots zero through two into output slots zero
through two; three output-slot-three mappings (constant `0xff`, palette source slot three unchanged,
or that slot doubled and clamped to `0xff`); and two linear row origins (top-to-bottom or whole-row
bottom-to-top). That is 2 x 1 x 6 x 3 x 2 = 72 explicit combinations. The slot-zero-through-two
permutation never affects output slot three: opaque emits constant `0xff`, source-slot-three copies
palette source slot three, and doubled-clamped source-slot-three doubles and clamps that source slot.
No intra-row, page, or block swizzle candidate is implemented, and no mip, frame, or material
candidate exists.

Odd texel cardinality has an exact stated behavior rather than an inferred one. The plane must carry
`(width * height + 1) / 2` bytes, so an odd rectangle requires a final packed byte whose second
nibble is never read: under low-nibble-first that is the final high nibble, and under
high-nibble-first that is the final low nibble. Changing the unused nibble cannot change any output
byte. This is a project containment rule for a synthetic shape, not an observed retail padding,
alignment, or tail convention.

The result is an independently owned four-slot RGBA8-shaped `DebugImage` of exactly
`width * height * 4` bytes. Its field name does not promote the source slots to channel names or the
result to display-correct RGBA. The borrowed storage is never mutated, repeated projections are
byte-identical yet separately allocated, and the returned pixels survive mutation and destruction of
the source. Caller limits may tighten but cannot raise the project hard maxima of 8 MiB of packed
source plus the exact 64-byte palette and 64 MiB of output; a caller budget above either maximum is
rejected as `invalid-limits` rather than silently clamped. `std::bad_alloc` and `std::length_error`
are contained and reported as `allocation-failed`, so no exception escapes the entry point.

Twenty-five fixed typed diagnostics carry fixed kebab-case names and fixed category-only messages
that disclose no input-derived dimensions, payload, offset, source identity, path, runtime value, or
exception text. Validation priority is fixed and asserted rather than incidental: the five policy
axes in declared order, then caller limits, then texture dimensions, then sample-encoding validity
before Indexed4 support, then block count, plane count, and palette presence, then plane dimensions,
then transfer-element-encoding validity before `Packed4` support, then the texture/plane rectangle
match, then source and output byte arithmetic, then packed index cardinality, then palette
dimensions, palette entry-count agreement, and the exact 16-entry requirement, and only then the
source and output byte budgets.

The utility is stateless, reentrant, callable from any worker thread, CPU-only, and performs no I/O,
platform, GPU, service, or shared-state work. It is not wired into startup, `AssetService`, asset
selection, renderer upload, material binding, or menu presentation. Generated tests cover the fixed
error contract, all 72 candidate combinations with every source-order index traversed, distinguishing
outputs for each multi-valued implemented axis while enumerating the singleton identity-palette axis,
both odd-cardinality unused-nibble cases, determinism, source nonmutation, independent ownership after
source mutation and destruction, representative competing-invalid-input boundaries across the
documented validation order, exact and one-below budgets, and an exact 4097x4096 packed rectangle
above the 8 MiB hard cap.

This is hypothesis plumbing, not corroboration. It assigns no retail nibble order, palette order,
channel names, alpha meaning or scale, row origin, swizzle, color space, premultiplication,
filtering, UV, block/plane/mip/frame purpose, material binding, menu use, GPU upload, rendering,
gameplay, corpus result, or PCSX2 equivalence. Only tracked clean-room source and project-generated
fixtures were used; no private or owner file, proprietary input, D-drive content, disc image, retail
executable, emulator, or PCSX2 runtime input was accessed.
