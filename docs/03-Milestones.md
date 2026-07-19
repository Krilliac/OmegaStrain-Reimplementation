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
- E-0043 implements and verifies asynchronous native `AssetService` v0. `OmegaApp` owns the service
  optionally when startup has a texture store; fixed reusable generation handles, explicit release,
  bounded in-flight/resident accounting, shared worker-held implementation lifetime, and ordered
  teardown define the lifecycle. A clean MSVC build produced zero warnings and errors, focused and
  full 18/18 CTest runs passed, and 100 repeated lifecycle-test runs passed. Two owned-tree verifier
  passes are byte-identical schema version 1 and accept 18 levels, 36 sources, and 5,801 occurrences
  with zero errors. Requests, `Ready` states, `Get` calls, releases, stale-handle rejections, and
  zero-residual checks each total 5,801. Storage totals are 5,913 blocks, 7,603 planes, 615,232
  palette entries, 27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 owned bytes;
  maximum active/in-flight/resident-logical usage is `1 / 1 / 333,232`. The verifier service limits
  are `1 / 1 / 524,288`; runtime defaults are `64 / 64 / 64 MiB`, with a hard 8,192-slot maximum.
  These are synthetic project bounds, not retail limits or user settings. No VUM name/material
  lookup, alias resolution, binding, display pixels, GPU upload, placement, visibility, or rendering
  is established. The fixed report exposes no identities or private data, and the unchanged E-0038
  verifier was revalidated.
- Aggregate-only TDX coherence scoring accepts the same 15,248 spans and nominates low-nibble-first
  for direct four-bit `0x14` planes and the bit-3/bit-4 palette permutation for direct eight-bit
  `0x13` planes. The content-dependent scores are hypotheses, not display-layout semantics.
- VUM aggregate validation: 7,036/7,036 catalogs normalize to 38,793 owned names, 38,899 materials,
  and 42,631 dense name references. Its passive payload pass validates 91,460 pairs, 38,023
  normalized targets, 134,122 middle-to-final references, and 365,840 ordered Q/P references,
  with zero errors.
- The bounded analysis-only full-normalized-member-name lexical-coherence rerun scans 18/18 levels
  with zero errors and reproduces the native level/cell/catalog/name/material/reference and
  source/locator populations. All 34,267 VUM name occurrences and 37,893 dense references lack
  terminal `.TDX`, so none enters the eligible exact lookup branch and all 5,801 class-qualified
  locators remain unreached. This is narrow negative evidence only; no alias family, retail lookup,
  material binding, class priority, or runtime integration is established.
- The additive exact-first one-terminal-extension family leaves that default result and the direct
  `.TDX` locator universe unchanged. Two byte-identical passes scan 18/18 levels with zero errors;
  all 34,267 names and 37,893 dense references are extension-elided unique-primary candidates, and
  all 34,589 material records inherit a unique extension-elided candidate. Of the 5,801 original
  class-qualified locators, 5,690 are reached only through extension elision and 111 remain
  unreached. This remains offline lexical evidence: retail extension removal, lookup, material
  consumption, texture binding, class priority, and runtime integration are not established.
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
materials, or expand display pixels; no store payload enters GPU upload or rendering. The rendered
host path uploads only the existing project-generated diagnostic RGBA8 image.
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
loading files or retail data. A synthetic positioned diagnostic component and pure digital
locomotion planner now exercise the world/input boundary, but retail player components and gameplay
systems, render scene snapshots, decoded voices, and mixing remain incomplete.

The simulation world now solely owns a bounded, preallocated generational entity registry plus one
direct bounded `Position3` component store. Entity
creation/reuse is deterministic for identical call sequences, stale or nonmatching generations are
inert, and capacity exhaustion is explicit. The world now exposes creation, destruction, liveness,
and aggregate identity snapshots without releasing a mutable registry reference; destruction
cleans the exact-generation position before registry reuse. Complete world
ownership remains move-constructible, while move assignment is deleted so replacing a live world
cannot bypass that coordination point. A reusable header-only `ComponentStore<T>` foundation
provides bounded startup allocation, exact-generation access, constant-time same-slot replacement
of stale payloads, explicit exact-generation retained cleanup, inert moved-from behavior, and
aggregate-only snapshots for direct world-owned stores.
Unrelated retained payloads are not swept during insertion and fail capacity closed until cleanup.
Only the E-0060 synthetic diagnostic position is instantiated; retail components and systems remain
future evidence-driven work. Entity and component capacities are synthetic host limits, not retail
population claims.

