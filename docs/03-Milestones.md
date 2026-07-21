# End-to-end milestones

Each milestone ends with an executable regression, not only a research note.

## Cross-cutting engine and SDK evolution

M0 through M8 remain the compatibility and release spine. In parallel, every milestone follows the
compatibility-first direction in [`ADR 0004`](adr/0004-compatibility-first-engine-sdk.md) and the
staged plan in [`07-Engine-and-SDK-Roadmap.md`](07-Engine-and-SDK-Roadmap.md):

- Retail parsing stays in bounded compatibility adapters.
- Passive descriptors remain inspection-only until independent evidence supports semantic promotion.
- Every published value owns its data. Existing source-preserving compatibility values may retain
  structural text, raw flags, observed fields, or locators without becoming stable engine IR.
- Promoted semantic/cooked IR excludes borrowed retail spans, retail offsets, platform objects, and
  speculative fields, with provenance kept separate.
- Project-owned and retail inputs converge only at stable semantic/import contracts.
- The promoted-engine target is for runtime services to consume promoted semantic or cooked data;
  the current `omega_content`-to-retail path remains a grandfathered transitional compatibility
  composition until its provider/import boundary exists. Tools and editors depend downward on SDK
  contracts and never become shipping-engine dependencies.
- Generalization waits for a second real consumer or a concrete retail-independence test.
- Every promotion records ownership, thread affinity, resource bounds, reload behavior, evidence,
  synthetic regressions, and any remaining compatibility gap.

This work happens as compatible vertical slices mature; M9 and M10 are measurable culmination proofs,
not a reason to postpone reusable boundaries.

## M0: Reproducible laboratory

Status: complete for the current NTSC-U research baseline.

- Current PCSX2 source build, isolated BIOS/disc data root, known game hash, boot log.
- E-0099 records runnable-tool and configuration-initialization readiness for a separately maintained
  custom PCSX2 observation stack based on that pinned source. Its VUM, front-end, and coordinated
  lifecycle commits compile in both a one-job MSVC RelWithDebInfo `PCSX2` core target and a true
  Release `pcsx2-qt` target. The linked Release executable has a recorded SHA-256, release-only
  dependency chain, embedded producer namespaces, and isolated version/configuration startup probes.
  This establishes no producer capture, owner-input access, sanitized report, or behavioral
  observation.
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
and SKL descriptors, a fixed-output retail-only SKA descriptor, bounded canonical LPD and SKAS
envelope adapters, a fixed passive VPK wrapper descriptor, and an analysis-only passive SO module
descriptor are also implemented. E-0101 records passive FNT, GUI, and IE project-defined prefix
hypotheses; generated fixtures confirm their implementation boundaries, while the retail provenance
of their constants remains unrecorded. They are nonsemantic offline scaffolding, not front-end IR.
Other scene decoders remain incomplete.

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
  SKA descriptor implements the aggregate-proven neutral counted-word extent as fixed output, while
  the separate bounded SKAS adapter owns only exact structural text and opaque line ranges. Neither
  assigns animation semantics or a relationship between SKA and SKAS. The native corpus verifier
  accepts 213/213 SKA spans with zero errors: 158 exact, 55 zero-padded, and 2,180,832 aggregate
  logical bytes.
- E-0091 adds an owned LPD counted-envelope IR and stateless two-pass decoder for the fixed
  22-word/21-track structure. It preserves every source-order four-byte entry as opaque bytes,
  accepts exact input or any all-zero tail through the aggregate-proven 1,932-byte maximum, and
  reports truncation, the earliest dirty-tail byte, and input/item/output limits deterministically
  before allocation. The observed 8-byte corpus tail minimum is not promoted to a minimum or
  alignment rule. The fixed 4,096-byte physical-input maximum derives fixed 1,002-entry,
  1,024-item, and `sizeof(LpdEnvelopeIR) + 4,008`-byte output ceilings. Explicit final-sized vector
  construction keeps first and later allocation failures inside the typed error boundary. Scratch
  and nesting remain zero, and no track, scalar, timing, interpolation, pose, animation, audio-link,
  or playback semantics are assigned.
- E-0094 adds a fixed-output, stateless VPK wrapper-envelope decoder over the public aggregate
  boundary. It requires the exact raw bytes `b" KPV"`, an independent little-endian 2,048 word at
  `0x08`, physical spans from 1,320,960 through 9,005,056 bytes, and independent divisibility by
  2,048. Its fixed descriptor preserves the two unassigned four-byte prefix fields in source order
  and publishes only physical byte count plus derived aligned-block count. One descriptor and its
  fixed size debit item and output budgets; the complete input debits input. Scratch and nesting
  remain zero. The remainder and payload are borrowed only during the call and never retained or
  inspected. The equal values of the observed word and alignment establish no relationship. No
  codec, ADPCM, sample rate, channels, audio or music role, seek table, streaming, storage geometry,
  playback, runtime, or emulator semantics are assigned.
