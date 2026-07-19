# Session handoff

## Objective

Build a clean-room, pure-native reimplementation of *Syphon Filter: The Omega Strain* for
modern host CPUs. Use metadata and observable behavioral tests to derive contracts, then write
the implementation independently. PCSX2 and PS2-code analysis are offline reference tools, not
shipping dependencies or execution mechanisms.

## Verified game identity

| Field | Value |
| --- | --- |
| Region/build | NTSC-U retail |
| Boot executable | `SCUS_972.64` |
| PCSX2 serial | `SCUS-97264` |
| Game CRC | `D5605611` |
| ELF size | 4,071,592 bytes |
| ELF entry point | `0x00100008` |
| ISO size | 3,457,744,896 bytes |
| ISO SHA-256 | `A60A4B41FEA808335BAA3B887A377BB98063F6F169CA498EFEE49397FBA96130` |

## Completed setup

1. Pulled current official PCSX2 `master` at
   `86d76bbf590566d9ea74d381eeff3acd9856503a`.
2. Installed the official 2026-07-09 Windows dependency snapshot and current patches database.
3. Built `Release AVX2` through VS2022/MSBuild with zero warnings and zero errors.
4. Recovered the owner's existing USA BIOS, ISO, and two `SCUS-97264` save states from private
   storage into the ignored `private/` tree.
5. Initialized a separate PCSX2 data root under `runtime/data/PCSX2` and selected
   `scph39001.bin`.
6. Booted the game successfully through the newly compiled emulator. The log confirms BIOS
   v1.60, `SCUS_972.64`, CRC `D5605611`, D3D12, VM initialization, and the entry point.
7. Extracted the ISO9660 filesystem: 448 files, 36 directories, 3,455,648,408 bytes.
8. Generated deterministic disc and ELF metadata under `analysis/`.
9. Reverse-engineered the common HOG directory structure and validated all 273 top-level
   archives: 32,351 entries, zero structural failures. A safe parser/extractor now lives at
   `tools/hog.py`.
10. Built a pure-native HOG/VFS layer that validates all 6,677 nested spans, resolves all 5,351
    POP terrain references across 18 level manifests, semantically decodes all 7,036 COL spatial
    meshes, all 15,248 TDX storage assets, and all 7,036 VUM material catalogs with zero errors.
11. Added owner-supplied data-root validation and native named-level startup. A headless MINSK
    probe loads 299 canonical manifest cells and matching owned spatial meshes; the SDL_GPU/D3D12
    path renders a deterministic synthetic canonical-COL wireframe contact sheet for 120 frames
    and exits without a lingering process. Meshes occupy source-order tiles, with each mesh
    projected along its two largest coordinate extents. This clean-room diagnostic is not world
    placement or reconstructed geometry and makes no VUM, TDX, or other retail semantic claim.
12. Routed every manifest cell through `GameDataService` into owned neutral spatial meshes under a
    shared level-operation budget. Native verification covers all 18 levels and 5,351 cells with
    zero errors: 20,203 canonical nodes, 93,356 leaves, 889,640 vertices, 1,239,980 triangles and
    references, and 2,137 normalized empty meshes. Headless startup independently reports matching
    manifest/spatial cardinality for all 18 levels.
13. Corrected the TDX counted extent to `64 + block_count * block_stride` and added an owned
    storage-plane adapter. It preserves source-order blocks, transfer planes, palette channel
    bytes, and packed sample families without claiming display-ready pixels. Aggregate verification
    covers 15,248 textures, 15,442 blocks, 17,960 primary planes, 285,521,272 owned primary bytes,
    and 4,112 duplicate-proven implicit zero bytes with zero errors.
14. Added an owned VUM material catalog plus a separate retail-only passive render-payload
    descriptor. Aggregate verification covers 7,036 catalogs, 38,793 names, 38,899 materials,
    42,631 dense name references, 91,460 Q/P pairs, 38,023 normalized T targets, 134,122
    middle-to-final references, and 365,840 ordered Q/P final references with zero errors. Payload
    bytes, topology, vertex attributes, usage-code meaning, and material binding remain unassigned.
15. Split SDL input from the GPU host into the app-owned, non-hot-reloadable `SdlInputService`. Its
    `PumpEvents` owns the process-global event pump, while the service owns the gamepad subsystem
    and one primary `SDL_Gamepad`; accepts button events only from that instance; resets only
    gamepad controls when it disconnects; and promotes the next available device. `SdlGpuHost` now
    owns video/render resources only. Deterministic headless virtual-gamepad coverage exercises
    attach, button edges, disconnect reconciliation, and promotion. The single-primary rule is
    synthetic shell policy, not inferred retail behavior.
16. Added a fixed-output retail-only SKA descriptor for the proven 112-byte-prefix counted-word
    envelope. Native aggregate verification accepts all 213 owner-supplied spans with zero errors:
    158 exact, 55 zero-padded, and 2,180,832 logical bytes. It retains no payload and assigns no
    actor, skeleton, timing, channel, transform, compression, or animation semantics; SKAS remains
    a separate two-candidate text-evidence family.
