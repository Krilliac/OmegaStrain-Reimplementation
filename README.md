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
  manifest cells, and renders a deterministic synthetic coverage grid through SDL_GPU/D3D12.
- The native content service resolves all 5,351 manifest cells across all 18 levels into 5,351
  owned spatial meshes with zero errors: 20,203 canonical nodes, 93,356 leaves, 889,640 vertices,
  1,239,980 triangles/references, and 2,137 normalized empty meshes.
- The native TDX adapter converts all 15,248 texture assets into owned renderer-neutral storage:
  15,442 source-order blocks, 17,960 primary planes, 285,521,272 primary bytes, 15,190 palette
  blocks, and zero errors. It normalizes 4,112 duplicate-proven implicit zero bytes without
  guessing pixel or channel layout.
- The native VUM adapter converts all 7,036 material catalogs into owned neutral data: 38,793
  source-order names, 38,899 material records, and 42,631 dense name references with zero errors.
  It validates 220,943 bounded metadata records without exposing render packets or console
  instructions.

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
.\build\msvc\Debug\omega_tool.exe asset-metadata-verify-tree .\private\extracted-disc
.\build\msvc\Debug\openomega.exe --data-root=.\private\extracted-disc --level=MINSK --probe-only
.\build\msvc\Debug\openomega.exe --data-root=.\private\extracted-disc --level=MINSK --frames=120
python -B .\tools\probe_native_levels.py .\build\msvc\Debug\openomega.exe .\private\extracted-disc
.\build\msvc\Debug\openomega.exe --frames=120
```

`openomega` is the pure-native SDL3/SDL_GPU host shell. `--frames=N` is an automated smoke mode
that opens the modern GPU backend, renders exactly `N` frames, and exits without user input.
`--probe-only` validates the retail root and selected level, then loads matching owned manifest and
spatial-mesh state without opening a window. The current MINSK view is still a synthetic
manifest-coverage grid, not reconstructed world geometry.

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