At E-0044, each rendered frame crossed an explicit owned `RenderFramePacket` boundary containing the
host frame index, deterministic world clock, live-entity count, and a default-invalid fixed-width
diagnostic texture handle. The packet remained trivially copyable and standard layout. The SDL-free
`RenderTexturePool` preallocated fixed metadata slots; validated exact, overflow-checked, tightly
packed project-owned RGBA8 extents; supported transactional `Reserve`/`Publish`/`Rollback`;
rejected default, foreign, stale, and released handles; refunded logical bytes on explicit release;
and retired a maximum generation rather than wrapping. Defaults were 64 slots and 64 MiB logical
RGBA8, with a hard 8,192-slot maximum. A clean MSVC build had zero warnings or errors, the focused
test and 100 repeats passed, and full 19/19 CTest passed. The SDL host consumed the frame packet
synchronously but did not consume this handle; the pool created no GPU object and the existing
one-off debug upload remained unchanged. Those are historical E-0044 boundaries.

At E-0045, the host integration superseded the historical host non-consumption and one-off-upload
state. `SdlGpuHost` owned a fixed SDL texture table parallel to the portable pool. Project-owned
RGBA8 upload was an RAII transaction across `Reserve`, backend create/copy/submit, and `Publish`;
failures rolled back residency and acquired backend resources. Release waited for idle, packets
selected the resident generation for blitting, and the aggregate snapshot exposed no identities.
`OmegaApp` uploaded its existing project-generated diagnostic image, stored only the handle, and
explicitly released it before host fallback teardown. Post-swapchain command-buffer unwinding
submitted rather than taking SDL's illegal cancel path. Pool defaults remained 64 slots and 64 MiB
logical RGBA8, hard-limited to 8,192 slots.

The E-0045 validation completed with a clean MSVC build at zero warnings and errors, and default
CTest passed 19/19. One initial plus 20 repeated zero-file GPU smokes all passed on `direct3d12` (21
total), and a public two-frame `openomega` smoke passed. Each GPU smoke used capacity one/budget 256
bytes; submitted one clear-only frame; uploaded, blitted, and released opaque 8x8 A (256 bytes);
rejected its stale handle before GPU access; reused the same slot at a new generation for opaque 4x8
B (128 bytes); blitted and released B; then checked idle. Exact totals were two uploads/384
cumulative logical bytes, two releases, two blits, one clear-only submission, one rejection, zero
unavailable submissions, and final capacity one/free one with zero reserved, resident, retired, or
charged bytes. This proved command submission and idle, not framebuffer identity or readback. At
E-0045 the target compiled with tests plus the SDL backend, while hardware CTest registration was
off by default and opt-in via `OMEGA_RUN_GPU_SMOKE_TEST`.

E-0045 established no `TextureStorageIR`/`AssetService` bridge; TDX plane/palette consumption;
channel, alpha, nibble, palette, swizzle, mip, or display expansion; VUM/material/alias/binding;
scene placement/visibility; retail rendering; gameplay; measured GPU bytes; streaming/eviction;
asynchronous upload; or fence design.

E-0046 supersedes E-0045's single diagnostic-texture packet field with an owned, fixed-capacity
`RenderDrawList`. Its fully zeroed backing store holds at most 16 commands, and construction
validates every handle and nonempty normalized target rectangle while preserving order and
duplicates. The normalized extent is 65,536. Deterministic 64-bit planning floors left/top edges,
ceilings right/bottom edges, and centers an aspect-contained source rectangle. The list, its command
values, and the containing packet remain trivially copyable and standard layout.

Commands neither own nor pin textures; the current synchronous caller keeps their generations
resident. `SdlGpuHost` pre-resolves every handle into fixed storage before acquiring a GPU command
buffer, so a stale handle anywhere rejects the list before any valid prefix can be submitted. An
available nonempty frame clears the full target, then records nearest-filtered `LOAD` blits in
source order; an empty list remains clear-only. `OmegaApp` separately retains the handle needed for
release and one otherwise-immutable full-target diagnostic draw list, clears the list during
destruction, then releases the handle.

A clean MSVC build completed with zero warnings and errors; the focused checks and 100 repeats
passed; and default CTest passed 20/20. One initial plus 20 repeated public zero-file GPU smokes
passed on `direct3d12` (21 total). With capacity two and a 384-byte logical-residency budget, each
completed run ended at exactly three uploads/640 cumulative bytes, three releases, two blit
frames/four successful draws, one clear-only submission, one stale-list rejection, zero unavailable
submissions, and zero residual residency. The opt-in GPU CTest passed as test 21, after which
registration was restored to default off and the default suite to 20 tests. A public two-frame
`openomega` smoke with dummy audio passed.

This proves host-side stale-handle rejection before prefix GPU work, not texture ownership or pins,
arbitrary backend-failure atomicity, framebuffer identity, or readback. The command limit, order,
normalized coordinates, aspect containment, nearest filter, clear/compositing behavior, and
diagnostic placement are project-owned policy rather than retail semantics. No retail order,
coordinates, filtering, clearing, composition, placement, camera, material, mesh, or gameplay
meaning is assigned. No asset-service/storage bridge, display expansion, measured GPU allocation,
streaming/eviction, asynchronous upload/rendering, pin contract, or fence design is established.