- E-0098 adds a stateless passive SO module descriptor over the recovered custom little-endian
  framing. It owns bounded section ranges, counts, neutral type/enum/global/callable summaries, and
  structural regularity results while retaining no string content, code-cell value, or opaque
  payload byte. The fixed 512 KiB input ceiling is a synthetic decoder safety policy and callers may
  only tighten input, item, output, and string budgets. The descriptor is not canonical script IR,
  is not composed into content or simulation, and never executes, interprets, translates,
  recompiles, or dispatches retail cells.
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
E-0102 also implements one explicit modal opening-movie path. It validates bounded MPEG-PS, H.262,
and the accepted PSS PCM variant; presents decoded video and audio through owned buffers, a stable GPU
texture, a fixed audio ring, and a device-demand clock; and supports skip, EOS, safety timeout, and
fail-open transition to the existing front end without scheduler catch-up. Capture/replay remains
mutually exclusive. This does not establish general media selection/mixing, non-Windows end-to-end
playback, perceptual synchronization, or exact retail audiovisual timing.
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
limits. The app also owns the SDL process lifetime and a system-default playback stream. Its callback
supplies bounded project-owned silence while idle and consumes opening-movie samples from a fixed
ring. Project callback code performs no loading, logging, explicit locking, or dynamic allocation.
The main thread validates and deinterleaves the accepted PSS PCM variant, refills the ring, and
contains it on transition; this is not a general mixer, final hardware-playback proof, or retail
timing-parity claim. A synthetic positioned diagnostic component and pure digital
locomotion planner now exercise the world/input boundary, but retail player components and gameplay
systems, render scene snapshots, general decoded voices, and mixing remain incomplete.

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

E-0064 renders readable project-authored 3x5 labels into the same generated 128x72 diagnostic card:
`OPEN OMEGA`, the fixed control legend, `START DIAGNOSTIC`, and both `RESERVED SLOT` rows. It adds
no font or asset input and preserves the single startup texture upload, one hidden list, three
prebuilt visible lists, existing card/marker geometry, and allocation-free per-frame list selection.

The post-reducer menu state becomes an app-layer simulation gate in both live and opt-in replay.
`DiagnosticPlay` passes measured/captured elapsed to the scheduler; `MainMenu` or invalid state
passes zero and skips locomotion/world work. Reduction occurs first, so freeze and resume take
effect on the transition frame, while advancing the live clock baseline prevents modal catch-up.
Capture still records actual elapsed, and modal frames continue input/capture, rendering, audio,
and job-service operation. Replay's optional caller-supplied initial menu state defaults absent for
legacy nonmodal compatibility; the finite fresh-replay route supplies the native startup state
internally. No action/binding, input or trace schema, captured checkpoint, serialization, CLI
syntax, or CLI output changes. These labels and modal rules remain synthetic and establish no
retail menu, pause, timing, persistence, or PCSX2-equivalence semantics. The final incremental MSVC
build was warning-free. Portable diagnostic and replay tests each passed directly plus 20/20
repetitions; the Direct3D12 host smoke passed directly plus 20/20 repetitions; default CTest passed
29/29 before and after the opt-in 33/33 GPU matrix; and capture/replay CLI passed 20/20 repetitions
plus one 20-frame run. Runtime-off built and ran the exact portable target, its focused CTest passed,
and 26 tests were registered. The dependency gate checked 152 native files, all 209 tooling tests
passed, Python compile-all passed, and the public-tree gate checked 239 indexed text blobs.
Publication CI remains separate and unclaimed.

E-0065 promotes synthetic main-menu row one to `ShowControls` and adds a third `Controls` mode while
preserving startup at MainMenu row zero, the safe DiagnosticPlay default, action IDs and bindings,
and the existing capture schemas. Invalid representations fail closed. Primary has priority, enters
Controls from main row one, returns every Controls row to main row one, and leaves reserved row two
inert. Previous/next remains press-edge-only, MainMenu-only, clamped, and simultaneous-neutral.
MainMenu and Controls both gate simulation; only a fully valid DiagnosticPlay state runs it.

The main card now labels row one `CONTROLS` and has exact four-color populations
3,739/1,491/3,506/480 with FNV-1a-64 `0x5303b94979cd74d6`. A second independent generated 128x72
opaque RGBA8 card labels `CONTROLS`, `W FORWARD`, `S REVERSE`, `A LEFT`, `D RIGHT`, `F1 RETURN`, and
`ESC QUIT`; its populations are 2,104/1,326/5,373/413 and FNV-1a-64 is
`0xa68873cc7444bdf6`. Startup uploads both project cards once and retains one controls draw list in
addition to the hidden and three MainMenu lists. The public zero-file host owns two uploads and
73,728 resident logical bytes; Controls renders one card blit, MainMenu renders card plus marker, and
DiagnosticPlay remains clear-only. All lists are cleared before reverse presentation-texture release.

