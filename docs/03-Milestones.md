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
a semantic VUM material-catalog adapter plus a retail-only passive render-payload descriptor are
implemented. Bounded passive SKM and SKL descriptors plus a fixed-output retail-only SKA descriptor
are also implemented; SKAS remains separate aggregate-only evidence. Other scene decoders remain
incomplete.

- Native HOG parser validated against all 273 top-level archives and 6,677 nested spans.
- Virtual filesystem with physical-directory and HOG mounts.
- Owned level-manifest, spatial-mesh, and texture-storage IR plus bounded COL/VUM/TDX structural
  descriptors. The VUM render-payload descriptor remains retail-only and is not canonical IR.
- Native aggregate validation: 18/18 level manifests, 7,036/7,036 semantic COL meshes, and all
  29,320 structural descriptor assets.
- GameDataService aggregate validation: 5,351/5,351 manifest cells load as owned spatial meshes,
  and all 18 headless startup probes preserve manifest/spatial cardinality.
- TDX aggregate validation: 15,248/15,248 textures normalize to 15,442 owned blocks and 17,960
  primary planes containing 285,521,272 owned bytes, including 4,112 duplicate-proven implicit
  zero bytes, with zero errors.
- VUM aggregate validation: 7,036/7,036 catalogs normalize to 38,793 owned names, 38,899 materials,
  and 42,631 dense name references. Its passive payload pass validates 91,460 pairs, 38,023
  normalized targets, 134,122 middle-to-final references, and 365,840 ordered Q/P references,
  with zero errors.
- Passive SKM and SKL native descriptors preserve only bounded structural metadata. The passive
  SKA descriptor implements the aggregate-proven neutral counted-word extent as fixed output while
  the two-candidate SKAS text envelope remains separate; neither assigns animation semantics. SKA
  is wired into the sanitized native corpus verifier, with its independent 213/213 run still pending.
- Inspectors for ELF, scripts, textures, meshes, skeletons, animation, audio, and maps.
- Synthetic malformed-input tests for every decoder.

## M2: Native shell

Status: in progress. The SDL_GPU shell, strict launch parser, owner-supplied NTSC-U data-root
validation, headless probe, named-level manifest/spatial load, and deterministic synthetic
canonical-COL wireframe contact sheet are implemented. The contact sheet places meshes in
source-order tiles and projects each mesh along its two largest coordinate extents. It is a
clean-room diagnostic, not world placement or reconstructed geometry, and makes no VUM, TDX, or
other retail semantic claim. Headless named-level startup also owns the complete canonical
spatial-mesh set.
The logging service (bounded thread-safe writes, stderr and ring sinks), configuration service
(strict bounded key/value grammar with typed lookups and overrides), job service (bounded
worker-pool owner with deterministic shutdown), fixed-step frame scheduler (pure integer-
nanosecond accumulator; the simulation step stays a caller-supplied value, never a retail
claim), and the platform-neutral input tracking core (bounded binding table plus per-frame
edge snapshots) are implemented as tested library services. Wiring those services into the
SDL host shell is complete through an app-owned composition root: strict file/command-line
configuration resolves their bounded settings, logging owns stderr and ring sinks, the worker
pool drains before shutdown, and steady-clock deltas drive fixed-step planning. SDL input now has
an app-owned, non-hot-reloadable `SdlInputService` that owns the gamepad subsystem, the global event
pump through `PumpEvents`, and one primary gamepad. It filters button events by instance ID,
reconciles only gamepad controls on disconnect, and promotes the next available device;
deterministic headless virtual-gamepad coverage exercises this boundary. The single-primary rule is
synthetic shell policy, not inferred retail behavior, and `SdlGpuHost` is now video/render-only.
Executing a `SimulationWorld` from the fixed-step plans is wired through `OmegaApp`: every planned
step advances an owned,
platform-neutral deterministic world clock before rendering, with fail-closed representation
limits. The app also owns the SDL process lifetime and a resumed system-default playback stream;
its callback supplies bounded project-owned silence and exposes lock-free health counters without
loading files or retail data. Components, gameplay systems, render snapshots, decoded voices, and
mixing remain incomplete.

- Window, input, logging, configuration, jobs, renderer, audio device, and frame scheduler.
- Load the retail data tree supplied by the owner; clear diagnostics for missing/wrong region.
- Render a debug scene with no proprietary data embedded in the executable.

## M3: First level scene

Status: in progress. Native `--level=MINSK` selection, canonical manifest/spatial loading, and
renderer-neutral texture storage decoding are complete; the current synthetic canonical-COL
wireframe contact sheet is diagnostic only. It uses source-order tiles and a per-mesh projection of
the two largest coordinate extents, not world placement or reconstructed geometry, and makes no
VUM, TDX, or other retail semantic claim. VUM material catalogs are decoded, but render geometry,
material binding/parameters, display-ready texture expansion, cameras, placements, transforms, and
visibility remain incomplete. A passive retail-only VUM descriptor now preserves the proven
pair/reference grammar without asserting vertices, indices, draws, or material assignments.

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