E-0047 supersedes E-0046's fixed full-source, contained, nearest-filter blit policy. Every bounded
command now owns a half-open normalized Q16 source crop and explicit project-owned
`Contain`/`Stretch` fit and `Nearest`/`Linear` filter choices in addition to its generation handle
and normalized target rectangle. Allocation-free draw-list creation rejects capacity overflow
first and then validates complete commands in source order with stable handle, source, target, fit,
and filter priority. The maximum remains 16; source order and duplicates are retained; the inactive
tail remains zeroed; only the active const prefix is exposed; and all public command/list/plan/frame
packet values remain trivially copyable and standard layout.

The pure `MapTextureSourceRect` phase maps normalized crops to half-open mip-zero texel rectangles,
flooring start edges and ceiling end edges with 64-bit overflow-safe arithmetic. The pure
`PlanTextureBlit` phase retains that mapped crop exactly, maps the target with the same edge rule,
and either stretches to the mapped target or aspect-contains from the cropped dimensions using
deterministic round-half-up sizing and centering. Its error ordering checks the mapped source,
target extent, normalized target rectangle, and fit mode deterministically.

The host then runs three complete-list fail-closed passes. Before GPU acquisition it resolves every
handle and backend slot into fixed storage. It then maps every source crop and filter. After command
buffer and swapchain acquisition, it plans every destination against the nonzero swapchain extent
before recording the clear or any blit. Only then does it clear the full target once and issue
source-order `LOAD` blits with the per-command crop and filter. Thus a stale later generation cannot
reach GPU work, and a later planning rejection cannot record a visible accepted prefix. The
post-acquisition path uses fixed arrays and pre-reserved error storage, empty lists remain
clear-only, and command-buffer submit-on-unwind remains intact.

A clean MSVC build compiled seven translation units with zero warnings and errors. The focused
portable executable passed once plus 100 repeats, and default CTest passed 20/20. One initial plus
20 repeated public zero-file GPU smokes all passed on `direct3d12`; each run ended with exactly
three uploads/640 cumulative logical bytes, three releases, two submitted blit frames/four
successful draws, one clear-only submission, one stale-list rejection, zero unavailable
submissions, all slots free, and zero reserved, resident, retired, or charged state. The opt-in GPU
configuration passed 21/21 CTests; registration was then restored to OFF and the default listing to
20 tests. A public two-frame D3D12 `openomega` smoke with dummy audio also passed. Publication CI is
tracked separately from these local validation claims.

This proves bounded project-owned crop/fit/filter validation, planning, SDL submission, and cleanup,
not framebuffer pixel identity or readback, filter-pixel correctness, or arbitrary backend-failure
atomicity. Crop, fit, filter, ordering, clear/composition, and placement remain project-owned
policy, not retail semantics. No asynchronous pin/fence design, measured GPU allocation, streaming
or eviction, `TextureStorageIR`/`AssetService` bridge, TDX plane/palette/channel/alpha/nibble/
swizzle/mip or display expansion, or VUM/material/alias/cell/mesh/placement/visibility/camera/
retail-rendering/gameplay meaning is established.

E-0048 adds packet-owned clear policy without changing draw-list or submission behavior.
`RenderClearColorRgba8` generically defaults to `{0, 0, 0, 0}`; the named
`kDefaultRenderClearColor` and default packet member are `{4, 5, 10, 255}`. Every unsigned-byte
combination is valid. Before command-buffer acquisition, the SDL host maps channels in RGBA order
by `byte / 255.0` and applies the same value to clear-only frames and the full-target clear before
blits. This replaces both the host pulse and the list-dependent fixed color. `OmegaApp` explicitly
uses the named default, while blits retain `LOAD` with an inert blit clear-color field.

The final regenerated MSVC build completed with zero warnings and errors. The focused portable
executable passed once plus 100 repeated runs, and default CTest passed 20/20. One initial plus 20
repeated public zero-file GPU smokes passed on `direct3d12`; each retained exactly three uploads/640
cumulative logical bytes, three releases, two blit frames/four successful draws, one clear-only
submission, one stale rejection, zero unavailable submissions, and zero residual residency. The
opt-in configuration passed 21/21 CTests, was restored to OFF, and listed 20 default tests. A public
two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI is tracked separately from
these local validation claims.

Counters, complete-list preflight, planning, submit-on-unwind, and failure ordering remain
unchanged. This in-process project value establishes no stable ABI, persistence, serialization,
wire/plugin, retail, pixel/readback, color-space, alpha, blending, display-expansion, or
`TextureStorageIR`/`AssetService` asset-binding contract.