Live and opt-in replay resolve terminal input before reduction, apply the reducer before the modal
elapsed gate, ignore held primary levels, and discard Controls elapsed without later catch-up. Null
menu replay stays legacy nonmodal. No action/binding, input or elapsed trace schema, captured
checkpoint, persistence, serialization, CLI, file, wire, or stable-ABI schema changes. E-0065
validation used only public project-generated zero-file fixtures. The final MSVC build was clean;
diagnostic and replay tests passed directly plus 20/20 repetitions; Direct3D12 host testing passed
directly plus 20/20; default, opt-in GPU, and restored-default CTest passed 29/29, 33/33, and 29/29;
and a 20-frame capture/replay plus 20/20 short repetitions passed. Runtime-off focused direct and
CTest checks passed with 26 registrations. Dependency, tooling, compile-all, and public-tree gates
passed at 152 native files, 209 tests, and 239 indexed text blobs. Three validation-only
`SimulationState` C2676 comparisons were corrected to the fieldwise helper. A direct non-`vcvars`
configure also contaminated generated cache state; the exact MSVC linker, archiver, and flags were
restored without changing source. No private data, disc image, retail executable, emulator, or PCSX2
input was used. This proves only synthetic developer presentation and gating, not retail menu,
controls, pause, timing, persistence, private-input behavior, or emulator equivalence. Publication CI
remains separate and unclaimed for E-0065.

E-0066 adds a portable metadata-only `TextureStorageIR` to `runtime::DebugImage` adapter without
wiring canonical texture storage into `OmegaApp`, `AssetService`, GPU upload, or the retail decoder.
The reentrant, no-I/O call borrows one canonical value, applies a strict typed fail order and
independent block/plane/palette-entry/output-byte budgets, then returns fully owned RGBA8 pixels.
Source-order blocks occupy 32x32 tiles with at most eight columns. Opaque background/slate framing,
cyan enum masks `0x1`/`0x9`/`0x7`/`0xf`, and an amber palette-presence plus are the complete visual
vocabulary. After validation, only block order, sample/transfer encodings, and palette presence can
affect the image; bytes and dimensions remain validation inputs. The canonical three-block fixture
is 96x32 (12,288 bytes), has exact background/slate/cyan/amber populations 2,667/372/23/10, and
FNV-1a-64 `0xb56c8db088c5a9fe`.

MSVC configure, focused-target, and full builds were clean with zero warnings or errors. The focused
test passed directly plus 20/20 repetitions, focused CTest passed, and default CTest passed 30/30.
Runtime-off direct and focused checks passed with 27 registrations. The dependency gate checked 155
native files, all 209 tooling tests passed, Python compile-all passed, and the final staged-tree
public gate checked 242 indexed text blobs. No private data, D-drive content, disc image, retail
executable, emulator, or PCSX2 input was used.
Publication CI remains separate and unclaimed. This proves no retail/display/channel/swizzle/palette,
material, geometry, asset-binding, app, or GPU semantics.

E-0067 proves a test-only end-to-end composition from the generated public TDX fixture through
`LevelTextureStore`, asynchronous `JobService`/`AssetService` publication, and the E-0066 topology
adapter. Two distinct direct-24 payloads produce independently owned, pixel-identical 32x32 RGBA8
images; both asset slots release back to an exact empty snapshot without invalidating either image.
No production API, startup selection, app state, GPU upload, draw command, or retail binding is
added. Texture display expansion and every material, geometry, placement, and visibility relationship
remain unresolved.

E-0068 adds a zero-file, project-owned `ASSET TOPOLOGY` screen on main-menu row two. The new byte-3
mode uses the exact E-0066 three-block public fixture, constructed before any SDL/platform mutation,
then uploads its owned 96x32 topology image once and retains one immutable optional-base-plus-card
`Contain`/`Nearest` draw list. Zero-file startup now owns three uploads, three resident textures, and
86,016 logical bytes; teardown clears all lists before reverse-order release. The updated main card
hashes to `0xf37b700c33071a92`, and the topology raster remains `0xb56c8db088c5a9fe`.
Live and opt-in replay treat the screen as modal through the existing reducer-before-gate policy:
raw elapsed remains captured, simulation and locomotion freeze, terminal input precedes reduction,
and returning cannot schedule accumulated menu time. Real Direct3D12 testing verifies sixteen exact
topology texels and the full entry/hold/release/terminal/return lifecycle. No retail source selection,
per-frame allocation, action/binding, trace schema, asset-service API, material, geometry, or display
semantics are added.

E-0069 makes the synthetic frontend conventionally confirmable without expanding its logical
schema. Return, keypad Enter, and gamepad South join F1 and gamepad Start on action 6, producing 15
physical bindings and the same six unique actions. First-down/last-up aggregation prevents alias
repeat or premature release, while capture/replay, terminal priority, modal timing, resources, and
teardown remain unchanged. Project-generated legends become `F1/ENTER` and `F1/ENTER RETURN`; the
main and Controls hashes become `0x0a1373c69c8bcce2` and `0xd57a5b0500696505`. This improves native
main-menu usability without asserting a retail input map or platform-specific gamepad label.

