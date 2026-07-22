# Debugging OpenOmega on Windows

This guide covers the native OpenOmega executable and its independently written
libraries. PCSX2 remains a separate research reference and is not part of any
native launch or debugging workflow described here. Keep owner-supplied data,
configuration, captures, and dumps outside version control.

## What this productization slice provides

The current Visual Studio integration provides:

- a `vs2022-x64` configure preset that generates a Visual Studio 2022 x64
  solution in `build/vs2022-x64`;
- one Visual Studio project for every buildable CMake target, organized into
  Products, Engine, Compatibility, Platform, Tests, and Developer Utilities
  solution folders;
- `openomega` as the default startup project when the native runtime is enabled,
  with `omega_tool` as the runtime-disabled fallback;
- stable product locations at
  `build/<preset>/products/game/<Config>/openomega.exe` and
  `build/<preset>/products/sdk/<Config>/omega_tool.exe`, with linker PDBs beside
  the executables when the selected configuration emits them;
- checked-in normal-play and Visual Studio launchers; and
- debugger commands on buildable library projects that select a representative
  executable host.

The library projects also set a Debug-only
`OPENOMEGA_DEBUG_BREAK_SUBSYSTEM=<name>` environment variable. That variable is
deliberately **inert in this slice**: no native code consumes it yet, so selecting
a library as the startup project launches its host but does not automatically
break at the subsystem's first entry. The eventual hook must break only on a
real call into that subsystem, exactly once, and remain absent from non-Debug
builds.

The following are useful workflows, but are **not yet project-integrated
features**: wait-for-debugger startup, persistent capture files and automatic
first-divergence breaks, automatic crash-dump capture, checked-in PIX or
RenderDoc launch presets, dedicated performance presets, and debug-only failure
injection. The sections below distinguish the external workflows available now
from those forthcoming project hooks.

## Configure, build, and open Visual Studio

From a normal PowerShell prompt at the repository root:

```powershell
cmake --preset vs2022-x64
cmake --build --preset vs2022-game-debug
cmake --open build/vs2022-x64
```

`Open-OpenOmega-VS.cmd` performs the configure and open steps. It intentionally
does not build. For a complete Debug solution build, use:

```powershell
cmake --build --preset vs2022-debug
```

For a symbolized optimized build, use:

```powershell
cmake --build --preset vs2022-relwithdebinfo --target openomega
```

The Visual Studio generator has its own binary directory. Do not point it at
`build/msvc`, which belongs to Ninja Multi-Config.

## Normal play

After building, double-click `Play-OpenOmega.cmd` or run it from PowerShell:

```powershell
.\Play-OpenOmega.cmd
```

The launcher never builds implicitly. If the requested binary is absent, it
prints the exact build commands and expected locations. It starts the game with
the repository root as the working directory and preserves the game's exit
code. Keyboard and mouse are the normal input path; a gamepad is not required.

The Debug Visual Studio product is directly available at:

```text
build/vs2022-x64/products/game/Debug/openomega.exe
```

## Debug the game

1. Open `build/vs2022-x64/OmegaStrainReimplementation.sln` or run
   `Open-OpenOmega-VS.cmd`.
2. Select the `Debug` solution configuration.
3. Set `openomega` as the startup project if it is not already selected.
4. Set breakpoints and press F5.

The generated project uses the repository root as its working directory. It
does not embed or discover an owner data path. Supply any private data-root or
configuration argument only in your local debugger settings, and never commit
that value.

## Deterministic scenarios

The existing bounded frame path is the best repeatable debugger scenario that
does not require proprietary content:

```powershell
.\build\vs2022-x64\products\game\Debug\openomega.exe `
  --frames=120 --capture-run --replay-capture
```

This records a bounded in-memory run, creates a fresh replay session in the same
process, and checks final scheduler, simulation, front-end, and diagnostic state.
It does not write a reusable capture file.

For owner-supplied content, pass paths locally:

```powershell
.\build\vs2022-x64\products\game\Debug\openomega.exe `
  --data-root=<owner-data-root> --level=MINSK --probe-only
```

Remove `--probe-only` and add `--frames=<count>` to exercise bounded rendering.
Do not put the real path in CMake, a checked-in `.vcxproj.user` file, screenshots,
issue text, or logs intended for publication.

## Debug a library project

Build the complete Debug solution once before pressing F5 on a library project.
The generated library project launches either `openomega` or a small
representative test executable through `VS_DEBUGGER_COMMAND`. A debugger command
does not create a reverse build dependency, and adding one would form a cycle
because the host already links the library.

Game-hosted library projects run a one-frame capture/replay scenario. Test-hosted
libraries run their representative test with no arguments. Today this is useful
for setting ordinary source breakpoints in that library. The generated
`OPENOMEGA_DEBUG_BREAK_SUBSYSTEM` value is reserved for the forthcoming one-shot
entry hook and has no behavior yet.

Header-only interface targets, including `omega_assets`, have neither object code
nor a runnable Visual Studio project and therefore cannot provide an entry
breakpoint. Third-party SDL targets are not instrumented.

