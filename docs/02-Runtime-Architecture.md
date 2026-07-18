# Native runtime architecture

## Ownership

```text
OmegaApp [game thread, sole lifetime owner]
|- PlatformService [main thread]
|- GameDataService [owns frozen VirtualFileSystem]
|- JobService [worker pool owner]
|- AssetService [game thread API; worker decode]
|- RenderService [render thread]
|- AudioService [game thread API; audio callback]
|- SdlInputService [main thread; SDL event/gamepad owner]
|- ScriptService [game thread]
|- SimulationWorld [game thread]
|- UiService [game thread build; render thread consume]
`- NetworkService [game thread API; I/O worker]
```

`OmegaApp` owns every service through `std::unique_ptr` and destroys them in reverse order.
Services never own the app or one another. Dependencies are constructor references whose
lifetime is guaranteed by the app. Long-lived asset references are typed generation handles,
not raw pointers or `shared_ptr` ownership graphs.

The initial composition root now owns the validated configuration store, content startup state,
stderr/ring log sinks, logging service, worker pool, fixed-step scheduler, input tracker,
`SimulationWorld`, SDL process-global platform service, SDL input service, SDL audio service, and
SDL GPU host in that order. The host is created last and destroyed first; audio and input stop
before the platform service calls the process-global SDL shutdown.
`OmegaApp::Run` owns the steady clock, closes immutable input frames, asks the scheduler for a
bounded plan, executes every planned world step, then submits one render frame.
`SdlInputService::PumpEvents` is the sole process-global event-queue consumer. `SdlGpuHost` owns
only video, window, GPU, and rendering resources; no SDL type crosses into the platform-neutral
runtime or simulation libraries.

`GameDataService` is the implemented startup boundary. It owns its VFS, freezes mounts during
`Open()`, and returns only canonical owned IR. It now resolves each manifest cell HOG and its unique
COL member into one `SpatialMeshIR` in manifest order; HOG objects, byte spans, and retail offsets
remain local to the call. Future `AssetService` code receives a non-owning reference; neither
service owns the other.

## Components and services

Entity components are plain state: transform, renderable, skeleton pose, health, inventory,
controller, AI state, mission tags, and network identity. Services own lifecycle-heavy
resources: files, jobs, GPU objects, audio voices, input devices, scripts, and sockets.

The simulation operates on components through systems. Platform and resource services do not
contain mission rules.

## Thread contract

- **Game thread:** deterministic simulation, scripts, AI, mission state, and entity mutation.
- **Render thread:** GPU device/queue/resources and immutable frame packets.
- **Worker pool:** file reads, decompression, parsing, and CPU-side asset preparation.
- **Audio callback:** consumes lock-free commands; never loads files or blocks.
- **Network I/O:** receives packets into queues; game-thread code applies state changes.

Every public API carries a thread-affinity comment. Debug builds will add thread assertions as
each service becomes real. Cross-thread ownership transfers use immutable packets or explicit
queues.

## Initial contracts

- `VirtualFileSystem` mounts physical directories, ISO views, and HOG archives behind
  normalized case-insensitive game paths.
- `GameDataService` validates the owner-supplied NTSC-U root from bounded `SYSTEM.CNF` metadata,
  owns the frozen VFS, and maps named levels into canonical manifest and spatial-mesh values.
- `AssetService` maps paths to typed handles, performs async decode, and publishes immutable
  CPU assets before render/audio upload.
- `ScriptService` executes only project-owned native logic or declarative mission data. Retail
  executable/script modules are inspected offline and are never loaded as executable code.
- `SimulationWorld` advances only from explicit fixed-step calls and owns deterministic completed-
  step/simulated-time state. The composition root supplies the scheduler's validated step; its
  current default is a synthetic-shell value, while the retail tick rate remains evidence-driven.
- `RenderService` receives scene snapshots and exposes no retail-format details.
- `SdlInputService` is an app-owned, non-hot-reloadable main-thread leaf. It owns the ref-counted
  SDL gamepad subsystem, pumps the global SDL event queue, and owns at most one primary gamepad.
  Button events are accepted only when their instance ID matches that primary. Window focus loss
  reconciles all neutral controls; primary disconnect instead reconciles only `GamepadButton`
  controls to up, preserving keyboard and mouse state, then promotes the next available gamepad.
  Choosing one primary is a synthetic host-shell policy, not a retail behavior claim. A
  deterministic headless virtual-gamepad regression covers attach/open, filtered button edges,
  disconnect reconciliation, and promotion without a window or physical controller.
- `AudioService` owns a system-default SDL playback stream. Its first callback supplies bounded,
  frame-aligned project-owned silence from a fixed buffer and publishes only lock-free diagnostic
  counters. The 48 kHz stereo F32 source format is a native engineering choice, not a retail claim;
  decoded voices and mixing remain future clean-room work.

## Hot reload

Research builds may hot-reload decoded assets, internal scripts, and mission compatibility
tables at frame boundaries. Platform, renderer, input device/event pump, audio device, and network
transport are non-hot-reloadable initially. The validated retail-data root and its frozen mount
table are also non-hot-reloadable. No vtable pointer crosses a reloadable boundary.

## Dependency direction

```text
apps -> runtime -> content -> retail formats -> assets/core
   |                   \-> vfs -> core
   \-> simulation