E-0070 makes vertical navigation conventional without expanding the six-action schema. Keyboard Up
joins W and gamepad D-pad Up on action 2; keyboard Down joins S and gamepad D-pad Down on action 3,
for exactly 17 physical bindings. First-down/last-up aggregation prevents alias repeat and premature
release. Real SDL coverage drives Down, overlapping S, non-final/final release, Up navigation, and
upper clamping while preserving modal owner state. The project legends become `W/S/UP/DOWN`,
`W/UP FORWARD`, and `S/DOWN REVERSE`; the main and Controls hashes become
`0x9a4662f8f943521d` and `0xcfa7cc57696aae0a`. Capture/replay rows, reducer and terminal priority,
modal timing, topology, resources, and teardown remain unchanged. Up/Down also alias the shared
synthetic diagnostic locomotion actions and establish no retail input or movement semantics.

E-0071 adds a fail-closed startup-owner classifier before `OmegaApp` allocation and platform work.
Only empty, game-data-only, and complete five-owner level-content states classify as `NoContent`,
`DataMounted`, and `LevelContent`; every partial shape returns typed `InconsistentOwnership`.
`OmegaApp::Create` copies the valid stage, moves content once, and otherwise returns exact text
`content startup state: inconsistent-ownership` without touching SDL. The stage appears only on the
project-generated main card as `CONTENT NONE`, `CONTENT DATA`, or `CONTENT LEVEL`; invalid stage bytes
render `NONE`. The exact hashes are `0x8e8e3f7fff4f971a`, `0x517ad52bbf1fbe61`, and
`0x08405186aa105db1`. Controls remains `0xcfa7cc57696aae0a`, topology remains
`0xb56c8db088c5a9fe`, and resource counts, draw geometry, input, reducer, modal timing,
capture/replay, teardown, and all retail-data boundaries are unchanged. This is synthetic developer
owner-health presentation, not a retail startup or UI claim.

E-0072 adds a fail-closed presentation adapter for the existing all-or-error startup boundary.
`DescribeContentStartupError` borrows the nonempty outer message, allocates nothing, is `noexcept`,
and accepts only the four nested-error shapes produced by `StartContent`. Unknown outer or nested
codes and missing, unexpected, or both nested errors return typed `InconsistentRepresentation`.
The underlying code-name functions retain their stable `unknown` fallback. Valid process output is
unchanged,
while an inconsistent representation has one fixed sanitized error. A CMake-created empty-root
process case freezes nonzero exit, empty stdout, and the existing exact missing-`SYSTEM.CNF` stderr
before SDL/GPU or owner input. Startup ownership, DiagnosticPlay/menu behavior, assets, resources,
retry/fallback policy, persistence, schemas, capture, and replay remain unchanged. The slice is
synthetic-only. Focused/full MSVC, direct core/process, 30/34/30 CTest, runtime-off with 27
registrations, 157-file dependency, 209-tooling-test, and compile-all validation passed.
Publication remains unclaimed.

E-0073 replaces clear-only no-level `DiagnosticPlay` presentation with one synthetic project-owned
placeholder while leaving level-content rendering intact. The independent opaque 128x72 RGBA8
builder draws the existing frame/header plus `DIAGNOSTIC PLAY`, `NO LEVEL IMAGE`, `F1/ENTER MENU`,
and `ESC QUIT`; its exact four-color populations are 3,327/1,285/4,124/480 and FNV-1a-64 is
`0x37f823d27a4cb3ce`. Valid owner images still win. Otherwise the placeholder uses the existing
diagnostic upload/error/handle/list path before menu, controls, and topology, yielding four distinct
zero-file textures and exactly 122,880 resident logical bytes. MainMenu now draws base/card/marker,
Controls and AssetTopology draw base/card, and no-level DiagnosticPlay draws the one full-target
`Contain`/`Nearest` base. Reverse teardown is unchanged. START DIAGNOSTIC stays available in
`NoContent` and `DataMounted`; reducer, simulation, locomotion, elapsed, return, terminal, capture,
and replay contracts are unchanged. The byte total is no-level-only. This is not retail UI, level
selection, full-framebuffer identity, DataMounted hardware coverage, private-input evidence, or
emulator equivalence. Focused/full MSVC, direct and 20/20 repeated diagnostic/replay/GPU checks,
30/34/30 default/GPU/restored CTest, 20-frame and 20/20 short capture-replay, runtime-off with 27
registrations, the 157-file dependency gate, 209 tooling tests, and Python compile-all passed.
Publication remains unclaimed.

