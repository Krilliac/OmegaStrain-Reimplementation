#include "omega/runtime/scene_structure_snapshot.h"

#include "omega/runtime/canonical_level_scene.h"

#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
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
} // namespace

int main()
{
    // Empty scene.
    auto empty_snapshot = omega::runtime::BuildSceneStructureSnapshot({});
    Check(empty_snapshot && empty_snapshot->render_mesh_count == 0U &&
              empty_snapshot->mesh_instance_count == 0U &&
              empty_snapshot->total_position_count == 0U &&
              empty_snapshot->total_triangle_index_count == 0U &&
              empty_snapshot->render_mesh_position_counts.empty() &&
              empty_snapshot->render_mesh_triangle_index_counts.empty() &&
              empty_snapshot->mesh_instance_render_mesh_indices.empty() &&
              empty_snapshot->camera_is_identity,
        "an empty scene produces an all-zero snapshot with an identity camera flag");

    // Determinism: two snapshots of the same scene are equal, including the digest.
    omega::asset::LevelSpatialIR spatial{
        .terrain_cells = {MakeTriangleMesh(), omega::asset::SpatialMeshIR{}, MakeTriangleMesh(2.0F)},
    };
    auto scene = omega::runtime::BuildCanonicalLevelScene(spatial);
    Check(scene.has_value(), "canonical scene builds for the snapshot fixture");
    if (scene)
    {
        auto first = omega::runtime::BuildSceneStructureSnapshot(*scene);
        auto second = omega::runtime::BuildSceneStructureSnapshot(*scene);
        Check(first.has_value() && second.has_value() && *first == *second,
            "repeated snapshots of the same scene are identical, including the digest");
        Check(first && first->render_mesh_count == 3U && first->mesh_instance_count == 3U,
            "snapshot counts mirror the canonical scene's per-cell cardinality");
        Check(first &&
                  first->render_mesh_position_counts == std::vector<std::uint64_t>({3U, 0U, 3U}),
            "per-mesh position counts follow source order exactly");
        Check(first &&
                  first->render_mesh_triangle_index_counts ==
                      std::vector<std::uint64_t>({3U, 0U, 3U}),
            "per-mesh triangle-index counts follow source order exactly");
        Check(first &&
                  first->mesh_instance_render_mesh_indices ==
                      std::vector<std::uint32_t>({0U, 1U, 2U}),
            "per-instance render-mesh indices follow source order exactly");
        Check(first && first->total_position_count == 6U &&
                  first->total_triangle_index_count == 6U,
            "aggregate counts sum every mesh");
        Check(first && first->camera_is_identity, "the canonical scene's fixed camera is identity");

        // Sensitivity: a differently-scaled cell changes the digest despite identical counts.
        omega::asset::LevelSpatialIR rescaled{
            .terrain_cells = {
                MakeTriangleMesh(), omega::asset::SpatialMeshIR{}, MakeTriangleMesh(3.0F)},
        };
        auto rescaled_scene = omega::runtime::BuildCanonicalLevelScene(rescaled);
        auto rescaled_snapshot =
            rescaled_scene ? omega::runtime::BuildSceneStructureSnapshot(*rescaled_scene)
                           : std::expected<omega::runtime::SceneStructureSnapshot, std::string>{};
        Check(rescaled_snapshot.has_value() && first &&
                  rescaled_snapshot->render_mesh_position_counts ==
                      first->render_mesh_position_counts &&
                  rescaled_snapshot->structure_digest != first->structure_digest,
            "identical counts with different coordinates produce a different digest");
    }

    // A non-identity camera is observable through the flag without exposing raw matrix bytes.
    omega::asset::SceneIR non_identity_camera_scene;
    non_identity_camera_scene.camera.world_to_view.row_major[0] = 2.0F;
    auto non_identity_snapshot =
        omega::runtime::BuildSceneStructureSnapshot(non_identity_camera_scene);
    Check(non_identity_snapshot && !non_identity_snapshot->camera_is_identity,
        "a non-identity camera is reflected in the identity flag");

    // Resource limits, exact and one below.
    omega::asset::LevelSpatialIR bounded{
        .terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh()},
    };
    auto bounded_scene = omega::runtime::BuildCanonicalLevelScene(bounded);
    Check(bounded_scene.has_value(), "canonical scene builds for the limits fixture");
    if (bounded_scene)
    {
        auto exact = omega::runtime::SceneStructureSnapshotLimits{};
        exact.maximum_render_meshes = 2U;
        auto below = exact;
        below.maximum_render_meshes = 1U;
        Check(omega::runtime::BuildSceneStructureSnapshot(*bounded_scene, exact).has_value() &&
                  !omega::runtime::BuildSceneStructureSnapshot(*bounded_scene, below),
            "exact and one-below render-mesh limits are enforced");

        auto exact_output = omega::runtime::SceneStructureSnapshotLimits{};
        auto snapshot_for_size = omega::runtime::BuildSceneStructureSnapshot(*bounded_scene);
        Check(snapshot_for_size.has_value(), "an unbounded snapshot succeeds for size discovery");
        if (snapshot_for_size)
        {
            const std::uint64_t exact_bytes = sizeof(omega::runtime::SceneStructureSnapshot) +
                2U * (2U * sizeof(std::uint64_t)) + 2U * sizeof(std::uint32_t);
            exact_output.maximum_output_bytes = exact_bytes;
            auto below_output = exact_output;
            below_output.maximum_output_bytes = exact_bytes - 1U;
            Check(omega::runtime::BuildSceneStructureSnapshot(*bounded_scene, exact_output)
                          .has_value() &&
                      !omega::runtime::BuildSceneStructureSnapshot(*bounded_scene, below_output),
                "exact and one-below logical output-byte limits are enforced");
        }
    }

    return failures;
}
