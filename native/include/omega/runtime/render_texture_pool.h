#pragma once

#include "omega/runtime/render_texture.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace omega::runtime
{
namespace detail
{
struct RenderTexturePoolTestAccess;
}

// SDL-free render-resource metadata owner. The main/render thread reserves a slot before backend
// creation/upload, publishes it only after backend success, and rolls it back on every failure.
// A backend keeps its opaque GPU pointers in a parallel slot-indexed table; neither pointer nor API
// type enters this pool. All methods are main/render-thread only and externally synchronized.
class RenderTexturePool final
{
public:
    [[nodiscard]] static std::expected<RenderTexturePool, RenderTextureError> Create(
        RenderTexturePoolConfig config = {});

    RenderTexturePool(RenderTexturePool&& other) noexcept;
    RenderTexturePool& operator=(RenderTexturePool&&) = delete;
    RenderTexturePool(const RenderTexturePool&) = delete;
    RenderTexturePool& operator=(const RenderTexturePool&) = delete;
    ~RenderTexturePool() = default;

    // [main/render thread] Validates exact tightly packed RGBA8 extent, charges the combined
    // reserved/resident logical budget, and returns one generation-scoped transaction.
    [[nodiscard]] std::expected<RenderTextureReservation, RenderTextureError> Reserve(
        Rgba8TextureUploadView upload) noexcept;

    // [main/render thread] Commits a reservation after backend creation and upload succeed.
    [[nodiscard]] std::expected<RenderTextureHandle, RenderTextureError> Publish(
        const RenderTextureReservation& reservation) noexcept;

    // [main/render thread] Atomically releases an unpublished reservation after backend failure.
    [[nodiscard]] std::expected<void, RenderTextureError> Rollback(
        const RenderTextureReservation& reservation) noexcept;

    // [main/render thread] Resolves only a published resident generation.
    [[nodiscard]] std::expected<RenderTextureMetadata, RenderTextureError> Get(
        const RenderTextureHandle& handle) const noexcept;

    // [main/render thread] Releases one published resident generation and returns its metadata so
    // the backend can clear its parallel resource slot. Maximum generation retires instead of
    // wrapping to a reusable identity.
    [[nodiscard]] std::expected<RenderTextureMetadata, RenderTextureError> Release(
        const RenderTextureHandle& handle) noexcept;

    // [main/render thread] Aggregate metadata only; no pool identity or external resource identity.
    [[nodiscard]] RenderTexturePoolSnapshot Snapshot() const noexcept;

private:
    friend struct detail::RenderTexturePoolTestAccess;

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
        std::uint64_t logical_bytes = 0U;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        std::uint32_t next_free = 0U;
        SlotState state = SlotState::Free;
    };

    RenderTexturePool(RenderTexturePoolConfig config, std::uint64_t identity);

    [[nodiscard]] RenderTextureHandle HandleFor(
        std::uint32_t slot_index, const Slot& slot) const noexcept;
    [[nodiscard]] bool Matches(
        const RenderTextureHandle& handle, SlotState required_state) const noexcept;
    void Recycle(std::uint32_t slot_index, Slot& slot) noexcept;

    RenderTexturePoolConfig config_{};
    std::uint64_t identity_ = 0U;
    std::vector<Slot> slots_;
    std::uint32_t free_head_ = 0U;
    std::size_t free_slots_ = 0U;
    std::size_t reserved_slots_ = 0U;
    std::size_t resident_slots_ = 0U;
    std::size_t retired_slots_ = 0U;
    std::uint64_t reserved_logical_bytes_ = 0U;
    std::uint64_t resident_logical_bytes_ = 0U;
};
} // namespace omega::runtime