E-0049 adds one private friend-only synchronous clear-readback seam without changing the public
renderer contract. An empty packet drives a temporary 2x2 `R8G8B8A8_UNORM` color target through
the same `RecordClearPass` used for production swapchain clears. The host downloads 16 tightly
packed bytes, disarms the command guard into fence-producing submission, waits, maps, explicitly
decodes four owned RGBA8 pixels, unmaps the transfer buffer, and releases the fence, transfer
buffer, and target through guards. It changes no production counter or portable texture residency.

The public zero-file smoke reads back `{0, 255, 0, 255}` and `{255, 0, 255, 0}` exactly from all
four pixels and proves complete snapshot invariance. A nonempty synthetic draw list fails before
SDL/GPU work with exact error `clear readback requires an empty draw list` and also leaves the
snapshot unchanged.

A clean incremental MSVC build issued four compile requests with zero warnings or errors. One
initial plus 20 repeated public zero-file `direct3d12` GPU smokes passed; every run preserved the
established three uploads/640 cumulative logical bytes, three releases, two blit frames/four
draws, one clear-only submission, one stale rejection, zero unavailable submissions, and zero
residual residency. Default CTest passed 20/20. The opt-in configuration passed 21/21, was restored
to OFF, and listed 20 default tests. A public two-frame D3D12 `openomega` smoke passed with dummy
audio. Publication CI is tracked separately from these local validation claims.

This confirms only those two synthetic endpoint values in a temporary offscreen target on the
observed D3D12 path. It establishes no stable public readback API or exposed SDL handle; no
swapchain/on-screen/presentation, sRGB/HDR, color-space, or intermediate-value rounding guarantee
and no guarantee for untested values; no alpha interpretation, blending, or composition semantics
beyond the exact tested 0/255 alpha bytes; no blit/filter or cross-backend pixel guarantee; no
arbitrary backend-failure
atomicity or production asynchronous queue/pin/fence contract; and no stable ABI, serialization,
persistence, wire/plugin, measured GPU-memory, streaming/eviction, display-expansion,
`TextureStorageIR`/`AssetService` binding, retail-rendering, or gameplay semantic.

E-0050 adds one private friend-only synchronous blit-readback seam without changing the public
renderer contract. An empty draw list fails before SDL/resource work with exact error
`blit readback requires a nonempty draw list`. A nonempty list completes every handle/backend-slot,
source-crop, filter, and fixed-4x4 target-plan check before creating a temporary
`R8G8B8A8_UNORM` target, 64-byte download buffer, or command buffer. Production and diagnostic
paths share filter mapping and source-order `LOAD` blit recording, while both retain the shared
clear-pass recorder. The probe submits with a fence, waits, maps, explicitly decodes sixteen owned
row-major RGBA8 pixels, and cleans up through guards without changing production counters or
portable residency.

The public zero-file fixture uploads opaque endpoint texels `R G / B W`, clears to opaque black,
applies one exact top-row Contain+Nearest blit and one later bottom-left Stretch+Nearest overwrite,
and reads back `KKKK/RRBG/RRBG/KKKK`. Both rejection and successful readback preserve the complete
host snapshot; probe release restores empty residency before the existing production flow.

The corrected MSVC build completed with zero warnings or errors. Default CTest passed 20/20. One
initial plus 20 repeated public zero-file `direct3d12` GPU smokes passed; every run ended with four
uploads/656 cumulative logical bytes, four releases, two production blit frames/four draws, one
clear-only submission, one stale rejection, zero unavailable submissions, and zero residual
residency. The opt-in configuration passed 21/21, was restored to OFF, and listed 20 default tests.
A public two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI is tracked
separately from these local validation claims.

This endpoint-only fixture confirms the two tested source/destination plans, command order, load
preservation, and sixteen exact opaque RGBA8 values on the observed D3D12 path. It establishes no
public readback API; general Nearest/Linear, crop, aspect, rounding, sample-center, edge/border,
Contain/Stretch, flip/cycle/mip/layer, alpha interpretation, blending, sRGB/HDR/color-space,
presentation/swapchain, cross-backend, asynchronous-lifetime, ABI/serialization, asset-binding,
retail-rendering, or gameplay guarantee.

E-0051 changes no production/runtime code or public interface. The added smoke fixture uses the
existing private fixed-4x4 readback with two simultaneously resident sources. A's first texel
supplies exact RGBA8 `{32, 192, 224, 255}` across the target; B's later first-texel command
overwrites center `[1,1,3,3)` with `{224, 80, 32, 255}`. The exact source-order Stretch+Nearest
result is
`AAAA/ABBA/ABBA/AAAA` on the observed D3D12 path. Full snapshot equality confirms unchanged
production counters and portable texture-pool snapshot fields, and the diagnostic packet remains
isolated from production.
This closes only E-0050's same-handle fixture gap for the two tested live backend sources.

