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
|- InputService [main/game thread]
|- ScriptService [game thread]
|- SimulationWorld [game thread]
|- UiService [game thread build; render thread consume]
`- NetworkService [game thread API; I/O worker]
```

`OmegaApp` owns every service through `std::unique_ptr` and destroys them in reverse order.
Services never own the app or one another. Dependencies are constructor references whose
lifetime is guaranteed by the app. Long-lived asset references are typed generation handles,
not raw pointers or `shared_ptr` ownership graphs.

`GameDataService` is the implemented startup boundary. It owns its VFS, freezes mounts during
`Open()`, and returns only canonical owned IR. Future `AssetService` code receives a non-owning
reference; neither service owns the other.

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
  owns the frozen VFS, and maps named levels into canonical manifest values.
- `AssetService` maps paths to typed handles, performs async decode, and publishes immutable
  CPU assets before render/audio upload.
- `ScriptService` executes only project-owned native logic or declarative mission data. Retail
  executable/script modules are inspected offline and are never loaded as executable code.
- `SimulationWorld` uses a measured fixed step. The retail tick rate remains evidence-driven,
  not assumed from display refresh.
- `RenderService` receives scene snapshots and exposes no retail-format details.

## Hot reload

Research builds may hot-reload decoded assets, internal scripts, and mission compatibility
tables at frame boundaries. Platform, renderer, audio device, and network transport are
non-hot-reloadable initially. The validated retail-data root and its frozen mount table are also
non-hot-reloadable. No vtable pointer crosses a reloadable boundary.

## Dependency direction

```text
apps -> runtime -> content -> retail formats -> assets/core
                       \-> vfs -> core
gameplay -> simulation -> core
        \-> assets -> vfs -> core
renderer/audio/platform -> core
```

Platform backends and retail decoders are leaves. Core and simulation never include PCSX2,
Windows, GPU API, or proprietary-format implementation headers.

The initial native build targets express the same direction:

- `omega_core`: HOG indexing, VFS, and generic bounded infrastructure;
- `omega_assets`: canonical owned IR values and decode contracts;
- `omega_retail_formats`: stateless POP and later COL/VUM/TDX adapters that may depend on the
  first two targets;
- `omega_content`: the non-hot-reloadable data-root service and retail-to-canonical startup
  orchestration; and
- `omega_runtime`: launch options and renderer-neutral diagnostic scene values consumed by the
  SDL host.

The initial COL/VUM/TDX adapters are passive scalar descriptors. They validate only proven
container arithmetic and never expose VU/VIF instructions, palette guesses, or decoded pixels.

Tools may link retail adapters. Renderer and simulation targets must consume canonical assets and
must not include retail-format headers. A source-include dependency check will turn this convention
into a CI enforcement boundary as more targets appear. The existing terrain-prefix parser remains
in `omega_core` temporarily; new semantic adapters enter through `omega_retail_formats`.

The initial synthetic manifest grid consumes only `LevelManifestIR`. Its tile positions are not
retail world coordinates and are never claimed as decoded geometry.

The runtime contains no MIPS execution path. This boundary is permanent and documented in
`docs/adr/0001-pure-native-runtime.md`.

The initial host backend is SDL 3.4.10 for windowing, events, gamepads, audio streams, and the
modern GPU device. SDL is private to the platform/render/audio/input leaves, as documented in
`docs/adr/0002-sdl3-platform-layer.md`.
