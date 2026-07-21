# OpenOmega

[![Native CI](https://github.com/Krilliac/OmegaStrain-Reimplementation/actions/workflows/native-ci.yml/badge.svg)](https://github.com/Krilliac/OmegaStrain-Reimplementation/actions/workflows/native-ci.yml)
[![License: Apache-2.0](https://img.shields.io/badge/License-Apache--2.0-blue.svg)](LICENSE)

OpenOmega is a clean-room, pure-native compatibility reimplementation research project for
*Syphon Filter: The Omega Strain* (NTSC-U, `SCUS-97264`). PCSX2 is used as a behavioral
oracle and debugger; it is not intended to be a dependency of the eventual native runtime.
The shipping runtime is pure modern-CPU native code: no MIPS interpreter, recompiler, translated
retail instruction blocks, or PS2 execution layer.

Omega Strain compatibility is also the proving workload for an independently designed OpenOmega
engine and SDK. Evidence-backed canonical data, runtime services, and authoring tools may become
reusable as their contracts stabilize; this project does not claim to recover or represent Bend
Studio's historical engine source or internal toolchain.

## Current verified baseline

- Official PCSX2 `master` built from commit `86d76bbf590566d9ea74d381eeff3acd9856503a`.
- Build output: `third_party/pcsx2/bin/pcsx2-qtx64-avx2.exe` (PCSX2 2.7.485).
- E-0099 records runnable-tool and configuration-initialization readiness for a separately
  maintained local custom PCSX2 branch based on that exact source. The branch combines bounded VUM
  producer commit `13df8ef7`, front-end producer commit `78ea3c8`, and coordinated-lifecycle commit
  `d17a521c`. Its one-job MSVC RelWithDebInfo `PCSX2` core target and true Release `pcsx2-qt` target
  both build successfully. The Release executable has SHA-256
  `2D3EAC421BDD1E6C09C8454F574D4AF32AD41F4E51BF87AA254CDAFB93BEA870`; import inspection confirms
  release Qt/KDDockWidgets dependencies with no debug-CRT chain, and linked-image inspection finds
  both private producer namespaces. An isolated `-version` launch reports
  `v2.7.485-3-gd17a521ca` before its intentional exit 1, and an isolated external
  `-datapath -testconfig` launch exits 0. The custom source, build tree, executable, configuration,
  and any future private inputs or reports remain outside this repository. No producer capture,
  BIOS, disc, savestate, private site, or owner input was used for E-0099.
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
- A retail-only passive POP post-terrain descriptor fail-closed checks the already-established
  ordered 19-candidate aligned-literal envelope and five exact arithmetic extent hypotheses without
  promoting any literal, word, stride, or opaque range to section, count, record, or payload
  semantics. Its fixed-schema native verifier discovers and accepts all 18 owned-corpus `.POP`
  candidates with zero rejections or errors. Independent accepted-only
  input/items/logical-output/string/scratch maxima are `919360 / 1 / 168 / 26 / 80036`; no tuple
  co-occurrence is asserted. The descriptor is fixed owned hypothesis metadata, not canonical asset
  IR, and the report exposes no source identity or descriptor observation.
- The native host validates an owner-supplied NTSC-U data root, loads MINSK as 299 canonical
  manifest cells with matching owned spatial meshes, and renders a deterministic synthetic
  canonical-COL wireframe contact sheet through SDL_GPU/D3D12.
- E-0102 adds an explicit native opening-movie path: bounded MPEG-PS and H.262 inspection, Windows
  Media Foundation video decode to owned NV12, project-owned RGBA8 conversion, stable GPU texture
  updates, and bounded PSS PCM presentation through a device-demand clock. The modal boot reducer
  supports skip, EOS, safety timeout, and fail-open transition to the existing native front end while
  suppressing simulation catch-up and isolating capture/replay. This is one narrow compatibility
  path, not a general media engine or a frame-exact retail audiovisual parity claim. Its custom PCM
  grammar remains a project-defined provisional compatibility hypothesis: generated fixtures prove
  implementation boundaries, and the recorded owner-stream smoke proves only that one stream passed
  them, not the field meanings or deinterleave semantics independently.
- E-0104 adds an opt-in generated-playback acceptance smoke at the `OmegaApp` boundary. Generated
  RGBA and silent PCM traverse the actual SDL GPU host, dummy-driver SDL audio service, event-input
  path, boot reducer, scheduler gate, and synthetic front end. Natural EOS reaches Main and a fresh
  next-frame Down edge selects Profiles; primary skip is covered before playback and after two and
  five advances. The smoke asserts zero transition simulation, complete movie-resource and audio
  containment, and categorical redaction of a hostile path-bearing playback error. It does not read
  or decode an owned stream, exercise Media Foundation lifetime, prove finite-source PCM or hardware
  drain, establish perceptual synchronization or retail timing, or replace repeated owner runs.
- The native host owns the system-default SDL playback stream as 48 kHz stereo F32. Its callback
  supplies project-owned silence while idle and can consume opening-movie PCM from a fixed 4,096-
  frame ring. Project callback code performs no file access, logging, explicit locking, or dynamic
  allocation. The main thread applies the bounded provisional PSS `SShd`/`SSbd` 48 kHz stereo PCM16
  compatibility shape, deinterleaves exact frame ranges into owned memory, queues converted samples,
  and contains the stream on skip, completion, or failure. This is opening-movie presentation, not a
  general mixer, proof of final hardware playback, or a retail timing-parity claim. E-0090 separately
  adds a backend-neutral, bounded VAG-to-owned-mono-PCM16 decoder for the complete observed
  48-byte-header/version/rate/zero-tail envelope and standard five-predictor PS-ADPCM frames. Raw
  frame flags and marker sample offsets survive as data, but no flag applies playback, looping,
  resampling, mixing, asset selection, or title-specific audio policy.
- E-0093 adds a separate bounded SKAS structural-text adapter for the two-candidate aggregate
  envelope. It owns the exact printable-ASCII/CRLF text and 72 opaque source-order line ranges,
  validates five blank lines, 67 one-colon lines, and one through three trailing zero bytes, and
  assigns no label, value, relationship, animation, skeleton, or SKA-association semantics.
- E-0101 records bounded passive FNT, GUI, and IE project-defined prefix hypotheses backed by
  generated tests. They retain only narrow neutral scalars/ranges, own no source payload, and remain
  offline structural scaffolding. No tracked evidence records the retail provenance of their exact
  constants, and no font, widget, hierarchy, layout, rendering, consumer, or retail-menu semantics
  are assigned.
- An app-owned, non-hot-reloadable SDL input leaf owns the process-global event pump, keyboard and
  mouse routing, and explicitly opt-in primary-gamepad support. No controller is required to start,
  navigate, or play the native diagnostic runtime. Gamepad discovery is disabled by default and is
  enabled with `--set=input.gamepad_enabled=true`; only then does the leaf filter controller events
  by SDL instance ID, reset gamepad controls on disconnect, and promote the next available device. A
  deterministic headless virtual-gamepad test covers that hotplug boundary; selecting only one
  primary gamepad is a synthetic host-shell policy, not a claim about retail behavior.
- The standalone `omega_persistence` foundation implements OpenOmega-owned native saves instead of
  PS2 RAM, memory-card-device, or emulator-savestate state. Its versioned little-endian key/value
  database uses two complete checksummed snapshots, private-temp flush plus atomic inactive-slot
  replacement, synchronized directory publication, strict allocation and file bounds,
  generation-unique record revisions, atomic optimistic batches, anchored no-follow file access,
  and an operating-system-held exclusive lifetime lock. Synthetic tests cover reopen, torn-newest
  fallback, missing-established-slot rejection, post-recovery commit, hard-link containment,
  namespace anchoring, both-slot corruption, future-version rejection, and competing owners.
  The native profile catalog and app-owned startup composition never create or select an implicit
  default profile. E-0089 snapshots at most 1,024 already-existing profiles before SDL startup and
  projects at most three sorted names into a fixed project font. E-0096 also copies those three IDs
  into the owned bounded front-end model and lets an explicit project-owned Profiles action select
  one as session-only `OmegaApp` state. E-0106 adds one explicit zero-to-one transition: Primary on
  an empty Profiles screen creates fixed project profile `00000000000000000000000000000001` named
  `PROFILE 1` through the transactional catalog, but leaves the active profile unset until a later
  Primary edge selects it. Complete empty and one-profile GPU presentations are preloaded before
  the transaction, so the successful durable commit is followed only by a no-throw presentation
  swap. Main, Profiles, Controls, and AssetTopology remain modal; only DiagnosticPlay advances
  simulation. This synthetic shell
  establishes no retail art, layout, input, save policy, behavior, or fidelity. The standalone
  `omega_ps2_compat` layer now recognizes strict standard 8 MiB logical/raw card envelopes, reads
  one explicitly selected top-level directory as ordered opaque files, and creates a new
  deterministic logical or raw card without patching an existing image. Owner saves and exported
  card images stay outside version control; Omega Strain payload interpretation remains a separate
  evidence-backed mapping slice.
- E-0108 makes entry into that project-owned shell explicitly profile-gated. The pure
  `constexpr`/`noexcept` `PlanProjectFrontEndStartupState` opens `Profiles` on its first slot only
  for a bounded, internally consistent nonempty snapshot or an exact empty snapshot with explicit
  first-profile-creation capability. A malformed or out-of-bounds count pair, or zero profiles
  without that capability, fails closed to the established `Main` startup state. Live `OmegaApp`
  startup and fresh replay call the same planner; neither path creates or selects a profile
  automatically. The fresh-replay summary publishes only its bounded final project-owned
  `front_end=<mode>/<row>/<slot>` state, and the existing CLI smoke pins the exact empty-profile
  route to `Profiles/Profiles/First` without changing capture schema. Generated acceptance
  fixtures compose the existing opening-movie and profile
  lanes as `movie -> Profiles -> explicit create -> release -> explicit select -> Main`, including
  a swallowed skip press that must be released before creation. A generated bounded playback failure
  also fails open to `Profiles` while leaving the durable catalog empty. This policy adds no GPU
  resource: tested `NoContent` no-opening-movie accounting remains 8 slots / 233,476 logical bytes
  for an empty catalog and 6 / 159,748 for a nonempty catalog; the generated `NoContent` 2x2 movie
  fixture temporarily raises the empty case to 9 / 233,492, then releases exactly one slot and 16
  bytes on transition. Compilation and execution of the new fixtures remain pending. This is
  project-owned startup composition, not retail
  startup, menu, profile, save, PS2, proprietary-input, or behavioral-parity evidence.
- E-0109 makes the existing Profiles Primary action an explicit project-owned durable
  confirmation before it publishes either the projected Main state or the app-session active
  profile. `NativePersistence` stores `profiles/active` at record schema 1 as exactly 32 bytes:
  `OOACTPRF`, little-endian payload version 1, zero flags, zero reserved word, and the raw 16-byte
  `ProfileId`. Its default app database ceiling is 1,025 records, providing capacity for the
  bounded 1,024-profile startup model plus this pointer without claiming a namespace reservation.
  Bootstrap rejects a malformed, unsupported, or stale pointer through one typed startup category.
  A same-ID reconfirmation validates the durable revision but writes no new generation. Every app
  launch still enters `Profiles/Profiles/First` with session activation unset; a valid stored
  confirmation is never an automatic selection. Generated persistence totals are exactly one
  41-byte logical profile value at generation 1, then two records / 73 logical value bytes at
  generation 2 after the 32-byte pointer commit. Failure leaves Profiles, database totals, session
  state, and GPU state unchanged. Capture/replay retains the existing schema and remains
  persistence-free, and the live policy adds no GPU resource. A non-installed test-only writer now
  prepares the same deterministic profile plus confirmation for the direct process and Windows
  portable-package contracts. Both launch the real shell twice at zero frames, require one profile
  with empty stderr, and require the native-save bytes and neighboring test/package trees to remain
  unchanged; the package's exact member allowlist excludes both the helper and native-save state.
  Static validation passed all 340 tooling tests, Python compile-all, the 262-file native dependency
  gate, the 109-record ledger gates, the 439-blob staged public-tree gate, diff checks, and independent
  core/package reviews. Local C++ compilation/execution and publication CI remain pending. This
  establishes no retail or PS2 save, menu, profile, campaign, checkpoint, memory-card,
  proprietary-input, owner-corpus, or behavioral-parity semantics.
- E-0111 supersedes only E-0109's production record ceiling and no-checkpoint boundary. Startup
  enumerates at most the shared front-end maximum of 1,024 direct profile markers with
  `ProfileCatalog::ListBounded`; checkpoint-shaped and other child records do not consume that
  marker budget, and a 1,025th direct marker fails closed before parsing. The app database ceiling
  is now 2,049 records: enough for 1,024 profile markers, one independent 32-byte project
  diagnostic checkpoint per profile, and `profiles/active` when no unrelated record consumes
  capacity. `FrontEndCapabilities` keeps creation, diagnostic-start support, and the active-profile
  requirement independent. Main/Start Diagnostic is inert unless start support is open and the
  optional gate is satisfied by a confirmed ID that still resolves in the bounded model. A
  successful live transition publishes typed `StartDiagnosticCampaign`, applies
  `PrepareDiagnosticCampaignStart`, and only then publishes DiagnosticPlay and simulates. The
  canonical record `profiles/<id>/campaigns/diagnostic/checkpoint`, schema 1, contains exactly
  `OODIAGCP`, little-endian payload version 1, zero flags/reserved word, and the raw 16-byte
  `ProfileId`. Bootstrap rejects malformed, mismatched, or orphan markers through one sanitized
  category; the same confirmed active ID and observed pointer revision are prerequisites, and an
  exact existing marker is a no-write success. Replay may publish the typed command from its
  caller-supplied support plus identity-free confirmation mirror but never touches persistence.
  Generated live and opening-movie paths reach generation 3, three records, and 105 logical value
  bytes. Static validation covers 361 tooling tests and 111 evidence records; no local C++ build or
  executable test was attempted under the host RAM STOP condition, and remote compilation/execution
  remains pending. This is a project-generated diagnostic launch marker, not a retail/PS2 campaign,
  save, checkpoint, gameplay, continuation, world-state, memory-card, emulator, owner-input, or
  parity claim.
- E-0112 adds the first complete project-owned profile-to-character-to-briefing-to-session path. A
  stateless
  `CharacterCatalog` stores bounded metadata at
  `profiles/<profile-id>/characters/<character-id>/metadata` and validates the parent profile on
  every operation. Explicit profile confirmation now prepares and opens a Characters surface.
  Primary on its exact empty state creates fixed native ID
  `00000000000000000000000000000001` named `DIAGNOSTIC CHARACTER` but does not select it; a later
  release and Primary edge confirms it through the 48-byte `OOACTCHR` active-character pointer and
  opens the project-owned `BriefingRoom`/mission-selection surface. Switching profiles atomically
  replaces the profile pointer and invalidates the character pointer. Mission activation remains
  inert until both per-launch identities resolve against their bounded models, then
  `PrepareGameSessionStart` validates both durable revisions and writes or reuses the 48-byte
  `OOGAMECP` marker at the character-owned diagnostic-session key before publishing DiagnosticPlay.
  Cancel returns from BriefingRoom to Characters, and the DiagnosticPlay menu edge returns to
  BriefingRoom. Capture/replay retain bounded commands and identity-free confirmation mirrors;
  neither owns persistence or a catalog view. The production database uses the existing hard 4,096
  record and 255-byte key ceilings because the canonical session key exceeds the standalone 96-byte
  default. Generated catalog, reducer, corruption, profile-switch, reopen, and session tests cover
  the complete transactional boundary. A separate owner-only manual smoke reached the existing
  MINSK diagnostic topology and actor marker through create, select, start, close, reopen, reselect,
  and restart; no owner input or captured byte entered this tree. This is a functional native
  bootstrap into the current diagnostic game view. BriefingRoom is a synthetic selector for the one
  startup-bound diagnostic content value; it is not a retail character editor, appearance, loadout,
  campaign, mission catalog, scene, save, checkpoint, world-state, or parity claim.
- E-0103 introduced the project-owned cancel edge; the current keyboard/mouse-first host mapping
  requires no gamepad. W/A/S/D and the arrow keys navigate menus and move in DiagnosticPlay;
  Return, keypad Enter, or F1 select; Space or left mouse selects in menus and fires the synthetic
  diagnostic cue in play; Escape or Backspace cancels; T or held right mouse targets in play, with
  right mouse also acting as menu back; and F10 quits. The 26 physical bindings cover nine logical
  actions. Gamepad buttons and D-pad directions become optional aliases only after
  `--set=input.gamepad_enabled=true`, never a startup or play requirement. Live capture and replay consume bounded logical
  actions without physical provenance. These bindings, cues, and transitions are synthetic
  native-shell policy, not evidence of a retail control map, combat system, or menu graph.
- E-0086 adds a bounded aggregate-only front-end HOG topology scanner. It accepts one supplied HOG
  or recursively discovers HOG files below one supplied directory, then follows only normalized
  `.hog` members through the established span parser. Its fixed schema reports approved public
  extension/category counts, archive-depth and exact/zero-padded extent distributions, fixed
  member-size buckets, and sibling same-basename extension-pair totals. Unknown suffixes collapse
  to `other`; no input path, member name, hash, offset, payload byte, raw suffix, identifier,
  per-archive row, or exception message is emitted. Strict configurable plus hard count, depth,
  directory, name, file-size, and cumulative-span caps fail closed. The slice uses only synthetic
  exact, padded, malformed, and limit fixtures and makes no front-end role, lookup, layout, state,
  binding, rendering, audio, or retail-behavior claim.
- The native content service resolves all 5,351 manifest cells across all 18 levels into 5,351
  owned spatial meshes with zero errors: 20,203 canonical nodes, 93,356 leaves, 889,640 vertices,
  1,239,980 triangles/references, and 2,137 normalized empty meshes.
- The native TDX adapter converts all 15,248 texture assets into owned renderer-neutral storage:
  15,442 source-order blocks, 17,960 primary planes, 285,521,272 primary bytes, 15,190 palette
  blocks, and zero errors. It normalizes 4,112 duplicate-proven implicit zero bytes without
  guessing pixel or channel layout.
- E-0091 adds a stateless bounded LPD counted-envelope adapter. It requires the fixed 22-word
  little-endian header, preflights the 21 source-track counts and exact payload extent, and owns each
  source-order four-byte entry without interpreting it. Exact inputs and all-zero physical tails up
  to the aggregate-proven 1,932-byte maximum canonicalize identically; the observed 8-byte minimum
  is not enforced as an invented minimum or alignment rule. The aggregate-proven 4,096-byte
  physical-input ceiling derives fixed maxima of 1,002 entries, 1,024 items, and
  `sizeof(LpdEnvelopeIR) + 4,008` output bytes; caller limits may only tighten them. All 21
  final-sized vectors are explicitly allocated inside the typed error boundary. Scratch and nesting
  are unused. The slice is not wired to startup, audio, animation, or playback and assigns no track,
  scalar, timing, interpolation, pose, or VAG-relationship semantics.
- E-0092 adds a strict native PAR text-envelope boundary over the public aggregate evidence. It
  preserves exact owned CRLF logical text and source-order line ranges, recognizes only the eight
  observed six-decimal first-line version tokens, and omits only a validated bounded NUL tail. It
  assigns no key/value, field, path, asset-role, particle, compatibility, rendering, gameplay, or
  emulator-equivalence semantics.
- E-0094 adds a strict passive VPK wrapper-envelope boundary over the 85 aggregate-fingerprinted
  spans. It requires raw `b" KPV"`, the independent little-endian word 2,048 at `0x08`, the observed
  1,320,960-through-9,005,056-byte physical range, and independent 2,048-byte alignment. Its fixed
  descriptor retains only two source-order opaque four-byte prefix values, physical byte count, and
  derived aligned-block count; it retains no remaining wrapper or payload byte. The equal numeric
  values of the observed word and alignment imply no semantic relationship. No codec, ADPCM, rate,
  channels, audio or music role, seek table, streaming, playback, storage geometry, runtime wiring,
  or emulator semantics are assigned.
- E-0098 adds a bounded passive SO module descriptor over the already tracked custom little-endian
  grammar. It retains section ranges, counts, neutral record summaries, and structural regularity
  results but no LP string, code-cell value, or opaque payload byte. Its 512 KiB input ceiling is a
  synthetic decoder safety policy, not an owner-corpus or wire-format limit. The descriptor is
  analysis-only and is never consumed by content, simulation, `ScriptService`, or the shipping
  runtime; retail script cells are never interpreted, translated, recompiled, dispatched, or
  executed.
- E-0087 adds a diagnostic-only Indexed8 candidate projection over one strict canonical storage
  shape: one matching `Packed8` plane, one exact 256-entry palette, and exact index, palette, and
  output cardinality. Callers must explicitly choose identity versus bit-3/4 palette permutation,
  one of all six source-slot-zero-through-two mappings, opaque/slot-three/doubled-clamped alpha,
  and linear top-down versus bottom-up rows. Caller budgets can tighten but cannot raise the
  synthetic 16 MiB-plus-palette source and 64 MiB output hard maxima. This utility is not wired to
  startup or the renderer and establishes no retail display, channel, alpha, row-origin, swizzle,
  palette, material, or menu semantics.
- A bounded common-archive containment scan accepts all 18 runtime levels, 5,351 manifest cell
  occurrences, and 5,413 scanned archive-directory occurrences with zero errors and finds zero
  normalized `.TDX`-suffixed members. This extension-bounded negative result does not exclude
  embedded texture data or assign ownership or a texture-to-cell/material/mesh relationship.
- A separate direct-only scan accepts both explicit sibling container classes for every
  runtime level: 36/36 exact containers and 5,801/5,801 direct TDX members with zero errors,
  collisions, malformed textures, nested traversal, or non-TDX members. The two role classes
  contribute 5,765 and 36 occurrences. This establishes direct containment only, not retail
  ownership, necessity, priority, or a texture-to-material/cell/mesh binding.
- The public-safe native level-texture verifier accepts all 18 runtime levels, 36 explicit sources,
  and 5,801 level-inventory texture occurrences with zero errors. It loads 5,913 storage blocks,
  7,603 planes, 615,232 palette entries, and 29,562,280 owned storage bytes. Its independent
  field maxima are Open input/items/logical-output/depth/scratch
  `3,076,944 / 1,460 / 111,014 / 0 / 71,467`
  and Load `3,139,344 / 5,169 / 333,232 / 0 / 65,595`; these maxima are fieldwise and are not
  asserted to co-occur. Internal store defaults are 4 MiB input, 512 KiB logical output, 128 KiB
  scratch, 8,192 items, 4 KiB strings, and depth one. The measured byte/item fields are rounded
  independently to the next binary boundary above the larger Open/Load maximum; depth one is the
  smallest nonzero headroom above measured depth zero. These values are not runtime configuration
  or `--set` keys. The verifier emits no paths, names, hashes, offsets, payloads, per-level rows,
  identities, or bindings.
- E-0043 adds the bounded asynchronous native `AssetService` v0. `OmegaApp` owns it optionally only
  when startup has a `LevelTextureStore`; fixed reusable generation handles and explicit release
  govern a preallocated slot pool, while accepted `JobService` work retains a shared implementation
  through worker return and teardown expires the public service identity. A clean MSVC build produced
  zero warnings and errors, the focused checks and full 18/18 CTest suite passed, and 100 repeated
  lifecycle-test runs passed. Two owned-tree verifier passes produced byte-identical schema-version-1
  reports over 18 levels, 36 sources, and 5,801 texture occurrences with zero errors. Requests,
  `Ready` states, successful `Get` calls, releases, stale-handle rejections, and zero-residual checks
  each total 5,801. Loaded storage totals are 5,913 blocks, 7,603 planes, 615,232 palette entries,
  27,101,352 plane bytes, 2,460,928 palette bytes, and 29,562,280 owned bytes; maximum active slots,
  in-flight requests, and resident logical bytes are `1 / 1 / 333,232`. The verifier uses one worker,
  one pending job, and service limits of one slot, one in-flight request, and 524,288 resident logical
  bytes. Runtime defaults are 64 slots, 64 in-flight requests, and 64 MiB resident logical output,
  with a hard 8,192-slot maximum. These bounds are synthetic project policy, not retail limits or
  user settings. The service performs no VUM-name or material lookup, alias resolution, binding,
  display-pixel expansion, GPU upload, placement, visibility, or rendering. Its fixed aggregate
  report exposes no paths, names, hashes, offsets, payloads, per-level rows, identities, bindings,
  messages, or exception text; the unchanged E-0038 store verifier was revalidated separately.
- E-0044 added the SDL-free bounded `RenderTexturePool` foundation for project-owned tightly packed
  RGBA8 images. It preallocates a fixed slot pool, checks the exact `width * height * 4` extent with
  overflow protection, and separates backend work into transactional `Reserve`, `Publish`, and
  `Rollback` phases. Unique nonzero process-local pool identities plus slot and 64-bit generation
  fields reject default, foreign, stale, and released handles; explicit release refunds logical
  resident bytes, and a maximum generation retires rather than wraps. Defaults are 64 slots and
  64 MiB of logical RGBA8 bytes with a hard 8,192-slot maximum. At E-0044, `RenderFramePacket`
  carried one default-invalid fixed-width diagnostic handle while remaining
  trivially copyable and standard layout. A clean MSVC build produced zero warnings and errors, the
  focused test passed, 100 repeated focused runs passed, and the full 19/19 CTest suite passed. This
  was renderer-neutral metadata and lifecycle policy only: at that milestone it created no GPU
  resource, and neither `SdlGpuHost` nor `OmegaApp` consumed the handle. The existing one-off
  debug-image upload remained unchanged; no GPU upload, blit, residency, `AssetService`, TDX, VUM,
  material, binding, or display semantic was connected.
- E-0045 then superseded E-0044's host non-consumption and unchanged one-off-upload state. At that
  milestone, `SdlGpuHost` owned a fixed parallel SDL texture table and ran project-owned RGBA8
  uploads as RAII transactions: pool `Reserve`, backend create/copy/submit, then `Publish`, with
  rollback and backend cleanup on failure. Explicit release waited for GPU idle.
  `RenderFramePacket` carried the generation handle that selected a blit, while its default-invalid
  handle selected a clear; aggregate host snapshots exposed no resource or pool identities.
  `OmegaApp` uploaded its existing project-generated diagnostic image, retained only the handle, and
  explicitly released it before host fallback teardown. Post-swapchain command buffers submitted on
  unwind instead of attempting the illegal cancel path. Host defaults remain 64 slots and 64 MiB of
  logical RGBA8 with the hard 8,192-slot maximum.
  A clean MSVC build completed with zero warnings and errors, and the default full 19/19 CTest suite
  passed. One initial plus 20 repeated public zero-file GPU smokes all passed on `direct3d12` (21
  total), and the separate public two-frame `openomega` smoke passed. Each GPU smoke uses one slot
  and a 256-byte budget, submits one clear-only frame, uploads and blits an opaque 8x8/256-byte
  image, releases it, rejects its stale handle before GPU access, then uploads and blits a
  4x8/128-byte image in the same slot with a new generation. The final checked-idle snapshot has
  two uploads, 384 cumulative logical bytes, two releases, two blits, one clear-only submission, one
  rejection, zero unavailable-swapchain submissions, and one free slot with no reserved, resident,
  retired, or charged bytes. This proves command submission and checked idle, not framebuffer pixel
  identity or readback. It establishes no `TextureStorageIR`/`AssetService` bridge, TDX plane or
  palette consumption, channel/alpha/nibble/palette/swizzle/mip/display expansion,
  VUM/material/alias/binding, scene placement/visibility, retail rendering, gameplay, measured
  GPU-byte accounting, streaming/eviction, asynchronous upload, or fence design.
- E-0046 supersedes E-0045's historical single-handle frame boundary. `RenderFramePacket` now owns a
  fixed `RenderDrawList` with at most 16 commands and a normalized Q16 target extent of 65,536. Each
  command contains a generation handle and normalized destination rectangle; construction rejects
  overflow, default handles, and zero, inverted, or out-of-range rectangles while preserving source
  order and duplicates. The draw list and packet remain trivially copyable and standard layout, but
  commands neither own nor pin texture generations. A future asynchronous queue therefore requires
  an explicit residency-pinning design.
  `SdlGpuHost` resolves every active handle and backend slot into fixed storage before acquiring a
  GPU command buffer. The first invalid, stale, foreign, released, or inconsistent handle rejects
  the entire list before any prefix can reach the GPU. An empty list retains clear-only rendering.
  For a nonempty list and available swapchain, the host first performs one full-target clear and
  then issues every full-source opaque nearest-filtered aspect-contained blit in source order with
  `LOAD` target semantics; later overlapping commands overwrite earlier ones. Aggregate snapshots
  add the successfully submitted draw total without exposing resource identities.
  A clean MSVC build completed with zero warnings and errors. The focused portable test passed once
  and through 100 additional repetitions; the default suite passed 20/20. One initial plus 20
  repeated direct GPU smokes all passed on `direct3d12`. Each smoke used capacity two with a
  384-byte logical budget and ended at exactly three uploads, 640 cumulative logical bytes, three
  releases, two successful blit frames, four successful blit draws, one clear-only submission, one
  rejected stale list, zero unavailable-swapchain submissions, and zero residual residency. The
  opt-in GPU test passed as the 21st CTest; registration was then restored to off and the default
  listing returned to 20 tests. The public two-frame `openomega` smoke also passed with
  deterministic dummy audio.
  This validates bounded command ownership, ordered submission, checked idle, and atomic
  resident-handle prevalidation only. It does not guarantee all-or-none behavior for arbitrary
  backend failures, framebuffer readback, or pixel identity. It establishes no retail draw order,
  target coordinates, filtering, clear color, compositing, placement, visibility, camera, material,
  texture, mesh, or gameplay semantics; no `TextureStorageIR`/`AssetService` bridge or display
  expansion; and no measured GPU-byte accounting, streaming, eviction, asynchronous upload, or
  fence design.
- E-0047 extends each bounded blit command with a half-open normalized Q16 source crop plus explicit
  project-owned `Contain`/`Stretch` fit and `Nearest`/`Linear` filter choices. Draw-list
  construction rejects capacity overflow first, then validates commands in source order with fixed
  handle/source/target/fit/filter priority while retaining the owned, zero-tailed, trivially
  copyable value contract. The pure integer source mapper floors left/top texel edges and ceilings
  right/bottom edges; the overflow-safe planner preserves that mapped crop exactly, maps the target
  with the same half-open rule, and either aspect-contains with deterministic round-half-up
  centering or stretches to the complete mapped destination.
  `SdlGpuHost` preserves three complete-list fail-closed passes: it resolves the complete
  handle/backend-slot set, then maps every source crop and filter before acquiring GPU work. Once a
  nonzero swapchain is available, it plans the complete frame before recording the full-target clear
  and source-order blits, so a later stale handle or planning rejection cannot submit an accepted
  visible prefix. `OmegaApp` keeps its existing
  project-generated diagnostic image on the full-source, full-target, `Contain` plus `Nearest`
  default and retains its separate handle solely for explicit release.
  A clean MSVC build rebuilt seven translation units with zero warnings and errors. The focused
  portable executable passed once and through 100 additional repetitions, and the default suite
  passed 20/20. One initial plus 20 repeated public zero-file GPU smokes all passed on `direct3d12`;
  every run ended with exactly three uploads and 640 cumulative logical bytes, three releases, two
  submitted blit frames containing four successful draws, one clear-only submission, one stale-list
  rejection, zero unavailable-swapchain submissions, and zero residual residency. The opt-in GPU
  configuration passed 21/21 CTests, after which registration was restored to OFF and the default
  listing to 20 tests. A public two-frame D3D12 `openomega` smoke also passed with deterministic
  dummy audio. Publication CI is tracked separately from these local validation claims.
  This proves bounded project-owned crop/fit/filter command validation, planning, and SDL
  submission, not framebuffer pixel identity or readback, arbitrary backend-failure atomicity,
  alpha or blending semantics, or retail draw order, source/target coordinates, filtering,
  clear/composition, placement, visibility, camera, material, texture, mesh, or gameplay meaning.
  It establishes no `TextureStorageIR`/`AssetService` bridge, TDX plane/palette or display
  expansion, measured GPU-byte accounting, streaming/eviction, asynchronous upload/rendering,
  residency pins, or fence design.
- E-0048 adds packet-owned project clear policy through `RenderClearColorRgba8`. The generic value
  defaults to `{0, 0, 0, 0}`, while `kDefaultRenderClearColor` and a default
  `RenderFramePacket::clear_color` are `{4, 5, 10, 255}`. Every unsigned-byte channel combination
  is valid. Before GPU command-buffer acquisition, `SdlGpuHost` maps each channel to SDL as
  `byte / 255.0` and uses that same value for both an empty-list clear and the full-target clear
  preceding source-order blits. This removes the host pulse and draw-list-dependent fixed colors;
  `OmegaApp` explicitly selects the named default.
  The final regenerated MSVC build completed with zero warnings and errors. The focused portable
  executable passed once plus 100 repeated runs, and the default suite passed 20/20. One initial
  plus 20 repeated public zero-file GPU smokes passed on `direct3d12`; every run retained the exact
  three-upload/640-byte, three-release, two-blit-frame/four-draw, one-clear-only, one-stale-rejection,
  zero-unavailable, and zero-residual snapshot. The opt-in configuration passed 21/21 CTests, then
  was restored to OFF with 20 default tests. A public two-frame D3D12 `openomega` smoke passed with
  dummy audio. Publication CI is tracked separately from these local validation claims.
  Frame submission counters, complete-list preflight, planning, submit-on-unwind, and failure
  ordering are unchanged. The clear value is an in-process renderer-neutral policy, not a stable
  ABI, serialized or persistent value, or retail semantic. It establishes no framebuffer pixel
  identity, readback, color-space, alpha, blending, display-expansion, or asset-service/storage
  bridge claim.
- E-0049 adds a private friend-only `SdlGpuHostTestAccess` seam for a synchronous synthetic clear
  readback. An empty packet clears a temporary 2x2 `R8G8B8A8_UNORM` color target through the same
  `RecordClearPass` used by production rendering, downloads its tightly packed 16 bytes, submits
  with a fence, waits, maps, explicitly decodes four owned RGBA8 values, and unmaps/releases every
  SDL resource through guards. The probe changes no production counter or texture residency.
  The public zero-file smoke reads back endpoint colors `{0, 255, 0, 255}` and
  `{255, 0, 255, 0}` exactly from all four pixels. A nonempty draw list is rejected before SDL/GPU
  work with exact error `clear readback requires an empty draw list`; accepted and rejected probes
  both preserve the complete host snapshot.
  A clean incremental MSVC build issued four compile requests with zero warnings or errors. One
  initial plus 20 repeated public zero-file `direct3d12` GPU smokes passed; every run preserved the
  established production totals of three uploads/640 cumulative logical bytes, three releases,
  two blit frames/four draws, one clear-only submission, one stale rejection, zero unavailable
  submissions, and zero residual residency. Default CTest passed 20/20. The opt-in configuration
  passed 21/21, was restored to OFF, and listed 20 default tests. A public two-frame D3D12
  `openomega` smoke passed with dummy audio. Publication CI is tracked separately from these local
  validation claims.
  E-0049 confirms only the two synthetic endpoint values in a temporary offscreen target on the
  observed D3D12 path. It establishes no stable public readback API or exposed SDL handle; no
  swapchain, on-screen, presentation, sRGB/HDR, color-space, or intermediate-value rounding
  guarantee and no guarantee for untested values; no alpha interpretation, blending, or composition
  semantics beyond the exact tested 0/255 alpha bytes; no blit/filter or other backend pixel
  guarantee; no arbitrary backend-
  failure atomicity or production asynchronous queue/pin/fence contract; and no stable ABI,
  serialization, persistence, wire/plugin, measured GPU-memory, streaming/eviction,
  display-expansion, asset-binding, retail-rendering, or gameplay semantic.
- E-0050 adds a private friend-only synchronous blit-readback seam over a fixed synthetic 4x4
  `R8G8B8A8_UNORM` target without widening the public renderer contract. An empty draw list fails
  before SDL/resource work with exact error `blit readback requires a nonempty draw list`. For a
  nonempty list, the seam resolves every generation/backend slot, maps every source crop and
  filter, and plans every 4x4 destination before allocating the temporary target, 64-byte download
  buffer, or command buffer. Production rendering and the probe now share filter mapping and the
  exact source-order `LOAD` blit recorder, while both retain the shared clear-pass recorder.
  The public zero-file smoke uploads an opaque endpoint 2x2 source laid out `R G / B W`, clears to
  opaque black, applies one top-row Contain+Nearest blit and one later bottom-left
  Stretch+Nearest overwrite, then reads back exactly the sixteen row-major RGBA8 pixels
  `KKKK/RRBG/RRBG/KKKK`. Accepted and rejected probes preserve the complete host snapshot, and
  releasing the probe restores empty portable/backend residency before the existing A/B/C flow.
  The corrected MSVC build completed with zero warnings or errors. Default CTest passed 20/20.
  One initial plus 20 repeated public zero-file GPU smokes passed on `direct3d12`; every run ended
  with exactly four uploads/656 cumulative logical bytes, four releases, two production blit
  frames/four draws, one clear-only submission, one stale rejection, zero unavailable submissions,
  and zero residual residency. The opt-in configuration passed 21/21, was restored to OFF, and
  listed 20 default tests. A public two-frame D3D12 `openomega` smoke passed with dummy audio.
  Publication CI is tracked separately from these local validation claims.
  This endpoint-only fixture confirms the two tested crops/plans, command order, load preservation,
  and sixteen exact opaque RGBA8 pixels on the observed D3D12 path. It is not a public readback API
  and establishes no general Nearest, Linear, crop, aspect, rounding, sample-center, edge, border,
  Contain, Stretch, flip, cycle, mip, layer, alpha interpretation, blending, sRGB/HDR, color-space,
  presentation, swapchain, asynchronous lifetime, cross-backend, retail-rendering, asset-binding,
  or gameplay guarantee.
- E-0051 changes no production/runtime code or public interface. The added smoke fixture uses the
  existing private offscreen probe with two simultaneously resident synthetic sources. A's first
  texel from its 8x8 source is stretched over the fixed 4x4 target, then B's first texel from its
  4x8 source overwrites the center 2x2. Exact colors A=`{32, 192, 224, 255}` and
  B=`{224, 80, 32, 255}` produce the exact row-major fixed-4x4 grid
  `AAAA/ABBA/ABBA/AAAA` with source-order `LOAD` blits on the observed `direct3d12` path. The
  complete host snapshot remains unchanged, and the production packet is reset before the existing
  A/B submission flow. This closes only E-0050's same-handle fixture gap by confirming that the two
  tested commands select their respective live generation/backend texture.
  The one-file MSVC build completed with zero warnings or errors. Default CTest passed 20/20. One
  initial plus 20 repeated `direct3d12` GPU smokes passed with unchanged exact totals of four
  uploads/656 cumulative logical bytes, four releases, two production blit frames/four draws, one
  clear-only submission, one stale rejection, zero unavailable submissions, and zero residual
  residency. The opt-in configuration passed 21/21, was restored to OFF, and listed 20 default
  tests. A public two-frame D3D12 `openomega` smoke passed with dummy audio. Publication CI is
  tracked separately.
  This proves only the two tested handles, first-texel crops, colors, commands, and fixed 4x4
  result. It establishes no arbitrary multi-texture, slot/generation, crop, target, Stretch,
  Nearest, interpolation, edge, aspect, rounding, Linear, Contain, alpha, blending, color-space,
  swapchain, asynchronous-lifetime, cross-backend, asset-binding, retail-rendering, or gameplay
  guarantee.
- E-0052 adds a bounded, move-only `InputTraceRecorder` and immutable `InputTrace` for in-process
  post-binding logical snapshot capture. `Create` accepts a synthetic capacity of 1 through 65,536
  frames whose contiguous `uint64_t` range cannot overflow and one nonempty, strictly ascending,
  unique schema of at most 64 logical actions. It validates configuration before schema before
  allocation and pre-sizes private 32-byte records. At the hard maximum, 65,536 record elements
  plus the fixed 64-slot `uint32_t` schema backing contain exactly 2,097,408 bytes of element
  payload. This does not measure excess vector capacity, allocator/object overhead, or process RSS.
  Allocation-free `Append` observes a const caller snapshot,
  captures held/pressed/released masks plus accepted/rejected event counts, and fails atomically in
  fixed recorder-state, capacity, frame-discontinuity, then schema-mismatch priority.
  Allocation-free expected `Finish` accepts an open empty recorder and leaves the source inert.
  `FrameAt` and `ActionAt` return owned values; only `actions()` borrows storage. Recorder use is
  game-thread exclusive, while a published immutable trace supports reentrant const reads on any
  thread when no read races its move or destruction.
  The final MSVC build completed with zero warnings or errors. The focused public zero-file test
  passed once plus 100 repeated runs, and default CTest passed 21/21. The opt-in Direct3D12
  configuration passed 22/22, after which registration was restored to `OFF` and the default list
  returned to 21 tests. Publication CI remains separate.
  This establishes only bounded logical action/schema/counter capture and owned query behavior.
  It provides no input injection, playback, scheduler timing or pacing, quit/run-control,
  simulation or gameplay state, replay execution, host event/device capture, serialization,
  file/wire/stable ABI contract, concurrent recorder use, or retail limit or timing semantics.
- E-0053 adds a bounded, move-only `SchedulerElapsedTraceRecorder` and immutable
  `SchedulerElapsedTrace` for exact caller-supplied scheduler elapsed values. `Create` validates a
  capacity of 1 through the synthetic hard maximum of 65,536 and a nonoverflowing contiguous
  `uint64_t` frame range before allocation, then pre-sizes one private `int64_t` record per slot.
  At the hard maximum, those record elements contain exactly 524,288 bytes (512 KiB) of element
  payload. This excludes excess vector capacity, allocator/object overhead, and process RSS.
  Allocation-free atomic `Append` preserves every signed nanosecond value and fails in fixed
  recorder-state, capacity, then frame-discontinuity priority. Allocation-free expected `Finish`
  accepts an open empty recorder and leaves the source inert. `FrameAt` returns an owned value.
  Recorder use is game-thread exclusive, while a published immutable trace supports reentrant
  const reads on any thread when no read races its move or destruction. A paired
  `FrameScheduler` test proves direct and trace-retrieved elapsed values produce identical plans,
  accumulator state, planned-step totals, and dropped-time totals for the tested configuration.
  The final MSVC build of the signed-nanosecond implementation completed with zero warnings or
  errors. The focused `omega_scheduler_elapsed_trace_tests` executable passed once plus 100/100
  repeated runs, and default CTest passed 22/22. The opt-in Direct3D12 configuration passed 23/23,
  after which registration was restored to `OFF` and the default list returned to 22 tests. The
  static native dependency gate passed 133 files, and all 204 tooling tests passed. Publication CI
  remains separate.
  This establishes no clock source or timestamp accuracy, `FramePlan` capture or checkpoint
  restoration, input alignment beyond caller indices, quit/run-control, simulation/gameplay,
  injection/replay/app wiring, CLI, persistence, file/wire/stable ABI, retail tick rate, or
  cross-configuration determinism.
- E-0054 adds the SDL-free `omega_runtime` `RunCaptureSession` coordinator. It pairs one
  `InputTrace` with one `SchedulerElapsedTrace` under a shared capacity of 1 through 65,536 and a
  contiguous leaf range that may end exactly at `UINT64_MAX`. `Create` allocates input backing
  before elapsed backing and publishes a session only after both succeed. The exclusive
  game-thread state machine accepts input followed by either elapsed or terminal input; elapsed
  uses the internally retained pending input index, while a terminal owns that index plus
  independent host-quit and logical-quit flags and requires at least one true reason.
  Errors retain an explicit operation stage, fixed session category, and exact optional leaf code.
  Phase checks run first, and every failure before a successful transition is atomic. An open empty
  session may finish; a pending unpaired input rejects finish without consumption. Once valid leaf
  finalization begins, the session is consumed even if a leaf fails. Session and immutable pair are
  move-only with nothrow moves and inert sources. Pair trace accessors borrow references, while the
  optional terminal query returns an owned value. Published pair reads are reentrant on any thread
  when no read races pair move or destruction.
  At the hard maximum, the paired input records, fixed action-schema backing, and elapsed records
  contain exactly 2,621,696 bytes of element payload. This excludes excess vector capacity,
  allocator/object overhead, and process RSS. The final MSVC build completed with zero warnings or
  errors. The focused `omega_run_capture_session_tests` executable passed once plus 100/100
  repeated runs; default CTest passed 23/23. The opt-in Direct3D12 configuration passed 24/24, was
  restored to `OFF`, and left 23 tests in the default list. The static native dependency gate
  passed 136 files, and all 204 tooling tests passed. Publication CI remains separate.
  This is capture coordination only. It adds no `OmegaApp` wiring, clock measurement,
  scheduler/`RunResult`/checkpoint capture, host quit detection beyond caller-supplied flags, CLI,
  simulation/render/audio work, persistence/file/wire/stable ABI, injection/playback/replay,
  external-failure rollback, concurrent session use, tracker-wide exhaustion guarantee, or retail
  limit, timing, or determinism claim. The separately published `InputTracker::next_frame_index()`
  accessor is future app-integration support, not coordinator behavior; E-0055 must preflight a
  planned capture of `N` frames with `N`, not `N - 1`, before tracker-index wrap.
- E-0055 adds owned scheduler snapshots and finite captured app runs.
  `FrameScheduler::Snapshot` copies configuration, accumulated remainder, and lifetime
  planned-step and dropped-time totals; it adds no restore or delta API. Planning rejects negative
  limits before limits above 65,536. A zero-frame plan creates capacity-one empty traces at any
  next index. Positive `N` requires `N <= UINT64_MAX - next_frame_index`, intentionally using
  `N` so the following tracker index remains representable.
  Move-only `RunCaptureOutcome` owns the requested limit, partial `RunResult`, completion,
  before/after scheduler states, optional failure text, and an optional trace pair.
  `OmegaApp::Run` and `RunWithCapture` share one loop, preserving ordinary `Run`. Capture
  preallocates before logging, clock sampling, or mutation. Zero capture performs no event, clock,
  scheduler mutation, simulation, render, audio, job, or log work. Each active frame captures
  `EndFrame` input first, retains both independent quit flags on terminal input, or captures the
  exact raw elapsed value before the same `BeginFrame`. Only planning or session creation returns
  outer `unexpected`. Loop operational and capture failures publish nontransactional partial
  outcomes and best-effort traces. The CLI and `main` remain unchanged and use ordinary `Run`.
  The clean MSVC build completed with zero warnings or errors. `omega_core_tests` passed.
  `omega_run_capture_tests` passed once plus 100/100 repeated runs; default CTest passed 24/24.
  With Direct3D12 and dummy audio, `omega_app_capture_smoke` passed once plus 20/20 repeated
  runs. Its unowned-draw fixture forced the real render-error path, which retained one paired
  input/elapsed sample, zero rendered frames, owned failure text, and the exact scheduler boundary;
  the next capture then resumed successfully. The public zero-file `openomega.exe --frames=2` path
  also succeeded with two rendered and input frames and equal planned and executed steps. GPU
  CTest passed 26/26. Registration was restored to `OFF` with 24 default tests. The dependency gate
  passed 140 files, all 204 tooling tests passed, and Python compile-all passed. Publication CI
  remains separate.
  This adds no capture CLI, replay, input/playback injection, restore, persistence, serialization,
  stable ABI, simulation checkpoint, RNG state, fake services, rollback, ordinary `Run` tracker
  exhaustion guarantee, or retail timing or determinism claim.
- E-0056 adds an explicit finite-capture command without changing ordinary finite runs.
  The exact once-only `--capture-run` flag requires an explicit `--frames=N` in the synthetic
  range 1 through 65,536. Without the flag, existing `--frames=0` and values above 65,536 retain
  their ordinary parser behavior; capture cannot compose with `--probe-only`, and help remains
  standalone. Only the explicit flag routes `main` through `RunWithCapture`; all other runs still
  call ordinary `Run`.
  A captured command prints the ordinary `RunResult` counters, aggregate trace-pair presence and
  frame counts, optional terminal metadata, and selected absolute before/after scheduler counters
  derived from snapshots. These counters are not complete snapshots or deltas. The tested
  `IsCompleteRunCaptureOutcome` helper fails closed unless a positive request ends at the frame
  limit with no failure, quit, or terminal; exact run/input/trace counts and capacities; matching
  trace origins; and equal planned and executed steps. A portable process contract verifies exact
  zero-frame and invalid-capture exit/output behavior without entering the host loop. The opt-in
  GPU test uses an exit-code-safe CMake wrapper before checking the two-frame run, aggregate trace,
  and scheduler summaries.
  The clean incremental MSVC build completed with zero warnings or errors. `omega_core_tests`
  passed. `omega_run_capture_tests` passed once plus 100/100 repeated runs; default CTest passed
  25/25. With Direct3D12 and dummy audio, GPU CTest passed 28/28, including the capture CLI smoke.
  Registration was restored to `OFF` with 25 default tests. The dependency gate passed 140 files,
  all 204 tooling tests passed, and Python compile-all passed. Publication CI remains separate.
  This adds no capture files, persistence, serialization, wire format, stable ABI, per-frame
  printing, replay, injection/playback, restore or delta interpretation, checkpoint, RNG state,
  rollback, interactive or zero-frame capture command, probe composition, ordinary-above-65,536
  execution claim, or retail timing or determinism semantics.
- E-0057 adds the SDL-free `omega_runtime` `RunCaptureReplaySession`. The concrete move-only
  session owns one moved `RunCaptureTracePair` and advances one exclusive game-thread cursor.
  `Create` validates both leaf traces, shared capacity and origin, normal-versus-terminal frame
  counts, and terminal reason/index before moving; rejection leaves the caller's pair unchanged.
  Every successful `Next` publishes a `RunCaptureReplayFrame` that reconstructs the exact recorded
  logical state as an owned immutable `InputSnapshot`, including its frame index, schema,
  held/pressed/released masks, and accepted/rejected event counts. A normal frame owns the exact
  signed elapsed value; a terminal frame owns the independent host-quit and logical-quit flags
  instead, so a frame never exposes both.
  `InputSnapshot` reconstruction allocation or defensive trace-read failure leaves the cursor
  unchanged. Exhausted and moved-from sessions report distinct complete and invalid-state errors.
  `RunCaptureOutcome::TakeTracePair` is rvalue-only and transfers its optional trace pair while
  normalizing the whole outcome to the same inert state used after move construction, including
  when no pair exists.
  This is bounded in-process replay data access only. It adds no SDL, `OmegaApp`, or CLI replay
  route; input injection or `InputTracker` mutation; synthetic physical events; scheduler creation,
  feeding, pacing, clocking, or state restoration; simulation checkpointing, RNG restoration, or
  world mutation; render, audio, or job work; persistence, serialization, file/wire/stable ABI, or
  cross-process contract; seek, rewind, looping, rollback, or retail timing or determinism claim.
- E-0058 adds the portable app-layer `RunReplaySession`. The move-only session takes one validated
  capture pair only after it has created a fresh scheduler from caller-supplied synthetic timing
  configuration and a fresh empty simulation world with the same fixed step. Each successfully
  published normal replay frame feeds its exact signed elapsed value to that scheduler and executes
  every planned simulation step. Separate session observers return owned scheduler and simulation
  snapshots. A terminal
  frame completes without changing either subsystem. A lower replay read or reconstruction failure
  leaves the app session,
  scheduler, and world unchanged so the same frame can be retried. The defensive representation
  branch is unreachable under the current 65,536-frame, 64-step-per-frame, and one-second-step
  bounds. If those bounds expand and it is reached, consumed replay/scheduler work and earlier world
  steps make it a permanent failed state.
  This is replay into a fresh synthetic timeline, not restoration of the captured app. Reconstructed
  input is observable but is not injected into the world. There is no existing-`OmegaApp` replay,
  host-event synthesis, captured scheduler/world checkpoint, entity or RNG restoration, pacing or
  clock ownership, rendering, audio, jobs, persistence, file/wire/stable ABI, cross-process
  compatibility, seek, rewind, loop, rollback, or retail timing or determinism claim.
- E-0059 adds the exact once-only `--replay-capture` launch flag for one-process finite capture and
  immediate fresh replay. It is valid only with `--capture-run` and its explicit `--frames=N` range
  of 1 through 65,536; token order does not change those dependencies. `main` first performs the
  existing capture and accepts replay only when the capture is complete and consistent, has no
  terminal frame, and began from a scheduler whose counters and remainder were all zero. Only after
  those checks does rvalue extraction transfer the trace pair and normalize the capture outcome.
  Replay then creates a fresh scheduler from the captured starting configuration and a fresh empty
  world, consumes every elapsed frame on the main thread, and compares replay aggregates, the final
  scheduler state, and the fresh world's final clock and empty-entity state with capture results.
  Any mismatch or replay error fails the process immediately. Success alone prints one fixed
  `OpenOmega fresh replay:` aggregate line with `replayed_frames`, planned and completed simulation
  steps, clamped and dropped frame counts, and `completion=complete`. Ordinary runs and
  capture-only commands retain their prior behavior.
  This does not replay into the existing `OmegaApp`; after capture, the main-thread replay
  orchestration makes no calls into or reads from it. The host remains alive, so its audio callback
  may continue independently. `RunReplaySession` itself performs no pacing, clock sampling,
  rendering, audio, or job work. The command adds no terminal or incomplete CLI replay, input
  injection or world input use, gameplay reconstruction, captured scheduler/world state, entity or
  RNG restoration, persistence, file/wire/stable ABI, cross-process contract, seek, rewind, loop,
  rollback, or retail timing or determinism claim.
- E-0060 adds the first input-driven native simulation component without assigning retail meaning.
  For captures with the new movement schema, it supersedes E-0059's empty-world and no-world-input
  clauses; older replay callers remain neutral by default.
  `SimulationWorld` now directly owns a bounded, preallocated `Position3` store and can create,
  query, clean up, and translate one positioned entity as part of an allocation-free fixed-step
  transaction. Clock exhaustion, stale identity, absent position, and signed coordinate overflow
  are typed failures that leave the complete attempted step unchanged; the original no-input step
  remains the neutral wrapper. A separate SDL-free `omega_gameplay` value maps only the synthetic
  digital domain `-1/0/1` to one signed project unit on the X/Z axes.
  `OmegaApp` owns one synthetic diagnostic actor and binds W/S/A/D plus the arrow keys to held
  movement actions; the gamepad D-pad is an optional alias. The same immutable command is applied to
  each fixed step planned for that
  input frame. `RunReplaySession` can opt into the identical input-to-world path; its default stays
  neutral for old callers, while the finite capture/replay CLI enables it only when all four
  synthetic action identifiers are present. The established `OpenOmega fresh replay:` output line
  remains unchanged. Synthetic tests verify exact movement, release, and terminal outcomes, while
  the real-host smoke compares the captured app and fresh-replay positions; the CLI itself validates
  only fresh replay ownership, aggregates, scheduler state, clock, actor
  count, and position presence, and still never reads the live app after capture.
  This diagnostic actor is not the retail player. Its identifiers, bindings, origin, axes, integer
  unit, step rate, and diagonal policy establish no retail controls, coordinates, movement,
  physics, collision, camera, animation, mission, network, asset, or rendering semantics.
- E-0107 gives that synthetic actor one bounded project-owned presentation only while
  `DiagnosticPlay` is active. A pure `constexpr`/`noexcept` planner maps an owned `Position3` copy
  into a half-open normalized-Q16 marker rectangle: the origin is centered, every marker is
  2,048 by 2,048, each synthetic X/Z unit moves 1,024, X increases right, Z increases up, Y is
  ignored, and X/Z saturate independently to `[-31, 31]` before arithmetic. `OmegaApp` preloads one
  opaque 1x1 RGBA8 `{255,64,224,255}` texture, adding exactly one texture slot and four logical
  resident bytes. After simulation it rebuilds only the fixed CPU base-plus-marker draw list, using
  Stretch plus Nearest; it performs no frame-time upload, texture update, or release. Fresh replay
  derives the same optional Q16 destination from its existing owned diagnostic position without a
  capture-schema change or retained marker state. This overlay, mapping, color, extent, axes, clamp,
  composition, and error policy are native diagnostics, not retail actor/player, coordinate,
  camera, transform, level-placement, collision, visibility, animation, or rendering-equivalence
  evidence. No proprietary or owner input is consumed.
- E-0061 begins with a portable project-owned diagnostic-menu value and deterministic 128x72
  opaque RGBA8 `DEV` card. The pure toggle-edge reducer and integer-only image generator use no
  files, platform APIs, decoded assets, or retail inputs. The complete 36,864-byte output contains
  four project-authored colors and is frozen by FNV-1a-64 `0xdaf00c60d17f05b5`. Action 6 is
  reserved only; this first slice is not bound to a physical control, uploaded to the GPU, rendered,
  serialized, or replayed. It establishes no retail menu art, text, palette, layout, controls,
  navigation, selection, activation, pause behavior, timing, asset provenance, or UI semantics.
- E-0062 wires that project diagnostic value into `OmegaApp`: keyboard F1 and gamepad Start feed
  logical action 6 as a press edge. The toggle is processed after terminal handling and remains
  nonmodal, so a nonterminal toggle frame continues through the existing movement, simulation, and
  rendering path. Startup generates and uploads the 128x72 card once, then retains fixed hidden/base
  and visible/base-plus-menu draw lists. The menu blit uses Q16 source `{0,0,65536,65536}`, exact Q16
  destination `{2048,2048,26624,15872}`, `Stretch`, and `Nearest`; frames select one prebuilt list
  without another menu upload or per-frame draw-list allocation. Capture retains action 6 in the
  ordinary input row, including a terminal row, but terminal input does not mutate menu visibility.
  Fresh replay preserves that input row while owning no menu state, and the established
  `OpenOmega fresh replay:` success line is unchanged. This remains synthetic developer UI and
  establishes no retail menu, control, pause, navigation, rendering, timing, or replay semantics.
  The clean MSVC build completed with zero warnings or errors. The focused portable test passed;
  the real D3D12 host smoke passed directly plus 20/20 repetitions; default CTest passed 29/29;
  the opt-in GPU matrix passed 33/33; and the unchanged capture/replay CLI smoke passed 20/20.
  Runtime-off validation built and ran the exact portable menu target and registered 26 tests.
  Static dependency, tooling, compile-all, and public-tree gates also passed. The exact E-0062
  main tree subsequently passed the Windows x86-64 native, Linux x86-64 native, and public-tree
  safety publication-CI jobs.
- E-0063 makes the project diagnostic menu the explicit native startup state while retaining a
  safe default. Default construction yields `DiagnosticPlay` on `StartDiagnosticPlay`, and
  `OmegaApp` explicitly starts in `MainMenu` on that first row. The constexpr/noexcept reducer
  validates both enums before consuming press edges; invalid state resets directly to the startup
  value. Existing actions 2/3/6 remain W/S plus gamepad D-pad Up/Down and F1 plus gamepad Start.
  Primary has priority, opens row zero from play, enters play from the first menu row, and is inert
  on the two reserved project rows. Previous/next clamp, and simultaneous navigation edges are
  neutral. Capture and terminal handling remain before the reducer, while ordinary menu frames
  remain nonmodal for held locomotion, scheduling, simulation, rendering, and fresh replay. Replay
  reconstructs the ordinary action rows but owns no menu state.
  Startup reuses the one existing 128x72 card upload and retains one hidden/base list plus three
  immutable base/card/amber-marker lists. The card uses source `{0,0,65536,65536}` and target
  `{2048,2048,26624,15872}`. The marker reuses card crop `{18432,9103,59392,14563}` at row targets
  `{3584,7424,4352,9344}`, `{3584,10304,4352,12224}`, and
  `{3584,13184,4352,15104}`, with `Stretch`/`Nearest` throughout. Frames select a prebuilt list
  without another upload, list construction, or per-frame menu allocation. These modes, rows,
  reused controls, priorities, crop, and targets are synthetic and establish no retail menu,
  control, pause, activation, timing, persistence, replay-UI, private-input, or PCSX2-equivalence
  claim. E-0063 local validation used project-generated zero-file fixtures only and no private
  inputs. A clean incremental MSVC build completed with zero warnings and errors. The focused
  portable executable passed directly plus 20/20 repetitions, and the real `direct3d12` host
  smoke passed directly plus 20/20 repetitions. Default CTest passed 29/29 both before and after
  restoring GPU registration; the opt-in GPU configuration passed 33/33. The unchanged
  capture/replay CLI smoke passed 20/20. Runtime-off validation built and ran the exact portable
  menu target directly and through CTest, with 26 tests registered. The native dependency gate
  checked 152 files, all 209 tooling tests passed, Python compile-all passed, and the public-tree
  gate checked 239 indexed text blobs. Publication CI remains separate and is not claimed here.
- E-0064 replaces the card's geometric-only presentation with readable project-authored 3x5 labels:
  `OPEN OMEGA`, `W/S SELECT`, `F1 START`, `ESC QUIT`, `START DIAGNOSTIC`, and the two
  `RESERVED SLOT` rows. The card remains a generated 128x72 opaque RGBA8 image with no external
  font or asset input. Host ownership is unchanged: startup performs one menu-texture upload,
  retains the same hidden list and three base/card/amber-marker lists, and selects a prebuilt list
  per frame without another upload, list construction, or menu allocation.
  The resulting menu state now gates only app-layer simulation. On each nonterminal live frame,
  input capture and menu reduction happen before the gate: a valid `DiagnosticPlay` state supplies
  actual elapsed time to the scheduler, while `MainMenu` or an invalid state supplies zero and
  skips locomotion planning and world steps. Entering the menu freezes that same frame; entering
  diagnostic play resumes that same frame, and the live clock baseline still advances while modal,
  so menu wall time is not replayed as catch-up work. Capture stores the actual measured elapsed
  value rather than the gated zero. Input pumping/capture, rendering, audio, and job-service
  boundaries continue while the menu is modal.
  `RunReplaySessionConfig` can optionally own an initial diagnostic-menu state and applies the same
  reducer-before-gate rule to reconstructed rows. Its default remains no menu state, preserving the
  legacy nonmodal replay contract; the finite capture/replay route explicitly supplies the native
  startup state without changing CLI syntax or output. No action ID or binding, input/trace schema,
  captured checkpoint, serialization, or CLI surface changes, and no menu state is inferred from or
  embedded in a capture. These labels and modal rules remain synthetic project diagnostics, not a
  retail-menu, timing, pause, persistence, or PCSX2-equivalence claim. E-0064 local validation is
  green: the final incremental MSVC build was warning-free; the portable diagnostic and replay
  tests each passed directly plus 20/20 repetitions; the real Direct3D12 host smoke passed directly
  plus 20/20 repetitions; default CTest passed 29/29 before and after the opt-in 33/33 GPU matrix;
  and the capture/replay CLI passed 20/20 repetitions plus one 20-frame run. The exact runtime-off
  portable target and focused CTest passed with 26 tests registered. The native dependency gate
  checked 152 files, all 209 tooling tests passed, Python compile-all passed, and the public-tree
  gate checked 239 indexed text blobs. Publication CI remains separate and is not claimed here.
- E-0065 replaces main-menu row one with `CONTROLS` and adds the synthetic
  `DiagnosticMenuMode::Controls` value without adding an action or retail meaning. The explicit
  startup state remains `MainMenu` / `StartDiagnosticPlay`, the safe default remains
  `DiagnosticPlay` / `StartDiagnosticPlay`, and invalid mode or row bytes reset to startup before
  consuming an edge. Primary still has reducer priority: main row zero enters diagnostic play,
  main row one enters Controls while retaining row one, main row two is inert, any valid
  DiagnosticPlay row returns to main row zero, and any valid Controls row returns to main row one.
  Previous/next navigation is MainMenu-only, clamped, press-edge-only, and neutral when simultaneous.
  Only a fully valid DiagnosticPlay state permits simulation; MainMenu, Controls, and invalid states
  remain modal while input/capture, rendering, audio, and jobs continue.
  The main card now labels row one `CONTROLS`. A second independent project-generated 128x72 opaque
  RGBA8 card labels `CONTROLS`, `W FORWARD`, `S REVERSE`, `A LEFT`, `D RIGHT`, `F1 RETURN`, and
  `ESC QUIT`. The main image has exact background/cyan/slate/amber populations
  3,739/1,491/3,506/480 and FNV-1a-64 `0x5303b94979cd74d6`; the Controls image has
  2,104/1,326/5,373/413 and FNV-1a-64 `0xa68873cc7444bdf6`. Startup uploads both generated cards once.
  In the zero-file host that is exactly two uploads and 73,728 resident logical bytes. Retained
  Controls presentation contains the optional base diagnostic draw followed by one controls-card
  draw; main-menu presentation retains the optional base draw, main card, and row marker. Teardown
  clears every retained list before releasing controls, menu, and optional base textures.
  Live and opt-in replay use the established reducer-before-gate rule, discard modal elapsed from
  scheduling without later catch-up, and resolve terminal input before reduction. Null replay menu
  ownership remains legacy nonmodal. Action IDs and bindings, input/elapsed trace schemas, captures,
  checkpoints, serialization, CLI syntax/output, and file/wire/stable-ABI surfaces do not change.
  E-0065 validation used only public, project-generated zero-file fixtures. The final MSVC build was
  clean; diagnostic and replay tests passed directly plus 20/20 repetitions; the Direct3D12 host
  passed directly plus 20/20; default CTest passed 29/29, the opt-in GPU matrix passed 33/33, and
  restored-default CTest passed 29/29. A 20-frame capture/replay passed, as did 20/20 short
  repetitions. Runtime-off focused direct and CTest runs passed with 26 registrations. The native
  dependency gate checked 152 files, all 209 tooling tests passed, Python compile-all passed, and the
  public-tree gate checked 239 indexed text blobs. During validation, three test-only
  `SimulationState` C2676 comparisons were corrected to the fieldwise helper; a direct configure
  outside `vcvars` also contaminated generated cache settings, which were restored to the exact MSVC
  linker, archiver, and flags without a source change. No private data, disc image, retail executable,
  emulator, or PCSX2 input was used. This establishes only synthetic developer UI and no retail menu,
  controls, pause, timing, persistence, private-input, or emulator-equivalence semantics.
  Publication CI remains separate and is not claimed for E-0065.
- E-0066 adds a portable, metadata-only canonical `TextureStorageIR` to
  `runtime::DebugImage` adapter. It is not connected to `OmegaApp`, `AssetService`, GPU upload, or
  the retail TDX decoder. `BuildTextureStorageTopologyDebugImage` borrows one already-canonical
  value, validates it in a strict typed order under independent block, plane, palette-entry, and
  output-byte budgets, and returns a fully owned image without I/O or shared state. Source-order
  blocks occupy 32x32 tiles with at most eight columns. Opaque background and slate borders frame
  cyan 2x2 sample/transfer-enum masks (`0x1`, `0x9`, `0x7`, `0xf`); palette presence adds one amber
  plus. After validation, only block order, sample and transfer encodings, and palette presence can
  affect pixels. Plane and palette bytes and dimensions are validation inputs, not display data.
  The canonical three-block fixture is 96x32 RGBA8 (12,288 bytes), has exact
  background/slate/cyan/amber populations 2,667/372/23/10, and FNV-1a-64
  `0xb56c8db088c5a9fe`. The adapter is reentrant and safe for any worker thread. It establishes no
  retail pixel expansion, display/channel/alpha/nibble/swizzle/palette interpretation, material or
  geometry binding, or app/render behavior. MSVC configure, focused-target, and full builds were
  clean with zero warnings or errors; the executable passed directly and 20/20 repetitions, focused
  CTest passed, and default CTest passed 30/30. Runtime-off direct and focused checks passed with 27
  tests registered. The dependency gate checked 155 native files, all 209 tooling tests passed, and
  Python compile-all passed, and the final staged-tree public gate checked 242 indexed text blobs.
  Validation used no private data, D-drive content, disc
  image, retail executable, emulator, or PCSX2 input. Publication CI remains separate and unclaimed.
- E-0067 verifies the first complete public-fixture composition of the existing texture asset path
  with that topology adapter, without adding a production binding. Two generated direct-24 TDX
  members contain deliberately different payload bytes and load asynchronously through
  `LevelTextureStore`, `JobService`, and `AssetService`. Their ready immutable
  `TextureAssetView::storage` values each produce one owned 32x32 RGBA8 topology tile with 4,096
  bytes, and the two images are pixel-identical because payload bytes are not topology inputs. Both
  asset handles then release to an exactly empty two-slot snapshot while both independent images
  remain intact. This is a test-only ownership and composition proof: `OmegaApp`, startup selection,
  GPU upload, material/geometry association, and retail display expansion remain unwired.
- E-0068 turns main-menu row two into a zero-file, project-owned `ASSET TOPOLOGY` screen without
  selecting a retail asset. The additive `AssetTopology` mode is byte 3 and row-two
  `ShowAssetTopology` remains byte 2. Primary enters the screen from row two and returns every valid
  topology state to that row; the screen is modal under the existing live/replay reducer-before-gate
  rule. The updated 128x72 main card has exact background/cyan/slate/amber populations
  3,739/1,481/3,516/480 and FNV-1a-64 `0xf37b700c33071a92`.
  `BuildProjectDiagnosticAssetTopologyImage` creates the exact public E-0066 three-block fixture
  before SDL/platform creation and returns its owned 96x32, 12,288-byte image with populations
  2,667/372/23/10 and FNV-1a-64 `0xb56c8db088c5a9fe`; allocation failures stay in the existing typed
  topology-error contract. After host creation, startup uploads the topology texture after the menu
  and controls cards and builds one immutable optional-base-plus-topology `Contain`/`Nearest` list.
  Zero-file startup therefore owns three uploads, three resident textures, and 86,016 logical bytes.
  Teardown clears every retained list, then releases topology, controls, menu, and optional spatial
  textures in reverse upload order. Real Direct3D12 testing freezes sixteen topology texels and the
  complete entry/hold/release/terminal/return modal lifecycle without adding a per-frame upload or
  allocation. No action/binding, capture/replay schema, CLI, asset-service, decoder, material,
  geometry, or retail display behavior changes.
- E-0069 adds conventional confirmation aliases without changing the logical input contract.
  Keyboard Return, keypad Enter, and gamepad South join F1 and gamepad Start as five physical
  bindings for the existing action 6. The complete host table is 15 physical bindings over the
  unchanged six sorted logical actions. Existing first-down/last-up tracking means a second alias
  cannot repeat a press and one release cannot end action 6 while another alias remains held.
  Capture still records one action-6 row without physical provenance, and replay consumes that row
  through the unchanged reducer, terminal-before-reducer order, and modal elapsed gate.
  The generated legends now read `F1/ENTER` and `F1/ENTER RETURN`. The main card's exact
  background/cyan/slate/amber populations are 3,725/1,495/3,516/480 with FNV-1a-64
  `0x0a1373c69c8bcce2`; the Controls card's are 2,104/1,381/5,318/413 with FNV-1a-64
  `0xd57a5b0500696505`. Dimensions, full opacity, three uploads, three resident textures, 86,016
  logical bytes, draw lists, topology raster, failure ordering, and teardown remain unchanged.
  These project-owned aliases and labels establish no retail input map or platform-specific
  button-name claim.
- E-0070 adds conventional vertical-navigation aliases without expanding the logical input
  contract. Keyboard Up joins W and gamepad D-pad Up on existing action 2; keyboard Down joins S
  and gamepad D-pad Down on action 3. The complete host table is now 17 physical bindings over the
  unchanged six sorted actions. First-down/last-up aggregation prevents a second same-action alias
  from repeating navigation and prevents a non-final release from ending the action. Because the
  menu intentionally reuses the synthetic forward/reverse actions, Up/Down also drive that
  project-owned diagnostic locomotion while in DiagnosticPlay; this is not a retail control claim.
  The legends become `W/S/UP/DOWN`, `W/UP FORWARD`, and `S/DOWN REVERSE`. The fully opaque main
  card's background/cyan/slate/amber populations are 3,702/1,518/3,516/480 with FNV-1a-64
  `0x9a4662f8f943521d`; the Controls card's are 2,104/1,452/5,247/413 with FNV-1a-64
  `0xcfa7cc57696aae0a`. Dimensions, topology pixels, action rows, reducer rules, modal timing, draw
  lists, three-texture 86,016-byte residency, failure ordering, and reverse teardown remain
  unchanged. These aliases establish no retail menu, key-repeat, wrapping, or controller behavior.
- E-0071 makes the existing content-startup ownership boundary explicit before the native host
  allocates or touches platform state. A nonallocating `noexcept` classifier accepts only an empty
  state (`NoContent`), `GameDataService` alone (`DataMounted`), or the complete five-owner level
  state (`LevelContent`); every partial or mixed shape is rejected. `OmegaApp::Create` performs this
  check before owning config/content, creating logs/jobs/scheduler/input/simulation/topology, or
  touching SDL, audio, or GPU state, and reports the fixed
  `content startup state: inconsistent-ownership` error on rejection.
  The generated main card now labels the copied startup stage as `CONTENT NONE`, `CONTENT DATA`, or
  `CONTENT LEVEL`. The three opaque images have exact background/cyan/slate/amber populations
  3,593/1,627/3,516/480, 3,600/1,620/3,516/480, and 3,594/1,626/3,516/480, with FNV-1a-64 values
  `0x8e8e3f7fff4f971a`, `0x517ad52bbf1fbe61`, and `0x08405186aa105db1`. Invalid stage enum values
  render the `NoContent` card. Controls remains `0xcfa7cc57696aae0a`, topology remains
  `0xb56c8db088c5a9fe`, and no action, reducer, capture/replay, file, schema, asset decoding, retail
  presentation, or platform lifecycle contract changes.
- E-0072 adds a borrowed `DescribeContentStartupError` presentation adapter for the existing
  all-or-error startup boundary. It accepts only the four representations that `StartContent`
  publishes: `InvalidOptions` and `DebugImage` with no nested error, `GameData` with only its
  `GameDataError`, and `LevelTextures` with only its `LevelTextureStoreError`. Empty messages,
  unknown outer or nested codes, missing or unexpected nested errors, and both-nested shapes fail
  closed to the fixed process diagnostic
  `content startup [inconsistent-error]: content startup error representation is inconsistent`.
  Valid output is byte-for-byte unchanged and borrows the outer message. The underlying name
  functions retain their stable `unknown` fallback even though the adapter rejects those values. A
  CMake-created empty root now fixes the pre-SDL process contract at
  `content startup [missing-required-file]: game-data root is missing SYSTEM.CNF`. This synthetic
  error-presentation seam adds no retry, picker, fallback, persistence, menu, resource, asset,
  capture/replay, schema, or retail-input behavior. Focused and full MSVC builds were clean;
  `omega_core_tests`, the process contract, and 30/34/30 default/GPU/restored CTest matrices passed.
  Runtime-off checks retained 27 registrations, the dependency gate checked 157 native files, all
  209 tooling tests passed, and Python compile-all passed. Publication remains unclaimed.
- E-0073 gives valid no-level startup states a visible synthetic `DiagnosticPlay` surface without
  changing the level-content path. `BuildProjectDiagnosticNoLevelImage` independently owns one
  opaque 128x72 RGBA8 image and reads no file, platform object, decoded asset, or retail input. It
  draws the existing frame and `OPEN OMEGA` header with `DIAGNOSTIC PLAY`, `NO LEVEL IMAGE`,
  `F1/ENTER MENU`, and `ESC QUIT`; its exact background/cyan/slate/amber populations are
  3,327/1,285/4,124/480 and its FNV-1a-64 is `0x37f823d27a4cb3ce`.
  After the existing ownership classifier succeeds, `OmegaApp` keeps an owner-supplied level debug
  image when present and otherwise builds this placeholder. Both flow through the existing
  diagnostic texture upload, full-source/full-target `Contain`/`Nearest` command, handle, error,
  and teardown path. Zero-file upload order is placeholder, menu, controls, topology: four distinct
  resident textures and exactly 122,880 logical bytes. MainMenu submits base/card/marker, Controls
  and AssetTopology submit base/card, and no-level DiagnosticPlay submits the one base blit. START
  DIAGNOSTIC remains available for `NoContent` and `DataMounted`; reducer, simulation, locomotion,
  elapsed, return, terminal priority, capture, and replay behavior are unchanged. Residency totals
  apply only to no-level startup. This is a project-generated missing-level diagnostic, not a retail
  UI, level selector, framebuffer-identity, private-input, or emulator-equivalence claim. Focused
  and full MSVC builds, direct and repeated diagnostic/replay/GPU checks, 30/34/30
  default/GPU/restored CTest, capture-replay, runtime-off, dependency, tooling, and compile-all
  validation passed. Publication remains unclaimed.
- E-0074 adds an explicit content launch profile to the existing strict runtime configuration.
  `content.data_root` plus optional `content.level_code` are read only from a selected `--config`
  file and its validated `--set` overrides; no neighboring or per-user file is discovered. The
  effective configuration tuple is validated even when direct CLI content options are present.
  A direct `--data-root` plus optional `--level` then wins atomically and never inherits the
  configured level. A configured level without a root, an empty/unrepresentable native root, an
  invalid level, and a defensive malformed direct pair return the stable categories
  `missing-data-root`, `invalid-data-root`, `invalid-level-code`, and `invalid-options` with fixed
  diagnostics that echo no path or invalid level bytes. Valid level codes are 1 to 32 ASCII
  alphanumeric bytes and normalize to uppercase before the unchanged `StartContent` boundary.
  With neither content key and no direct root, startup remains zero-file. `/openomega.cfg` is ignored
  because it can contain a private local path. This slice adds no ambient/default discovery,
  persistence, picker, filesystem probing in the resolver, asset semantics, retail UI, or
  emulator-equivalence claim. Focused and full MSVC builds, `omega_core_tests`, the process
  contract, 30/34/30 default/GPU/restored CTest, runtime-off checks with 27 registrations, the
  157-file dependency gate, all 209 tooling tests, and Python compile-all passed. Publication
  remains unclaimed.
- E-0075 adds an optional per-user default runtime profile without changing the configuration
  grammar or content precedence. After successful argument parsing and the help fast path, main
  captures only the host-family search roots when `--config` is absent. Absolute roots resolve
  lexically to `%LOCALAPPDATA%/OpenOmega/openomega.cfg` on Windows,
  `$HOME/Library/Application Support/OpenOmega/openomega.cfg` on macOS, and
  `$XDG_CONFIG_HOME/openomega/openomega.cfg` or `$HOME/.config/openomega/openomega.cfg` on XDG
  hosts. An explicit `--config` bypasses discovery and inspection. A missing default is silent;
  a regular default is loaded before validated `--set` overrides; and a reported final-entry
  symlink, dangling symlink, directory, or other non-regular type is rejected without following
  it. Configuration failures use fixed explicit/default/`--set` source categories and never
  disclose a source filesystem path, user-controlled key, or raw value. Parser diagnostics retain
  only structural line and budget information; typed runtime diagnostics may name only a
  compile-time-known public setting. Discovery performs no normalization, canonicalization, token
  expansion, write, directory creation, migration, or success-path printing. This slice does not claim
  rejection of symlinked parents, add a picker or startup dialog, choose a default level, or
  inspect private content.
  Serialized local validation passed: focused and full MSVC builds completed cleanly; direct
  `omega_core_tests` and the exact process contract passed; default, opt-in GPU, and restored
  CTest passed 30/30, 34/34, and 30/30; runtime-off direct and focused checks passed with 27
  registrations; the dependency gate checked 160 native files; all 209 tooling tests and Python
  compile-all passed; and the staged public-tree gate checked 247 indexed text blobs. On Windows,
  the non-missing inspection-error oracle was explicitly skipped
  because MSVC maps the available invalid and overlong candidates to not-found. Commit, DCO,
  publication, and exact-main validation remain unclaimed. The later non-reflective diagnostic
  hardening passed scoped diff checks, the 244-file dependency gate, the 411-blob public-tree gate,
  Python compile-all, and all 298 tooling tests. Its C++ build, process contract, and CTest remain
  delegated to the serialized integration lane because local preflight was `CAUTION`.
- E-0076 adds one app-private, stateless startup-failure dialog adapter for the already-fatal
  pre-SDL runtime-configuration, runtime-settings, content-launch-profile, and content-startup
  paths. Main preserves each exact stderr line and exit code, flushes stderr, then best-effort calls
  `SDL_ShowSimpleMessageBox` with title `OpenOmega startup error`, no parent, and a fixed projected
  body. The adapter neither initializes nor quits SDL, retains no borrowed input, and owns no
  global state; suppression reads SDL's cached environment view. Category and detail fields are
  sanitized with bounded local stack storage into an owned 640-byte result, with 48-byte and
  384-byte limits; empty fields use fixed fallbacks and overflow ends in `...`. Only exact
  `OPENOMEGA_DISABLE_STARTUP_DIALOG=1` suppresses presentation, and invalid policy values fail
  closed as suppressed. CMake supplies that suppression to the existing synthetic process and
  capture contracts, while the dedicated unit contract exercises only suppressed presentation and
  verifies that SDL remains uninitialized. Parse/help, the run loop, capture, and replay failures
  remain console-only. A later hardening step extends presentation to app creation, including
  SDL/GPU/audio setup, while preserving its exact stderr diagnostic: the dialog uses fixed code
  `application-startup` and fixed detail `Application components could not be initialized.`, so
  raw backend text never reaches the packaged dialog. The E-0076 baseline validation passed:
  focused and full MSVC builds; the direct dialog unit and exact process contract; CTest 31/35/31;
  runtime-off direct and focused `omega_core_tests` with 27 registrations and no dialog target; the
  163-file dependency gate; all 209 tooling tests; Python compile-all; and the staged public-tree
  gate checked 250 indexed text blobs. Interactive dialog smoke, commit, DCO, publication, and
  exact-main validation remain unclaimed. No private or
  owner files, D-drive content, disc image, executable,
  emulator, or PCSX2 input was used.
- E-0077 adds a stateless startup adapter that selects only canonical
  `LevelTextureStore::HandleAt(0)`, loads it through the existing asynchronous `AssetService`, and
  feeds the borrowed immutable storage to the existing metadata-only topology raster. It requires
  exclusive access to an aggregate-empty asset service, rejects an empty inventory, waits only for
  the accepted asset request, and always attempts `Release` for an accepted handle before returning
  an independently owned image. Fixed identity-free diagnostics retain only the applicable typed
  texture-store, asset-service, or topology-image enum; release failure outranks residual-state
  mismatch, which outranks an earlier Get or image failure. Entry and final snapshots are captured;
  after a successful `Release`, all ten public fields are compared while deliberately ignoring the
  hidden recycled-slot generation.
  `OmegaApp::Create` builds the resulting 32x32 RGBA8 image only for complete `LevelContent` before
  SDL creation; the later GPU upload retains the existing fourth-texture path. Its 2x2 fixture base,
  two 128x72 cards, and topology image total four
  resident textures and 77,840 logical bytes. `NoContent` and `DataMounted` retain the project
  synthetic 96x32 topology and 122,880-byte presentation. Public generated contracts pin the
  32x32 hash, repeat ownership, exact cleanup, bounded and nested failures, canonical-first
  selection, GPU probes, upload order, draw policy, and unchanged fallback branches. Serialized
  local validation passed: focused and full MSVC builds; direct asset and D3D12 app smokes; focused
  asset CTest; default, GPU-opt-in, and restored CTest at 31/35/31; 20/20 repeated D3D12 app smokes;
  runtime-off direct and focused asset checks with 27 registrations; the 165-file dependency gate;
  all 209 tooling tests; and Python compile-all. The staged public-tree gate checked 252 indexed
  text blobs. The merged commit carries its matching DCO sign-off; PR #38 published E-0077 as exact
  `main` commit `2a9182e560a504125a5b8278a7202fcad7220c44`, and exact-main run `29710089254`
  passed all three jobs. No private or owner files, D-drive
  content, disc image, executable, emulator, or PCSX2 input was used. This is not a claim about
  display texels, channel or alpha meaning, palettes, nibble order, swizzles, mip levels, UVs,
  materials, cells, placement, visibility, geometry, retail rendering, gameplay, streaming,
  eviction, GPU pinning, asynchronous upload, or emulator equivalence.
- E-0078 adds `BuildPacked24TransferDebugImage`, a stateless worker-thread diagnostic projection
  for one strict canonical storage shape: nonzero matching texture/plane rectangles, known
  `Packed24` sample and transfer-element enums, exactly one block and one plane, no palette, and an
  exact `width * height * 3` source allocation. Checked source/output arithmetic and independent
  48 MiB/64 MiB synthetic limits fail closed through sixteen fixed typed errors. Each consecutive
  three source byte slots is copied into the first three slots of one owned four-slot output group;
  the fourth slot is the project constant `0xff`. A generated 16x16 fixture maps 768 source bytes
  to 1,024 owned bytes with FNV-1a-64 `0x4abb645f50f5a325` for seed `0x21` and
  `0x36590f25eee3ab25` for seed `0x61`. The standalone runtime-off-capable contract freezes every
  ordinal/name/message, validation priority, independent overflow oracle, exact and one-below
  budgets, slot mapping, hashes, repeat determinism, and ownership after source mutation and
  destruction. Serialized local validation passed: focused/full MSVC builds; the direct unit plus
  100/100 repeated runs; focused CTest; default/GPU/restored CTest at 32/36/32; runtime-off direct
  and focused checks with 28 registrations; the 168-file dependency gate; all 209 tooling tests;
  and Python compile-all. The staged public-tree gate checked 255 indexed text blobs. The merged
  commit carries its matching DCO sign-off; PR #39 published E-0078 as exact `main` commit
  `47378588471d9271a43dfaeb56f3138c01137e1f`, and exact-main run `29710670162` passed all three
  jobs. This slice changes no app,
  GPU, renderer, AssetService, E-0077, or existing test behavior and uses no private or owner files,
  D-drive content, disc image, executable, emulator, or PCSX2 input. It assigns no channel names,
  display-ready meaning, row origin/order, swizzle, color space, alpha semantics, premultiplication,
  block/plane purpose, Packed32/indexed expansion, palette/nibble policy, material/UV/geometry
  binding, gameplay behavior, or emulator equivalence.
- E-0079 freezes the first Windows portable-delivery contract. Only an MSVC x64 `Release` build may
  produce `OpenOmega-0.1.0-windows-x86_64.zip`; that archive must contain exactly one internal
  `OpenOmega-0.1.0-windows-x86_64/` root with `openomega.exe`, `launch-openomega.cmd`,
  `README-WINDOWS.md`, `LICENSE`, `NOTICE`, `TRADEMARKS.md`, `THIRD_PARTY_NOTICES.md`, and
  `LICENSES/SDL3.txt`. The executable uses the static MSVC runtime for this package. The command
  launcher changes to its own directory, forwards every argument to `openomega.exe`, and preserves
  the process exit code; the package contract requires exact
  `OpenOmega native shell: rendered_frames=0` stdout with empty stderr plus an invalid-option
  forwarding check from an unrelated working directory. A sibling `.zip.sha256` sidecar covers the
  exact archive. This is an unsigned preview, not an installer or signed release, and it deliberately
  excludes proprietary inputs or assets, PCSX2, user profiles, PDBs, and developer tools. Serialized
  local validation generated the package and matching SHA-256 sidecar, passed the focused package
  contract, and observed one canonical root containing exactly two directories and eight files. The
  launcher is exactly 96 ASCII bytes with five CRLF line endings and no BOM; its success and
  invalid-option forwarding oracles passed. The executable is x64 PE32+ with the Windows console
  subsystem. The local MSVC 19.38 binary imports exactly 11 allowed direct operating-system DLLs;
  the contract also permits only the Windows OS synchronization API-set
  `API-MS-WIN-CORE-SYNCH-L1-2-0` observed under hosted VS 18/MSVC 19.51/Windows SDK 26100, while rejecting SDL, MSVC,
  UCRT, debug-runtime, every other API-set, and every other unapproved import. The executable
  contains no source/build path prefix after deterministic `Release` path
  mapping and the enforced narrow/wide byte scan. Full MSVC CTest passed 32/32 `Debug`, 32/32
  `RelWithDebInfo`, and 33/33 `Release`; the 168-file dependency gate, all 209 tooling tests, and
  Python compile-all also passed. The staged public-tree gate checked 258 indexed text blobs.
  DCO passed, PR #40 merged the slice as exact `main` commit `ff8376b`, and exact-main run
  `29713390065` passed all four jobs and retained the named Windows archive artifact. General
  clean-machine behavior remains unclaimed. Validation used only
  public source and generated output; no private or owner files, D-drive content, disc image,
  executable, emulator, or PCSX2
  input was accessed.
- E-0080 defines the first main-push-only consumer of the retained Windows archive. After the
  `windows-portable-package` producer succeeds, a separate fresh GitHub-hosted `windows-2022` job
  downloads the same run's named `OpenOmega-0.1.0-windows-x86_64` artifact without checking out
  source or invoking CMake, CTest, a compiler, or the producer build tree. It requires exactly the
  ZIP and `.zip.sha256` sidecar as regular non-reparse files, parses its BOM-free ASCII lowercase
  digest, two spaces, exact filename, and single-CRLF syntax,
  recomputes the archive digest, and extracts exactly the frozen two-directory/eight-file package
  tree into an isolated directory. It then constrains `PATH` to the Windows system directories and
  points `LOCALAPPDATA`, `APPDATA`, `USERPROFILE`, `TEMP`, and `TMP` at an isolated synthetic profile.
  Through the absolute system command processor, from an unrelated working directory, it requires
  the packaged launcher to pass both exact process oracles: exit zero, exact
  `OpenOmega native shell: rendered_frames=0` plus one newline on stdout, and empty stderr for
  `--frames=0`; then exit one, empty stdout, and the frozen exact diagnostic and usage text on stderr
  for the invalid sentinel. SHA-256 manifests of the downloaded artifact, extracted package,
  unrelated working directory, and synthetic profile must be identical after each launch. Local
  emulation of the exact PowerShell body extracted from the workflow YAML passed against both the
  freshly regenerated local package and the retained E-0079 main artifact. Full Release CTest
  passed 33/33, the 168-file dependency gate and all 209 tooling tests passed, Python compile-all
  passed, and the staged public-tree gate checked 258 indexed text blobs. PR #41 merged E-0080 as
  exact `main` commit `4868e1118bcd32c6713a7f4be57dd243d40996ed`. Exact-main push run
  `29714679947` completed all five jobs successfully. Consumer job `88266108572` downloaded retained
  artifact ID `8450186290`; GitHub verified that artifact envelope's SHA-256 as
  `aea3f4869d17874305bf6027bce370d884ddcaed35e3e9d7a4bc2217aa6baac2`, while the strict
  sidecar/recompute check verified the retained inner package ZIP SHA-256 as
  `c06ce722572c5edbb0c34ce6b3fc985bcadd4e24ebda0cc07dff59df65ccfe5d`. The job emitted
  `fresh-VM portable consumer: OK (artifact, checksum, tree, launch, immutability)`. This hosted
  result covers only same-run artifact transfer, integrity, extraction, package-relative launch,
  exact process behavior, and non-mutation on that runner image. Because its zero-frame path returns
  before application/platform creation, E-0080 does not validate a window, menu interaction, GPU,
  audio, owner data, physical or arbitrary clean machines, Windows client editions, other Windows
  Server releases, or other runner images. Both digests identify retained E-0080 artifact
  `8450186290`; they are not hashes for every later package build.
- E-0081 locally smoke-validates a separately identified current portable package's generated
  no-content main menu on the tested Windows host. Its sidecar matched the package ZIP, all eight
  extracted regular files matched their ZIP members, and the absolute extracted launcher started
  without arguments from an unrelated empty working directory under an isolated profile. No owner
  data, level, or configuration argument was supplied. The exact `OpenOmega - native runtime`
  window title appeared, and a temporary operator-visible screenshot showed only the
  project-generated no-content OPEN OMEGA diagnostic menu. Its ignored PNG was deleted after
  display and is not retained or committed. Native Escape closed the visible
  run, and a second same-package exit oracle returned zero. Direct process evidence contained
  two stdout lines, exactly three INFO-only stderr records, and zero warning, error, or fatal stderr
  records. The unrelated work directory remained empty. GPU drivers wrote only shader-cache files
  inside the isolated profile, so profile immutability is intentionally unclaimed. No proprietary
  input, retail asset, retail executable, emulator, PCSX2 input, or D-drive filesystem input was
  accessed.
  This is not a pixel-golden test or evidence of retail-menu fidelity, owner-data behavior,
  controller or audio coverage, another machine, another Windows release, or PCSX2 equivalence.
- E-0082 wires the strict E-0078 Packed24 projection into complete `LevelContent` startup while
  preserving the mandatory E-0077 topology path. `BuildFirstLevelTextureDiagnosticPreview` runs
  both builders inside the existing canonical-handle `AssetService` transaction and returns an
  independently owned topology image plus either an optional transfer image or its typed rejection.
  Every Packed24 diagnostic rejection is intentionally nonfatal; `OmegaApp` keeps the topology-only
  presentation and emits one fixed identity-free INFO category. On the public synthetic Packed24
  fixture, the combined helper produces frozen 32x32 topology and 16x16/1,024-byte transfer outputs.
  App-state inspection establishes five resident uploads totaling 78,864 bytes and an exact
  three-command base-plus-split-topology-plus-transfer draw list; a GPU probe confirms the first four
  packed triples and their synthetic `0xff` fourth slots. Teardown releases the transfer texture
  before the topology texture. On the synthetic Packed32 fixture, startup remains topology-only with
  four resident uploads totaling 77,840 bytes, and the log contains exactly one fixed
  `unsupported-sample-encoding` INFO record without fixture identity. A helper-level output-limit
  rejection preserves topology and restores aggregate `AssetService` state. Source inspection shows
  one Request/Release transaction. A test-only 77,840-byte renderer-pool budget rejects the optional
  fifth upload before GPU allocation; startup still succeeds with four resident textures, the
  full-width topology list, exact pool-state preservation, and one fixed identity-free `upload-failed`
  INFO record. Serialized local validation passed focused and full MSVC builds;
  direct asset-service and Direct3D12 app smokes; default, GPU-opt-in, and restored CTest at 32/36/32;
  the 168-file dependency gate; all 209 tooling tests; Python compile-all; and the public-tree gate
  over 258 indexed text blobs. Diff and DCO checks passed. PR, head, publication, and exact-main
  validation remain unclaimed. These checks do not prove a cumulative exact request count, every
  rejection category, full-image GPU fidelity, backend-specific fifth-upload failures, release
  failure, allocation injection, or valid-transfer failure rollback. The opt-in GPU suite ran locally; default hosted CI
  only compiles that integration test. The diagnostic assigns no channel, alpha, row-order, swizzle,
  color-space, material, geometry, retail-rendering, gameplay, or emulator-equivalence semantics.
  Only public source and generated fixtures were used; no private or owner file, proprietary input,
  D-drive content, disc image, retail executable, emulator, or PCSX2 input was accessed.
> **Provenance quarantine:** an earlier README revision reported a private ReSymbol
> `Ps2EeR5900LeCoreV1` cross-check with a 38/38 result. No evidence-ledger entry preserves its
> source and check, so it is excluded from this verified baseline pending a new reproducible,
> privacy-safe evidence record. It supports no current loader, decoder, format, runtime, or
> retail-behavior claim.

- E-0023 records that the native VUM adapter converts all 7,036 material catalogs into owned neutral
  data: 38,793 source-order names, 38,899 material records, and 42,631 dense name references with
  zero errors. E-0033 records that level-wide service orchestration independently loads the 5,351
  manifest-referenced catalogs across all 18 levels with zero errors: 34,267 owned names, 34,589
  material records, and 37,893 dense references in exact manifest order. E-0024 records that a
  separate retail-only passive descriptor validates 91,460 payload pairs, 38,023 normalized targets,
  134,122 middle-to-final references, and 365,840 ordered Q/P references without exposing payload
  bytes, render geometry, or console instructions.
- E-0041 records a bounded analysis-only lexical-coherence pass that compares complete normalized
  VUM member-name occurrences only against exact complete direct locator strings when the VUM name
  ends `.TDX`.
  Two passes are byte-identical, scan 18/18 levels with zero errors, and exactly reproduce the native
  cell/catalog/name/material/reference and container/locator populations. All 34,267 VUM name
  occurrences and 37,893 dense references classify as non-`.TDX`; no name enters the eligible
  exact-candidate lookup branch, and all 5,801 class-qualified locators remain unreached. This narrow
  negative result tests no alias family, observes no native or retail lookup, establishes no texture
  role, material binding, source priority, or runtime integration, and exposes no identities or
  proprietary data.
- E-0042 records a separate exact-first one-terminal-extension pass that keeps that direct `.TDX`
  locator universe unchanged. After complete normalization, it independently removes at most one
  syntactic extension from the final component on both sides only when the full strings are not
  already equal; directory components and original class-qualified locators are preserved. Two
  passes are byte-identical and scan 18/18 levels with zero errors. All 34,267 VUM name occurrences
  and 37,893 dense references are
  extension-elided unique-primary candidates; all 34,589 material records inherit a unique
  extension-elided candidate, while 5,690 original locator occurrences are reached only through
  extension elision and 111 remain unreached. This offline lexical result does not observe a retail
  alias rule or material consumption, establish a texture role or binding, assign class priority, or
  justify runtime integration, and it exposes no identities or proprietary data.
- E-0034 records the first privacy-safe VUM consumer trace: one strict-validator-accepted complete
  120-frame pair from one selected runtime copy. Its repeat is byte-identical; each validated report
  contains two EE-read aggregate rows, two anonymous-site rows, and zero VIF1 chunk rows. Post-run
  containment auditing found no retained runtime copy, executable surface, reparse point,
  owner-input copy, or emulator/build process. An independently guarded second ranked trial
  reproduced those aggregates exactly: one capture with four accepted aggregate rows, split evenly
  between EE-read and anonymous-site rows, zero VIF1 rows, and zero aggregate-count deltas. In both
  trials, the EE-read rows remain confined to the already-opaque header-vector block; no accepted
  row reaches counts, records, metadata, payload, VIF, or tail data. This repeated header-only
  observation does not exclude copied buffers or activity outside the bounded observation window
  and assigns no geometry, topology, vertex, material, packet, draw, placement, visibility, or
  gameplay semantics.

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
.\build\msvc\Debug\omega_sdl_gpu_texture_smoke.exe
.\build\msvc\Debug\omega_tool.exe hog-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe hog-verify-nested-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe pop-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe pop-post-terrain-hypotheses-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-manifest-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-spatial-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-material-catalogs-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe level-texture-store-verify-tree .\private\extracted-disc
.\build\msvc\Debug\omega_tool.exe asset-metadata-verify-tree .\private\extracted-disc
.\build\msvc\Debug\openomega.exe --data-root=.\private\extracted-disc --level=MINSK --probe-only
.\build\msvc\Debug\openomega.exe --data-root=.\private\extracted-disc --level=MINSK --frames=120
python -B .\tools\probe_native_levels.py .\build\msvc\Debug\openomega.exe .\private\extracted-disc --aggregate-only
.\build\msvc\Debug\openomega.exe --frames=120
.\build\msvc\Debug\openomega.exe --frames=120 --capture-run
.\build\msvc\Debug\openomega.exe --frames=120 --capture-run --replay-capture
.\build\msvc\Debug\openomega.exe --config=.\openomega.cfg --set=log.minimum_severity=debug --frames=120
```

`openomega` is the pure-native SDL3/SDL_GPU host shell. `--frames=N` is an automated smoke mode
that opens the modern GPU backend, renders exactly `N` frames, and exits without user input.
Adding `--capture-run` explicitly selects bounded in-process capture for 1 through 65,536 frames
and prints only aggregate trace metadata plus selected absolute scheduler counters; it creates no
capture file and prints no per-frame input or elapsed records. Ordinary `--frames` behavior is
unchanged when the flag is absent.
Adding `--replay-capture` to that finite capture performs immediate main-thread replay into a fresh
scheduler and world in the same process. When the captured action schema contains all four E-0060
debug-movement actions, that fresh world owns one positioned synthetic actor and consumes the
reconstructed held input; older schemas remain clock-only. Replay requires a complete nonterminal
capture and a zero-origin capture scheduler, fails fast on any replay or comparison error, and
prints the unchanged fixed fresh-replay aggregate line only after aggregate and final-state
verification succeeds. Without `--replay-capture`, capture-only output and behavior remain unchanged.
`--probe-only` validates the retail root and selected level, then loads the owned manifest plus one
all-or-error `LevelContentIR` and opens an inventory-only `LevelTextureStore` without opening a
window. The store is retained only after the existing content and debug-image gates succeed. No
`LevelTextureStore` payload is loaded or used for display expansion, material binding, GPU upload, or
rendering; the rendered path uploads only the existing project-generated diagnostic RGBA8 image.
The spatial meshes and role-free material catalogs are decoded under one shared budget while each
common/cell archive is traversed once; their parallel manifest order asserts no mesh-to-material
binding. The current MINSK view is a deterministic synthetic
canonical-COL wireframe contact sheet: meshes occupy source-order tiles, and each mesh is projected
along its two largest coordinate extents. This clean-room diagnostic is not world placement or
reconstructed geometry and makes no VUM, TDX, or other retail semantic claim. The app-owned loop
converts steady-clock deltas into bounded fixed-step plans and executes each step against a
platform-neutral deterministic `SimulationWorld`; the configured default remains a synthetic-shell
timing value, not a retail-rate claim. The app also owns a non-hot-reloadable SDL audio service whose
callback never reads files, logs, blocks, or touches retail data; the current silent stream proves
device lifecycle and thread plumbing without embedding or guessing proprietary audio. A separate
non-hot-reloadable `SdlInputService` is the sole global event-queue consumer through `PumpEvents`,
routes keyboard and mouse input without requiring a controller, and owns a primary-gamepad lifetime
only when explicitly enabled. `SdlGpuHost` is consequently limited to video, window, GPU, and
rendering resources.

The current native controls are keyboard/mouse first. W/A/S/D or the arrow keys navigate and move;
Return, keypad Enter, or F1 select; Space or left mouse selects in menus and fires the synthetic
diagnostic cue in play; Escape or Backspace cancels; T or held right mouse targets in play, with
right mouse acting as back in menus; and F10 quits. Gamepad discovery is off by default;
`--set=input.gamepad_enabled=true` opts into optional button and D-pad aliases. These are
project-authored diagnostic controls, not a retail input or combat map.

The zero-file `omega_sdl_gpu_texture_smoke` target compiles whenever tests and the SDL backend are
built, but hardware/display-dependent CTest registration is off by default for headless safety.
Configure with `-DOMEGA_RUN_GPU_SMOKE_TEST=ON` to register it as a serial GPU integration test.

OpenOmega owns a native persistence database rather than emulating PS2 RAM, memory-card hardware,
or emulator savestates. Non-probe startup opens the platform-native `OpenOmega/native-save`
directory and validates front-end-bounded profile and character catalogs plus project-owned active
identity and diagnostic-session records before platform creation; `--frames=0` therefore bootstraps
persistence, while `--probe-only` never touches it. A fresh database contains two checksummed
generation-zero snapshots and an empty process-lock file. Profiles and characters use bounded,
versioned metadata records. Startup never automatically creates or selects a default identity:
explicit Primary edges separately create, select, and durably confirm the native profile and
`DIAGNOSTIC CHARACTER`. Character confirmation opens the project-owned BriefingRoom/mission
selector.
Only the currently resolved profile/character pair can activate its one startup-bound diagnostic
content value; successful preparation writes the fixed session marker before DiagnosticPlay, and
repeating the same valid preparation performs no write. This flow is native bootstrap policy, not a
retail profile, character, mission, or save representation.
Windows uses `%LOCALAPPDATA%/OpenOmega/native-save`, macOS uses
`$HOME/Library/Application Support/OpenOmega/native-save`, and XDG hosts use
`$XDG_DATA_HOME/openomega/native-save` or `$HOME/.local/share/openomega/native-save`.

The optional project-owned configuration file uses strict `lower_snake_case` dotted keys and
`key = value` lines. `--config=PATH` is always authoritative. Without that option, `openomega`
looks for one host-family default: `%LOCALAPPDATA%/OpenOmega/openomega.cfg` on Windows,
`$HOME/Library/Application Support/OpenOmega/openomega.cfg` on macOS,
`$XDG_CONFIG_HOME/openomega/openomega.cfg` when that root is absolute on XDG hosts, otherwise
`$HOME/.config/openomega/openomega.cfg` when `HOME` is absolute. Missing, empty, or relative
required roots produce no candidate. A missing default profile is equivalent to the empty store;
the final entry must be a reported regular file and is never followed when reported as a symlink.
Configuration failures use fixed explicit/default/`--set` source categories without printing the
source path, a user-controlled key, or a raw value. Structural parser diagnostics may report a line
number or fixed budget, and typed errors may name a compile-time-known public setting. No profile
directory or file is created. `--set=KEY=VALUE` applies one validated command-line override per key.
Current keys are
`log.minimum_severity`, `log.ring_capacity`, `jobs.worker_count`, `jobs.max_pending_jobs`,
`frame.simulation_step_ns`, `frame.max_steps_per_frame`, `frame.max_delta_ns`,
`input.max_events_per_frame`, `input.gamepad_enabled`, `content.data_root`, and
`content.level_code`. The gamepad key is an exact boolean and defaults to `false`. The content root plus
optional level form one profile; direct `--data-root`/`--level` options override that profile as one
pair. Frame defaults are synthetic host-shell engineering values, not claims about the retail tick
rate. A local root-level `openomega.cfg` is ignored by version control because it may contain a
private filesystem path.

Architecture, completion criteria, and the compatibility-first engine/SDK direction are versioned in
[`docs/02-Runtime-Architecture.md`](docs/02-Runtime-Architecture.md) and
[`docs/03-Milestones.md`](docs/03-Milestones.md), with the staged long-horizon plan in
[`docs/07-Engine-and-SDK-Roadmap.md`](docs/07-Engine-and-SDK-Roadmap.md) and its accepted decision in
[`ADR 0004`](docs/adr/0004-compatibility-first-engine-sdk.md). Research confidence is tracked in
`analysis/evidence/ledger.jsonl`.

## Layout

- `docs/` — architecture, milestones, decisions, policy, research contracts, and handoffs.
- `native/` — independently written runtime, engine libraries, tools, and synthetic tests.
- `tools/` — deterministic disc/ELF archaeology tools.
- `analysis/` — generated, redistributable metadata and reports.
- `analysis/formats/HOG.md` — validated HOG container layout and extraction notes.
- `analysis/formats/POP.md` — validated terrain-prefix contract, passive post-terrain hypothesis
  boundary, and native verifier.
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
