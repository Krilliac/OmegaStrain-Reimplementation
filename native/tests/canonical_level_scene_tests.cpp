#include "omega/runtime/canonical_level_scene.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool IsIdentity(const omega::asset::Matrix4x4IR& matrix) noexcept
{
    return matrix == omega::asset::kIdentityMatrix4x4IR;
}

[[nodiscard]] omega::asset::SpatialMeshIR MakeTriangleMesh(const float scale = 1.0F)
{
    return omega::asset::SpatialMeshIR{
        .vertices =
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = scale, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = scale, .z = 0.0F},
            },
        .triangles =
            {
                {.vertex_indices = {0U, 1U, 2U}},
            },
    };
}

void CheckLimit(const omega::asset::LevelSpatialIR& spatial,
                omega::runtime::CanonicalLevelSceneLimits exact,
                omega::runtime::CanonicalLevelSceneLimits one_below, const std::string_view message)
{
    Check(omega::runtime::BuildCanonicalLevelScene(spatial, exact).has_value() &&
              !omega::runtime::BuildCanonicalLevelScene(spatial, one_below),
          message);
}
} // namespace

int main()
{
    // Empty level.
    auto empty_scene = omega::runtime::BuildCanonicalLevelScene({});
    Check(empty_scene && empty_scene->cells.empty() &&
              IsIdentity(empty_scene->camera.world_to_view) &&
              IsIdentity(empty_scene->camera.view_to_clip),
          "an empty level produces an empty canonical scene with an identity "
          "camera");

    // Ordering, an empty gap cell, and exact per-cell cardinality (no
    // concatenation/rebasing).
    omega::asset::LevelSpatialIR ordered{
        .terrain_cells = {MakeTriangleMesh(), omega::asset::SpatialMeshIR{},
                          MakeTriangleMesh(100.0F)},
    };
    const auto ordered_original = ordered;
    auto scene = omega::runtime::BuildCanonicalLevelScene(ordered);
    Check(scene.has_value(), "a bounded canonical spatial input builds a canonical scene");
    Check(ordered == ordered_original,
          "canonical scene construction does not mutate its source IR");
    if (scene)
    {
        Check(scene->cells.size() == 3U,
              "output cardinality equals input cardinality, including the empty "
              "gap cell");
        for (std::uint32_t index = 0U; index < 3U; ++index)
        {
            Check(scene->cells[index].source_cell_ordinal ==
                      omega::runtime::SourceCellOrdinal{.value = index},
                  "each cell carries an explicit typed source-order ordinal");
            Check(IsIdentity(scene->cells[index].local_to_world),
                  "every cell uses the fixed project identity transform");
        }
        Check(IsIdentity(scene->camera.world_to_view) && IsIdentity(scene->camera.view_to_clip),
              "the canonical scene uses explicit identity camera stages");

        Check(scene->cells[0].render_mesh.positions == ordered.terrain_cells[0].vertices &&
                  scene->cells[2].render_mesh.positions == ordered.terrain_cells[2].vertices,
              "vertex coordinates are copied verbatim, never re-projected or fitted");
        Check(scene->cells[0].render_mesh.triangle_indices ==
                  std::vector<std::uint32_t>({0U, 1U, 2U}),
              "triangle indices are copied verbatim without cross-cell rebasing");
        Check(scene->cells[1].render_mesh.positions.empty() &&
                  scene->cells[1].render_mesh.triangle_indices.empty(),
              "a fully empty cell remains canonical data at its explicit source "
              "ordinal");

        const auto owned_positions = scene->cells[0].render_mesh.positions;
        ordered.terrain_cells[0].vertices[0] = {.x = 12345.0F, .y = 6789.0F, .z = -1.0F};
        Check(scene->cells[0].render_mesh.positions == owned_positions,
              "the scene owns its geometry after the source IR changes");
        ordered = ordered_original;
    }

    // Extreme finite coordinates and multi-triangle winding survive unmodified.
    omega::asset::LevelSpatialIR extreme{
        .terrain_cells = {omega::asset::SpatialMeshIR{
            .vertices =
                {
                    {.x = 1.0E30F, .y = -1.0E30F, .z = 1.0E-30F},
                    {.x = -2.5F, .y = 0.0F, .z = 0.0F},
                    {.x = 0.0F, .y = 3.5F, .z = 0.0F},
                    {.x = 9.0F, .y = 9.0F, .z = 9.0F},
                },
            .triangles =
                {
                    {.vertex_indices = {0U, 1U, 2U}},
                    {.vertex_indices = {2U, 1U, 3U}},
                },
        }},
    };
    auto extreme_scene = omega::runtime::BuildCanonicalLevelScene(extreme);
    Check(extreme_scene && extreme_scene->cells.size() == 1U &&
              extreme_scene->cells[0].render_mesh.positions == extreme.terrain_cells[0].vertices,
          "extreme finite coordinates are preserved exactly, never clamped or "
          "normalized");
    Check(extreme_scene && extreme_scene->cells[0].render_mesh.triangle_indices ==
                               std::vector<std::uint32_t>({0U, 1U, 2U, 2U, 1U, 3U}),
          "multi-triangle winding order is preserved in source order");

    // Vertices without triangles are still preserved (nothing invented, nothing
    // dropped).
    omega::asset::LevelSpatialIR triangle_free{
        .terrain_cells = {omega::asset::SpatialMeshIR{
            .vertices = {{.x = 1.0F, .y = 2.0F, .z = 3.0F}},
        }},
    };
    auto triangle_free_scene = omega::runtime::BuildCanonicalLevelScene(triangle_free);
    Check(triangle_free_scene && triangle_free_scene->cells.size() == 1U &&
              triangle_free_scene->cells[0].render_mesh.positions.size() == 1U &&
              triangle_free_scene->cells[0].render_mesh.triangle_indices.empty(),
          "a triangle-free cell still publishes its inspected vertex positions");

    // Rejections.
    auto nonfinite = ordered_original;
    nonfinite.terrain_cells[1].vertices.push_back(
        {.x = std::numeric_limits<float>::infinity(), .y = 0.0F, .z = 0.0F});
    Check(!omega::runtime::BuildCanonicalLevelScene(nonfinite),
          "non-finite coordinates are rejected even in a triangle-free cell");

    auto nan_case = ordered_original;
    nan_case.terrain_cells[0].vertices[0].z = std::numeric_limits<float>::quiet_NaN();
    Check(!omega::runtime::BuildCanonicalLevelScene(nan_case), "NaN coordinates are rejected");

    auto bad_index = omega::asset::LevelSpatialIR{.terrain_cells = {MakeTriangleMesh()}};
    bad_index.terrain_cells[0].triangles[0].vertex_indices[2] = 3U;
    Check(!omega::runtime::BuildCanonicalLevelScene(bad_index),
          "triangle indices are range checked against their own cell's vertex "
          "count");

    auto out_of_range_other_cell = omega::asset::LevelSpatialIR{
        .terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh()},
    };
    out_of_range_other_cell.terrain_cells[1].triangles[0].vertex_indices[0] = 99U;
    Check(!omega::runtime::BuildCanonicalLevelScene(out_of_range_other_cell),
          "an out-of-range index in a later cell is rejected before output "
          "allocation");

    // Cumulative resource limits, exact and one below.
    omega::asset::LevelSpatialIR bounded{
        .terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh()},
    };
    auto exact = omega::runtime::CanonicalLevelSceneLimits{};
    auto below = exact;
    exact.maximum_cells = 2U;
    below.maximum_cells = 1U;
    CheckLimit(bounded, exact, below, "exact and one-below cell limits are enforced");

    exact = {};
    below = {};
    exact.maximum_positions = 6U;
    below.maximum_positions = 5U;
    CheckLimit(bounded, exact, below, "exact and one-below aggregate position limits are enforced");

    exact = {};
    below = {};
    exact.maximum_triangle_indices = 6U;
    below.maximum_triangle_indices = 5U;
    CheckLimit(bounded, exact, below,
               "exact and one-below aggregate triangle-index limits are enforced");

    constexpr std::uint64_t output_bytes = sizeof(omega::runtime::CanonicalLevelScene) +
                                           2U * sizeof(omega::runtime::CanonicalLevelSceneCell) +
                                           6U * sizeof(omega::asset::Float3IR) +
                                           6U * sizeof(std::uint32_t);
    exact = {};
    below = {};
    exact.maximum_output_bytes = output_bytes;
    below.maximum_output_bytes = output_bytes - 1U;
    CheckLimit(bounded, exact, below,
               "exact and one-below logical output-byte limits are enforced");

    auto widened = omega::runtime::CanonicalLevelSceneLimits{};
    ++widened.maximum_cells;
    Check(!omega::runtime::BuildCanonicalLevelScene(bounded, widened),
          "callers cannot widen canonical scene safety maxima");

    return failures;
}
