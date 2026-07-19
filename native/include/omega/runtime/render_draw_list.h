#pragma once

#include "omega/runtime/render_texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::runtime
{
inline constexpr std::size_t kMaximumRenderTextureBlitsPerFrame = 16U;
inline constexpr std::uint32_t kNormalizedRenderExtent = 65'536U;

struct RenderTargetRectQ16
{
    std::uint32_t left = 0U;
    std::uint32_t top = 0U;
    std::uint32_t right = 0U;
    std::uint32_t bottom = 0U;

    friend constexpr bool operator==(
        const RenderTargetRectQ16&, const RenderTargetRectQ16&) noexcept = default;
};

struct RenderSourceRectQ16
{
    // Half-open [left, top, right, bottom) edges normalized to the referenced
    // texture's mip-zero extent by kNormalizedRenderExtent.
    std::uint32_t left = 0U;
    std::uint32_t top = 0U;
    std::uint32_t right = 0U;
    std::uint32_t bottom = 0U;

    friend constexpr bool operator==(
        const RenderSourceRectQ16&, const RenderSourceRectQ16&) noexcept = default;
};

struct RenderTargetRectPixels
{
    std::uint32_t left = 0U;
    std::uint32_t top = 0U;
    std::uint32_t right = 0U;
    std::uint32_t bottom = 0U;

    friend constexpr bool operator==(
        const RenderTargetRectPixels&, const RenderTargetRectPixels&) noexcept = default;
};

struct RenderSourceRectPixels
{
    // Half-open [left, top, right, bottom) mip-zero texel edges.
    std::uint32_t left = 0U;
    std::uint32_t top = 0U;
    std::uint32_t right = 0U;
    std::uint32_t bottom = 0U;

    friend constexpr bool operator==(
        const RenderSourceRectPixels&, const RenderSourceRectPixels&) noexcept = default;
};

enum class RenderTextureFitMode : std::uint8_t
{
    Contain = 0U,
    Stretch = 1U,
};

enum class RenderTextureFilterMode : std::uint8_t
{
    Nearest = 0U,
    Linear = 1U,
};

struct RenderTextureBlitCommand
{
    RenderTextureHandle texture;
    RenderSourceRectQ16 source;
    RenderTargetRectQ16 destination;
    RenderTextureFitMode fit_mode = RenderTextureFitMode::Contain;
    RenderTextureFilterMode filter_mode = RenderTextureFilterMode::Nearest;

    friend constexpr bool operator==(
        const RenderTextureBlitCommand&, const RenderTextureBlitCommand&) noexcept = default;
};

enum class RenderDrawListErrorCode
{
    CapacityExceeded,
    InvalidTextureHandle,
    InvalidSourceRect,
    InvalidTargetRect,
    InvalidFitMode,
    InvalidFilterMode,
    InvalidSourceExtent,
    InvalidTargetExtent,
};

[[nodiscard]] constexpr std::string_view RenderDrawListErrorCodeName(
    const RenderDrawListErrorCode code) noexcept
{
    switch (code)
    {
    case RenderDrawListErrorCode::CapacityExceeded:
        return "capacity-exceeded";
    case RenderDrawListErrorCode::InvalidTextureHandle:
        return "invalid-texture-handle";
    case RenderDrawListErrorCode::InvalidSourceRect:
        return "invalid-source-rect";
    case RenderDrawListErrorCode::InvalidTargetRect:
        return "invalid-target-rect";
    case RenderDrawListErrorCode::InvalidFitMode:
        return "invalid-fit-mode";
    case RenderDrawListErrorCode::InvalidFilterMode:
        return "invalid-filter-mode";
    case RenderDrawListErrorCode::InvalidSourceExtent:
        return "invalid-source-extent";
    case RenderDrawListErrorCode::InvalidTargetExtent:
        return "invalid-target-extent";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RenderDrawListErrorMessage(
    const RenderDrawListErrorCode code) noexcept
{
    switch (code)
    {
    case RenderDrawListErrorCode::CapacityExceeded:
        return "render draw list command capacity is exceeded";
    case RenderDrawListErrorCode::InvalidTextureHandle:
        return "render draw list texture handle is invalid";
    case RenderDrawListErrorCode::InvalidSourceRect:
        return "render draw list source rectangle is invalid";
    case RenderDrawListErrorCode::InvalidTargetRect:
        return "render draw list target rectangle is invalid";
    case RenderDrawListErrorCode::InvalidFitMode:
        return "render draw list fit mode is invalid";
    case RenderDrawListErrorCode::InvalidFilterMode:
        return "render draw list filter mode is invalid";
    case RenderDrawListErrorCode::InvalidSourceExtent:
        return "render draw list source extent is invalid";
    case RenderDrawListErrorCode::InvalidTargetExtent:
        return "render draw list target extent is invalid";
    }
    return "render draw list error is unknown";
}

struct RenderDrawListError
{
    RenderDrawListErrorCode code = RenderDrawListErrorCode::CapacityExceeded;
    // Fixed category text only; it contains no resource identity or backend detail.
    std::string_view message = RenderDrawListErrorMessage(code);
};

namespace detail
{
struct RenderDrawListTestAccess;
}

// Fixed owned renderer-neutral command value. Commands retain generation identities but do not pin
// their resources; the current synchronous caller keeps every referenced texture resident.
class RenderDrawList final
{
public:
    // Clears the complete fixed backing storage, including padding bytes, so inactive commands
    // cannot retain an earlier resource identity even when the value is copied as raw bytes.
    RenderDrawList() noexcept;

    [[nodiscard]] static std::expected<RenderDrawList, RenderDrawListError> Create(
        std::span<const RenderTextureBlitCommand> commands) noexcept;

    [[nodiscard]] std::span<const RenderTextureBlitCommand> commands() const noexcept
    {
        return {commands_.data(), static_cast<std::size_t>(count_)};
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return count_; }
    [[nodiscard]] bool empty() const noexcept { return count_ == 0U; }

private:
    friend struct detail::RenderDrawListTestAccess;

    std::array<RenderTextureBlitCommand, kMaximumRenderTextureBlitsPerFrame> commands_{};
    std::uint32_t count_ = 0U;
};

struct RenderTextureBlitPlan
{
    RenderSourceRectPixels source;
    RenderTargetRectPixels destination;

    friend constexpr bool operator==(
        const RenderTextureBlitPlan&, const RenderTextureBlitPlan&) noexcept = default;
};

[[nodiscard]] std::expected<RenderSourceRectPixels, RenderDrawListError>
    MapTextureSourceRect(RenderSourceRectQ16 source,
        std::uint32_t source_width, std::uint32_t source_height) noexcept;

// The source is an already mapped half-open mip-zero texel rectangle. This pure
// planner retains it exactly and computes a half-open target-pixel rectangle.
[[nodiscard]] std::expected<RenderTextureBlitPlan, RenderDrawListError>
    PlanTextureBlit(RenderSourceRectPixels source, RenderTargetRectQ16 destination,
        RenderTextureFitMode fit_mode,
        std::uint32_t target_width, std::uint32_t target_height) noexcept;

static_assert(std::is_trivially_copyable_v<RenderTargetRectQ16>);
static_assert(std::is_standard_layout_v<RenderTargetRectQ16>);
static_assert(std::is_trivially_copyable_v<RenderSourceRectQ16>);
static_assert(std::is_standard_layout_v<RenderSourceRectQ16>);
static_assert(std::is_trivially_copyable_v<RenderTargetRectPixels>);
static_assert(std::is_standard_layout_v<RenderTargetRectPixels>);
static_assert(std::is_trivially_copyable_v<RenderSourceRectPixels>);
static_assert(std::is_standard_layout_v<RenderSourceRectPixels>);
static_assert(sizeof(RenderTextureFitMode) == 1U);
static_assert(sizeof(RenderTextureFilterMode) == 1U);
static_assert(std::is_trivially_copyable_v<RenderTextureBlitCommand>);
static_assert(std::is_standard_layout_v<RenderTextureBlitCommand>);
static_assert(std::is_trivially_copyable_v<RenderDrawList>);
static_assert(std::is_standard_layout_v<RenderDrawList>);
static_assert(std::is_trivially_copyable_v<RenderTextureBlitPlan>);
static_assert(std::is_standard_layout_v<RenderTextureBlitPlan>);
} // namespace omega::runtime
