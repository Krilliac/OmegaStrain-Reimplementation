#include "omega/runtime/scene_structure_snapshot.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>

namespace omega::runtime
{
namespace
{
constexpr std::uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr std::uint64_t kFnvPrime = 0x100000001b3ULL;

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& output) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

void FoldByte(std::uint64_t& digest, const std::uint8_t value) noexcept
{
    digest ^= static_cast<std::uint64_t>(value);
    digest *= kFnvPrime;
}

void FoldU32(std::uint64_t& digest, const std::uint32_t value) noexcept
{
    FoldByte(digest, static_cast<std::uint8_t>(value));
    FoldByte(digest, static_cast<std::uint8_t>(value >> 8U));
    FoldByte(digest, static_cast<std::uint8_t>(value >> 16U));
    FoldByte(digest, static_cast<std::uint8_t>(value >> 24U));
}

void FoldU64(std::uint64_t& digest, const std::uint64_t value) noexcept
{
    FoldU32(digest, static_cast<std::uint32_t>(value));
    FoldU32(digest, static_cast<std::uint32_t>(value >> 32U));
}

void FoldFloat(std::uint64_t& digest, const float value) noexcept
{
    FoldU32(digest, std::bit_cast<std::uint32_t>(value));
}

void FoldFloat3(std::uint64_t& digest, const asset::Float3IR& value) noexcept
{
    FoldFloat(digest, value.x);
    FoldFloat(digest, value.y);
    FoldFloat(digest, value.z);
}

void FoldMatrix(std::uint64_t& digest, const asset::Matrix4x4IR& value) noexcept
{
    for (const float element : value.row_major)
        FoldFloat(digest, element);
}
} // namespace

std::expected<SceneStructureSnapshot, std::string> BuildSceneStructureSnapshot(
    const asset::SceneIR& scene, const SceneStructureSnapshotLimits& limits)
{
    const std::uint64_t render_mesh_count =
        static_cast<std::uint64_t>(scene.render_meshes.size());
    if (render_mesh_count > limits.maximum_render_meshes)
        return std::unexpected("scene structure snapshot exceeds the render-mesh limit");

    std::uint64_t logical_output_bytes = sizeof(SceneStructureSnapshot);
    std::uint64_t mesh_scalar_bytes = 0U;
    if (!Multiply(render_mesh_count, 2U * sizeof(std::uint64_t), mesh_scalar_bytes) ||
        !Add(logical_output_bytes, mesh_scalar_bytes, logical_output_bytes))
        return std::unexpected("scene structure snapshot output byte size overflows");

    const std::uint64_t instance_count =
        static_cast<std::uint64_t>(scene.mesh_instances.size());
    std::uint64_t instance_bytes = 0U;
    if (!Multiply(instance_count, sizeof(std::uint32_t), instance_bytes) ||
        !Add(logical_output_bytes, instance_bytes, logical_output_bytes))
        return std::unexpected("scene structure snapshot output byte size overflows");
    if (logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected("scene structure snapshot exceeds the output-byte limit");

    SceneStructureSnapshot snapshot;
    snapshot.camera_is_identity = scene.camera.world_to_view == asset::kIdentityMatrix4x4IR &&
                                  scene.camera.view_to_clip == asset::kIdentityMatrix4x4IR;

    std::uint64_t digest = kFnvOffsetBasis;
    FoldU64(digest, render_mesh_count);
    FoldU64(digest, instance_count);

    try
    {
        snapshot.render_mesh_position_counts.reserve(scene.render_meshes.size());
        snapshot.render_mesh_triangle_index_counts.reserve(scene.render_meshes.size());
        for (const asset::RenderMeshIR& mesh : scene.render_meshes)
        {
            const auto position_count = static_cast<std::uint64_t>(mesh.positions.size());
            const auto index_count = static_cast<std::uint64_t>(mesh.triangle_indices.size());
            if (!Add(snapshot.total_position_count, position_count,
                    snapshot.total_position_count) ||
                !Add(snapshot.total_triangle_index_count, index_count,
                    snapshot.total_triangle_index_count))
                return std::unexpected("scene structure snapshot aggregate count overflows");

            snapshot.render_mesh_position_counts.push_back(position_count);
            snapshot.render_mesh_triangle_index_counts.push_back(index_count);

            FoldU64(digest, position_count);
            for (const asset::Float3IR& position : mesh.positions)
                FoldFloat3(digest, position);
            FoldU64(digest, index_count);
            for (const std::uint32_t index : mesh.triangle_indices)
                FoldU32(digest, index);
        }

        snapshot.mesh_instance_render_mesh_indices.reserve(scene.mesh_instances.size());
        for (const asset::SceneMeshInstanceIR& instance : scene.mesh_instances)
        {
            snapshot.mesh_instance_render_mesh_indices.push_back(instance.render_mesh_index);
            FoldU32(digest, instance.render_mesh_index);
            FoldMatrix(digest, instance.local_to_world);
        }
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("scene structure snapshot allocation failed");
    }
    catch (const std::length_error&)
    {
        return std::unexpected("scene structure snapshot exceeds host container capacity");
    }

    FoldMatrix(digest, scene.camera.world_to_view);
    FoldMatrix(digest, scene.camera.view_to_clip);

    snapshot.render_mesh_count = render_mesh_count;
    snapshot.mesh_instance_count = instance_count;
    snapshot.structure_digest = digest;
    return snapshot;
}
} // namespace omega::runtime
