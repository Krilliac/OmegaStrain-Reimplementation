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

`omega_persistence` and `omega_profiles` are separate bottom-level native service layers. Persistence
may use host filesystem APIs in its private implementation, but both public interfaces are
platform-neutral and neither can include runtime, content, retail-format, simulation, gameplay, app,
SDL, or PCSX2 headers. Profiles may depend only on persistence; the app may own both through its
composition service, and no lower layer depends upward on it.

The initial native build targets express the same direction:

- `omega_core`: HOG indexing, VFS, and generic bounded infrastructure;
- `omega_persistence`: the project-owned transactional save database, with no emulator or retail
  format dependency;
- `omega_profiles`: bounded, versioned profile metadata over `omega_persistence`, with no implicit
  default or active-profile policy;
- `omega_assets`: canonical owned IR values and decode contracts;
- `omega_simulation`: platform-neutral deterministic world state and fixed-step execution;
- `omega_retail_formats`: stateless POP/COL/VUM/TDX/SKM/SKL/SKA adapters that may depend on the
  first two targets;
- `omega_content`: the non-hot-reloadable data-root service and retail-to-canonical startup
  orchestration;
- `omega_runtime`: launch/configuration services and renderer-neutral diagnostic scene values
  consumed by the composition root and SDL host; and
- `omega_sdl_backend`: the non-hot-reloadable SDL platform, audio, input translation, and GPU leaf.

E-0083 implements the standalone `omega_persistence` foundation described in
`docs/04-Native-Persistence.md`. `SaveDatabase` is movable but noncopyable and holds one exclusive
operating-system lock for its complete live lifetime. Its API is externally serialized on one
persistence/game thread and returns only owned copies. It uses two complete checksummed snapshots;
each optimistic batch writes and flushes a private temporary, atomically replaces the inactive
generation, synchronizes its directory entry, and performs only non-allocating state publication
afterward. Directory identity is retained for the lock and all slot I/O; opened leaves are
no-follow, multi-link snapshots are rejected, and only definite checksum/torn corruption may fall
back. A missing established slot, transient I/O, unsupported version, or indeterminate replacement
fails closed. The format and decoder have explicit configurable plus hard bounds, canonical key
grammar, generation-unique record revisions, reserved-field checks, sorted-key checks, CRC-32
protection, and integrity-checked future-version handling.

The profile composition slice adds `omega_profiles` and the app-owned `NativePersistence` service.
`ProfileCatalog` borrows one stable heap-owned database and stores only bounded versioned markers at
`profiles/<32-lower-hex-id>/metadata`; it deterministically lists identifiers and never creates or
selects a default. `OmegaApp` solely owns the composed service on the persistence/game thread and
destroys it last. Non-probe startup resolves the host-native save directory, opens the database, and
validates all profile markers before platform creation. `--probe-only` returns before persistence;
`--frames=0` returns after bootstrap. This still assigns no active-profile, campaign, checkpoint,
retail-payload, PS2 filesystem, memory-card-device, guest-memory, or emulator-savestate semantics.

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

E-0066 adds a separate portable runtime diagnostic over that canonical boundary.
`BuildTextureStorageTopologyDebugImage` borrows an already-created `TextureStorageIR` and returns an
independent owned `DebugImage`; it is reentrant on any worker thread, has no global state or I/O, and
does not call or depend on the retail decoder, `AssetService`, `OmegaApp`, or a GPU backend. Its
typed fail order first validates nonzero texture dimensions, the sample enum, block presence and
limit, then each source-order block's plane presence and 64-marker hard cap, cumulative plane limit,
each plane's dimensions/enum/exact encoded byte size, optional palette dimensions/cardinality, and
cumulative palette limit. Checked final image dimensions, RGBA8 byte size, output limit, host-size
conversion, and allocation complete the fail-closed boundary. Limits are caller-replaceable values;
the defaults are 4,096 blocks, 262,144 planes, 1,048,576 palette entries, and 64 MiB of output.

The diagnostic raster is intentionally topological. Source-order blocks occupy 32x32 tiles in rows
of at most eight columns. An opaque `{8,12,24,255}` background and `{28,38,58,255}` slate border
contain cyan `{112,220,255,255}` 2x2 masks: `0x1`, `0x9`, `0x7`, and `0xf` distinguish the four
sample and transfer-element enum values. An optional palette adds one amber `{255,196,64,255}` plus.
After validation, only block order, sample/transfer encodings, and palette presence affect pixels;
plane bytes, palette bytes, and dimensions cannot be interpreted as display data. The frozen
three-block fixture is 96x32 RGBA8 (12,288 bytes), has exact background/slate/cyan/amber populations
2,667/372/23/10, and hashes to FNV-1a-64 `0xb56c8db088c5a9fe`.

This value adapter is deliberately not an app, service, asset-binding, upload, or rendering path. It
establishes no retail pixel expansion, display order, channel/alpha conversion, nibble order,
palette permutation, swizzle, material association, geometry relationship, or gameplay meaning.
MSVC configure, focused-target, and full builds were clean with zero warnings or errors. The focused
executable passed directly plus 20/20 repetitions, focused CTest passed, and default CTest passed
30/30. Runtime-off direct and focused checks passed with 27 tests registered. The dependency gate
checked 155 native files, all 209 tooling tests passed, Python compile-all passed, and the final
staged-tree public gate checked 242 indexed text blobs. Validation used no private data, D-drive
content, disc image, retail executable, emulator, or
PCSX2 input. Publication CI remains separate and unclaimed.

E-0067 verifies the boundary composition without expanding the production surface. The synthetic
asset-service fixture loads two generated direct-24 TDX members with deliberately different payload
seeds through the real `LevelTextureStore` to `JobService` to `AssetService` chain. Once both handles
are ready, the test thread borrows each `TextureAssetView::storage` only long enough to call
`BuildTextureStorageTopologyDebugImage`. Each result independently owns a 32x32 RGBA8 tile and 4,096
bytes. The images are pixel-identical even though the canonical plane bytes differ. Both handles can
then be released, returning the service to its exact empty two-slot snapshot while both images stay
valid and unchanged.

This is a test-only proof of the already-public ownership contracts, not a new service method or
runtime policy. It does not choose an asset, wait during an app frame, upload a texture, change a draw
list, or associate texture storage with a catalog name, material, mesh, or cell. Display expansion,
channel/alpha conversion, nibble order, palette permutation, swizzle, placement, visibility, and
retail behavior remain outside the boundary.

