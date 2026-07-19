#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::runtime
{
inline constexpr std::size_t kMaximumRenderTextureSlotCapacity = 8192U;

// Renderer-neutral, non-owning identity. The issuing RenderTexturePool remains the authority for
// pool membership, publication state, and generation validation. Value initialization is invalid.
struct RenderTextureHandle
{
    std::uint64_t pool_identity = 0U;
    std::uint64_t generation = 0U;
    std::uint32_t slot_index = 0U;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return pool_identity != 0U && generation != 0U;
    }

    friend constexpr bool operator==(
        const RenderTextureHandle&, const RenderTextureHandle&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<RenderTextureHandle>);
static_assert(std::is_standard_layout_v<RenderTextureHandle>);

// The caller owns the pixels and must keep them alive only for the synchronous Reserve preflight
// and backend upload. V0 accepts exactly tightly packed RGBA8: width * height * 4 bytes.
struct Rgba8TextureUploadView
{
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::span<const std::byte> pixels;
};

struct RenderTexturePoolConfig
{
    // Synthetic native-renderer policy, not a retail limit or user-facing setting.
    std::size_t slot_capacity = 64U;
    // Logical tightly packed RGBA8 bytes only; not allocator capacity, transfer-buffer residency,
    // process RSS, or GPU allocation size.
    std::uint64_t maximum_resident_logical_bytes = 64ULL * 1024ULL * 1024ULL;
};

enum class RenderTextureErrorCode
{
    InvalidConfiguration,
    AllocationFailed,
    PoolIdentityExhausted,
    InvalidImage,
    SlotCapacityExceeded,
    ResidentBudgetExceeded,
    InvalidReservation,
    InvalidHandle,
};

[[nodiscard]] constexpr std::string_view RenderTextureErrorCodeName(
    const RenderTextureErrorCode code) noexcept
{
    switch (code)
    {
    case RenderTextureErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case RenderTextureErrorCode::AllocationFailed:
        return "allocation-failed";
    case RenderTextureErrorCode::PoolIdentityExhausted:
        return "pool-identity-exhausted";
    case RenderTextureErrorCode::InvalidImage:
        return "invalid-image";
    case RenderTextureErrorCode::SlotCapacityExceeded:
        return "slot-capacity-exceeded";
    case RenderTextureErrorCode::ResidentBudgetExceeded:
        return "resident-budget-exceeded";
    case RenderTextureErrorCode::InvalidReservation:
        return "invalid-reservation";
    case RenderTextureErrorCode::InvalidHandle:
        return "invalid-handle";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RenderTextureErrorMessage(
    const RenderTextureErrorCode code) noexcept
{
    switch (code)
    {
    case RenderTextureErrorCode::InvalidConfiguration:
        return "render texture pool configuration is invalid";
    case RenderTextureErrorCode::AllocationFailed:
        return "render texture pool allocation failed";
    case RenderTextureErrorCode::PoolIdentityExhausted:
        return "render texture pool identity space is exhausted";
    case RenderTextureErrorCode::InvalidImage:
        return "render texture upload view is invalid";
    case RenderTextureErrorCode::SlotCapacityExceeded:
        return "render texture slot capacity is exhausted";
    case RenderTextureErrorCode::ResidentBudgetExceeded:
        return "render texture resident budget is exhausted";
    case RenderTextureErrorCode::InvalidReservation:
        return "render texture reservation is invalid";
    case RenderTextureErrorCode::InvalidHandle:
        return "render texture handle is invalid";
    }
    return "render texture error is unknown";
}

struct RenderTextureError
{
    RenderTextureErrorCode code = RenderTextureErrorCode::InvalidConfiguration;
    // Fixed category text only. It never contains paths, names, backend messages, resource
    // identities, source identities, offsets, payload bytes, or exception text.
    std::string_view message = RenderTextureErrorMessage(code);
};

struct RenderTextureReservation
{
    RenderTextureHandle handle;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t logical_bytes = 0U;
};

struct RenderTextureMetadata
{
    RenderTextureHandle handle;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint64_t logical_bytes = 0U;
};

struct RenderTexturePoolSnapshot
{
    std::size_t slot_capacity = 0U;
    std::size_t free_slots = 0U;
    std::size_t reserved_slots = 0U;
    std::size_t resident_slots = 0U;
    std::size_t retired_slots = 0U;
    std::uint64_t reserved_logical_bytes = 0U;
    std::uint64_t resident_logical_bytes = 0U;

    friend constexpr bool operator==(
        const RenderTexturePoolSnapshot&, const RenderTexturePoolSnapshot&) noexcept = default;
};
} // namespace omega::runtime
