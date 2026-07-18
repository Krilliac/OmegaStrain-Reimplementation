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
asset-family fingerprinting, the POP terrain prefix plus a retail-only passive post-terrain
hypothesis descriptor and fixed-schema native verifier, the canonical level manifest, a semantic
COL spatial-mesh adapter, shared-budget level-spatial orchestration, a semantic TDX storage adapter,
a bounded level-scoped texture store and native verifier, and a semantic VUM material-catalog
adapter plus a retail-only passive render-payload descriptor are implemented. Bounded passive SKM
and SKL descriptors plus a fixed-output retail-only SKA descriptor are also implemented; SKAS
remains separate aggregate-only evidence. Other scene decoders remain incomplete.

- Native HOG parser validated against all 273 top-level archives and 6,677 nested spans.
- Virtual filesystem with physical-directory and HOG mounts.
- Owned level-manifest, spatial-mesh, and texture-storage IR plus bounded COL/VUM/TDX structural
  descriptors. The VUM render-payload descriptor remains retail-only and is not canonical IR.
- Native aggregate validation: 18/18 level manifests, 7,036/7,036 semantic COL meshes, and all
  35,013 COL/VUM/TDX/SKA/SKM/SKL descriptor assets.
- GameDataService aggregate validation: 5,351/5,351 manifest cells load as owned spatial meshes,
  and all 18 headless startup probes preserve manifest/spatial cardinality. Startup now obtains
  the spatial and material collections through one all-or-error `LevelContentIR` archive traversal
  and shared budget, then opens and retains an inventory-only `LevelTextureStore` after the existing
  debug-image gate; the parallel collections assert no binding and startup performs no texture
  `Load`. The public-safe
  level-material catalog verifier also loads all 18 levels and 5,351/5,351 manifest-ordered
  catalogs with zero errors: 34,267 owned names, 34,589 material records, and 37,893 dense name
  references. It emits only aggregate counts and typed error categories.
- TDX aggregate validation: 15,248/15,248 textures normalize to 15,442 owned blocks and 17,960
  primary planes containing 285,521,272 owned bytes, including 4,112 duplicate-proven implicit
  zero bytes, with zero errors.
- Bounded common-archive containment scanning accepts all 18 runtime levels, 5,351 manifest cell
  occurrences, and 5,413 scanned archive-directory occurrences with zero errors and finds zero
  normalized `.TDX`-suffixed members. This extension-bounded result does not exclude another
  representation or assign a texture owner or binding.
- Direct sibling-container scanning accepts both explicit container classes for all 18 runtime
  levels: 36 exact containers and 5,801 direct TDX members with zero errors, collisions, malformed
  textures, nested traversal, or non-TDX entries. The role classes contribute 5,765 and 36
  occurrences; containment establishes neither retail ownership, necessity, priority, nor a
  texture-to-material/cell/mesh relationship.
- The public-safe native level-texture verifier accepts 18/18 levels, 36 explicit sources, and 5,801
  level-inventory texture occurrences with zero errors. It loads 5,913 blocks, 7,603 planes,
  615,232 palette entries, and 29,562,280 owned storage bytes. Independent Open
  input/items/logical-output/depth/scratch maxima are
  `3,076,944 / 1,460 / 111,014 / 0 / 71,467`; Load
  maxima are `3,139,344 / 5,169 / 333,232 / 0 / 65,595`. These are fieldwise maxima, not one
  co-occurring operation profile. Internal defaults are 4 MiB input, 512 KiB logical output,
  128 KiB scratch, 8,192 items, 4 KiB strings, and depth one. Measured byte/item fields are
  independently next-binary-rounded; depth one is the smallest nonzero headroom above measured
  zero. The values are not runtime configuration or `--set` keys, and the aggregate report exposes
  no private identity or binding.
- Aggregate-only TDX coherence scoring accepts the same 15,248 spans and nominates low-nibble-first
  for direct four-bit `0x14` planes and the bit-3/bit-4 palette permutation for direct eight-bit
  `0x13` planes. The content-dependent scores are hypotheses, not display-layout semantics.
- VUM aggregate validation: 7,036/7,036 catalogs normalize to 38,793 owned names, 38,899 materials,
  and 42,631 dense name references. Its passive payload pass validates 91,460 pairs, 38,023
  normalized targets, 134,122 middle-to-final references, and 365,840 ordered Q/P references,
  with zero errors.
- The first privacy-safe VUM consumer trace has one strict-validator-accepted complete 120-frame
  pair from one selected runtime copy. Its repeat is byte-identical; each validated report contains
  two EE-read aggregate rows, two anonymous-site rows, and zero VIF1 chunk rows. Post-run
  containment auditing found no retained runtime copy, executable surface, reparse point,
  owner-input copy, or emulator/build process. An independently guarded second ranked trial
  reproduced those aggregates exactly: one capture with four accepted aggregate rows, split evenly
  between EE-read and anonymous-site rows, zero VIF1 rows, and zero aggregate-count deltas. In both
  trials, the EE-read rows remain confined to the already-opaque header-vector block; no accepted
  row reaches counts, records, metadata, payload, VIF, or tail data. This repeated header-only
  evidence does not exclude copied buffers or activity outside the bounded observation window and
  assigns no geometry, topology, vertex, material, packet, draw, placement, visibility, or gameplay
  semantics.