E-0068 adds a separate zero-file app presentation over the same adapter. The app-core
`BuildProjectDiagnosticAssetTopologyImage` function owns and builds the exact public E-0066
three-block fixture, then returns the adapter's independently owned `DebugImage`. The call is
reentrant, has no arguments or I/O, and translates fixture-vector `bad_alloc`/`length_error` into
the existing typed topology `AllocationFailed` category. `OmegaApp::Create` performs this build
after non-platform services and before `SdlPlatformService::Create`; a typed failure therefore
causes no SDL, audio, or GPU mutation. No `AssetService`, `LevelTextureStore`, decoder, locator, or
retail source participates.

The app then creates SDL/audio/GPU owners in the existing order and uploads optional spatial, menu,
controls, and topology textures in that order. The topology command uses full normalized source,
the existing menu target, `Contain`, and `Nearest`. Its immutable draw list copies the optional base
prefix and appends exactly that command. Main-menu row two enters byte-3 `AssetTopology`, every valid
topology row returns to byte-2 `ShowAssetTopology`, and invalid rows select the hidden list. No
per-frame image build, upload, list construction, or allocation occurs. Zero-file ownership is
exactly three textures/uploads and 86,016 logical bytes. Destruction clears topology, controls,
visible, and hidden lists before releasing topology, controls, menu, and optional base handles in
reverse upload order; host-authoritative fallback cleanup remains intact.

The card label `ASSET TOPOLOGY` yields exact 128x72 background/cyan/slate/amber populations
3,739/1,481/3,516/480 and FNV-1a-64 `0xf37b700c33071a92`. The topology raster remains the E-0066
96x32/12,288-byte value with populations 2,667/372/23/10 and FNV-1a-64
`0xb56c8db088c5a9fe`. Live and opt-in replay reuse the existing terminal-before-reducer and
reducer-before-elapsed-gate order, so topology frames retain captured raw elapsed but schedule zero,
freeze world/locomotion state, and never accumulate catch-up time. This is synthetic developer
presentation only; it establishes no texture expansion, material/geometry association, placement,
visibility, retail UI, or PCSX2-equivalence semantics.

E-0069 keeps physical confirmation aliases inside the existing SDL-to-neutral input leaf. The
composition root adds keyboard Return, keypad Enter, and gamepad South beside F1 and gamepad Start;
all five map to project action 6. `InputBindingTable` still exposes six unique sorted actions from
exactly 15 physical bindings, and `InputTracker`'s per-control state plus per-action down count
provides one first-down press and one last-up release across aliases. No platform code or reducer
retains which physical control produced the logical row.

`OmegaApp` and opt-in replay continue to query only `WasPressed(6)`. Captures therefore retain their
six-row schema and omit physical provenance, while primary priority, terminal-before-reducer
handling, reducer-before-modal-gate timing, and no-catch-up behavior remain unchanged. Binding-table
validation still occurs before tracker, simulation, and SDL creation, and the added constexpr rows
introduce no new typed failure or rollback path.

The project cards replace only the keyboard legends with `F1/ENTER` and `F1/ENTER RETURN`. Their
new exact FNV-1a-64 values are `0x0a1373c69c8bcce2` and `0xd57a5b0500696505`, with respective
background/cyan/slate/amber populations 3,725/1,495/3,516/480 and 2,104/1,381/5,318/413. Image
dimensions, full opacity, upload/list geometry, topology pixels, three-texture 86,016-byte zero-file
residency, and reverse teardown remain unchanged. Gamepad South is intentionally not rendered as
`A` or `Cross`; that naming is host-dependent and no retail controller mapping is claimed.

E-0070 keeps conventional vertical navigation in the same physical-to-logical leaf. Keyboard Up is
an additional binding for action 2 beside W and gamepad D-pad Up; keyboard Down similarly joins S
and D-pad Down on action 3. `InputBindingTable` now owns exactly 17 physical rows while retaining the
same six sorted actions. Per-control levels and per-action down counts still yield only one first-down
press and one last-up release, so a same-action alias cannot repeat navigation or release early.

The reducer, live host, capture, and opt-in replay continue to consume only action 2/3 rows. Captures
retain no physical provenance. MainMenu navigation remains edge-only, clamped, and simultaneous-
opposite neutral; terminal input and primary retain their existing priorities, and the modal elapsed
gate still prevents catch-up. In DiagnosticPlay, Up/Down also reach the intentionally shared
synthetic forward/reverse actions; this is project diagnostic policy rather than a retail mapping.
Binding validation remains before tracker, simulation, and SDL construction, with no new failure or
rollback stage.

The card legends become `W/S/UP/DOWN`, `W/UP FORWARD`, and `S/DOWN REVERSE`. Their new FNV-1a-64
values are `0x9a4662f8f943521d` and `0xcfa7cc57696aae0a`, with respective
background/cyan/slate/amber populations 3,702/1,518/3,516/480 and 2,104/1,452/5,247/413. Dimensions,
full opacity, topology pixels, draw-list geometry, three-texture 86,016-byte residency, and reverse
teardown remain unchanged. This establishes no retail key map, held-key repeat, navigation wrapping,
mouse selection, or controller-label semantics.

E-0071 inserts a borrowed validation seam at the start of `OmegaApp::Create`.
`ClassifyContentStartupState` is `noexcept`, allocates nothing, and recognizes only the three states
that `StartContent` can publish: no owners, `GameDataService` alone, or the complete five-owner
level-content state. A manifest-only, debug-image-only, incomplete level, or any other mixed shape
returns typed `InconsistentOwnership`. The app copies the resulting `ContentStartupStage`, moves the
valid aggregate exactly once into its existing owner, and rejects an invalid aggregate with
`content startup state: inconsistent-ownership` before config/content owner allocation, service
construction, topology generation, or any SDL/audio/GPU mutation. The classifier adds no stable ABI,
wire, file, config, or replay schema.

The copied stage is consumed only while generating the existing startup menu texture. `CONTENT NONE`,
`CONTENT DATA`, and `CONTENT LEVEL` are project-authored owner-health labels; an invalid enum fails
closed to `NONE`. Their exact FNV-1a-64 values are `0x8e8e3f7fff4f971a`,
`0x517ad52bbf1fbe61`, and `0x08405186aa105db1`, with background/cyan/slate/amber populations
3,593/1,627/3,516/480, 3,600/1,620/3,516/480, and 3,594/1,626/3,516/480. Full opacity,
menu dimensions, draw geometry, Controls and topology pixels, upload count, residency, reducer,
capture/replay, and teardown are unchanged. These labels establish no retail UI or startup-state
semantics.