E-0074 adds a typed, explicit-only configuration path for content selection. The effective
`--config` plus `--set` store recognizes `content.data_root` and `content.level_code` and is always
validated before direct CLI precedence. A valid direct root plus optional level wins as one tuple;
it never inherits a configured level. No content keys preserve zero-file startup. A configured level
without a root, an empty or unrepresentable native root, an invalid level, and an inconsistent direct
pair map to four fixed sanitized error categories. Valid levels contain 1 to 32 ASCII alphanumeric
bytes and normalize uppercase. Main resolves this tuple after service settings and before unchanged
content startup and E-0072 diagnostics. The root-level local `openomega.cfg` is ignored for privacy,
but E-0074 does not discover it or any other ambient/default file. It adds no persistence, picker,
path existence validation, content decoding, retail UI, or emulator-equivalence behavior. Local
focused/full MSVC, direct core/process, 30/34/30 default/GPU/restored CTest, runtime-off with 27
registrations, the 157-file dependency gate, 209 tooling tests, and Python compile-all passed.
Publication remains unclaimed.

E-0075 adds optional per-user default-profile discovery while preserving the explicit and content
precedence contracts. A no-I/O compile-time host classifier and pure lexical resolver accept only
nonempty absolute captured roots and append the fixed Windows, macOS, or XDG profile suffix. Main
captures only relevant environment values after successful parsing/help and only without
`--config`; Windows uses the wide environment representation. Explicit config bypasses default
inspection. A missing default is the empty store, a regular default uses the bounded loader, and a
reported final-entry symlink, dangling symlink, directory, or other non-regular type is rejected
without following. This does not claim rejection of parent symlinks or all reparse points.
Configuration failures now use fixed explicit/default/`--set` categories without disclosing source
paths, user-controlled keys, or raw values; only structural line/budget data and compile-time-known
public setting labels remain. Values remain below `--set`, E-0074 validation, and atomic direct CLI
selection. Discovery performs no normalization, canonicalization, expansion, write, creation, migration, path
success output, picker, dialog, default-level choice, private-content access, retail inference, or
emulator comparison. Serialized local validation passed: focused and full MSVC builds completed cleanly;
direct `omega_core_tests` and the exact process contract passed; default, opt-in GPU, and restored
CTest passed 30/30, 34/34, and 30/30; runtime-off direct and focused checks passed with 27
registrations; the dependency gate checked 160 native files; all 209 tooling tests and Python
compile-all passed; and the staged public-tree gate checked 247 indexed text blobs. On Windows, the
non-missing inspection-error oracle was explicitly skipped
because MSVC maps the available invalid and overlong candidates to not-found. Commit, DCO,
publication, and exact-main validation remain unclaimed. The later non-reflective diagnostic
hardening passed scoped diff checks, the 244-file dependency gate, the 411-blob public-tree gate,
Python compile-all, and all 298 tooling tests. Its C++ build, process contract, and CTest remain
delegated to the serialized integration lane because local preflight was `CAUTION`.

E-0076 adds a stateless, app-private startup-failure dialog for the already-fatal pre-SDL
runtime-configuration, runtime-settings, content-launch-profile, and content-startup paths. Exact
stderr and exit behavior remains unchanged; stderr is flushed before a best-effort parentless SDL
error message. Fixed 48-byte category and 384-byte detail projections use bounded local stack
storage to produce an owned 640-byte result, sanitize hostile bytes to bounded ASCII, and retain no
source text. Only exact
`OPENOMEGA_DISABLE_STARTUP_DIALOG=1` suppresses presentation, invalid policies fail closed, and
CMake suppresses all automated process/capture presentation. The unit source exercises only
suppressed calls and verifies no SDL initialization. Later startup and runtime failures stay
console-only. Serialized local validation passed: focused and full MSVC builds; the direct dialog
unit and exact process contract; CTest 31/35/31; runtime-off direct and focused `omega_core_tests`
with 27 registrations and no dialog target; the 163-file dependency gate; all 209 tooling tests;
Python compile-all; and the staged public-tree gate checked 250 indexed text blobs. Interactive
dialog smoke, commit, DCO, publication, and exact-main validation remain unclaimed. No private or
owner files, D-drive content, disc image,
executable,
emulator, or PCSX2 input was used.

E-0077 connects the first canonical `LevelTextureStore` entry to the existing metadata-only
topology raster through a stateless, exclusive-startup `AssetService` adapter. The adapter admits
only an aggregate-empty service, requests canonical handle zero exactly once, waits, builds while
the immutable view is borrowed, makes every accepted request reach `Release`, and requires all ten
public snapshot fields to match for success. Fixed identity-free errors retain only applicable typed
nested enums, with release and residual-state failures taking precedence over earlier Get/image
results. Complete
`LevelContent` now builds the resulting independently owned 32x32 topology before SDL startup,
then uploads it through the unchanged fourth-texture path; the generated fixture pins four textures
and 77,840 resident bytes plus exact GPU probes.
`NoContent` and `DataMounted` retain the synthetic 96x32 topology and 122,880 bytes. Serialized local
validation passed focused/full MSVC, direct and 20/20 repeated D3D12 app smokes, 31/35/31 CTest,
runtime-off focused checks with 27 registrations, dependency 165, tooling 209, and compile-all. The
staged public-tree gate checked 252 indexed text blobs. The merged commit carries its matching DCO
sign-off; PR #38 published E-0077 as exact `main` commit
`2a9182e560a504125a5b8278a7202fcad7220c44`, and exact-main run `29710089254` passed all three
jobs. No retail display,
material, geometry, gameplay, streaming, or emulator-equivalence semantics are assigned.

