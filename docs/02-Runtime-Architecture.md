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
|  `- ComponentStore<Position3> [world-owned bounded synthetic position state]
|- UiService [game thread build; render thread consume]
`- NetworkService [game thread API; I/O worker]
```

E-0043 supersedes the historical planned/unimplemented `AssetService` labels in this tree and the
earlier prose retained below. The current composition root owns an optional `AssetService` after
`JobService` and only when `ContentStartupState` contains a `LevelTextureStore`. Its declaration and
reverse-destruction order release the asset service before the worker pool and content state, so its
non-owning `JobService`, `GameDataService`, and `LevelTextureStore` dependencies remain valid.

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
When the existing project-generated diagnostic image uploads successfully, `OmegaApp` retains its
generation handle separately for release and builds one otherwise-immutable full-target draw list.
It copies that list into each packet, then clears the list before explicitly releasing the handle
and resetting the host; host destruction remains the fallback resource teardown.
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

`AssetService::Request`, `State`, `Get`, `Release`, `WaitForIdle`, and `Snapshot` are game-thread
operations. `Request` reserves a preallocated generation slot and submits only
`LevelTextureStore::Load` to `JobService`; the worker publishes an independently owned immutable
`TextureStorageIR`. `Get` is valid only for `Ready`, and `Release` explicitly recycles `Ready` or
`Failed` slots while advancing the generation. Queued or loading slots are busy and cannot be
cancelled in v0; a slot retires rather than wrapping its maximum generation. Handles contain a weak
service identity plus slot and generation, and retain no service, source, locator, or asset.

Every accepted job captures the shared implementation through callable return. Destruction stops
acceptance, waits only this service's in-flight counter rather than `JobService` global idle, then
resets the service identity so public handles expire deterministically even if the final worker-held
implementation survives through lambda teardown.

`RenderTexturePool::Reserve`, `Publish`, `Rollback`, `Get`, `Release`, and `Snapshot` are
main/render-thread operations and require external serialization. The pool preallocates all slots at
creation and owns only portable metadata: a backend will keep opaque GPU resources separately by slot
index. `Reserve` validates an exact overflow-checked tightly packed RGBA8 extent and charges combined
reserved/resident logical bytes; a backend may publish only after its own creation/upload succeeds and
must roll back every failure. `Release` refunds resident logical bytes and advances the generation;
the maximum generation retires instead of wrapping. Default, foreign, stale, reserved, and released
handles fail closed at resident lookup.

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
- E-0043 supersedes the two preceding historical `AssetService` clauses: v0 accepts an existing
  `LevelTextureHandle`, schedules bounded asynchronous native storage loading, and publishes a
  generation handle. It deliberately performs no path/name lookup, alias resolution, material
  consumption or binding, display expansion, GPU upload, placement, visibility, or rendering.
- `ScriptService` executes only project-owned native logic or declarative mission data. Retail
  executable/script modules are inspected offline and are never loaded as executable code.
- `SimulationWorld` advances only from explicit fixed-step calls and owns deterministic completed-
  step/simulated-time state, a preallocated bounded `EntityRegistry`, and a direct preallocated
  `ComponentStore<Position3>`. Generational entity IDs
  reject stale handles, and the registry allocates only during world creation. The composition root
  supplies the scheduler's validated step; its current default and entity capacity are synthetic-
  shell values, while retail timing and population limits remain evidence-driven. Entity IDs are
  plain world-scoped values: they do not own the world, and an identical numeric value in another
  world is a different identity even though the value itself cannot distinguish those worlds.
  `CreateEntity`, `CreatePositionedEntity`, `DestroyEntity`, `IsAlive`, `PositionOf`, and the
  aggregate value-returning snapshots form the lifecycle facade; no mutable registry or component
  reference escapes world ownership. `DestroyEntity` erases an exact-generation position before
  registry reuse. Optional one-entity translation input is checked together with the clock before
  a fixed step commits, so every reported failure leaves both position and clock unchanged. A world may transfer
  complete ownership by move construction, but move assignment is deleted so a live destination
  cannot be replaced outside that lifecycle facade. Whole-world destruction releases future stores
  in reverse member order before the registry.
- `ComponentStore<T>` is the reusable header-only foundation for direct `SimulationWorld`
  members; E-0060 instantiates only the synthetic project-owned `Position3` component. Creation allocates one optional
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
  details. The current packet carries only host frame index, deterministic simulation clock,
  live-entity count, and E-0047's owned `RenderDrawList`. The list has a fully zeroed fixed backing
  store for at most 16 renderer-neutral texture-blit commands; future scene values must enter as
  independently owned canonical state. Each command contains a generation handle, a half-open
  mip-zero source crop and target rectangle in a synthetic normalized extent of 65,536, and explicit
  `Contain`/`Stretch` fit plus `Nearest`/`Linear` filter policy. Commands retain handles without
  owning or pinning the referenced texture generations.
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

`omega_tool asset-service-verify-tree` verifies the fixed capacity-one sequential service lifecycle
without widening that evidence boundary. Two owned-tree passes are byte-identical schema version 1
and accept all 18 levels, 36 explicit sources, and 5,801 texture occurrences with zero errors.
Occurrences, requests, `Ready` observations, successful `Get` calls, releases, stale-handle
rejections, and zero-residual checks each total 5,801. Loaded storage totals are 5,913 blocks, 7,603
planes, 615,232 palette entries, 27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 owned
bytes. Independent maxima are one active slot, one in-flight request, and 333,232 resident logical
bytes.

The verifier uses one worker, one pending job, one slot, one allowed in-flight request, and a
524,288-byte resident-logical limit. Runtime defaults are 64 slots, 64 in-flight requests, and 64 MiB
resident logical output; the hard slot maximum is 8,192. These are synthetic project-policy bounds,
not retail limits or user settings. A clean MSVC build produced zero warnings and errors, the focused
checks and full 18/18 CTest suite passed, and 100 repeated lifecycle-test runs passed. The unchanged
E-0038 level-store verifier was revalidated. The fixed service report exposes no paths, names, hashes,
offsets, payloads, per-level rows, identities, bindings, messages, or exception text.

Startup owns `LevelManifestIR`, one `LevelContentIR`, and the inventory-only `LevelTextureStore` as
one all-or-error content state. Store Open occurs after the synthetic debug image succeeds; startup
does not call texture `Load`. `AssetService` remains unimplemented, and no texture locator or loaded
storage enters `RenderFramePacket`, `RenderService`, `SimulationWorld`, material catalogs, or a
renderer upload path. Display expansion and all ownership/binding semantics beyond this native
level-scoped locator inventory remain explicitly unwired.

E-0043 supersedes only the historical “`AssetService` remains unimplemented” clause above. Startup
may now construct the optional service, but still issues no asset request and sends no texture data to
`RenderFramePacket`, `RenderService`, `SimulationWorld`, a material catalog, or any upload path. The
service performs no VUM-name/material lookup, alias resolution, material/texture/cell/mesh/draw
binding, display-pixel expansion, placement, visibility, or rendering.

E-0044 added one independent SDL-free `RenderTexturePool` policy boundary. Its defaults are 64 fixed
slots and 64 MiB of logical tightly packed RGBA8 bytes, with a hard 8,192-slot maximum. Every live
pool receives a unique nonzero process-local identity; handles add slot and 64-bit generation values,
and slot reuse is explicit and generation-safe. `RenderFramePacket` remains trivially copyable and
standard layout after receiving its default-invalid diagnostic handle. A clean MSVC build produced
zero warnings and errors, the focused test and 100 repeated focused runs passed, and the full 19/19
CTest suite passed.

At E-0044 the pool created and owned no SDL or GPU object, `SdlGpuHost` and `OmegaApp` did not yet
consume its handles, and the existing one-off debug-image upload remained unchanged. That historical
slice validated no GPU behavior.

At E-0045, the host integration superseded that host non-consumption and one-off-upload state
without changing the portable pool defaults: 64 slots, 64 MiB logical RGBA8, and a hard 8,192-slot
maximum. `SdlGpuHost` owned a fixed slot-indexed table of SDL texture pointers parallel to the
portable metadata pool. Upload of an exact tightly packed project-owned RGBA8 view performed
`Reserve`, backend texture and transfer-buffer creation, copy-command submission, then `Publish`.
Scope guards rolled the reservation back and released every acquired backend object on failure.
Successful release waited for GPU idle before retiring the exact generation and clearing its backend
slot. After a swapchain was successfully acquired, the command-buffer guard submitted on unwind
instead of attempting SDL's illegal post-acquisition cancel.

At that milestone, `RenderFramePacket::diagnostic_texture` was consumed synchronously: the
default-invalid handle selected a clear, while every nondefault value had to resolve as a current
resident generation before the parallel backend slot was read. `OmegaApp` uploaded its existing
project-generated diagnostic image, stored only the returned handle, placed that handle in frame
packets, and released it explicitly before host fallback teardown. `GpuHostSnapshot` exposed only
pool totals and saturating operation counters; it contained no pool, texture, source, or backend
identity.

The E-0045 validation completed with a clean MSVC build at zero warnings and errors, and the default
suite passed 19/19 tests. One initial plus 20 repeated public zero-file GPU smokes all passed on
`direct3d12` (21 total), and a public two-frame `openomega` smoke passed. Each GPU smoke used
capacity one and a 256-byte logical budget, submitted one clear-only frame, and
uploaded/blitted/released opaque 8x8 A (256 bytes). It rejected A's stale handle before GPU access
and reused the same slot at a new generation for opaque 4x8 B (128 bytes), blitted/released B, and
performed a checked idle wait. The exact final totals were two uploads, 384 cumulative logical
bytes, two releases, two blits, one clear-only submission, one rejection, zero unavailable-swapchain
submissions, and one free slot with zero reserved, resident, retired, or charged bytes. At E-0045
the executable target compiled whenever testing and the SDL backend were enabled; hardware/display
CTest registration was off by default and became a serial GPU integration test only with
`OMEGA_RUN_GPU_SMOKE_TEST=ON`.

The E-0045 smoke proved SDL GPU command submission and checked idle, not framebuffer pixel identity
or readback. E-0045 established no `TextureStorageIR`/`AssetService` bridge, TDX plane or palette
consumption, channel/alpha/nibble/palette/swizzle/mip/display expansion, VUM/material/alias/binding,
scene placement/visibility, retail rendering, gameplay, measured GPU allocation bytes,
streaming/eviction, asynchronous upload, or fence design.

E-0046 supersedes E-0045's single `RenderFramePacket::diagnostic_texture` field with the owned
fixed-capacity `RenderDrawList`. Construction accepts at most 16 commands, validates every texture
handle and normalized nonempty in-bounds target rectangle, and preserves both source order and
duplicates. Default construction clears the complete fixed command backing storage, while the list,
commands, rectangles, and containing frame packet remain trivially copyable and standard layout.
The E-0046-era normalized target extent was 65,536. Its now-superseded contained-blit planner mapped
left/top edges by floor and right/bottom edges by ceiling with 64-bit arithmetic, then
deterministically centered an aspect-contained source rectangle in the mapped destination.

Draw-list commands are non-owning and do not pin texture generations. The current same-thread,
synchronous caller keeps every referenced texture resident through consumption; a future queued or
asynchronous renderer requires an independently designed pin/lifetime contract. Before acquiring a
GPU command buffer, `SdlGpuHost` resolves every command into a fixed local array of current pool
generations and backend slots. A stale handle anywhere in the list therefore rejects the whole list
before any earlier command can reach the GPU. On an available swapchain, a nonempty list first
clears the complete target in a render pass, then records nearest-filtered blits in source order
with `LOAD`; an empty list remains clear-only. `OmegaApp` retains its upload handle separately for
explicit release and constructs one otherwise-immutable full-target diagnostic draw list.
Destruction clears that list before releasing the separate handle, with host teardown remaining the
fallback owner.

E-0046 validation completed with a clean MSVC build at zero warnings and errors, the focused checks
plus 100 repeated focused runs, and the default 20/20 CTest suite. One initial plus 20 repeated
public zero-file GPU smokes passed on `direct3d12` (21 total). Each completed run used a two-slot,
384-byte logical-residency configuration and ended with exactly three uploads, 640 cumulative
upload bytes, three releases, two blit frames containing four successful draws, one clear-only
submission, one stale-list rejection, zero unavailable-swapchain submissions, and zero residual
residency. The opt-in GPU CTest also passed as the twenty-first test; registration was then restored
to its default-off state and the default suite remained 20 tests. A public two-frame `openomega`
smoke with dummy audio also passed.

The pre-resolution result is only a host-side stale-handle and prefix-submission guarantee; E-0046
does not establish arbitrary backend-failure atomicity, framebuffer identity, or readback. The
16-command limit, source order, normalized coordinate system, aspect-contained placement, nearest
filter, clear behavior, and composition are project-owned policies, not retail semantics. This slice
assigns no retail draw order, coordinates, filtering, clear/compositing behavior, placement, camera,
material, mesh, or gameplay meaning. It also establishes no `TextureStorageIR`/`AssetService`
bridge, display expansion, measured GPU allocation, streaming or eviction, asynchronous
upload/rendering, resource pins, or fence design.

E-0047 supersedes E-0046's full-source, `Contain`-only, `Nearest`-only command policy. Each command
now owns a half-open normalized mip-zero source crop, a normalized target rectangle, and explicit
`Contain`/`Stretch` and `Nearest`/`Linear` modes together with its generation handle. The fixed
16-command capacity, owned-copy construction, source order, duplicate preservation, zeroed inactive
tail, and const-prefix access remain unchanged. Construction reports fixed typed error categories
and validates capacity first, then every command in source order by handle, source rectangle, target
rectangle, fit mode, and filter mode. The source and target rectangles, modes, command, plan, list,
and containing packet remain trivially copyable and standard layout.

`MapTextureSourceRect` validates the nonzero mip-zero source extent before the normalized source
rectangle. It maps left/top by floor and right/bottom by ceiling with 64-bit products, producing a
bounded, nonempty, half-open texel crop. `PlanTextureBlit` then validates the mapped source before
the target extent, target rectangle, and fit mode. It retains the mapped crop exactly. `Stretch`
uses the complete mapped target rectangle; `Contain` performs overflow-safe aspect comparison,
round-half-up sizing, and deterministic centering. These are pure renderer-neutral policies;
filter selection is mapped separately by the backend.

The host uses three complete-list fail-closed passes. Before acquiring a GPU command buffer, it
first resolves every generation and backend slot, then maps every normalized source crop and every
`Nearest`/`Linear` filter into fixed local storage. After an available nonzero swapchain extent is
known, the third pass plans every destination and fit into another fixed array before recording the
full clear or any blit. A planning failure submits the required empty acquired command buffer and
records no successful-frame counters, so no accepted prefix reaches visible GPU work. Successful
SDL blits use the planned half-open source and destination extents, the selected filter, no flip,
and `LOAD` target semantics in source order after one full-target clear. Empty lists remain
clear-only, while
aggregate counters continue to distinguish successful nonempty frames from individual draws.

`OmegaApp` keeps the generated diagnostic texture handle separately for explicit release and builds
one immutable full-source, full-target, `Contain`-and-`Nearest` command. E-0047 therefore expands
the bounded renderer command vocabulary without changing the app's diagnostic placement policy or
connecting a retail asset to a draw.

Local E-0047 validation completed with a clean MSVC build that exited zero after compiling seven
translation units with zero warnings and zero errors. The focused portable test passed once plus
100 repeated runs, and the default suite passed 20/20. One initial plus 20 repeated public zero-file
GPU smokes passed on `direct3d12` (21 total). Each completed run ended with exactly three uploads,
640 cumulative logical upload bytes, three releases, two successful blit frames, four successful
draws, one clear-only submission, one stale-list rejection, zero unavailable-swapchain submissions,
and zero residual residency. With GPU test registration enabled, the full opt-in suite passed 21/21;
the option was then restored to `OFF` and the default listing returned to 20 tests. A public
two-frame `openomega` smoke also passed on `direct3d12` with deterministic dummy audio. Windows,
Linux, and public-tree CI results are tracked separately from these local validation claims.

The GPU smoke proves checked command acceptance, submission, counters, and idle cleanup, not
framebuffer pixel identity, readback, interpolation quality, or arbitrary backend-failure
atomicity. Source crops, normalized coordinates, fit and filter modes, order, clearing, and
composition are project-owned policies, not retail semantics. E-0047 assigns no retail placement,
visibility, camera, material, texture, mesh, or gameplay meaning and establishes no
`TextureStorageIR`/`AssetService`, TDX, VUM, material, cell, or mesh-to-draw binding. The packet and
draw list are in-process owned C++ values, not a serialized, persistent, network, plugin, or stable
wire ABI. Commands still do not pin texture generations; asynchronous queuing, a pin contract,
fences, streaming or eviction, measured GPU memory, and display expansion remain unestablished.

E-0048 extends the owned frame boundary with `RenderClearColorRgba8`. Its generic construction is
the all-zero `{0, 0, 0, 0}` value. The named `kDefaultRenderClearColor` and default
`RenderFramePacket::clear_color` instead use `{4, 5, 10, 255}`, and `OmegaApp` explicitly selects
that named value. All four channels are unsigned bytes and every combination is valid; the packet
owns the value directly without a view, pointer, or backend type.

Before acquiring a GPU command buffer, `SdlGpuHost` maps red, green, blue, and alpha in order to
SDL floats by `byte / 255.0`. One mapped value supplies every available full-target clear: both the
empty-list clear-only path and the clear preceding a nonempty frame's source-order `LOAD` blits.
The prior host-generated pulse and draw-list-dependent fixed clear colors are removed.
`SDL_GPUBlitInfo::clear_color` remains inert because each blit retains `LOAD` semantics.

The final regenerated MSVC build completed with zero warnings and errors. The focused portable
executable passed once plus 100 repeated runs, and default CTest passed 20/20. One initial plus 20
repeated public zero-file GPU smokes passed on `direct3d12`; every run retained exactly three
uploads/640 cumulative logical bytes, three releases, two blit frames/four successful draws, one
clear-only submission, one stale rejection, zero unavailable submissions, and zero residual
residency. The opt-in configuration passed 21/21 CTests, was restored to `OFF`, and listed 20
default tests. A public two-frame D3D12 `openomega` smoke passed with dummy audio. Windows, Linux,
and public-tree CI are tracked separately from these local validation claims.

This changes no frame counters, complete-list handle/source/filter preflight, target planning,
submit-on-unwind behavior, stale-handle rejection, or unavailable-swapchain accounting. The color
is an in-process renderer-neutral policy, not a stable ABI, persistent/serialized/wire/plugin value,
or retail semantic. It establishes no framebuffer pixel identity, readback, color-space transfer,
alpha or blending behavior, display expansion, or `TextureStorageIR`/`AssetService` asset bridge.

E-0049 adds a private friend-only diagnostic seam rather than a stable renderer API.
`SdlGpuHostTestAccess` can invoke `ReadbackClearForTesting` with an empty frame packet; all SDL
handles remain inside `SdlGpuHost`, and the returned value is an owned four-element array of
`RenderClearColorRgba8`. The operation mutates neither `GpuHostSnapshot` counters nor the portable
texture pool.

The synchronous probe first validates `R8G8B8A8_UNORM` as a two-dimensional color target with a
four-byte texel, then converts the packet clear before acquiring command work. It owns a temporary
2x2 color target and tightly packed 16-byte download transfer buffer. Both the production
swapchain path and this offscreen path call one `RecordClearPass` helper with `LOADOP_CLEAR` and
`STOREOP_STORE`. The probe ends that pass, records the texture download, takes the command buffer
out of its cancel/submit guard before `SDL_SubmitGPUCommandBufferAndAcquireFence`, waits for the
fence, maps only after completion, explicitly decodes four RGBA byte values, unmaps the transfer
buffer, and releases the fence, transfer buffer, and target through bounded RAII ownership.
Render- or copy-pass failure before submission cancels legally because no swapchain was acquired;
after submission, the consumed command buffer is never reused or canceled.

The public zero-file smoke reads back endpoint colors `{0, 255, 0, 255}` and
`{255, 0, 255, 0}` exactly from every one of the four pixels and checks the complete host snapshot
after each readback. A nonempty synthetic draw list returns exact error
`clear readback requires an empty draw list` before any SDL/GPU call and likewise leaves the
snapshot unchanged.

A clean incremental MSVC build issued four compile requests with zero warnings or errors. One
initial plus 20 repeated public zero-file `direct3d12` GPU smokes passed; every run preserved the
established production totals of three uploads/640 cumulative logical bytes, three releases, two
blit frames/four draws, one clear-only submission, one stale rejection, zero unavailable
submissions, and zero residual residency. Default CTest passed 20/20. The opt-in configuration
passed 21/21, was restored to `OFF`, and listed 20 default tests. A public two-frame D3D12
`openomega` smoke passed with dummy audio. Windows, Linux, and public-tree CI are
tracked separately from these local validation claims.

This establishes exact storage/readback only for those two synthetic endpoint colors in the
temporary offscreen target on the observed D3D12 path. The private seam is not a stable public
readback or capture interface and exposes no backend resource. E-0049 establishes no swapchain,
on-screen, presentation, sRGB/HDR, color-space transfer, or intermediate-value UNORM rounding
guarantee and no guarantee for untested values; no alpha interpretation, blending, or composition
semantics beyond the exact tested 0/255 alpha bytes; no blit/filter/source/target pixel correctness
or other driver, platform, or hardware guarantee; no arbitrary backend-failure atomicity or production
asynchronous queue, lifetime-pin, or fence contract; and no stable ABI, persistence,
serialization, wire/plugin, measured GPU memory, performance, streaming/eviction,
display-expansion, `TextureStorageIR`/`AssetService` binding, retail-rendering, or gameplay meaning.

E-0050 adds a second private friend-only diagnostic seam, `ReadbackBlitsForTesting`, whose result
is an owned row-major array of sixteen `RenderClearColorRgba8` values from a fixed synthetic 4x4
target. It does not add a production readback interface or expose an SDL resource. An empty draw
list fails before SDL/resource work with exact error
`blit readback requires a nonempty draw list`. A nonempty probe completes generation/backend-slot
resolution, source-crop mapping, filter mapping, and fixed-target planning for the entire list
before creating the temporary `R8G8B8A8_UNORM` target, 64-byte download transfer buffer, or command
buffer. The operation remains counter-neutral and does not change portable texture residency.

`TryMapTextureFilter` and `RecordTextureBlits` are now shared by the production swapchain path and
the offscreen probe. Each prepared entry stores only a non-owning resolved source pointer, an owned
portable blit plan, and a mapped SDL filter. The recorder preserves source order, `LOAD`, no flip,
no cycling, and the same source/destination/layer/mip fields in both paths. The probe clears through
the existing shared `RecordClearPass`, records every prepared blit, downloads the target, takes the
command buffer into fence-producing submission, waits, maps and explicitly decodes sixteen RGBA8
pixels, unmaps the transfer buffer, then releases the fence, transfer buffer, and target through
guards.

The public zero-file fixture uploads opaque endpoint texels laid out `R G / B W`. On an opaque-black
target, its exact top-row Contain+Nearest plan followed by a later bottom-left Stretch+Nearest
overwrite reads back `KKKK/RRBG/RRBG/KKKK`. Rejected and accepted probes leave the complete host
snapshot unchanged, and probe release restores empty residency before the production A/B/C flow.
The corrected MSVC build completed with zero warnings or errors; default CTest passed 20/20; one
initial plus 20 repeated `direct3d12` GPU smokes passed with exact final production totals of four
uploads/656 cumulative logical bytes, four releases, two blit frames/four draws, one clear-only
submission, one stale rejection, zero unavailable submissions, and zero residual residency. The
opt-in configuration passed 21/21, was restored to `OFF`, and listed 20 default tests. A public
two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI remains separate.

This confirms only opaque endpoint bytes for those two exact source/destination plans, command
order, and load preservation in the fixed 4x4 target on the observed D3D12 path. It establishes no
general Nearest/Linear filtering, cropping, aspect, rounding, sample-center, edge/border,
Contain/Stretch, flip/cycle/mip/layer, alpha interpretation, blending, sRGB/HDR/color-space,
presentation/swapchain, cross-backend, asynchronous-lifetime, ABI/serialization, asset-binding,
retail-rendering, or gameplay guarantee.

E-0051 changes no runtime implementation or public interface. The public zero-file smoke reuses
the private fixed-4x4 readback after A and B occupy the pool's two backend slots. A command selects
A's first texel from its 8x8 source and stretches exact RGBA8 `{32, 192, 224, 255}` over the target.
A later command selects B's first texel from its 4x8 source and overwrites center `[1,1,3,3)` with
exact `{224, 80, 32, 255}`. Source-order `LOAD` plus Nearest therefore yields
`AAAA/ABBA/ABBA/AAAA` on the observed D3D12 path. Full snapshot equality before and after the
probe confirms unchanged production counters and portable texture-pool snapshot fields, while its
own packet keeps diagnostic state out of the existing production A/B submission.

The one-file MSVC build completed with zero warnings or errors; default CTest passed 20/20; one
initial plus 20 repeated `direct3d12` smokes passed with unchanged production totals of four
uploads/656 cumulative logical bytes, four releases, two blit frames/four draws, one clear-only
submission, one stale rejection, zero unavailable submissions, and zero residual residency. The
opt-in configuration passed 21/21, was restored to `OFF`, and listed 20 default tests. A public
two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI remains separate.

This closes only E-0050's same-handle fixture gap by confirming the respective live backend source
selection for these two handles and exact commands. It proves no arbitrary multi-texture list,
slot/generation behavior, crop/target, Stretch/Nearest, interpolation, sample-center, edge/border,
aspect/rounding, Linear/Contain, alpha/blending/color-space, swapchain/presentation, asynchronous
lifetime, cross-backend, public readback, asset-binding, retail-rendering, or gameplay guarantee.

E-0052 adds a bounded post-binding logical capture boundary without changing event ownership.
`InputTraceRecorder::Create` may run on any thread before publication. It validates configuration
before schema before allocation: capacity is 1 through the synthetic hard maximum of 65,536,
the configured contiguous `uint64_t` frame range cannot overflow, and the copied schema is
nonempty, strictly ascending, unique, and at most 64 logical actions. Creation pre-sizes every
private 32-byte frame record. At maximum configuration, 65,536 32-byte record elements plus the
fixed 64-slot `uint32_t` schema backing contain exactly 2,097,408 bytes of element payload. This
does not measure excess vector capacity, allocator/object overhead, or process RSS.

After creation the recorder is an exclusive game-thread owner. Allocation-free `Append` observes
a const `InputSnapshot` without retaining or mutating it. It requires the exact next contiguous
frame and exact schema, records three 64-bit held/pressed/released masks plus accepted/rejected
event counts, and preserves caller and recorder on failure. Error priority is invalid recorder
state, capacity, frame discontinuity, then action-schema mismatch. Allocation-free expected
`Finish` accepts an open zero-frame recorder, transfers the complete backing into a move-only
immutable trace, and leaves the recorder inert. Custom nothrow moves likewise normalize sources;
copy and move assignment are deleted.

`FrameAt` and `ActionAt` return owned values. Invalid frames return `nullopt`; an unknown action on
a valid frame returns an engaged all-false value. Only `actions()` borrows schema storage. The
recorder view ends at recorder move, successful `Finish`, or destruction; the trace view ends at
trace move or destruction. After ownership publication, const trace reads are reentrant on any
thread provided no read races a trace move or destruction. The recorder and trace are
non-hot-reloadable.

The final MSVC build completed with zero warnings or errors. The focused public zero-file test
passed once plus 100 repeated runs, and default CTest passed 21/21. The opt-in Direct3D12
configuration passed 22/22, after which registration was restored to `OFF` and the default list
returned to 21 tests. Publication CI remains separate. This validation covers bounded logical
action/schema/counter capture and owned query behavior only. It establishes no input injection,
playback, scheduler timing or pacing, quit/run-control, simulation/gameplay state, replay executor,
host event/device capture, serialization, file/wire/stable ABI, concurrent recorder use, or retail
limit, timing, or determinism semantics.

E-0053 adds a bounded scheduler-elapsed capture boundary without selecting or reading a clock.
`SchedulerElapsedTraceRecorder::Create` may run on any thread before publication. It validates
configuration before allocation: capacity is 1 through the synthetic hard maximum of 65,536, and
the configured contiguous `uint64_t` frame range cannot overflow. Creation pre-sizes one private
`int64_t` elapsed-nanosecond record per slot. At maximum configuration, the 65,536 record elements
contain exactly 524,288 bytes (512 KiB) of element payload. This excludes excess vector capacity,
allocator/object overhead, and process RSS.

After creation the recorder is an exclusive game-thread owner. Allocation-free `Append` records
the exact caller-supplied signed nanoseconds without measuring, clamping, or interpretation;
negative, zero, minimum, and maximum representation values remain data. Failure preserves the
recorder and has invalid-recorder-state, capacity, then frame-discontinuity priority.
Allocation-free expected `Finish` accepts an open zero-frame recorder, transfers the complete
backing into a move-only immutable trace, and leaves the recorder inert. Custom nothrow moves
normalize sources; copy and move assignment are deleted. `FrameAt` returns an owned active-frame
value and returns `nullopt` outside the recorded range.

After ownership publication, const trace reads are reentrant on any thread provided no read races
a trace move or destruction. The recorder and trace are non-hot-reloadable. A paired
`FrameScheduler` test feeds one scheduler the original elapsed sequence and an identically
configured scheduler the owned values retrieved from the trace. Every tested `FramePlan`,
accumulator state, total planned-step count, and total dropped time remains identical.

The final MSVC build of the signed-nanosecond implementation completed with zero warnings or
errors. The focused `omega_scheduler_elapsed_trace_tests` executable passed once plus 100/100
repeated runs, and default CTest passed 22/22. The opt-in Direct3D12 configuration passed 23/23,
after which registration was restored to `OFF` and the default list returned to 22 tests. The
static native dependency gate passed 133 files, and all 204 tooling tests passed. Publication CI
remains separate. This validation establishes no clock source or timestamp accuracy, `FramePlan`
capture or checkpoint restoration, input alignment beyond caller indices, quit/run-control,
simulation/gameplay, injection/replay/app wiring, CLI, persistence, file/wire/stable ABI, retail
tick rate, or cross-configuration determinism.

E-0054 adds `RunCaptureSession`, an SDL-free `omega_runtime` coordinator that pairs the existing
logical input and scheduler-elapsed traces. Creation accepts 1 through 65,536 frames and a
contiguous leaf range that may end exactly at `UINT64_MAX`. It creates input backing first, elapsed
backing second, and publishes no session unless both succeed. At the hard maximum, input records,
the fixed action-schema backing, and elapsed records contain exactly 2,621,696 bytes of element
payload. This excludes excess vector capacity, allocator/object overhead, and process RSS.

After creation the session is an exclusive game-thread owner. Its phase machine accepts one input
snapshot followed by either elapsed time or terminal input. Successful input capture retains the
pending input index internally, so the elapsed caller supplies only a duration. A terminal owns
that same index and two independent caller-supplied flags for host quit and logical quit; at least
one flag must be true. The coordinator does not detect either condition itself. Each error carries
an explicit operation stage, fixed session category text, and the exact optional input or elapsed
leaf code. Phase checks run before argument or leaf checks. Every pretransition failure preserves
the session and caller input, including a failed elapsed append that leaves its input pending.

Expected rvalue `Finish` accepts either an open empty/balanced session or a terminal session. It
rejects a pending unpaired input without consuming the session. Once a valid finalization starts
leaf finish, the session is consumed even if a leaf reports failure; no rollback or external
failure recovery is promised. Session and pair are move-only, use custom nothrow moves, delete move
assignment, and normalize their sources to inert values. The immutable pair borrows const trace
references until pair move or destruction and returns an owned optional terminal value. After
ownership publication, pair const reads are reentrant on any thread provided no read races pair
move or destruction. Session and pair are non-hot-reloadable.

The final MSVC build completed with zero warnings or errors. The focused
`omega_run_capture_session_tests` executable passed once plus 100/100 repeated runs, and default
CTest passed 23/23. The opt-in Direct3D12 configuration passed 24/24, after which registration was
restored to `OFF` and the default list returned to 23 tests. The static native dependency gate
passed 136 files, and all 204 tooling tests passed. Publication CI remains separate.

This is capture coordination only. It establishes no `OmegaApp` wiring, clock measurement,
scheduler/`RunResult`/checkpoint capture, host quit detection beyond caller flags, CLI,
simulation/render/audio behavior, persistence/file/wire/stable ABI, injection/playback/replay,
external-failure recovery/rollback, concurrent session use, tracker-wide exhaustion guarantee, or
retail limit, timing, or determinism semantics. The separately published
`InputTracker::next_frame_index()` accessor exists only to support future app integration; it is
not coordinator behavior. E-0055 must preflight a planned capture length `N` with `N`, not
`N - 1`, before tracker-index wrap.

E-0055 adds `FrameSchedulerState`, a trivially copyable owned snapshot containing the validated
configuration, accumulated remainder, lifetime planned-step count, and lifetime dropped time.
`FrameScheduler::Snapshot` copies those fields exactly. The value survives later scheduler
mutation and destruction, but supplies no restore or delta operation.

Finite run-capture planning checks a negative limit before the 65,536-frame maximum. A zero-frame
request creates capacity-one empty trace backing at any `next_frame_index`, including
`UINT64_MAX`. A positive `N` is accepted only when
`N <= UINT64_MAX - next_frame_index`. The deliberate `N` check, rather than `N - 1`, keeps the
following `InputTracker` index representable.

Move-only `RunCaptureOutcome` owns the requested limit, possibly partial `RunResult`, completion
category, scheduler states before and after the run, optional failure text, and an optional
`RunCaptureTracePair`. Its source becomes inert after move construction. Published const reads are
reentrant when no read races outcome move or destruction. Operational and capture failures are
nontransactional: service and scheduler work is not rolled back, partial counters remain visible,
and trace finalization is best effort. An operational failure can own finished traces. A capture
failure can own finished partial traces or no pair when finalization fails.

`OmegaApp::Run` and `RunWithCapture` use one shared `RunLoop`; the null capture path preserves
ordinary `Run` behavior. `RunWithCapture` completes planning and all trace-backing allocation
before host-loop logging, clock sampling, or service mutation. The zero-frame path finalizes its
capacity-one empty pair without entering the loop or performing event, clock, scheduler mutation,
simulation, rendering, audio, job, or logging work.

For each active captured frame, the app pumps events, calls `InputTracker::EndFrame`, and appends
that exact logical snapshot. It then computes host quit and logical quit independently. If either
is true, `MarkTerminal` retains both flags and no clock or scheduler work occurs. Otherwise the app
appends the exact elapsed nanoseconds before passing the same value to
`FrameScheduler::BeginFrame`. Only finite-plan validation and session creation can return the
outer `unexpected<string>`. Once the loop begins, operational or capture failure is an owned
outcome. The CLI and `main` remain unchanged and continue to use ordinary `Run`.

The clean MSVC build completed with zero warnings or errors. `omega_core_tests` passed.
`omega_run_capture_tests` passed once plus 100/100 repeated runs, and default CTest passed 24/24.
With Direct3D12 and dummy audio, `omega_app_capture_smoke` passed once plus 20/20 repeated runs.
Its unowned-draw fixture forced the real render-error path. The operational outcome retained one
paired input/elapsed sample, zero rendered frames, owned failure text, and the exact scheduler
boundary, after which the next capture resumed successfully. The public zero-file
`openomega.exe --frames=2` path also succeeded with two rendered and input frames and equal planned
and executed steps. GPU CTest passed 26/26. Registration was restored to `OFF` and the default list
returned to 24 tests. The static native dependency gate passed 140 files, all 204 tooling tests
passed, and Python compile-all passed. Publication CI remains separate.

This establishes no capture CLI, replay, input/playback injection, scheduler restore, persistence,
serialization, file or wire format, stable ABI, simulation checkpoint, RNG state, fake services,
rollback,
ordinary `Run` tracker-exhaustion guarantee, or retail timing or determinism semantics.

E-0056 exposes finite capture only through the exact once-only `--capture-run` launch flag. The
flag requires an explicit `--frames=N` in the synthetic range 1 through 65,536. Ordinary launch
parsing without the flag still accepts zero and values above the capture maximum. Capture cannot
compose with probe mode, and help remains standalone. `main` calls `RunWithCapture` only for this
explicit route; the null-capture path continues through ordinary `Run`.

The command first prints the same `RunResult` counters as an ordinary run, then prints only
aggregate trace-pair presence/counts, optional terminal metadata, and selected absolute scheduler
counters before and after the run. Those scheduler values are derived from owned snapshots but are
neither complete snapshot publication nor deltas. The app-private, tested
`IsCompleteRunCaptureOutcome` policy accepts only a positive matching request with
`FrameLimitReached`, no owned failure, quit, or terminal, exact rendered/input/trace counts and
capacities, matching trace first indices, and equal planned/executed simulation steps. Every other
outcome fails closed at the process boundary.

The portable process contract verifies exact exit, stdout, and stderr behavior for ordinary
zero-frame startup plus missing, zero, and over-limit capture arguments without entering the host
loop. The opt-in GPU capture smoke invokes the successful two-frame command through a CMake wrapper
that checks the child exit code before matching the ordinary result, aggregate trace, and scheduler
summaries. The clean incremental MSVC build completed with zero warnings or errors.
`omega_core_tests` passed. `omega_run_capture_tests` passed once plus 100/100 repeated runs; default
CTest passed 25/25. Direct3D12 plus dummy audio passed GPU CTest 28/28, including the capture CLI
smoke. Registration was restored to `OFF` with 25 default tests. The static native dependency gate
passed 140 files, all 204 tooling tests passed, and Python compile-all passed. Publication CI
remains separate.

This command surface adds no capture files, persistence, serialization, wire format, stable ABI,
per-frame printing, replay, injection/playback, scheduler restore or delta interpretation,
simulation checkpoint, RNG state, rollback, interactive or zero-frame capture command, probe
composition, ordinary-above-65,536 execution claim, or retail timing or determinism semantics.

E-0057 adds `RunCaptureReplaySession`, a concrete SDL-free, move-only `omega_runtime` value that
owns one moved `RunCaptureTracePair`. It is not a service and is non-hot-reloadable. Creation may
validate and take ownership on any thread; after publication, the replay cursor is exclusive to one
game thread and has no concurrent-use contract.

`Create` validates both leaf traces before moving from the caller. It requires valid trace metadata
and input schema, equal capacities and first frame indices, equal normal input/elapsed counts or one
extra terminal input, and a terminal at the final input index with at least one quit reason. Failure
leaves the caller's pair unchanged.

Each successful `Next` publishes a `RunCaptureReplayFrame` and reconstructs the recorded logical
input as a new owned immutable `InputSnapshot`. Reconstruction preserves the frame index, schema
order, held/pressed/released masks, and accepted/rejected event counts. A normal frame couples that
snapshot to the exact signed elapsed value. A terminal frame couples it to the owned independent
host-quit and logical-quit flags and exposes no elapsed value. The session therefore yields exactly
the captured input-plus-elapsed or input-plus-terminal sequence without borrowing a snapshot or
mutating an input source.
`InputSnapshot` reconstruction allocation or defensive trace-read failure leaves the cursor
unchanged. `Next` distinguishes clean exhaustion from a moved-from inert session with fixed
complete and invalid-state errors.

`RunCaptureOutcome::TakeTracePair` is an rvalue-only ownership bridge from app capture to replay.
It moves out the optional pair and normalizes the complete outcome to its inert state whether the
pair was present or absent. Existing outcome borrows end at extraction, as they do at outcome move
or destruction.

This replay layer does not call SDL or add an `OmegaApp`/CLI path. It performs no input injection,
`InputTracker` mutation, synthetic physical event generation, scheduler creation/feed/pacing,
clock sampling, scheduler restore, simulation checkpoint or RNG restore, world mutation, render,
audio, or job work. It defines no persistence, serialization, file/wire/stable ABI, cross-process,
seek, rewind, loop, rollback, retail timing, or retail determinism contract.

E-0058 adds `omega::app::RunReplaySession`, a concrete move-only `omega_app_core` value above the
lower capture replay cursor. It is not a service, is non-hot-reloadable, and is exclusive to one
game thread after publication. The factory first validates and creates a fresh `FrameScheduler`
from caller-supplied synthetic timing configuration, validates entity capacity, and creates a fresh
empty `SimulationWorld` whose fixed step matches that scheduler. Only then does it validate and
take the `RunCaptureTracePair`; an expected creation failure therefore preserves the pair.

For a successful elapsed publication, `Next` feeds the exact signed captured duration to the owned
scheduler, executes every step in the resulting plan on the owned world, and publishes a move-only
frame with the reconstructed input and owned plan. Scheduler and simulation observers return owned
snapshots.
A lower replay read or reconstruction failure leaves the wrapper state, scheduler, and world
unchanged for exact-frame retry. A terminal publication has no elapsed plan, completes the session,
and does not mutate either subsystem. The defensive representation branch is unreachable under the
current 65,536-frame, 64-step-per-frame, and one-second-step bounds. If those bounds expand and the
branch is reached after a plan is consumed, prior steps may already be committed, so the wrapper
enters a permanent failed state.

The owned scheduler and world always begin fresh under the caller's configuration. The capture pair
contains no checkpoint from either subsystem, and reconstructed input is observable but is not
consumed by `SimulationWorld`. This layer therefore does not restore captured app state, inject
input, reproduce captured gameplay, synthesize host events, restore entities or RNG, own pacing or
a host clock, replay an existing `OmegaApp`, add a CLI route, render, mix audio, dispatch jobs,
persist data, define a file/wire/stable ABI or cross-process contract, seek, rewind, loop, roll
back, or claim retail timing or determinism.

E-0059 adds the exact once-only `--replay-capture` launch flag. The one-process flow is
`openomega --frames=N --capture-run --replay-capture`, with `N` in the existing explicit capture
range of 1 through 65,536. The replay and capture flags may appear in either token order, but replay
always requires capture. Ordinary launch and capture-only behavior remain unchanged.

The process first completes the existing captured run. Before any destructive ownership transfer,
`main` requires `IsCompleteRunCaptureOutcome` to accept the capture and requires the captured
starting `FrameSchedulerState` to contain only its configuration, with zero remainder, planned
steps, and dropped time. The complete-capture gate excludes operational or capture failure, quit or
terminal input, incomplete result or trace counts, capacity/origin disagreement, and unequal
planned and executed steps. Only then does rvalue-only `TakeTracePair` normalize the outcome and
transfer its pair into a new `RunReplaySession`.

The replay uses the capture's starting scheduler configuration to create a fresh scheduler and a
fresh empty world. It advances every elapsed frame synchronously on the main thread, accumulating
replayed-frame, planned-step, clamped-frame, and dropped-frame totals. Completion requires those
aggregates to match the captured result, the replay scheduler snapshot to equal the captured final
scheduler snapshot, and the fresh world to have matching completed steps and derived simulated
time with zero live entities. Creation, frame-shape, overflow, replay, completion, aggregate, and
final-state failures are fail-fast. Only complete success prints one fixed
`OpenOmega fresh replay:` line containing replayed frames, planned and completed simulation steps,
clamped and dropped frames, and `completion=complete`.

This CLI does not replay into the existing `OmegaApp`. After capture, its main-thread replay
orchestration makes no calls into or reads from that app. The host remains alive, so its audio
callback may continue independently. `RunReplaySession` performs no pacing, host-clock sampling,
rendering, audio, or job work. The CLI accepts no terminal or incomplete capture for replay, injects
no reconstructed input, and the fresh world does not consume that input. It reconstructs no
gameplay and restores no captured scheduler/world state, entities, or RNG. It defines no
persistence, file, wire, stable ABI, cross-process format, seek, rewind, loop, rollback, retail
timing, or retail determinism contract.

E-0060 supersedes only the prior statements that every fresh replay world is empty and reconstructed
input is never consumed. The new `omega_gameplay` library is a portable value/system layer above
`omega_simulation`; it owns no service, thread, callback, platform handle, world pointer, or retained
component reference. Its pure planner accepts only signed digital axes `-1`, `0`, and `1`, mapping
them to a project-defined one-unit X/Z translation with Y unchanged. That mapping is synthetic host
policy rather than recovered player or coordinate semantics.

`OmegaApp` creates one positioned diagnostic entity in its owned world. W/S/A/D and gamepad D-pad
bindings publish actions 2 through 5 through the existing immutable input snapshot. Opposing held
actions cancel. Once a nonterminal frame has an immutable input snapshot, the app derives one
translation and supplies that same value to every fixed step in the scheduler plan. The world
preflights clock representation, exact entity liveness, exact-generation position presence, and
signed X/Y/Z addition before committing the position and clock together. Terminal frames still end
before clock sampling, scheduler planning, locomotion, simulation, or rendering.

`RunReplaySessionConfig::enable_debug_locomotion` defaults false, preserving the prior clock-only
behavior for every existing caller and trace. When explicitly enabled, the session creates its own
fresh positioned diagnostic entity and consumes reconstructed held actions through the same pure
planner and world transaction. Its observer returns an owned optional position. The finite CLI
enables this option only when all four action identifiers are present in the captured schema. It
still transfers no captured world checkpoint and does not read the still-live `OmegaApp`; it checks
fresh actor/position presence plus the established aggregate, scheduler, and clock conditions. The
real-host integration smoke separately compares the captured app position with the fresh replay
position. The fixed E-0059 success line is unchanged.

This path renders no actor and establishes no retail input IDs, bindings, axes, handedness, origin,
unit, speed, diagonal normalization, tick rate, analog/deadzone behavior, transform, physics,
collision, gravity, camera, animation, weapons, AI, mission, asset, network, or retail determinism
contract.

E-0061 first adds only an app-core diagnostic-menu value and owned project-generated image. The
trivially copyable state contains one visibility bit; its constexpr/noexcept reducer receives an
already-routed press edge and retains no input or host reference. Logical action 6 is reserved in
the header but is not present in any binding table. The image builder allocates and returns one
independent 128x72 opaque RGBA8 vector, then uses bounded integer rectangle and 3x5 block-glyph
rasterization to create a visibly synthetic `DEV` card. Its exact four-color output is pinned by
complete FNV-1a-64 `0xdaf00c60d17f05b5`.

This first E-0061 slice is compiled into `omega_app_core` and remains available when
`OMEGA_BUILD_RUNTIME=OFF`. Nothing calls the builder from `OmegaApp`; no texture is uploaded, no
draw list or render packet references the card, and capture/replay owns no menu state. The reserved
action has no physical binding. Consequently this slice defines no retail screen, layout, text,
font, palette, navigation, activation, pause, timing, rendering, asset, persistence, or replay
contract. Host input and GPU ownership are intentionally deferred to a separate milestone.

E-0062 supplies that native-host ownership while keeping the portable reducer independent of SDL.
`OmegaApp` owns `DiagnosticMenuState`, binds keyboard F1 and gamepad Start to logical action 6, and
applies only `WasPressed(6)` after capture publication and terminal host/logical-quit handling. A
terminal snapshot can therefore retain the action row without changing visibility. On a normal
frame, visibility changes do not suppress locomotion planning, scheduler work, simulation, audio,
or rendering; the menu is a nonmodal draw-list choice.

Startup creates the project image and performs one 128x72 RGBA8 upload. The optional base diagnostic
texture remains independent. Two fixed `RenderDrawList` values are assembled once: hidden contains
the base command when present, while visible contains that same base command followed by the menu
command. The menu command uses Q16 source `{0,0,65536,65536}`, target
`{2048,2048,26624,15872}`, `RenderTextureFitMode::Stretch`, and
`RenderTextureFilterMode::Nearest`. Each frame packet copies the selected immutable list; it does not
allocate a replacement list or upload the menu again. Destruction clears both lists before releasing
the menu and base handles independently; the GPU host remains the fallback owner after a failed
release.

Action 6 remains ordinary captured input evidence. `RunCaptureReplaySession` reconstructs the row,
but `RunReplaySession` has no diagnostic-menu value or reducer and transfers no visibility state.
The finite CLI continues to publish the established `OpenOmega fresh replay:` success line. This
host slice remains synthetic developer presentation and defines no retail menu, input map, pause,
navigation, selection, activation, layout, timing, render composition, persistence, or replay
contract.

E-0062 local validation completed with a clean zero-warning MSVC build. The focused portable menu
test passed, the real D3D12 host smoke passed directly plus 20/20 repetitions, default CTest passed
29/29, the opt-in GPU matrix passed 33/33, and the unchanged capture/replay CLI smoke passed 20/20.
Runtime-off validation built and ran the exact portable target and registered 26 tests. The native
dependency gate checked 152 files, all 209 tooling tests and Python compile-all passed, and the
public-tree gate checked 239 indexed text blobs. Publication CI remains separate.

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
The offline exact-first one-terminal-extension candidate family likewise remains outside this
boundary. Its transformed comparison keys and candidate relationships do not enter canonical IR,
and no runtime service removes a name extension, resolves an alias, consumes the candidate result,
or binds a material catalog entry to a texture locator.

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