E-0072 keeps startup errors typed until the final process-presentation edge.
`DescribeContentStartupError` is a nonallocating, reentrant, `noexcept` borrowed adapter over the
existing aggregate. It accepts only `InvalidOptions` and `DebugImage` with no nested error,
`GameData` with only `game_data_error`, and `LevelTextures` with only `level_texture_error`; every
valid shape also requires a nonempty outer message. Unknown outer or nested codes, missing required
nested errors, unexpected nested errors, and both-nested shapes return typed
`InconsistentRepresentation`.
Valid category selection is the existing nested code name for game-data/texture failures and the
existing outer code name otherwise. The returned message aliases the aggregate's outer string.
The underlying nested code-name functions retain their stable `unknown` fallback, but the adapter
rejects those enum values rather than presenting them.

The composition root describes a failed `StartContent` result before streaming it. Valid stderr is
therefore byte-for-byte unchanged. An inconsistent aggregate emits only
`content startup [inconsistent-error]: content startup error representation is inconsistent` and
exits nonzero through the existing path. The process contract creates an empty synthetic data root
and fixes its result at empty stdout plus
`content startup [missing-required-file]: game-data root is missing SYSTEM.CNF`, before any
SDL/audio/GPU work or owner input. `StartContent` remains all-or-error, menu and DiagnosticPlay
gating are unchanged, and no retry, picker, fallback, persistence, schema, capture, or replay path
is introduced. Focused/full MSVC, direct core/process, 30/34/30 CTest, runtime-off, dependency,
tooling, and compile-all validation passed. Publication remains unclaimed.

E-0073 makes the existing optional diagnostic-base presentation concrete for accepted no-level
owner states. `BuildProjectDiagnosticNoLevelImage` is a reentrant app-core builder that returns a
new owned, fully opaque 128x72 RGBA8 image on every call. It uses only the existing integer glyph
and rectangle primitives and reads no file, platform object, asset service, canonical asset, or
retail payload. The fixed labels are `DIAGNOSTIC PLAY`, `NO LEVEL IMAGE`, `F1/ENTER MENU`, and
`ESC QUIT` beneath the existing project frame and header. Exact background/cyan/slate/amber
populations are 3,327/1,285/4,124/480; the full FNV-1a-64 is `0x37f823d27a4cb3ce`.

The E-0071 classification still occurs before allocation or service construction. Once a state is
valid, `OmegaApp::Create` borrows its owner `debug_image` when present; otherwise it owns the
no-level image locally through the synchronous upload. This is precisely the existing diagnostic
upload error boundary and retained handle, not a new resource owner or failure category. Complete
`LevelContent` continues to require and present its owner image. `NoContent` and `DataMounted`
instead use the placeholder, so START DIAGNOSTIC remains reachable in both no-level states.

The no-level upload sequence is diagnostic placeholder, menu, controls, then topology. Fresh host
slots therefore hold four distinct textures totaling 122,880 logical bytes. The retained hidden
draw list contains exactly one full-source/full-target `Contain`/`Nearest` diagnostic command.
Each MainMenu list copies that base then appends the full card and selection marker; Controls and
AssetTopology copy the base then append their card. DiagnosticPlay selects the one-command base
list. Per-frame image construction, upload, draw-list construction, and allocation remain absent.
Destruction clears topology, controls, visible, and hidden lists before releasing topology,
controls, menu, and diagnostic textures in reverse upload order. The four-texture count and byte
total apply only to no-level startup; a level owner's image may have different dimensions and size.

This presentation does not change the reducer, valid state domain, logical or physical input,
simulation or locomotion gate, elapsed scheduling, return transition, terminal priority, capture,
replay, configuration, CLI, file/wire/stable-ABI schema, asset decoding, or content ownership.
The image is a synthetic developer placeholder. It establishes no typed allocation-error contract,
full-framebuffer pixel identity, retail frontend, level selection, DataMounted hardware result,
private-input result, or emulator equivalence. Focused and full MSVC builds were clean. The focused
diagnostic and replay executables and the real D3D12 host smoke each passed directly plus 20/20
repetitions; default, opt-in GPU, and restored CTest passed 30/34/30. A 20-frame capture-replay and
20/20 short repetitions passed. Runtime-off direct and focused CTest checks retained 27
registrations. The dependency gate checked 157 native files, all 209 tooling tests passed, and
Python compile-all passed. Publication remains unclaimed.

E-0074 keeps configuration parsing, runtime-service settings, content selection, and content startup
as separate composition-root stages. `LoadRuntimeConfig` still loads only an explicitly selected
`--config` file (or an empty store) and applies the existing validated `--set` sequence. Both
`content.data_root` and `content.level_code` are strict known keys, so the service-settings resolver
accepts them without consuming them. `ResolveContentLaunchProfile` then validates the effective
configured content tuple before considering direct CLI content options. This order deliberately
makes malformed configured content fatal even when direct CLI would otherwise win.

The resolver returns `expected<optional<ContentLaunchProfile>, ContentLaunchProfileError>`. Neither
configured content key means no configured profile. A configured level without a root is
`missing-data-root`; an empty root or exception while converting its opaque bytes to a native
`filesystem::path` is `invalid-data-root`. A configured level must be 1 to 32 ASCII alphanumeric
bytes and is copied uppercase, otherwise it is `invalid-level-code`. Once configuration is valid, a
direct root and its optional direct level replace that whole tuple; the configured level is never
inherited. Programmatically inconsistent direct options return defensive `invalid-options`.
Diagnostics are fixed, sanitized strings and do not include path or invalid level bytes.

Main resolves this profile after service settings and projects it back into the existing
`LaunchOptions` content fields immediately before `StartContent`; the E-0072 error adapter remains
the following failure boundary. The resolver performs no filesystem existence check or other I/O.
There is no ambient/default profile discovery, persistence, picker, hot reload, new schema, asset
semantic, retail behavior, or emulator-equivalence claim. `/openomega.cfg` is ignored solely as a
privacy boundary for a possible local path. Focused and full MSVC builds were clean;
`omega_core_tests`, the process contract, and default/GPU/restored CTest passed 30/34/30.
Runtime-off direct and focused checks retained 27 registrations. The dependency gate checked 157
native files, all 209 tooling tests passed, and Python compile-all passed. Publication remains
unclaimed.