E-0078 adds a strict, payload-sensitive Packed24 transfer diagnostic utility without wiring it into
the app or renderer. One block, one plane, matching nonzero rectangles, no palette, known Packed24
sample/transfer enums, checked three-slot source and four-slot output sizes, and independent limits
are required. It copies source slots zero through two and supplies a synthetic `0xff` fourth slot in
owned output. Generated 16x16 seeds pin the two frozen hashes, all sixteen typed diagnostics,
validation priority, exact cleanup-independent ownership, and overflow/budget behavior through a
standalone runtime-off-capable test. Serialized local validation passed focused/full MSVC, the unit
direct plus 100/100, 32/36/32 CTest, runtime-off focused checks with 28 registrations, dependency
168, tooling 209, and compile-all. The staged public-tree gate checked 255 indexed text blobs. The
merged commit carries its matching DCO sign-off; PR #39 published E-0078 as exact `main` commit
`47378588471d9271a43dfaeb56f3138c01137e1f`, and exact-main run `29710670162` passed all three
jobs. No channel name,
display-ready, row-order, swizzle, color-space, alpha, material, geometry, gameplay, GPU, E-0077, or
emulator-equivalence semantics are assigned, and no private or D-drive inputs were used.

E-0079 freezes the first M8 Windows delivery slice without claiming a completed release. The only
permitted package input is an MSVC x64 `Release` build using the static MSVC runtime. The fixed
`OpenOmega-0.1.0-windows-x86_64.zip` contains exactly one identically named internal root and eight
files: `openomega.exe`, `launch-openomega.cmd`, `README-WINDOWS.md`, `LICENSE`, `NOTICE`,
`TRADEMARKS.md`, `THIRD_PARTY_NOTICES.md`, and `LICENSES/SDL3.txt`. The launcher contract changes to
its own directory, forwards every argument, and preserves the child exit code; validation must cover
exact `OpenOmega native shell: rendered_frames=0` stdout with empty stderr and an invalid-option
oracle from an unrelated working directory. A
sibling `.zip.sha256` sidecar records the archive's SHA-256 digest for integrity checks. The result is an unsigned preview and
must contain no proprietary inputs or assets, PCSX2, user profiles, PDBs, or developer tools.
Serialized local validation generated the package and matching sidecar, passed the focused package
contract, and observed one canonical root with exactly two directories and eight files. The
launcher is exactly 96 ASCII bytes with five CRLF endings and no BOM; its success and invalid-option
forwarding oracles passed. The executable is x64 PE32+ with the Windows console subsystem. The local
MSVC 19.38 binary imports exactly 11 allowed direct OS DLLs; the contract additionally permits only
the Windows OS synchronization API-set `API-MS-WIN-CORE-SYNCH-L1-2-0` observed under hosted VS
18/MSVC 19.51/Windows SDK 26100, while rejecting SDL, MSVC, UCRT, debug-runtime, every other API-set, and every other
unapproved import. It contains no source/build path prefix after deterministic `Release` path mapping
and the enforced narrow/wide byte scan. Full
MSVC CTest passed 32/32 `Debug`, 32/32 `RelWithDebInfo`, and 33/33 `Release`; the 168-file dependency
gate, all 209 tooling tests, Python compile-all, and the staged public-tree gate over 258 indexed
text blobs passed. DCO passed, PR #40 merged the slice as exact `main` commit `ff8376b`, and
exact-main run `29713390065` passed all four jobs and retained the named Windows archive artifact.
General clean-machine behavior remains unclaimed. Validation used only public source and generated output; no private or owner files, D-drive content,
disc image, executable, emulator,
or PCSX2 input was accessed.

