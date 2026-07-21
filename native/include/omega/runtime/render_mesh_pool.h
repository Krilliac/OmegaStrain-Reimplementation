#pragma once

#include "omega/runtime/render_mesh.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace omega::runtime
{
namespace detail
{
struct RenderMeshPoolTestAccess;
}

// Renderer-neutral metadata owner. The main/render thread reserves a slot before backend resource
// creation/upload, publishes only after backend success, and rolls back every failed transaction.
// A backend owns opaque resources in a parallel slot-indexed table. All methods are main/render-
// thread only and externally synchronized; this service is not hot-reloadable.
class RenderMeshPool final
{
public:
    [[nodiscard]] static std::expected<RenderMeshPool, RenderMeshError> Create(
        RenderMeshPoolConfig config = {});

    RenderMeshPool(RenderMeshPool&& other) noexcept;
    RenderMeshPool& operator=(RenderMeshPool&&) = delete;
    RenderMeshPool(const RenderMeshPool&) = delete;
    RenderMeshPool& operator=(const RenderMeshPool&) = delete;
    ~RenderMeshPool() = default;

    // [main/render thread] Validates the complete borrowed view, charges combined reserved and
    // resident position/index/logical-byte budgets, and returns one generation-scoped transaction.
    [[nodiscard]] std::expected<RenderMeshReservation, RenderMeshError> Reserve(
        RenderMeshUploadView upload) noexcept;

    // [main/render thread] Commits one reservation after backend creation and upload succeed.
    [[nodiscard]] std::expected<RenderMeshHandle, RenderMeshError> Publish(
        const RenderMeshReservation& reservation) noexcept;

    // [main/render thread] Atomically releases an unpublished reservation after backend failure.
    [[nodiscard]] std::expected<void, RenderMeshError> Rollback(
        const RenderMeshReservation& reservation) noexcept;

    // [main/render thread] Resolves only a published resident generation.
    [[nodiscard]] std::expected<RenderMeshMetadata, RenderMeshError> Get(
        const RenderMeshHandle& handle) const noexcept;

    // [main/render thread] Releases one resident generation and returns portable metadata so the
    // backend can clear its parallel slot. Maximum generation retires instead of wrapping.
    [[nodiscard]] std::expected<RenderMeshMetadata, RenderMeshError> Release(
        const RenderMeshHandle& handle) noexcept;

    // [main/render thread] Aggregate counters only; no pool or external resource identities.
    [[nodiscard]] RenderMeshPoolSnapshot Snapshot() const noexcept;

private:
    friend struct detail::RenderMeshPoolTestAccess;

    enum class SlotState : std::uint8_t
    {
        Free,
        Reserved,
        Resident,
        Retired,
    };

    struct Slot
    {
        std::uint64_t generation = 1U;
        std::uint64_t position_count = 0U;
        std::uint64_t triangle_index_count = 0U;
        std::uint64_t logical_bytes = 0U;
        std::uint32_t next_free = 0U;
        SlotState state = SlotState::Free;
    };

    RenderMeshPool(RenderMeshPoolConfig config, std::uint64_t identity);

    [[nodiscard]] RenderMeshHandle HandleFor(
        std::uint32_t slot_index, const Slot& slot) const noexcept;
    [[nodiscard]] bool Matches(
        const RenderMeshHandle& handle, SlotState required_state) const noexcept;
    void Recycle(std::uint32_t slot_index, Slot& slot) noexcept;

    RenderMeshPoolConfig config_{};
    std::uint64_t identity_ = 0U;
    std::vector<Slot> slots_;
    std::uint32_t free_head_ = 0U;
    std::size_t free_slots_ = 0U;
    std::size_t reserved_slots_ = 0U;
    std::size_t resident_slots_ = 0U;
    std::size_t retired_slots_ = 0U;
    std::uint64_t reserved_positions_ = 0U;
    std::uint64_t resident_positions_ = 0U;
    std::uint64_t reserved_triangle_indices_ = 0U;
    std::uint64_t resident_triangle_indices_ = 0U;
    std::uint64_t reserved_logical_bytes_ = 0U;
    std::uint64_t resident_logical_bytes_ = 0U;
};
} // namespace omega::runtime
