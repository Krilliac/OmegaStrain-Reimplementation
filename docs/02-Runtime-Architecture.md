# Native runtime architecture

## Ownership

```text
OmegaApp [game thread, sole lifetime owner]
|- PlatformService [main thread]
|- ContentStartupState [all-or-error level-content owner]
|  |- GameDataService [owns frozen VirtualFileSystem]
|  `- LevelTextureStore [optional immutable locator inventory]
|- JobService [worker pool owner]
|- AssetService [planned, unimplemented; game thread API; worker decode]
|- RenderService [render thread]
|- AudioService [game thread API; audio callback]
|- SdlInputService [main thread; SDL event/gamepad owner]
|- ScriptService [game thread]
|- SimulationWorld [game thread; non-hot-reloadable owner]
|  |- EntityRegistry [world-owned identity component]
|  `- ComponentStore<T> [future direct members; bounded plain state]
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
bounded plan, executes every planned world step, copies the clock and live-entity aggregate into an
owned renderer-neutral `RenderFramePacket`, then submits one render frame. The current host consumes
the packet synchronously; it contains no component pointers, retail views, SDL types, or vtables.
`SdlInputService::PumpEvents` is the sole process-global event-queue consumer. `SdlGpuHost` owns
only video, window, GPU, and rendering resources; no SDL type crosses into the platform-neutral
runtime or simulation libraries.

`GameDataService` is the implemented startup boundary. It owns its VFS, freezes mounts during
`Open()`, and returns only canonical owned IR. It resolves each manifest cell HOG and its unique COL
member into one `SpatialMeshIR`, and separately resolves the unique VUM member into one semantic
`MaterialCatalogIR`, both in manifest order and cardinality. HOG objects, byte spans, and retail
offsets remain local to each call. The catalog names retain no assigned role, and no COL triangle,
TDX asset, placement, transform, visibility, or draw binding is asserted. A future, currently
unimplemented `AssetService` will receive a non-owning reference; neither service will own the other.

`LevelManifestIR` also owns explicit `SourceLocator` values for the level's sibling `TEX.HOG` and
`MAPTEX.HOG` texture sources. These locators preserve source provenance only; their order assigns no
priority or binding. `LevelTextureStore` is implemented as a standalone immutable content value, not
as an app-owned service. It owns its canonical texture-locator inventory and a private store identity,
but retains only a weak source identity from the `GameDataService` that opened it. It owns no service,
VFS, HOG object, input byte span, material catalog, mesh, or renderer resource. Named-level startup
constructs and retains the store only after manifest, `LevelContentIR`, and debug-image success. The
service is declared earlier in the startup state and therefore outlives the bound store during reverse
destruction; no-level startup leaves the optional store disengaged.

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

`LevelTextureStore::Open` is a game-thread operation and requires that neither the store nor its
source service be moved or destroyed concurrently. After a successful Open, `size`, `HandleAt`, and
the immutable inventory may be queried concurrently, while `Load` is reentrant on worker threads.
Move construction transfers the complete implementation and preserves handles for the moved-to
store; the moved-from store is unavailable. Handles are non-owning identity-plus-index values:
expired, stale, out-of-range, or foreign-store handles fail closed. `Load` also rejects a different,
moved-from, or expired `GameDataService`. No operation may race either object's move or destruction.

## Initial contracts

- `VirtualFileSystem` mounts physical directories, ISO views, and HOG archives behind
  normalized case-insensitive game paths.
- `GameDataService` validates the owner-supplied NTSC-U root from bounded `SYSTEM.CNF` metadata,
  owns the frozen VFS, and maps named levels into canonical manifest and spatial-mesh values. Each
  manifest carries the two explicit sibling texture-source locators without assigning a material,
  cell, mesh, draw, or render relationship.
- `LevelTextureStore` normalizes, sorts, and deduplicates the manifest's explicit texture sources,
  resolves each source through the bound `GameDataService`, and owns a sorted, deduplicated direct
  TDX locator inventory. Named-level startup invokes `Open` and owns the resulting value, but does not
  call `Load`; the planned `AssetService` remains unimplemented.
- The planned, currently unimplemented `AssetService` will map paths to typed handles, perform async
  decode, and publish immutable CPU assets before render/audio upload.
- `ScriptService` executes only project-owned native logic or declarative mission data. Retail
  executable/script modules are inspected offline and are never loaded as executable code.
- `SimulationWorld` advances only from explicit fixed-step calls and owns deterministic completed-
  step/simulated-time state plus a preallocated bounded `EntityRegistry`. Generational entity IDs
  reject stale handles, and the registry allocates only during world creation. The composition root
  supplies the scheduler's validated step; its current default and entity capacity are synthetic-
  shell values, while retail timing and population limits remain evidence-driven. Entity IDs are
  plain world-scoped values: they do not own the world, and an identical numeric value in another
  world is a different identity even though the value itself cannot distinguish those worlds.
  `CreateEntity`, `DestroyEntity`, `IsAlive`, and the aggregate value-returning `EntitySnapshot`
  form the lifecycle facade; no mutable registry reference escapes world ownership. `DestroyEntity`
  is reserved as the sole in-place removal path so future direct component stores can erase the
  exact generation in deterministic declaration order before registry reuse. A world may transfer
  complete ownership by move construction, but move assignment is deleted so a live destination
  cannot be replaced outside that lifecycle facade. Whole-world destruction releases future stores
  in reverse member order before the registry.
- `ComponentStore<T>` is the reusable header-only foundation for future direct `SimulationWorld`
  members; no speculative gameplay component is instantiated yet. Creation allocates one optional
  sparse slot per possible entity index and captures a caller-bounded maximum occupancy, after which
  store access is allocation-free and game-thread-only. Every lookup or mutation receives the
  issuing registry and validates the exact live generation. Future world lifecycle code erases
  components before destroying an entity; if that ordering is violated, the payload remains
  occupied but inaccessible until exact-generation `EraseRetained`, `Clear`, or reuse of that same
  sparse slot.
  Insertion never scans unrelated slots: unrelated retained payloads consume bounded capacity and
  fail closed until explicit cleanup. Because `EntityId` has no registry token, a same-capacity
  foreign registry with the same live numeric handle cannot be distinguished; world ownership, not
  this value type, enforces registry scope. Stores expose only aggregate snapshots and short-lived
  borrowed value pointers; neither storage nor pointers cross a reload boundary.
- `RenderService` receives owned renderer-neutral frame packets and exposes no retail-format
  details. The initial packet carries only host frame index, deterministic simulation clock, and
  live-entity count; future scene values must enter as independently owned canonical state.
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
table, `SimulationWorld`, its `EntityRegistry`, and future direct `ComponentStore<T>` members are
also non-hot-reloadable. Entity IDs may be copied as plain data, but registry/component storage and
borrowed component references never cross a reloadable boundary. No vtable pointer crosses a
reloadable boundary.

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

POP has a separate retail-only passive post-terrain hypothesis descriptor in
`omega_retail_formats`. It reuses the validated terrain-prefix parser, requires the established
ordered 19-candidate aligned-literal envelope and five exact arithmetic extents, and returns one
fixed owned descriptor that retains no input span or payload. It is evidence scaffolding rather than
canonical asset IR and is not consumed by content, runtime, simulation, or renderer targets. Its
literal, observed-word, stride, and opaque-range fields carry no section, count, record, payload,
placement, visibility, rendering, or gameplay semantics.

`omega_tool pop-post-terrain-hypotheses-verify-tree` is a tool-only aggregate boundary. Directory
traversal and file reads reject links, reparse points, special entries, identity changes, metadata
changes, and declared limits; POP bytes are read from an opened stable handle or descriptor. The
report exposes only typed totals and independent accepted-only logical maxima. Descriptor
observations do not contribute to those maxima, and the report emits no paths, names, hashes,
literal spellings, candidate offsets, observed words, strides, opaque-range sizes, per-file rows,
identities, or bindings. The confirmed run discovers and accepts all 18 candidates with zero
rejections or errors; independent input/items/logical-output/string/scratch maxima are
`919360 / 1 / 168 / 26 / 80036`.

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

`DecodeTdxTextureStorageMeasured` returns that same owned storage together with exact standalone
decoder-budget usage. Its item count covers the root, blocks, primary planes, present palette
objects, and palette entries; its logical-output count covers the compiled-ABI storage objects,
owned plane bytes, and four source bytes per palette entry. These counters are logical operation
budgets rather than allocator or process-memory measurements.

## Level texture inventory and loading

`LevelTextureStore::Open` applies one cumulative operation budget across all canonical explicit
sources. It resolves the source chain, requires the terminal source to be one exact-end HOG, and
builds the complete normalized directory before extension filtering. Any normalized collision,
including a collision between ignored members, rejects the operation. Inventory then accepts only
direct normalized `.TDX` members; it does not recurse into member HOGs. Identical source locators and
identical resulting texture locators are sorted and deduplicated, so manifest order conveys no
priority. Non-TDX members are validated by the complete directory pass and otherwise ignored.

The store reports exact logical usage under its documented accounting model:

- Open input is every resolved ancestor container plus each terminal texture-source HOG, once per
  canonical source. Open items are all traversed ancestor and terminal directory entries plus one
  item for each emitted canonical texture locator. Logical output is the compiled-ABI store,
  implementation, identity, locator/string objects, and owned normalized string bytes. Archive depth
  is the maximum explicit source-container edge count. Logical scratch is the deterministic maximum
  of the pre-normalization source workspace, the canonical-source workspace plus one sequential
  source-resolver workspace, and the canonical-source workspace plus one complete normalized
  terminal directory. Sources and their directories are processed sequentially.
- Load input is every resolved ancestor container plus the selected terminal TDX payload. Load items
  are the traversed ancestor directory entries plus the measured decoder root, blocks, planes,
  present palette objects, and palette entries. Logical output is exactly the measured owned
  `TextureStorageIR`; archive depth is the selected locator's container depth. The current stateless
  TDX decoder contributes zero logical scratch.

These are deterministic API budgets, not allocator traffic, vector capacity, resident memory, or
process memory. The frozen VFS, filesystem metadata, service-owned caches, HOG-parser storage,
allocator metadata, and spare container capacity are outside these usage counters. Normalized
resolver locator/directory work is included in the reported scratch peak; the remaining excluded
storage stays subject to its own bounded parser, archive-size, directory-count, name, and read
limits. Open does not decode TDX payloads, and Load does not infer
pixels, channels, mip rank, source priority, or any texture-to-material, cell, mesh, draw, placement,
visibility, or render relationship.

The two aggregate Python scans answer different containment questions and do not execute this native
API. `measure_level_tdx_topology.py` is extension-bounded to normalized `.TDX` members in the complete
recursive common `DATA.HOG` graph, with `DATA.POP` manifest references used for designated cell-
occurrence accounting; it explicitly excludes sibling texture containers. The separate
`measure_level_texture_container_topology.py` requires the two sibling roles, treats each as one exact
top-level HOG, validates the complete normalized directory, and measures direct TDX members only.
Both publish structural proxies rather than compiled-ABI Open/Load usage.

`omega_tool level-texture-store-verify-tree` exercises the native store across every strictly
discovered level. The confirmed run accepts 18/18 levels, 36 explicit sources, and 5,801
level-inventory texture occurrences with zero errors. It loads 5,913 storage blocks, 7,603 planes,
615,232 palette entries, 27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 total owned
storage bytes. The independent Open input/items/logical-output/depth/scratch maxima are
`3,076,944 / 1,460 / 111,014 / 0 / 71,467`; the Load maxima are
`3,139,344 / 5,169 / 333,232 / 0 / 65,595`. Each field is maximized independently across its Open or
Load observations; no single level, texture, or operation is asserted to exhibit the complete tuple.
Those measurements set internal defaults of 4 MiB input, 512 KiB logical output, 128 KiB scratch,
8,192 items, 4 KiB strings, and nesting depth one. Input, output, scratch, and items are rounded
independently to the next binary boundary above the larger Open/Load field maximum. Depth one is the
smallest nonzero headroom above measured depth zero while retaining bounded nested-source support.
The string limit retains the common 4 KiB safety cap. These values are implementation policy, not
runtime configuration or `--set` keys, and do not describe a co-occurring corpus tuple.
The aggregate texture and storage totals are level-inventory occurrences rather than unique
whole-disc asset identities. The fixed report emits no paths, names, hashes, offsets, payloads,
per-level rows, identities, or bindings.

Startup owns `LevelManifestIR`, one `LevelContentIR`, and the inventory-only `LevelTextureStore` as
one all-or-error content state. Store Open occurs after the synthetic debug image succeeds; startup
does not call texture `Load`. `AssetService` remains unimplemented, and no texture locator or loaded
storage enters `RenderFramePacket`, `RenderService`, `SimulationWorld`, material catalogs, or a
renderer upload path. Display expansion and all ownership/binding semantics beyond this native
level-scoped locator inventory remain explicitly unwired.

`LoadLevelSpatial` composes the outer DATA.HOG, any container-only source chain, every referenced
cell HOG, and every COL decoder under one operation budget. Input work and item counts are
cumulative, logical output includes every owned mesh/vector payload, semantic-adapter scratch is a
reusable peak, and nesting depth combines archive edges with COL tree edges. HOG input/copy/parser
workspace is bounded independently by configured archive byte caps and the parser's fixed
directory/count/name limits. The default depth is nine: one cell HOG edge plus the corpus maximum
eight-edge COL tree. The returned `LevelSpatialIR` has the same order and cardinality as
`LevelManifestIR::terrain_cells`; provenance remains in the manifest.

`LoadLevelMaterialCatalogs` traverses the same immutable archive chain and requires exactly one VUM
member in every referenced cell HOG. Normalized archive-name collisions fail before member
selection, and zero or multiple VUM members fail closed. One shared operation budget cumulatively
debits outer, nested, cell-HOG, and selected VUM input work plus every archive directory and exact
catalog item/output cost; it never resets limits per cell. VUM semantic scratch is zero and archive
depth ends at the cell-HOG edge. The fully owned `LevelMaterialCatalogsIR` preserves manifest order,
cardinality, and repeated references without deduplication. It exposes only the already-confirmed
name table and dense MTRL-to-name index relationships; passive VUM payloads remain retail-only.
Offline exact equality of complete normalized names ending `.TDX` does not cross this service
boundary: `GameDataService`, `LevelContentIR`, and `LevelTextureStore` perform no catalog-name-to-
locator lookup or alias resolution and expose no material-to-texture binding.

`LoadLevelContent` is the startup composition boundary for those two canonical collections. It
reads and indexes the common archive chain and each referenced cell HOG once, then decodes the
unique COL and VUM members under one cumulative input, item, output, scratch-peak, and depth budget.
The `GameDataService` shared-operation input default is 72 MiB; standalone decoders and each
physical DATA.HOG, nested-HOG, and cell-HOG read retain their separate 64 MiB defaults or caps.
The all-or-error `LevelContentIR` preserves both manifest-order collections together; their parallel
positions assert common source order and cardinality only, not a mesh-to-material relationship.
Startup retains that composite directly, while the synthetic diagnostic reads only its spatial
member. Independent worker calls use only the service's frozen VFS and return separately owned
results.

`omega_tool level-material-catalogs-verify-tree` exercises that service boundary across every
strictly discovered level directory. It publishes only aggregate level, cell, catalog, name,
material, reference, and error counts. Diagnostics expose stage and typed error categories without
printing level codes, filesystem paths, archive/member names, hashes, payloads, or inferred roles.

Tools may link retail adapters. Renderer and simulation targets must consume canonical assets and
must not include retail-format headers. The native source-dependency CI gate scans every native
C/C++ source, header, test, tool, and common source fragment after BOM removal, escaped-newline
splicing, and comment/string handling. It requires literal canonical include paths, enforces an
explicit shipping-module edge allowlist, admits only allowlisted C/C++ standard headers to
platform-neutral modules, and rejects PCSX2-named headers globally. Unclassified shipping paths,
C++ module/import syntax, module-source suffixes, links, reparse points, special files, and files
that change while being read fail closed. This is a source-level boundary: CMake link edges and the
contents of generated headers remain build/review responsibilities. The existing terrain-prefix
parser header is classified as `omega_core` alongside its implementation, matching the target that
contains the implementation despite the header's legacy `omega/asset` path; new semantic adapters
enter through `omega_retail_formats`. Canonical local includes require exact on-disk spelling and
reject Windows alternate-data-stream, reserved-device, trailing-dot/space, and other non-portable
path aliases.
Unsupported file types under both classified and unclassified shipping roots also fail closed.

Startup owns `LevelManifestIR`, one `LevelContentIR`, and one inventory-only `LevelTextureStore` as
an all-or-error content state. Neither material catalogs nor texture locators enter
`RenderFramePacket`, `SimulationWorld`, or
`SdlGpuHost`. The initial renderer consumes canonical spatial meshes only to build a deterministic
synthetic canonical-COL wireframe contact sheet. Meshes occupy source-order tiles, and each mesh is
projected along its two largest coordinate extents. This clean-room diagnostic is not world
placement or reconstructed geometry and makes no VUM, TDX, or other retail semantic claim.

The runtime contains no MIPS execution path. This boundary is permanent and documented in
`docs/adr/0001-pure-native-runtime.md`.

The initial host backend is SDL 3.4.10 for windowing, events, gamepads, audio streams, and the
modern GPU device. SDL is private to the platform/render/audio/input leaves, as documented in
`docs/adr/0002-sdl3-platform-layer.md`.