gameplay -> simulation -> core
        \-> assets -> vfs -> core
renderer/audio/platform -> core
```

Platform backends and retail decoders are leaves. Core and simulation never include PCSX2,
Windows, GPU API, or proprietary-format implementation headers.

The initial native build targets express the same direction:

- `omega_core`: HOG indexing, VFS, and generic bounded infrastructure;
- `omega_assets`: canonical owned IR values and decode contracts;
- `omega_simulation`: platform-neutral deterministic world state and fixed-step execution;
- `omega_retail_formats`: stateless POP/COL/VUM/TDX/SKM/SKL/SKA adapters that may depend on the
  first two targets;
- `omega_content`: the non-hot-reloadable data-root service and retail-to-canonical startup
  orchestration;
- `omega_runtime`: launch/configuration services and renderer-neutral diagnostic scene values
  consumed by the composition root and SDL host; and
- `omega_sdl_backend`: the non-hot-reloadable SDL platform, audio, input translation, and GPU leaf.

VUM has a bounded semantic adapter that returns owned source-order names plus one-to-three dense
name indices per material. A separate retail-only passive descriptor preserves only the three
bounded payload regions, source-order Q/P pairs, normalized T target ordinals, observed middle-span
families, and opaque final-region-relative references. It is evidence scaffolding, not canonical
asset IR, and renderer or simulation targets must not include it. COL has a bounded semantic
adapter that returns neutral owned spatial-mesh IR: source coordinates and topology are preserved
while winding,
collision behavior, transforms, materials, opaque primitive words, and trailing payload remain
unassigned. TDX has a separate bounded `TextureStorageIR` adapter that owns source-order blocks,
transfer planes, and four-byte palette entries while leaving block purpose, mip meaning, channel
order, alpha conversion, nibble order, palette permutation, swizzle, and GPU upload unassigned.
None of these adapters exposes VU/VIF instructions or decoded pixel guesses.

SKA has a separate retail-only passive descriptor rather than canonical animation IR. Its fixed
output contains only the observed version/count words and the computed 112-byte-prefix
counted-word extent, classified as exact or followed by zero padding. It retains no input span and
assigns no payload, animation, timing, channel, transform, compression, or bone semantics. SKAS
remains a separate two-candidate text-evidence family with no native descriptor.

VUM catalog decoding and passive payload inspection are stateless and need no dynamic scratch.
They share fail-closed validation of the proven prefix, counted extents, P/Q/T relationships,
middle-span families, and ordered reference grammar. Each preflights its exact owned output and
item count before allocation. Payload bytes, packet words, opcodes, registers, microprograms,
vertices, indices, draws, and material assignments never cross either adapter. Render code will
consume only a future independently proven render-mesh IR.

TDX storage decoding is flat and stateless. It debits input once, preflights exact owned vector and
payload bytes, uses fixed local layout records, and retains no input span. Sixty-two single-plane
assets use a narrowly allowlisted implicit-zero suffix normalization backed by complete duplicate
twins; this diagnostic provenance is not part of renderer-neutral IR. Render code consumes only a
future independently validated expansion result and never includes the retail decoder header.

`LoadLevelSpatial` composes the outer DATA.HOG, any container-only source chain, every referenced
cell HOG, and every COL decoder under one operation budget. Input work and item counts are
cumulative, logical output includes every owned mesh/vector payload, semantic-adapter scratch is a
reusable peak, and nesting depth combines archive edges with COL tree edges. HOG input/copy/parser
workspace is bounded independently by configured archive byte caps and the parser's fixed
directory/count/name limits. The default depth is nine: one cell HOG edge plus the corpus maximum
eight-edge COL tree. The returned `LevelSpatialIR` has the same order and cardinality as
`LevelManifestIR::terrain_cells`; provenance remains in the manifest.

Tools may link retail adapters. Renderer and simulation targets must consume canonical assets and
must not include retail-format headers. A source-include dependency check will turn this convention
into a CI enforcement boundary as more targets appear. The existing terrain-prefix parser remains
in `omega_core` temporarily; new semantic adapters enter through `omega_retail_formats`.

Startup owns both `LevelManifestIR` and `LevelSpatialIR`. The initial renderer consumes canonical
spatial meshes only to build a deterministic synthetic canonical-COL wireframe contact sheet.
Meshes occupy source-order tiles, and each mesh is projected along its two largest coordinate
extents. This clean-room diagnostic is not world placement or reconstructed geometry and makes no
VUM, TDX, or other retail semantic claim.

The runtime contains no MIPS execution path. This boundary is permanent and documented in
`docs/adr/0001-pure-native-runtime.md`.

The initial host backend is SDL 3.4.10 for windowing, events, gamepads, audio streams, and the
modern GPU device. SDL is private to the platform/render/audio/input leaves, as documented in
`docs/adr/0002-sdl3-platform-layer.md`.