The one-file MSVC build completed with zero warnings or errors. Default CTest passed 20/20. One
initial plus 20 repeated `direct3d12` smokes passed with unchanged exact totals of four uploads/656
cumulative logical bytes, four releases, two production blit frames/four draws, one clear-only
submission, one stale rejection, zero unavailable submissions, and zero residual residency. The
opt-in configuration passed 21/21, was restored to OFF, and listed 20 default tests. A public
two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI remains separate.

This proves only the two tested handles, first-texel crops, colors, commands, and fixed 4x4 result.
It establishes no arbitrary multi-texture list, slot/generation, crop/target, Stretch/Nearest,
interpolation, edge, aspect, rounding, Linear/Contain, alpha/blending/color-space,
swapchain/presentation, asynchronous lifetime, cross-backend, public readback, asset-binding,
retail-rendering, or gameplay guarantee.

E-0052 adds the bounded logical input-capture foundation. A move-only game-thread recorder copies
one validated nonempty, ascending, unique schema of at most 64 actions and pre-sizes 1 through
65,536 private 32-byte frame records without permitting contiguous `uint64_t` frame-index overflow.
At the hard maximum, those record elements plus the fixed 64-slot `uint32_t` schema backing contain
exactly 2,097,408 bytes of element payload. This does not measure excess vector capacity,
allocator/object overhead, or process RSS. Allocation-free atomic appends capture only post-binding
held/pressed/released masks and accepted/rejected counts. Allocation-free expected finalization
publishes an immutable move-only trace, including a valid empty trace; owned frame/action queries
remain distinct from the one borrowed schema view.

The final MSVC build completed with zero warnings or errors. The focused public zero-file test
passed once plus 100 repeated runs; default CTest passed 21/21. The opt-in Direct3D12 configuration
passed 22/22, was restored to `OFF`, and left 21 tests in the default list. Publication CI remains
separate. This is capture/storage/query infrastructure only, not input injection or playback,
scheduler timing/pacing, quit/run-control, simulation/gameplay state, replay execution, host
event/device capture, serialization, a file/wire/stable ABI, concurrent recorder use, or a retail
limit, timing, or determinism claim.

E-0053 adds the bounded scheduler-elapsed capture foundation. A move-only game-thread recorder
pre-sizes one private `int64_t` record for each of 1 through 65,536 configured slots after
validating a nonoverflowing contiguous `uint64_t` frame range. At the hard maximum, those record
elements contain exactly 524,288 bytes (512 KiB) of element payload, excluding excess vector
capacity, allocator/object overhead, and process RSS. Allocation-free atomic appends preserve
every signed nanosecond value with recorder-state, capacity, then frame-discontinuity failure
priority. Allocation-free expected finalization publishes an immutable move-only trace, including
a valid empty trace; `FrameAt` returns owned values. Published trace reads are reentrant on any
thread when no read races move or destruction.

A paired `FrameScheduler` test confirms identical per-frame plans, accumulator state,
planned-step totals, and dropped-time totals for direct and trace-retrieved elapsed values under the
tested configuration. The final MSVC build of the signed-nanosecond implementation completed with
zero warnings or errors. The focused `omega_scheduler_elapsed_trace_tests` executable passed once
plus 100/100 repeated runs; default CTest passed 22/22. The opt-in Direct3D12 configuration passed
23/23, was restored to `OFF`, and left 22 tests in the default list. The static native dependency
gate passed 133 files, and all 204 tooling tests passed. Publication CI remains separate. This is
capture/storage/query infrastructure only, not a clock source or timestamp-accuracy guarantee,
`FramePlan` capture or checkpoint restoration, input alignment beyond caller indices,
quit/run-control, simulation/gameplay, injection/replay/app wiring, CLI, persistence, a
file/wire/stable ABI, retail tick claim, or cross-configuration determinism.

E-0054 adds the SDL-free `omega_runtime` capture coordinator. One shared configuration validates a
capacity of 1 through 65,536 and a contiguous leaf range that may end exactly at `UINT64_MAX`, then
creates input backing before elapsed backing and publishes only after both succeed. Its exclusive
game-thread phase machine pairs every input with elapsed or terminal input. Elapsed uses the
internally retained input index; a terminal owns that index plus independent caller-supplied host
and logical quit flags and requires at least one true reason. Errors retain the operation stage,
fixed session category, and exact optional leaf code. Phase checks run first, and pretransition
failures are atomic.

An open empty session may finish, while pending input rejects finish without consumption. Valid
finalization consumes the session once leaf finish begins. Session and immutable pair are move-only
with nothrow moves and inert sources. The pair lends trace references and returns an owned optional
terminal value. Published pair reads are reentrant on any thread when no read races pair move or
destruction. At the hard maximum, the paired records and fixed schema backing contain exactly
2,621,696 bytes of element payload, excluding excess vector capacity, allocator/object overhead,
and process RSS.

