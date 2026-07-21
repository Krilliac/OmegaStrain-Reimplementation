#pragma once

#include "omega/asset/level_spatial_ir.h"
#include "omega/asset/scene_ir.h"

#include <cstdint>
#include <expected>
#include <string>

namespace omega::runtime
{
// Diagnostic construction budgets, not retail scene or renderer limits. Positions are charged
// while inspected even when their triangle-free cell does not publish a render mesh.
struct SpatialDiagnosticSceneLimits
{
    std::uint64_t maximum_cells = 4096U;
    std::uint64_t maximum_positions = 1ULL << 20U;
    std::uint64_t maximum_triangle_indices = 6ULL << 20U;
    std::uint64_t maximum_output_bytes = 128ULL * 1024ULL * 1024ULL;
};

// [any worker thread; reentrant] Builds a project-owned contact-sheet scene from canonical spatial
// geometry. Source-order square-tile placement, per-cell projection, identity transforms, and the
// camera are diagnostic choices. They make no claim about retail placement, visibility, camera,
// axes, winding, collision use, or materials.
[[nodiscard]] std::expected<asset::SceneIR, std::string> BuildSpatialDiagnosticScene(
    const asset::LevelSpatialIR& spatial,
    const SpatialDiagnosticSceneLimits& limits = SpatialDiagnosticSceneLimits{});
} // namespace omega::runtime
