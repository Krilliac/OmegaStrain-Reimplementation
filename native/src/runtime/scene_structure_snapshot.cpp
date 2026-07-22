#include "omega/runtime/scene_structure_snapshot.h"

#include <cstdint>
#include <limits>

namespace omega::runtime
{
namespace
{
[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& output) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool AreTightenedLimits(const SceneStructureSnapshotLimits& limits) noexcept
{
    const SceneStructureSnapshotLimits maxima;
    return limits.maximum_render_meshes <= maxima.maximum_render_meshes &&
           limits.maximum_mesh_instances <= maxima.maximum_mesh_instances &&
           limits.maximum_positions <= maxima.maximum_positions &&
           limits.maximum_triangle_indices <= maxima.maximum_triangle_indices &&
           limits.maximum_output_bytes <= maxima.maximum_output_bytes;
}
} // namespace

std::expected<SceneStructureSnapshot, std::string> BuildSceneStructureSnapshot(
    const asset::SceneIR& scene, const SceneStructureSnapshotLimits& limits)
{
    if (!AreTightenedLimits(limits))
        return std::unexpected("scene structure snapshot limits may only tighten safety maxima");
    if (sizeof(SceneStructureSnapshot) > limits.maximum_output_bytes)
        return std::unexpected("scene structure snapshot exceeds the output-byte limit");

    const std::uint64_t render_mesh_count = static_cast<std::uint64_t>(scene.render_meshes.size());
    if (render_mesh_count > limits.maximum_render_meshes)
        return std::unexpected("scene structure snapshot exceeds the render-mesh limit");

    const std::uint64_t instance_count = static_cast<std::uint64_t>(scene.mesh_instances.size());
    if (instance_count > limits.maximum_mesh_instances)
        return std::unexpected("scene structure snapshot exceeds the mesh-instance limit");

    SceneStructureSnapshot snapshot;
    snapshot.render_mesh_count = render_mesh_count;
    snapshot.mesh_instance_count = instance_count;
    for (const asset::RenderMeshIR& mesh : scene.render_meshes)
    {
        if (!Add(snapshot.total_position_count, mesh.positions.size(),
                 snapshot.total_position_count))
            return std::unexpected("scene structure snapshot position count overflows");
        if (snapshot.total_position_count > limits.maximum_positions)
            return std::unexpected("scene structure snapshot exceeds the position limit");

        if (!Add(snapshot.total_triangle_index_count, mesh.triangle_indices.size(),
                 snapshot.total_triangle_index_count))
            return std::unexpected("scene structure snapshot triangle-index count overflows");
        if (snapshot.total_triangle_index_count > limits.maximum_triangle_indices)
            return std::unexpected("scene structure snapshot exceeds the triangle-index limit");
    }

    // Instance traversal is bounded above before this loop. A snapshot reports
    // aggregate shape only; it deliberately does not publish the source-order
    // references or transform bit patterns.
    for (const asset::SceneMeshInstanceIR& instance : scene.mesh_instances)
    {
        if (instance.render_mesh_index >= scene.render_meshes.size())
            return std::unexpected("scene structure snapshot instance reference is invalid");
    }

    snapshot.camera_is_identity = scene.camera.world_to_view == asset::kIdentityMatrix4x4IR &&
                                  scene.camera.view_to_clip == asset::kIdentityMatrix4x4IR;
    return snapshot;
}
} // namespace omega::runtime
