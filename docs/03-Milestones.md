# End-to-end milestones

Each milestone ends with an executable regression, not only a research note.

## M0: Reproducible laboratory

Status: complete for the current NTSC-U research baseline.

- Current PCSX2 source build, isolated BIOS/disc data root, known game hash, boot log.
- Proprietary-data ignore gate and clean-room method.
- Evidence ledger, native CMake build, unit-test executable, and corpus-safe tooling.
- Accepted pure-native ADR: no MIPS interpretation, recompilation, or translated retail code.

## M1: Content mount and inspection

Status: in progress. Top-level/nested HOG indexing, VFS mounting, script-container inspection,
asset-family fingerprinting, the POP terrain prefix, the canonical level manifest, a semantic COL
spatial-mesh adapter, shared-budget level-spatial orchestration, a semantic TDX storage adapter, and
the passive VUM descriptor are implemented; other scene decoders remain incomplete.

- Native HOG parser validated against all 273 top-level archives and 6,677 nested spans.
- Virtual filesystem with physical-directory and HOG mounts.
- Owned level-manifest, spatial-mesh, and texture-storage IR plus allocation-free COL/VUM/TDX
  structural descriptors.
- Native aggregate validation: 18/18 level manifests, 7,036/7,036 semantic COL meshes, and all
  29,320 structural descriptor assets.
- GameDataService aggregate validation: 5,351/5,351 manifest cells load as owned spatial meshes,
  and all 18 headless startup probes preserve manifest/spatial cardinality.
- TDX aggregate validation: 15,248/15,248 textures normalize to 15,442 owned blocks and 17,960
  primary planes containing 285,521,272 owned bytes, including 4,112 duplicate-proven implicit
  zero bytes, with zero errors.
- Inspectors for ELF, scripts, textures, meshes, skeletons, animation, audio, and maps.
- Synthetic malformed-input tests for every decoder.

## M2: Native shell

Status: in progress. The SDL_GPU shell, strict launch parser, owner-supplied NTSC-U data-root
validation, headless probe, named-level manifest load, and synthetic debug view are implemented.
Headless named-level startup also owns the complete canonical spatial-mesh set.
Logging/configuration services, jobs, real input handling, audio output, and a frame scheduler
remain incomplete.

- Window, input, logging, configuration, jobs, renderer, audio device, and frame scheduler.
- Load the retail data tree supplied by the owner; clear diagnostics for missing/wrong region.
- Render a debug scene with no proprietary data embedded in the executable.

## M3: First level scene

Status: in progress. Native `--level=MINSK` selection, canonical manifest/spatial loading, and
renderer-neutral texture storage decoding are complete; the current grid is diagnostic only.
VUM render geometry, display-ready texture expansion, materials, cameras, placements, transforms,
and visibility remain incomplete.

- Decode and render MINSK geometry, textures, materials, cameras, and static objects.
- Match coordinate system, transforms, visibility, and representative frames against PCSX2.
- Load level through a native command-line option independent of the retail argument parser.

## M4: Actors and controls

- Skeletons, animation, player movement, camera, collision, weapons, and basic AI.
- Deterministic capture/replay for input and simulation state.

## M5: First mission loop

- Independently rewritten native mission behavior required by MINSK.
- Objectives, triggers, combat, inventory, checkpoints, failure, and completion.
- Golden scenario comparisons at named checkpoints.

## M6: Campaign coverage

- All offline levels and mission variants load and complete.
- Front end, character/gear progression, saves, difficulty, cinematics, subtitles, and audio.
- Compatibility database for known retail-data variants.

## M7: Multiplayer compatibility

- Document the original PS Rewired/DNAS-facing behavior from legal captures.
- Provide a local/community replacement protocol where lawful and technically feasible.
- Keep service credentials and packet captures containing personal data out of the repository.

## M8: Ship-quality runtime

- Performance budgets, crash recovery, accessibility, controller remapping, packaging,
  license notices, CI matrix, clean-machine install, and full regression suite.
- No firmware, disc content, executable code, or extracted asset ships with the runtime.