The final MSVC build completed with zero warnings or errors. The focused
`omega_run_capture_session_tests` executable passed once plus 100/100 repeated runs; default CTest
passed 23/23. The opt-in Direct3D12 configuration passed 24/24, was restored to `OFF`, and left 23
tests in the default list. The static native dependency gate passed 136 files, and all 204 tooling
tests passed. Publication CI remains separate. This is capture coordination only, not `OmegaApp`
wiring, clock measurement, scheduler/`RunResult`/checkpoint capture, host quit detection beyond
caller flags, CLI, simulation/render/audio, persistence/file/wire/stable ABI,
injection/playback/replay, external-failure recovery/rollback, concurrent session use,
tracker-wide exhaustion safety, or a retail limit, timing, or determinism claim. The separately
published `InputTracker::next_frame_index()` accessor is future app-integration support only.
E-0055 must preflight a requested length `N` with `N`, not `N - 1`, before tracker-index wrap.

E-0055 adds exact owned scheduler snapshots and finite captured app runs.
`FrameScheduler::Snapshot` copies validated configuration, accumulated remainder, and lifetime
planned-step and dropped-time totals without adding restore or delta. Planning rejects negative
limits before those above 65,536. Zero frames use capacity-one empty backing at any next index.
Positive `N` requires `N <= UINT64_MAX - next_frame_index`, deliberately using `N` so the
following tracker index remains representable.

Move-only `RunCaptureOutcome` owns the requested limit, partial `RunResult`, completion,
before/after scheduler states, optional failure text, and an optional trace pair. `OmegaApp::Run`
and `RunWithCapture` share one loop, preserving ordinary `Run`. Capture preallocates before
logging, clock sampling, or mutation. Zero capture performs no host-loop service work. Active
capture appends exact `EndFrame` input, retains both quit flags on a terminal, or appends the exact
raw elapsed value before the same `BeginFrame` call. Only plan and creation errors return outer
`unexpected`. Operational and capture failures are nontransactional partial outcomes with
best-effort trace finalization. The CLI and `main` remain unchanged.

The clean MSVC build completed with zero warnings or errors. `omega_core_tests` passed.
`omega_run_capture_tests` passed once plus 100/100 repeated runs; default CTest passed 24/24.
With Direct3D12 and dummy audio, `omega_app_capture_smoke` passed once plus 20/20 repeated runs.
Its unowned-draw fixture forced a real render error and retained one paired input/elapsed sample,
zero rendered frames, owned failure text, and the scheduler boundary. The next capture resumed
successfully. The public zero-file `openomega.exe --frames=2` path succeeded with two rendered and
input frames and equal planned and executed steps. GPU CTest passed 26/26. Registration was
restored to `OFF` with 24 default tests. The dependency gate passed 140 files, all 204 tooling tests
passed, and Python compile-all passed. Publication CI remains separate.

This establishes no capture CLI, replay, input/playback injection, restore, persistence,
serialization, wire format, stable ABI, simulation checkpoint, RNG state, fake services,
rollback, ordinary `Run` tracker-exhaustion guarantee, or retail timing or determinism claim.

E-0056 adds the explicit finite-capture command. Exact once-only `--capture-run` requires an
explicit `--frames=N` from 1 through the shared synthetic maximum of 65,536. Without the flag,
ordinary zero and above-65,536 frame values retain their parser behavior. Capture cannot compose
with probe mode, help remains standalone, and only the explicit route calls `RunWithCapture`;
ordinary runs continue through `Run`.

Captured output includes the ordinary `RunResult` counters plus aggregate trace presence/counts,
optional terminal metadata, and selected absolute before/after scheduler counters derived from
snapshots, not complete snapshots or deltas. The tested `IsCompleteRunCaptureOutcome` helper fails
closed unless a positive request reaches `FrameLimitReached` without failure, quit, or terminal and
with exact result/trace counts and capacities, matching trace origins, and planned/executed-step
agreement. The portable process contract covers exact ordinary-zero and invalid-capture process
behavior. The opt-in two-frame GPU smoke uses a CMake wrapper that checks the child exit code before
matching the run, aggregate trace, and scheduler summaries.

The clean incremental MSVC build completed with zero warnings or errors. `omega_core_tests` passed.
`omega_run_capture_tests` passed once plus 100/100 repeated runs; default CTest passed 25/25.
Direct3D12 plus dummy audio passed GPU CTest 28/28, including the capture CLI smoke. Registration
was restored to `OFF` with 25 default tests. The dependency gate passed 140 files, all 204 tooling
tests passed, and Python compile-all passed. Publication CI remains separate.

