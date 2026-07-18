#include "omega/runtime/spatial_debug_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <utility>

namespace
{
constexpr std::uint32_t kTilePixels = 32U;
constexpr std::array kWireColor{
    std::byte{112}, std::byte{220}, std::byte{255}, std::byte{255}};

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

omega::asset::SpatialMeshIR MakeTriangleMesh()
{
    return omega::asset::SpatialMeshIR{
        .vertices = {
            {.x = 0.0F, .y = 0.0F, .z = 0.0F},
            {.x = 1.0F, .y = 0.0F, .z = 0.0F},
            {.x = 0.0F, .y = 1.0F, .z = 0.0F},
        },
        .triangles = {
            {.vertex_indices = {0U, 1U, 2U}},
        },
    };
}

omega::asset::SpatialMeshIR MakeTieBreakMesh()
{
    return omega::asset::SpatialMeshIR{
        .vertices = {
            {.x = 0.0F, .y = 0.0F, .z = 0.0F},
            {.x = 1.0F, .y = 0.25F, .z = 0.0F},
            {.x = 0.5F, .y = 1.0F, .z = 0.0F},
        },
        .triangles = {
            {.vertex_indices = {0U, 1U, 2U}},
        },
    };
}

[[nodiscard]] std::array<std::byte, 4> PixelAt(const omega::runtime::DebugImage& image,
    const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset = (static_cast<std::size_t>(y) * image.width + x) * 4U;
    return {image.rgba8_pixels[offset], image.rgba8_pixels[offset + 1U],
        image.rgba8_pixels[offset + 2U], image.rgba8_pixels[offset + 3U]};
}

[[nodiscard]] std::size_t CountWirePixels(
    const omega::runtime::DebugImage& image, const std::size_t cell_index)
{
    const std::uint32_t side_tiles = image.width / kTilePixels;
    const std::uint32_t origin_x =
        static_cast<std::uint32_t>(cell_index % side_tiles) * kTilePixels;
    const std::uint32_t origin_y =
        static_cast<std::uint32_t>(cell_index / side_tiles) * kTilePixels;
    std::size_t count = 0;
    for (std::uint32_t y = origin_y; y < origin_y + kTilePixels; ++y)
    {
        for (std::uint32_t x = origin_x; x < origin_x + kTilePixels; ++x)
            count += PixelAt(image, x, y) == kWireColor ? 1U : 0U;
    }
    return count;
}

void CheckLimit(const omega::asset::LevelSpatialIR& spatial,
    omega::runtime::SpatialDebugImageLimits exact,
    omega::runtime::SpatialDebugImageLimits one_below, const std::string_view message)
{
    Check(omega::runtime::BuildSpatialDebugImage(spatial, exact).has_value() &&
              !omega::runtime::BuildSpatialDebugImage(spatial, one_below),
        message);
}
} // namespace