17. Added a privacy-safe bounded POP post-terrain scanner. It accepts all 18 owner-supplied POPs
    with zero errors and finds the same 19 aligned literal-tag candidates exactly once per file and
    in one shared order: 342 aggregate hits. These are candidate markers only; section headers,
    counts, extents, placement, visibility, and all other field semantics remain unassigned.
18. Added a bounded, aggregate-only TDX display-layout hypothesis scorer. Across 15,248 spans, the
    direct four-bit family favors low-nibble-first coherence on 1,765 planes versus 139, with 110
    ties; the direct eight-bit family favors the bit-3/bit-4 palette permutation on 145 planes
    versus 7, with 10 ties. These content-dependent scores are candidate selection only and do not
    confirm nibble order, palette order, channel meaning, swizzle, or display-ready pixels.
19. Added a bounded POP layout hypothesis scorer. Across all 18 POPs, five marker-relative `+4`
    word/fixed-stride tuples fit every occurrence: `INL:`/36, `PNT:`/88, `DIR:`/44, `ENV:`/76,
    and `INV:`/84 bytes. Nonzero instances establish the candidate strides; one zero `PNT:` and two
    zero `INV:` instances have the predicted empty extent but add no stride evidence. These
    arithmetic fits do not yet confirm markers, counts, records, section boundaries, placement,
    visibility, or field meanings.
20. Completed the first bounded privacy-safe VUM consumer trace. One selected runtime copy produced
    one complete 120-frame pair; strict validation accepted both validated reports, and the repeat
    was byte-identical. Each report contains two EE-read aggregate rows, two anonymous-site rows,
    and zero VIF1 chunk rows. The post-run containment audit found no retained runtime copy,
    executable surface, reparse point, owner-input copy, or emulator/build process. An independently
    guarded second ranked trial reproduced those aggregates exactly: one capture with four accepted
    aggregate rows, split evenly between EE-read and anonymous-site rows, zero VIF1 rows, and zero
    aggregate-count deltas. Both accepted trials are header-only: their EE-read rows remain confined
    to the already-opaque header-vector block, and no accepted row reaches counts, records, metadata,
    payload, VIF, or tail data. This repeated observation does not exclude copied buffers or activity
    outside the bounded observation window and assigns no geometry, topology, vertex, material,
    packet, draw, placement, visibility, or gameplay semantics.
21. Added all-or-error `LevelContentIR` startup composition. `GameDataService` now traverses each
    common/nested archive and manifest-referenced cell once while decoding its unique COL and VUM
    members under one shared operation budget. The C-only headless probe accepts all 18 levels and
    all 5,351 manifest cells with zero errors. Parallel spatial/material positions preserve source
    order and cardinality only; they assert no binding, placement, visibility, or render semantics.
22. Bounded the former level-TDX topology question to its actual common-archive scope. The
    aggregate scanner accepts all 18 runtime levels, 5,351 manifest cell occurrences, and 5,413
    scanned DATA.HOG-graph directory occurrences with zero errors and finds zero normalized
    `.TDX`-suffixed members. This extension-bounded negative result does not exclude another
    representation or establish ownership or a texture-to-cell/material/mesh binding.
23. Measured two explicit sibling texture-container classes separately. The direct-only scanner
    accepts both roles for all 18 runtime levels: 36 exact containers and 5,801 direct TDX members
    with zero errors, collisions, malformed textures, nested traversal, or non-TDX entries. The
    generic classes contribute 5,765 and 36 occurrences. This establishes containment, not retail
    ownership, necessity, priority, or any texture-to-material/cell/mesh/render relationship.
24. Added the public-safe native `LevelTextureStore` and all-level verifier. It accepts 18/18 levels,
    36 explicit sources, and 5,801 level-inventory texture occurrences with zero errors, loading
    5,913 blocks, 7,603 planes, 615,232 palette entries, 27,101,352 plane bytes, 2,460,928 palette
    bytes, and 29,562,280 owned bytes. Independent Open input/items/logical-output/depth/scratch
    maxima are `3,076,944 / 1,460 / 111,014 / 0 / 71,467`; Load maxima are
    `3,139,344 / 5,169 / 333,232 / 0 / 65,595`. Each maximum is fieldwise and is not asserted to
    co-occur with the others. Internal defaults are 4 MiB input, 512 KiB logical output, 128 KiB
    scratch, 8,192 items, 4 KiB strings, and depth one. The measured byte/item fields are separately
    next-binary-rounded above the larger Open/Load maximum; depth one is the smallest nonzero
    headroom above measured depth zero. They are not runtime configuration or `--set` keys.
    Named-level startup now retains the locator inventory after manifest, content, and debug-image
    success; the 18/18 aggregate probe remains valid across all 5,351 cells. Startup performs no
    texture `Load`, material binding, display expansion, GPU upload, or rendering.
