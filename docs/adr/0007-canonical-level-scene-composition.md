# ADR 0007: Canonical, exact-coordinate level-scene composition

- Status: accepted
- Date: 2026-07-22

## Context

ADR 0005 established `SceneIR` and two diagnostic adapters (`BuildSpatialDiagnosticScene`,
`BuildGlobalSpatialDiagnosticScene`) that deliberately re-project decoded `LevelSpatialIR` geometry
into a normalized contact-sheet or shared-aggregate projection. Both discard original coordinate
values and concatenate cells, so neither is suitable for later evidence-backed placement,
per-cell association, or collision/query work that must address one source terrain cell.

OpenOmega needs canonical storage that preserves decoded coordinates and triangle winding with a
stable source-order identity. The existing host accepts at most 63 environment meshes and rejects
empty mesh uploads, so canonical storage and renderer-facing batching cannot share one shape.

## Decision

`BuildCanonicalLevelScene` (`native/include/omega/runtime/canonical_level_scene.h`) consumes a
`LevelSpatialIR` and produces a `CanonicalLevelScene` where:

1. `cells.size()` always equals `spatial.terrain_cells.size()`, with no cell skipped, merged, or
   reordered, including empty and triangle-free cells;
2. each cell carries `SourceCellOrdinal{i}`. The typed ordinal is project-owned source order, not a
   renderer mesh index, retail identifier, placement claim, path, or archive-member identity;
3. every vertex position and triangle index is copied verbatim from its own cell with no rebasing,
   fitting, clamping, offset, or cross-cell concatenation; and
4. every cell transform and the camera are the fixed project identity value. This is a placeholder
   for unproven per-cell placement, not a claim that cells share one coordinate frame, are correctly
   positioned relative to one another, overlap, or represent retail world space.

Validation runs before output allocation: every coordinate must be finite, every triangle index is
range-checked against its own cell, and cell, position, triangle-index, and logical-output totals are
overflow-checked against tighten-only safety limits.

Output-byte limits in these APIs are conservative ABI-local logical host-footprint budgets. They
sum owned value and element sizes with payload bytes, but they are not serialization sizes,
cross-ABI fingerprints, or exact heap-allocation caps; allocator metadata and
implementation-defined capacity are outside the accounting.

`BuildCanonicalLevelSceneWithMaterials` preserves `LevelContentIR`'s documented positional
relationship by assigning the same explicit ordinal to each scene cell and material catalog. The
result revalidates cardinality and ordinal equality. This preservation is not independent provenance
proof and establishes no triangle, texture, visibility, or draw binding. Nested catalog, name,
material, string, scene, and combined-output budgets are checked before allocation. The public
association validator independently rechecks the canonical geometry, material references, and every
passed tighten-only budget without allocating output.

`BuildCanonicalLevelRenderPages` is the renderer-facing adapter. It validates canonical ordinals,
omits cells without complete renderable triangles, records every omitted ordinal, and copies the
remaining cells into source-order `SceneIR` pages of at most 63 meshes. Each page owns an explicit
`SourceCellOrdinal` to page-local `render_mesh_index` mapping. A local renderer index can restart at
zero on every page without changing source identity.

`BuildSceneStructureSnapshot` publishes only a fixed-size, versioned aggregate of bounded counts and
an identity-camera flag. It contains no geometry digest or per-cell rows. Snapshot equality means
only aggregate equality; it is neither scene equality nor authentication.

## Consequences

- Downstream per-cell work can key project-owned associations by `SourceCellOrdinal` without
  treating a mutable renderer array index as identity.
- Empty cells remain in canonical storage but never reach the renderer's nonempty upload path.
  Levels above the 63-environment-mesh boundary are deterministically paged with ordinals intact.
- This work is not wired into application startup. `OmegaApp` continues to use the ADR 0005/0006
  diagnostic path until a separately reviewed page-consumption policy exists.
- The composer does not merge or place cells relative to one another. Future evidence-backed
  placement remains an independent contract.

## Non-goals

- Establishing retail placement, a shared coordinate frame, visibility, camera axes, winding
  convention, or collision use.
- Replacing the visualization-oriented ADR 0005 diagnostic adapters.
- Material-to-geometry, texture, visibility, or animation binding of any kind.
