#include "omega/runtime/spatial_diagnostic_scene.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
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

[[nodiscard]] bool NearlyEqual(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 0.00001F;
}

[[nodiscard]] bool IsIdentity(const omega::asset::Matrix4x4IR& matrix) noexcept
{
    return matrix == omega::asset::kIdentityMatrix4x4IR;
}

[[nodiscard]] omega::asset::SpatialMeshIR MakeTriangleMesh(const float scale = 1.0F)
{
    return omega::asset::SpatialMeshIR{
        .vertices = {
            {.x = 0.0F, .y = 0.0F, .z = 0.0F},
            {.x = scale, .y = 0.0F, .z = 0.0F},
            {.x = 0.0F, .y = scale, .z = 0.0F},
        },
        .triangles = {
            {.vertex_indices = {0U, 1U, 2U}},
        },
    };
}

void CheckLimit(const omega::asset::LevelSpatialIR& spatial,
    omega::runtime::SpatialDiagnosticSceneLimits exact,
    omega::runtime::SpatialDiagnosticSceneLimits one_below,
    const std::string_view message)
{
    Check(omega::runtime::BuildSpatialDiagnosticScene(spatial, exact).has_value() &&
              !omega::runtime::BuildSpatialDiagnosticScene(spatial, one_below),
        message);
}
} // namespace

