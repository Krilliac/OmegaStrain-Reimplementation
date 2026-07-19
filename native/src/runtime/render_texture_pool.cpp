#include "omega/runtime/render_texture_pool.h"

#include <atomic>
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

[[nodiscard]] constexpr RenderTextureError Error(
    const RenderTextureErrorCode code) noexcept
{
    return RenderTextureError{.code = code, .message = RenderTextureErrorMessage(code)};
}

[[nodiscard]] std::uint64_t NextPoolIdentity() noexcept
{
    // Zero is permanently reserved for invalid/moved-from pools. Once the process-local sequence
    // reaches UINT64_MAX, it transitions to zero and fails closed rather than reusing an identity.
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

[[nodiscard]] std::expected<std::uint64_t, RenderTextureError> ExactRgba8Bytes(
    const Rgba8TextureUploadView upload) noexcept
{
    if (upload.width == 0U || upload.height == 0U)
        return std::unexpected(Error(RenderTextureErrorCode::InvalidImage));

    constexpr std::uint64_t channels = 4U;
    const std::uint64_t width = upload.width;
    const std::uint64_t height = upload.height;
    if (width > std::numeric_limits<std::uint64_t>::max() / height)
        return std::unexpected(Error(RenderTextureErrorCode::InvalidImage));
    const std::uint64_t pixel_count = width * height;
    if (pixel_count > std::numeric_limits<std::uint64_t>::max() / channels)
        return std::unexpected(Error(RenderTextureErrorCode::InvalidImage));
    const std::uint64_t logical_bytes = pixel_count * channels;
    if (logical_bytes != static_cast<std::uint64_t>(upload.pixels.size()))
        return std::unexpected(Error(RenderTextureErrorCode::InvalidImage));
    return logical_bytes;
}
} // namespace

std::expected<RenderTexturePool, RenderTextureError> RenderTexturePool::Create(
    const RenderTexturePoolConfig config)
{
    if (config.slot_capacity == 0U ||
        config.slot_capacity > kMaximumRenderTextureSlotCapacity ||
        config.maximum_resident_logical_bytes == 0U)
    {
        return std::unexpected(Error(RenderTextureErrorCode::InvalidConfiguration));
    }

    const std::uint64_t identity = NextPoolIdentity();
    if (identity == 0U)
        return std::unexpected(Error(RenderTextureErrorCode::PoolIdentityExhausted));

    try
    {
        return RenderTexturePool(config, identity);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(RenderTextureErrorCode::AllocationFailed));
    }
}

RenderTexturePool::RenderTexturePool(
    const RenderTexturePoolConfig config, const std::uint64_t identity)
    : config_(config), identity_(identity), slots_(config.slot_capacity), free_slots_(slots_.size())
{
    for (std::size_t index = 0U; index < slots_.size(); ++index)
    {
        slots_[index].next_free = index + 1U < slots_.size()
                                      ? static_cast<std::uint32_t>(index + 1U)
                                      : kNoFreeSlot;
    }
}

RenderTexturePool::RenderTexturePool(RenderTexturePool&& other) noexcept
    : config_(other.config_), identity_(std::exchange(other.identity_, 0U)),
      slots_(std::move(other.slots_)),
      free_head_(std::exchange(other.free_head_, kNoFreeSlot)),
      free_slots_(std::exchange(other.free_slots_, 0U)),
      reserved_slots_(std::exchange(other.reserved_slots_, 0U)),
      resident_slots_(std::exchange(other.resident_slots_, 0U)),
      retired_slots_(std::exchange(other.retired_slots_, 0U)),
      reserved_logical_bytes_(std::exchange(other.reserved_logical_bytes_, 0U)),
      resident_logical_bytes_(std::exchange(other.resident_logical_bytes_, 0U))
{
    // The standard permits a moved-from vector to retain a nonzero size. Normalize the inert
    // source so its aggregate snapshot cannot resemble another live owner of the transferred slots.
    other.slots_.clear();
    other.config_.slot_capacity = 0U;
    other.config_.maximum_resident_logical_bytes = 0U;
}

std::expected<RenderTextureReservation, RenderTextureError> RenderTexturePool::Reserve(
    const Rgba8TextureUploadView upload) noexcept
{
    auto exact_bytes = ExactRgba8Bytes(upload);
    if (!exact_bytes)
        return std::unexpected(exact_bytes.error());
    if (identity_ == 0U)
        return std::unexpected(Error(RenderTextureErrorCode::InvalidConfiguration));
    if (free_slots_ == 0U || free_head_ == kNoFreeSlot)
        return std::unexpected(Error(RenderTextureErrorCode::SlotCapacityExceeded));

    const std::uint64_t charged_bytes = reserved_logical_bytes_ + resident_logical_bytes_;
    if (*exact_bytes > config_.maximum_resident_logical_bytes - charged_bytes)
        return std::unexpected(Error(RenderTextureErrorCode::ResidentBudgetExceeded));

    const std::uint32_t slot_index = free_head_;
    Slot& slot = slots_[slot_index];
    free_head_ = slot.next_free;
    --free_slots_;

    slot.state = SlotState::Reserved;
    slot.next_free = kNoFreeSlot;
    slot.width = upload.width;
    slot.height = upload.height;
    slot.logical_bytes = *exact_bytes;
    ++reserved_slots_;
    reserved_logical_bytes_ += *exact_bytes;

    return RenderTextureReservation{
        .handle = HandleFor(slot_index, slot),
        .width = slot.width,
        .height = slot.height,
        .logical_bytes = slot.logical_bytes,
    };
}

