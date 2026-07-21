#include "omega/runtime/render_mesh_pool.h"

#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace omega::runtime
{
namespace
{
constexpr std::uint32_t kNoFreeSlot = std::numeric_limits<std::uint32_t>::max();

struct UploadFacts
{
    std::uint64_t position_count = 0U;
    std::uint64_t triangle_index_count = 0U;
    std::uint64_t logical_bytes = 0U;
};

[[nodiscard]] constexpr RenderMeshError Error(const RenderMeshErrorCode code) noexcept
{
    return RenderMeshError{.code = code, .message = RenderMeshErrorMessage(code)};
}

[[nodiscard]] std::uint64_t NextPoolIdentity() noexcept
{
    // Zero remains invalid. Exhaustion fails closed instead of reusing a process-local identity.
    static std::atomic<std::uint64_t> next_identity{1U};
    std::uint64_t current = next_identity.load(std::memory_order_relaxed);
    while (current != 0U)
    {
        const std::uint64_t next =
            current == std::numeric_limits<std::uint64_t>::max() ? 0U : current + 1U;
        if (next_identity.compare_exchange_weak(
                current, next, std::memory_order_relaxed, std::memory_order_relaxed))
            return current;
    }
    return 0U;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& output) noexcept
{
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    output = left * right;
    return true;
}

[[nodiscard]] std::expected<UploadFacts, RenderMeshError> Inspect(
    const RenderMeshUploadView upload) noexcept
{
    if (upload.positions.empty() || upload.triangle_indices.empty() ||
        upload.triangle_indices.size() % 3U != 0U)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidMesh));

    const std::uint64_t position_count = static_cast<std::uint64_t>(upload.positions.size());
    const std::uint64_t triangle_index_count =
        static_cast<std::uint64_t>(upload.triangle_indices.size());
    constexpr std::uint64_t addressable_positions =
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;
    if (position_count > addressable_positions)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidMesh));

    for (const asset::Float3IR& position : upload.positions)
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z))
            return std::unexpected(Error(RenderMeshErrorCode::InvalidMesh));
    }
    for (const std::uint32_t index : upload.triangle_indices)
    {
        if (index >= upload.positions.size())
            return std::unexpected(Error(RenderMeshErrorCode::InvalidMesh));
    }

    std::uint64_t position_bytes = 0U;
    std::uint64_t index_bytes = 0U;
    if (!Multiply(position_count, sizeof(asset::Float3IR), position_bytes) ||
        !Multiply(triangle_index_count, sizeof(std::uint32_t), index_bytes) ||
        index_bytes > std::numeric_limits<std::uint64_t>::max() - position_bytes)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidMesh));
    return UploadFacts{
        .position_count = position_count,
        .triangle_index_count = triangle_index_count,
        .logical_bytes = position_bytes + index_bytes,
    };
}
} // namespace

std::expected<RenderMeshPool, RenderMeshError> RenderMeshPool::Create(
    const RenderMeshPoolConfig config)
{
    if (config.slot_capacity == 0U || config.slot_capacity > kMaximumRenderMeshSlotCapacity ||
        config.maximum_resident_positions == 0U ||
        config.maximum_resident_triangle_indices == 0U ||
        config.maximum_resident_logical_bytes == 0U)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidConfiguration));

    const std::uint64_t identity = NextPoolIdentity();
    if (identity == 0U)
        return std::unexpected(Error(RenderMeshErrorCode::PoolIdentityExhausted));

    try
    {
        return RenderMeshPool(config, identity);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(RenderMeshErrorCode::AllocationFailed));
    }
}

RenderMeshPool::RenderMeshPool(
    const RenderMeshPoolConfig config, const std::uint64_t identity)
    : config_(config), identity_(identity), slots_(config.slot_capacity), free_slots_(slots_.size())
{
    for (std::size_t index = 0U; index < slots_.size(); ++index)
    {
        slots_[index].next_free = index + 1U < slots_.size()
                                      ? static_cast<std::uint32_t>(index + 1U)
                                      : kNoFreeSlot;
    }
}

