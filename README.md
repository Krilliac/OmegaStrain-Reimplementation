# OpenOmega

[![Native CI](https://github.com/Krilliac/OmegaStrain-Reimplementation/actions/workflows/native-ci.yml/badge.svg)](https://github.com/Krilliac/OmegaStrain-Reimplementation/actions/workflows/native-ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

OpenOmega is a clean-room, pure-native compatibility reimplementation research project for
*Syphon Filter: The Omega Strain* (NTSC-U, `SCUS-97264`). PCSX2 is used as a behavioral
oracle and debugger; it is not intended to be a dependency of the eventual native runtime.
The shipping runtime is pure modern-CPU native code: no MIPS interpreter, recompiler, translated
retail instruction blocks, or PS2 execution layer.

## Current verified baseline

- Official PCSX2 `master` built from commit `86d76bbf590566d9ea74d381eeff3acd9856503a`.
- Build output: `third_party/pcsx2/bin/pcsx2-qtx64-avx2.exe` (PCSX2 2.7.485).
- Official Windows dependency archive SHA-256:
  `9C02B2CF15DA632B7D862131486B6CD8D7C58979D5F8C345F0BA4E0E2AEC3FD1`.
- Owned NTSC-U disc image SHA-256:
  `A60A4B41FEA808335BAA3B887A377BB98063F6F169CA498EFEE49397FBA96130`.
- Boot verified with USA BIOS v1.60, serial `SCUS-97264`, CRC `D5605611`,
  ELF entry point `0x00100008`, and D3D12 rendering.
- The disc is extracted under the ignored `private/extracted-disc/` tree.
- All 273 top-level HOG archives validate against the documented directory layout: 32,351
  contained entries and zero structural errors.
- The native range reader validates all 6,677 nested HOG spans: 501 exact and 6,176 with
  verified all-zero sector tails.
- The first native scene importer validates the terrain prefix of all 18 level POP files:
  5,351 records, including 4,144 with nonzero alignment bytes that are safely skipped.
- A retail-only passive POP post-terrain descriptor fail-closed checks the already-established
  ordered 19-candidate aligned-literal envelope and five exact arithmetic extent hypotheses without
  promoting any literal, word, stride, or opaque range to section, count, record, or payload
  semantics. Its fixed-schema native verifier discovers and accepts all 18 owned-corpus `.POP`
  candidates with zero rejections or errors. Independent accepted-only
  input/items/logical-output/string/scratch maxima are `919360 / 1 / 168 / 26 / 80036`; no tuple
  co-occurrence is asserted. The descriptor is fixed owned hypothesis metadata, not canonical asset
  IR, and the report exposes no source identity or descriptor observation.
- The native host validates an owner-supplied NTSC-U data root, loads MINSK as 299 canonical
  manifest cells with matching owned spatial meshes, and renders a deterministic synthetic
  canonical-COL wireframe contact sheet through SDL_GPU/D3D12.
- The native host opens and resumes the system-default SDL playback stream as 48 kHz stereo F32;
  its fixed-buffer callback supplies project-owned silence until clean-room audio decode and
  mixing land, with a deterministic dummy-device test covering the callback boundary.
- An app-owned, non-hot-reloadable SDL input leaf owns the gamepad subsystem, the process-global
  event pump, and one primary gamepad. It filters controller events by SDL instance ID, resets only
  gamepad controls when that device disconnects, and promotes the next available device. A
  deterministic headless virtual-gamepad test covers that hotplug boundary; selecting only one
  primary gamepad is a synthetic host-shell policy, not a claim about retail behavior.
- The native content service resolves all 5,351 manifest cells across all 18 levels into 5,351
  owned spatial meshes with zero errors: 20,203 canonical nodes, 93,356 leaves, 889,640 vertices,
  1,239,980 triangles/references, and 2,137 normalized empty meshes.
- The native TDX adapter converts all 15,248 texture assets into owned renderer-neutral storage:
  15,442 source-order blocks, 17,960 primary planes, 285,521,272 primary bytes, 15,190 palette
  blocks, and zero errors. It normalizes 4,112 duplicate-proven implicit zero bytes without
  guessing pixel or channel layout.
- A bounded common-archive containment scan accepts all 18 runtime levels, 5,351 manifest cell
  occurrences, and 5,413 scanned archive-directory occurrences with zero errors and finds zero
  normalized `.TDX`-suffixed members. This extension-bounded negative result does not exclude
  embedded texture data or assign ownership or a texture-to-cell/material/mesh relationship.
- A separate direct-only scan accepts both explicit sibling container classes for every
  runtime level: 36/36 exact containers and 5,801/5,801 direct TDX members with zero errors,
  collisions, malformed textures, nested traversal, or non-TDX members. The two role classes
  contribute 5,765 and 36 occurrences. This establishes direct containment only, not retail
  ownership, necessity, priority, or a texture-to-material/cell/mesh binding.
- The public-safe native level-texture verifier accepts all 18 runtime levels, 36 explicit sources,
  and 5,801 level-inventory texture occurrences with zero errors. It loads 5,913 storage blocks,
  7,603 planes, 615,232 palette entries, and 29,562,280 owned storage bytes. Its independent
  field maxima are Open input/items/logical-output/depth/scratch
  `3,076,944 / 1,460 / 111,014 / 0 / 71,467`
  and Load `3,139,344 / 5,169 / 333,232 / 0 / 65,595`; these maxima are fieldwise and are not
  asserted to co-occur. Internal store defaults are 4 MiB input, 512 KiB logical output, 128 KiB
  scratch, 8,192 items, 4 KiB strings, and depth one. The measured byte/item fields are rounded
  independently to the next binary boundary above the larger Open/Load maximum; depth one is the
  smallest nonzero headroom above measured depth zero. These values are not runtime configuration
  or `--set` keys. The verifier emits no paths, names, hashes, offsets, payloads, per-level rows,
  identities, or bindings.
- E-0043 adds the bounded asynchronous native `AssetService` v0. `OmegaApp` owns it optionally only
  when startup has a `LevelTextureStore`; fixed reusable generation handles and explicit release
  govern a preallocated slot pool, while accepted `JobService` work retains a shared implementation
  through worker return and teardown expires the public service identity. A clean MSVC build produced
  zero warnings and errors, the focused checks and full 18/18 CTest suite passed, and 100 repeated
  lifecycle-test runs passed. Two owned-tree verifier passes produced byte-identical schema-version-1
  reports over 18 levels, 36 sources, and 5,801 texture occurrences with zero errors. Requests,
  `Ready` states, successful `Get` calls, releases, stale-handle rejections, and zero-residual checks
  each total 5,801. Loaded storage totals are 5,913 blocks, 7,603 planes, 615,232 palette entries,
  27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 owned bytes; maximum active slots,
  in-flight requests, and resident logical bytes are `1 / 1 / 333,232`. The verifier uses one worker,
  one pending job, and service limits of one slot, one in-flight request, and 524,288 resident logical
  bytes. Runtime defaults are 64 slots, 64 in-flight requests, and 64 MiB resident logical output,
  with a hard 8,192-slot maximum. These bounds are synthetic project policy, not retail limits or
  user settings. The service performs no VUM-name or material lookup, alias resolution, binding,
  display-pixel expansion, GPU upload, placement, visibility, or rendering. Its fixed aggregate
  report exposes no paths, names, hashes, offsets, payloads, per-level rows, identities, bindings,
  messages, or exception text; the unchanged E-0038 store verifier was revalidated separately.
- E-0044 adds the SDL-free bounded `RenderTexturePool` foundation for project-owned tightly packed
  RGBA8 images. It preallocates a fixed slot pool, checks the exact `width * height * 4` extent with
  overflow protection, and separates backend work into transactional `Reserve`, `Publish`, and
  `Rollback` phases. Unique nonzero process-local pool identities plus slot and 64-bit generation
  fields reject default, foreign, stale, and released handles; explicit release refunds logical
  resident bytes, and a maximum generation retires rather than wraps. Defaults are 64 slots and
  64 MiB of logical RGBA8 bytes with a hard 8,192-slot maximum. `RenderFramePacket` now carries one
  default-invalid fixed-width diagnostic handle while remaining trivially copyable and standard
  layout. A
  clean MSVC build produced zero warnings and errors, the focused test passed, 100 repeated focused
  runs passed, and the full 19/19 CTest suite passed. This is renderer-neutral metadata and lifecycle
  policy only: it creates no GPU resource, and neither `SdlGpuHost` nor `OmegaApp` consumes the new
  handle yet. The existing one-off debug-image upload remains unchanged; no GPU upload, blit, or
  residency is validated, and no `AssetService`, TDX, VUM, material, binding, or display semantic is
  connected.
- The native VUM adapter converts all 7,036 material catalogs into owned neutral data: 38,793
  source-order names, 38,899 material records, and 42,631 dense name references with zero errors.
  Level-wide service orchestration independently loads the 5,351 manifest-referenced catalogs
  across all 18 levels with zero errors: 34,267 owned names, 34,589 material records, and 37,893
  dense references in exact manifest order.
  A separate retail-only passive descriptor validates 91,460 payload pairs, 38,023 normalized
  targets, 134,122 middle-to-final references, and 365,840 ordered Q/P references without
  exposing payload bytes, render geometry, or console instructions.
- A bounded analysis-only lexical-coherence pass compares complete normalized VUM member-name
  occurrences only against exact complete direct locator strings when the VUM name ends `.TDX`.
  Two passes are byte-identical, scan 18/18 levels with zero errors, and exactly reproduce the native
  cell/catalog/name/material/reference and container/locator populations. All 34,267 VUM name
  occurrences and 37,893 dense references classify as non-`.TDX`; no name enters the eligible
  exact-candidate lookup branch, and all 5,801 class-qualified locators remain unreached. This narrow
  negative result tests no alias family, observes no native or retail lookup, establishes no texture
  role, material binding, source priority, or runtime integration, and exposes no identities or
  proprietary data.
- A separate exact-first one-terminal-extension pass keeps that direct `.TDX` locator universe
  unchanged. After complete normalization, it independently removes at most one syntactic extension
  from the final component on both sides only when the full strings are not already equal; directory
  components and original class-qualified locators are preserved. Two passes are byte-identical and
  scan 18/18 levels with zero errors. All 34,267 VUM name occurrences and 37,893 dense references are
  extension-elided unique-primary candidates; all 34,589 material records inherit a unique
  extension-elided candidate, while 5,690 original locator occurrences are reached only through
  extension elision and 111 remain unreached. This offline lexical result does not observe a retail
  alias rule or material consumption, establish a texture role or binding, assign class priority, or
  justify runtime integration, and it exposes no identities or proprietary data.
- The first privacy-safe VUM consumer trace has one strict-validator-accepted complete 120-frame pair
  from one selected runtime copy. Its repeat is byte-identical; each validated report contains two
  EE-read aggregate rows, two anonymous-site rows, and zero VIF1 chunk rows. Post-run containment
  auditing found no retained runtime copy, executable surface, reparse point, owner-input copy, or
  emulator/build process. An independently guarded second ranked trial reproduced those aggregates
  exactly: one capture with four accepted aggregate rows, split evenly between EE-read and
  anonymous-site rows, zero VIF1 rows, and zero aggregate-count deltas. In both trials, the EE-read
  rows remain confined to the already-opaque header-vector block; no accepted row reaches counts,
  records, metadata, payload, VIF, or tail data. This repeated header-only observation does not
  exclude copied buffers or activity outside the bounded observation window and assigns no geometry,
  topology, vertex, material, packet, draw, placement, visibility, or gameplay semantics.

## Quick start

```powershell
powershell -NoProfile -File .\scripts\launch-omega.ps1
```

Open the PCSX2 debugger and break at the game entry point:

```powershell
powershell -NoProfile -File .\scripts\launch-omega.ps1 -Debugger
```

Load the recovered resume state:

```powershell
powershell -NoProfile -File .\scripts\launch-omega.ps1 -Resume
```

Print the exact command without launching another emulator instance:

```powershell
powershell -NoProfile -File .\scripts\launch-omega.ps1 -Debugger -DryRun
```

Static analysis confirms that `-lMINSK` is valid attached argument syntax and selects the MINSK
entry. The user-facing meaning of `-x` and the resulting gameplay state remain unverified; the
launcher can test the pair explicitly against the private reference environment:

```powershell
powershell -NoProfile -File .\scripts\launch-omega.ps1 -Debugger -GameArgs '-x -lMINSK'
```

## Native runtime build

The native runtime is C++23 with bounded content intake and an SDL3/SDL_GPU host. Configure inside
a VS2022 developer environment, then use the checked-in multi-config presets:

```powershell
cmake --preset msvc
cmake --build --preset msvc-debug
ctest --preset msvc-debug
.\build\msvc\Debug\omega_tool.exe hog-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe hog-verify-nested-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe pop-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe pop-post-terrain-hypotheses-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-manifest-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-spatial-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-material-catalogs-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-texture-store-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe asset-metadata-verify-tree .\private\extracted-disc
.\build\msvc\Debug\openomega.exe --data-root=.\private\extracted-disc --level=MINSK --probe-only
.\build\msvc\Debug\openomega.exe --data-root=.\private\extracted-disc --level=MINSK --frames=120
python -B .\tools\probe_native_levels.py .\build\msvc\Debug\openomega.exe .\private\extracted-disc --aggregate-only
.\build\msvc\Debug\openomega.exe --frames=120
.\build\msvc\Debug\openomega.exe --config=.\openomega.cfg --set=log.minimum_severity=debug --frames=120
```

`openomega` is the pure-native SDL3/SDL_GPU host shell. `--frames=N` is an automated smoke mode
that opens the modern GPU backend, renders exactly `N` frames, and exits without user input.
`--probe-only` validates the retail root and selected level, then loads the owned manifest plus one
all-or-error `LevelContentIR` and opens an inventory-only `LevelTextureStore` without opening a
window. The store is retained only after the existing content and debug-image gates succeed; startup
does not call `Load`, expand display pixels, bind materials, upload to the GPU, or render textures.
The spatial meshes and role-free material catalogs are decoded under one shared budget while each
common/cell archive is traversed once; their parallel manifest order asserts no mesh-to-material
binding. The current MINSK view is a deterministic synthetic
canonical-COL wireframe contact sheet: meshes occupy source-order tiles, and each mesh is projected
along its two largest coordinate extents. This clean-room diagnostic is not world placement or
reconstructed geometry and makes no VUM, TDX, or other retail semantic claim. The app-owned loop
converts steady-clock deltas into bounded fixed-step plans and executes each step against a
platform-neutral deterministic `SimulationWorld`; the configured default remains a synthetic-shell
timing value, not a retail-rate claim. The app also owns a non-hot-reloadable SDL audio service whose
callback never reads files, logs, blocks, or touches retail data; the current silent stream proves
device lifecycle and thread plumbing without embedding or guessing proprietary audio. A separate
non-hot-reloadable `SdlInputService` is the sole global event-queue consumer through `PumpEvents`,
owns the gamepad subsystem and primary-gamepad lifetime, and translates accepted events into the
neutral input tracker. `SdlGpuHost` is consequently limited to video, window, GPU, and rendering
resources.

The optional project-owned configuration file uses strict `lower_snake_case` dotted keys and
`key = value` lines. `--set=KEY=VALUE` applies one validated command-line override per key. Current
keys are `log.minimum_severity`, `log.ring_capacity`, `jobs.worker_count`,
`jobs.max_pending_jobs`, `frame.simulation_step_ns`, `frame.max_steps_per_frame`,
`frame.max_delta_ns`, and `input.max_events_per_frame`. Frame defaults are synthetic host-shell
engineering values, not claims about the retail tick rate.

Architecture and completion criteria are versioned in
[`docs/02-Runtime-Architecture.md`](docs/02-Runtime-Architecture.md) and
[`docs/03-Milestones.md`](docs/03-Milestones.md). Research confidence is tracked in
`analysis/evidence/ledger.jsonl`.

## Layout

- `docs/` — handoff and research notes.
- `tools/` — deterministic disc/ELF archaeology tools.
- `analysis/` — generated, redistributable metadata and reports.
- `analysis/formats/HOG.md` — validated HOG container layout and extraction notes.
- `analysis/formats/POP.md` — validated terrain-prefix contract, passive post-terrain hypothesis
  boundary, and native verifier.
- `analysis/formats/TDX.md` — validated texture block, plane, palette, and zero-suffix contract.
- `analysis/formats/VUM.md` — validated material catalog and render-payload boundary contract.
- `analysis/elf/loader-hints.md` — confirmed executable evidence and open loader questions.
- `third_party/pcsx2/` — ignored official PCSX2 checkout and build.
- `runtime/` — ignored isolated PCSX2 data/configuration and logs.
- `private/` — ignored owned BIOS, disc image, extracted assets, and save states.

## Legal boundary

The repository must never contain Sony firmware, a game ISO, extracted proprietary assets,
save states, or keys. Contributors provide their own legally dumped PS2 BIOS and retail disc.
Only original source code, tools, documentation, hashes, metadata, and independently created
compatibility data belong in version control.

This is an unofficial project and is not affiliated with Sony Interactive Entertainment or
Bend Studio. See [`TRADEMARKS.md`](TRADEMARKS.md) and [`CONTRIBUTING.md`](CONTRIBUTING.md).

Original project code and documentation are licensed under Apache-2.0. That license does not
grant rights to any retail game, firmware, asset, or third-party trademark.
See [`docs/04-Legal-and-Takedown-Readiness.md`](docs/04-Legal-and-Takedown-Readiness.md) for the
project's preventive controls and response process. Dependency licenses are recorded in
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

Official references: [PCSX2 source](https://github.com/PCSX2/pcsx2),
[BIOS dumping](https://pcsx2.net/docs/setup/bios/), and
[disc dumping](https://pcsx2.net/docs/setup/discs/).
