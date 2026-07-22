#include "omega/runtime/scene_structure_snapshot.h"

#include <cstdint>
#include <iostream>
#include <string_view>

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

[[nodiscard]] omega::asset::RenderMeshIR MakeTriangleMesh(const float scale = 1.0F)
{
    return omega::asset::RenderMeshIR{
        .positions =
            {
                {.x = 0.0F, .y = 0.0F, .z = 0.0F},
                {.x = scale, .y = 0.0F, .z = 0.0F},
                {.x = 0.0F, .y = scale, .z = 0.0F},
            },
        .triangle_indices = {0U, 1U, 2U},
    };
}

[[nodiscard]] omega::asset::SceneIR MakeScene()
{
    omega::asset::SceneIR scene;
    scene.render_meshes = {MakeTriangleMesh(), {}, MakeTriangleMesh(2.0F)};
    scene.mesh_instances = {
        {.render_mesh_index = 0U},
        {.render_mesh_index = 1U},
        {.render_mesh_index = 2U},
    };
    return scene;
}
} // namespace

int main()
{
    auto empty_snapshot = omega::runtime::BuildSceneStructureSnapshot({});
    Check(
        empty_snapshot &&
            empty_snapshot->schema_version ==
                omega::runtime::kSceneStructureSnapshotSchemaVersion &&
            empty_snapshot->render_mesh_count == 0U && empty_snapshot->mesh_instance_count == 0U &&
            empty_snapshot->total_position_count == 0U &&
            empty_snapshot->total_triangle_index_count == 0U && empty_snapshot->camera_is_identity,
        "an empty scene produces a versioned all-zero aggregate with an "
        "identity camera flag");

    auto scene = MakeScene();
    auto first = omega::runtime::BuildSceneStructureSnapshot(scene);
    auto second = omega::runtime::BuildSceneStructureSnapshot(scene);
    Check(first.has_value() && second.has_value() && *first == *second,
          "repeated aggregate snapshots of one scene are deterministic");
    Check(first && first->render_mesh_count == 3U && first->mesh_instance_count == 3U &&
              first->total_position_count == 6U && first->total_triangle_index_count == 6U &&
              first->camera_is_identity,
          "snapshot aggregates count every bounded scene collection");

    auto changed_coordinates = scene;
    changed_coordinates.render_meshes[2].positions[1].x = 99.0F;
    auto changed_snapshot = omega::runtime::BuildSceneStructureSnapshot(changed_coordinates);
    Check(first && changed_snapshot && *first == *changed_snapshot,
          "aggregate equality deliberately does not claim coordinate-level scene "
          "equality");

    auto non_identity_camera = scene;
    non_identity_camera.camera.world_to_view.row_major[0] = 2.0F;
    auto non_identity_snapshot = omega::runtime::BuildSceneStructureSnapshot(non_identity_camera);
    Check(non_identity_snapshot && !non_identity_snapshot->camera_is_identity,
          "a non-identity camera changes only the aggregate identity flag");

    auto limits = omega::runtime::SceneStructureSnapshotLimits{};
    limits.maximum_render_meshes = 3U;
    Check(omega::runtime::BuildSceneStructureSnapshot(scene, limits).has_value(),
          "the exact render-mesh work limit succeeds");
    limits.maximum_render_meshes = 2U;
    Check(!omega::runtime::BuildSceneStructureSnapshot(scene, limits),
          "one below the render-mesh work limit fails");

    limits = {};
    limits.maximum_mesh_instances = 2U;
    Check(!omega::runtime::BuildSceneStructureSnapshot(scene, limits),
          "the mesh-instance traversal limit is enforced before traversal");

    limits = {};
    limits.maximum_positions = 6U;
    Check(omega::runtime::BuildSceneStructureSnapshot(scene, limits).has_value(),
          "the exact aggregate position work limit succeeds");
    limits.maximum_positions = 5U;
    Check(!omega::runtime::BuildSceneStructureSnapshot(scene, limits),
          "one below the aggregate position work limit fails");

    limits = {};
    limits.maximum_triangle_indices = 6U;
    Check(omega::runtime::BuildSceneStructureSnapshot(scene, limits).has_value(),
          "the exact aggregate triangle-index work limit succeeds");
    limits.maximum_triangle_indices = 5U;
    Check(!omega::runtime::BuildSceneStructureSnapshot(scene, limits),
          "one below the aggregate triangle-index work limit fails");

    limits = {};
    limits.maximum_output_bytes = sizeof(omega::runtime::SceneStructureSnapshot);
    Check(omega::runtime::BuildSceneStructureSnapshot(scene, limits).has_value(),
          "the exact constant output-byte limit succeeds");
    --limits.maximum_output_bytes;
    Check(!omega::runtime::BuildSceneStructureSnapshot(scene, limits),
          "one below the constant output-byte limit fails");

    limits = {};
    ++limits.maximum_positions;
    Check(!omega::runtime::BuildSceneStructureSnapshot(scene, limits),
          "callers cannot widen snapshot safety maxima");

    auto invalid_reference = scene;
    invalid_reference.mesh_instances[2].render_mesh_index = 3U;
    Check(!omega::runtime::BuildSceneStructureSnapshot(invalid_reference),
          "an unavailable renderer mesh reference fails closed");

    return failures;
}