RenderMeshPool::RenderMeshPool(RenderMeshPool&& other) noexcept
    : config_(other.config_), identity_(std::exchange(other.identity_, 0U)),
      slots_(std::move(other.slots_)),
      free_head_(std::exchange(other.free_head_, kNoFreeSlot)),
      free_slots_(std::exchange(other.free_slots_, 0U)),
      reserved_slots_(std::exchange(other.reserved_slots_, 0U)),
      resident_slots_(std::exchange(other.resident_slots_, 0U)),
      retired_slots_(std::exchange(other.retired_slots_, 0U)),
      reserved_positions_(std::exchange(other.reserved_positions_, 0U)),
      resident_positions_(std::exchange(other.resident_positions_, 0U)),
      reserved_triangle_indices_(std::exchange(other.reserved_triangle_indices_, 0U)),
      resident_triangle_indices_(std::exchange(other.resident_triangle_indices_, 0U)),
      reserved_logical_bytes_(std::exchange(other.reserved_logical_bytes_, 0U)),
      resident_logical_bytes_(std::exchange(other.resident_logical_bytes_, 0U))
{
    other.slots_.clear();
    other.config_ = {};
    other.config_.slot_capacity = 0U;
    other.config_.maximum_resident_positions = 0U;
    other.config_.maximum_resident_triangle_indices = 0U;
    other.config_.maximum_resident_logical_bytes = 0U;
}

std::expected<RenderMeshReservation, RenderMeshError> RenderMeshPool::Reserve(
    const RenderMeshUploadView upload) noexcept
{
    auto facts = Inspect(upload);
    if (!facts)
        return std::unexpected(facts.error());
    if (identity_ == 0U)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidConfiguration));
    if (free_slots_ == 0U || free_head_ == kNoFreeSlot)
        return std::unexpected(Error(RenderMeshErrorCode::SlotCapacityExceeded));

    const std::uint64_t charged_positions = reserved_positions_ + resident_positions_;
    if (facts->position_count > config_.maximum_resident_positions - charged_positions)
        return std::unexpected(Error(RenderMeshErrorCode::PositionBudgetExceeded));
    const std::uint64_t charged_indices =
        reserved_triangle_indices_ + resident_triangle_indices_;
    if (facts->triangle_index_count >
        config_.maximum_resident_triangle_indices - charged_indices)
        return std::unexpected(Error(RenderMeshErrorCode::TriangleIndexBudgetExceeded));
    const std::uint64_t charged_bytes = reserved_logical_bytes_ + resident_logical_bytes_;
    if (facts->logical_bytes > config_.maximum_resident_logical_bytes - charged_bytes)
        return std::unexpected(Error(RenderMeshErrorCode::LogicalByteBudgetExceeded));

    const std::uint32_t slot_index = free_head_;
    Slot& slot = slots_[slot_index];
    free_head_ = slot.next_free;
    --free_slots_;

    slot.state = SlotState::Reserved;
    slot.next_free = kNoFreeSlot;
    slot.position_count = facts->position_count;
    slot.triangle_index_count = facts->triangle_index_count;
    slot.logical_bytes = facts->logical_bytes;
    ++reserved_slots_;
    reserved_positions_ += facts->position_count;
    reserved_triangle_indices_ += facts->triangle_index_count;
    reserved_logical_bytes_ += facts->logical_bytes;

    return RenderMeshReservation{
        .handle = HandleFor(slot_index, slot),
        .position_count = slot.position_count,
        .triangle_index_count = slot.triangle_index_count,
        .logical_bytes = slot.logical_bytes,
    };
}

std::expected<RenderMeshHandle, RenderMeshError> RenderMeshPool::Publish(
    const RenderMeshReservation& reservation) noexcept
{
    if (!Matches(reservation.handle, SlotState::Reserved))
        return std::unexpected(Error(RenderMeshErrorCode::InvalidReservation));
    Slot& slot = slots_[reservation.handle.slot_index];
    if (reservation.position_count != slot.position_count ||
        reservation.triangle_index_count != slot.triangle_index_count ||
        reservation.logical_bytes != slot.logical_bytes)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidReservation));

    slot.state = SlotState::Resident;
    --reserved_slots_;
    ++resident_slots_;
    reserved_positions_ -= slot.position_count;
    resident_positions_ += slot.position_count;
    reserved_triangle_indices_ -= slot.triangle_index_count;
    resident_triangle_indices_ += slot.triangle_index_count;
    reserved_logical_bytes_ -= slot.logical_bytes;
    resident_logical_bytes_ += slot.logical_bytes;
    return reservation.handle;
}