E-0075 adds default-profile selection as a separate composition-root stage. The compile-time
`HostRuntimeConfigPlatform` classifier is `noexcept` and performs no I/O. The pure lexical
`ResolveDefaultRuntimeConfigPath` consumes only caller-captured `RuntimeConfigSearchRoots`; it
requires nonempty absolute roots and appends one fixed suffix. Windows uses
`LOCALAPPDATA/OpenOmega/openomega.cfg`, macOS uses
`HOME/Library/Application Support/OpenOmega/openomega.cfg`, and XDG uses
`XDG_CONFIG_HOME/openomega/openomega.cfg` with `HOME/.config/openomega/openomega.cfg` as the
fallback when no usable XDG root is present. Dot-dot components and environment-looking tokens are
literal. The resolver does not read the environment or filesystem, normalize, canonicalize,
absolutize, expand, create, or write.

After successful launch parsing and the help fast path, main captures only the roots relevant to
the compile-time host, and only when `--config` is absent. Windows captures `LOCALAPPDATA` through
the wide environment representation. The two-argument `LoadRuntimeConfig` selects explicit config
ahead of the supplied default candidate, so explicit selection bypasses default inspection. The
one-argument overload remains ambient-free. A missing default yields the empty store. Otherwise
the loader inspects only the final entry with `symlink_status(error_code)`: regular files use the
existing bounded loader; a reported symlink, dangling symlink, directory, or other non-regular
entry fails without being followed; other inspection errors fail. This does not assert rejection
of symlinked parents or every Windows reparse point. Default-only load and inspection diagnostics
carry `runtime configuration default profile: `; existing explicit and `--set` diagnostics are
unchanged. File values, source-order overrides, E-0074 tuple validation, and atomic direct CLI
precedence retain their order. Success remains silent. There is no profile write, directory
creation, migration, picker, startup dialog, default level, hot reload, private-data access, retail
behavior, or emulator-equivalence claim.

Serialized local validation passed: focused and full MSVC builds completed cleanly; direct
`omega_core_tests` and the exact process contract passed; default, opt-in GPU, and restored CTest
passed 30/30, 34/34, and 30/30; runtime-off direct and focused checks passed with 27 registrations;
the dependency gate checked 160 native files; all 209 tooling tests and Python compile-all passed;
and the staged public-tree gate checked 247 indexed text blobs. On Windows, the non-missing
inspection-error oracle was explicitly skipped because MSVC maps the
available invalid and overlong candidates to not-found. Commit, DCO, publication, and exact-main
validation remain unclaimed.

E-0076 adds startup-failure presentation as an app-private, stateless adapter rather than a service
or component. `StartupFailureDialogRequest` borrows a stage, category, and detail only for the
duration of a call. `BuildStartupFailureDialogText` projects them with bounded local stack storage
into an owned 640-byte result, and `TryShowStartupFailureDialog` is a main-thread, blocking,
best-effort boundary that
calls `SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "OpenOmega startup error", body, nullptr)`.
It performs no SDL initialization, shutdown, metadata, or window work and owns no global state;
suppression reads SDL's cached environment view. The returned Presented, Suppressed, or Unavailable
outcome does not alter process control flow.

Main invokes the adapter only after writing and flushing an existing fatal stderr diagnostic, and
only for default-profile capture or runtime-config load (`runtime-configuration`), runtime-settings
resolution (`runtime-settings`), E-0074 content-profile resolution (its typed code name), and E-0072
content startup (its projected category or `inconsistent-error`). Runtime configuration, runtime
settings, content launch profile, and content startup use fixed stage labels; an invalid stage
projects to `startup`. The exact body begins `OpenOmega could not reach the main menu.`, includes
Stage, Code, and Detail lines, and ends with a fixed configuration/game-data/arguments prompt with
no trailing newline. Parse/help, app creation, SDL/GPU/audio setup, loop, capture, and replay errors
remain console-only.

Category and detail projection is nonallocating and retains no source view. Printable bytes
`0x21..0x7e` survive, space/tab/CR/LF runs collapse to one trimmed space, and NUL, controls, DEL,
and high bytes become `?`. Limits apply after projection: category is at most 48 bytes, detail at
most 384, and overflow replaces the final three bytes with `...`; empty results use `unknown` and
`No additional detail is available.`. The complete text is ASCII, NUL-terminated, and at most 616
bytes. Only exact `OPENOMEGA_DISABLE_STARTUP_DIALOG=1` suppresses the dialog; invalid policy values
fail closed as suppressed. CMake supplies that environment value to existing synthetic process and
capture tests. The dedicated unit source exercises text and policy projection and calls the dialog
boundary only when presentation is suppressed, including a check that SDL remains uninitialized.
Serialized local validation passed: focused and full MSVC builds; the direct dialog unit and exact
process contract; CTest 31/35/31; runtime-off direct and focused `omega_core_tests` with 27
registrations and no dialog target; the 163-file dependency gate; all 209 tooling tests; Python
compile-all; and the staged public-tree gate checked 250 indexed text blobs. Interactive dialog
smoke, commit, DCO, publication, and exact-main validation remain unclaimed. No private or owner
files, D-drive content, disc image, executable,
emulator, or PCSX2 input was used.

E-0077 adds one stateless bridge between the canonical level texture inventory and the existing
portable topology raster. `BuildFirstLevelTextureTopologyPreview` is a blocking game/main-thread
startup operation that requires exclusive access to a concrete `AssetService`. It first captures
the ten-field public service snapshot and accepts only aggregate-empty state: free slots equal
capacity and active, retired, queued, loading, ready, failed, in-flight, and resident logical bytes
are all zero. It then requires a nonempty store and requests only `HandleAt(0)`, whose lexical,
deduplicated ordering remains owned by `LevelTextureStore`.

After one accepted request the adapter waits for the asset service, obtains one immutable borrowed
view, and invokes `BuildTextureStorageTopologyDebugImage` while that view is valid. The owned image
does not retain the view. Every accepted request reaches `Release`; the final snapshot is always
captured and, after successful `Release`, compared field by field with the entry snapshot. Slot
recycling may advance a hidden generation, which is
deliberately not observable through this aggregate equality. Error precedence is release failure,
residual snapshot mismatch, earlier Get or image failure, then success. Rejected submission also
must restore the exact entry snapshot. The fixed error object contains only one of eight category
messages plus optional enum values. Source-handle failures retain only the texture-store code;
Request and Release retain only the asset code; Get retains the asset code and its nested
texture-store code when present; image construction retains only the topology-image code. No
diagnostic carries source identity or nested free-form text.

