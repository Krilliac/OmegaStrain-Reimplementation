#include "omega/simulation/simulation_world.h"

#include <limits>
#include <utility>

namespace omega::simulation
{
std::expected<SimulationWorld, std::string> SimulationWorld::Create(
    const SimulationWorldConfig& config)
{
    if (config.fixed_step <= std::chrono::nanoseconds::zero())
        return std::unexpected("simulation fixed step must be positive");
    auto entities = EntityRegistry::Create(config.maximum_entities);
    if (!entities)
        return std::unexpected("entity registry: " + entities.error());
    return SimulationWorld(config, std::move(*entities));
}

SimulationWorld::SimulationWorld(
    const SimulationWorldConfig& config, EntityRegistry entities) noexcept
    : config_(config), entities_(std::move(entities))
{
}

std::expected<EntityId, EntityCreateError> SimulationWorld::CreateEntity() noexcept
{
    return entities_.CreateEntity();
}

EntityDestroyResult SimulationWorld::DestroyEntity(const EntityId entity) noexcept
{
    if (!entities_.IsAlive(entity))
        return EntityDestroyResult::NotAlive;

    // Future direct component stores erase this exact generation here, in
    // member declaration order, before the registry makes the slot reusable.
    return entities_.DestroyEntity(entity);
}

bool SimulationWorld::IsAlive(const EntityId entity) const noexcept
{
    return entities_.IsAlive(entity);
}

EntityRegistrySnapshot SimulationWorld::EntitySnapshot() const noexcept
{
    return entities_.Snapshot();
}

SimulationStepResult SimulationWorld::AdvanceOneStep() noexcept
{
    if (state_.completed_steps == std::numeric_limits<std::uint64_t>::max() ||
        state_.simulated_time > std::chrono::nanoseconds::max() - config_.fixed_step)
        return SimulationStepResult::RepresentationExhausted;

    ++state_.completed_steps;
    state_.simulated_time += config_.fixed_step;
    return SimulationStepResult::Advanced;
}

SimulationState SimulationWorld::Snapshot() const noexcept
{
    SimulationState snapshot = state_;
    snapshot.alive_entities = EntitySnapshot().alive;
    return snapshot;
}

const SimulationWorldConfig& SimulationWorld::config() const noexcept
{
    return config_;
}
} // namespace omega::simulation