int main()
{
    const auto identity = omega::asset::kIdentityMatrix4x4IR;
    Check(identity.row_major.size() == 16U && identity.row_major[0] == 1.0F &&
              identity.row_major[5] == 1.0F && identity.row_major[10] == 1.0F &&
              identity.row_major[15] == 1.0F,
        "the project matrix identity has four unit diagonal elements");

    omega::asset::LevelSpatialIR ordered{
        .terrain_cells = {
            MakeTriangleMesh(), omega::asset::SpatialMeshIR{}, MakeTriangleMesh(100.0F)},
    };
    const auto original = ordered;
    auto scene = omega::runtime::BuildSpatialDiagnosticScene(ordered);
    Check(scene.has_value(), "a bounded canonical spatial input builds a diagnostic scene");
    Check(ordered == original, "diagnostic scene construction does not mutate its source IR");
    Check(scene && scene->render_meshes.size() == 1U && scene->mesh_instances.size() == 1U,
        "non-empty diagnostics concatenate into one mesh and one instance");
    if (scene)
    {
        Check(IsIdentity(scene->camera.world_to_clip) &&
                  IsIdentity(scene->mesh_instances[0].local_to_world) &&
                  scene->mesh_instances[0].render_mesh_index == 0U,
            "the project diagnostic scene uses explicit identity camera and instance transforms");
        const auto& mesh = scene->render_meshes[0];
        Check(mesh.positions.size() == 6U &&
                  mesh.triangle_indices == std::vector<std::uint32_t>({0U, 1U, 2U, 3U, 4U, 5U}),
            "source triangles remain ordered and later cell indices are rebased exactly once");
        Check(NearlyEqual(mesh.positions[0].x, -0.9F) &&
                  NearlyEqual(mesh.positions[0].y, 0.1F) &&
                  NearlyEqual(mesh.positions[3].x, -0.9F) &&
                  NearlyEqual(mesh.positions[3].y, -0.9F),
            "source-order cells retain row-major square-tile locations including an empty gap");
        Check(NearlyEqual(mesh.positions[1].x - mesh.positions[0].x, 0.8F) &&
                  NearlyEqual(mesh.positions[4].x - mesh.positions[3].x, 0.8F),
            "each cell is independently fitted despite unrelated source coordinate scales");
        Check(std::ranges::all_of(mesh.positions, [](const omega::asset::Float3IR& position) {
                  return std::isfinite(position.x) && std::isfinite(position.y) &&
                         std::isfinite(position.z) && NearlyEqual(position.z, 0.5F);
              }),
            "every generated diagnostic position is finite and uses project-owned depth");

        const auto owned_positions = mesh.positions;
        ordered.terrain_cells[0].vertices[0] = {.x = 99.0F, .y = 99.0F, .z = 99.0F};
        Check(scene->render_meshes[0].positions == owned_positions,
            "the scene owns its geometry after the source IR changes");
        ordered = original;
    }

    omega::asset::LevelSpatialIR dominant_axes{
        .terrain_cells = {omega::asset::SpatialMeshIR{
            .vertices = {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 1.0F, .y = 1.0F, .z = 4.0F},
                {.x = 0.5F, .y = 2.0F, .z = 2.0F},
            },
            .triangles = {{{.vertex_indices = {0U, 1U, 2U}}}},
        }},
    };
    auto dominant_scene = omega::runtime::BuildSpatialDiagnosticScene(dominant_axes);
    Check(dominant_scene && NearlyEqual(dominant_scene->render_meshes[0].positions[1].x, 0.8F) &&
              NearlyEqual(dominant_scene->render_meshes[0].positions[2].y, 0.4F),
        "the two largest coordinate extents are selected independently for a cell");

    omega::asset::LevelSpatialIR tied_axes{
        .terrain_cells = {omega::asset::SpatialMeshIR{
            .vertices = {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = 2.0F, .y = 1.0F, .z = 0.0F},
                {.x = 1.0F, .y = 2.0F, .z = 2.0F},
            },
            .triangles = {{{.vertex_indices = {0U, 1U, 2U}}}},
        }},
    };
    auto tied_scene = omega::runtime::BuildSpatialDiagnosticScene(tied_axes);
    Check(tied_scene && NearlyEqual(tied_scene->render_meshes[0].positions[1].x, 0.8F) &&
              NearlyEqual(tied_scene->render_meshes[0].positions[1].y, 0.0F),
        "equal extents use the deterministic X-then-Y-then-Z axis tie-break");

    auto empty_scene = omega::runtime::BuildSpatialDiagnosticScene({});
    Check(empty_scene && empty_scene->render_meshes.empty() &&
              empty_scene->mesh_instances.empty() && IsIdentity(empty_scene->camera.world_to_clip),
        "an empty level produces an identity-camera scene without empty GPU-facing resources");
    omega::asset::LevelSpatialIR triangle_free{
        .terrain_cells = {omega::asset::SpatialMeshIR{
            .vertices = {{{.x = 1.0F, .y = 2.0F, .z = 3.0F}}},
        }},
    };
    auto triangle_free_scene = omega::runtime::BuildSpatialDiagnosticScene(triangle_free);
    Check(triangle_free_scene && triangle_free_scene->render_meshes.empty() &&
              triangle_free_scene->mesh_instances.empty(),
        "triangle-free cells are validated but do not force empty render meshes");

    auto nonfinite = original;
    nonfinite.terrain_cells[1].vertices.push_back(
        {.x = std::numeric_limits<float>::infinity(), .y = 0.0F, .z = 0.0F});
    Check(!omega::runtime::BuildSpatialDiagnosticScene(nonfinite),
        "non-finite coordinates are rejected even in a triangle-free cell");
    auto bad_index = omega::asset::LevelSpatialIR{.terrain_cells = {MakeTriangleMesh()}};
    bad_index.terrain_cells[0].triangles[0].vertex_indices[2] = 3U;
    Check(!omega::runtime::BuildSpatialDiagnosticScene(bad_index),
        "triangle indices are range checked before output allocation");

    omega::asset::LevelSpatialIR bounded{
        .terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh()},
    };
    auto exact = omega::runtime::SpatialDiagnosticSceneLimits{};
    auto below = exact;
    exact.maximum_cells = 2U;
    below.maximum_cells = 1U;
    CheckLimit(bounded, exact, below, "exact and one-below cell limits are enforced");

    exact = {};
    below = {};
    exact.maximum_positions = 6U;
    below.maximum_positions = 5U;
    CheckLimit(bounded, exact, below,
        "exact and one-below aggregate position limits are enforced");

    exact = {};
    below = {};
    exact.maximum_triangle_indices = 6U;
    below.maximum_triangle_indices = 5U;
    CheckLimit(bounded, exact, below,
        "exact and one-below aggregate triangle-index limits are enforced");

    constexpr std::uint64_t output_bytes = sizeof(omega::asset::SceneIR) +
                                           sizeof(omega::asset::RenderMeshIR) +
                                           sizeof(omega::asset::SceneMeshInstanceIR) +
                                           6U * sizeof(omega::asset::Float3IR) +
                                           6U * sizeof(std::uint32_t);
    exact = {};
    below = {};
    exact.maximum_output_bytes = output_bytes;
    below.maximum_output_bytes = output_bytes - 1U;
    CheckLimit(bounded, exact, below,
        "exact and one-below logical output-byte limits are enforced");

    return failures;
}