E-0080 confirms the next bounded M8 delivery check without claiming general Windows compatibility.
On main pushes only, a separate fresh GitHub-hosted `windows-2022` consumer
depends on the successful package producer and downloads the same run's named artifact without a
source checkout, compiler, build system, or access to the producer build tree. It requires exactly
the ZIP and SHA-256 sidecar, validates its BOM-free ASCII lowercase digest, two spaces, exact
filename, single CRLF, and archive digest, and extracts exactly the
frozen two-directory/eight-file tree. With a minimal system `PATH`, unrelated working directory, and
isolated `LOCALAPPDATA`, `APPDATA`, `USERPROFILE`, `TEMP`, and `TMP`, it runs the packaged launcher
through the absolute command processor for the exact zero-frame success and invalid-sentinel
failure/forwarding cases. Before/after SHA-256 manifests require the downloaded artifact, extracted
package, unrelated working directory, and synthetic profile to remain unchanged after each launch.
Local emulation of the exact PowerShell body extracted from the workflow YAML passed against both
the freshly regenerated local package and retained E-0079 main artifact. Full Release CTest passed
33/33, the 168-file dependency gate and all 209 tooling tests passed, Python compile-all passed, and
the staged public-tree gate checked 258 indexed text blobs. PR #41 merged E-0080 as exact `main`
commit `4868e1118bcd32c6713a7f4be57dd243d40996ed`. Exact-main push run `29714679947` completed all
five jobs successfully. Consumer job `88266108572` downloaded retained artifact ID `8450186290`;
GitHub verified that artifact envelope's SHA-256 as
`aea3f4869d17874305bf6027bce370d884ddcaed35e3e9d7a4bc2217aa6baac2`, while the strict
sidecar/recompute check verified the retained inner package ZIP SHA-256 as
`c06ce722572c5edbb0c34ce6b3fc985bcadd4e24ebda0cc07dff59df65ccfe5d`. The job emitted
`fresh-VM portable consumer: OK (artifact, checksum, tree, launch, immutability)`. This hosted
result covers only same-run artifact transfer, integrity, extraction, package-relative launch,
exact process behavior, and non-mutation on that runner image. Its zero-frame path does not validate
a window, menu interaction, GPU, audio, owner data, physical or arbitrary clean machines, Windows
client editions, other Windows Server releases, or other runner images. Both digests identify
retained E-0080 artifact `8450186290`; they are not hashes for every later package build.

E-0081 separately smoke-validates a current portable package's visible generated no-content menu on
the tested Windows host. Its sidecar matched the ZIP, all eight extracted regular files matched
their ZIP members, and the absolute extracted launcher started without arguments from an unrelated
empty working directory under an isolated profile. No owner data, level, or configuration argument
was supplied. The exact `OpenOmega - native runtime` title appeared, and a temporary operator-visible
screenshot showed only the project-generated no-content OPEN OMEGA diagnostic menu. Its ignored PNG
was deleted after display and is not retained or committed. Native Escape
closed the visible run, and a second same-package exit oracle returned zero. Direct process evidence
contained two stdout lines, exactly three INFO-only stderr records, and zero warning, error, or fatal
stderr records. The unrelated work directory remained empty. GPU drivers wrote only shader-cache
files inside the isolated profile, so profile immutability is intentionally unclaimed. No
proprietary input, retail asset, retail executable, emulator, PCSX2 input, or D-drive filesystem input was
accessed. This does not establish a pixel-golden contract, retail-menu fidelity, owner-data behavior,
controller or audio coverage, another host or Windows release, or PCSX2 equivalence.

A private aggregate-only ReSymbol cross-check pinned `Ps2EeR5900LeCoreV1`. Because the frozen ELF
was unavailable, it used the available private 32 MiB EE memory and reproduced 38/38 already-public
loader control-transfer sites: 33 calls and five branches, with zero mismatch or error classes. No
private path, hash, byte, address, symbol, disassembly, input copy, or ReSymbol output is committed.
This is a bounded decoder-consistency result, not semantic, decompilation, or retail-behavior
evidence.

E-0082 wires the strict E-0078 Packed24 diagnostic into complete `LevelContent` startup through the
existing E-0077 canonical `AssetService` transaction. The combined helper always returns owned
topology and treats every transfer rejection as a typed nonfatal fallback. On the public synthetic
Packed24 fixture it produces frozen 32x32 topology and 16x16/1,024-byte transfer outputs; app-state
inspection pins five resident uploads totaling 78,864 bytes and an exact three-command
base-plus-split-topology-plus-transfer draw list. A GPU probe verifies the first four source triples
and their synthetic `0xff` fourth slots. Transfer teardown precedes topology. Synthetic Packed32
startup remains topology-only with four resident uploads totaling 77,840 bytes and exactly one fixed
identity-free `unsupported-sample-encoding` INFO record. A helper-level output-limit rejection
preserves topology and restores aggregate `AssetService` state, and source inspection shows one
Request/Release transaction. A test-only exact 77,840-byte renderer-pool budget rejects the optional
fifth reservation before GPU allocation and proves successful four-texture/full-width-topology
fallback, exact pool-state preservation, and one fixed identity-free `upload-failed` INFO record. Serialized
local validation passed focused/full MSVC builds, direct
asset-service and Direct3D12 app smokes, 32/36/32 CTest, dependency 168, tooling 209, Python
compile-all, and the public-tree gate over 258 indexed text blobs. Diff and DCO checks passed. PR,
head, publication, and exact-main validation remain unclaimed. The local opt-in GPU suite ran the
integration test; default hosted CI only compiles it. These checks do not prove cumulative exact
request count, every rejection category, full-image GPU fidelity, backend-specific fifth-upload
failures, release failure, allocation injection, or valid-transfer failure rollback, and assign no retail texture,
material, geometry, gameplay, or emulator-equivalence semantics. Only public source and generated
fixtures were used; no private or owner file, proprietary input, D-drive content, disc image, retail
executable, emulator, or PCSX2 input was accessed.

