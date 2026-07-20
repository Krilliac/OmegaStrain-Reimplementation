# Session handoff

## Objective

Build a clean-room, pure-native reimplementation of *Syphon Filter: The Omega Strain* for
modern host CPUs. Use metadata and observable behavioral tests to derive contracts, then write
the implementation independently. PCSX2 and PS2-code analysis are offline reference tools, not
shipping dependencies or execution mechanisms.

## Verified game identity

| Field | Value |
| --- | --- |
| Region/build | NTSC-U retail |
| Boot executable | `SCUS_972.64` |
| PCSX2 serial | `SCUS-97264` |
| Game CRC | `D5605611` |
| ELF size | 4,071,592 bytes |
| ELF entry point | `0x00100008` |
| ISO size | 3,457,744,896 bytes |
| ISO SHA-256 | `A60A4B41FEA808335BAA3B887A377BB98063F6F169CA498EFEE49397FBA96130` |

## Completed setup

1. Pulled current official PCSX2 `master` at
   `86d76bbf590566d9ea74d381eeff3acd9856503a`.
2. Installed the official 2026-07-09 Windows dependency snapshot and current patches database.
3. Built `Release AVX2` through VS2022/MSBuild with zero warnings and zero errors.
4. Recovered the owner's existing USA BIOS, ISO, and two `SCUS-97264` save states from private
   storage into the ignored `private/` tree.
5. Initialized a separate PCSX2 data root under `runtime/data/PCSX2` and selected
   `scph39001.bin`.
6. Booted the game successfully through the newly compiled emulator. The log confirms BIOS
   v1.60, `SCUS_972.64`, CRC `D5605611`, D3D12, VM initialization, and the entry point.
7. Extracted the ISO9660 filesystem: 448 files, 36 directories, 3,455,648,408 bytes.
8. Generated deterministic disc and ELF metadata under `analysis/`.
9. Reverse-engineered the common HOG directory structure and validated all 273 top-level
   archives: 32,351 entries, zero structural failures. A safe parser/extractor now lives at
   `tools/hog.py`.
10. Built a pure-native HOG/VFS layer that validates all 6,677 nested spans, resolves all 5,351
    POP terrain references across 18 level manifests, semantically decodes all 7,036 COL spatial
    meshes, all 15,248 TDX storage assets, and all 7,036 VUM material catalogs with zero errors.
11. Added owner-supplied data-root validation and native named-level startup. A headless MINSK
    probe loads 299 canonical manifest cells and matching owned spatial meshes; the SDL_GPU/D3D12
    path renders a deterministic synthetic canonical-COL wireframe contact sheet for 120 frames
    and exits without a lingering process. Meshes occupy source-order tiles, with each mesh
    projected along its two largest coordinate extents. This clean-room diagnostic is not world
    placement or reconstructed geometry and makes no VUM, TDX, or other retail semantic claim.
12. Routed every manifest cell through `GameDataService` into owned neutral spatial meshes under a
    shared level-operation budget. Native verification covers all 18 levels and 5,351 cells with
    zero errors: 20,203 canonical nodes, 93,356 leaves, 889,640 vertices, 1,239,980 triangles and
    references, and 2,137 normalized empty meshes. Headless startup independently reports matching
    manifest/spatial cardinality for all 18 levels.
13. Corrected the TDX counted extent to `64 + block_count * block_stride` and added an owned
    storage-plane adapter. It preserves source-order blocks, transfer planes, palette channel
    bytes, and packed sample families without claiming display-ready pixels. Aggregate verification
    covers 15,248 textures, 15,442 blocks, 17,960 primary planes, 285,521,272 owned primary bytes,
    and 4,112 duplicate-proven implicit zero bytes with zero errors.
14. Added an owned VUM material catalog plus a separate retail-only passive render-payload
    descriptor. Aggregate verification covers 7,036 catalogs, 38,793 names, 38,899 materials,
    42,631 dense name references, 91,460 Q/P pairs, 38,023 normalized T targets, 134,122
    middle-to-final references, and 365,840 ordered Q/P final references with zero errors. Payload
    bytes, topology, vertex attributes, usage-code meaning, and material binding remain unassigned.
15. Split SDL input from the GPU host into the app-owned, non-hot-reloadable `SdlInputService`. Its
    `PumpEvents` owns the process-global event pump, while the service owns the gamepad subsystem
    and one primary `SDL_Gamepad`; accepts button events only from that instance; resets only
    gamepad controls when it disconnects; and promotes the next available device. `SdlGpuHost` now
    owns video/render resources only. Deterministic headless virtual-gamepad coverage exercises
    attach, button edges, disconnect reconciliation, and promotion. The single-primary rule is
    synthetic shell policy, not inferred retail behavior.
16. Added a fixed-output retail-only SKA descriptor for the proven 112-byte-prefix counted-word
    envelope. Native aggregate verification accepts all 213 owner-supplied spans with zero errors:
    158 exact, 55 zero-padded, and 2,180,832 logical bytes. It retains no payload and assigns no
    actor, skeleton, timing, channel, transform, compression, or animation semantics; SKAS remains
    a separate two-candidate text-evidence family.
17. Added a privacy-safe bounded POP post-terrain scanner. It accepts all 18 owner-supplied POPs
    with zero errors and finds the same 19 aligned literal-tag candidates exactly once per file and
    in one shared order: 342 aggregate hits. These are candidate markers only; section headers,
    counts, extents, placement, visibility, and all other field semantics remain unassigned.
18. Added a bounded, aggregate-only TDX display-layout hypothesis scorer. Across 15,248 spans, the
    direct four-bit family favors low-nibble-first coherence on 1,765 planes versus 139, with 110
    ties; the direct eight-bit family favors the bit-3/bit-4 palette permutation on 145 planes
    versus 7, with 10 ties. These content-dependent scores are candidate selection only and do not
    confirm nibble order, palette order, channel meaning, swizzle, or display-ready pixels.
19. Added a bounded POP layout hypothesis scorer. Across all 18 POPs, five marker-relative `+4`
    word/fixed-stride tuples fit every occurrence: `INL:`/36, `PNT:`/88, `DIR:`/44, `ENV:`/76,
    and `INV:`/84 bytes. Nonzero instances establish the candidate strides; one zero `PNT:` and two
    zero `INV:` instances have the predicted empty extent but add no stride evidence. These
    arithmetic fits do not yet confirm markers, counts, records, section boundaries, placement,
    visibility, or field meanings.
20. Completed the first bounded privacy-safe VUM consumer trace. One selected runtime copy produced
    one complete 120-frame pair; strict validation accepted both validated reports, and the repeat
    was byte-identical. Each report contains two EE-read aggregate rows, two anonymous-site rows,
    and zero VIF1 chunk rows. The post-run containment audit found no retained runtime copy,
    executable surface, reparse point, owner-input copy, or emulator/build process. An independently
    guarded second ranked trial reproduced those aggregates exactly: one capture with four accepted
    aggregate rows, split evenly between EE-read and anonymous-site rows, zero VIF1 rows, and zero
    aggregate-count deltas. Both accepted trials are header-only: their EE-read rows remain confined
    to the already-opaque header-vector block, and no accepted row reaches counts, records, metadata,
    payload, VIF, or tail data. This repeated observation does not exclude copied buffers or activity
    outside the bounded observation window and assigns no geometry, topology, vertex, material,
    packet, draw, placement, visibility, or gameplay semantics.
21. Added all-or-error `LevelContentIR` startup composition. `GameDataService` now traverses each
    common/nested archive and manifest-referenced cell once while decoding its unique COL and VUM
    members under one shared operation budget. The C-only headless probe accepts all 18 levels and
    all 5,351 manifest cells with zero errors. Parallel spatial/material positions preserve source
    order and cardinality only; they assert no binding, placement, visibility, or render semantics.
22. Bounded the former level-TDX topology question to its actual common-archive scope. The
    aggregate scanner accepts all 18 runtime levels, 5,351 manifest cell occurrences, and 5,413
    scanned DATA.HOG-graph directory occurrences with zero errors and finds zero normalized
    `.TDX`-suffixed members. This extension-bounded negative result does not exclude another
    representation or establish ownership or a texture-to-cell/material/mesh binding.
23. Measured two explicit sibling texture-container classes separately. The direct-only scanner
    accepts both roles for all 18 runtime levels: 36 exact containers and 5,801 direct TDX members
    with zero errors, collisions, malformed textures, nested traversal, or non-TDX entries. The
    generic classes contribute 5,765 and 36 occurrences. This establishes containment, not retail
    ownership, necessity, priority, or any texture-to-material/cell/mesh/render relationship.
24. Added the public-safe native `LevelTextureStore` and all-level verifier. It accepts 18/18 levels,
    36 explicit sources, and 5,801 level-inventory texture occurrences with zero errors, loading
    5,913 blocks, 7,603 planes, 615,232 palette entries, 27,101,352 plane bytes, 2,460,928 palette
    bytes, and 29,562,280 owned bytes. Independent Open input/items/logical-output/depth/scratch
    maxima are `3,076,944 / 1,460 / 111,014 / 0 / 71,467`; Load maxima are
    `3,139,344 / 5,169 / 333,232 / 0 / 65,595`. Each maximum is fieldwise and is not asserted to
    co-occur with the others. Internal defaults are 4 MiB input, 512 KiB logical output, 128 KiB
    scratch, 8,192 items, 4 KiB strings, and depth one. The measured byte/item fields are separately
    next-binary-rounded above the larger Open/Load maximum; depth one is the smallest nonzero
    headroom above measured depth zero. They are not runtime configuration or `--set` keys.
    Named-level startup now retains the locator inventory after manifest, content, and debug-image
    success; the 18/18 aggregate probe remains valid across all 5,351 cells. Startup performs no
    texture `Load`, material binding, display expansion, GPU upload, or rendering.
25. Added a retail-only passive POP post-terrain hypothesis descriptor and privacy-safe fixed-schema
    native verifier. The descriptor fail-closed checks only the established ordered aligned-literal
    envelope and five arithmetic extent hypotheses, retains no input span or payload, and does not
    enter canonical asset IR. The owned-corpus verifier discovers and accepts all 18 `.POP`
    candidates with zero rejections or errors. Independent accepted-only
    input/items/logical-output/string/scratch maxima are `919360 / 1 / 168 / 26 / 80036`; no maximum
    tuple is asserted to co-occur. This confirms native conformity to the published hypothesis family
    only, not section boundaries, counts, records, payload meaning, placement, visibility, rendering,
    or gameplay.
26. Added and reran the bounded full-normalized-member-name lexical-coherence experiment over
    manifest-scoped VUM names and the two explicit direct TDX locator classes. It scans 18/18 levels
    with zero errors, and its cell/catalog/name/material/reference and container/locator totals match
    the native validation populations exactly. All 34,267 VUM name occurrences and 37,893 dense
    references lack terminal `.TDX`; no name enters the eligible exact lookup branch, all 34,589
    material records have an ineligible reference, and all 5,801 class-qualified locators remain
    unreached. The experiment compares complete normalized strings only and excludes basename, stem,
    extension addition/removal, suffix substitution, nested-container, fuzzy, and other alias
    hypotheses. This narrow negative result establishes no retail lookup, texture-name role, material
    binding, class priority, or runtime integration.
27. Added the separate exact-first one-terminal-extension candidate family without changing the
    E-0041 default output or direct `.TDX` locator universe. After complete normalization, it removes
    at most one syntactic extension from the final component independently on both sides, preserves
    directories and original class-qualified locators, and performs no basename, substring, fuzzy,
    or repeated-extension search. Two byte-identical passes scan 18/18 levels with zero errors. All
    34,267 VUM names and 37,893 dense references classify as extension-elided unique-primary
    candidates; all 34,589 material records inherit a unique extension-elided candidate. Of 5,801
    original locator occurrences, 5,690 are reached only through extension elision and 111 remain
    unreached. This offline lexical result does not observe a retail alias rule or material
    consumption, assign texture-name or binding semantics, establish class priority, or justify
    runtime integration.
28. Added and verified the bounded native `AssetService` v0. `OmegaApp` owns it optionally only when
    startup has a `LevelTextureStore`; a fixed reusable slot pool issues generation handles, requires
    explicit `Ready`/`Failed` release, and enforces bounded in-flight and resident-logical accounting.
    Accepted `JobService` callables retain a shared implementation through final return, while service
    teardown stops acceptance, waits only its accepted work, expires handles, and runs before the job
    pool and content dependencies. A clean MSVC build produced zero warnings and errors, focused and
    full 18/18 CTest runs passed, and 100 repeated lifecycle-test runs passed. Two verifier passes are
    byte-identical schema version 1 and accept 18 levels, 36 sources, and 5,801 occurrences with zero
    errors. Requests, `Ready` states, `Get` calls, releases, stale-handle rejections, and zero-residual
    checks also each total 5,801. Storage totals are 5,913 blocks, 7,603 planes, 615,232 palette
    entries, 27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 owned bytes; maximum
    active/in-flight/resident-logical usage is `1 / 1 / 333,232`. The verifier service limits are
    `1 / 1 / 524,288`; runtime defaults are `64 / 64 / 64 MiB` with a hard 8,192-slot maximum. These
    are synthetic project bounds, not retail limits or user settings. The service performs no VUM
    name/material lookup, alias resolution, binding, pixel expansion, GPU upload, placement,
    visibility, or rendering. Its aggregate exposes no identities or private data, and the unchanged
    E-0038 verifier was revalidated.
29. Added and verified the portable bounded `RenderTexturePool` foundation. The SDL-free pool
    preallocates fixed metadata slots, accepts only exact overflow-checked tightly packed
    project-owned RGBA8 extents, and supports transactional `Reserve`/`Publish`/`Rollback` around
    future backend creation and upload. Unique nonzero process-local pool identities and 64-bit slot
    generations reject default, foreign, stale, and released handles; explicit release refunds
    logical resident bytes, while a maximum generation retires instead of wrapping. Defaults are 64
    slots and 64 MiB logical RGBA8 with a hard 8,192-slot maximum. At E-0044,
    `RenderFramePacket` contained a default-invalid fixed-width diagnostic handle and remained
    trivially copyable and standard layout. A clean MSVC build had zero warnings or errors, the
    focused test passed, 100 repeated focused runs passed, and full 19/19 CTest passed. At that
    milestone this was metadata/lifecycle policy only: it created no GPU resource, `SdlGpuHost`
    and `OmegaApp` did not consume the handle, and the existing one-off debug upload remained
    unchanged. No GPU upload, blit, residency, `AssetService`, TDX, VUM, material, binding, or
    display semantic was established.
