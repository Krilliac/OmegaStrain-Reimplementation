#pragma once

#include "omega/simulation/entity_registry.h"

#include <cstdint>
#include <expected>
#include <new>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace omega::simulation
{
// Component values remain project-owned plain state. They may not expose a
// polymorphic boundary, and every operation needed by the preallocated store is
// non-throwing after Create(). Owning allocations inside T remain the caller's
// responsibility and are discouraged for steady-state simulation values.
template <typename T>
concept ComponentValue = std::is_object_v<T> && std::is_standard_layout_v<T> &&
                         !std::is_pointer_v<T> && std::is_nothrow_move_constructible_v<T> &&
                         std::is_nothrow_move_assignable_v<T> &&
                         std::is_nothrow_destructible_v<T>;

enum class ComponentStoreWriteResult : std::uint8_t
{
    Inserted,
    Replaced,
    RegistryMismatch,
    EntityNotAlive,
    CapacityExhausted,
};

enum class ComponentStoreEraseResult : std::uint8_t
{
    Erased,
    RegistryMismatch,
    EntityNotAlive,
    NotPresent,
};

struct ComponentStoreSnapshot
{
    // Sparse slots are fixed to the issuing registry's startup capacity.
    std::uint32_t registry_slots = 0;
    // Maximum simultaneously occupied component values.
    std::uint32_t value_capacity = 0;
    // Values retained in slots, including inaccessible values whose entity was
    // destroyed before erase.
    std::uint32_t occupied = 0;
    // Occupied values whose exact EntityId is live in the supplied registry.
    std::uint32_t accessible = 0;
    bool registry_compatible = false;

    [[nodiscard]] friend constexpr bool operator==(
        const ComponentStoreSnapshot&, const ComponentStoreSnapshot&) = default;
};

// Header-only, deterministic component data storage intended to be owned as a
// direct member of SimulationWorld once a concrete project-owned component is
// justified. It is game state, not a service: no vtable, callback, global, or
// borrowed storage view crosses a hot-reload boundary. All sparse storage is
// allocated during Create(); Store, Find, Contains, Erase, EraseRetained,
// Clear, and Snapshot allocate nothing.
//
// EntityId intentionally carries no registry token. Supplying a registry
// validates its capacity and the handle's current liveness, but cannot detect a
// different registry with the same capacity and identical live numeric handle.
// SimulationWorld ownership and game-thread call sites enforce that scope.
template <ComponentValue T>
class ComponentStore final
{
  public:
    // [game thread, startup] Allocates one sparse slot per possible entity
    // index. value_capacity must be positive and no larger than the
    // registry's validated startup capacity.
    [[nodiscard]] static std::expected<ComponentStore, std::string> Create(
        const EntityRegistry& registry, const std::uint32_t value_capacity)
    {
        const std::uint32_t registry_capacity = registry.Snapshot().capacity;
        if (registry_capacity == 0U)
            return std::unexpected("component store requires a live registry capacity");
        if (value_capacity == 0U)
            return std::unexpected("component value capacity must be positive");
        if (value_capacity > registry_capacity)
            return std::unexpected("component value capacity exceeds the registry capacity");

        try
        {
            return ComponentStore(registry_capacity, value_capacity);
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected("component store allocation failed");
        }
    }

    // [game thread, lifecycle] Transfers all storage without allocation. The
    // moved-from store becomes an inert logical zero-capacity value: queries
    // return null/false/incompatible snapshots, registry-bound mutations
    // report RegistryMismatch, EraseRetained returns false, and Clear remains
    // safe.
    ComponentStore(ComponentStore&& other) noexcept
        : slots_(std::move(other.slots_)),
          registry_capacity_(std::exchange(other.registry_capacity_, 0U)),
          value_capacity_(std::exchange(other.value_capacity_, 0U)),
          occupied_(std::exchange(other.occupied_, 0U))
    {
        other.slots_.clear();
    }

    ComponentStore& operator=(ComponentStore&& other) noexcept
    {
        if (this == &other)
            return *this;

        slots_ = std::move(other.slots_);
        registry_capacity_ = std::exchange(other.registry_capacity_, 0U);
        value_capacity_ = std::exchange(other.value_capacity_, 0U);
        occupied_ = std::exchange(other.occupied_, 0U);
        other.slots_.clear();
        return *this;
    }

    ComponentStore(const ComponentStore&) = delete;
    ComponentStore& operator=(const ComponentStore&) = delete;

    // [game thread] Inserts or replaces a value only for an exact live EntityId
    // in a registry with the startup capacity captured by Create(). A stale
    // payload in this same sparse slot is replaced in O(1) without changing
    // occupancy. Unrelated stale payloads are never swept on this hot path and
    // continue to consume capacity until EraseRetained() or Clear().
    [[nodiscard]] ComponentStoreWriteResult Store(
        const EntityRegistry& registry, const EntityId entity, T value) noexcept
    {
        if (!RegistryMatches(registry))
            return ComponentStoreWriteResult::RegistryMismatch;
        if (!registry.IsAlive(entity))
            return ComponentStoreWriteResult::EntityNotAlive;

        Slot& slot = slots_[entity.index];
        if (slot.value && slot.generation == entity.generation)
        {
            *slot.value = std::move(value);
            return ComponentStoreWriteResult::Replaced;
        }

        if (slot.value)
        {
            slot.generation = entity.generation;
            *slot.value = std::move(value);
            return ComponentStoreWriteResult::Inserted;
        }

        if (occupied_ >= value_capacity_)
            return ComponentStoreWriteResult::CapacityExhausted;

        slot.generation = entity.generation;
        slot.value.emplace(std::move(value));
        ++occupied_;
        return ComponentStoreWriteResult::Inserted;
    }

    // [game thread] Returns a borrowed pointer only for an exact currently live
    // generation. It is invalidated by Store on the same entity, Erase, Clear,
    // move assignment, or destruction and may not cross a hot-reload boundary.
    [[nodiscard]] T* Find(const EntityRegistry& registry, const EntityId entity) noexcept
    {
        return const_cast<T*>(std::as_const(*this).Find(registry, entity));
    }

    [[nodiscard]] const T* Find(
        const EntityRegistry& registry, const EntityId entity) const noexcept
    {
        if (!RegistryMatches(registry) || !registry.IsAlive(entity))
            return nullptr;
        const Slot& slot = slots_[entity.index];
        return slot.value && slot.generation == entity.generation ? &*slot.value : nullptr;
    }

    // [game thread] Tests the same exact live-generation contract as Find().
    [[nodiscard]] bool Contains(
        const EntityRegistry& registry, const EntityId entity) const noexcept
    {
        return Find(registry, entity) != nullptr;
    }

    // [game thread] Erases only a present component on an exact live EntityId.
    // World lifecycle code must erase components before DestroyEntity(). A
    // destroyed/stale handle is inert; EraseRetained() is the explicit
    // generation-safe cleanup for destroy-before-erase ordering.
    [[nodiscard]] ComponentStoreEraseResult Erase(
        const EntityRegistry& registry, const EntityId entity) noexcept
    {
        if (!RegistryMatches(registry))
            return ComponentStoreEraseResult::RegistryMismatch;
        if (!registry.IsAlive(entity))
            return ComponentStoreEraseResult::EntityNotAlive;

        Slot& slot = slots_[entity.index];
        if (!slot.value || slot.generation != entity.generation)
            return ComponentStoreEraseResult::NotPresent;
        slot.value.reset();
        --occupied_;
        return ComponentStoreEraseResult::Erased;
    }

    // [game thread, lifecycle] Erases a retained payload by exact index and
    // generation without consulting registry liveness. This is safe immediately
    // after DestroyEntity(): an old handle cannot erase a newer generation in
    // the reused slot. Returns false for nonmatching-generation, out-of-range,
    // absent, or already-replaced handles. As with every EntityId operation,
    // the issuing-world scope remains a caller invariant.
    [[nodiscard]] bool EraseRetained(const EntityId entity) noexcept
    {
        if (entity.index >= slots_.size())
            return false;
        Slot& slot = slots_[entity.index];
        if (!slot.value || slot.generation != entity.generation)
            return false;
        slot.value.reset();
        --occupied_;
        return true;
    }

    // [game thread] Destroys every retained value without releasing sparse
    // startup storage. Safe and inert on a moved-from store.
    void Clear() noexcept
    {
        for (Slot& slot : slots_)
            slot.value.reset();
        occupied_ = 0U;
    }

    // [game thread] Returns aggregate owned state only. A mismatched registry
    // reports zero accessible values without exposing internal storage.
    [[nodiscard]] ComponentStoreSnapshot Snapshot(const EntityRegistry& registry) const noexcept
    {
        const bool compatible = RegistryMatches(registry);
        std::uint32_t accessible = 0U;
        if (compatible)
        {
            for (std::uint32_t index = 0U; index < registry_capacity_; ++index)
            {
                const Slot& slot = slots_[index];
                if (slot.value && registry.IsAlive(EntityId{
                                      .index = index,
                                      .generation = slot.generation,
                                  }))
                {
                    ++accessible;
                }
            }
        }
        return ComponentStoreSnapshot{
            .registry_slots = registry_capacity_,
            .value_capacity = value_capacity_,
            .occupied = occupied_,
            .accessible = accessible,
            .registry_compatible = compatible,
        };
    }

  private:
    struct Slot
    {
        std::uint64_t generation = 0U;
        std::optional<T> value;
    };

    ComponentStore(
        const std::uint32_t registry_capacity, const std::uint32_t value_capacity)
        : slots_(registry_capacity), registry_capacity_(registry_capacity),
          value_capacity_(value_capacity)
    {
    }

    [[nodiscard]] bool RegistryMatches(const EntityRegistry& registry) const noexcept
    {
        return registry_capacity_ != 0U &&
               registry.Snapshot().capacity == registry_capacity_;
    }

    std::vector<Slot> slots_;
    std::uint32_t registry_capacity_ = 0U;
    std::uint32_t value_capacity_ = 0U;
    std::uint32_t occupied_ = 0U;
};
} // namespace omega::simulation