25. Added a retail-only passive POP post-terrain hypothesis descriptor and privacy-safe fixed-schema
    native verifier. The descriptor fail-closed checks only the established ordered aligned-literal
    envelope and five arithmetic extent hypotheses, retains no input span or payload, and does not
    enter canonical asset IR. The owned-corpus verifier discovers and accepts all 18 `.POP`
    candidates with zero rejections or errors. Independent accepted-only
    input/items/logical-output/string/scratch maxima are `919360 / 1 / 168 / 26 / 80036`; no maximum
    tuple is asserted to co-occur. This confirms native conformity to the published hypothesis family
    only, not section boundaries, counts, records, payload meaning, placement, visibility, rendering,
    or gameplay.
26. Added and reran the bounded full-normalized-member-name lexical-coherence experiment over
    manifest-scoped VUM names and the two explicit direct TDX locator classes. It scans 18/18 levels
    with zero errors, and its cell/catalog/name/material/reference and container/locator totals match
    the native validation populations exactly. All 34,267 VUM name occurrences and 37,893 dense
    references lack terminal `.TDX`; no name enters the eligible exact lookup branch, all 34,589
    material records have an ineligible reference, and all 5,801 class-qualified locators remain
    unreached. The experiment compares complete normalized strings only and excludes basename, stem,
    extension addition/removal, suffix substitution, nested-container, fuzzy, and other alias
    hypotheses. This narrow negative result establishes no retail lookup, texture-name role, material
    binding, class priority, or runtime integration.

## Disc observations

- The root contains `SYSTEM.CNF`, `SCUS_972.64`, `OVL_DNAS.BIN`, `SFO_GAME.INI`, and PS2
  networking/USB IRX modules.
- `GAMEDATA/` is organized by level (`BELARUS1`, `BELARUS2`, `CHECHNYA`, `ITALY`,
  `KYRGSTAN`, `LORELEI`, `MINSK`, and others).
- Repeated level payloads include `DATA.HOG`, `DATA.POP`, `OBJECTS.HOG`, `SCRIPTS.HOG`,
  `TEX.HOG`, `MAPVUM.HOG`, `SND.HOG`, and `SNDVAG.HOG`.
- Static analysis confirms attached `-lMINSK` syntax and the MINSK table entry. The independent
  `-x` startup mode changes splash and one construction path; it does not select the level.
- The ISO contains 28,672 bytes after the ISO9660 physical end. Preserve the original image
  for LBA and tail analysis; the extracted tree is not a bit-perfect substitute.
- HOG files begin with five little-endian 32-bit words, followed by `count + 1` payload
  offsets, a NUL-delimited ASCII filename table, alignment padding, and concatenated payloads.
  The first word varies and its checksum/tag semantics remain unknown.
- `MINSK/SCRIPTS.HOG` contains six modules: `INIT.SO`, `MUSIC.SO`, `OBJECTIVES.SO`,
  `PRAGUE.SO`, `UTILS.SO`, and `VOICE.SO`.

## Next focused pass

1. Use the accepted deterministic VUM trace only as a structural baseline. Collect additional
   bounded pairs through controlled comparisons that change one research condition at a time,
   require strict validation and byte-identical repeats, and compare only sanitized relative
   ranges and counts. Keep sites anonymous and bounds private. Promote no relationship without
   cross-capture stability plus independent corroboration, and infer no geometry, topology,
   vertex, material, packet, draw, placement, visibility, or gameplay semantics from the current
   header-only aggregate rows or the absence of VIF1 chunks. A zero VIF1 count does not rule out
   copied buffers or consumption outside the bounded observation window.
2. Use E-0041's ineligible partition only to prioritize controlled observations of retail name
   lookup and MTRL-record consumption. Test one-pass extension removal, basename/stem/suffix, or
   other alias behavior as separate bounded experiments because E-0041 excludes them. Do not connect
   `MaterialCatalogIR` to `LevelTextureStore` until lookup behavior and material consumption are
   independently observed and corroborated.
3. Validate the TDX scorer's favored direct-family nibble and palette candidates through an
   independent behavioral oracle; separately resolve transfer-`0x00` swizzle and channel expansion
   before producing display-ready pixels or GPU uploads.
4. Continue POP beyond the now-native guarded hypothesis envelope only through independent evidence.
   Test record-internal invariants and controlled behavioral consumption before promoting any literal
   to a boundary or any observed word/stride to count or record semantics; independently connect
   consumed fields to placement or visibility behavior. Passive-descriptor acceptance is a
   conformity check, not semantic corroboration.
5. Capture PS Rewired network behavior separately before designing any replacement service.

## Installed research tools

- PCSX2 source/debug build under `third_party/pcsx2`.
- IDA 9.1 with MIPS processor support (private local installation).
- Ghidra 12.1.2 (private local installation).
- radare2 (private local installation).

## Safety rules

- Never commit anything under `private/`, `runtime/`, `third_party/`, or `downloads/`.
- Publish no firmware, executable, archive, asset, save-state, or decrypted proprietary data.
- Keep claims tied to hashes, logs, captures, and reproducible scripts.
