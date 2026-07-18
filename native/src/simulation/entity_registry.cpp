#include "omega/simulation/entity_registry.h"

#include <new>
#include <utility>

namespace omega::simulation
{
std::expected<EntityRegistry, std::string> EntityRegistry::Create(const std::uint32_t capacity)
{
    if (capacity == 0U)
        return std::unexpected("entity capacity must be positive");
    if (capacity > kMaximumCapacity)
        return std::unexpected("entity capacity exceeds the hard limit");

    try
    {
        return EntityRegistry(capacity);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("entity registry allocation failed");
    }
}

EntityRegistry::EntityRegistry(const std::uint32_t capacity) : slots_(capacity)
{
    free_indices_.reserve(capacity);
    for (std::uint32_t index = capacity; index > 0U; --index)
        free_indices_.push_back(index - 1U);
}

std::expected<EntityId, EntityCreateError> EntityRegistry::CreateEntity() noexcept
{
    if (free_indices_.empty())
        return std::unexpected(EntityCreateError::CapacityExhausted);

    const std::uint32_t index = free_indices_.back();
    free_indices_.pop_back();
    Slot& slot = slots_[index];
    slot.alive = true;
    ++alive_count_;
    return EntityId{.index = index, .generation = slot.generation};
}

EntityDestroyResult EntityRegistry::DestroyEntity(const EntityId entity) noexcept
{
    if (!IsAlive(entity))
        return EntityDestroyResult::NotAlive;

    Slot& slot = slots_[entity.index];
    slot.alive = false;
    --alive_count_;
    if (slot.generation == std::numeric_limits<std::uint64_t>::max())
    {
        ++retired_count_;
    }
    else
    {
        ++slot.generation;
        free_indices_.push_back(entity.index);
    }
    return EntityDestroyResult::Destroyed;
}

bool EntityRegistry::IsAlive(const EntityId entity) const noexcept
{
    return entity.index < slots_.size() && slots_[entity.index].alive &&
           slots_[entity.index].generation == entity.generation;
}

EntityRegistrySnapshot EntityRegistry::Snapshot() const noexcept
{
    return EntityRegistrySnapshot{
        .capacity = static_cast<std::uint32_t>(slots_.size()),
        .alive = alive_count_,
        .reusable = static_cast<std::uint32_t>(free_indices_.size()),
        .retired = retired_count_,
    };
}
} // namespace omega::simulation
