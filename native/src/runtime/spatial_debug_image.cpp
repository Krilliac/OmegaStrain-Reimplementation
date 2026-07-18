#include "omega/runtime/spatial_debug_image.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

namespace omega::runtime
{
namespace
{
constexpr std::uint32_t kTilePixels = 32U;
constexpr std::uint32_t kTileInset = 3U;
constexpr std::uint64_t kChannelsPerPixel = 4U;
constexpr std::uint64_t kEdgesPerTriangle = 3U;
constexpr std::array kBackgroundColor{
    std::byte{8}, std::byte{12}, std::byte{24}, std::byte{255}};
constexpr std::array kBorderColor{
    std::byte{28}, std::byte{38}, std::byte{58}, std::byte{255}};
constexpr std::array kWireColor{
    std::byte{112}, std::byte{220}, std::byte{255}, std::byte{255}};

static_assert(2U * kTileInset + 1U < kTilePixels);

struct Pixel
{
    std::uint32_t x = 0;
    std::uint32_t y = 0;
};

struct ImagePlan
{
    std::uint32_t side_tiles = 0;
    std::uint32_t width = 0;
    std::uint64_t output_bytes = 0;
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

[[nodiscard]] std::expected<ImagePlan, std::string> Preflight(
    const asset::LevelSpatialIR& spatial, const SpatialDebugImageLimits& limits)
{
    const std::uint64_t cell_count = static_cast<std::uint64_t>(spatial.terrain_cells.size());
    if (cell_count > limits.maximum_cells)
        return std::unexpected("spatial debug image exceeds the cell limit");

    std::uint64_t vertex_count = 0;
    std::uint64_t triangle_count = 0;
    for (const asset::SpatialMeshIR& mesh : spatial.terrain_cells)
    {
        if (!Add(vertex_count, static_cast<std::uint64_t>(mesh.vertices.size()), vertex_count) ||
            vertex_count > limits.maximum_vertices)
            return std::unexpected("spatial debug image exceeds the vertex limit");
        if (!Add(triangle_count, static_cast<std::uint64_t>(mesh.triangles.size()),
                triangle_count) ||
            triangle_count > limits.maximum_triangles)
            return std::unexpected("spatial debug image exceeds the triangle limit");

        for (const asset::Float3IR& vertex : mesh.vertices)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                !std::isfinite(vertex.z))
                return std::unexpected("spatial debug image requires finite vertex coordinates");
        }
        for (const asset::SpatialTriangleIR& triangle : mesh.triangles)
        {
            for (const std::uint32_t index : triangle.vertex_indices)
            {
                if (index >= mesh.vertices.size())
                    return std::unexpected("spatial debug image triangle index is out of range");
            }
        }
    }

    std::uint64_t raster_steps = 0;
    if (!Multiply(triangle_count, kEdgesPerTriangle, raster_steps) ||
        !Multiply(raster_steps, kTilePixels, raster_steps) ||
        raster_steps > limits.maximum_raster_steps)
        return std::unexpected("spatial debug image exceeds the raster-work limit");

    const std::uint64_t displayed_cells = std::max<std::uint64_t>(1U, cell_count);
    const std::uint64_t side_tiles = CeilSquareRoot(displayed_cells);
    if (side_tiles > std::numeric_limits<std::uint32_t>::max() / kTilePixels)
        return std::unexpected("spatial debug image dimensions overflow");
    const std::uint64_t width = side_tiles * kTilePixels;
    std::uint64_t pixel_count = 0;
    std::uint64_t output_bytes = 0;
    if (!Multiply(width, width, pixel_count) ||
        !Multiply(pixel_count, kChannelsPerPixel, output_bytes))
        return std::unexpected("spatial debug image byte size overflows");
    if (output_bytes > limits.maximum_output_bytes ||
        output_bytes > std::numeric_limits<std::size_t>::max())
        return std::unexpected("spatial debug image exceeds the output-byte limit");