30. E-0045 superseded item 29's host non-consumption and unchanged one-off-upload state. At that
    milestone, `SdlGpuHost` owned a fixed parallel SDL texture table. Project-owned RGBA8 upload was
    an RAII transaction across pool `Reserve`, backend texture/transfer creation, copy-command
    submission, and `Publish`; every failure rolled back portable residency and released acquired
    backend state. Release waited for GPU idle. Frame packets carried the handle resolved for
    blitting, and aggregate host snapshots exposed no identities. `OmegaApp` uploaded the existing
    project-generated diagnostic image, stored only its generation handle, and released it
    explicitly before host fallback teardown. The swapchain path submitted, rather than illegally
    canceled, a command buffer during unwinding after successful acquisition. Host defaults remain
    64 slots and 64 MiB logical RGBA8 with the hard 8,192-slot maximum.
    A clean MSVC build completed with zero warnings and errors; default full CTest passed 19/19. One
    initial and 20 repeated public zero-file GPU smokes all passed on `direct3d12` (21 total), and
    the public `openomega` two-frame smoke also passed. Every GPU smoke uses capacity one and a
    256-byte budget: one successful clear-only submission; opaque A upload at 8x8/256 bytes and
    blit; release and stale-handle rejection before GPU access; opaque B upload at 4x8/128 bytes in
    the same slot with a new generation and blit; release; and checked idle. Exact totals are two
    uploads, 384 cumulative logical bytes, two releases, two blits, one clear-only submission, one
    rejection, zero unavailable-swapchain submissions, and final capacity one/free one with zero
    reserved, resident, retired, or charged bytes. The smoke proves command submission and idle, not
    framebuffer identity or readback.
    No `TextureStorageIR`/`AssetService` data, TDX plane/palette, channel, alpha, nibble, palette,
    swizzle, mip, display expansion, VUM/material/alias/binding, scene placement/visibility, retail
    rendering, gameplay, measured GPU bytes, streaming/eviction, async upload, or fence design is
    established. The GPU executable always builds with tests plus the SDL backend, while CTest
    registration remains opt-in through `OMEGA_RUN_GPU_SMOKE_TEST` for headless safety.
31. E-0046 supersedes item 30's historical single-handle frame boundary. `RenderFramePacket` now
    owns a fixed `RenderDrawList` with a maximum of 16 commands and normalized Q16 target extent
    65,536. Each command contains a generation handle and normalized destination rectangle.
    Construction rejects overflow, default handles, and zero, inverted, or out-of-range rectangles
    while preserving source order and duplicates. The draw list and packet remain trivially copyable
    and standard layout; commands do not own or pin generations, so any future asynchronous queue
    needs explicit residency pins.
    Before acquiring a GPU command buffer, `SdlGpuHost` resolves every active handle and backend
    slot into fixed storage. The first invalid, stale, foreign, released, or inconsistent handle
    rejects the complete list before a prefix can reach the GPU. Empty lists retain clear-only
    rendering. With an available swapchain, nonempty lists first perform one full-target clear and
    then issue all full-source opaque nearest-filtered aspect-contained blits in source order with
    `LOAD` target semantics; later overlapping commands overwrite earlier ones. Aggregate snapshots
    count successfully submitted blit draws without exposing resource identities.
    A clean MSVC build completed with zero warnings and errors. The focused test passed once and
    through 100 additional repetitions, and the default suite passed 20/20. One initial plus 20
    repeated direct GPU smokes all passed on `direct3d12`. Each used capacity two and a 384-byte
    logical budget and ended at exactly three uploads, 640 cumulative logical bytes, three releases,
    two successful blit frames, four successful blit draws, one clear-only submission, one
    stale-list rejection, zero unavailable-swapchain submissions, and zero residual residency. The
    opt-in GPU test passed as the 21st CTest; registration was restored to off and the default
    listing returned to 20 tests. The public two-frame `openomega` smoke passed with deterministic
    dummy audio.
    This proves bounded command ownership, ordered submission, checked idle, and atomic
    resident-handle prevalidation only. It does not guarantee all-or-none behavior for arbitrary
    backend failures, framebuffer readback, or pixel identity. It establishes no retail draw order,
    target coordinates, filtering, clear color, compositing, placement, visibility, camera,
    material, texture, mesh, or gameplay semantics; no `TextureStorageIR`/`AssetService` bridge
    or display expansion; and no measured GPU-byte accounting, streaming, eviction, asynchronous
    upload, or fence design.
32. E-0047 supersedes item 31's historical full-source, contained, nearest-filter policy. Each
    `RenderTextureBlitCommand` now owns a half-open normalized Q16 source crop, a normalized target
    rectangle, and explicit project-owned `Contain`/`Stretch` fit and `Nearest`/`Linear` filter
    values. Allocation-free creation rejects capacity overflow first, then validates each command
    in source order with fixed handle, source, target, fit, and filter priority. The maximum remains
    16 commands; order and duplicates are preserved; the inactive tail is zeroed; only a const
    prefix is exposed; and the command, list, plan, and frame-packet values remain trivially
    copyable and standard layout.
    The pure source phase maps normalized crops into mip-zero texel rectangles by flooring
    left/top and ceiling right/bottom with overflow-safe 64-bit arithmetic. The pure planning phase
    retains that mapped crop exactly, maps the target by the same half-open rule, and either fills
    it for `Stretch` or uses the cropped aspect ratio with deterministic round-half-up sizing and
    centering for `Contain`. Invalid source extents, mapped crops, target extents, target
    rectangles, and fit values fail with deterministic priority.
    `SdlGpuHost` runs three complete-list fail-closed passes: it first resolves the complete
    handle/backend-slot set into fixed arrays. It then maps every source crop and filter before
    acquiring GPU work. After acquiring the command buffer and swapchain, the third pass uses the
    nonzero swapchain extent to plan the complete target set before recording one full-target clear
    and the source-order
    `LOAD` blits. A later stale handle therefore cannot
    validate or render a prefix, and a later planning failure cannot record a visible prefix. The
    post-acquisition path uses only fixed arrays and pre-reserved error storage. Empty lists remain
    clear-only, and the existing submit-on-unwind behavior is retained.
    A clean MSVC build compiled seven translation units with zero warnings and errors. The focused
    portable executable passed once plus 100 repeated runs, and the default suite passed 20/20.
    One initial plus 20 repeated public zero-file GPU smokes all passed on `direct3d12`; each ended
    at exactly three uploads, 640 cumulative logical bytes, three releases, two submitted blit
    frames, four successful draws, one clear-only submission, one stale-list rejection, zero
    unavailable submissions, all slots free, and zero reserved, resident, retired, or charged state.
    The opt-in GPU configuration passed 21/21 CTests. Registration was then restored to OFF, and the
    default listing returned to 20 tests. A public two-frame D3D12 `openomega` smoke passed with
    dummy audio. Publication CI is tracked separately from these local validation claims.
    This proves bounded project-owned crop/fit/filter validation, deterministic planning, checked
    SDL submission, and cleanup, not framebuffer identity, readback, or filter-pixel correctness;
    arbitrary backend-failure atomicity; asynchronous lifetime pins or fences; measured GPU memory,
    streaming, or eviction; `TextureStorageIR`/`AssetService` consumption or binding; TDX plane,
    palette, channel, alpha, nibble, swizzle, mip, or display expansion; or VUM, material, alias,
    cell, mesh, placement, visibility, camera, retail rendering, or gameplay semantics.
33. E-0048 adds `RenderClearColorRgba8` as an owned `RenderFramePacket` value. A generic color
    defaults to `{0, 0, 0, 0}`; `kDefaultRenderClearColor` and the packet member default are
    `{4, 5, 10, 255}`. Every unsigned-byte combination is valid. Before command-buffer acquisition,
    `SdlGpuHost` converts each channel to SDL by `byte / 255.0` and reuses the result for clear-only
    frames and the full-target clear before any blits. The host pulse and list-dependent fixed
    colors are removed, and `OmegaApp` explicitly assigns the named default.
    The final regenerated MSVC build completed with zero warnings and errors. The focused portable
    executable passed once plus 100 repeated runs, and default CTest passed 20/20. One initial plus
    20 repeated public zero-file GPU smokes passed on `direct3d12`; each retained exactly three
    uploads/640 cumulative logical bytes, three releases, two blit frames/four successful draws,
    one clear-only submission, one stale rejection, zero unavailable submissions, and zero residual
    residency. The opt-in configuration passed 21/21 CTests, was restored to OFF, and listed 20
    default tests. A public two-frame D3D12 `openomega` smoke passed with dummy audio. Publication
    CI is tracked separately from these local validation claims.
    Existing counters, complete-list handle/source/filter preflight, target planning,
    submit-on-unwind, and fail-closed ordering remain unchanged. This project-owned in-process
    value establishes no ABI, persistence, serialization, wire, plugin, or retail contract; no
    framebuffer identity, pixel correctness, readback, color-space, alpha, or blending semantics;
    and no `TextureStorageIR`/`AssetService`, material, cell, mesh, or retail asset-to-draw bridge.
34. E-0049 adds a private friend-only `SdlGpuHostTestAccess` seam for a synchronous synthetic
    clear readback. For an empty packet, the host validates 2D color-target support and four-byte
    texels for `R8G8B8A8_UNORM`, maps the packet clear before command acquisition, creates a
    temporary 2x2 target plus 16-byte download buffer, and records the clear through the same
    `RecordClearPass` used by production rendering. It then downloads, disarms the command guard
    into submit-and-fence acquisition, waits, maps, explicitly decodes four RGBA8 values, and uses
    guards for unmap, fence, transfer-buffer, and texture cleanup. No production counter or
    portable texture residency changes.
    The public zero-file smoke reads back `{0, 255, 0, 255}` and `{255, 0, 255, 0}` exactly from all
    four pixels and proves snapshot invariance after each probe. A nonempty synthetic draw list is
    rejected before any SDL/GPU work with exact error
    `clear readback requires an empty draw list`, again without snapshot mutation.
    A clean incremental MSVC build issued four compile requests with zero warnings or errors. One
    initial plus 20 repeated public zero-file `direct3d12` GPU smokes passed; every run preserved
    the established three uploads/640 cumulative logical bytes, three releases, two blit frames/
    four draws, one clear-only submission, one stale rejection, zero unavailable submissions, and
    zero residual residency. Default CTest passed 20/20. The opt-in configuration passed 21/21,
    was restored to OFF, and listed 20 default tests. A public two-frame D3D12 `openomega` smoke
    passed with dummy audio. Publication CI is tracked separately from these local claims.
    This confirms only those synthetic endpoint values in a temporary offscreen target on the
    observed D3D12 path. The friend seam is not a stable public readback API and exposes no SDL
    handle. E-0049 establishes no swapchain/on-screen/presentation, sRGB/HDR, color-space, or
    intermediate-value rounding guarantee and no guarantee for untested values; no alpha
    interpretation, blending, or composition semantics beyond the exact tested 0/255 alpha bytes;
    no blit/filter or cross-backend pixel guarantee; arbitrary backend-failure atomicity; production
    asynchronous queue, residency-pin,
    or fence contract; stable ABI, serialization, persistence, wire/plugin, measured GPU-memory,
    streaming/eviction, display-expansion, asset-binding, retail-rendering, or gameplay semantic.
35. E-0050 adds a private friend-only `ReadbackBlitsForTesting` seam for one synchronous synthetic
    blit readback without changing the public renderer API. An empty draw list returns exact error
    `blit readback requires a nonempty draw list` before SDL or resource work and leaves the full
    snapshot unchanged. For a nonempty list, the seam resolves every generation and backend slot,
    maps all source crops and filters, and plans every fixed 4x4 destination before allocating a
    temporary `R8G8B8A8_UNORM` color target, 64-byte download buffer, or command buffer.
    Production rendering and the probe share `TryMapTextureFilter` plus one source-order
    `RecordTextureBlits` path whose SDL blits retain `LOAD`, no flip, the mapped filter, and no
    cycling. The probe first calls the existing shared clear recorder, records all blits, downloads,
    takes the command buffer into fence-producing submission, waits, maps, explicitly decodes all
    sixteen row-major RGBA8 pixels, and releases every temporary resource through guards.
    The public zero-file fixture uploads opaque `R G / B W` endpoint texels, clears to opaque black,
    applies a top-row Contain+Nearest blit followed by a bottom-left Stretch+Nearest overwrite, and
    reads back exactly `KKKK/RRBG/RRBG/KKKK`. Successful readback preserves the full host snapshot;
    releasing the probe restores empty residency before the existing production flow.
    The corrected MSVC build completed with zero warnings or errors. Default CTest passed 20/20.
    One initial plus 20 repeated public zero-file `direct3d12` smokes passed, each ending at exactly
    four uploads/656 cumulative logical bytes, four releases, two production blit frames/four draws,
    one clear-only submission, one stale rejection, zero unavailable submissions, and zero residual
    residency. The opt-in configuration passed 21/21, was restored to OFF, and listed 20 default
    tests. A public two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI is
    tracked separately from these local claims.
    This confirms only the opaque endpoint bytes, two exact source/destination plans, source order,
    and load preservation in the fixed 4x4 target on the observed D3D12 path. It establishes no
    general Nearest/Linear, crop, aspect, rounding, sample-center, edge/border, Contain/Stretch,
    flip/cycle/mip/layer, alpha interpretation, blending, sRGB/HDR/color-space, presentation,
    swapchain, cross-backend, asynchronous-lifetime, public ABI/readback, asset-binding,
    retail-rendering, or gameplay guarantee.
36. E-0051 changes no production/runtime code or public interface. The added smoke fixture uses the
    existing private fixed-4x4 readback with two simultaneously resident synthetic textures. A's
    Q16 first-texel crop maps exact `{32, 192, 224, 255}` across the target; B's later first-texel
    crop maps exact `{224, 80, 32, 255}` into center pixels `[1,1,3,3)`. Two source-order
    Stretch+Nearest `LOAD`
    blits therefore read back `AAAA/ABBA/ABBA/AAAA` on the observed D3D12 path. The complete host
    snapshot stays unchanged, and the production packet is reset before the existing A/B flow.
    This closes only E-0050's same-handle fixture gap by confirming that these two commands select
    their respective simultaneously resident generation/backend texture.
    The one-file MSVC build completed with zero warnings or errors. Default CTest passed 20/20. One
    initial plus 20 repeated `direct3d12` smokes passed with unchanged exact totals of four uploads/
    656 cumulative logical bytes, four releases, two production blit frames/four draws, one
    clear-only submission, one stale rejection, zero unavailable submissions, and zero residual
    residency. The opt-in configuration passed 21/21, was restored to OFF, and listed 20 default
    tests. A public two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI is
    tracked separately from these local claims.
    This proves only the two tested handles, first-texel crops, colors, commands, and fixed 4x4
    result. It establishes no arbitrary multi-texture list, slot/generation, crop/target,
    Stretch/Nearest, interpolation, sample-center, edge/border, aspect/rounding, Linear/Contain,
    alpha/blending/color-space, swapchain/presentation, asynchronous lifetime, cross-backend,
    public readback, asset-binding, retail-rendering, or gameplay guarantee.
