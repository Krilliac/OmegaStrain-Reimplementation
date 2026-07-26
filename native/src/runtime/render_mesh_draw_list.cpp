#include "omega/runtime/render_mesh_draw_list.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

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

[[nodiscard]] double ClipComponent(const asset::Matrix4x4IR& object_to_clip,
    const std::size_t row, const double x, const double y, const double z) noexcept
{
    return static_cast<double>(object_to_clip.row_major[row * 4U]) * x +
           static_cast<double>(object_to_clip.row_major[row * 4U + 1U]) * y +
           static_cast<double>(object_to_clip.row_major[row * 4U + 2U]) * z +
           static_cast<double>(object_to_clip.row_major[row * 4U + 3U]);
}

[[nodiscard]] std::uint32_t CornerOutsideMask(const asset::Matrix4x4IR& object_to_clip,
    const double x, const double y, const double z) noexcept
{
    const double clip_x = ClipComponent(object_to_clip, 0U, x, y, z);
    const double clip_y = ClipComponent(object_to_clip, 1U, x, y, z);
    const double clip_z = ClipComponent(object_to_clip, 2U, x, y, z);
    const double clip_w = ClipComponent(object_to_clip, 3U, x, y, z);
    if (!std::isfinite(clip_x) || !std::isfinite(clip_y) || !std::isfinite(clip_z) ||
        !std::isfinite(clip_w))
    {
        return 0U;
    }

    std::uint32_t mask = 0U;
    if (clip_x < -clip_w)
        mask |= kOutsideLeft;
    if (clip_x > clip_w)
        mask |= kOutsideRight;
    if (clip_y < -clip_w)
        mask |= kOutsideBottom;
    if (clip_y > clip_w)
        mask |= kOutsideTop;
    if (clip_z < 0.0)
        mask |= kOutsideNear;
    if (clip_z > clip_w)
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

    const std::array<double, 2U> xs{
        static_cast<double>(minimum.x), static_cast<double>(maximum.x)};
    const std::array<double, 2U> ys{
        static_cast<double>(minimum.y), static_cast<double>(maximum.y)};
    const std::array<double, 2U> zs{
        static_cast<double>(minimum.z), static_cast<double>(maximum.z)};

    std::uint32_t shared_outside = kOutsideEveryPlane;
    for (const double z : zs)
    {
        for (const double y : ys)
        {
            for (const double x : xs)
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
