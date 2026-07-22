# ADR 0007: Canonical, exact-coordinate level-scene composition

- Status: accepted
- Date: 2026-07-22

## Context

ADR 0005 established `SceneIR` and two diagnostic adapters (`BuildSpatialDiagnosticScene`,
`BuildGlobalSpatialDiagnosticScene`) that deliberately re-project decoded `LevelSpatialIR` geometry
into a normalized contact-sheet or shared-aggregate projection for visualization. Both intentionally
discard the original per-vertex coordinate values and concatenate cells into one indexed mesh, so
neither is suitable as a base for later evidence-backed placement, per-cell material association
(queue item 14), or per-cell collision/query work (Wave E) that must address a specific source
terrain cell.

OpenOmega needs a second, non-diagnostic composer that preserves exactly what was decoded: the
literal per-cell vertex coordinates and triangle winding, with a deterministic and stable
correspondence back to the originating manifest terrain cell. This is queue items 11 and 13
(canonical project-owned level-scene builder; composition into one renderer-neutral scene with
deterministic IDs) plus the validation half of item 12.

## Decision

`BuildCanonicalLevelScene` (`native/include/omega/runtime/canonical_level_scene.h`) consumes a
`LevelSpatialIR` and produces a `SceneIR` where:

1. output cardinality always equals input cardinality: `render_meshes.size()` and
   `mesh_instances.size()` both equal `spatial.terrain_cells.size()`, with no cell skipped, merged,
   or reordered;
2. `mesh_instances[i].render_mesh_index == i`, so array position is a deterministic, source-order
   identifier for the originating terrain cell — unlike the diagnostic adapters, which either omit
   triangle-free cells or concatenate every cell into a single mesh;
3. every vertex position and triangle index is copied verbatim from its own cell with no rebasing,
   fitting, clamping, offset, or cross-cell concatenation — a cell's `RenderMeshIR` addresses only
   its own vertices; and
4. every instance transform and the camera are the fixed project identity value. This is an explicit
   placeholder for unproven per-cell placement. It is not a claim that cells share one coordinate
   frame, are positioned correctly relative to one another, overlap, or represent retail world
   space — those remain separately gated, evidence-backed work.

Validation runs before any output allocation: every vertex coordinate must be finite (checked even
in a triangle-free cell), every triangle index is range-checked against its own cell's vertex count,
cell count/position count/triangle-index count/logical output bytes are each checked against
caller-supplied `CanonicalLevelSceneLimits` with overflow-checked accumulation, and the camera and
every instance transform are confirmed finite (a currently-trivial check on the fixed identity value
that establishes the contract point for a future non-identity transform).

## Consequences

- Downstream per-cell work (material/name tables gated on VUM evidence, a texture-candidate seam,
  collision-world construction) can address `mesh_instances[i]` / `render_meshes[i]` as "the i-th
  manifest terrain cell" without re-deriving that correspondence.
- This composer is deliberately not wired into application startup. `OmegaApp` continues to use the
  ADR 0005/0006 diagnostic path; wiring a new provisional startup seam is separately gated queue
  work that touches application/GPU files outside this contract's scope.
- The composer does not merge or place cells relative to each other. A future evidence-backed
  placement transform is a separate, independently reviewed contract; this ADR establishes only the
  exact-preservation and deterministic-identifier properties.

## Non-goals

- Establishing retail placement, a shared coordinate frame across cells, visibility, camera axes,
  winding convention, or collision use.
- Replacing `BuildSpatialDiagnosticScene` or `BuildGlobalSpatialDiagnosticScene`, which remain the
  visualization-oriented adapters described in ADR 0005.
- Material, texture, or animation binding of any kind.