37. E-0052 adds bounded in-process post-binding logical snapshot capture through the move-only
    `InputTraceRecorder` and immutable `InputTrace`. Creation validates a synthetic capacity from
    1 through 65,536 frames and a nonoverflowing contiguous `uint64_t` range before a nonempty,
    strictly ascending, unique schema of at most 64 logical actions, then pre-sizes private 32-byte
    records.
    At the hard maximum, 65,536 32-byte record elements plus the fixed 64-slot `uint32_t` schema
    backing contain exactly 2,097,408 bytes of element payload. This does not measure excess vector
    capacity, allocator/object overhead, or process RSS. Allocation-free `Append` observes but never
    retains or mutates its const snapshot, stores only held/pressed/released masks and
    accepted/rejected counts, and preserves both caller and recorder on every ordered failure.
    Allocation-free expected `Finish` permits an open empty recording and makes the source inert.
    Frame/action queries are owned values; only the schema view borrows storage. Recorder use is
    game-thread exclusive. Published immutable trace reads are reentrant on any thread when no read
    races move or destruction.
    The final MSVC build completed with zero warnings or errors. The focused public zero-file test
    passed once plus 100 repeated runs; default CTest passed 21/21. The opt-in Direct3D12
    configuration passed 22/22, was restored to `OFF`, and left 21 tests in the default list.
    Publication CI remains separate.
    This is only a bounded logical capture/storage/query foundation. It adds no input injection,
    playback, scheduler timing/pacing, quit/run-control, simulation/gameplay state, replay
    execution, host event/device capture, serialization, file/wire/stable ABI, concurrent recorder
    use, or retail limit/timing claim.
38. E-0053 adds bounded in-process scheduler-elapsed capture through the move-only
    `SchedulerElapsedTraceRecorder` and immutable `SchedulerElapsedTrace`. Creation validates a
    synthetic capacity from 1 through 65,536 frames and a nonoverflowing contiguous `uint64_t`
    range before allocation, then pre-sizes one private `int64_t` record per slot. At the hard
    maximum, the 65,536 record elements contain exactly 524,288 bytes (512 KiB) of element payload.
    This excludes excess vector capacity, allocator/object overhead, and process RSS.
    Allocation-free atomic `Append` preserves the exact caller-supplied signed nanoseconds,
    including negative, zero, minimum, and maximum values. Failures have recorder-state, capacity,
    then frame-discontinuity priority. Allocation-free expected `Finish` permits an open empty
    recording and makes the source inert. `FrameAt` returns an owned value. Recorder use is
    game-thread exclusive. Published immutable trace reads are reentrant on any thread when no read
    races move or destruction. A paired `FrameScheduler` test produces identical per-frame plans,
    accumulator state, planned-step totals, and dropped-time totals from direct and trace-retrieved
    elapsed values under the tested configuration.
    The final MSVC build of the signed-nanosecond implementation completed with zero warnings or
    errors. The focused `omega_scheduler_elapsed_trace_tests` executable passed once plus 100/100
    repeated runs; default CTest passed 22/22. The opt-in Direct3D12 configuration passed 23/23,
    was restored to `OFF`, and left 22 tests in the default list. The static native dependency gate
    passed 133 files, and all 204 tooling tests passed. Publication CI remains separate.
    This is only bounded elapsed-value capture/storage/query infrastructure. It adds no clock
    source or timestamp-accuracy claim, `FramePlan` capture or checkpoint restoration, input
    alignment beyond caller indices, quit/run-control, simulation/gameplay, injection, replay, app
    wiring, CLI, persistence, file/wire/stable ABI, retail tick claim, or cross-configuration
    determinism.
39. E-0054 adds the SDL-free `omega_runtime` `RunCaptureSession` coordinator. One shared
    configuration validates capacity from 1 through 65,536 and a contiguous leaf range that may
    end exactly at `UINT64_MAX`. `Create` constructs input backing before elapsed backing and
    publishes a session only after both succeed. At the hard maximum, the paired input records,
    fixed action-schema backing, and elapsed records contain exactly 2,621,696 bytes of element
    payload, excluding excess vector capacity, allocator/object overhead, and process RSS.
    The exclusive game-thread phase machine accepts input followed by elapsed or terminal input.
    Elapsed capture uses the retained pending input index. A terminal owns that index plus
    independent host-quit and logical-quit flags and requires at least one true reason. Errors
    preserve an explicit operation stage, fixed session category, and exact optional leaf code.
    Phase validation has priority, and every failure before a transition preserves coordinator and
    caller state. An open empty session may finish; a pending unpaired input rejects finish without
    consumption. Once valid leaf finalization begins, the session is consumed even if a leaf fails.
    Session and immutable pair are move-only, use nothrow moves, and leave sources inert. The pair
    lends trace references and returns an owned optional terminal value. Published pair reads are
    reentrant on any thread when no read races pair move or destruction.
    The final MSVC build completed with zero warnings or errors. The focused
    `omega_run_capture_session_tests` executable passed once plus 100/100 repeated runs; default
    CTest passed 23/23. The opt-in Direct3D12 configuration passed 24/24, was restored to `OFF`, and
    left 23 tests in the default list. The static native dependency gate passed 136 files, and all
    204 tooling tests passed. Publication CI remains separate.
    This adds no `OmegaApp` wiring, clock measurement, scheduler/`RunResult`/checkpoint capture,
    host quit detection beyond caller flags, CLI, simulation/render/audio work,
    persistence/file/wire/stable ABI, injection/playback/replay, external-failure recovery or
    rollback, concurrent session use, tracker-wide exhaustion guarantee, or retail limit, timing,
    or determinism claim. The separately published `InputTracker::next_frame_index()` accessor is
    future app-integration support, not coordinator behavior. E-0055 must preflight a requested
    capture length `N` with `N`, not `N - 1`, before tracker-index wrap.
40. E-0055 adds an owned `FrameSchedulerState` and finite `OmegaApp` capture. `Snapshot` copies
    validated configuration, accumulated remainder, lifetime planned-step total, and lifetime
    dropped time. It has no restore or delta operation. Finite planning rejects a negative limit
    before a limit above 65,536. Zero frames use a capacity-one empty session at any next input
    index. Positive `N` requires `N <= UINT64_MAX - next_frame_index`, using `N` rather than
    `N - 1` so the following tracker index remains representable.
    Move-only `RunCaptureOutcome` owns the requested limit, partial `RunResult`, completion,
    scheduler states before and after, optional failure text, and an optional trace pair.
    `OmegaApp::Run` and `RunWithCapture` share `RunLoop`, preserving ordinary `Run` behavior.
    Capture allocates all backing before logging, clock sampling, or mutation. Zero-frame capture
    enters no loop and performs no event, clock, scheduler mutation, simulation, rendering, audio,
    job, or logging work.
    Active capture taps `InputTracker::EndFrame` before `AppendInput`. It then preserves host and
    logical quit as independent terminal flags, or appends the exact raw elapsed value before
    passing it to the unchanged `FrameScheduler::BeginFrame` call. Only planning and session
    creation failures return outer `unexpected`. After loop entry, operational and capture
    failures return nontransactional partial outcomes and attempt to publish best-effort traces.
    The CLI and `main` are unchanged and continue to call ordinary `Run`.
    The clean MSVC build completed with zero warnings or errors. `omega_core_tests` passed.
    `omega_run_capture_tests` passed once plus 100/100 repeated runs; default CTest passed 24/24.
    With Direct3D12 and dummy audio, `omega_app_capture_smoke` passed once plus 20/20 repeated
    runs. Its unowned-draw fixture forced a real render error and retained one paired input/elapsed
    sample, zero rendered frames, owned failure text, and the scheduler boundary. The next capture
    resumed successfully. The public zero-file `openomega.exe --frames=2` path also succeeded with
    two rendered and input frames and equal planned and executed steps. GPU CTest passed 26/26.
    Registration was restored to `OFF` with 24 default tests. The dependency gate passed 140 files,
    all 204 tooling tests passed, and Python compile-all passed. Publication CI remains separate.
    This adds no capture CLI, replay, input/playback injection, restore, persistence,
    serialization, wire format,
    stable ABI, simulation checkpoint, RNG state, fake services, rollback, ordinary `Run` tracker
    exhaustion guarantee, or retail timing or determinism claim.
41. E-0056 adds the explicit finite-capture command after E-0055's app API milestone. The exact
    once-only `--capture-run` flag requires an explicit `--frames=N` from 1 through the shared
    synthetic maximum of 65,536. Without the flag, ordinary zero and above-65,536 frame values
    retain their parser behavior. Capture cannot compose with probe mode, and help remains
    standalone. Only this flag routes `main` through `RunWithCapture`; ordinary paths still call
    `Run`.
    Captured output retains the ordinary `RunResult` counters and adds aggregate trace presence and
    frame counts, optional terminal metadata, and selected absolute before/after scheduler counters
    derived from snapshots. It does not print complete snapshots or deltas. The tested
    `IsCompleteRunCaptureOutcome` policy fails closed unless a positive request reaches the frame
    limit with no failure, quit, or terminal and with exact result/trace counts and capacities,
    matching origins, and planned/executed-step agreement. The portable process contract verifies
    exact zero-frame and invalid-capture process behavior before host-loop entry. The opt-in GPU
    smoke runs the successful two-frame command through a CMake wrapper that checks the child
    exit code before validating its run, aggregate trace, and scheduler summaries.
    The clean incremental MSVC build completed with zero warnings or errors. `omega_core_tests`
    passed. `omega_run_capture_tests` passed once plus 100/100 repeated runs; default CTest passed
    25/25. Direct3D12 plus dummy audio passed GPU CTest 28/28, including the capture CLI smoke.
    Registration was restored to `OFF` with 25 default tests. The dependency gate passed 140 files,
    all 204 tooling tests passed, and Python compile-all passed. Publication CI remains separate.
    This adds no capture files, persistence, serialization, wire format, stable ABI, per-frame
    printing, replay, injection/playback, restore, delta interpretation, checkpoint, RNG state,
    rollback, interactive or zero-frame capture command, probe composition, ordinary-above-65,536
    execution claim, or retail timing or determinism semantics.
42. E-0057 adds the SDL-free `omega_runtime` `RunCaptureReplaySession`. The concrete move-only
    session owns a moved `RunCaptureTracePair` and advances one exclusive game-thread cursor. The
    pair is validated before ownership transfer; invalid leaf state, capacity/origin alignment,
    normal-versus-terminal counts, or terminal reason/index leaves the caller's pair unchanged. Each
    successful `Next` publishes a `RunCaptureReplayFrame` with an exact owned immutable
    `InputSnapshot`, retaining the recorded frame index, schema, held/pressed/released masks, and
    accepted/rejected event counts. A normal frame owns the exact signed elapsed value. A terminal
    frame instead owns the independent host-quit and logical-quit flags, so the same frame never
    exposes elapsed and terminal data.
    `InputSnapshot` reconstruction allocation or defensive trace-read failure leaves the cursor
    unchanged. Exhausted and moved-from sessions produce distinct complete and invalid-state
    errors.
    `RunCaptureOutcome::TakeTracePair` is rvalue-only. It transfers the optional pair and normalizes
    the entire source outcome to the same inert state used after move construction, including when
    no pair exists.
    This milestone is only bounded in-process replay data access. It adds no SDL, `OmegaApp`, or CLI
    replay path; input injection, `InputTracker` mutation, or synthetic physical events; scheduler
    creation, feeding, pacing, clocking, or state restoration; simulation checkpointing, RNG
    restoration, or world mutation; rendering, audio, or job work; persistence, serialization,
    file/wire/stable ABI, or cross-process contract; seek, rewind, looping, rollback, or retail
    timing or determinism claim.
43. E-0058 adds the portable `omega_app_core` `RunReplaySession`. This move-only, non-hot-reloadable
    app value owns a lower replay session plus a fresh scheduler created from caller-supplied
    synthetic timing configuration and a fresh empty simulation world with the scheduler's fixed
    step. Creation validates scheduler configuration, entity capacity, and fresh-world construction
    before transferring the capture pair; expected failure preserves the pair for correction and
    retry. After publication, the session is exclusive to one game thread.
    Each successfully published elapsed frame advances the scheduler and executes every planned
    world step. Separate session observers return owned scheduler and simulation snapshots.
    Terminal consumption completes without changing either subsystem. A lower replay failure is
    transactional and retryable. The defensive representation branch is unreachable under the
    current 65,536-frame, 64-step-per-frame, and one-second-step bounds. If those bounds expand and
    the branch is reached, consumed replay/scheduler work and earlier world steps make the session
    permanently failed.
    This starts from fresh synthetic state under caller configuration. It does not restore captured
    scheduler or world state, inject reconstructed input into simulation, reconstruct captured
    gameplay, synthesize host events, restore entities or RNG, pace from a host clock, integrate an
    existing `OmegaApp` or CLI path, render, mix audio, dispatch jobs, persist data, define a stable
    ABI, seek, rewind, loop, roll back, or claim retail timing or determinism.
44. E-0059 adds the exact once-only `--replay-capture` flag. It composes only with
    `--capture-run` and an explicit `--frames=N` from 1 through 65,536. The canonical one-process
    command is `openomega --frames=N --capture-run --replay-capture`; the two boolean flags are
    accepted in either token order. Ordinary runs and capture-only commands remain on their prior
    paths with unchanged output and behavior.
    `main` completes the existing finite capture first and fails closed unless
    `IsCompleteRunCaptureOutcome` accepts it. That gate excludes failures, quit, terminal input,
    partial counts, capacity or origin disagreement, and planned/executed-step disagreement. The
    captured scheduler must also have started with zero remainder, planned steps, and dropped time.
    Only after those preconditions pass does rvalue extraction transfer the pair and normalize the
    outcome. The fresh replay session uses the captured starting scheduler configuration and a new
    empty world. Main-thread replay makes no calls into or reads from the captured `OmegaApp`; the
    host remains alive, so its audio callback may continue independently.
    Replay runs synchronously on the main thread and fails immediately on creation, frame-shape,
    counter-overflow, replay, completion, aggregate, or final-state disagreement. It compares
    replayed-frame, planned-step, clamped-frame, and dropped-frame aggregates with the capture,
    requires the final scheduler state to equal the captured final scheduler state, and requires a
    zero-entity fresh world whose completed steps and simulated time match the capture. Success
    alone prints one fixed `OpenOmega fresh replay:` aggregate line with replayed frames, planned
    and completed steps, clamped and dropped frames, and `completion=complete`.
    This adds no terminal or incomplete CLI replay, input injection, world input consumption,
    gameplay reconstruction, captured scheduler/world checkpoint, entity or RNG restoration,
    persistence, file/wire/stable ABI, or cross-process format. `RunReplaySession` owns no pacing or
    host clock and performs no rendering, audio, or job work. The command adds no seek, rewind,
    loop, rollback, or retail timing or determinism claim.
