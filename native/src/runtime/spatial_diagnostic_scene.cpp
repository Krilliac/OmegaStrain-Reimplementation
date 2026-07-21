#include "omega/runtime/spatial_diagnostic_scene.h"

#include <algorithm>
#include <array>
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
constexpr double kClipMinimum = -1.0;
constexpr double kClipSpan = 2.0;
constexpr double kTileInsetFraction = 0.1;
constexpr float kDiagnosticDepth = 0.5F;
constexpr std::uint64_t kIndicesPerTriangle = 3U;

struct ScenePlan
{
    std::uint32_t side_tiles = 1U;
    std::uint64_t output_positions = 0U;
    std::uint64_t output_triangle_indices = 0U;
    std::uint64_t logical_output_bytes = sizeof(asset::SceneIR);

    [[nodiscard]] bool has_geometry() const noexcept
    {
        return output_triangle_indices != 0U;
    }
};

struct Projection
{
    std::array<std::size_t, 2> axes{};
    std::array<double, 2> minimum{};
    std::array<double, 2> offsets{};
    double scale = 0.0;
    double tile_left = 0.0;
    double tile_bottom = 0.0;
    double tile_inset = 0.0;
    double usable_span = 0.0;
};

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

[[nodiscard]] std::uint64_t CeilSquareRoot(const std::uint64_t value) noexcept
{
    std::uint64_t low = 1U;
    std::uint64_t high = value;
    while (low < high)
    {
        const std::uint64_t middle = low + (high - low) / 2U;
        const std::uint64_t quotient = value / middle;
        const std::uint64_t needed = quotient + (value % middle != 0U ? 1U : 0U);
        if (middle >= needed)
            high = middle;
        else
            low = middle + 1U;
    }
    return low;
}

[[nodiscard]] double Coordinate(const asset::Float3IR& vertex, const std::size_t axis) noexcept
{
    if (axis == 0U)
        return vertex.x;
    if (axis == 1U)
        return vertex.y;
    return vertex.z;
}

