#include "omega/simulation/simulation_world.h"

#include <chrono>
#include <iostream>
#include <limits>
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

template <typename T>
concept HasMutableEntityRegistryAccessor = requires(T& value) { value.entities(); };

template <typename T>
concept HasConstEntityRegistryAccessor = requires(const T& value) { value.entities(); };
} // namespace

int SimulationWorldFailureCount()
{
    using omega::simulation::EntityCreateError;
    using omega::simulation::EntityDestroyResult;
    using omega::simulation::EntityId;
    using omega::simulation::EntityRegistrySnapshot;
    using omega::simulation::SimulationStepResult;
    using omega::simulation::SimulationWorld;
    using omega::simulation::SimulationWorldConfig;

    static_assert(!std::is_copy_constructible_v<SimulationWorld>);
    static_assert(!std::is_copy_assignable_v<SimulationWorld>);
    static_assert(std::is_nothrow_move_constructible_v<SimulationWorld>);
    static_assert(std::is_nothrow_move_assignable_v<SimulationWorld>);
    static_assert(!HasMutableEntityRegistryAccessor<SimulationWorld>);
    static_assert(!HasConstEntityRegistryAccessor<SimulationWorld>);
    static_assert(noexcept(std::declval<SimulationWorld&>().CreateEntity()));
    static_assert(noexcept(std::declval<SimulationWorld&>().DestroyEntity(EntityId{})));
    static_assert(noexcept(std::declval<const SimulationWorld&>().IsAlive(EntityId{})));
    static_assert(noexcept(std::declval<const SimulationWorld&>().EntitySnapshot()));
    static_assert(std::is_same_v<
        decltype(std::declval<const SimulationWorld&>().EntitySnapshot()),
        EntityRegistrySnapshot>);

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
        Check(world.EntitySnapshot() == EntityRegistrySnapshot{
                                            .capacity = 65'536U,
                                            .alive = 0U,
                                            .reusable = 65'536U,
                                            .retired = 0U,
                                        },
            "the world exposes only a value snapshot of its preallocated empty identity state");
        const auto entity = world.CreateEntity();
        Check(entity && world.IsAlive(*entity) && world.Snapshot().alive_entities == 1U &&
                  world.EntitySnapshot().alive == 1U,
            "the world lifecycle facade creates and reports one exact live identity");
        if (entity)
        {
            const EntityId nonmatching_generation{
                .index = entity->index,
                .generation = entity->generation + 1U,
            };
            const EntityId out_of_range{
                .index = std::numeric_limits<std::uint32_t>::max(),
                .generation = entity->generation,
            };
            const auto before_hostile_state = world.Snapshot();
            const auto before_hostile_entities = world.EntitySnapshot();
            Check(!world.IsAlive(EntityId{}) && !world.IsAlive(nonmatching_generation) &&
                      !world.IsAlive(out_of_range),
                "default, nonmatching-generation, and out-of-range handles are not alive");
            Check(world.DestroyEntity(EntityId{}) == EntityDestroyResult::NotAlive &&
                      world.DestroyEntity(nonmatching_generation) ==
                          EntityDestroyResult::NotAlive &&
                      world.DestroyEntity(out_of_range) == EntityDestroyResult::NotAlive &&
                      world.IsAlive(*entity),
                "hostile destruction attempts cannot affect an exact live identity");
            Check(world.Snapshot().completed_steps == before_hostile_state.completed_steps &&
                      world.Snapshot().simulated_time == before_hostile_state.simulated_time &&
                      world.Snapshot().alive_entities == before_hostile_state.alive_entities &&
                      world.EntitySnapshot() == before_hostile_entities,
                "all rejected lifecycle operations leave complete world state unchanged");

            Check(world.DestroyEntity(*entity) == EntityDestroyResult::Destroyed &&
                      !world.IsAlive(*entity) &&
                      world.DestroyEntity(*entity) == EntityDestroyResult::NotAlive &&
                      world.Snapshot().alive_entities == 0U,
                "exact destruction succeeds once and the stale handle remains inert");
            const auto replacement = world.CreateEntity();
            Check(replacement && replacement->index == entity->index &&
                      replacement->generation == entity->generation + 1U &&
                      world.IsAlive(*replacement) && !world.IsAlive(*entity),
                "world-owned deterministic reuse advances the sparse-slot generation");
            if (replacement)
            {
                Check(world.DestroyEntity(*replacement) == EntityDestroyResult::Destroyed,
                    "the replacement can leave through the same sole lifecycle path");
            }
        }

        Check(world.AdvanceOneStep() == SimulationStepResult::Advanced &&
                  world.AdvanceOneStep() == SimulationStepResult::Advanced &&
                  world.AdvanceOneStep() == SimulationStepResult::Advanced,
            "each explicit call advances exactly one step");
        const auto advanced = world.Snapshot();
        Check(advanced.completed_steps == 3U && advanced.simulated_time == step * 3,
            "step count and simulated time advance without wall-clock input");
    }

    auto bounded = SimulationWorld::Create({
        .fixed_step = std::chrono::microseconds{500},
        .maximum_entities = 2U,
    });
    Check(bounded.has_value(), "the bounded lifecycle world constructs");
    if (bounded)
    {
        const auto first_entity = bounded->CreateEntity();
        const auto second_entity = bounded->CreateEntity();
        const auto before_exhaustion = bounded->EntitySnapshot();
        const auto exhausted = bounded->CreateEntity();
        Check(first_entity && second_entity && !exhausted &&
                  exhausted.error() == EntityCreateError::CapacityExhausted,
            "the world lifecycle facade reports bounded identity exhaustion explicitly");
        Check(bounded->EntitySnapshot() == before_exhaustion &&
                  bounded->Snapshot().alive_entities == 2U,
            "identity capacity exhaustion is allocation-free and leaves world state unchanged");
    }

    auto first = SimulationWorld::Create({.fixed_step = std::chrono::microseconds{500}});
    auto second = SimulationWorld::Create({.fixed_step = std::chrono::microseconds{500}});
    Check(first && second, "matching deterministic worlds construct");
    if (first && second)
    {
        const auto first_entity = first->CreateEntity();
        const auto second_entity = second->CreateEntity();
        Check(first_entity && second_entity && first_entity == second_entity,
            "matching worlds issue identical world-scoped numeric identities");
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

    auto scope_left = SimulationWorld::Create({
        .fixed_step = std::chrono::microseconds{500},
        .maximum_entities = 1U,
    });
    auto scope_right = SimulationWorld::Create({
        .fixed_step = std::chrono::microseconds{500},
        .maximum_entities = 1U,
    });
    Check(scope_left && scope_right, "same-shaped world-scope fixtures construct");
    if (scope_left && scope_right)
    {
        const auto left_entity = scope_left->CreateEntity();
        const auto right_entity = scope_right->CreateEntity();
        Check(left_entity && right_entity && *left_entity == *right_entity &&
                  scope_left->IsAlive(*right_entity) && scope_right->IsAlive(*left_entity),
            "plain same-numeric EntityId values cannot identify their issuing world");
        Check(left_entity && right_entity &&
                  scope_left->DestroyEntity(*right_entity) == EntityDestroyResult::Destroyed &&
                  !scope_left->IsAlive(*left_entity) && scope_right->IsAlive(*right_entity),
            "issuing-world scope is a caller invariant rather than a capability check");
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
        const auto entity = move_source->CreateEntity();
        Check(entity.has_value(), "the world owns an identity before transfer");
        SimulationWorld moved = std::move(*move_source);
        Check(entity && moved.IsAlive(*entity) &&
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
            Check(destination->IsAlive(*entity) &&
                      destination->config().fixed_step == std::chrono::microseconds{250},
                "world move assignment transfers entity ownership and immutable configuration");
        }
    }
    return failures;
}
