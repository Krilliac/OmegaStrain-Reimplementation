#pragma once

#include "omega/asset/render_mesh_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::runtime
{
inline constexpr std::size_t kMaximumRenderMeshSlotCapacity = 8192U;

// Renderer-neutral, non-owning identity. The issuing RenderMeshPool remains the authority for
// pool membership, publication state, and generation validation. Value initialization is invalid.
struct RenderMeshHandle
{
    std::uint64_t pool_identity = 0U;
    std::uint64_t generation = 0U;
    std::uint32_t slot_index = 0U;

    [[nodiscard]] constexpr bool valid() const noexcept
    {
        return pool_identity != 0U && generation != 0U;
    }

    friend constexpr bool operator==(
        const RenderMeshHandle&, const RenderMeshHandle&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<RenderMeshHandle>);
static_assert(std::is_standard_layout_v<RenderMeshHandle>);

// Borrowed upload data for one synchronous Reserve preflight and backend upload. The caller owns
// both spans and keeps them alive through backend completion; neither span survives in the pool.
struct RenderMeshUploadView
{
    std::span<const asset::Float3IR> positions;
    std::span<const std::uint32_t> triangle_indices;
};

struct RenderMeshPoolConfig
{
    // Synthetic native-renderer policy, not a retail or user-facing limit.
    std::size_t slot_capacity = 64U;
    std::uint64_t maximum_resident_positions = 4ULL << 20U;
    std::uint64_t maximum_resident_triangle_indices = 12ULL << 20U;
    // Position and index payload bytes only; not allocator capacity, transfer storage, process RSS,
    // or GPU allocation size.
    std::uint64_t maximum_resident_logical_bytes = 128ULL * 1024ULL * 1024ULL;
};

enum class RenderMeshErrorCode
{
    InvalidConfiguration,
    AllocationFailed,
    PoolIdentityExhausted,
    InvalidMesh,
    SlotCapacityExceeded,
    PositionBudgetExceeded,
    TriangleIndexBudgetExceeded,
    LogicalByteBudgetExceeded,
    InvalidReservation,
    InvalidHandle,
};

[[nodiscard]] constexpr std::string_view RenderMeshErrorCodeName(
    const RenderMeshErrorCode code) noexcept
{
    switch (code)
    {
    case RenderMeshErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case RenderMeshErrorCode::AllocationFailed:
        return "allocation-failed";
    case RenderMeshErrorCode::PoolIdentityExhausted:
        return "pool-identity-exhausted";
    case RenderMeshErrorCode::InvalidMesh:
        return "invalid-mesh";
    case RenderMeshErrorCode::SlotCapacityExceeded:
        return "slot-capacity-exceeded";
    case RenderMeshErrorCode::PositionBudgetExceeded:
        return "position-budget-exceeded";
    case RenderMeshErrorCode::TriangleIndexBudgetExceeded:
        return "triangle-index-budget-exceeded";
    case RenderMeshErrorCode::LogicalByteBudgetExceeded:
        return "logical-byte-budget-exceeded";
    case RenderMeshErrorCode::InvalidReservation:
        return "invalid-reservation";
    case RenderMeshErrorCode::InvalidHandle:
        return "invalid-handle";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RenderMeshErrorMessage(
    const RenderMeshErrorCode code) noexcept
{
    switch (code)
    {
    case RenderMeshErrorCode::InvalidConfiguration:
        return "render mesh pool configuration is invalid";
    case RenderMeshErrorCode::AllocationFailed:
        return "render mesh pool allocation failed";
    case RenderMeshErrorCode::PoolIdentityExhausted:
        return "render mesh pool identity space is exhausted";
    case RenderMeshErrorCode::InvalidMesh:
        return "render mesh upload view is invalid";
    case RenderMeshErrorCode::SlotCapacityExceeded:
        return "render mesh slot capacity is exhausted";
    case RenderMeshErrorCode::PositionBudgetExceeded:
        return "render mesh resident position budget is exhausted";
    case RenderMeshErrorCode::TriangleIndexBudgetExceeded:
        return "render mesh resident triangle-index budget is exhausted";
    case RenderMeshErrorCode::LogicalByteBudgetExceeded:
        return "render mesh resident logical-byte budget is exhausted";
    case RenderMeshErrorCode::InvalidReservation:
        return "render mesh reservation is invalid";
    case RenderMeshErrorCode::InvalidHandle:
        return "render mesh handle is invalid";
    }
    return "render mesh error is unknown";
}

struct RenderMeshError
{
    RenderMeshErrorCode code = RenderMeshErrorCode::InvalidConfiguration;
    // Fixed category text only. It contains no source identity, resource identity, payload data,
    // backend message, or exception text.
    std::string_view message = RenderMeshErrorMessage(code);
};

struct RenderMeshReservation
{
    RenderMeshHandle handle;
    std::uint64_t position_count = 0U;
    std::uint64_t triangle_index_count = 0U;
    std::uint64_t logical_bytes = 0U;
};

struct RenderMeshMetadata
{
    RenderMeshHandle handle;
    std::uint64_t position_count = 0U;
    std::uint64_t triangle_index_count = 0U;
    std::uint64_t logical_bytes = 0U;
};

struct RenderMeshPoolSnapshot
{
    std::size_t slot_capacity = 0U;
    std::size_t free_slots = 0U;
    std::size_t reserved_slots = 0U;
    std::size_t resident_slots = 0U;
    std::size_t retired_slots = 0U;
    std::uint64_t reserved_positions = 0U;
    std::uint64_t resident_positions = 0U;
    std::uint64_t reserved_triangle_indices = 0U;
    std::uint64_t resident_triangle_indices = 0U;
    std::uint64_t reserved_logical_bytes = 0U;
    std::uint64_t resident_logical_bytes = 0U;

    friend constexpr bool operator==(
        const RenderMeshPoolSnapshot&, const RenderMeshPoolSnapshot&) noexcept = default;
};
} // namespace omega::runtime
