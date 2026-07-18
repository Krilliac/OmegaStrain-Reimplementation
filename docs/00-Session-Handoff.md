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

1. Correct the private save-state load for the built oracle-only PCSX2 VUM tracer. Its first smoke
   run ended with `savestate_load_failed` before selecting a runtime copy or recording an event, so
   no report has passed the gate. Privately select an exact-primary MINSK VUM, run the identical
   save state twice, and validate both complete reports with `tools/validate_vum_read_trace.py`,
   including a byte-identical repeat comparison. The validator receives expected VUM size privately
   and enforces strict bounds, exact schema and lifecycle status, deterministic ordering, unique
   keyed rows, and detail/summary cross-counts. Publish only VUM-relative offset/width/count
   aggregates, anonymous site ranges/counts, VIF source-relative ranges/output counts, and
   lifecycle status. Reports must contain no PCs;
   absolute or RAM addresses; CRCs or hashes; paths or names; payload bytes/data; or instructions,
   opcodes, or registers. No accepted report or evidence claim exists yet: add no evidence-ledger
   entry and infer no geometry, topology, vertex, packet, or material semantics until a real
   deterministic trace passes the gate.
2. Validate TDX swizzle, nibble order, palette permutation, and channel expansion independently
   before producing display-ready pixels or GPU uploads.
3. Continue POP after the validated terrain prefix, beginning with placement and visibility data.
4. SKM, SKL, and SKA now have bounded passive native descriptors. SKA is a fixed-output retail-only
   view of the proven 112-byte-prefix counted-word envelope. Keep the two-candidate SKAS text family
   separate and assign no actor, skeleton, timing, channel, transform, or animation semantics. Its
   `omega_tool` corpus-verifier path is wired, but record no native aggregate-pass claim until an
   owner-supplied private run reports 213 candidates, 213 valid, zero errors, 158 exact, and 55
   zero-tailed spans.
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
