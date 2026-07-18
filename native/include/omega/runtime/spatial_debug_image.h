#pragma once

#include "omega/asset/level_spatial_ir.h"
#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <expected>
#include <string>

namespace omega::runtime
{
// Synthetic diagnostic budgets, not retail scene or renderer limits. Defaults admit the complete
// currently validated level-spatial corpus while keeping image allocation and raster work bounded.
struct SpatialDebugImageLimits
{
    std::uint64_t maximum_cells = 4096U;
    std::uint64_t maximum_vertices = 1ULL << 20U;
    std::uint64_t maximum_triangles = 2ULL << 20U;
    std::uint64_t maximum_raster_steps = 1ULL << 27U;
    std::uint64_t maximum_output_bytes = 64ULL * 1024ULL * 1024ULL;
};

// [any worker thread; reentrant] Produces a source-order square contact sheet from canonical COL
// spatial meshes. Every cell is fitted independently into a synthetic tile by projecting its two
// largest coordinate extents, with equal extents ordered X, then Y, then Z. Tile positions,
// projection, colors, and scale are diagnostic choices and make no claim about retail world
// placement, axes, camera, winding, or materials.
[[nodiscard]] std::expected<DebugImage, std::string> BuildSpatialDebugImage(
    const asset::LevelSpatialIR& spatial,
    const SpatialDebugImageLimits& limits = SpatialDebugImageLimits{});
} // namespace omega::runtime