This adds no capture files, persistence, serialization, wire format, stable ABI, per-frame
printing, replay, injection/playback, restore, delta interpretation, checkpoint, RNG state,
rollback, interactive or zero-frame capture command, probe composition, ordinary-above-65,536
execution claim, or retail timing or determinism semantics.

E-0057 adds the SDL-free `omega_runtime` `RunCaptureReplaySession`. This concrete move-only value
owns a moved `RunCaptureTracePair` and advances one exclusive game-thread cursor. It validates both
leaf traces, shared capacity/origin, normal-versus-terminal counts, and terminal reason/index before
taking ownership; rejection leaves the caller's pair unchanged. Each successful `Next` publishes a
`RunCaptureReplayFrame` with an exact owned immutable `InputSnapshot`, retaining the recorded frame
index and schema, held/pressed/released masks, and accepted/rejected event counts. The snapshot is
paired with exactly one owned successor: the captured signed elapsed value for a normal frame or
the independent host-quit/logical-quit flags for a terminal frame.
`RunCaptureOutcome::TakeTracePair` is
rvalue-only; it transfers the optional pair and normalizes the whole outcome inert even when no
pair exists.
`InputSnapshot` reconstruction allocation or defensive trace-read failure does not advance the
cursor; exhausted and moved-from sessions report distinct complete and invalid-state errors.

This milestone adds no SDL, `OmegaApp`, or CLI replay path; input injection, `InputTracker`
mutation, or synthetic physical events; scheduler creation, feeding, pacing, clocking, or restore;
simulation checkpointing, RNG restore, or world mutation; render, audio, or job work; persistence,
serialization, file/wire/stable ABI, or cross-process contract; seek, rewind, looping, rollback, or
retail timing or determinism claim.

E-0058 adds the portable app-layer `RunReplaySession`. It owns a lower replay cursor, a fresh
scheduler created from caller-provided synthetic timing configuration, and a fresh empty simulation
world with the matching fixed step. Reported creation failures preserve the capture pair until all
fresh owners are ready. Successfully published elapsed frames advance the scheduler and execute
every planned world step; terminal frames complete without changing either subsystem. Lower replay
failures are retryable and transactional. The defensive representation branch is unreachable under
the current 65,536-frame, 64-step-per-frame, and one-second-step bounds. If those bounds expand and
it is reached, consumed replay/scheduler work and earlier world steps make the session permanently
failed. Move-only frame
publications and owned scheduler/world snapshots expose the resulting fresh timeline without
borrowing mutable subsystem state.

This is not captured scheduler/world restoration or replay into an existing `OmegaApp`. Input is
reconstructed for observation but not injected into simulation. The milestone adds no captured
entity or RNG checkpoint, gameplay reconstruction, host-event synthesis, pacing or clock ownership,
CLI route, render, audio, jobs, persistence, file/wire/stable ABI, cross-process compatibility,
seek, rewind, looping, rollback, or retail timing or determinism claim.

E-0059 adds one-process finite capture followed by immediate fresh replay through the exact
`--frames=N --capture-run --replay-capture` command surface. Replay requires the existing explicit
capture range of 1 through 65,536. Ordinary and capture-only paths remain unchanged. Before moving
the trace pair, `main` requires a complete, consistent, nonterminal capture and a capture scheduler
that began with zero remainder and counters. Replay then runs synchronously on the main thread with
a fresh scheduler and empty world, failing immediately on any replay or comparison error.

Success requires replay aggregates to match captured frame, step, clamp, and drop totals; the final
scheduler snapshot to match the captured final snapshot; and the fresh world's final step count,
simulated time, and zero-entity state to agree. Only success prints the fixed
`OpenOmega fresh replay:` aggregate line. This is not replay into the existing `OmegaApp`; the
main-thread replay orchestration makes no calls into or reads from it. The host remains alive, so
its audio callback may continue independently. `RunReplaySession` itself performs no pacing, clock
sampling, rendering, audio, or job work. The command adds no terminal or incomplete CLI replay,
input injection or world input use, gameplay reconstruction, captured scheduler/world, entity or
RNG restoration, persistence, file/wire/stable ABI, cross-process format, seek, rewind, loop,
rollback, or retail timing or determinism claim.

E-0060 adds the first synthetic input-driven world component. `SimulationWorld` now owns bounded
preallocated signed `Position3` values behind exact generational identities. Positioned creation,
query, cleanup-before-reuse, and optional one-translation fixed steps are allocation-free after
startup. Each translating step validates the clock, target, component, and all three signed sums
before committing position and clock together; the legacy no-input step stays neutral and
backward-compatible. The separate portable `omega_gameplay` planner accepts only digital
`-1/0/1` axes and maps them to one project unit on X/Z.