- Passive SKM and SKL native descriptors preserve only bounded structural metadata. The passive
  SKA descriptor implements the aggregate-proven neutral counted-word extent as fixed output while
  the two-candidate SKAS text envelope remains separate; neither assigns animation semantics. The
  native corpus verifier accepts 213/213 SKA spans with zero errors: 158 exact, 55 zero-padded, and
  2,180,832 aggregate logical bytes.
- Bounded POP post-terrain scanning accepts 18/18 level POPs with zero errors and finds 19 aligned
  literal-tag candidates exactly once per file in one shared order, for 342 aggregate hits. The
  literals are not yet decoded section boundaries and assign no placement or visibility semantics.
- Aggregate POP layout scoring finds five marker-relative `+4` word/fixed-stride tuples that fit
  all 18 occurrences each (`INL:`/36, `PNT:`/88, `DIR:`/44, `ENV:`/76, and `INV:`/84 bytes).
  Zero-count instances validate only the predicted empty extent. The tuples remain arithmetic
  hypotheses, not decoded sections or field semantics.
- Bounded POP candidate-shape profiling accepts those five guarded formulas across all 18 POPs and
  measures 8,019 candidate records and 105,985 anonymous four-byte column observations. The output
  is aggregate bit-pattern classification only and assigns no field type or runtime semantics.
- The retail-only native POP hypothesis descriptor fail-closed checks the established ordered
  aligned-literal envelope and five arithmetic extent hypotheses while retaining only fixed owned
  hypothesis metadata. Its fixed-schema verifier discovers and accepts all 18 owned-corpus
  candidates with zero rejections or errors; independent accepted-only
  input/items/logical-output/string/scratch maxima are `919360 / 1 / 168 / 26 / 80036`. It is not
  canonical IR and confirms no section boundary, count, record, payload, placement, visibility,
  rendering, or gameplay semantics.
- Inspectors for ELF, scripts, textures, meshes, skeletons, animation, audio, and maps.
- Synthetic malformed-input tests for every decoder.

## M2: Native shell

Status: in progress. The SDL_GPU shell, strict launch parser, owner-supplied NTSC-U data-root
validation, headless probe, named-level manifest/content load plus texture-locator inventory, and
deterministic synthetic canonical-COL wireframe contact sheet are implemented. The contact sheet
places meshes in source-order tiles and projects each mesh along its two largest coordinate extents. It is a
clean-room diagnostic, not world placement or reconstructed geometry, and makes no VUM, TDX, or
other retail semantic claim. Headless named-level startup owns the complete canonical spatial and
material collections plus an inventory-only texture store. It does not load texture storage, bind
materials, expand display pixels, upload GPU resources, or render textures.
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
loading files or retail data. Concrete components, gameplay systems, render scene snapshots,
decoded voices, and mixing remain incomplete.

The simulation world now solely owns a bounded, preallocated generational entity registry. Entity
creation/reuse is deterministic for identical call sequences, stale or nonmatching generations are
inert, and capacity exhaustion is explicit. The world now exposes creation, destruction, liveness,
and aggregate identity snapshots without releasing a mutable registry reference; destruction is
the reserved in-place coordination point for future direct component cleanup. Complete world
ownership remains move-constructible, while move assignment is deleted so replacing a live world
cannot bypass that coordination point. A reusable header-only `ComponentStore<T>` foundation
provides bounded startup allocation, exact-generation access, constant-time same-slot replacement
of stale payloads, explicit exact-generation retained cleanup, inert moved-from behavior, and
aggregate-only snapshots for future direct world-owned stores.
Unrelated retained payloads are not swept during insertion and fail capacity closed until cleanup.
No speculative gameplay component is instantiated; concrete components and systems remain future
project-owned work. Entity and component capacities are synthetic host limits, not retail
population claims.

Each rendered frame now crosses an explicit owned `RenderFramePacket` boundary containing the host
frame index, deterministic world clock, and live-entity count. The SDL host consumes it
synchronously and still renders only the content-free diagnostic; component/render scene snapshots
remain future project-owned work.

- Window, input, logging, configuration, jobs, renderer, audio device, and frame scheduler.
- Load the retail data tree supplied by the owner; clear diagnostics for missing/wrong region.
- Render a debug scene with no proprietary data embedded in the executable.

## M3: First level scene

Status: in progress. Native `--level=MINSK` selection, canonical manifest/spatial loading, and
renderer-neutral texture storage decoding are complete; the current synthetic canonical-COL
wireframe contact sheet is diagnostic only. It uses source-order tiles and a per-mesh projection of
the two largest coordinate extents, not world placement or reconstructed geometry, and makes no
VUM, TDX, or other retail semantic claim. Startup now owns its spatial meshes and one confirmed
semantic VUM material/name catalog per manifest cell together in `LevelContentIR`. One archive pass
and one cumulative fail-closed budget preserve exact manifest order and cardinality without
asserting a mesh-to-material binding. After that content and the synthetic debug image succeed,
startup also owns the level's direct-TDX locator inventory; it performs no texture `Load` and asserts
no texture-to-name/material/cell/mesh/draw relationship. The names remain role-free and unbound:
render geometry, material binding/parameters, display-ready texture expansion, cameras, placements,
transforms, and visibility remain incomplete. A passive retail-only VUM descriptor preserves the proven
pair/reference grammar without asserting vertices, indices, draws, or material assignments, and
its payload does not enter the canonical level IR.

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