[[nodiscard]] std::expected<ScenePlan, std::string> Preflight(
    const asset::LevelSpatialIR& spatial, const SpatialDiagnosticSceneLimits& limits)
{
    const std::uint64_t cell_count = static_cast<std::uint64_t>(spatial.terrain_cells.size());
    if (cell_count > limits.maximum_cells)
        return std::unexpected("spatial diagnostic scene exceeds the cell limit");

    ScenePlan plan;
    const std::uint64_t displayed_cells = std::max<std::uint64_t>(1U, cell_count);
    const std::uint64_t side_tiles = CeilSquareRoot(displayed_cells);
    if (side_tiles > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected("spatial diagnostic scene tile dimensions overflow");
    plan.side_tiles = static_cast<std::uint32_t>(side_tiles);

    std::uint64_t inspected_positions = 0U;
    for (const asset::SpatialMeshIR& mesh : spatial.terrain_cells)
    {
        if (!Add(inspected_positions, static_cast<std::uint64_t>(mesh.vertices.size()),
                inspected_positions))
            return std::unexpected("spatial diagnostic scene position count overflows");
        if (inspected_positions > limits.maximum_positions)
            return std::unexpected("spatial diagnostic scene exceeds the position limit");

        for (const asset::Float3IR& vertex : mesh.vertices)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                !std::isfinite(vertex.z))
                return std::unexpected(
                    "spatial diagnostic scene requires finite vertex coordinates");
        }

        std::uint64_t cell_triangle_indices = 0U;
        if (!Multiply(static_cast<std::uint64_t>(mesh.triangles.size()),
                kIndicesPerTriangle, cell_triangle_indices) ||
            !Add(plan.output_triangle_indices, cell_triangle_indices,
                plan.output_triangle_indices))
            return std::unexpected("spatial diagnostic scene triangle-index count overflows");
        if (plan.output_triangle_indices > limits.maximum_triangle_indices)
            return std::unexpected(
                "spatial diagnostic scene exceeds the triangle-index limit");

        const std::uint64_t position_base = plan.output_positions;
        for (const asset::SpatialTriangleIR& triangle : mesh.triangles)
        {
            for (const std::uint32_t index : triangle.vertex_indices)
            {
                if (index >= mesh.vertices.size())
                    return std::unexpected(
                        "spatial diagnostic scene triangle index is out of range");
                if (position_base >
                    static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) -
                        index)
                    return std::unexpected(
                        "spatial diagnostic scene rebased triangle index overflows");
            }
        }

        if (!mesh.triangles.empty() &&
            !Add(plan.output_positions, static_cast<std::uint64_t>(mesh.vertices.size()),
                plan.output_positions))
            return std::unexpected("spatial diagnostic scene output position count overflows");
    }

    if (plan.output_positions > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        plan.output_triangle_indices >
            static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
        return std::unexpected("spatial diagnostic scene exceeds host container capacity");

    if (plan.has_geometry())
    {
        if (!Add(plan.logical_output_bytes, sizeof(asset::RenderMeshIR),
                plan.logical_output_bytes) ||
            !Add(plan.logical_output_bytes, sizeof(asset::SceneMeshInstanceIR),
                plan.logical_output_bytes))
            return std::unexpected("spatial diagnostic scene output byte size overflows");

        std::uint64_t position_bytes = 0U;
        std::uint64_t index_bytes = 0U;
        if (!Multiply(plan.output_positions, sizeof(asset::Float3IR), position_bytes) ||
            !Multiply(plan.output_triangle_indices, sizeof(std::uint32_t), index_bytes) ||
            !Add(plan.logical_output_bytes, position_bytes, plan.logical_output_bytes) ||
            !Add(plan.logical_output_bytes, index_bytes, plan.logical_output_bytes))
            return std::unexpected("spatial diagnostic scene output byte size overflows");
    }
    if (plan.logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected("spatial diagnostic scene exceeds the output-byte limit");
    return plan;
}

[[nodiscard]] Projection MakeProjection(const asset::SpatialMeshIR& mesh,
    const std::size_t cell_index, const std::uint32_t side_tiles) noexcept
{
    std::array minimum{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array maximum{-std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    for (const asset::Float3IR& vertex : mesh.vertices)
    {
        for (std::size_t axis = 0U; axis < minimum.size(); ++axis)
        {
            const double value = Coordinate(vertex, axis);
            minimum[axis] = std::min(minimum[axis], value);
            maximum[axis] = std::max(maximum[axis], value);
        }
    }

    std::array<double, 3> extents{};
    for (std::size_t axis = 0U; axis < extents.size(); ++axis)
        extents[axis] = maximum[axis] - minimum[axis];
    std::array<std::size_t, 3> axes{0U, 1U, 2U};
    for (std::size_t left = 0U; left < axes.size(); ++left)
    {
        std::size_t best = left;
        for (std::size_t right = left + 1U; right < axes.size(); ++right)
        {
            if (extents[axes[right]] > extents[axes[best]] ||
                (extents[axes[right]] == extents[axes[best]] && axes[right] < axes[best]))
                best = right;
        }
        if (best != left)
            std::swap(axes[left], axes[best]);
    }

    const double tile_span = kClipSpan / static_cast<double>(side_tiles);
    const double tile_inset = tile_span * kTileInsetFraction;
    const double usable_span = tile_span - 2.0 * tile_inset;
    const std::uint64_t column = static_cast<std::uint64_t>(cell_index) % side_tiles;
    const std::uint64_t row = static_cast<std::uint64_t>(cell_index) / side_tiles;
    Projection projection{
        .axes = {axes[0], axes[1]},
        .minimum = {minimum[axes[0]], minimum[axes[1]]},
        .tile_left = kClipMinimum + static_cast<double>(column) * tile_span,
        .tile_bottom = 1.0 - static_cast<double>(row + 1U) * tile_span,
        .tile_inset = tile_inset,
        .usable_span = usable_span,
    };
    const std::array projected_extents{extents[axes[0]], extents[axes[1]]};
    const double largest_extent = std::max(projected_extents[0], projected_extents[1]);
    if (largest_extent > 0.0)
    {
        projection.scale = usable_span / largest_extent;
        for (std::size_t axis = 0U; axis < projection.offsets.size(); ++axis)
        {
            projection.offsets[axis] =
                (usable_span - projected_extents[axis] * projection.scale) * 0.5;
        }
    }
    else
    {
        projection.offsets = {usable_span * 0.5, usable_span * 0.5};
    }
    return projection;
}

[[nodiscard]] asset::Float3IR Project(
    const asset::Float3IR& vertex, const Projection& projection) noexcept
{
    std::array<double, 2> fitted{};
    for (std::size_t axis = 0U; axis < fitted.size(); ++axis)
    {
        fitted[axis] = projection.offsets[axis] +
                       (Coordinate(vertex, projection.axes[axis]) -
                           projection.minimum[axis]) *
                           projection.scale;
        fitted[axis] = std::clamp(fitted[axis], 0.0, projection.usable_span);
    }
    return asset::Float3IR{
        .x = static_cast<float>(projection.tile_left + projection.tile_inset + fitted[0]),
        .y = static_cast<float>(projection.tile_bottom + projection.tile_inset + fitted[1]),
        .z = kDiagnosticDepth,
    };
}
} // namespace

std::expected<asset::SceneIR, std::string> BuildSpatialDiagnosticScene(
    const asset::LevelSpatialIR& spatial, const SpatialDiagnosticSceneLimits& limits)
{
    auto planned = Preflight(spatial, limits);
    if (!planned)
        return std::unexpected(planned.error());

    asset::SceneIR scene;
    if (!planned->has_geometry())
        return scene;

    try
    {
        asset::RenderMeshIR render_mesh;
        render_mesh.positions.reserve(static_cast<std::size_t>(planned->output_positions));
        render_mesh.triangle_indices.reserve(
            static_cast<std::size_t>(planned->output_triangle_indices));

        for (std::size_t cell_index = 0U; cell_index < spatial.terrain_cells.size();
             ++cell_index)
        {
            const asset::SpatialMeshIR& mesh = spatial.terrain_cells[cell_index];
            if (mesh.triangles.empty())
                continue;

            const Projection projection = MakeProjection(mesh, cell_index, planned->side_tiles);
            const auto position_base = static_cast<std::uint32_t>(render_mesh.positions.size());
            for (const asset::Float3IR& vertex : mesh.vertices)
                render_mesh.positions.push_back(Project(vertex, projection));
            for (const asset::SpatialTriangleIR& triangle : mesh.triangles)
            {
                for (const std::uint32_t index : triangle.vertex_indices)
                    render_mesh.triangle_indices.push_back(position_base + index);
            }
        }

        scene.render_meshes.push_back(std::move(render_mesh));
        scene.mesh_instances.push_back(asset::SceneMeshInstanceIR{
            .render_mesh_index = 0U,
            .local_to_world = asset::kIdentityMatrix4x4IR,
        });
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("spatial diagnostic scene allocation failed");
    }
    catch (const std::length_error&)
    {
        return std::unexpected("spatial diagnostic scene exceeds host container capacity");
    }
    return scene;
}
} // namespace omega::runtime
