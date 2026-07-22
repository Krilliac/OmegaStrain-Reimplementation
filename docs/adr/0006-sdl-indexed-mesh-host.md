# ADR 0006: SDL GPU indexed diagnostic mesh boundary

- Status: accepted
- Date: 2026-07-21

## Context

ADR 0005 established a project-owned, renderer-neutral `SceneIR`, but deliberately stopped before
GPU resources or application-host integration. OpenOmega now needs a bounded native path that can
prove sanitized indexed triangles reach the real SDL GPU backend without making unevidenced retail
rendering claims or changing the existing menu and texture presentation path.

## Decision

`SdlGpuHost` owns a `RenderMeshPool` and a parallel slot-indexed table of SDL vertex and index
buffers. Upload borrows only the positions and triangle indices already validated by the CPU pool,
creates and submits both backend buffers synchronously, and publishes the reserved generation only
after submission succeeds. Failure rolls back the reservation and releases every uncommitted SDL
resource. Release waits for GPU idle, retires the exact resident generation, and clears its backend
slot. Only aggregate residency and saturating counters are observable.

`RenderFramePacket` owns a fixed `RenderMeshDrawList` in addition to its existing texture draw list.
The new list defaults empty. Consequently, callers that publish no mesh commands keep the prior
clear-plus-texture path and do not create mesh pipelines, shaders, or color buffers. Before any GPU
command is acquired, the host resolves every texture and mesh generation and prepares all portable
command data. A stale generation rejects the whole frame without a GPU-side prefix.

The diagnostic mesh pass clears the target, applies each command's project `object_to_clip` matrix,
binds a 32-bit index buffer, and issues source-order indexed triangle-list draws. Fill and wireframe
are separate lazily created pipelines. Existing texture blits then run in their established order
as an overlay. The host converts ADR 0005 row-major matrices to the column-major storage consumed by
the shader at the SDL boundary.

When canonical `LevelContentIR` is present, `OmegaApp` now invokes
`BuildGlobalSpatialDiagnosticScene` exactly once during startup. It preserves decoded inter-cell
offsets and scale under one aggregate diagnostic projection, then validates scene instance indices
and camera-times-instance matrices, transactionally uploads the complete mesh set, and owns one fixed
validated draw list. Only `DiagnosticPlay` copies that list into a frame packet. Its texture list
keeps the existing actor, targeting, and firing cues but omits the opaque full-screen diagnostic
base so indexed geometry remains visible; every menu/card list is unchanged. Shutdown clears mesh
commands and explicitly releases generations in reverse upload order before host teardown.

The shader is SDL's position/color `testgpu` shader in the DXIL, SPIR-V, and MSL forms shipped in the
exact permissively licensed SDL revision already pinned by CMake. Those generated headers are
consumed at build time from the fetched SDL source; OpenOmega adds no runtime shader compiler or
external shader payload. SDL objects and shader representations remain private to the app backend.

## Constraints

- This is an opaque unlit diagnostic path. Command alpha remains renderer-neutral data and does not
  yet select blending; the current shader writes alpha one.
- There is no depth attachment, material, texture sampling, lighting, skinning, visibility,
  occlusion, or retail draw-order claim. Source-order opaque draws are the entire current policy.
- `SdlGpuHost` does not upload a `SceneIR` aggregate implicitly. Its thin overload still borrows
  one owned `RenderMeshIR`; `OmegaApp` is the explicit caller that selects instances and composes
  matrices for the startup diagnostic scene.
- All upload, draw, wait, and release methods are main/render-thread operations. Handles retain no
  SDL pointer and do not pin a generation beyond synchronous packet consumption.
- Committed tests use synthetic project-authored triangles only. Proprietary inputs, paths,
  addresses, hashes, screenshots, and extracted shader or mesh data remain outside version control.

## Validation

- CPU tests prove a `RenderFramePacket` owns independent texture and mesh command copies and that
  both lists default empty.
- Pool and draw-list tests continue to enforce finite positions and matrices, complete triangles,
  in-range indices, bounded capacity, exact pool identity, and generation reuse.
- An opt-in SDL GPU smoke uploads one synthetic `SceneIR` mesh, reads an actual indexed fill draw
  back from an 8-by-8 RGBA8 target, then applies unequal scales plus translation and requires every
  colored pixel to land in the expected quadrant. This makes row/column storage regressions
  observable. The smoke also exercises fill and wireframe swapchain submissions, rejects a stale
  generation before submission, verifies generation-safe slot reuse, and ends with no resident mesh
  resources.
- The app capture smoke uses only a generated triangle and proves Profiles, Characters, and
  BriefingRoom submit no mesh; mission activation submits exactly one mesh plus the existing actor
  overlay; return to BriefingRoom suppresses the mesh; and explicit teardown restores zero mesh
  residency. A non-finite scene fails before SDL with a fixed path-free diagnostic.

## Consequences

OpenOmega has a real native indexed-triangle backend boundary suitable for future sanitized scene
fragments while preserving current behavior when no fragment is supplied. Retail materials,
camera behavior, visibility, transforms, and scene submission remain separately gated work rather
than assumptions hidden in this diagnostic renderer.