45. E-0060 adds the first project-owned simulation component and input-consuming replay path.
    `SimulationWorld` directly owns a preallocated `ComponentStore<Position3>` after its entity
    registry. Zero configured position capacity resolves to the world entity capacity at creation;
    no steady-state operation allocates. Positioned creation preflights identity then component
    capacity, destruction erases the exact-generation component before registry reuse, and queries
    return owned copies only. An optional `EntityTranslation` participates in the same fixed-step
    transaction as the simulation clock. Clock representation, target liveness, position presence,
    and signed X/Y/Z addition are checked in that fixed priority before any write. The no-argument
    step remains a neutral wrapper and retains its prior results.
    The new SDL-free `omega_gameplay` target maps a validated `-1/0/1` lateral/longitudinal command
    to exactly one signed synthetic project unit on X/Z. `OmegaApp` creates one positioned debug
    actor and maps W/S/A/D plus gamepad D-pad held state to actions 2 through 5; opposing inputs
    cancel, and one frame command is reused for each scheduler-planned step. Terminal frames still
    stop before scheduler or world mutation.
    `RunReplaySessionConfig::enable_debug_locomotion` is opt-in and defaults false. When enabled, a
    fresh replay owns its own positioned actor, reconstructs the same held command, and exposes only
    an owned optional final position. The CLI enables this path only when the captured schema has
    all four synthetic actions. It keeps the E-0059 output line byte-compatible, validates fresh
    ownership and state without reading the still-live app, and relies on the real-host smoke for
    exact host-versus-replay position comparison. No position checkpoint is added to the capture.
    Every identifier, binding, coordinate, axis, unit, speed, diagonal rule, and capacity is a
    project diagnostic choice. E-0060 claims no retail player, input map, analog behavior, transform,
    collision, gravity, camera, rendering, animation, weapons, AI, mission, or determinism semantics.
46. E-0061 starts with an unwired portable diagnostic-menu value in `omega_app_core`.
    `DiagnosticMenuState` is trivially copyable and its constexpr/noexcept reducer toggles only from
    an already-routed press edge. Action 6 is reserved but has no keyboard or gamepad binding yet.
    `BuildProjectDiagnosticMenuImage` returns an independent owned 128x72 opaque RGBA8 image built
    only from integer rectangles and project-authored 3x5 `DEV` glyph masks. Its 36,864 bytes use
    exactly four colors; complete FNV-1a-64 is `0xdaf00c60d17f05b5`.
    The standalone test freezes state transitions, dimensions, ownership, determinism, opacity,
    color populations, representative geometry, and the complete digest in normal and
    runtime-disabled builds. No `OmegaApp`, SDL, input tracker, GPU upload, draw list, render packet,
    capture, replay, file, decoded asset, or private input consumes this value yet. It establishes
    no retail menu art, text, palette, layout, controls, navigation, selection, activation, pause,
    timing, asset provenance, or UI behavior.
47. E-0062 connects the project diagnostic menu to the native host without making it modal.
    Keyboard F1 and gamepad Start bind to logical action 6. Each input snapshot is appended to an
    active capture first; host/logical terminal checks run next; only a nonterminal frame can pass
    the action-6 press edge through `UpdateDiagnosticMenu`. Movement, scheduling, simulation, and
    rendering continue on that same frame regardless of visibility.
    `OmegaApp::Create` builds and uploads the 128x72 menu image once. It retains one fixed hidden
    draw list containing only the optional base diagnostic command and one fixed visible draw list
    containing the base command followed by the menu command. The menu command uses Q16 source
    `{0,0,65536,65536}`, exact destination `{2048,2048,26624,15872}`, `Stretch` fit, and `Nearest`
    filtering. Frame publication selects between those lists without rebuilding either list or
    uploading the card again. Teardown clears both lists, then releases the menu and base textures
    independently with the existing host-owned fallback.
    Capture/replay preserves action 6 as ordinary input-row state, including on a terminal row, but
    neither the trace nor `RunReplaySession` owns diagnostic-menu visibility. Terminal input leaves
    host menu state unchanged, and the `OpenOmega fresh replay:` success line remains unchanged.
    These bindings, dimensions, rectangles, ordering, and visibility rules are project diagnostics;
    they establish no retail menu, pause, navigation, selection, activation, layout, timing,
    rendering, persistence, or replay behavior.
    Local validation completed with a clean zero-warning MSVC build, focused portable and real-host
    passes, 20/20 repeated real-host runs, 29/29 default CTest, 33/33 opt-in GPU CTest, and 20/20
    unchanged capture/replay CLI repetitions. Runtime-off validation built and ran the exact
    portable menu target and registered 26 tests. The native dependency gate checked 152 files,
    all 209 tooling tests passed, Python compile-all passed, and the public-tree gate checked 239
    indexed text blobs. The exact E-0062 main tree subsequently passed the Windows x86-64 native,
    Linux x86-64 native, and public-tree safety publication-CI jobs.
48. E-0063 replaces the host's visibility bit with a portable two-mode, three-row state machine.
    Default construction remains the safe `DiagnosticPlay`/`StartDiagnosticPlay` value, while
    `InitialDiagnosticMenuState()` explicitly starts `OmegaApp` in `MainMenu` on that first row.
    Invalid mode or row values reset to that startup state before any edge is consumed. The reducer
    remains constexpr/noexcept and observes only press edges from existing actions 2/3/6: W/S plus
    gamepad D-pad Up/Down and F1 plus gamepad Start. Primary has priority, opens main-menu row zero
    from play, enters play from the first row, and is inert on both reserved rows. Previous and next
    clamp at the boundaries; simultaneous navigation edges are neutral.
    Capture publication and host/logical terminal handling still precede the reducer. Normal menu
    frames remain nonmodal: the same actions 2/3 continue as held locomotion input, and scheduling,
    simulation, audio, rendering, capture, and fresh replay are not suppressed. Terminal rows may
    retain the action edges without changing menu, scheduler, world, or renderer state.
    `RunReplaySession` reconstructs the ordinary actions and still consumes 2/3 only for synthetic
    locomotion; it owns no menu mode, row, or transition.
    Startup performs no additional texture upload. It reuses the one 128x72 card texture and owns
    one immutable hidden/base list plus three immutable base/card/amber-marker lists. The full card
    uses source `{0,0,65536,65536}` and target `{2048,2048,26624,15872}`. The marker reuses crop
    `{18432,9103,59392,14563}` at row targets `{3584,7424,4352,9344}`,
    `{3584,10304,4352,12224}`, and `{3584,13184,4352,15104}`; every card and marker command uses
    `Stretch` and `Nearest`. Frames only select a prebuilt list, with no per-frame menu upload, list
    construction, or allocation.
    Every mode, row, binding reuse, clamp, priority, crop, and target is synthetic project behavior.
    E-0063 establishes no retail title/menu sequence, art, text, control map, wrapping, selection,
    activation, pause, transition timing, persistence, replay UI state, private-input result, or
    PCSX2 equivalence. E-0063 local validation used project-generated zero-file fixtures only and
    no private inputs. A clean incremental MSVC build completed with zero warnings and errors. The
    focused portable executable passed directly plus 20/20 repetitions, and the real
    `direct3d12` host smoke passed directly plus 20/20 repetitions. Default CTest passed 29/29 both
    before and after restoring GPU registration; the opt-in GPU configuration passed 33/33. The
    unchanged capture/replay CLI smoke passed 20/20. Runtime-off validation built and ran the exact
    portable menu target directly and through CTest, with 26 tests registered. The native
    dependency gate checked 152 files, all 209 tooling tests passed, Python compile-all passed, and
    the public-tree gate checked 239 indexed text blobs. Publication CI remains separate and is not
    claimed here.
49. E-0064 makes the existing generated 128x72 card readable and narrows modal behavior to the
    simulation boundary. Project-authored 3x5 masks draw `OPEN OMEGA`, the fixed
    `W/S SELECT` / `F1 START` / `ESC QUIT` legend, `START DIAGNOSTIC`, and the two
    `RESERVED SLOT` labels without a font, file, decoded asset, or private input. Startup still owns
    one menu texture upload, one hidden/base list, and three immutable base/card/amber-marker lists.
    Existing source/target rectangles, `Stretch`/`Nearest` choices, list selection, teardown order,
    and the no-additional-upload/no-per-frame-menu-allocation contract remain unchanged.
    On a nonterminal live frame, `OmegaApp` captures the input row, applies the menu reducer, samples
    actual elapsed time, and appends that actual value to capture. A fully valid resulting
    `DiagnosticPlay` state feeds the measured elapsed value into `FrameScheduler`; `MainMenu` and
    invalid state feed zero, skip locomotion planning, and therefore execute no world steps. The
    transition into the menu freezes immediately on that frame, and the transition into diagnostic
    play resumes immediately on that frame. The live clock baseline advances on every modal frame,
    preventing accumulated menu time from becoming catch-up simulation. Input pumping and capture,
    render publication, audio operation/health checks, and job-service ownership remain active.
    Replay can opt into identical behavior through caller-owned
    `RunReplaySessionConfig::initial_diagnostic_menu_state`. Each reconstructed nonterminal row
    reduces that state before the same elapsed gate; the session exposes its current owned state.
    The optional value defaults absent, which preserves legacy nonmodal replay for existing callers.
    The finite capture/replay route supplies `InitialDiagnosticMenuState()` internally but retains
    its exact CLI syntax and output surface. No action or binding, input/trace schema, captured menu
    checkpoint, scheduler/world checkpoint, serialization, or CLI contract changes. Captures keep
    raw elapsed values and never infer or embed the replay's initial menu state. This remains
    synthetic developer UI, not a retail pause, title/menu, timing, persistence, or equivalence
    claim. The final incremental MSVC build was warning-free. Portable diagnostic and replay tests
    each passed directly plus 20/20 repetitions; the Direct3D12 host smoke passed directly plus
    20/20 repetitions; default CTest passed 29/29 before and after the opt-in 33/33 GPU matrix; and
    capture/replay CLI passed 20/20 repetitions plus one 20-frame run. Runtime-off built and ran the
    exact portable target, its focused CTest passed, and 26 tests were registered. The dependency
    gate checked 152 native files, all 209 tooling tests passed, Python compile-all passed, and the
    public-tree gate checked 239 indexed text blobs. Publication CI remains separate and unclaimed.
50. E-0065 turns the synthetic second main-menu row into a complete project-owned Controls submenu.
    `DiagnosticMenuMode::Controls` is byte value 2 and `DiagnosticMenuRow::ShowControls` preserves
    row byte 1. Explicit startup remains `MainMenu` / `StartDiagnosticPlay`; the safe default remains
    `DiagnosticPlay` / `StartDiagnosticPlay`; any invalid mode or row fails closed to startup before
    edge processing. Primary retains priority over navigation. From MainMenu, row zero enters
    DiagnosticPlay, row one enters Controls without changing the row, and reserved row two is inert.
    Primary from any valid DiagnosticPlay row returns to main row zero, while primary from any valid
    Controls row returns to main row one. Previous/next applies only in MainMenu, clamps at rows zero
    and two, consumes press edges only, and is neutral when both edges arrive together.
    `DiagnosticMenuAllowsSimulation` remains true only for a fully valid DiagnosticPlay state, so
    MainMenu, Controls, and invalid representations gate scheduler/world work.
    The main card now draws `CONTROLS` on row one. `BuildProjectDiagnosticControlsImage` independently
    returns another owned 128x72 opaque RGBA8 card with `CONTROLS`, `W FORWARD`, `S REVERSE`,
    `A LEFT`, `D RIGHT`, `F1 RETURN`, and `ESC QUIT`. The main card's exact
    background/cyan/slate/amber populations are 3,739/1,491/3,506/480 and its FNV-1a-64 is
    `0x5303b94979cd74d6`. The Controls card's populations are 2,104/1,326/5,373/413 and its FNV-1a-64
    is `0xa68873cc7444bdf6`. Both are integer-only project images with no font, file, decoded asset, or
    retail input.
    Startup uploads the two cards once and retains a separate controls texture and immutable controls
    draw list. That list contains the optional base diagnostic command followed by the controls card;
    the three MainMenu lists retain the optional base, main card, and row marker. The public zero-file
    fixture therefore owns exactly two uploads and 73,728 resident logical bytes, renders Controls as
    one blit, MainMenu as two, and DiagnosticPlay as clear-only. Destruction clears the controls,
    visible, and hidden lists before releasing controls, menu, and optional base handles; startup
    failure still falls back to authoritative host cleanup.
    Live and opt-in replay reduce each nonterminal row before gating elapsed, so entering Controls
    freezes on the same frame, large modal elapsed is discarded from scheduling, returning to
    DiagnosticPlay advances only that frame's elapsed, and no modal catch-up occurs. Held primary
    levels do not repeat. Terminal resolution remains before the reducer and therefore preserves the
    Controls state even when a primary edge is captured. Null replay menu ownership remains legacy
    nonmodal. No action/binding, input or elapsed trace schema, captured menu state or checkpoint,
    persistence, serialization, CLI, file, wire, or stable-ABI contract changes.
    E-0065 validation used only public project-generated zero-file fixtures. The final MSVC build was
    clean. Diagnostic and replay tests passed directly plus 20/20 repetitions; the Direct3D12 host
    passed directly plus 20/20; default CTest passed 29/29, opt-in GPU CTest passed 33/33, and restored
    default passed 29/29. One 20-frame capture/replay and 20/20 short repetitions passed. Runtime-off
    focused direct and CTest checks passed with 26 registrations. The dependency gate checked 152
    native files, all 209 tooling tests passed, Python compile-all passed, and the public-tree gate
    checked 239 indexed text blobs. Three initial validation-only `SimulationState` C2676 comparisons
    were changed to a fieldwise helper. A direct configure outside `vcvars` also
    contaminated generated cache state; the exact MSVC linker, archiver, and flags were restored
    without a source change. No private data, disc image, retail executable, emulator, or PCSX2 input
    was used. This proves only synthetic developer presentation and gating, not retail menu art,
    controls, pause, timing, persistence, private-input behavior, or emulator equivalence.
    Publication CI remains separate and unclaimed for E-0065.
