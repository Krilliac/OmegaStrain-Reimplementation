#include "omega/runtime/canonical_level_view_camera.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace omega::runtime
{
namespace
{
// Project clip-volume framing constants. They mirror the accepted global spatial
// diagnostic projection so the same decoded union fills the same clip extent, and
// they carry no retail camera, projection, or depth-range meaning.
constexpr double kClipSpan = 2.0;
constexpr double kClipDepthSpan = 1.0;
constexpr double kDiagnosticInsetFraction = 0.1;
constexpr double kDiagnosticDepth = 0.5;
constexpr std::size_t kAxisCount = 3U;

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& output) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    output = left + right;
    return true;
}

[[nodiscard]] bool AreTightenedLimits(const CanonicalLevelViewCameraLimits& limits) noexcept
{
    const CanonicalLevelViewCameraLimits maxima;
    return limits.maximum_cells <= maxima.maximum_cells &&
           limits.maximum_positions <= maxima.maximum_positions &&
           limits.maximum_triangle_indices <= maxima.maximum_triangle_indices;
}

[[nodiscard]] double Coordinate(const asset::Float3IR& position, const std::size_t axis) noexcept
{
    if (axis == 0U)
        return position.x;
    if (axis == 1U)
        return position.y;
    return position.z;
}

[[nodiscard]] bool NarrowToFloat(const double value, float& output) noexcept
{
    if (!std::isfinite(value))
        return false;
    if (std::abs(value) > static_cast<double>(std::numeric_limits<float>::max()))
        return false;
    output = static_cast<float>(value);
    return true;
}

struct CoordinateBounds
{
    std::array<double, kAxisCount> minimum{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double, kAxisCount> maximum{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
};

void Include(CoordinateBounds& bounds, const asset::Float3IR& position) noexcept
{
    for (std::size_t axis = 0U; axis < kAxisCount; ++axis)
    {
        const double value = Coordinate(position, axis);
        bounds.minimum[axis] = std::min(bounds.minimum[axis], value);
        bounds.maximum[axis] = std::max(bounds.maximum[axis], value);
    }
}

// Establishes the ADR 0005 order: the two largest extents lead, with equal
// extents broken in X, then Y, then Z order. The remaining axis becomes depth.
[[nodiscard]] std::array<std::size_t, kAxisCount> SelectAxes(
    const std::array<double, kAxisCount>& extents) noexcept
{
    std::array<std::size_t, kAxisCount> axes{0U, 1U, 2U};
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
    return axes;
}
} // namespace

std::expected<CanonicalLevelViewCamera, std::string> BuildCanonicalLevelDiagnosticViewCamera(
    const CanonicalLevelScene& canonical, const CanonicalLevelViewCameraLimits& limits)
{
    if (!AreTightenedLimits(limits))
        return std::unexpected("canonical level view camera limits may only tighten safety maxima");
    if (static_cast<std::uint64_t>(canonical.cells.size()) > limits.maximum_cells)
        return std::unexpected("canonical level view camera exceeds the source-cell limit");

    CanonicalLevelViewCamera view;
    CoordinateBounds bounds;
    std::uint64_t inspected_positions = 0U;
    std::uint64_t inspected_triangle_indices = 0U;
    for (std::size_t cell_index = 0U; cell_index < canonical.cells.size(); ++cell_index)
    {
        const CanonicalLevelSceneCell& cell = canonical.cells[cell_index];
        if (cell.source_cell_ordinal !=
            SourceCellOrdinal{.value = static_cast<std::uint32_t>(cell_index)})
            return std::unexpected("canonical level view camera source ordinal is invalid");
        if (cell.local_to_world != asset::kIdentityMatrix4x4IR)
        {
            return std::unexpected(
                "canonical level view camera requires canonical identity cell transforms");
        }
        if (!Add(inspected_positions, static_cast<std::uint64_t>(cell.render_mesh.positions.size()),
                 inspected_positions) ||
            inspected_positions > limits.maximum_positions)
            return std::unexpected("canonical level view camera exceeds the position limit");
        if (!Add(inspected_triangle_indices,
                 static_cast<std::uint64_t>(cell.render_mesh.triangle_indices.size()),
                 inspected_triangle_indices) ||
            inspected_triangle_indices > limits.maximum_triangle_indices)
            return std::unexpected(
                "canonical level view camera exceeds the triangle-index limit");

        for (const asset::Float3IR& position : cell.render_mesh.positions)
        {
            if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                !std::isfinite(position.z))
                return std::unexpected(
                    "canonical level view camera requires finite cell coordinates");
        }
        if (cell.render_mesh.triangle_indices.size() % 3U != 0U)
            return std::unexpected(
                "canonical level view camera mesh has an incomplete triangle");
        for (const std::uint32_t index : cell.render_mesh.triangle_indices)
        {
            if (index >= cell.render_mesh.positions.size())
                return std::unexpected(
                    "canonical level view camera triangle index is invalid");
        }

        if (cell.render_mesh.positions.empty() || cell.render_mesh.triangle_indices.empty())
            continue;
        for (const asset::Float3IR& position : cell.render_mesh.positions)
            Include(bounds, position);
        ++view.framed_cells;
        view.framed_positions += static_cast<std::uint64_t>(cell.render_mesh.positions.size());
    }

