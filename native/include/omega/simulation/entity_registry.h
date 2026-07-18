#pragma once

#include <cstdint>
#include <expected>
#include <limits>
#include <string>
#include <vector>

namespace omega::simulation
{
// EntityId is a non-owning, registry-scoped value rather than a capability
// token. Identical values may be alive in two independent registries; callers
// must use a handle only with the SimulationWorld that issued it.
struct EntityId
{
    std::uint32_t index = std::numeric_limits<std::uint32_t>::max();
    std::uint64_t generation = 0;

    [[nodiscard]] friend constexpr bool operator==(const EntityId&, const EntityId&) = default;
};

enum class EntityCreateError : std::uint8_t
{
    CapacityExhausted,
};

enum class EntityDestroyResult : std::uint8_t
{
    Destroyed,
    NotAlive,
};

struct EntityRegistrySnapshot
{
    std::uint32_t capacity = 0;
    std::uint32_t alive = 0;
    std::uint32_t reusable = 0;
    std::uint32_t retired = 0;

    [[nodiscard]] friend constexpr bool operator==(
        const EntityRegistrySnapshot&, const EntityRegistrySnapshot&) = default;
};

// Project-owned deterministic entity-identity storage. This is game state, not
// a lifecycle service: SimulationWorld is its sole owner, and future component
// stores key their plain data by EntityId. The registry is app-owned,
// non-hot-reloadable host state: no vtable, pointer, or storage view crosses a
// reload boundary. It performs no allocation after Create().
class EntityRegistry final
{
  public:
    static constexpr std::uint32_t kMaximumCapacity = 1U << 20U;

    // [any thread; reentrant, startup] Allocates all slot/free-list storage up front.
    [[nodiscard]] static std::expected<EntityRegistry, std::string> Create(std::uint32_t capacity);

    // [game thread, lifecycle] Transfers all storage without allocation. The
    // moved-from registry becomes an inert zero-capacity value that may be
    // queried, destroyed, or move-assigned.
    EntityRegistry(EntityRegistry&& other) noexcept;
    EntityRegistry& operator=(EntityRegistry&& other) noexcept;
    EntityRegistry(const EntityRegistry&) = delete;
    EntityRegistry& operator=(const EntityRegistry&) = delete;

    // [game thread] Returns the next deterministic reusable identity, or
    // CapacityExhausted.
    [[nodiscard]] std::expected<EntityId, EntityCreateError> CreateEntity() noexcept;

    // [game thread] Destroys only an exact live generation. Stale or forged
    // handles are inert.
    [[nodiscard]] EntityDestroyResult DestroyEntity(EntityId entity) noexcept;

    // [game thread] Tests an exact generation without mutating registry state.
    [[nodiscard]] bool IsAlive(EntityId entity) const noexcept;

    // [game thread] Returns aggregate state only; no internal storage view
    // escapes ownership.
    [[nodiscard]] EntityRegistrySnapshot Snapshot() const noexcept;

  private:
    struct Slot
    {
        std::uint64_t generation = 1;
        bool alive = false;
    };

    explicit EntityRegistry(std::uint32_t capacity);

    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_indices_;
    std::uint32_t alive_count_ = 0;
    std::uint32_t retired_count_ = 0;
};
} // namespace omega::simulation