The composition root selects this bridge only when `ClassifyContentStartupState` reports complete
`LevelContent`. It builds the image after AssetService creation but before SDL; the later upload
uses the unchanged fourth-texture slot and retains the existing base-plus-card Contain/Nearest
draw list and reverse release order. A generated direct-24 one-block texture produces a 32x32,
4096-byte image with FNV-1a-64 `0x666d00371feff88d`; the synthetic host contract combines it with a
2x2 base and two 128x72 cards for four textures and 77,840 resident logical bytes. `NoContent` and
`DataMounted` continue through `BuildProjectDiagnosticAssetTopologyImage`, preserving the synthetic
96x32 topology and 122,880-byte presentation. Serialized local validation passed focused/full MSVC,
direct asset and D3D12 app smokes, focused asset CTest, default/GPU/restored CTest at 31/35/31,
20/20 repeated D3D12 app smokes, runtime-off direct/focused asset checks with 27 registrations, the
165-file dependency gate, all 209 tooling tests, and Python compile-all. The staged public-tree gate
checked 252 indexed text blobs. The merged commit carries its matching DCO sign-off; PR #38
published E-0077 as exact `main` commit `2a9182e560a504125a5b8278a7202fcad7220c44`, and exact-main
run `29710089254` passed all three jobs. The bridge
does not assign display texels, channel or alpha semantics, palette or nibble interpretation,
swizzle or mip behavior, UVs, materials, cells, placement, visibility, geometry, retail rendering,
gameplay, streaming, eviction, GPU pinning, asynchronous upload, or emulator equivalence; source
payload bytes remain invisible to topology pixels.

E-0078 adds a separate payload-sensitive diagnostic boundary without changing E-0077 or app
composition. `BuildPacked24TransferDebugImage` is stateless and reentrant on worker threads. It
borrows `TextureStorageIR` for the call, performs no I/O or shared-state mutation, and returns a
fully independent `DebugImage`. It accepts only nonzero matching texture/plane rectangles, a known
`Packed24` top-level sample, exactly one block, exactly one plane, no palette, a known `Packed24`
transfer element, and exact three-slot source cardinality. Other known encodings are unsupported;
unknown cast values are invalid. Multi-block and multi-plane storage fails instead of implying
priority, mip, slice, face, frame, or animation purpose.

Preflight follows the frozen sixteen-code priority: top dimensions, sample validity/support,
block/plane cardinality, palette absence, plane dimensions, transfer validity/support, rectangle
match, checked source `area * 3`, checked output `area * 4`, source cardinality, independent 48 MiB
source and 64 MiB output budgets, then allocation. Output also must fit `size_t`. Allocation maps
`bad_alloc` and `length_error` to one fixed category. Every error name/message is constexpr,
category-only, and contains no dimension, payload, offset, identity, or exception text.

For element `n`, source slots `3n + 0..2` are copied without transformation to output slots
`4n + 0..2`; slot `4n + 3` receives synthetic `0xff`. The rectangle supplies deterministic owned
output dimensions only. Seeded 16x16 public fixtures produce 1,024 bytes and hashes
`0x4abb645f50f5a325` and `0x36590f25eee3ab25`. This projection supplies no channel names,
display-ready claim, row origin/order, swizzle, color space, alpha meaning, premultiplication,
Packed32/indexed policy, nibble/palette behavior, or material/UV/geometry semantics. At E-0078's
publication it was not wired to OmegaApp, GPU upload, renderer selection, AssetService, or the E-0077
preview. Serialized local validation passed focused/full MSVC, the direct unit plus 100/100 repeated
runs, focused and
32/36/32 CTest, runtime-off direct/focused checks with 28 registrations, dependency 168, tooling
209, and Python compile-all. The staged public-tree gate checked 255 indexed text blobs. The merged
commit carries its matching DCO sign-off; PR #39 published E-0078 as exact `main` commit
`47378588471d9271a43dfaeb56f3138c01137e1f`, and exact-main run `29710670162` passed all three
jobs. No private or owner files, D-drive content, disc image, executable,
emulator, or PCSX2 input was used.

## Windows portable delivery boundary

E-0079 defines a deployment boundary around the existing native host without adding a runtime,
asset, or emulator dependency. Packaging is restricted to MSVC x64 `Release`, and the packaged
`openomega.exe` uses the static MSVC runtime. The fixed output is
`OpenOmega-0.1.0-windows-x86_64.zip` with exactly one internal
`OpenOmega-0.1.0-windows-x86_64/` root. That root contains only `openomega.exe`,
`launch-openomega.cmd`, `README-WINDOWS.md`, `LICENSE`, `NOTICE`, `TRADEMARKS.md`,
`THIRD_PARTY_NOTICES.md`, and `LICENSES/SDL3.txt`. Explicit install entries own this manifest; no
directory-wide install rule, glob, second archive wrapper, or additional package entry is part of
the contract.

`launch-openomega.cmd` is a minimal package-local process boundary. It changes the working directory
to its own location, invokes the adjacent executable with the caller's complete argument list, and
returns the child exit code. Validation must launch it through the absolute Windows command
processor from an unrelated working directory and an isolated empty user-profile root. The positive
oracle is exact `OpenOmega native shell: rendered_frames=0` stdout followed by one newline, empty
stderr, and exit zero; an invalid sentinel must preserve the existing exact stderr and exit-one
contract, proving argument and exit-code forwarding. Archive validation also
requires the exact regular-file tree, rejects links and Windows reparse-point entries, checks the
x86-64 PE32+ console executable and allowed system imports, and proves that no dynamically linked
SDL or MSVC/UCRT runtime accompanies the static-runtime package.

