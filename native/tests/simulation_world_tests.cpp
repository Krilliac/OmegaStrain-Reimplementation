#include "omega/simulation/simulation_world.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <type_traits>
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

    static_assert(!std::is_copy_constructible_v<SimulationWorld>);
    static_assert(!std::is_copy_assignable_v<SimulationWorld>);
    static_assert(std::is_nothrow_move_constructible_v<SimulationWorld>);
    static_assert(std::is_nothrow_move_assignable_v<SimulationWorld>);

    Check(!SimulationWorld::Create({.fixed_step = std::chrono::nanoseconds::zero()}),
        "a zero fixed step is rejected");
    Check(!SimulationWorld::Create({.fixed_step = std::chrono::nanoseconds{-1}}),
        "a negative fixed step is rejected");
    Check(!SimulationWorld::Create({
              .fixed_step = std::chrono::nanoseconds{1},
              .maximum_entities = 0U,
          }),
        "an invalid world entity capacity is rejected");
    Check(!SimulationWorld::Create({
              .fixed_step = std::chrono::nanoseconds{1},
              .maximum_entities = omega::simulation::EntityRegistry::kMaximumCapacity + 1U,
          }),
        "a world entity capacity above the registry hard limit is rejected");

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
        Check(world.entities().Snapshot().capacity == 65'536U &&
                  world.entities().Snapshot().alive == 0U,
            "the world solely owns its preallocated empty entity registry");
        const auto entity = world.entities().CreateEntity();
        Check(entity && world.Snapshot().alive_entities == 1U,
            "the owned simulation snapshot copies the current live-entity count");
        Check(entity &&
                  world.entities().DestroyEntity(*entity) ==
                      omega::simulation::EntityDestroyResult::Destroyed &&
                  world.Snapshot().alive_entities == 0U,
            "destroyed identities disappear from the next owned simulation snapshot");

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
                  left.simulated_time == right.simulated_time &&
                  left.alive_entities == right.alive_entities,
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
                  unchanged.simulated_time == full.simulated_time &&
                  unchanged.alive_entities == full.alive_entities,
            "representation exhaustion is atomic and leaves state unchanged");
    }

    auto move_source = SimulationWorld::Create({
        .fixed_step = std::chrono::microseconds{250},
        .maximum_entities = 2U,
    });
    Check(move_source.has_value(), "a movable world with bounded identity storage constructs");
    if (move_source)
    {
        const auto entity = move_source->entities().CreateEntity();
        Check(entity.has_value(), "the world owns an identity before transfer");
        SimulationWorld moved = std::move(*move_source);
        Check(entity && moved.entities().IsAlive(*entity) &&
                  moved.config().maximum_entities == 2U &&
                  moved.Snapshot().completed_steps == 0U,
            "world move construction transfers the registry, configuration, and clock");

        auto destination = SimulationWorld::Create({
            .fixed_step = std::chrono::seconds{1},
            .maximum_entities = 1U,
        });
        Check(destination.has_value(), "the world move-assignment destination constructs");
        if (destination && entity)
        {
            *destination = std::move(moved);
            Check(destination->entities().IsAlive(*entity) &&
                      destination->config().fixed_step == std::chrono::microseconds{250},
                "world move assignment transfers entity ownership and immutable configuration");
        }
    }
    return failures;
}