51. E-0066 adds `BuildTextureStorageTopologyDebugImage`, a portable metadata-only adapter from an
    already-canonical `TextureStorageIR` to one fully owned `runtime::DebugImage`. It borrows the IR
    only for the call, is reentrant on any worker thread, performs no I/O or shared-state mutation,
    and is not wired to `OmegaApp`, `AssetService`, GPU upload, or the retail TDX decoder. A strict
    typed fail order validates top-level dimensions, sample encoding, block presence/count, each
    source-order block's plane presence/count, plane dimensions/encoding/exact byte size, optional
    palette dimensions/cardinality, cumulative budgets, and final image dimensions/bytes/allocation.
    The independent defaults cap blocks, planes, palette entries, and output bytes; a block can emit
    at most 64 plane markers.
    Each source-order block occupies one 32x32 tile in rows of at most eight columns. The image uses
    only opaque background `{8,12,24,255}`, slate `{28,38,58,255}`, cyan `{112,220,255,255}`, and
    amber `{255,196,64,255}`. Cyan 2x2 masks `0x1`, `0x9`, `0x7`, and `0xf` identify the four
    validated sample/transfer enum values; optional palette presence draws an amber plus. Once
    validation succeeds, only block order, sample and transfer encodings, and palette presence affect
    pixels. Payload/palette bytes and dimensions cannot become display pixels. The canonical
    three-block fixture is 96x32 RGBA8 (12,288 bytes), has exact background/slate/cyan/amber
    populations 2,667/372/23/10, and FNV-1a-64 `0xb56c8db088c5a9fe`.
    MSVC configure, focused-target, and full builds were clean with zero warnings or errors. The
    focused executable passed directly plus 20/20 repetitions, focused CTest passed, and default
    CTest passed 30/30. Runtime-off direct and focused checks passed with 27 registrations. The
    dependency gate checked 155 native files, all 209 tooling tests passed, and Python compile-all
    passed, and the final staged-tree public gate checked 242 indexed text blobs. No private data,
    D-drive content, disc image, retail executable, emulator,
    or PCSX2 input was used. Publication CI remains separate and unclaimed. This contact sheet proves
    no retail pixel/display expansion, channel/alpha/nibble/palette/swizzle meaning, material or
    geometry relationship, or app/GPU behavior.
52. E-0067 adds no production API or retail binding. Instead, the public synthetic
    `omega_asset_service_tests` fixture now proves that the existing asynchronous runtime ownership
    chain composes with E-0066. Two direct-24 TDX members with distinct payload seeds load through
    `LevelTextureStore`, `JobService`, and `AssetService`; their ready immutable storage views each
    produce one owned 32x32 RGBA8 topology image with 4,096 bytes. The images are pixel-identical
    despite different canonical plane bytes. Releasing both asset handles restores the exact empty
    two-slot snapshot, and both images remain intact afterward, proving that no image retains a slot
    or storage view. This is test-only integration. No source is selected at startup, and no image is
    connected to `OmegaApp`, a GPU upload, a draw list, a material, or geometry. It establishes no
    retail display, channel, alpha, nibble, palette, swizzle, placement, or visibility behavior.
53. E-0068 promotes project row two to a complete zero-file `ASSET TOPOLOGY` diagnostic screen.
    `DiagnosticMenuMode::AssetTopology` is byte 3 and `DiagnosticMenuRow::ShowAssetTopology` remains
    byte 2. Primary enters from main row two and returns every valid topology row to main row two;
    invalid mode/row state still fails closed, primary keeps priority, and existing clamped,
    simultaneous-neutral navigation is unchanged. Simulation remains enabled only in a valid
    `DiagnosticPlay` state, so live and opt-in replay discard topology elapsed without later
    catch-up, while capture retains raw elapsed and terminal handling precedes reduction.
    The main card now labels row two `ASSET TOPOLOGY` and has exact background/cyan/slate/amber
    populations 3,739/1,481/3,516/480 with FNV-1a-64 `0xf37b700c33071a92`.
    A no-input builder creates the exact E-0066 public three-block fixture and returns the same owned
    96x32/12,288-byte topology raster, populations 2,667/372/23/10, and FNV-1a-64
    `0xb56c8db088c5a9fe`. Fixture and adapter allocation failures use the existing typed allocation
    category. Construction happens before SDL/platform/audio/GPU creation. The host then uploads the
    image after menu and controls, retaining one optional-base-plus-topology `Contain`/`Nearest`
    list. Zero-file startup owns exactly three uploads, three resident textures, and 86,016 logical
    bytes. Lists clear before reverse-order topology/controls/menu/optional-base release.
    The real Direct3D12 test reads sixteen exact topology texels and exercises entry, held/released
    primary, terminal precedence, return, and no-catch-up behavior. No arbitrary retail source,
    per-frame image work, AssetService/LevelTextureStore API, action/binding, trace/checkpoint,
    persistence, CLI, file/wire/stable-ABI schema, material, geometry, or display interpretation is
    added.
54. E-0069 adds three conventional physical aliases to the existing project primary action without
    changing any logical action identifier. Keyboard Return, keypad Enter, and gamepad South join
    F1 and gamepad Start, yielding exactly 15 physical bindings over the same six sorted actions.
    `InputTracker` retains first-down/last-up aggregation: pressing another alias while action 6 is
    held emits no repeat edge, releasing a non-final alias emits no release, and only the last up
    transition releases the logical action. The host consumes the same `WasPressed(6)` value, so
    reducer priority, invalid-state handling, terminal precedence, modal gating, capture action
    rows, and replay behavior are unchanged.
    The zero-file cards advertise the keyboard aliases as `F1/ENTER` and `F1/ENTER RETURN` without
    choosing a platform-specific name for gamepad South. The 128x72 main image has exact
    background/cyan/slate/amber populations 3,725/1,495/3,516/480 and FNV-1a-64
    `0x0a1373c69c8bcce2`; the Controls image has 2,104/1,381/5,318/413 and FNV-1a-64
    `0xd57a5b0500696505`. Topology pixels, texture/draw ownership, three-upload and 86,016-byte
    residency, failure order, and reverse teardown are unchanged. This is project frontend
    usability only and establishes no retail confirmation mapping or controller-label semantics.
55. E-0070 adds keyboard Up and Down as physical aliases for the existing project actions 2 and 3.
    W and D-pad Up remain on action 2; S and D-pad Down remain on action 3. The host therefore owns
    exactly 17 physical bindings over the unchanged six sorted actions. A real SDL sequence freezes
    the modal owners while Down navigates, S cannot repeat that held action, releasing Down cannot
    release it while S remains held, and releasing S last emits the single logical release. Up then
    navigates toward row zero and retains the existing upper clamp.
    The cards advertise `W/S/UP/DOWN`, `W/UP FORWARD`, and `S/DOWN REVERSE`. The 128x72 main image
    has exact background/cyan/slate/amber populations 3,702/1,518/3,516/480 and FNV-1a-64
    `0x9a4662f8f943521d`; the Controls image has 2,104/1,452/5,247/413 and FNV-1a-64
    `0xcfa7cc57696aae0a`. Input and trace schemas, physical-provenance omission, reducer priority,
    terminal precedence, modal gating, topology pixels, three-texture 86,016-byte residency,
    failure order, and reverse teardown remain unchanged. Up/Down also alias the intentionally
    shared synthetic forward/reverse diagnostic actions; no retail input or movement meaning is
    claimed.
56. E-0071 classifies the content composition root before `OmegaApp` creates any owner or service.
    `ClassifyContentStartupState` is a nonallocating `noexcept` borrow that accepts exactly three
    shapes: all five optionals empty (`NoContent`), only `game_data` engaged (`DataMounted`), or all
    five `game_data`, `level_texture_store`, `level_manifest`, `level_content`, and `debug_image`
    engaged (`LevelContent`). Every other shape returns typed `InconsistentOwnership`.
    `OmegaApp::Create` copies that one-byte stage, rejects inconsistent input with exact text
    `content startup state: inconsistent-ownership`, and only then moves the valid content state
    once into its owner. Rejection precedes config/content allocation, logging, jobs, scheduling,
    input, simulation, the topology fixture, and all SDL/audio/GPU work.
    The project main card adds `CONTENT` plus `NONE`, `DATA`, or `LEVEL`; invalid stage bytes render
    `NONE`. The three exact background/cyan/slate/amber populations are
    3,593/1,627/3,516/480, 3,600/1,620/3,516/480, and 3,594/1,626/3,516/480, with respective
    FNV-1a-64 values `0x8e8e3f7fff4f971a`, `0x517ad52bbf1fbe61`, and
    `0x08405186aa105db1`. Controls and topology remain `0xcfa7cc57696aae0a` and
    `0xb56c8db088c5a9fe`. This synthetic owner-health label uses no private input and establishes no
    retail startup stage, UI, content availability meaning, or emulator-equivalence behavior.
57. E-0072 adds a pure borrowed adapter between `ContentStartupError` and process diagnostics.
    `DescribeContentStartupError` is nonallocating, reentrant, and `noexcept`. It accepts exactly
    `InvalidOptions`/`DebugImage` with neither nested error, `GameData` with only
    `game_data_error`, and `LevelTextures` with only `level_texture_error`; a nonempty outer message
    is mandatory. Unknown outer or nested codes and every missing, unexpected, or both-nested
    representation return typed `InconsistentRepresentation`. Valid diagnostics retain the existing
    category and message bytes and borrow the outer message storage. The nested code-name functions
    retain their stable `unknown` fallback independently. Invalid representations emit only the
    fixed sanitized line
    `content startup [inconsistent-error]: content startup error representation is inconsistent`.
    An integration case uses a CMake-created empty root to freeze the existing pre-SDL, nonzero,
    empty-stdout missing-`SYSTEM.CNF` diagnostic. `StartContent` ownership/order and all menu,
    DiagnosticPlay, resource, retry, picker, fallback, persistence, schema, capture/replay, and
    retail-data behavior remain unchanged. The slice is synthetic-only. Focused/full MSVC,
    `omega_core_tests`, process-contract, 30/34/30 CTest, runtime-off with 27 registrations,
    157-file dependency, 209-tooling-test, and Python compile-all validation passed. Publication is
    not yet claimed.
58. E-0073 adds one project-generated no-level `DiagnosticPlay` placeholder while retaining the
    complete level-content image path. `BuildProjectDiagnosticNoLevelImage` returns an independent
    opaque 128x72 RGBA8 allocation without file I/O, platform work, an asset request, or retail
    input. The existing frame and `OPEN OMEGA` header surround `DIAGNOSTIC PLAY`, `NO LEVEL IMAGE`,
    `F1/ENTER MENU`, and `ESC QUIT`. Its exact background/cyan/slate/amber populations are
    3,327/1,285/4,124/480 and its FNV-1a-64 is `0x37f823d27a4cb3ce`.
    After the E-0071 classifier accepts an owner aggregate, an existing owner `debug_image` wins;
    otherwise the app builds the placeholder and sends it through the unchanged diagnostic upload
    and draw path. Zero-file order is placeholder, menu, controls, topology. The four distinct
    resident textures own exactly 122,880 logical bytes. The hidden list is now one full-source,
    full-target `Contain`/`Nearest` base blit; MainMenu lists contain base/card/marker, and Controls
    and AssetTopology contain base/card. Teardown still clears every list before reverse topology,
    controls, menu, diagnostic release. These residency totals describe no-level startup only.
    START DIAGNOSTIC remains available in `NoContent` and `DataMounted`; simulation, locomotion,
    elapsed, return, terminal priority, reducer, capture, and replay contracts do not change. This
    proves only a synthetic missing-level presentation contract, not typed allocation recovery,
    full-framebuffer identity, retail UI or level selection, private-input behavior, DataMounted
    hardware coverage, or emulator equivalence. Focused/full MSVC, direct and 20/20 repeated
    diagnostic/replay/D3D12 host checks, 30/34/30 default/GPU/restored CTest, 20-frame and 20/20
    short capture-replay, runtime-off with 27 registrations, the 157-file dependency gate, all 209
    tooling tests, and Python compile-all passed. Publication remains unclaimed.
59. E-0074 introduces one typed content launch profile at the composition root without changing the
    launch parser or `StartContent`. `content.data_root` and optional `content.level_code` are known
    strict keys in the effective store produced by an explicitly selected `--config` file plus
    source-order `--set` overrides. The resolver validates that configured tuple first, even when a
    direct CLI content pair will win. A direct root plus optional level then replaces the configured
    tuple atomically, so a root-only CLI selection cannot inherit a configured level. No content
    keys and no direct root resolve to no profile and retain zero-file startup. Configured levels
    require a nonempty representable native path, and every level is 1 to 32 ASCII alphanumeric
    bytes normalized uppercase. Fixed, non-echoing errors use `missing-data-root`,
    `invalid-data-root`, `invalid-level-code`, or defensive `invalid-options`; main presents them as
    `content launch profile [category]: message` after runtime-setting validation and before the
    existing E-0072 startup diagnostic. Only `/openomega.cfg` is newly ignored because it may hold a
    private path. This slice performs no ambient/default discovery, persistence, picker, path
    existence check, asset decode, or retail inference. Focused/full MSVC, direct core and process
    checks, 30/34/30 default/GPU/restored CTest, runtime-off direct/focused checks with 27
    registrations, the 157-file dependency gate, all 209 tooling tests, and Python compile-all
    passed. Publication remains unclaimed.

60. E-0075 adds narrow per-user default-profile discovery at the composition root. A compile-time
    host classifier performs no I/O, and a separate pure lexical resolver accepts only captured
    absolute roots. It maps Windows `LOCALAPPDATA` to `OpenOmega/openomega.cfg`, macOS `HOME` to
    `Library/Application Support/OpenOmega/openomega.cfg`, and `XDG_CONFIG_HOME` to
    `openomega/openomega.cfg` with an absolute-`HOME` `.config/openomega/openomega.cfg` fallback.
    Main captures only those relevant variables after successful parsing and help handling, and
    only when explicit `--config` is absent; Windows uses the wide environment representation.
    Explicit config therefore bypasses all default discovery and inspection. A missing default is
    silent, a regular file uses the existing bounded loader, and `symlink_status(error_code)`
    rejects a reported final-entry symlink, dangling symlink, directory, or other non-regular type
    without following it. The slice intentionally does not claim rejection of parent symlinks or
    every Windows reparse-point shape. File values still precede source-order `--set`, E-0074
    validation, and atomic direct CLI content selection. Discovery is lexical: it does not
    normalize, canonicalize, absolutize, expand tokens, write, create directories, migrate, print
    a success path, choose a level, or inspect owner content. Serialized local validation passed:
    focused and full MSVC builds completed cleanly; direct `omega_core_tests` and the exact process
    contract passed; default, opt-in GPU, and restored CTest passed 30/30, 34/34, and 30/30;
    runtime-off direct and focused checks passed with 27 registrations; the dependency gate checked
    160 native files; all 209 tooling tests and Python compile-all passed; and the staged public-tree
    gate checked 247 indexed text blobs. On Windows, the non-missing inspection-error oracle was
    explicitly skipped because MSVC maps the available
    invalid and overlong candidates to not-found. Commit, DCO, publication, and exact-main
    validation remain unclaimed.

