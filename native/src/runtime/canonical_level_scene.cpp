#include "omega/runtime/canonical_level_scene.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace omega::runtime
{
namespace
{
constexpr std::uint64_t kIndicesPerTriangle = 3U;

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& output) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

[[nodiscard]] bool IsFiniteMatrix(const asset::Matrix4x4IR& matrix) noexcept
{
    for (const float element : matrix.row_major)
    {
        if (!std::isfinite(element))
            return false;
    }
    return true;
}

struct ScenePlan
{
    std::uint64_t output_positions = 0U;
    std::uint64_t output_triangle_indices = 0U;
    std::uint64_t logical_output_bytes = sizeof(CanonicalLevelScene);
};

[[nodiscard]] bool AreTightenedLimits(const CanonicalLevelSceneLimits& limits) noexcept
{
    const CanonicalLevelSceneLimits maxima;
    return limits.maximum_cells <= maxima.maximum_cells &&
           limits.maximum_positions <= maxima.maximum_positions &&
           limits.maximum_triangle_indices <= maxima.maximum_triangle_indices &&
           limits.maximum_output_bytes <= maxima.maximum_output_bytes;
}

[[nodiscard]] std::expected<ScenePlan, std::string> Preflight(
    const asset::LevelSpatialIR& spatial, const CanonicalLevelSceneLimits& limits)
{
    if (!AreTightenedLimits(limits))
        return std::unexpected("canonical level scene limits may only tighten safety maxima");

    const std::uint64_t cell_count = static_cast<std::uint64_t>(spatial.terrain_cells.size());
    if (cell_count > limits.maximum_cells)
        return std::unexpected("canonical level scene exceeds the cell limit");
    if (cell_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
        return std::unexpected(
            "canonical level scene cell count overflows the source ordinal type");

    ScenePlan plan;
    std::uint64_t cell_object_bytes = 0U;
    if (!Multiply(cell_count, sizeof(CanonicalLevelSceneCell), cell_object_bytes) ||
        !Add(plan.logical_output_bytes, cell_object_bytes, plan.logical_output_bytes))
        return std::unexpected("canonical level scene output byte size overflows");

    for (const asset::SpatialMeshIR& mesh : spatial.terrain_cells)
    {
        const std::uint64_t vertex_count = static_cast<std::uint64_t>(mesh.vertices.size());
        if (vertex_count > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
            return std::unexpected("canonical level scene cell vertex count overflows");
        if (!Add(plan.output_positions, vertex_count, plan.output_positions))
            return std::unexpected("canonical level scene position count overflows");
        if (plan.output_positions > limits.maximum_positions)
            return std::unexpected("canonical level scene exceeds the position limit");

        for (const asset::Float3IR& vertex : mesh.vertices)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) || !std::isfinite(vertex.z))
                return std::unexpected("canonical level scene requires finite vertex coordinates");
        }

        std::uint64_t cell_triangle_indices = 0U;
        if (!Multiply(static_cast<std::uint64_t>(mesh.triangles.size()), kIndicesPerTriangle,
                      cell_triangle_indices) ||
            !Add(plan.output_triangle_indices, cell_triangle_indices, plan.output_triangle_indices))
            return std::unexpected("canonical level scene triangle-index count overflows");
        if (plan.output_triangle_indices > limits.maximum_triangle_indices)
            return std::unexpected("canonical level scene exceeds the triangle-index limit");

        for (const asset::SpatialTriangleIR& triangle : mesh.triangles)
        {
            for (const std::uint32_t index : triangle.vertex_indices)
            {
                if (index >= mesh.vertices.size())
                    return std::unexpected("canonical level scene triangle index is out of range");
            }
        }
    }

    if (plan.output_positions >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        plan.output_triangle_indices >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return std::unexpected("canonical level scene exceeds host container capacity");

    std::uint64_t position_bytes = 0U;
    std::uint64_t index_bytes = 0U;
    if (!Multiply(plan.output_positions, sizeof(asset::Float3IR), position_bytes) ||
        !Multiply(plan.output_triangle_indices, sizeof(std::uint32_t), index_bytes) ||
        !Add(plan.logical_output_bytes, position_bytes, plan.logical_output_bytes) ||
        !Add(plan.logical_output_bytes, index_bytes, plan.logical_output_bytes))
        return std::unexpected("canonical level scene output byte size overflows");
    if (plan.logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected("canonical level scene exceeds the output-byte limit");

    return plan;
}
} // namespace

std::expected<CanonicalLevelScene, std::string> BuildCanonicalLevelScene(
    const asset::LevelSpatialIR& spatial, const CanonicalLevelSceneLimits& limits)
{
    auto planned = Preflight(spatial, limits);
    if (!planned)
        return std::unexpected(planned.error());

    CanonicalLevelScene scene;
    if (!IsFiniteMatrix(scene.camera.world_to_view) || !IsFiniteMatrix(scene.camera.view_to_clip))
        return std::unexpected("canonical level scene camera is not finite");
    if (spatial.terrain_cells.empty())
        return scene;

    try
    {
        scene.cells.reserve(spatial.terrain_cells.size());
        for (std::size_t cell_index = 0U; cell_index < spatial.terrain_cells.size(); ++cell_index)
        {
            const asset::SpatialMeshIR& mesh = spatial.terrain_cells[cell_index];

            asset::RenderMeshIR render_mesh;
            render_mesh.positions.reserve(mesh.vertices.size());
            for (const asset::Float3IR& vertex : mesh.vertices)
                render_mesh.positions.push_back(vertex);

            render_mesh.triangle_indices.reserve(mesh.triangles.size() * kIndicesPerTriangle);
            for (const asset::SpatialTriangleIR& triangle : mesh.triangles)
            {
                for (const std::uint32_t index : triangle.vertex_indices)
                    render_mesh.triangle_indices.push_back(index);
            }

            CanonicalLevelSceneCell cell{
                .source_cell_ordinal =
                    SourceCellOrdinal{.value = static_cast<std::uint32_t>(cell_index)},
                .render_mesh = std::move(render_mesh),
                .local_to_world = asset::kIdentityMatrix4x4IR,
            };
            if (!IsFiniteMatrix(cell.local_to_world))
                return std::unexpected("canonical level scene cell transform is not finite");
            scene.cells.push_back(std::move(cell));
        }
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("canonical level scene allocation failed");
    }
    catch (const std::length_error&)
    {
        return std::unexpected("canonical level scene exceeds host container capacity");
    }
    return scene;
}
} // namespace omega::runtime