    // An unframed scene keeps the identity camera instead of inventing an extent.
    if (view.framed_cells == 0U)
        return view;

    std::array<double, kAxisCount> extents{};
    for (std::size_t axis = 0U; axis < kAxisCount; ++axis)
    {
        extents[axis] = bounds.maximum[axis] - bounds.minimum[axis];
        if (!std::isfinite(extents[axis]) || extents[axis] < 0.0)
            return std::unexpected("canonical level view camera extent is not finite");
    }
    const std::array<std::size_t, kAxisCount> axes = SelectAxes(extents);
    for (std::size_t stage = 0U; stage < kAxisCount; ++stage)
        view.source_axes[stage] = static_cast<std::uint8_t>(axes[stage]);

    // Horizontal and vertical stages centre the union; depth rebases on its minimum.
    std::array<float, kAxisCount> view_offsets{};
    for (std::size_t stage = 0U; stage < kAxisCount; ++stage)
    {
        const std::size_t axis = axes[stage];
        const double offset = stage == 2U
                                  ? bounds.minimum[axis]
                                  : (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5;
        if (!NarrowToFloat(-offset, view_offsets[stage]))
            return std::unexpected("canonical level view camera translation is not representable");
    }

    const double usable_span = kClipSpan * (1.0 - 2.0 * kDiagnosticInsetFraction);
    const double usable_depth_span = kClipDepthSpan * (1.0 - 2.0 * kDiagnosticInsetFraction);
    const double depth_inset = kClipDepthSpan * kDiagnosticInsetFraction;
    const double largest_extent = std::max(extents[axes[0]], extents[axes[1]]);
    const double depth_extent = extents[axes[2]];

    float planar_scale = 0.0F;
    if (largest_extent > 0.0 && !NarrowToFloat(usable_span / largest_extent, planar_scale))
        return std::unexpected("canonical level view camera scale is not representable");
    float depth_scale = 0.0F;
    float depth_offset = static_cast<float>(kDiagnosticDepth);
    if (depth_extent > 0.0)
    {
        if (!NarrowToFloat(usable_depth_span / depth_extent, depth_scale))
            return std::unexpected("canonical level view camera depth scale is not representable");
        depth_offset = static_cast<float>(depth_inset);
    }

    view.camera.world_to_view = asset::Matrix4x4IR{};
    for (std::size_t stage = 0U; stage < kAxisCount; ++stage)
    {
        view.camera.world_to_view.row_major[stage * 4U + axes[stage]] = 1.0F;
        view.camera.world_to_view.row_major[stage * 4U + 3U] = view_offsets[stage];
    }
    view.camera.world_to_view.row_major[15U] = 1.0F;

    view.camera.view_to_clip = asset::Matrix4x4IR{};
    view.camera.view_to_clip.row_major[0U] = planar_scale;
    view.camera.view_to_clip.row_major[5U] = planar_scale;
    view.camera.view_to_clip.row_major[10U] = depth_scale;
    view.camera.view_to_clip.row_major[11U] = depth_offset;
    view.camera.view_to_clip.row_major[15U] = 1.0F;
    return view;
}
} // namespace omega::runtime