61. E-0076 adds an app-private, stateless startup-failure presentation boundary before SDL
    initialization. Only failures from default-profile capture or runtime-config load,
    runtime-settings resolution, E-0074 content-profile resolution, and E-0072 content startup are
    projected. Existing stderr bytes and exit codes remain unchanged; main flushes stderr before a
    best-effort, parentless `SDL_ShowSimpleMessageBox` call using the fixed title
    `OpenOmega startup error`. The adapter performs no SDL initialization or shutdown, owns no
    global state, and retains no borrowed text; suppression reads SDL's cached environment view.
    Its owned 640-byte result holds the fixed message plus bounded 48-byte category and 384-byte
    detail projections built with bounded local stack storage. Printable ASCII is preserved,
    whitespace collapses and trims, other bytes become `?`, overflow receives `...`, and empty
    fields receive fixed fallbacks. Unknown stages fail closed to the label `startup`. Only exact
    `OPENOMEGA_DISABLE_STARTUP_DIALOG=1` suppresses presentation; invalid policy enum values are
    also suppressed. CMake injects suppression into the synthetic process and capture contracts,
    and the dedicated unit source calls the presentation function only under suppressed or invalid
    policy while checking that SDL remains uninitialized. Parse/help, app creation, SDL/GPU/audio,
    loop, capture, and replay errors remain console-only. Serialized local validation passed:
    focused and full MSVC builds; the direct dialog unit and exact process contract; CTest 31/35/31;
    runtime-off direct and focused `omega_core_tests` with 27 registrations and no dialog target;
    the 163-file dependency gate; all 209 tooling tests; Python compile-all; and the staged
    public-tree gate checked 250 indexed text blobs. Interactive manual smoke, commit, DCO,
    publication, and exact-main validation remain unclaimed.
    Implementation
    used only public project source and generated literals; no private or owner files, D-drive
    content, disc image, executable, emulator, or PCSX2 input was used.

62. E-0077 adds `BuildFirstLevelTextureTopologyPreview`, a blocking, non-hot-reloadable startup
    adapter for exclusive game/main-thread use. It requires the concrete `AssetService` aggregate
    snapshot to be empty, requires at least one canonical texture, selects only
    `LevelTextureStore::HandleAt(0)`, requests once, waits for that service, borrows the immutable
    ready view only while building the existing metadata-only topology image, and always attempts
    `Release` for every accepted handle before return. Request rejection verifies transactional
    rollback. Accepted-path precedence is release failure, then residual public snapshot mismatch,
    then an earlier Get or
    image error, then success. The final snapshot is always captured; after successful `Release`,
    comparison covers capacity, free, active, retired, queued, loading, ready, failed, in-flight,
    and resident-byte fields; the hidden generation may
    advance and is intentionally outside the aggregate contract. Eight fixed diagnostics expose no
    paths, names, locators, hashes, offsets, payloads, or nested text, while optional enums retain
    only the applicable texture-store, asset-service, and topology-image categories.
    `OmegaApp::Create` uses this adapter only for the complete `LevelContent` shape and still does so
    before SDL/GPU initialization. The existing upload order and base-plus-card Contain/Nearest draw
    list are unchanged. A generated one-texture fixture yields an independently owned 32x32/4096
    byte image with FNV-1a-64 `0x666d00371feff88d`; with its 2x2 base and two 128x72 cards the host
    owns four textures and 77,840 logical bytes. `NoContent` and `DataMounted` continue to use the
    synthetic 96x32 topology and 122,880-byte presentation. Source and generated contracts cover
    repeat cleanup, bounded image failure, empty inventory, malformed and foreign first handles,
    preoccupied service, queue rollback, GPU probes, and fallback residency. Serialized local
    validation passed: focused and full MSVC builds; direct asset and D3D12 app smokes; focused asset
    CTest; default/GPU/restored CTest at 31/35/31; 20/20 repeated D3D12 app smokes; runtime-off
    direct/focused asset checks with 27 registrations; the 165-file dependency gate; all 209 tooling
    tests; and Python compile-all. The staged public-tree gate checked 252 indexed text blobs. The
    merged commit carries its matching DCO sign-off; PR #38 published E-0077 as exact `main` commit
    `2a9182e560a504125a5b8278a7202fcad7220c44`, and exact-main run `29710089254` passed all three
    jobs. Implementation used only public source and generated fixtures; no private or owner files,
    D-drive content, disc image,
    executable, emulator, or PCSX2 input was used. No display-texel, channel, alpha, palette,
    nibble, swizzle, mip, UV, material, cell, placement, visibility, geometry, retail rendering,
    gameplay, streaming, eviction, GPU-pinning, asynchronous-upload, or emulator-equivalence claim
    is made.

63. E-0078 adds `BuildPacked24TransferDebugImage`, a stateless, reentrant worker-thread utility that
    borrows canonical storage only for one call and returns an independently owned `DebugImage`.
    Its strict eligibility shape is nonzero matching top-level/plane rectangles, known `Packed24`
    sample and transfer-element enums, exactly one block, exactly one plane, no palette, and exactly
    three source slots per rectangle element. Unknown enum values are invalid; other known values
    are unsupported. Validation priority then checks independent source and output overflow,
    source cardinality, the 48 MiB source limit, the 64 MiB output limit including `size_t`, and
    allocation. Sixteen fixed name/message pairs expose no dimensions, payload, offsets, source
    identity, or exception text. Consecutive source triples become output slots zero through two;
    output slot three is the synthetic constant `0xff`.
    Directly constructed public 16x16 fixtures map 768 bytes to 1,024 bytes with hashes
    `0x4abb645f50f5a325` (seed `0x21`) and `0x36590f25eee3ab25` (seed `0x61`). The standalone test is
    registered against `omega_runtime`, including runtime-off builds, and freezes all diagnostics,
    priority, no-allocation overflow oracles, exact/one-below budgets, source-slot mapping, repeat
    ownership, and survival after source mutation/destruction. Serialized local validation passed
    focused/full MSVC, the direct unit plus 100/100 repeated runs, focused and 32/36/32 CTest,
    runtime-off direct/focused checks with 28 registrations, the 168-file dependency gate, all 209
    tooling tests, and Python compile-all. The staged public-tree gate checked 255 indexed text
    blobs. The merged commit carries its matching DCO sign-off; PR #39 published E-0078 as exact
    `main` commit `47378588471d9271a43dfaeb56f3138c01137e1f`, and exact-main run `29710670162`
    passed all three jobs. E-0078 changes no app, GPU, renderer, AssetService,
    E-0077 adapter, or existing test. It assigns no channel names, display-ready correctness, row
    origin/order, swizzle, color space, alpha semantics, premultiplication, block/plane purpose,
    Packed32/indexed expansion, palette/nibble policy, material/UV/geometry binding, gameplay, or
    emulator equivalence. Only public source and generated fixtures were used; no private or owner
    files, D-drive content, disc image, executable, emulator, or PCSX2 input was accessed.

64. E-0079 is the first locally validated Windows portable-package contract. It permits only an
    MSVC x64 `Release` build and fixes the archive name as
    `OpenOmega-0.1.0-windows-x86_64.zip`. The ZIP must contain
    exactly one `OpenOmega-0.1.0-windows-x86_64/` directory with `openomega.exe`,
    `launch-openomega.cmd`, `README-WINDOWS.md`, `LICENSE`, `NOTICE`, `TRADEMARKS.md`,
    `THIRD_PARTY_NOTICES.md`, and `LICENSES/SDL3.txt`, with no second wrapper directory or extra
    entry. The packaged executable uses the static MSVC runtime. The five-line command launcher
    changes to its package directory, forwards all arguments, and returns the executable's exit code;
    the contract requires exact `OpenOmega native shell: rendered_frames=0` stdout with empty stderr
    and an invalid-option forwarding oracle from an unrelated working directory. A sibling
    `.zip.sha256` file names and hashes the exact
    archive. The output is an unsigned preview and excludes proprietary inputs and assets, PCSX2,
    user profiles, PDBs, and developer tools. Serialized local validation generated the package and
    matching sidecar, passed the focused package contract, and observed one canonical root with
    exactly two directories and eight files. The launcher is exactly 96 ASCII bytes with five CRLF
    endings and no BOM; both its zero-frame success and invalid-option forwarding oracles passed.
    The executable is x64 PE32+ with the Windows console subsystem. The local MSVC 19.38 binary
    imports exactly 11 allowed direct OS DLLs; the contract additionally permits only the Windows
    OS synchronization API-set `API-MS-WIN-CORE-SYNCH-L1-2-0` observed under hosted VS 18/MSVC
    19.51/Windows SDK 26100, while rejecting SDL, MSVC, UCRT, debug-runtime, every other API-set, and every other
    unapproved import. It contains no source/build path prefix after deterministic `Release` path
    mapping and the enforced narrow/wide byte scan. Full MSVC CTest
    passed 32/32 `Debug`, 32/32 `RelWithDebInfo`, and 33/33 `Release`; the 168-file dependency gate,
    all 209 tooling tests, and Python compile-all passed. The staged public-tree gate checked 258
    indexed text blobs. DCO passed, PR #40 merged the slice as exact `main` commit `ff8376b`, and
    exact-main run `29713390065` passed all four jobs and retained the named Windows archive
    artifact. General clean-machine behavior remains unclaimed. Validation used only public source
    and generated output; no private or owner files, D-drive
    content, disc image, executable,
    emulator, or PCSX2 input was accessed.

65. E-0080 defines a main-push-only consumer boundary for the exact retained E-0079 artifact. A
    separate fresh GitHub-hosted `windows-2022` job depends on `windows-portable-package` and
    downloads the same workflow run's named `OpenOmega-0.1.0-windows-x86_64` artifact. It has no
    source checkout or toolchain-setup step and invokes no compiler, CMake, CTest, or producer build
    tree. The consumer requires exactly the ZIP and `.zip.sha256` sidecar as regular non-reparse
    files, parses the BOM-free ASCII lowercase digest, two spaces, exact filename, and single-CRLF
    sidecar syntax, recomputes the archive SHA-256, and extracts exactly the
    frozen two-directory/eight-file package tree into an isolated directory. It replaces `PATH` with
    the minimal Windows system directories; redirects `LOCALAPPDATA`, `APPDATA`, `USERPROFILE`,
    `TEMP`, and `TMP` into an isolated synthetic profile; disables the startup dialog; and invokes
    the absolute packaged launcher through the absolute system command processor from an unrelated
    working directory. The two required cases are exact zero-frame stdout, empty stderr, and exit
    zero, followed by exact invalid-sentinel stderr, empty stdout, and exit one. SHA-256 manifests of
    the downloaded artifact, extracted package, unrelated working directory, and synthetic profile
    must remain byte-for-byte identical after each launch. Local emulation of the exact
    PowerShell body extracted from the workflow YAML passed against both the freshly regenerated
    local package and retained E-0079 main artifact. Full Release CTest passed 33/33, the 168-file
    dependency gate and all 209 tooling tests passed, Python compile-all passed, and the staged
    public-tree gate checked 258 indexed text blobs. PR #41 merged E-0080 as exact `main` commit
    `4868e1118bcd32c6713a7f4be57dd243d40996ed`. Exact-main push run `29714679947` completed all
    five jobs successfully. Consumer job `88266108572` downloaded retained artifact ID
    `8450186290`; GitHub verified that artifact envelope's SHA-256 as
    `aea3f4869d17874305bf6027bce370d884ddcaed35e3e9d7a4bc2217aa6baac2`, while the strict
    sidecar/recompute check verified the retained inner package ZIP SHA-256 as
    `c06ce722572c5edbb0c34ce6b3fc985bcadd4e24ebda0cc07dff59df65ccfe5d`. The job emitted
    `fresh-VM portable consumer: OK (artifact, checksum, tree, launch, immutability)`. This hosted
    result covers only same-run artifact transfer, integrity, extraction, package-relative launch,
    exact process behavior, and non-mutation on that runner image. Because its zero-frame path
    returns before application/platform creation, E-0080 does not validate a window, menu
    interaction, GPU, audio, owner data, a physical or arbitrary clean machine, another Windows
    version, another Windows Server release, or another runner image. Both digests identify retained
    E-0080 artifact `8450186290`; they are not hashes for every later package build.

66. E-0081 locally smoke-validates a separately identified current portable package's generated
    no-content main menu on the tested Windows host. The package sidecar matched its ZIP, all eight
    extracted regular files matched their ZIP members, and the absolute extracted launcher started
    without arguments from an unrelated empty working directory under an isolated profile. No owner
    data, level, or configuration argument was supplied. The exact `OpenOmega - native runtime`
    title appeared, and a temporary operator-visible screenshot showed only the project-generated
    no-content OPEN OMEGA diagnostic menu. Its ignored PNG was deleted after display and is not
    retained or committed. Native Escape closed the visible
    run, and a second same-package exit oracle returned zero. Direct process evidence contained two
    stdout lines, exactly three INFO-only stderr records, and zero warning, error, or fatal stderr
    records. The unrelated working directory remained empty. GPU drivers wrote only shader-cache
    files inside the isolated profile, so profile immutability is intentionally unclaimed. No
    proprietary input, retail asset, retail executable, emulator, PCSX2 input, or D-drive filesystem
    input was accessed. This is not a pixel-golden test or evidence of retail-menu fidelity, owner-data
    behavior, controller or audio coverage, another host, another Windows release, or PCSX2
    equivalence.

67. A private aggregate-only ReSymbol cross-check pinned the explicit `Ps2EeR5900LeCoreV1`
    decoder profile. Because the frozen ELF was unavailable, it used the available private 32 MiB
    EE memory and reproduced 38/38 already-public loader control-transfer sites: 33 calls and five
    branches, with zero mismatch or error classes. No private path, hash, byte, address, symbol,
    disassembly, input copy, or ReSymbol output is committed. This is a bounded decoder-consistency
    result, not a semantic, decompilation, or retail-behavior claim.