The native host owns one positioned diagnostic actor and maps W/S/A/D and gamepad D-pad held state
to synthetic actions 2 through 5. Opposites cancel, terminal input remains nonmutating, and the one
frame command reaches every scheduled fixed step. Replay locomotion is explicit and default-off;
the capture/replay CLI enables it only for a schema containing all four actions while preserving
the fixed E-0059 output line and the no-read boundary with the still-live app. Portable tests cover
the complete command domain, component lifecycle, error priority, signed overflow, release, and
fresh replay. The real SDL host smoke uses a nonzero step count and compares host and replay final
positions. This remains an invisible debug actor, not a retail player implementation, and establishes
no retail controls, coordinates, rate, analog behavior, physics, camera, animation, mission, asset,
network, or determinism semantics.

The first E-0061 slice adds `DiagnosticMenuState`, its pure toggle-edge reducer, and a deterministic
project-generated `DEV` diagnostic card to `omega_app_core`. The standalone test freezes dimensions,
independent ownership, full opacity, geometry, exact color populations, and complete byte digest in
normal and runtime-disabled builds. Action 6 is reserved only; no host binding exists. The card is
not uploaded or rendered, and this slice establishes no retail-menu art, text, palette, layout,
navigation, selection, activation, pause, control-layout, timing, asset, persistence, replay, or UI
behavior semantics.

E-0062 connects the value to the native host as synthetic nonmodal developer UI. F1 and gamepad
Start produce action 6; capture retains the ordinary input row, terminal handling precedes the
toggle, and normal toggle frames continue through movement and simulation. Startup uploads the
128x72 card once and builds fixed hidden/base and visible/base-plus-menu draw lists. The menu command
uses Q16 source `{0,0,65536,65536}`, exact destination `{2048,2048,26624,15872}`, `Stretch`, and
`Nearest`; frame publication only selects a list. Replay reconstructs action 6 but owns no menu
state, terminal input does not change host visibility, and the existing fresh-replay CLI line is
unchanged. This adds no retail menu, pause, navigation, selection, layout, timing, rendering,
persistence, or replay semantics.

E-0062 local validation is green: a clean zero-warning MSVC build; focused portable and real-host
passes; 20/20 repeated host runs; 29/29 default and 33/33 opt-in GPU CTest; 20/20 unchanged
capture/replay CLI repetitions; an exact runtime-off portable target build/run with 26 registered
tests; 152 native dependency files; 209 tooling tests; Python compile-all; and 239 indexed public
text blobs. The exact E-0062 main tree subsequently passed the Windows x86-64 native, Linux x86-64
native, and public-tree safety publication-CI jobs.

E-0063 makes the project diagnostic menu the explicit native startup state without assigning it
retail meaning. The safe default value remains `DiagnosticPlay` on `StartDiagnosticPlay`, while
`OmegaApp` explicitly starts in `MainMenu` on that first row. Invalid enum state resets to the
startup value before edge processing. Existing actions 2/3/6 remain W/S plus gamepad D-pad
Up/Down and F1 plus gamepad Start. The reducer consumes press edges only: primary has priority,
switches diagnostic play to main-menu row zero, switches the first main-menu row back to play, and
is inert on the two reserved rows; previous/next clamp and simultaneous edges are neutral.
Terminal handling remains first, and visible-menu frames remain nonmodal for held locomotion,
scheduling, simulation, capture, and fresh replay. Replay owns no menu state.

Startup still performs only the existing 128x72 card upload. It builds one immutable hidden/base
list and three immutable base/card/amber-marker lists. The card retains full source
`{0,0,65536,65536}` and target `{2048,2048,26624,15872}`. The marker reuses the same texture crop
`{18432,9103,59392,14563}` at row targets `{3584,7424,4352,9344}`,
`{3584,10304,4352,12224}`, and `{3584,13184,4352,15104}`; all use `Stretch`/`Nearest`.
Frames select a prebuilt list without another upload, list construction, or per-frame menu
allocation. Every mode, row, binding reuse, clamp, priority, crop, and target is synthetic and
establishes no retail menu, control, pause, activation, persistence, timing, replay-UI, private
input, or PCSX2-equivalence claim. E-0063 local validation used only project-generated zero-file
fixtures and no private inputs. The incremental MSVC build completed with zero warnings and zero
errors. The portable test passed directly plus 20/20 repetitions, and the real Direct3D12 host
smoke passed directly plus 20/20 repetitions. Default CTest passed 29/29 before GPU enablement and
again after restoring GPU registration off; the opt-in GPU matrix passed 33/33, and the unchanged
capture/replay CLI smoke passed 20/20. Runtime-off validation built the exact portable target,
which passed directly and through CTest, with 26 tests registered. The native dependency gate
checked 152 files, all 209 tooling tests passed, Python compile-all passed, and the public-tree gate
checked 239 indexed text blobs. Publication CI remains separate and unclaimed.

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