The package directory also owns a sibling `.zip.sha256` sidecar naming and hashing the exact ZIP.
This output is an unsigned preview, not an installer or signed release. Proprietary inputs and
assets, PCSX2, user profiles, PDBs, and developer tools are permanently outside the payload. The
contract uses only project source, generated build output, and redistributable notices. Serialized
local validation generated the package and matching sidecar, passed the focused package contract,
and observed one canonical root with exactly two directories and eight files. The launcher is
exactly 96 ASCII bytes with five CRLF endings and no BOM; both its zero-frame success and
invalid-option forwarding oracles passed from the isolated profile and unrelated working directory.
The executable is x64 PE32+ with the Windows console subsystem. The local MSVC 19.38 binary imports
exactly 11 allowed direct OS DLLs. The contract additionally permits only the Windows OS
synchronization API-set `API-MS-WIN-CORE-SYNCH-L1-2-0` observed under hosted VS 18/MSVC
19.51/Windows SDK 26100, while rejecting SDL, MSVC, UCRT, debug-runtime, every other API-set, and every other unapproved import.
Deterministic `Release` path mapping plus the enforced narrow/wide byte scan proved the source and
build path prefixes absent. Full MSVC CTest
passed 32/32 `Debug`, 32/32 `RelWithDebInfo`, and 33/33 `Release`; the 168-file dependency gate, all
209 tooling tests, Python compile-all, and the staged public-tree gate over 258 indexed text blobs
passed. DCO passed, PR #40 merged the slice as exact `main` commit `ff8376b`, and exact-main run
`29713390065` passed all four jobs and retained the named Windows archive artifact. General
clean-machine compatibility remains unclaimed. No private or owner files, D-drive content, disc
image, executable, emulator, or PCSX2 input was used for this validation.

E-0080 separates the delivery producer from its first hosted artifact consumer. The producer owns
source checkout, toolchain setup, compilation, tests, packaging, and main-only artifact upload. A
dependent main-push-only job on a separate fresh GitHub-hosted `windows-2022` runner downloads the
same workflow run's named artifact. The consumer has no checkout or toolchain-setup step and invokes
no compiler, CMake, CTest, or producer build tree. Its trust boundary begins with the two downloaded
files: the exact ZIP and SHA-256 sidecar, both regular non-reparse files. It requires a BOM-free
ASCII lowercase digest, two spaces, the exact filename, and one CRLF; recomputes the archive digest;
and extracts exactly the frozen
two-directory/eight-file tree into an isolated directory.

The consumer narrows `PATH` to the Windows system directories and redirects `LOCALAPPDATA`,
`APPDATA`, `USERPROFILE`, `TEMP`, and `TMP` to an isolated synthetic profile. Through the absolute
system command processor, from an unrelated working directory, it applies both launcher oracles:
exact zero-frame stdout, empty stderr, and exit zero; then exact invalid-sentinel stderr, empty
stdout, and exit one. Before the launches it records SHA-256 manifests for the downloaded artifact,
extracted package, unrelated working directory, and synthetic profile, and requires all four to be
unchanged after each launch. Local emulation of the exact PowerShell body extracted from the
workflow YAML passed against both the freshly regenerated local package and retained E-0079 main
artifact. Full Release CTest passed 33/33, the 168-file dependency gate and all 209 tooling tests
passed, Python compile-all passed, and the staged public-tree gate checked 258 indexed text blobs.
PR #41 merged E-0080 as exact `main` commit
`4868e1118bcd32c6713a7f4be57dd243d40996ed`. Exact-main push run `29714679947` completed all five
jobs successfully. Consumer job `88266108572` downloaded retained artifact ID `8450186290`; GitHub
verified that artifact envelope's SHA-256 as
`aea3f4869d17874305bf6027bce370d884ddcaed35e3e9d7a4bc2217aa6baac2`, while the strict
sidecar/recompute check verified the retained inner package ZIP SHA-256 as
`c06ce722572c5edbb0c34ce6b3fc985bcadd4e24ebda0cc07dff59df65ccfe5d`. The job emitted
`fresh-VM portable consumer: OK (artifact, checksum, tree, launch, immutability)`. This hosted result
demonstrates only same-run artifact transfer, integrity, extraction, package-relative launch, exact
process behavior, and non-mutation on that runner image. Its zero-frame path returns before
application/platform creation, so E-0080 does not validate a window, menu interaction, GPU, audio,
owner data, arbitrary clean or physical machines, Windows client editions, other Windows Server
releases, or other runner images. Both digests identify retained E-0080 artifact `8450186290`; they
are not hashes for every later package build.

E-0081 is a separate local visible-interface smoke over a separately identified current portable
package, not an expansion of E-0080's hosted zero-frame contract. The sidecar matched the package
ZIP, all eight extracted regular files matched their ZIP members, and the absolute extracted
launcher started without arguments from an unrelated empty working directory under an isolated
profile. No owner data, level, or configuration argument was supplied. The exact
`OpenOmega - native runtime` title appeared and a temporary operator-visible screenshot contained
only the project-generated no-content OPEN OMEGA diagnostic menu. Its ignored PNG was deleted after
display and is not retained or committed. Native Escape closed the visible run,
and a second same-package exit oracle returned zero. Direct process evidence contained two stdout lines,
exactly three INFO-only stderr records, and zero warning, error, or fatal stderr records. The
unrelated working directory remained empty. GPU drivers wrote only shader-cache files inside the
isolated profile, so profile immutability is intentionally unclaimed. No proprietary input, retail
asset, retail executable, emulator, PCSX2 input, or D-drive filesystem input was accessed. This result does
not establish a pixel-golden contract, retail-menu fidelity, owner-data behavior, controller or
audio coverage, portability to another host or Windows release, or PCSX2 equivalence.

E-0082 generalizes the E-0077 request/Get/build/Release transaction behind a shared internal helper
without changing its error precedence or exact aggregate-state restoration requirement.
`BuildFirstLevelTextureDiagnosticPreview` uses the same canonical handle-zero request to build the
mandatory metadata topology image and attempt the strict E-0078 Packed24 transfer projection while
the immutable asset view is borrowed. It returns independently owned output after Release: topology
is mandatory, while exactly one of the optional transfer image or typed transfer error is populated.
Every Packed24 diagnostic rejection remains nonfatal, including invalid or unsupported storage,
configured output limits, arithmetic bounds, and allocation failure. `OmegaApp` therefore preserves
the topology-only path and records a fixed identity-free INFO category when transfer projection is
unavailable.

For the public synthetic Packed24 `LevelContent` fixture, the combined helper freezes a 32x32
topology image and 16x16/1,024-byte transfer image. Startup uploads that transfer as the fifth
resident texture, for 78,864 logical bytes total, and builds an exact three-command draw list holding
the base diagnostic, split topology panel, and transfer panel. A real Direct3D12 probe verifies the
first four packed source triples and their synthetic `0xff` fourth slots. Draw lists clear before
the transfer texture is released ahead of topology. The public synthetic Packed32 fixture exercises
the nonfatal fallback: topology remains full-width, four uploads retain 77,840 resident bytes, and
the log contains exactly one `unsupported-sample-encoding` INFO record with no fixture identity. A
helper-level output-limit fixture proves that topology survives the typed transfer rejection and
that aggregate `AssetService` state is restored. Source inspection shows one Request/Release
transaction. Transfer upload is likewise optional: any failed fifth upload records the fixed
identity-free `upload-failed` INFO category and rebuilds the full-width topology-only list. A
test-only exact 77,840-byte renderer-pool budget rejects that fifth reservation before backend GPU
allocation and proves startup success, four resident textures, exact pool-state preservation, no transfer
handle, and no raw pool error or source identity in the log.

