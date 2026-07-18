#include "omega/simulation/simulation_world.h"

#include <limits>

namespace omega::simulation
{
std::expected<SimulationWorld, std::string> SimulationWorld::Create(
    const SimulationWorldConfig& config)
{
    if (config.fixed_step <= std::chrono::nanoseconds::zero())
        return std::unexpected("simulation fixed step must be positive");
    return SimulationWorld(config);
}

SimulationWorld::SimulationWorld(const SimulationWorldConfig& config) noexcept
    : config_(config)
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
} // namespace omega::simulation