E-0090 supplies the first canonical audio decode value without connecting it to the host. One
bounded reentrant retail adapter converts the complete observed 48-byte VAG envelope and supported
PS-ADPCM frames into owned 22,050 Hz mono PCM16 plus raw source-frame flags and marker sample
offsets. It applies no automatic marker, loop, playback, resampling, mixing, streaming, selection,
or SDL policy. Independent project-generated golden fixtures cover every predictor and arithmetic
boundary; owner audio and other proprietary bytes are absent. Runtime asset lookup, title-specific
sound roles/marker behavior, audio-service publication, behavioral comparison, and mixing remain
in progress.

E-0093 supplies a separate canonical SKAS structural text value without connecting it to content or
runtime systems. One bounded reentrant retail adapter validates only the two-candidate aggregate
physical/logical-size, zero-tail, printable-ASCII, CRLF, 72-line, five-blank-line, and
67-single-colon-line envelope. It owns the exact text and opaque source-order line ranges but does
not split labels and values or associate the text with SKA, animation, skeleton, actors, or
gameplay. Project-generated adversarial fixtures cover the strict envelope and resource boundary;
owner data is absent. Semantic decoding, lookup, association, and behavioral comparison remain in
progress.

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

E-0087 supplies a first strict Indexed8 diagnostic candidate projection over canonical storage. It
accepts only one matching direct `Packed8` plane and one exact 256-entry palette, and its caller must
name the CLUT, source-slot, alpha, and linear-row candidate policies explicitly. The output is not
wired into startup, asset selection, renderer upload, material binding, or menu presentation. It is
testable hypothesis plumbing for later independent behavioral comparison, not completion of M3
texture rendering or evidence for any retail display semantics.

E-0092 supplies the bounded native PAR text-envelope boundary. It owns exact CRLF logical text and
source-order opaque line ranges, recognizes only the eight exact six-decimal version tokens in the
public aggregate fingerprint, and discards only validated bounded NUL padding. It performs no
key/value or semicolon-comment parsing beyond the proven first-line marker and assigns no fields,
paths, asset roles, particle semantics, compatibility defaults, rendering behavior, gameplay, or
emulator equivalence. It is not wired into level loading, effects, or the front end.

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

Status: native persistence foundation and profile-catalog startup composition implemented. The
versioned transactional `SaveDatabase` stores only project-owned native records. `ProfileCatalog`
adds bounded metadata at `profiles/<32-lower-hex-id>/metadata`, lists it before platform startup, and
never creates or selects a default profile. `OmegaApp` owns the database/catalog lifetime through
`NativePersistence`; `--probe-only` does not touch persistence and `--frames=0` returns after
bootstrap. The bounded standard PS2 memory-card container/filesystem codec is also implemented.
E-0089 adds a bounded project-generated Main/Profiles/Controls/AssetTopology shell that snapshots
existing profile summaries once and displays at most three fixed labels. E-0096 extends that shell
with explicit, session-only active-profile selection: the startup snapshot copies the matching
fixed-size IDs, the pure reducer emits a typed first/second/third-slot command, and `OmegaApp`
resolves and owns the selected ID on its game thread. Selection never mutates the profile catalog,
writes the database, or creates/selects an implicit default. Only DiagnosticPlay advances
simulation. Persistent active-profile policy, profile mutation UI, campaign schemas, and the
independently evidenced Omega Strain payload mapping remain in progress. No PS2 memory-card device
or emulator savestate is part of the shipping-runtime design, and the synthetic shell is not a
retail-fidelity claim.

E-0086 also supplies a synthetic-verified, analysis-only front-end HOG topology scanner. Its
path-free fixed schema can measure recursive container depth, approved public format/category
populations, exact/zero-padded extents, bounded member-size buckets, and sibling same-basename
extension pairs without exporting identities. This is discovery scaffolding only: no retail input
was scanned for the public E-0086 claim, and it does not implement or infer menu state, UI layout,
asset lookup/binding, rendering, audio, or behavior.

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

## M9: OpenOmega SDK and authoring pipeline

This milestone culminates the required SDK-0 through SDK-6 stages in
[`07-Engine-and-SDK-Roadmap.md`](07-Engine-and-SDK-Roadmap.md); that roadmap owns the staged
implementation details. Optional SDK-7 exporters are independent and do not gate M9 or M10.

Exit regression: a completely project-owned sample is validated, cooked, packaged, and run through
the same promoted semantic/runtime contracts used by compatibility content.

## M10: Retail-independent engine proof

This milestone is the SDK-8 proof defined in
[`07-Engine-and-SDK-Roadmap.md`](07-Engine-and-SDK-Roadmap.md).

Exit regression: CI configures, builds, and runs a non-Omega, project-owned sample whose link graph is
restricted to an allowlist of deliberately promoted engine/SDK targets. No retail/compatibility
adapter or Omega title-composition/app-host target is present.
