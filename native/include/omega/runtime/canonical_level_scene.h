#pragma once

#include "omega/asset/level_spatial_ir.h"
#include "omega/asset/scene_ir.h"

#include <cstdint>
#include <expected>
#include <string>
#include <vector>

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

// Project-owned source-order identity for one canonical terrain cell. This is
// deliberately not a renderer mesh index and carries no retail identifier or
// placement meaning.
struct SourceCellOrdinal
{
    std::uint32_t value = 0U;

    bool operator==(const SourceCellOrdinal&) const = default;
};

struct CanonicalLevelSceneCell
{
    SourceCellOrdinal source_cell_ordinal;
    asset::RenderMeshIR render_mesh;
    asset::Matrix4x4IR local_to_world = asset::kIdentityMatrix4x4IR;

    bool operator==(const CanonicalLevelSceneCell&) const = default;
};

// Canonical cell storage is intentionally distinct from renderer-facing
// SceneIR. Every source cell remains present here, including cells with no
// renderable triangles. A renderer adapter may omit or page those cells while
// retaining source_cell_ordinal explicitly.
struct CanonicalLevelScene
{
    std::vector<CanonicalLevelSceneCell> cells;
    asset::SceneCameraIR camera;

    bool operator==(const CanonicalLevelScene&) const = default;
};

// [any worker thread; reentrant] Builds a project-owned canonical level scene
// that preserves canonical spatial geometry exactly as decoded. Unlike
// `BuildSpatialDiagnosticScene`, this composer never re-projects, fits,
// offsets, or concatenates coordinates: every terrain cell's vertex positions
// and triangle winding are copied verbatim and in source order into its own
// cell.
//
// Output cardinality always equals input cardinality, including cells with no
// triangles. Cell i has SourceCellOrdinal{i}; that typed ordinal remains
// independent from any later renderer mesh index. Every cell transform and the
// camera are the fixed project identity value; this is a placeholder for
// unproven per-cell placement, not a claim that cells share one coordinate
// frame, are correctly positioned relative to one another, overlap, or
// represent retail world space.
//
// The composer validates every input before allocating output: non-finite
// vertex coordinates are rejected (even in a triangle-free cell), triangle
// indices are range-checked against their own cell's vertex count, and every
// intermediate count and byte total is checked for overflow against both host
// container capacity and the caller-supplied limits before any output storage
// is reserved.
[[nodiscard]] std::expected<CanonicalLevelScene, std::string> BuildCanonicalLevelScene(
    const asset::LevelSpatialIR& spatial,
    const CanonicalLevelSceneLimits& limits = CanonicalLevelSceneLimits{});
} // namespace omega::runtime
