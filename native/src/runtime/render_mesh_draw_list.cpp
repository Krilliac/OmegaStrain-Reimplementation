#include "omega/runtime/render_mesh_draw_list.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr RenderMeshDrawListError Error(
    const RenderMeshDrawListErrorCode code) noexcept
{
    return RenderMeshDrawListError{
        .code = code,
        .message = RenderMeshDrawListErrorMessage(code),
    };
}

[[nodiscard]] bool IsFinite(const asset::Matrix4x4IR& matrix) noexcept
{
    for (const float value : matrix.row_major)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

[[nodiscard]] bool IsFinite(const asset::Float3IR& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] constexpr bool IsValidRasterMode(const RenderMeshRasterMode mode) noexcept
{
    switch (mode)
    {
    case RenderMeshRasterMode::Fill:
    case RenderMeshRasterMode::Wireframe:
        return true;
    }
    return false;
}

constexpr std::uint32_t kOutsideLeft = 1U << 0U;
constexpr std::uint32_t kOutsideRight = 1U << 1U;
constexpr std::uint32_t kOutsideBottom = 1U << 2U;
constexpr std::uint32_t kOutsideTop = 1U << 3U;
constexpr std::uint32_t kOutsideNear = 1U << 4U;
constexpr std::uint32_t kOutsideFar = 1U << 5U;
constexpr std::uint32_t kOutsideEveryPlane = kOutsideLeft | kOutsideRight | kOutsideBottom |
                                             kOutsideTop | kOutsideNear | kOutsideFar;

struct OutwardInterval final
{
    double lower = 0.0;
    double upper = 0.0;
};

[[nodiscard]] OutwardInterval ClipComponentBounds(const asset::Matrix4x4IR& object_to_clip,
    const std::size_t row, const float x, const float y, const float z) noexcept
{
    const std::array<float, 4U> coordinates{x, y, z, 1.0F};
    OutwardInterval bounds;
    constexpr double negative_infinity = -std::numeric_limits<double>::infinity();
    constexpr double positive_infinity = std::numeric_limits<double>::infinity();
    for (std::size_t column = 0U; column < coordinates.size(); ++column)
    {
        // A binary32-by-binary32 product is exactly representable in binary64. Only the running
        // additions round, so advancing each endpoint by one binary64 value encloses the exact dot
        // product even under catastrophic cancellation.
        const double term = static_cast<double>(object_to_clip.row_major[row * 4U + column]) *
                            static_cast<double>(coordinates[column]);
        bounds.lower = std::nextafter(bounds.lower + term, negative_infinity);
        bounds.upper = std::nextafter(bounds.upper + term, positive_infinity);
    }
    return bounds;
}

[[nodiscard]] double SumUpperBound(
    const OutwardInterval& left, const OutwardInterval& right) noexcept
{
    return std::nextafter(
        left.upper + right.upper, std::numeric_limits<double>::infinity());
}

[[nodiscard]] double DifferenceUpperBound(
    const OutwardInterval& left, const OutwardInterval& right) noexcept
{
    return std::nextafter(
        left.upper - right.lower, std::numeric_limits<double>::infinity());
}

[[nodiscard]] bool IsFinite(const OutwardInterval& value) noexcept
{
    return std::isfinite(value.lower) && std::isfinite(value.upper);
}

[[nodiscard]] std::uint32_t CornerOutsideMask(const asset::Matrix4x4IR& object_to_clip,
    const float x, const float y, const float z) noexcept
{
    const OutwardInterval clip_x = ClipComponentBounds(object_to_clip, 0U, x, y, z);
    const OutwardInterval clip_y = ClipComponentBounds(object_to_clip, 1U, x, y, z);
    const OutwardInterval clip_z = ClipComponentBounds(object_to_clip, 2U, x, y, z);
    const OutwardInterval clip_w = ClipComponentBounds(object_to_clip, 3U, x, y, z);
    if (!IsFinite(clip_x) || !IsFinite(clip_y) || !IsFinite(clip_z) || !IsFinite(clip_w))
    {
        return 0U;
    }

    // Each canonical Direct3D plane is expressed as an inside-is-nonnegative affine form. A
    // corner is outside only when the outward-rounded upper bound proves the complete interval is
    // strictly negative. Numerically uncertain boundary cases therefore fail soft to visible.
    std::uint32_t mask = 0U;
    if (SumUpperBound(clip_x, clip_w) < 0.0)
        mask |= kOutsideLeft;
    if (DifferenceUpperBound(clip_w, clip_x) < 0.0)
        mask |= kOutsideRight;
    if (SumUpperBound(clip_y, clip_w) < 0.0)
        mask |= kOutsideBottom;
    if (DifferenceUpperBound(clip_w, clip_y) < 0.0)
        mask |= kOutsideTop;
    if (clip_z.upper < 0.0)
        mask |= kOutsideNear;
    if (DifferenceUpperBound(clip_w, clip_z) < 0.0)
        mask |= kOutsideFar;
    return mask;
}
} // namespace

RenderMeshDrawList::RenderMeshDrawList() noexcept
{
    static_assert(std::is_trivially_copyable_v<RenderMeshDrawCommand>);
    std::memset(static_cast<void*>(commands_.data()), 0, sizeof(commands_));
}

std::expected<RenderMeshDrawList, RenderMeshDrawListError> RenderMeshDrawList::Create(
    const std::span<const RenderMeshDrawCommand> commands) noexcept
{
    if (commands.size() > kMaximumRenderMeshDrawsPerFrame)
        return std::unexpected(Error(RenderMeshDrawListErrorCode::CapacityExceeded));

    RenderMeshDrawList result;
    for (std::size_t index = 0U; index < commands.size(); ++index)
    {
        const RenderMeshDrawCommand& command = commands[index];
        if (!command.mesh.valid())
            return std::unexpected(Error(RenderMeshDrawListErrorCode::InvalidMeshHandle));
        if (!IsFinite(command.object_to_clip))
            return std::unexpected(Error(RenderMeshDrawListErrorCode::NonFiniteTransform));
        if (!IsValidRasterMode(command.raster_mode))
            return std::unexpected(Error(RenderMeshDrawListErrorCode::InvalidRasterMode));
        result.commands_[index] = command;
    }
    result.count_ = static_cast<std::uint32_t>(commands.size());
    return result;
}

bool IsBoxPossiblyVisible(const asset::Matrix4x4IR& object_to_clip,
    const asset::Float3IR& minimum, const asset::Float3IR& maximum) noexcept
{
    if (!IsFinite(object_to_clip) || !IsFinite(minimum) || !IsFinite(maximum))
        return true;
    if (!(minimum.x <= maximum.x) || !(minimum.y <= maximum.y) ||
        !(minimum.z <= maximum.z))
    {
        return true;
    }

    const std::array<float, 2U> xs{minimum.x, maximum.x};
    const std::array<float, 2U> ys{minimum.y, maximum.y};
    const std::array<float, 2U> zs{minimum.z, maximum.z};

    std::uint32_t shared_outside = kOutsideEveryPlane;
    for (const float z : zs)
    {
        for (const float y : ys)
        {
            for (const float x : xs)
            {
                shared_outside &= CornerOutsideMask(object_to_clip, x, y, z);
                if (shared_outside == 0U)
                    return true;
            }
        }
    }
    return false;
}
} // namespace omega::runtime