    return ImagePlan{
        .side_tiles = static_cast<std::uint32_t>(side_tiles),
        .width = static_cast<std::uint32_t>(width),
        .output_bytes = output_bytes,
    };
}

void SetPixel(DebugImage& image, const Pixel pixel, const std::array<std::byte, 4>& color) noexcept
{
    const std::size_t offset =
        (static_cast<std::size_t>(pixel.y) * image.width + pixel.x) * color.size();
    for (std::size_t channel = 0; channel < color.size(); ++channel)
        image.rgba8_pixels[offset + channel] = color[channel];
}

void DrawBorder(DebugImage& image, const std::uint32_t origin_x,
    const std::uint32_t origin_y) noexcept
{
    const std::uint32_t maximum_x = origin_x + kTilePixels - 1U;
    const std::uint32_t maximum_y = origin_y + kTilePixels - 1U;
    for (std::uint32_t offset = 0; offset < kTilePixels; ++offset)
    {
        SetPixel(image, {.x = origin_x + offset, .y = origin_y}, kBorderColor);
        SetPixel(image, {.x = origin_x + offset, .y = maximum_y}, kBorderColor);
        SetPixel(image, {.x = origin_x, .y = origin_y + offset}, kBorderColor);
        SetPixel(image, {.x = maximum_x, .y = origin_y + offset}, kBorderColor);
    }
}

[[nodiscard]] double Coordinate(const asset::Float3IR& vertex, const std::size_t axis) noexcept
{
    if (axis == 0U)
        return vertex.x;
    if (axis == 1U)
        return vertex.y;
    return vertex.z;
}

struct Projection
{
    std::array<std::size_t, 2> axes{};
    std::array<double, 2> minimum{};
    std::array<double, 2> extents{};
    double scale = 0.0;
    std::array<double, 2> offsets{};
    std::uint32_t origin_x = 0;
    std::uint32_t origin_y = 0;
};

[[nodiscard]] Projection MakeProjection(const asset::SpatialMeshIR& mesh,
    const std::uint32_t origin_x, const std::uint32_t origin_y) noexcept
{
    std::array minimum{
        std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array maximum{-std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    for (const asset::Float3IR& vertex : mesh.vertices)
    {
        for (std::size_t axis = 0; axis < minimum.size(); ++axis)
        {
            const double value = Coordinate(vertex, axis);
            minimum[axis] = std::min(minimum[axis], value);
            maximum[axis] = std::max(maximum[axis], value);
        }
    }

    std::array<double, 3> extents{};
    for (std::size_t axis = 0; axis < extents.size(); ++axis)
        extents[axis] = maximum[axis] - minimum[axis];
    std::array<std::size_t, 3> axes{0U, 1U, 2U};
    for (std::size_t left = 0; left < axes.size(); ++left)
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

    Projection projection{
        .axes = {axes[0], axes[1]},
        .minimum = {minimum[axes[0]], minimum[axes[1]]},
        .extents = {extents[axes[0]], extents[axes[1]]},
        .origin_x = origin_x,
        .origin_y = origin_y,
    };
    constexpr double span = static_cast<double>(kTilePixels - 1U - 2U * kTileInset);
    const double largest_extent = std::max(projection.extents[0], projection.extents[1]);
    if (largest_extent > 0.0)
    {
        projection.scale = span / largest_extent;
        for (std::size_t axis = 0; axis < projection.offsets.size(); ++axis)
        {
            projection.offsets[axis] =
                (span - projection.extents[axis] * projection.scale) * 0.5;
        }
    }
    else
    {
        projection.offsets = {span * 0.5, span * 0.5};
    }
    return projection;
}

[[nodiscard]] Pixel Project(
    const asset::Float3IR& vertex, const Projection& projection) noexcept
{
    constexpr double span = static_cast<double>(kTilePixels - 1U - 2U * kTileInset);
    std::array<double, 2> normalized{};
    for (std::size_t axis = 0; axis < normalized.size(); ++axis)
    {
        normalized[axis] = projection.offsets[axis] +
                           (Coordinate(vertex, projection.axes[axis]) -
                               projection.minimum[axis]) *
                               projection.scale;
        normalized[axis] = std::clamp(normalized[axis], 0.0, span);
    }
    const auto x = static_cast<std::uint32_t>(std::floor(normalized[0] + 0.5));
    const auto y = static_cast<std::uint32_t>(std::floor(normalized[1] + 0.5));
    return Pixel{
        .x = projection.origin_x + kTileInset + x,
        .y = projection.origin_y + kTileInset +
             static_cast<std::uint32_t>(span) - y,
    };
}

void DrawLine(DebugImage& image, Pixel first, Pixel second) noexcept
{
    if (second.x < first.x || (second.x == first.x && second.y < first.y))
        std::swap(first, second);

    std::int64_t x = first.x;
    std::int64_t y = first.y;
    const std::int64_t target_x = second.x;
    const std::int64_t target_y = second.y;
    const std::int64_t delta_x = std::abs(target_x - x);
    const std::int64_t delta_y = -std::abs(target_y - y);
    const std::int64_t step_x = x < target_x ? 1 : -1;
    const std::int64_t step_y = y < target_y ? 1 : -1;
    std::int64_t error = delta_x + delta_y;
    while (true)
    {
        SetPixel(image,
            {.x = static_cast<std::uint32_t>(x), .y = static_cast<std::uint32_t>(y)},
            kWireColor);
        if (x == target_x && y == target_y)
            break;
        const std::int64_t doubled_error = 2 * error;
        if (doubled_error >= delta_y)
        {
            error += delta_y;
            x += step_x;
        }
        if (doubled_error <= delta_x)
        {
            error += delta_x;
            y += step_y;
        }
    }
}
} // namespace

std::expected<DebugImage, std::string> BuildSpatialDebugImage(
    const asset::LevelSpatialIR& spatial, const SpatialDebugImageLimits& limits)
{
    auto planned = Preflight(spatial, limits);
    if (!planned)
        return std::unexpected(planned.error());

    DebugImage image{
        .width = planned->width,
        .height = planned->width,
        .rgba8_pixels =
            std::vector<std::byte>(static_cast<std::size_t>(planned->output_bytes)),
    };
    for (std::size_t offset = 0; offset < image.rgba8_pixels.size(); offset += 4U)
    {
        for (std::size_t channel = 0; channel < kBackgroundColor.size(); ++channel)
            image.rgba8_pixels[offset + channel] = kBackgroundColor[channel];
    }

    for (std::size_t cell_index = 0; cell_index < spatial.terrain_cells.size(); ++cell_index)
    {
        const std::uint32_t column =
            static_cast<std::uint32_t>(cell_index % planned->side_tiles);
        const std::uint32_t row =
            static_cast<std::uint32_t>(cell_index / planned->side_tiles);
        const std::uint32_t origin_x = column * kTilePixels;
        const std::uint32_t origin_y = row * kTilePixels;
        DrawBorder(image, origin_x, origin_y);

        const asset::SpatialMeshIR& mesh = spatial.terrain_cells[cell_index];
        if (mesh.vertices.empty() || mesh.triangles.empty())
            continue;
        const Projection projection = MakeProjection(mesh, origin_x, origin_y);
        for (const asset::SpatialTriangleIR& triangle : mesh.triangles)
        {
            const std::array projected{
                Project(mesh.vertices[triangle.vertex_indices[0]], projection),
                Project(mesh.vertices[triangle.vertex_indices[1]], projection),
                Project(mesh.vertices[triangle.vertex_indices[2]], projection),
            };
            DrawLine(image, projected[0], projected[1]);
            DrawLine(image, projected[1], projected[2]);
            DrawLine(image, projected[2], projected[0]);
        }
    }
    return image;
}
} // namespace omega::runtime
