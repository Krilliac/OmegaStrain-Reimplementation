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
  contribute 5,765 and 36 occurrences; containment establishes neither runtime ownership nor a
  texture-to-material/cell/mesh binding.
- The native VUM adapter converts all 7,036 material catalogs into owned neutral data: 38,793
  source-order names, 38,899 material records, and 42,631 dense name references with zero errors.
  Level-wide service orchestration independently loads the 5,351 manifest-referenced catalogs
  across all 18 levels with zero errors: 34,267 owned names, 34,589 material records, and 37,893
  dense references in exact manifest order.
  A separate retail-only passive descriptor validates 91,460 payload pairs, 38,023 normalized
  targets, 134,122 middle-to-final references, and 365,840 ordered Q/P references without
  exposing payload bytes, render geometry, or console instructions.
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
.\build\msvc\Debug\omega_tool.exe level-manifest-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-spatial-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-material-catalogs-verify-tree .\private\extracted-disc
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
all-or-error `LevelContentIR` without opening a window. Its spatial meshes and role-free material
catalogs are decoded under one shared budget while each common/cell archive is traversed once;
their parallel manifest order asserts no mesh-to-material binding. The current MINSK view is a
deterministic synthetic
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
- `analysis/formats/POP.md` — validated terrain-prefix contract and native parser boundary.
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