std::expected<void, RenderMeshError> RenderMeshPool::Rollback(
    const RenderMeshReservation& reservation) noexcept
{
    if (!Matches(reservation.handle, SlotState::Reserved))
        return std::unexpected(Error(RenderMeshErrorCode::InvalidReservation));
    Slot& slot = slots_[reservation.handle.slot_index];
    if (reservation.position_count != slot.position_count ||
        reservation.triangle_index_count != slot.triangle_index_count ||
        reservation.logical_bytes != slot.logical_bytes)
        return std::unexpected(Error(RenderMeshErrorCode::InvalidReservation));

    --reserved_slots_;
    reserved_positions_ -= slot.position_count;
    reserved_triangle_indices_ -= slot.triangle_index_count;
    reserved_logical_bytes_ -= slot.logical_bytes;
    Recycle(reservation.handle.slot_index, slot);
    return {};
}

std::expected<RenderMeshMetadata, RenderMeshError> RenderMeshPool::Get(
    const RenderMeshHandle& handle) const noexcept
{
    if (!Matches(handle, SlotState::Resident))
        return std::unexpected(Error(RenderMeshErrorCode::InvalidHandle));
    const Slot& slot = slots_[handle.slot_index];
    return RenderMeshMetadata{
        .handle = handle,
        .position_count = slot.position_count,
        .triangle_index_count = slot.triangle_index_count,
        .logical_bytes = slot.logical_bytes,
    };
}

std::expected<RenderMeshMetadata, RenderMeshError> RenderMeshPool::Release(
    const RenderMeshHandle& handle) noexcept
{
    if (!Matches(handle, SlotState::Resident))
        return std::unexpected(Error(RenderMeshErrorCode::InvalidHandle));
    Slot& slot = slots_[handle.slot_index];
    const RenderMeshMetadata metadata{
        .handle = handle,
        .position_count = slot.position_count,
        .triangle_index_count = slot.triangle_index_count,
        .logical_bytes = slot.logical_bytes,
    };
    --resident_slots_;
    resident_positions_ -= slot.position_count;
    resident_triangle_indices_ -= slot.triangle_index_count;
    resident_logical_bytes_ -= slot.logical_bytes;
    Recycle(handle.slot_index, slot);
    return metadata;
}

RenderMeshPoolSnapshot RenderMeshPool::Snapshot() const noexcept
{
    return RenderMeshPoolSnapshot{
        .slot_capacity = slots_.size(),
        .free_slots = free_slots_,
        .reserved_slots = reserved_slots_,
        .resident_slots = resident_slots_,
        .retired_slots = retired_slots_,
        .reserved_positions = reserved_positions_,
        .resident_positions = resident_positions_,
        .reserved_triangle_indices = reserved_triangle_indices_,
        .resident_triangle_indices = resident_triangle_indices_,
        .reserved_logical_bytes = reserved_logical_bytes_,
        .resident_logical_bytes = resident_logical_bytes_,
    };
}

RenderMeshHandle RenderMeshPool::HandleFor(
    const std::uint32_t slot_index, const Slot& slot) const noexcept
{
    return RenderMeshHandle{
        .pool_identity = identity_,
        .generation = slot.generation,
        .slot_index = slot_index,
    };
}

bool RenderMeshPool::Matches(
    const RenderMeshHandle& handle, const SlotState required_state) const noexcept
{
    return handle.valid() && identity_ != 0U && handle.pool_identity == identity_ &&
           handle.slot_index < slots_.size() &&
           slots_[handle.slot_index].generation == handle.generation &&
           slots_[handle.slot_index].state == required_state;
}

void RenderMeshPool::Recycle(const std::uint32_t slot_index, Slot& slot) noexcept
{
    slot.position_count = 0U;
    slot.triangle_index_count = 0U;
    slot.logical_bytes = 0U;
    if (slot.generation == std::numeric_limits<std::uint64_t>::max())
    {
        slot.state = SlotState::Retired;
        slot.next_free = kNoFreeSlot;
        ++retired_slots_;
        return;
    }

    ++slot.generation;
    slot.state = SlotState::Free;
    slot.next_free = free_head_;
    free_head_ = slot_index;
    ++free_slots_;
}
} // namespace omega::runtime
