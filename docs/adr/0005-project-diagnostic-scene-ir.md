# ADR 0005: Project-owned scene IR and spatial diagnostic projection

- Status: accepted
- Date: 2026-07-21

## Context

OpenOmega needs an owned CPU-side scene boundary before a native indexed render path can be added.
The current `LevelSpatialIR` preserves independently evidenced spatial geometry and terrain-cell
ordering. It does not establish retail placement, transforms, visibility, camera, rendering,
collision use, winding, materials, or the meaning of any coordinate axis. Promoting those unknowns
would turn a diagnostic into an unsupported retail claim.

## Decision

`Float3IR` is the shared project geometry scalar and `Matrix4x4IR` stores 16 values in row-major
order. OpenOmega matrix math indexes element `(row, column)` as `row * 4 + column` and multiplies
column vectors: `clip = world_to_clip * local_to_world * [x y z 1]^T`. The project clip volume is
`-w <= x <= w`, `-w <= y <= w`, and `0 <= z <= w`, with positive Y upward after division by W.
These are OpenOmega conventions, not statements about the retail game or its original engine.

`RenderMeshIR` owns positions and flat groups of three unsigned 32-bit triangle indices. `SceneIR`
owns render meshes, indexed mesh instances, and one camera. It contains no material assignment,
platform resource, retail field, borrowed view, source path, or executable behavior. Identity is the
default instance and camera transform.

`BuildSpatialDiagnosticScene` is a bounded, reentrant diagnostic adapter, not a retail scene
decoder. It:

1. assigns all source cells to a square row-major contact-sheet grid, including gaps for empty
   cells;
2. independently selects each rendered cell's two largest coordinate extents, breaking equal
   extents in X, then Y, then Z order;
3. uniformly fits and centers that projection within a project-owned ten-percent tile inset;
4. emits project clip-volume positions at Z=0.5, concatenates source-order triangles into one owned
   indexed mesh, and uses identity instance and camera matrices; and
5. returns no meshes or instances when no cell has a triangle, avoiding an empty GPU-facing
   resource while retaining the identity camera.

All source vertices and triangle indices are validated before allocation. Aggregate counts,
rebasing, host capacity, and logical output bytes are bounded. Triangle-free cells still consume
the inspection limits and participate in the source-order grid.

`BuildGlobalSpatialDiagnosticScene` is a second, opt-in diagnostic adapter. Instead of assigning
independent contact-sheet tiles, it measures the union of triangle-bearing decoded-cell vertices,
selects the two largest aggregate coordinate extents with the same X-then-Y-then-Z tie-break, and
uniformly fits that one projection into the project clip volume. Consequently, relative offsets and
scale along the selected axes survive normalization. Triangle-free cells remain fully validated and
consume inspection budgets, but do not distort a projection for geometry they do not publish.

"Global" means only that the diagnostic uses one shared decoded coordinate domain. It does not
establish that cell coordinates are retail world placement, assign semantic names or handedness to
the selected axes, or infer retail camera, visibility, winding, collision, material, or draw-order
behavior.

## Consequences

- Project-authored geometry and future evidence-backed importers can target the same small owned
  scene values without depending on SDL or retail byte layouts.
- A future renderer may consume this scene only under the matrix and clip policy above; backend
  conversion stays at the platform boundary.
- Diagnostic images and native indexed geometry can share observable source-order and axis-selection
  behavior without claiming that the contact sheet represents retail world space.
- The global diagnostic can expose decoded inter-cell coordinate relationships without promoting
  those relationships to retail placement or axis semantics.
- Retail placement, visibility, camera, materials, and draw submission remain separately gated by
  clean-room evidence.

## Non-goals

- Treating `COL`, `POP`, `VUM`, or any other observed structure as proven retail render geometry.
- Reconstructing retail transforms, coordinate axes, camera matrices, visibility, occlusion,
  materials, lighting, or draw order.
- Adding GPU resources, shaders, a render-mesh pool, or application-host integration in this slice.