## Tests and Test Explorer

The canonical complete Debug test command is:

```powershell
ctest --preset vs2022-debug
```

CTest remains authoritative even if the installed Visual Studio CMake tooling
also presents tests in Test Explorer. Every native test executable is a separate
Visual Studio project, so a focused test can also be built and run directly:

```powershell
cmake --build build/vs2022-x64 --config Debug `
  --target omega_diagnostic_mission_lifecycle_tests --parallel 1
.\build\vs2022-x64\Debug\omega_diagnostic_mission_lifecycle_tests.exe
```

Use the executable path reported by the build if a future target-specific output
rule relocates a test. Direct F5 execution does not inherit environment values
registered only on a CTest test; SDL audio tests, for example, may require
`SDL_AUDIO_DRIVER=dummy` in the local debugger environment.

## Attach to an existing game

Attaching is available now through Visual Studio's **Debug > Attach to Process**:

1. Start the interactive Debug game.
2. Attach the Native debugger to `openomega.exe`.
3. Confirm that symbols load from the PDB beside the executable.

Do not use a very small `--frames` limit when attaching manually because the
process may exit before attachment completes. There is no checked-in
`--wait-for-debugger` option yet. When implemented, it should be Debug-only,
bounded or explicitly cancellable, and visibly report that the process is
waiting rather than silently hanging.

## Capture/replay divergence

The available `--capture-run --replay-capture` path compares a fresh in-process
replay against the captured aggregate state and returns failure on mismatch.
Set breakpoints on the replay comparison diagnostics in
`native/apps/openomega/main.cpp` to inspect a current mismatch.

A persistent capture artifact, frame-by-frame binary search, and an automatic
break on the first divergent owner state are forthcoming. Until those exist,
do not describe the current in-memory replay as a general replay debugger or a
stable file format.

## Crashes and dumps

OpenOmega does not yet install an automatic dump handler. Available external
workflows are:

- break on thrown/unhandled C++ exceptions in Visual Studio and use
  **Debug > Save Dump As** while stopped; or
- if Sysinternals ProcDump is installed, launch under it with a private dump
  directory, for example:

  ```powershell
  procdump -ma -e -x D:\OpenOmegaDumps `
    .\build\vs2022-x64\products\game\Debug\openomega.exe
  ```

For a WinDbg first pass, use `!analyze -v`, `.ecxr`, `kb`, and `~*kb`. Full dumps
may contain owner data, paths, decoded assets, profile data, and other sensitive
process memory. Never commit or upload them to public issue trackers.

## Tracepoints and data breakpoints

Visual Studio tracepoints work without project code changes: create a normal
breakpoint, add an action/log message, and enable **Continue execution**. Prefer
tracepoints for high-frequency scheduler, input, and render transitions where a
stop would perturb timing. Conditional breakpoints and data breakpoints are also
available after the relevant object has a stable address.

These are debugger facilities, not deterministic telemetry. Important behavioral
claims still require a bounded test, capture/replay check, or sanitized aggregate
evidence.

## Natvis

`debugger/OpenOmega.natvis` contains conservative views for current front-end,
scheduler, entity, input, mission-lifecycle, capture-session, profile, and
character types. It references only fields that exist in the current source.

The Visual Studio CMake integration attaches this file to the generated
`openomega` project and places it in the **Debugger Visualizers** source group,
so Visual Studio loads it automatically when debugging the game. If a direct
test or an unusual attach session does not pick it up, load the file manually
through Visual Studio's debugger Natvis support. Keep Raw View available when
diagnosing a suspected visualizer error; Natvis changes display only and must
never be treated as runtime validation.

## PIX and RenderDoc

No checked-in GPU-capture preset exists yet. PIX and RenderDoc may be pointed at
the stable Debug or RelWithDebInfo `openomega.exe` as external launch targets.
Use a bounded `--frames` scenario and a repository-root working directory.

The Windows host currently exercises SDL GPU with Direct3D 12 in its validated
hardware path. A capture tool can alter timing, validation behavior, and device
selection, so reproduce any finding without the capture layer before treating it
as a runtime defect. Never capture or publish owner-supplied assets.

## Performance profiling

Use `RelWithDebInfo` for CPU or GPU profiling unless the issue depends on Debug
instrumentation. Fix the frame count, input path, level, window state, and data
root between comparisons. Visual Studio Performance Profiler and external GPU
profilers can launch the stable product executable now, but the repository does
not yet define benchmark scenes, budgets, baselines, or a dedicated performance
preset.

## Debug-only failure injection

There is no general failure-injection switch in the runtime. Current failure
coverage comes from focused tests and synthetic fixtures. A future injection
facility must:

- compile only into Debug builds;
- require an explicit named fault point;
- default off and fail closed on unknown names;
- remain deterministic and bounded; and
- never change Release or normal-player behavior.

Do not overload configuration keys, owner data, or the reserved subsystem-break
environment variable as an undocumented fault-injection mechanism.