int SpatialDebugImageFailureCount()
{
    omega::asset::LevelSpatialIR ordered{
        .terrain_cells = {
            MakeTieBreakMesh(), omega::asset::SpatialMeshIR{}, MakeTriangleMesh()},
    };
    auto first = omega::runtime::BuildSpatialDebugImage(ordered);
    auto second = omega::runtime::BuildSpatialDebugImage(ordered);
    Check(first && first->width == 64U && first->height == 64U &&
              first->rgba8_pixels.size() == 64U * 64U * 4U &&
              first->pixels().size() == first->rgba8_pixels.size(),
        "three source cells produce a fully owned square 32-pixel-tile contact sheet");
    Check(first && second && first->rgba8_pixels == second->rgba8_pixels,
        "spatial diagnostic pixels are deterministic");
    if (first)
    {
        const auto owned_pixels = first->rgba8_pixels;
        const auto original_vertex = ordered.terrain_cells[0].vertices[0];
        ordered.terrain_cells[0].vertices[0] = {.x = 99.0F, .y = 99.0F, .z = 99.0F};
        Check(first->rgba8_pixels == owned_pixels,
            "the debug image owns every pixel after its source scene changes");
        ordered.terrain_cells[0].vertices[0] = original_vertex;
        bool all_alpha_opaque = true;
        for (std::size_t index = 3U; index < first->rgba8_pixels.size(); index += 4U)
            all_alpha_opaque = all_alpha_opaque && first->rgba8_pixels[index] == std::byte{255};
        Check(all_alpha_opaque, "every contact-sheet pixel is fully opaque");
        Check(CountWirePixels(*first, 0U) != 0U && CountWirePixels(*first, 1U) == 0U &&
                  CountWirePixels(*first, 2U) != 0U,
            "source-order cells occupy row-major tiles and empty cells remain wire-free");
        Check(PixelAt(*first, 16U, 3U) == kWireColor,
            "an asymmetric equal-extent mesh proves the deterministic X-then-Y tie-break");
    }

    auto reordered = ordered;
    std::swap(reordered.terrain_cells[0], reordered.terrain_cells[1]);
    auto reordered_image = omega::runtime::BuildSpatialDebugImage(reordered);
    Check(reordered_image && CountWirePixels(*reordered_image, 0U) == 0U &&
              CountWirePixels(*reordered_image, 1U) != 0U,
        "reordering canonical cells moves their diagnostics without inventing placement");

    auto no_cells = omega::runtime::BuildSpatialDebugImage({});
    Check(no_cells && no_cells->width == 32U && no_cells->height == 32U &&
              no_cells->rgba8_pixels.size() == 32U * 32U * 4U,
        "an empty level produces one safe background tile");

    omega::asset::LevelSpatialIR forward{.terrain_cells = {MakeTriangleMesh()}};
    auto reversed = forward;
    reversed.terrain_cells[0].triangles[0].vertex_indices = {2U, 1U, 0U};
    auto forward_image = omega::runtime::BuildSpatialDebugImage(forward);
    auto reversed_image = omega::runtime::BuildSpatialDebugImage(reversed);
    Check(forward_image && reversed_image &&
              forward_image->rgba8_pixels == reversed_image->rgba8_pixels,
        "canonicalized edge endpoints make triangle winding pixel-identical");

    auto changed_tree = forward;
    changed_tree.terrain_cells[0].leaf_triangle_references = {0U, 0U};
    changed_tree.terrain_cells[0].nodes.push_back({});
    changed_tree.terrain_cells[0].leaves.push_back({});
    changed_tree.terrain_cells[0].root = omega::asset::SpatialElementRefIR{
        .kind = omega::asset::SpatialElementKind::Node, .index = 77U};
    auto changed_tree_image = omega::runtime::BuildSpatialDebugImage(changed_tree);
    Check(forward_image && changed_tree_image &&
              forward_image->rgba8_pixels == changed_tree_image->rgba8_pixels,
        "spatial acceleration-tree details do not enter the diagnostic wireframe");

    auto point_degenerate = forward;
    point_degenerate.terrain_cells[0].vertices[1] = point_degenerate.terrain_cells[0].vertices[0];
    point_degenerate.terrain_cells[0].vertices[2] = point_degenerate.terrain_cells[0].vertices[0];
    auto point_image = omega::runtime::BuildSpatialDebugImage(point_degenerate);
    Check(point_image && CountWirePixels(*point_image, 0U) == 1U,
        "a point-degenerate triangle safely rasterizes one centered pixel");

    auto line_degenerate = forward;
    line_degenerate.terrain_cells[0].vertices[2] = {.x = 2.0F, .y = 0.0F, .z = 0.0F};
    auto line_image = omega::runtime::BuildSpatialDebugImage(line_degenerate);
    Check(line_image && CountWirePixels(*line_image, 0U) != 0U,
        "a one-dimensional mesh safely selects a centered secondary projection axis");

    auto nonfinite = forward;
    nonfinite.terrain_cells[0].vertices.push_back(
        {.x = std::numeric_limits<float>::infinity(), .y = 0.0F, .z = 0.0F});
    Check(!omega::runtime::BuildSpatialDebugImage(nonfinite),
        "every vertex is validated even when no triangle references it");
    auto bad_index = forward;
    bad_index.terrain_cells[0].triangles[0].vertex_indices[2] = 3U;
    Check(!omega::runtime::BuildSpatialDebugImage(bad_index),
        "every triangle index is range checked before raster allocation");

    omega::asset::LevelSpatialIR bounded{
        .terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh()},
    };
    auto exact = omega::runtime::SpatialDebugImageLimits{};
    auto below = exact;
    exact.maximum_cells = 2U;
    below.maximum_cells = 1U;
    CheckLimit(bounded, exact, below, "exact and one-below cell limits are enforced");

    exact = {};
    below = {};
    exact.maximum_vertices = 6U;
    below.maximum_vertices = 5U;
    CheckLimit(bounded, exact, below, "exact and one-below vertex limits are enforced");

    exact = {};
    below = {};
    exact.maximum_triangles = 2U;
    below.maximum_triangles = 1U;
    CheckLimit(bounded, exact, below, "exact and one-below triangle limits are enforced");

    exact = {};
    below = {};
    exact.maximum_raster_steps = 2U * 3U * kTilePixels;
    below.maximum_raster_steps = exact.maximum_raster_steps - 1U;
    CheckLimit(bounded, exact, below, "exact and one-below raster-work limits are enforced");

    exact = {};
    below = {};
    exact.maximum_output_bytes = 64U * 64U * 4U;
    below.maximum_output_bytes = exact.maximum_output_bytes - 1U;
    CheckLimit(bounded, exact, below, "exact and one-below image-byte limits are enforced");
    return failures;
}