68. E-0082 connects the strict E-0078 Packed24 projection to complete `LevelContent` startup without
    weakening the mandatory E-0077 topology path. `BuildFirstLevelTextureDiagnosticPreview` performs
    the topology and transfer attempts inside the existing canonical-handle `AssetService`
    transaction, returns independently owned images, and exposes a typed optional transfer rejection.
    All Packed24 diagnostic failures are intentionally nonfatal: startup keeps topology-only output
    and logs one fixed identity-free INFO category. The public synthetic Packed24 fixture produces a
    frozen 32x32 topology image and a 16x16/1,024-byte transfer image. App-state inspection pins five
    resident uploads totaling 78,864 bytes and an exact three-command
    base-plus-split-topology-plus-transfer draw list; a GPU probe checks the first four source triples
    and their synthetic `0xff` fourth slots. Teardown releases transfer before topology. A synthetic
    Packed32 fixture remains topology-only with four resident uploads totaling 77,840 bytes and
    exactly one fixed `unsupported-sample-encoding` INFO record containing no fixture identity. One
    helper-level output-limit rejection preserves topology and restores aggregate `AssetService`
    state, while source inspection shows one Request/Release transaction. A test-only exact
    77,840-byte renderer-pool budget rejects the optional fifth reservation before GPU allocation;
    startup retains four textures, the full-width topology draw list, exact pool-state preservation, and one
    fixed identity-free `upload-failed` INFO record. Serialized local
    validation passed focused/full MSVC builds; direct asset-service and Direct3D12 app smokes;
    default/GPU-opt-in/restored CTest at 32/36/32; dependency 168; tooling 209; Python compile-all;
    and the public-tree gate over 258 indexed text blobs. Diff and DCO checks passed. PR, head,
    publication, and exact-main validation remain unclaimed. The checks do not prove a cumulative
    exact request count, every rejection category, full-image GPU fidelity, backend-specific
    fifth-upload failures, release failure, allocation injection, or valid-transfer failure
    rollback. The GPU integration test ran only in the local opt-in suite; default hosted CI
    compiles but does not run it. No
    channel, alpha, row-order, swizzle, color-space, material, geometry, retail-rendering, gameplay,
    or emulator-equivalence semantics are assigned. Only public source and generated fixtures were
    used; no private or owner file, proprietary input, D-drive content, disc image, retail executable,
    emulator, or PCSX2 input was accessed.

69. E-0083 pivots save ownership to an OpenOmega-native persistence foundation instead of PS2 RAM,
    a memory-card device, or emulator savestates. The independent `omega_persistence` target exposes
    one movable, noncopyable `SaveDatabase` intended for sole composition-root ownership and
    externally serialized persistence/game-thread use. Its public surface is platform-neutral and
    the dependency gate permits only self-includes; bidirectional cross-layer and PCSX2 edges fail
    closed. An operating-system-held lock rejects a second live owner as `busy`. Two complete
    checksummed little-endian A/B snapshots hold canonical sorted key/value records with explicit
    schema versions and revisions. A commit validates one optimistic batch against a private copy,
    writes and flushes a private same-directory temporary, atomically replaces the inactive slot,
    synchronizes the directory entry, and then performs only non-allocating in-memory publication.
    Strict configurable and hard record/key/value/logical/file limits guard every decoded extent and
    allocation. Synthetic tests cover fresh and interrupted genesis, put/update/delete,
    deterministic prefix listing, malformed keys/conditions, preconditions, revision ABA rejection,
    atomic rejection, limits, moves, exclusive ownership, anchored/no-follow namespace handling,
    hard-link containment, missing-established-slot rejection, torn-newest fallback, post-recovery
    commit, both-slot corruption, integrity-checked future versions, and fail-closed transient I/O.
    The focused Debug and Release tests each passed 20 consecutive runs; a strict Linux compile and
    direct filesystem test also passed.
    the complete runtime-disabled Debug build was warning-free and its 29/29 CTest suite passed.
    Full runtime-enabled Debug and Release builds were warning-free; their 33/33 and 34/34 CTest
    suites passed, including the Release portable-package contract. The 171-file dependency scan,
    all 212 tooling tests, Python compile-all, and the staged public-tree gate over 262 indexed text
    blobs passed. App/profile/menu integration,
    typed game schemas, and PS2 import/export codecs are intentionally not claimed by this slice.
    Only public source and generated fixtures were used; no private or owner file, proprietary input,
    D-drive content, disc image, save, memory-card image, savestate, executable, emulator, or PCSX2
    input was accessed.

70. E-0084 adds the first typed native schema and composes persistence into startup. The bottom-level
    `omega_profiles` catalog borrows one stable `SaveDatabase`, accepts exact 128-bit lowercase-hex
    identifiers, and stores one bounded versioned metadata marker per explicit profile. Display names
    must be control-free UTF-8 from 1 through 64 bytes; caller-supplied UTC-millisecond timestamps are
    bounded, creation time is immutable, modification time cannot move backwards, and successful
    database generations are exposed only as opaque optimistic-concurrency tokens. Listing is
    deterministic by identifier, ignores unrelated records, and never creates or selects a default.
    The pure native-save path resolver accepts only absolute already-captured platform roots for
    Windows, macOS, and XDG hosts. App-owned `NativePersistence` heap-owns the database and catalog at
    stable addresses, validates the catalog before platform creation, and moves into `OmegaApp` with
    catalog-before-database destruction. Probe-only startup never touches persistence; zero-frame
    startup bootstraps it and reports the profile count. Synthetic unit, process, package, and
    fresh-runner contracts cover creation, validation, optimistic updates, reopen, exact genesis,
    path failure, probe non-mutation, and future schema rejection. Static validation passed; C++
    compilation, publication CI, exact-main validation, profile menu actions, active-profile policy,
    and owner data remain unclaimed. No private or owner file, proprietary input, D-drive content,
    disc image, memory-card image, save, executable, emulator, or PCSX2 input was accessed.
71. E-0087 adds the first strict Indexed8 TDX display-candidate projection without changing the
    canonical decoder or wiring pixels into startup. One nonzero Indexed8 texture, one block, one
    matching nonzero `Packed8` plane, exact index cardinality, and one internally exact 256-entry
    palette are required. The caller must provide all four unresolved policies: identity versus
    bit-3/4 CLUT permutation, one of six mappings of source slots zero through two, opaque versus
    unchanged versus doubled-clamped source-slot-three alpha, and linear top-down versus bottom-up
    whole-row order. Unknown policies and every broader shape fail closed. Caller source/output
    budgets may tighten but cannot raise the synthetic 16 MiB-plus-1,024-byte and 64 MiB hard maxima.
    Generated tests cover all 256 indices, two distinguishing palettes, every exposed candidate,
    exact and one-below budgets, hard-limit rejection, malformed shapes, deterministic independent
    ownership, and source nonmutation. Serialized local validation passes focused Debug and Release
    builds with zero warnings, direct units in both configurations, 100/100 repeated Debug runs, the
    full 34/34 Debug CTest suite, formatting, diff, the 174-file dependency gate, all 212 tooling
    tests, Python compile-all, and the staged public-tree gate over 265 indexed text blobs.
    Publication and exact-main validation remain pending. This diagnostic makes no retail
    channel, alpha, row-origin,
    swizzle, palette, texture-role, material, menu, GPU, rendering, gameplay, or emulator-equivalence
    claim. Only public project source and generated fixtures were used; no private or owner input,
    proprietary data, D-drive content, disc image, retail executable, emulator, or PCSX2 input was
    accessed.

71. E-0086 adds `measure_frontend_hog_topology.py`, a bounded aggregate-only scanner for one
    supplied HOG or a recursively discovered directory of HOGs. It follows only normalized `.hog`
    members and emits a fixed public vocabulary: approved extension/category counts, archive depth,
    exact versus zero-padded nested extent families, fixed member-size buckets, and sibling
    same-basename extension-pair totals. All unapproved suffixes collapse into `other`; paths,
    member names, hashes, offsets, payload bytes, raw suffixes, identifiers, per-archive rows, and
    exception text remain absent. Configurable limits sit below hard ceilings for filesystem and
    archive counts, filesystem and archive depth, root/nested/file-directory/name extents, and
    cumulative parser work. Exact and zero-padded nested fixtures plus malformed offset, name,
    collision, depth, size, deterministic-output, and identity-containment tests are synthetic.
    No owner input, proprietary byte, executable, emulator, PCSX2 input, or D-drive content was
    accessed, and no front-end role, lookup, layout, state, binding, rendering, audio, or behavior
    is inferred.

72. E-0085 adds the independent `omega_ps2_compat` standard-container leaf. It strictly recognizes
    fixed 8 MiB PS2 cards with either 512-byte logical pages or 528-byte raw pages, validates the
    versioned superblock and fixed geometry, and canonicalizes raw ECC/spare data. A bounded reader
    follows IFC/FAT chains for one explicitly selected top-level directory and returns its immediate
    regular files as ordered owned opaque bytes while rejecting loops, cluster reuse among the
    traversed root, selected-directory, and selected-file chains, nested live directories, malformed
    entries, ambiguity, and configured limits. A deterministic exporter
    constructs a new logical or raw card with fresh superblock, allocation tables, directory entries,
    cluster chains, and ECC; it never patches an existing card. Synthetic fixtures cover envelope,
    filesystem, error, capacity, determinism, and logical/raw round-trip contracts. No owner save,
    retail payload, private input, PCSX2 session, emulator state, or D-drive input was used, and no
    Omega Strain payload semantics are claimed. Adversarial hardening accepts both standard unused
    IFC padding encodings while rejecting active metadata in erase block zero and backup blocks before
    the allocation extent, validates root and selected-directory backlinks, emits root-parent mode
    `0xA426`, and maps allocation failures to typed results. Warning-free focused Debug and Release
    builds, their direct tests and focused CTest 3/3, plus the full Debug build and CTest 40/40 passed.

73. E-0089 replaces the earlier diagnostic-menu shell with a bounded native front end. Startup
    snapshots the already-open native profile catalog once, rejects more than 1,024 summaries,
    preserves strict `ProfileId` order, and projects at most three valid UTF-8 display names into
    fixed 24-cell project-font labels without creating or selecting a profile. `OmegaApp` owns one
    immutable diagnostic base plus Main, Profiles, Controls, and AssetTopology cards and draw lists;
    frame updates perform no catalog access, upload, or allocation. Four main rows use primary-first
    reduction, all card screens return to their originating row, invalid states normalize to the
    initial Main row, and only DiagnosticPlay advances the scheduler and simulation. Capture/replay
    retains its fixed schema while renaming the in-process state field to `front_end`. Synthetic
    reducer, raster-hash, persistence-boundary, SDL/GPU, capture, replay, process, and package
    coverage is present; final serialized C++ validation is recorded in E-0089's ledger entry. This
    is project-owned bootstrap UI and makes no retail menu, art, font, input, timing, save-policy,
    behavior, or emulator-equivalence claim. No private or owner input was accessed.
74. E-0090 adds the stateless reentrant `DecodeVagAdpcm` retail adapter and canonical owned
    `MonoPcm16IR`. It strictly accepts the path-free aggregate envelope established across all 8,665
    VAG entries: a 48-byte big-endian `VAGp` header, versions `0`/`4`/`0x20`, reserved zero, 22,050
    Hz, 16-byte-aligned frame data, and either an exact end or 16-2,032 zero tail bytes. Standard
    predictor identifiers zero through four, shifts zero through twelve, low-then-high signed
    nibbles, cross-frame history, rounded prediction, and per-sample PCM16 clamp are decoded with
    checked integer arithmetic. Every source frame retains its raw flag byte and exact sample offset,
    but the adapter never stops, repeats, loops, resamples, mixes, selects, streams, or plays audio.
    Fixed 4 MiB ADPCM and 32 MiB logical-output ceilings compose with `DecodeLimits`; allocation and
    representation failures remain typed. Complete independent synthetic golden vectors cover all
    five predictors plus shifts, nibble order/sign, both clamps, cross-frame history, markers,
    malformed and unsupported inputs, tail shape, exact/one-below budgets, hard limits, ownership,
    and determinism. No owner audio, proprietary input, D-drive content, executable byte, emulator
    capture, or retail asset is present in the implementation or tests. Runtime selection, SDL
    upload, playback/mixing policy, title-specific flag meaning, and retail comparison remain open.
75. E-0091 adds a stateless reentrant LPD counted-envelope decoder and fully owned canonical IR.
    The fixed 22-word little-endian header supplies exactly 21 source-track entry counts; every
    counted four-byte entry is retained in source order as opaque bytes. Checked preflight debits the
    complete physical input, one root plus 21 tracks plus all entries, and the root plus entry bytes
    before allocation. Scratch and nesting are unused. Exact inputs and zero-only tails through the
    aggregate-proven fixed 1,932-byte maximum canonicalize identically. The observed 8-byte corpus
    minimum is not enforced as an invented minimum or alignment rule. The fixed 4,096-byte
    physical-input maximum derives 1,002-entry, 1,024-item, and
    `sizeof(LpdEnvelopeIR) + 4,008`-byte output ceilings that callers cannot raise. All 21 final-sized
    vectors are constructed inside the typed allocation-error boundary. Truncated headers and
    payloads, a wrong first word, hostile counts, the first nonzero tail byte, the first byte beyond
    the tail ceiling, exact/one-below caller budgets, and first/later output allocation failures have
    synthetic coverage. The adapter is not composed into content, startup, audio, animation, or
    playback and assigns no track, scalar, timing, interpolation, pose, or VAG relationship. Only
    tracked aggregate evidence and project-generated fixtures were used; no private or owner file,
    proprietary input, D-drive content, disc image, retail executable, emulator, or PCSX2 runtime
    input was accessed. The focused MSVC Debug target then built with zero warnings or errors, its
    direct executable returned zero, and focused CTest passed 1/1. One initial test-only MSVC C3535
    failure from a mixed-type braced range was corrected with an explicit fixed `size_t` case array
    before the clean rerun. The focused MSVC Release target also built with zero warnings or errors,
    its direct executable returned zero, and focused CTest passed 1/1. Post-rebase static validation
    passed the 200-file dependency gate, all 246 tooling tests, the 303-blob public-tree gate, and DCO
    for the one signed commit. The runtime-OFF full MSVC Debug integration tree built with zero
    warnings or errors and full Debug CTest passed 37/37. Rebase and signed commit are complete;
    publication and exact-main validation remain pending.
76. E-0093 adds the stateless reentrant `DecodeSkasTextEnvelope` retail adapter and canonical owned
    `SkasTextEnvelopeIR`. It accepts only the aggregate-proven 5,132-5,156-byte physical span,
    5,129-5,155-byte printable-ASCII/CRLF logical text, one-through-three-byte zero tail, exactly 72
    final-CRLF-terminated lines, five empty lines, and 67 lines containing exactly one colon. The
    result retains exact text, source-order opaque line ranges and terminator lengths, and only
    structural counts; it never splits or interprets labels and values or assigns a relationship to
    SKA. Its format-scoped default raises only the shared string budget to the fixed 5,155-byte
    ceiling; explicitly supplied caller input, string, root-plus-72-item, and exact owned-output
    budgets only tighten fixed ceilings. The flat decoder uses zero dynamic scratch and no nesting
    edge. Generated adversarial tests cover envelope boundaries, character and line-ending failures,
    every structural count, exact budgets, ownership, determinism, and both owning-allocation
    failures. Post-rebase focused MSVC Debug and Release targets built with zero warnings or errors;
    both direct executables returned zero and focused CTest passed 1/1 in each configuration. The
    runtime-OFF full MSVC Debug integration tree built with zero warnings or errors and full Debug
    CTest passed 39/39. Static validation passed Microsoft formatting, the 208-file dependency gate,
    all 246 tooling tests, Python compile-all, JSON/JSONL parsing, the 313-blob public-tree gate, and
    diff checks. Rebase, one signed commit, and DCO are complete; publication and exact-main
    validation remain pending. No private or owner input, proprietary byte, path, filename, D-drive
    content, disc image, executable, save, savestate, emulator state, or PCSX2 input was accessed or
    embedded. Semantic interpretation and runtime consumption remain open.