Serialized local validation passed focused and full MSVC builds; direct asset-service and Direct3D12
app smokes; default, GPU-opt-in, and restored CTest at 32/36/32; the 168-file dependency gate; all 209
tooling tests; Python compile-all; and the public-tree gate over 258 indexed text blobs. Diff and DCO
checks passed. PR, head, publication, and exact-main validation remain unclaimed. The GPU integration
test ran in the local opt-in suite; default hosted CI compiles but does not run it. The app GPU probe
covers four pixels while the standalone CPU projection contract covers all 256 triples. These checks
do not prove a cumulative exact request count, every rejection category, full-image GPU fidelity,
backend-specific fifth-upload failures, release failure, allocation injection, or valid-transfer failure rollback.
They assign no channel, alpha, row-order, swizzle, color-space, material, geometry, retail-rendering,
gameplay, or emulator-equivalence semantics. Only public source and generated fixtures were used; no
private or owner file, proprietary input, D-drive content, disc image, retail executable, emulator,
or PCSX2 input was accessed.

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
public-tree gate checked 239 indexed text blobs. The exact E-0062 main tree subsequently passed the
Windows x86-64 native, Linux x86-64 native, and public-tree safety publication-CI jobs.

E-0063 extends the same portable value into a bounded two-mode, three-row state machine without
making it a service. A default-constructed `DiagnosticMenuState` is the safe
`DiagnosticPlay`/`StartDiagnosticPlay` value, while `InitialDiagnosticMenuState()` explicitly
starts `OmegaApp` in `MainMenu` on `StartDiagnosticPlay`. Before consuming any edge, the
constexpr/noexcept reducer validates both enum values; an invalid mode or row resets directly to
that explicit startup state. It consumes only `WasPressed` edges. Primary action 6 has priority:
it opens the main menu at row zero from diagnostic play, enters diagnostic play from the main
menu's first row, and is inert on the two reserved project rows. Previous action 2 and next action
3 move only in `MainMenu`, clamp at the first and last rows, and are neutral when both edges occur
together.

The physical bindings remain the already-project-owned W/S and gamepad D-pad Up/Down controls for
actions 2/3 and F1/gamepad Start for action 6. Those same actions 2/3 remain held locomotion input,
so a menu navigation edge neither pauses nor suppresses movement, scheduling, simulation, audio,
or replay locomotion. Capture publication and host/logical terminal handling still run before the
menu reducer. Terminal rows may therefore retain any of these action edges without changing mode,
selection, scheduler, world, or renderer state. `RunReplaySession` continues to reconstruct the
ordinary action rows and consume actions 2/3 for synthetic locomotion; it owns no menu state and
performs no menu transition.

Rendering reuses the one existing project-generated 128x72 upload. Startup retains one hidden
draw list plus three immutable visible lists. Hidden contains only the optional base diagnostic
command. Every visible list contains that same optional base command, the full-card command with
source `{0,0,65536,65536}` and target `{2048,2048,26624,15872}`, then one amber marker command
cropped from the card at source `{18432,9103,59392,14563}`. The three marker targets are
`{3584,7424,4352,9344}`, `{3584,10304,4352,12224}`, and
`{3584,13184,4352,15104}` in row order. All card and marker commands use `Stretch` and `Nearest`.
The current mode and bounds-checked row select one prebuilt list; frame publication performs no
menu upload, list construction, or other per-frame menu allocation.

These modes, row names, action reuse, clamping, priorities, crop, and marker locations are
synthetic project diagnostics. E-0063 establishes no retail title-screen sequence, menu art or
text, controls, wrapping, selection, activation, pause, transition timing, audio, persistence,
render composition, replay UI state, private-input result, or PCSX2 equivalence. E-0063 local
validation used only project-generated zero-file fixtures and no private inputs. The incremental
MSVC build completed with zero warnings and zero errors. The portable test passed directly plus
20/20 repetitions, and the real Direct3D12 host smoke passed directly plus 20/20 repetitions.
Default CTest passed 29/29 before GPU enablement and again after restoring GPU registration off;
the opt-in GPU matrix passed 33/33, and the unchanged capture/replay CLI smoke passed 20/20.
Runtime-off validation built the exact portable target, which passed directly and through CTest,
with 26 tests registered. The native dependency gate checked 152 files, all 209 tooling tests
passed, Python compile-all passed, and the public-tree gate checked 239 indexed text blobs.
Publication CI remains separate and unclaimed.

E-0064 keeps the same host-side texture and draw-list ownership while replacing the card's
geometric-only presentation with readable project-authored labels. The integer-only 3x5 glyph table
draws `OPEN OMEGA`, `W/S SELECT`, `F1 START`, `ESC QUIT`, `START DIAGNOSTIC`,
`RESERVED SLOT 1`, and `RESERVED SLOT 2` into the generated 128x72 opaque RGBA8 image. It reads no
font, file, decoded asset, or retail input. `OmegaApp::Create` still uploads that one image once and
retains one hidden/base draw list plus the same three immutable base/card/amber-marker lists. Their
source crops, destinations, fit/filter policy, ordering, selection, and teardown ownership do not
change; neither another GPU upload nor per-frame menu allocation is introduced.

The app loop treats the post-reducer menu state as a narrow simulation gate. Input pumping and
snapshot capture run first, and terminal input still exits before menu mutation. A nonterminal row
then reduces the current state. `DiagnosticMenuAllowsSimulation` accepts only a fully valid
`DiagnosticPlay` state; `MainMenu` and invalid representations fail closed. The host samples actual
elapsed time and appends that unmodified value to an active capture, then supplies either that value
or zero to `FrameScheduler::BeginFrame`. Zero elapsed yields no newly planned fixed steps, and
locomotion planning is skipped while gated. Because reduction precedes this choice, entering
`MainMenu` freezes on the transition frame and entering `DiagnosticPlay` resumes on its transition
frame. The live `previous_frame` baseline advances in both modes, so elapsed menu time cannot become
a later catch-up burst. Rendering still publishes the selected diagnostic draw list each frame;
input/capture, audio operation and health checks, and job-service ownership also remain active.

