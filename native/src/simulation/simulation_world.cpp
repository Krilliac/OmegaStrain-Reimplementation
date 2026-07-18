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
    return state_;
}

const SimulationWorldConfig& SimulationWorld::config() const noexcept
{
    return config_;
}

EntityRegistry& SimulationWorld::entities() noexcept
{
    return entities_;
}

const EntityRegistry& SimulationWorld::entities() const noexcept
{
    return entities_;
}
} // namespace omega::simulation
