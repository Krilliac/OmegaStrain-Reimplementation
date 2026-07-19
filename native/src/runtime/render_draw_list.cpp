#include "omega/runtime/render_draw_list.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr RenderDrawListError Error(
    const RenderDrawListErrorCode code) noexcept
{
    return RenderDrawListError{
        .code = code,
        .message = RenderDrawListErrorMessage(code),
    };
}

[[nodiscard]] constexpr bool IsValidTargetRect(
    const RenderTargetRectQ16& rect) noexcept
{
    return rect.left < rect.right && rect.top < rect.bottom &&
           rect.left <= kNormalizedRenderExtent &&
           rect.top <= kNormalizedRenderExtent &&
           rect.right <= kNormalizedRenderExtent &&
           rect.bottom <= kNormalizedRenderExtent;
}

[[nodiscard]] constexpr std::uint32_t MapFloor(
    const std::uint32_t edge, const std::uint32_t extent) noexcept
{
    return static_cast<std::uint32_t>(
        static_cast<std::uint64_t>(edge) * extent / kNormalizedRenderExtent);
}

[[nodiscard]] constexpr std::uint32_t MapCeil(
    const std::uint32_t edge, const std::uint32_t extent) noexcept
{
    const std::uint64_t scaled = static_cast<std::uint64_t>(edge) * extent;
    return static_cast<std::uint32_t>(
        (scaled + kNormalizedRenderExtent - 1U) / kNormalizedRenderExtent);
}

[[nodiscard]] constexpr std::uint32_t DivideRoundNearest(
    const std::uint64_t numerator, const std::uint32_t denominator) noexcept
{
    std::uint64_t quotient = numerator / denominator;
    const std::uint64_t remainder = numerator % denominator;
    if (remainder * 2U >= denominator)
        ++quotient;
    return static_cast<std::uint32_t>(quotient);
}
} // namespace

RenderDrawList::RenderDrawList() noexcept
{
    static_assert(std::is_trivially_copyable_v<RenderTextureBlitCommand>);
    std::memset(static_cast<void*>(commands_.data()), 0, sizeof(commands_));
}

std::expected<RenderDrawList, RenderDrawListError> RenderDrawList::Create(
    const std::span<const RenderTextureBlitCommand> commands) noexcept
{
    if (commands.size() > kMaximumRenderTextureBlitsPerFrame)
        return std::unexpected(Error(RenderDrawListErrorCode::CapacityExceeded));

    RenderDrawList result;
    for (std::size_t index = 0U; index < commands.size(); ++index)
    {
        const RenderTextureBlitCommand& command = commands[index];
        if (!command.texture.valid())
            return std::unexpected(Error(RenderDrawListErrorCode::InvalidTextureHandle));
        if (!IsValidTargetRect(command.destination))
            return std::unexpected(Error(RenderDrawListErrorCode::InvalidTargetRect));
        result.commands_[index] = command;
    }
    result.count_ = static_cast<std::uint32_t>(commands.size());
    return result;
}

std::expected<RenderTargetRectPixels, RenderDrawListError> PlanContainedTextureBlit(
    const RenderTargetRectQ16 destination,
    const std::uint32_t source_width, const std::uint32_t source_height,
    const std::uint32_t target_width, const std::uint32_t target_height) noexcept
{
    if (source_width == 0U || source_height == 0U)
        return std::unexpected(Error(RenderDrawListErrorCode::InvalidSourceExtent));
    if (target_width == 0U || target_height == 0U)
        return std::unexpected(Error(RenderDrawListErrorCode::InvalidTargetExtent));
    if (!IsValidTargetRect(destination))
        return std::unexpected(Error(RenderDrawListErrorCode::InvalidTargetRect));

    const std::uint32_t mapped_left = MapFloor(destination.left, target_width);
    const std::uint32_t mapped_top = MapFloor(destination.top, target_height);
    const std::uint32_t mapped_right = MapCeil(destination.right, target_width);
    const std::uint32_t mapped_bottom = MapCeil(destination.bottom, target_height);
    const std::uint32_t available_width = mapped_right - mapped_left;
    const std::uint32_t available_height = mapped_bottom - mapped_top;

    std::uint32_t planned_width = available_width;
    std::uint32_t planned_height = available_height;
    if (static_cast<std::uint64_t>(available_width) * source_height <=
        static_cast<std::uint64_t>(available_height) * source_width)
    {
        planned_height = std::clamp(DivideRoundNearest(
            static_cast<std::uint64_t>(available_width) * source_height, source_width),
            1U, available_height);
    }
    else
    {
        planned_width = std::clamp(DivideRoundNearest(
            static_cast<std::uint64_t>(available_height) * source_width, source_height),
            1U, available_width);
    }

    const std::uint32_t left = mapped_left + (available_width - planned_width) / 2U;
    const std::uint32_t top = mapped_top + (available_height - planned_height) / 2U;
    return RenderTargetRectPixels{
        .left = left,
        .top = top,
        .right = left + planned_width,
        .bottom = top + planned_height,
    };
}
} // namespace omega::runtime