`RunReplaySessionConfig::initial_diagnostic_menu_state` is an optional caller-owned policy value.
When present, the replay session owns it, applies the same reducer to each reconstructed nonterminal
input row before planning, exposes the current state as an owned optional value, and gates the exact
captured elapsed value in the same way. When absent, replay performs no menu reduction or gating,
which preserves every legacy nonmodal caller. The finite capture/replay path explicitly supplies
`InitialDiagnosticMenuState()` so live and fresh replay use the same synthetic startup policy, but
this is internal orchestration: CLI arguments and output remain unchanged.

No action identifier or physical binding, `InputSnapshot` or trace schema, captured scheduler/world
or menu checkpoint, persistence, serialization, wire/stable ABI, or CLI surface changes. Capture
continues to store actual elapsed rather than gated scheduler input and does not contain the replay
initial state. The readable labels and modal simulation rule remain project diagnostics and define
no retail title/menu sequence, pause behavior, timing, art/font, persistence, or PCSX2 equivalence.
E-0064 local validation completed with a warning-free final incremental MSVC build. The portable
diagnostic and replay tests each passed directly plus 20/20 repetitions; the Direct3D12 host smoke
passed directly plus 20/20 repetitions; default CTest passed 29/29 before and after the opt-in
33/33 GPU matrix; and the capture/replay CLI passed 20/20 repetitions plus one 20-frame run. The
runtime-disabled configuration built and ran the exact portable target, its focused CTest passed,
and 26 tests were registered. The dependency gate checked 152 native files, all 209 tooling tests
passed, Python compile-all passed, and the public-tree gate checked 239 indexed text blobs.
Publication CI remains separate and unclaimed.

E-0065 extends the same app-layer value with `DiagnosticMenuMode::Controls = 2` and renames row byte
1 to `DiagnosticMenuRow::ShowControls`; the fixed one-byte enum representations, two-byte state,
safe default, and explicit startup state otherwise remain unchanged. The complete reducer domain is
three modes by three rows by eight input-edge masks. Invalid mode or row bytes return
`InitialDiagnosticMenuState()` before consuming input. Primary is evaluated first: DiagnosticPlay
always returns main row zero, Controls always returns main row one, main row zero enters
DiagnosticPlay, main row one enters Controls, and main row two is inert. Previous/next navigation is
accepted only in MainMenu, clamps at the two bounds, and treats equal edge values as neutral. The
simulation predicate accepts every valid DiagnosticPlay row and rejects MainMenu, Controls, and all
invalid representations.

The generated main card replaces `RESERVED SLOT 1` with `CONTROLS`. Its exact 128x72 opaque RGBA8
background/cyan/slate/amber populations are 3,739/1,491/3,506/480 and its full FNV-1a-64 is
`0x5303b94979cd74d6`. `BuildProjectDiagnosticControlsImage()` allocates an independent owned image
of the same dimensions and draws `CONTROLS`, `W FORWARD`, `S REVERSE`, `A LEFT`, `D RIGHT`,
`F1 RETURN`, and `ESC QUIT` with the same bounded integer-only glyph and rectangle primitives. Its
four populations are 2,104/1,326/5,373/413 and full FNV-1a-64 is `0xa68873cc7444bdf6`. Neither card
reads a font, path, decoded asset, texture IR, retail payload, or private input.

`OmegaApp::Create` uploads both generated cards once and retains distinct menu and controls texture
handles. The existing three MainMenu draw lists remain optional-base, full-main-card, then row
marker. A fourth immutable Controls list contains the same optional base prefix followed by the full
controls card at target `{2048,2048,26624,15872}` with `Stretch` and `Nearest`; it contains no row
marker. `CurrentDiagnosticDrawList` validates the row before selecting MainMenu, Controls, or hidden
DiagnosticPlay presentation, and invalid state returns the hidden list. In the public zero-file host,
the optional base is absent, so startup owns exactly two uploads and 73,728 resident logical bytes;
MainMenu submits two blits, Controls submits one, and DiagnosticPlay is clear-only. Destruction drops
the Controls list, all MainMenu lists, and the hidden list before attempting controls, menu, and base
texture release in that order. Any failed explicit release and every early startup return retain the
GPU host's authoritative slot cleanup.

Live and opt-in replay require no new orchestration. Terminal input is still captured and resolved
before the reducer. Each ordinary row reduces state before `DiagnosticMenuAllowsSimulation` chooses
actual or zero scheduler elapsed. Thus entry into Controls is modal on the transition frame, held
primary levels cannot repeat, return to MainMenu row one remains modal, and later DiagnosticPlay
activation schedules only its own captured elapsed instead of accumulated menu time. Replay terminal
rows likewise preserve Controls without reducer or scheduler/world mutation. An absent
`RunReplaySessionConfig::initial_diagnostic_menu_state` still selects legacy nonmodal replay.

No logical action, physical binding, action ordering, `InputSnapshot`, input trace, elapsed trace,
capture, checkpoint, persistence, serialization, CLI syntax/output, file format, wire format, or
stable-ABI schema changes. The row rename preserves ordinal 1, the new mode remains an in-process
project value, and no menu state is written into capture evidence. E-0065 validation used only
public, project-generated zero-file fixtures. The final MSVC build was clean. Portable diagnostic
and replay tests passed directly plus 20/20 repetitions; the Direct3D12 host passed directly plus
20/20; default CTest passed 29/29, opt-in GPU CTest passed 33/33, and restored-default CTest passed
29/29. A 20-frame capture/replay and 20/20 short repetitions passed. Runtime-off focused direct and
CTest checks passed with 26 registrations. The dependency gate checked 152 native files, all 209
tooling tests passed, Python compile-all passed, and the public-tree gate checked 239 indexed text
blobs. During validation, three test-only `SimulationState` comparisons that produced MSVC C2676
were changed to a fieldwise helper. A direct configure outside `vcvars` also
contaminated generated cache state; the exact MSVC linker, archiver, and flags were restored without
a source change. No private data, disc image, retail executable, emulator, or PCSX2 input was used.
These project labels, controls, transitions, and modal behavior establish no retail title/menu art,
font, layout, input map, pause behavior, timing, persistence, private-input result, or emulator
equivalence. Publication CI remains separate and is not claimed for E-0065.

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
