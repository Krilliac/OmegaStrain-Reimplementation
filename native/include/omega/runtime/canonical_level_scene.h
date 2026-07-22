#pragma once

#include "omega/asset/level_spatial_ir.h"
#include "omega/asset/scene_ir.h"

#include <cstdint>
#include <expected>
#include <string>

namespace omega::runtime
{
// Composition budgets, not retail scene or renderer limits.
struct CanonicalLevelSceneLimits
{
    std::uint64_t maximum_cells = 4096U;
    std::uint64_t maximum_positions = 1ULL << 20U;
    std::uint64_t maximum_triangle_indices = 6ULL << 20U;
    std::uint64_t maximum_output_bytes = 128ULL * 1024ULL * 1024ULL;
};

// [any worker thread; reentrant] Builds a project-owned renderer-neutral `SceneIR` that preserves
// canonical spatial geometry exactly as decoded. Unlike `BuildSpatialDiagnosticScene`, this
// composer never re-projects, fits, offsets, or concatenates coordinates: every terrain cell's
// vertex positions and triangle winding are copied verbatim and in source order into its own
// `RenderMeshIR`.
//
// Output cardinality always equals input cardinality: `render_meshes.size()` and
// `mesh_instances.size()` both equal `spatial.terrain_cells.size()`, including cells with no
// triangles (their `RenderMeshIR` still carries that cell's inspected vertex positions but an empty
// `triangle_indices`). `mesh_instances[i].render_mesh_index == i`, so an instance's position in the
// output is a deterministic, source-order identifier for its originating manifest terrain cell.
// Every instance transform and the camera are the fixed project identity value; this is a
// placeholder for unproven per-cell placement, not a claim that cells share one coordinate frame,
// are correctly positioned relative to each other, overlap, or represent retail world space.
//
// The composer validates every input before allocating output: non-finite vertex coordinates are
// rejected (even in a triangle-free cell), triangle indices are range-checked against their own
// cell's vertex count, and every intermediate count and byte total is checked for overflow against
// both host container capacity and the caller-supplied limits before any output storage is
// reserved.
[[nodiscard]] std::expected<asset::SceneIR, std::string> BuildCanonicalLevelScene(
    const asset::LevelSpatialIR& spatial,
    const CanonicalLevelSceneLimits& limits = CanonicalLevelSceneLimits{});
} // namespace omega::runtime
