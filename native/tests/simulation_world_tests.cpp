#include "omega/simulation/simulation_world.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <utility>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}
} // namespace

int SimulationWorldFailureCount()
{
    using omega::simulation::SimulationStepResult;
    using omega::simulation::SimulationWorld;
    using omega::simulation::SimulationWorldConfig;

    Check(!SimulationWorld::Create({.fixed_step = std::chrono::nanoseconds::zero()}),
        "a zero fixed step is rejected");
    Check(!SimulationWorld::Create({.fixed_step = std::chrono::nanoseconds{-1}}),
        "a negative fixed step is rejected");

    constexpr auto step = std::chrono::nanoseconds{16'666'667};
    auto created = SimulationWorld::Create({.fixed_step = step});
    Check(created.has_value(), "a positive synthetic-shell fixed step is accepted");
    if (created)
    {
        SimulationWorld world = std::move(*created);
        const auto initial = world.Snapshot();
        Check(initial.completed_steps == 0U &&
                  initial.simulated_time == std::chrono::nanoseconds::zero(),
            "a new world starts at deterministic step and time zero");
        Check(world.config().fixed_step == step,
            "the validated fixed step remains immutable");

        Check(world.AdvanceOneStep() == SimulationStepResult::Advanced &&
                  world.AdvanceOneStep() == SimulationStepResult::Advanced &&
                  world.AdvanceOneStep() == SimulationStepResult::Advanced,
            "each explicit call advances exactly one step");
        const auto advanced = world.Snapshot();
        Check(advanced.completed_steps == 3U && advanced.simulated_time == step * 3,
            "step count and simulated time advance without wall-clock input");
    }

    auto first = SimulationWorld::Create({.fixed_step = std::chrono::microseconds{500}});
    auto second = SimulationWorld::Create({.fixed_step = std::chrono::microseconds{500}});
    Check(first && second, "matching deterministic worlds construct");
    if (first && second)
    {
        for (int index = 0; index < 17; ++index)
        {
            Check(first->AdvanceOneStep() == second->AdvanceOneStep(),
                "matching worlds return matching step results");
        }
        const auto left = first->Snapshot();
        const auto right = second->Snapshot();
        Check(left.completed_steps == right.completed_steps &&
                  left.simulated_time == right.simulated_time,
            "identical step sequences produce identical owned snapshots");
    }

    auto maximum = SimulationWorld::Create(
        {.fixed_step = std::chrono::nanoseconds::max()});
    Check(maximum && maximum->AdvanceOneStep() == SimulationStepResult::Advanced,
        "the largest positive representable step advances once");
    if (maximum)
    {
        const auto full = maximum->Snapshot();
        Check(maximum->AdvanceOneStep() == SimulationStepResult::RepresentationExhausted,
            "a second maximum-sized step reports representation exhaustion");
        const auto unchanged = maximum->Snapshot();
        Check(unchanged.completed_steps == full.completed_steps &&
                  unchanged.simulated_time == full.simulated_time,
            "representation exhaustion is atomic and leaves state unchanged");
    }
    return failures;
}