77. E-0094 adds a stateless reentrant passive VPK wrapper-envelope decoder over the tracked
    aggregate evidence only. It strictly accepts raw prefix bytes `b" KPV"`, the independent
    little-endian word 2,048 at `0x08`, physical spans from 1,320,960 through 9,005,056 bytes, and
    independent 2,048-byte physical alignment. Its trivially copyable fixed descriptor retains the
    source-order opaque bytes at `0x04..0x07` and `0x0c..0x0f`, physical byte count, and derived
    aligned-block count. The observed word and alignment remain separate facts whose equal values
    prove no header-size, block-size, or alignment-declaration relationship. One fixed item and
    `sizeof(VpkWrapperEnvelopeDescriptor)` logical bytes are charged after the complete input;
    caller limits may only tighten the fixed ceilings. Scratch, strings, and nesting are zero. The
    borrowed remainder and payload are uninspected and never retained. Generated fixtures cover
    size and alignment boundaries, raw-signature direction, every signature byte, the observed
    word, opaque-field preservation, payload independence, ownership, determinism, typed path-free
    errors, exact and one-below caller budgets, unraiseable hard limits, and zero-resource budgets.
    No codec, ADPCM, rate, channels, audio or music role, seek table, streaming, playback, storage
    geometry, runtime, or emulator semantics are assigned. Only tracked aggregate evidence and
    project-generated fixtures were used; no private or owner file, proprietary input, D-drive
    content, disc image, executable, save, memory-card image, savestate, emulator, or PCSX2 runtime
    input was accessed. Post-rebase static validation passed Microsoft 4-space Allman formatting,
    the 211-file dependency gate, all 246 tooling tests, Python compile-all, JSON and JSONL parsing,
    diff checks, and the public-tree gate over 317 staged indexed text blobs. The focused MSVC Debug
    and Release targets built with zero warnings or errors; both direct executables returned zero,
    and focused CTest passed 1/1 in each configuration. The runtime-OFF full MSVC Debug integration
    tree built with zero warnings or errors, and full Debug CTest passed 40/40. Rebase, one signed
    commit, and DCO are complete; publication, owner-corpus validation, runtime integration, and
    exact-main validation remain deliberately unclaimed.
78. E-0096 adds explicit session-only active-profile selection to the synthetic native Profiles
    screen without moving persistence ownership. `NativePersistence` remains the sole
    `SaveDatabase`, `ProfileCatalog`, and sorted-summary owner. Before SDL startup,
    `MakeFrontEndStartupModel` copies the first three displayed fixed labels and their immutable
    `ProfileId` values into one owned fixed model. The pure `ReduceFrontEnd` function consumes only
    state, logical press edges, and a bounded visible-slot count, then returns owned state plus a
    typed First/Second/Third `SetActiveProfile` command. Previous/next clamp within displayed slots;
    primary wins over simultaneous navigation and publishes the pre-navigation slot. Empty-profile
    or stale out-of-range selection publishes no command, empty navigation is inert, and primary
    retains the established return-to-Main transition. `OmegaApp` revalidates a command against the
    immutable model on its game thread and copies the ID into an optional app-session value. It
    performs no catalog or
    database read/write, creates no profile or default, and defines no persistent active-profile
    policy. The one Profiles texture, one base list, and three marker lists are all built at startup;
    frame-time state only selects fixed draw data and performs no enumeration, allocation,
    rasterization, upload, or I/O. Teardown clears the marker lists before releasing their existing
    texture. Capture bytes remain unchanged: production supplies replay the same bounded startup
    count, replay derives and publishes the same typed command from captured input, and terminal
    frames complete before reduction and cannot select a profile. Synthetic tests cover copied-ID
    lifetime, invalid states and slots, counts zero through
    three plus adversarial counts, every edge combination, primary priority, exact marker lists,
    app-thread selection, unchanged catalog population, capture/replay parity, and terminal
    precedence. The exhaustive reducer checks cover 3,145,728 mode/row/count/edge combinations and
    245,760 valid-mode/row plus byte-slot/count/edge combinations. After rebasing onto exact `main`
    commit `2c5689dbb9d3f670f12cf5b6364f96beb9b748f6`, the `openomega` executable and three focused MSVC
    Debug test targets built with zero warnings or errors. Focused CTest passed 2/2, and the direct
    SDL/GPU app smoke passed with dummy audio. The runtime capture/replay CLI smoke was not rerun in
    this integration-only pass. The previously verified two changed Profiles rasters were
    independently rechecked from exact 36,864-byte RGBA dumps
    using a separate Python FNV-1a implementation, SHA-256, and four-color histograms; the temporary
    dumps and test instrumentation were removed. Static validation passed the 211-file
    native-dependency gate, all 251 tooling tests,
    ledger JSONL parsing, diff checks, and the public-tree gate over 321 indexed text blobs. No
    private or owner data, D-drive content, proprietary input, disc image, proprietary executable,
    save, memory-card image, savestate, emulator, PCSX2 runtime, or runtime network input was
    accessed. No full integration build, Release build, push, or retail-fidelity validation is
    claimed.

## Disc observations

- The root contains `SYSTEM.CNF`, `SCUS_972.64`, `OVL_DNAS.BIN`, `SFO_GAME.INI`, and PS2
  networking/USB IRX modules.
- `GAMEDATA/` is organized by level (`BELARUS1`, `BELARUS2`, `CHECHNYA`, `ITALY`,
  `KYRGSTAN`, `LORELEI`, `MINSK`, and others).
- Repeated level payloads include `DATA.HOG`, `DATA.POP`, `OBJECTS.HOG`, `SCRIPTS.HOG`,
  `TEX.HOG`, `MAPVUM.HOG`, `SND.HOG`, and `SNDVAG.HOG`.
- Static analysis confirms attached `-lMINSK` syntax and the MINSK table entry. The independent
  `-x` startup mode changes splash and one construction path; it does not select the level.
- The ISO contains 28,672 bytes after the ISO9660 physical end. Preserve the original image
  for LBA and tail analysis; the extracted tree is not a bit-perfect substitute.
- HOG files begin with five little-endian 32-bit words, followed by `count + 1` payload
  offsets, a NUL-delimited ASCII filename table, alignment padding, and concatenated payloads.
  The first word varies and its checksum/tag semantics remain unknown.
- `MINSK/SCRIPTS.HOG` contains six modules: `INIT.SO`, `MUSIC.SO`, `OBJECTIVES.SO`,
  `PRAGUE.SO`, `UTILS.SO`, and `VOICE.SO`.

## Next focused pass

1. Define the next profile-owned campaign/checkpoint schema only from independently corroborated
   evidence, and decide any persistent active-profile policy separately from E-0096's session-only
   selection. Preserve process/package isolation by redirecting persistence in tests. Build the
   separately evidenced Omega Strain payload mapper over the now-bounded standard card codecs;
   never make a PS2 memory-card device, guest RAM, or emulator savestate part of the shipping
   runtime.
2. Use the accepted deterministic VUM trace only as a structural baseline. Collect additional
   bounded pairs through controlled comparisons that change one research condition at a time,
   require strict validation and byte-identical repeats, and compare only sanitized relative
   ranges and counts. Keep sites anonymous and bounds private. Promote no relationship without
   cross-capture stability plus independent corroboration, and infer no geometry, topology,
   vertex, material, packet, draw, placement, visibility, or gameplay semantics from the current
   header-only aggregate rows or the absence of VIF1 chunks. A zero VIF1 count does not rule out
   copied buffers or consumption outside the bounded observation window.
3. Use E-0041's ineligible partition only to prioritize controlled observations of retail name
   lookup and MTRL-record consumption. Test one-pass extension removal, basename/stem/suffix, or
   other alias behavior as separate bounded experiments because E-0041 excludes them. Do not connect
   `MaterialCatalogIR` to `LevelTextureStore` until lookup behavior and material consumption are
   independently observed and corroborated.
   E-0042 now supplies the separate offline one-pass extension experiment; its positive lexical
   candidates still do not observe lookup, extension elision, or MTRL consumption. Keep basename,
   stem, substring, repeated-extension, suffix-family, and other alias hypotheses separate.
   E-0043's `AssetService` accepts only an already-issued `LevelTextureHandle`; it does not change
   this research boundary or consume either lexical experiment.
   E-0047 now applies explicit source-crop, fit, and filter policy to a frame-packet draw list that
   still references only the existing project-generated diagnostic texture. It does not connect an
   asset, decoded texture storage, catalog name, material record, locator, cell, or mesh to a draw
   command.
4. Validate the TDX scorer's favored direct-family nibble and palette candidates through an
   independent behavioral oracle; separately resolve transfer-`0x00` swizzle and channel expansion
   before producing display-ready pixels or GPU uploads.
5. Continue POP beyond the now-native guarded hypothesis envelope only through independent evidence.
   Test record-internal invariants and controlled behavioral consumption before promoting any literal
   to a boundary or any observed word/stride to count or record semantics; independently connect
   consumed fields to placement or visibility behavior. Passive-descriptor acceptance is a
   conformity check, not semantic corroboration.
6. Capture PS Rewired network behavior separately before designing any replacement service.
7. Run the synthetically verified size-only HOG-member collector privately for `.gui/.fnt/.ie`,
   review only its fixed-schema aggregate, and keep the semantic GUI-envelope gate closed unless a
   separate falsifiable grammar plus consumer evidence emerges.

## Installed research tools

- PCSX2 source/debug build under `third_party/pcsx2`.
- IDA 9.1 with MIPS processor support (private local installation).
- Ghidra 12.1.2 (private local installation).
- radare2 (private local installation).

## Safety rules

- Never commit anything under `private/`, `runtime/`, `third_party/`, or `downloads/`.
- Publish no firmware, executable, archive, asset, save-state, or decrypted proprietary data.
- Keep claims tied to hashes, logs, captures, and reproducible scripts.

## Front-end asset reconnaissance (branch `claude/frontend-asset-decoding`, 2026-07-20)

Verified outcomes (Python and documentation only; no C++ decoder added). Ledger entry E-0095.

- **`.gui` topology promotion (schema_version 2).** `tools/measure_frontend_hog_topology.py`
  now promotes only the `.gui` member suffix out of `other` into its own frozen public aggregate
  category `gui`; `.fnt` and `.ie` deliberately stay in `other`. The `gui` label echoes the suffix
  only and asserts no menu role, layout, lookup, timing, or render semantics. Output remains
  fixed-schema aggregate, deterministic, and path-free; caller `DecodeLimits` may only tighten fixed
  hard ceilings. Focused tests 14/14; full Python tooling discovery 251/251; native-dependency gate
  211; public-tree gate, compile-all, JSONL parse, and `git diff --check` all pass.
- **Front-end evidence audit** — `analysis/formats/FRONTEND-EVIDENCE-AUDIT.md`. A four-tier table
  (confirmed / aggregate-only / hypothesis / missing) for `.gui`/`.fnt`/`.ie` and front-end
  containers, derived only from tracked files. Key confirmed facts: these are HOG member suffixes,
  not standalone disc files; recursive occurrence counts 77/3/79 and top-level 21/3/23; no format
  handler, ledger entry, or loader trace assigns them any semantics.
- **Decoder-coverage matrix** — `analysis/formats/DECODER-COVERAGE.md`. Every observed format family
  classified (canonical decoder / structural envelope only / passive descriptor only / aggregate
  scanner only / unknown) with a tracked-source citation, CMake/test registration cross-checked, and
  a ranked privacy-safe next-evidence queue. `.PF`, `.TM2`, and `.tbl` are kept explicitly unknown.

Remaining limitations (deliberately unclaimed):

- **GUI-envelope tool not built.** The tracked tree records only existence and occurrence counts for
  `.gui`/`.fnt`/`.ie` — no member size, byte, header field, or alignment. The audit's Lane C gate
  verdict is therefore **NO**: there is not enough evidence to freeze a non-misleading positional
  or envelope schema, so `measure_frontend_gui_envelopes.py` was intentionally not written. The
  semantic envelope tool plus the evidence-gap note is the correct conservative result. A separate
  size-only fingerprint collector now exists under E-0097, but no owner-corpus result is tracked.
- No native `GuiEnvelopeIR` or decoder; no retail menu role, lookup, field, layout, state, timing,
  rendering, or audio semantics for `.gui`/`.fnt`/`.ie`; no owner-corpus, behavioral-oracle,
  runtime, packaged-host, or PCSX2-equivalence validation.
- The AI brief's hot-file list cites three headers that do not exist in the tree
  (`native/include/omega/asset/decode_result.h`, `decode_test_hooks.h`,
  `native/include/omega/retail/tdx_decoder.h`); the real contracts live in
  `native/include/omega/asset/decode.h` and `tdx_texture_storage_decoder.h`. Worth correcting the
  brief before the next pass.

## Format-dossier and size-fingerprint integration (E-0097, 2026-07-20)

- `analysis/formats/dossiers/catalog.json` deterministically covers all 47 suffixes in the three
  tracked inventories. It mirrors the authoritative 31-family decoder matrix and keeps 16
  whole-disc-only families in a separate disposition with no invented decoder or system status.
- `tools/measure_member_structural_fingerprint.py` and its generated tests implement the frozen
  size-only contract documented in `analysis/formats/MEMBER-STRUCTURAL-FINGERPRINT.md`: default
  `.gui/.fnt/.ie`, optional allowlisted `.bnk/.gun`, no target payload reads, identity-free output,
  fixed local error vocabulary, and bounded traversal, distinct-size, arithmetic, and output state.
- The collector implementation and tests prove no owner-corpus result. Size GCD is a divisor of
  sizes, not an address-alignment claim, and size regularity alone does not justify an accept/reject
  parser or native descriptor.
- The 47 dossiers retain tracked evidence and explicit nonclaims. The contract test freezes family
  coverage, authoritative status partitions, required evidence sections, resolving links,
  Windows-safe names, tracked catalog sources, and banned privacy constructs.
- Claude provenance is retained through commits `6c0e7e8` and `238aa87`; the useful scaffold
  checklist was consolidated into `docs/native-scaffolds/README.md`, while thirteen uncompiled,
  premature C++ declarations were removed.

Still unclaimed: an owner-corpus collector run or output; new format/header/alignment evidence;
native GUI/FNT/IE/BNK/GUN descriptors; retail consumer semantics; runtime integration; or PCSX2
behavioral parity.