std::expected<RenderTextureHandle, RenderTextureError> RenderTexturePool::Publish(
    const RenderTextureReservation& reservation) noexcept
{
    if (!Matches(reservation.handle, SlotState::Reserved))
        return std::unexpected(Error(RenderTextureErrorCode::InvalidReservation));
    Slot& slot = slots_[reservation.handle.slot_index];
    if (reservation.width != slot.width || reservation.height != slot.height ||
        reservation.logical_bytes != slot.logical_bytes)
    {
        return std::unexpected(Error(RenderTextureErrorCode::InvalidReservation));
    }

    slot.state = SlotState::Resident;
    --reserved_slots_;
    ++resident_slots_;
    reserved_logical_bytes_ -= slot.logical_bytes;
    resident_logical_bytes_ += slot.logical_bytes;
    return reservation.handle;
}

std::expected<void, RenderTextureError> RenderTexturePool::Rollback(
    const RenderTextureReservation& reservation) noexcept
{
    if (!Matches(reservation.handle, SlotState::Reserved))
        return std::unexpected(Error(RenderTextureErrorCode::InvalidReservation));
    Slot& slot = slots_[reservation.handle.slot_index];
    if (reservation.width != slot.width || reservation.height != slot.height ||
        reservation.logical_bytes != slot.logical_bytes)
    {
        return std::unexpected(Error(RenderTextureErrorCode::InvalidReservation));
    }

    --reserved_slots_;
    reserved_logical_bytes_ -= slot.logical_bytes;
    Recycle(reservation.handle.slot_index, slot);
    return {};
}

std::expected<RenderTextureMetadata, RenderTextureError> RenderTexturePool::Get(
    const RenderTextureHandle& handle) const noexcept
{
    if (!Matches(handle, SlotState::Resident))
        return std::unexpected(Error(RenderTextureErrorCode::InvalidHandle));
    const Slot& slot = slots_[handle.slot_index];
    return RenderTextureMetadata{
        .handle = handle,
        .width = slot.width,
        .height = slot.height,
        .logical_bytes = slot.logical_bytes,
    };
}

std::expected<RenderTextureMetadata, RenderTextureError> RenderTexturePool::Release(
    const RenderTextureHandle& handle) noexcept
{
    if (!Matches(handle, SlotState::Resident))
        return std::unexpected(Error(RenderTextureErrorCode::InvalidHandle));
    Slot& slot = slots_[handle.slot_index];
    const RenderTextureMetadata metadata{
        .handle = handle,
        .width = slot.width,
        .height = slot.height,
        .logical_bytes = slot.logical_bytes,
    };
    --resident_slots_;
    resident_logical_bytes_ -= slot.logical_bytes;
    Recycle(handle.slot_index, slot);
    return metadata;
}

RenderTexturePoolSnapshot RenderTexturePool::Snapshot() const noexcept
{
    return RenderTexturePoolSnapshot{
        .slot_capacity = slots_.size(),
        .free_slots = free_slots_,
        .reserved_slots = reserved_slots_,
        .resident_slots = resident_slots_,
        .retired_slots = retired_slots_,
        .reserved_logical_bytes = reserved_logical_bytes_,
        .resident_logical_bytes = resident_logical_bytes_,
    };
}

RenderTextureHandle RenderTexturePool::HandleFor(
    const std::uint32_t slot_index, const Slot& slot) const noexcept
{
    return RenderTextureHandle{
        .pool_identity = identity_,
        .generation = slot.generation,
        .slot_index = slot_index,
    };
}

bool RenderTexturePool::Matches(
    const RenderTextureHandle& handle, const SlotState required_state) const noexcept
{
    return handle.valid() && identity_ != 0U && handle.pool_identity == identity_ &&
           handle.slot_index < slots_.size() &&
           slots_[handle.slot_index].generation == handle.generation &&
           slots_[handle.slot_index].state == required_state;
}

void RenderTexturePool::Recycle(const std::uint32_t slot_index, Slot& slot) noexcept
{
    slot.logical_bytes = 0U;
    slot.width = 0U;
    slot.height = 0U;
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
