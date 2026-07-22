#include "omega/simulation/simulation_world.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
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

[[nodiscard]] bool SameSimulationState(
    const omega::simulation::SimulationState left,
    const omega::simulation::SimulationState right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
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
    using omega::simulation::EntityTranslation;
    using omega::simulation::EntityRegistrySnapshot;
    using omega::simulation::Position3;
    using omega::simulation::PositionResetResult;
    using omega::simulation::PositionedEntityCreateError;
    using omega::simulation::SimulationStepInput;
    using omega::simulation::SimulationStepResult;
    using omega::simulation::SimulationWorld;
    using omega::simulation::SimulationWorldConfig;
    using omega::simulation::Translation3;

    static_assert(std::is_standard_layout_v<Position3>);
    static_assert(std::is_standard_layout_v<Translation3>);
    static_assert(std::is_standard_layout_v<EntityTranslation>);
    static_assert(!std::is_copy_constructible_v<SimulationWorld>);
    static_assert(!std::is_copy_assignable_v<SimulationWorld>);
    static_assert(std::is_nothrow_move_constructible_v<SimulationWorld>);
    static_assert(!std::is_move_assignable_v<SimulationWorld>);
    static_assert(std::is_nothrow_destructible_v<SimulationWorld>);
    static_assert(!HasMutableEntityRegistryAccessor<SimulationWorld>);
    static_assert(!HasConstEntityRegistryAccessor<SimulationWorld>);
    static_assert(noexcept(std::declval<SimulationWorld&>().CreateEntity()));
    static_assert(noexcept(
        std::declval<SimulationWorld&>().CreatePositionedEntity(Position3{})));
    static_assert(noexcept(std::declval<SimulationWorld&>().DestroyEntity(EntityId{})));
    static_assert(noexcept(std::declval<const SimulationWorld&>().IsAlive(EntityId{})));
    static_assert(noexcept(std::declval<const SimulationWorld&>().EntitySnapshot()));
    static_assert(noexcept(std::declval<const SimulationWorld&>().PositionOf(EntityId{})));
    static_assert(noexcept(std::declval<SimulationWorld&>().ResetPosition(
        EntityId{}, Position3{})));
    static_assert(noexcept(std::declval<SimulationWorld&>().AdvanceOneStep()));
    static_assert(noexcept(std::declval<SimulationWorld&>().AdvanceOneStep(
        std::declval<const SimulationStepInput&>())));
    static_assert(std::is_same_v<
        decltype(std::declval<const SimulationWorld&>().EntitySnapshot()),
        EntityRegistrySnapshot>);
    static_assert(std::is_same_v<
        decltype(std::declval<const SimulationWorld&>().PositionOf(EntityId{})),
        std::optional<Position3>>);
    static_assert(std::is_same_v<
        decltype(std::declval<SimulationWorld&>().ResetPosition(
            EntityId{}, Position3{})),
        PositionResetResult>);

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
    Check(!SimulationWorld::Create({
              .fixed_step = std::chrono::nanoseconds{1},
              .maximum_entities = 2U,
              .maximum_positioned_entities = 3U,
          }),
        "a position capacity above the world entity capacity is rejected");

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
        Check(world.config().maximum_positioned_entities == world.config().maximum_entities,
            "an omitted position capacity resolves to the configured entity capacity");
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
        Check(move_source->AdvanceOneStep() == SimulationStepResult::Advanced &&
                  move_source->AdvanceOneStep() == SimulationStepResult::Advanced,
            "the move source owns non-default deterministic clock state");
        SimulationWorld moved = std::move(*move_source);
        Check(entity && moved.IsAlive(*entity) &&
                  moved.config().fixed_step == std::chrono::microseconds{250} &&
                  moved.config().maximum_entities == 2U &&
                  moved.Snapshot().completed_steps == 2U &&
                  moved.Snapshot().simulated_time == std::chrono::microseconds{500} &&
                  moved.Snapshot().alive_entities == 1U,
            "world move construction transfers the registry, configuration, and clock");
    }

    auto positioned_created = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds{5},
        .maximum_entities = 3U,
        .maximum_positioned_entities = 2U,
    });
    Check(positioned_created.has_value(), "a bounded positioned-entity world constructs");
    if (positioned_created)
    {
        SimulationWorld& world = *positioned_created;
        Check(world.config().maximum_positioned_entities == 2U,
            "an explicit valid position capacity remains immutable");

        const Position3 first_position{.x = 11, .y = -22, .z = 33};
        const Position3 second_position{
            .x = std::numeric_limits<std::int64_t>::min(),
            .y = 0,
            .z = std::numeric_limits<std::int64_t>::max(),
        };
        const auto first_positioned = world.CreatePositionedEntity(first_position);
        const auto second_positioned = world.CreatePositionedEntity(second_position);
        Check(first_positioned && second_positioned &&
                  world.PositionOf(*first_positioned) == first_position &&
                  world.PositionOf(*second_positioned) == second_position,
            "positioned creation retains owned nonzero and extreme coordinate values");

        const auto before_position_exhaustion_entities = world.EntitySnapshot();
        const auto before_position_exhaustion_state = world.Snapshot();
        const auto position_exhausted = world.CreatePositionedEntity(Position3{});
        Check(!position_exhausted &&
                  position_exhausted.error() ==
                      PositionedEntityCreateError::PositionCapacityExhausted,
            "position capacity exhaustion is reported before allocating an identity");
        Check(world.EntitySnapshot() == before_position_exhaustion_entities &&
                  world.Snapshot().completed_steps ==
                      before_position_exhaustion_state.completed_steps &&
                  world.Snapshot().simulated_time ==
                      before_position_exhaustion_state.simulated_time,
            "position capacity exhaustion leaves registry and clock state unchanged");

        const auto unpositioned = world.CreateEntity();
        Check(unpositioned && !world.PositionOf(*unpositioned),
            "the legacy entity creator remains valid and creates no position");
        const auto before_both_exhausted = world.EntitySnapshot();
        const auto both_exhausted = world.CreatePositionedEntity(Position3{});
        Check(!both_exhausted &&
                  both_exhausted.error() ==
                      PositionedEntityCreateError::EntityCapacityExhausted,
            "identity exhaustion has priority when identity and position capacities are full");
        Check(world.EntitySnapshot() == before_both_exhausted,
            "simultaneous capacity exhaustion leaves identity reuse state unchanged");

        if (first_positioned)
        {
            Check(world.DestroyEntity(*first_positioned) == EntityDestroyResult::Destroyed &&
                      !world.PositionOf(*first_positioned),
                "destroying an exact positioned entity removes its position before reuse");
            const Position3 replacement_position{.x = -7, .y = 8, .z = -9};
            const auto replacement = world.CreatePositionedEntity(replacement_position);
            Check(replacement && replacement->index == first_positioned->index &&
                      replacement->generation == first_positioned->generation + 1U &&
                      world.PositionOf(*replacement) == replacement_position &&
                      !world.PositionOf(*first_positioned),
                "position capacity is restored and stale generations cannot see replacement data");
        }
    }

    auto reset_created = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds{7},
        .maximum_entities = 5U,
        .maximum_positioned_entities = 3U,
    });
    Check(reset_created.has_value(), "a bounded position-reset world constructs");
    if (reset_created)
    {
        SimulationWorld& world = *reset_created;
        const Position3 initial{.x = 10, .y = 20, .z = 30};
        const Position3 replacement{
            .x = std::numeric_limits<std::int64_t>::min(),
            .y = -4,
            .z = std::numeric_limits<std::int64_t>::max(),
        };
        const auto target = world.CreatePositionedEntity(initial);
        const auto unpositioned = world.CreateEntity();
        const auto stale = world.CreatePositionedEntity(
            Position3{.x = 101, .y = 102, .z = 103});
        const auto dead = world.CreateEntity();
        Check(target && unpositioned && stale && dead,
            "position-reset fixtures acquire positioned, plain, stale, and dead candidates");
        if (target && unpositioned && stale && dead)
        {
            Check(world.AdvanceOneStep() == SimulationStepResult::Advanced &&
                      world.AdvanceOneStep() == SimulationStepResult::Advanced,
                "the position-reset fixture owns nonzero clock state");
            Check(world.DestroyEntity(*stale) == EntityDestroyResult::Destroyed,
                "the position-reset stale fixture is destroyed before reuse");
            const Position3 reused_position{.x = -101, .y = -102, .z = -103};
            const auto reused = world.CreatePositionedEntity(reused_position);
            Check(reused && reused->index == stale->index &&
                      reused->generation == stale->generation + 1U,
                "the position-reset stale slot is occupied by a newer exact generation");
            Check(world.DestroyEntity(*dead) == EntityDestroyResult::Destroyed,
                "the position-reset dead fixture is destroyed without reuse");
            const auto filler = world.CreatePositionedEntity(
                Position3{.x = 201, .y = 202, .z = 203});
            Check(filler.has_value(),
                "the position store is full before replacing an existing position");

            const auto before_reset_state = world.Snapshot();
            const auto before_reset_entities = world.EntitySnapshot();
            Check(world.ResetPosition(*target, replacement) ==
                          PositionResetResult::Reset &&
                      world.PositionOf(*target) == replacement &&
                      world.IsAlive(*target) &&
                      SameSimulationState(world.Snapshot(), before_reset_state) &&
                      world.EntitySnapshot() == before_reset_entities,
                "reset replaces all position coordinates without changing clock or identity state");

            const Position3 before_rejected_target = *world.PositionOf(*target);
            const std::optional<Position3> before_reused =
                reused ? world.PositionOf(*reused) : std::nullopt;
            const EntityId forged{
                .index = target->index,
                .generation = target->generation + 1U,
            };
            const EntityId out_of_range{
                .index = std::numeric_limits<std::uint32_t>::max(),
                .generation = target->generation,
            };
            const EntityId not_alive[]{
                EntityId{}, forged, out_of_range, *stale, *dead};
            bool not_alive_is_atomic = true;
            for (const EntityId entity : not_alive)
            {
                const auto before_state = world.Snapshot();
                const auto before_entities = world.EntitySnapshot();
                const PositionResetResult result =
                    world.ResetPosition(entity, Position3{.x = 999});
                const bool current_is_atomic =
                    result == PositionResetResult::EntityNotAlive &&
                    world.PositionOf(*target) == before_rejected_target &&
                    (!reused || world.PositionOf(*reused) == before_reused) &&
                    SameSimulationState(world.Snapshot(), before_state) &&
                    world.EntitySnapshot() == before_entities;
                not_alive_is_atomic = not_alive_is_atomic && current_is_atomic;
            }
            Check(not_alive_is_atomic,
                "default, forged, out-of-range, stale, and dead reset targets are atomically rejected");

            const auto before_missing_state = world.Snapshot();
            const auto before_missing_entities = world.EntitySnapshot();
            Check(world.ResetPosition(*unpositioned, Position3{.x = 777}) ==
                          PositionResetResult::PositionNotPresent &&
                      !world.PositionOf(*unpositioned) &&
                      world.PositionOf(*target) == before_rejected_target &&
                      (!reused || world.PositionOf(*reused) == before_reused) &&
                      SameSimulationState(world.Snapshot(), before_missing_state) &&
                      world.EntitySnapshot() == before_missing_entities,
                "a live unpositioned reset target remains unpositioned without any world mutation");

            const Position3 second_reset{.x = 1, .y = 2, .z = 3};
            const auto before_second_state = world.Snapshot();
            const auto before_second_entities = world.EntitySnapshot();
            Check(world.ResetPosition(*target, second_reset) ==
                          PositionResetResult::Reset &&
                      world.PositionOf(*target) == second_reset &&
                      SameSimulationState(world.Snapshot(), before_second_state) &&
                      world.EntitySnapshot() == before_second_entities,
                "repeated resets replace an existing component deterministically");
        }
    }

    auto movement_created = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds{10},
        .maximum_entities = 4U,
        .maximum_positioned_entities = 2U,
    });
    Check(movement_created.has_value(), "the translation transaction world constructs");
    if (movement_created)
    {
        SimulationWorld& world = *movement_created;
        const Position3 initial{.x = 10, .y = -20, .z = 30};
        const auto mover = world.CreatePositionedEntity(initial);
        const auto unpositioned = world.CreateEntity();
        Check(mover && unpositioned, "translation fixtures acquire positioned and plain entities");
        if (mover && unpositioned)
        {
            Check(world.AdvanceOneStep() == SimulationStepResult::Advanced &&
                      world.AdvanceOneStep(SimulationStepInput{}) ==
                          SimulationStepResult::Advanced &&
                      world.PositionOf(*mover) == initial,
                "legacy and explicit neutral steps advance the clock without moving an entity");

            const SimulationStepInput movement{
                .translation = EntityTranslation{
                    .entity = *mover,
                    .delta = Translation3{.dx = 5, .dy = -6, .dz = 7},
                },
            };
            Check(world.AdvanceOneStep(movement) == SimulationStepResult::Advanced &&
                      world.PositionOf(*mover) == Position3{.x = 15, .y = -26, .z = 37},
                "one fixed-step translation updates all signed coordinates");

            const SimulationStepInput zero_translation{
                .translation = EntityTranslation{.entity = *mover},
            };
            Check(world.AdvanceOneStep(zero_translation) == SimulationStepResult::Advanced &&
                      world.PositionOf(*mover) == Position3{.x = 15, .y = -26, .z = 37},
                "an explicit zero translation validates its target and advances one step");
            Check(world.AdvanceOneStep(movement) == SimulationStepResult::Advanced &&
                      world.AdvanceOneStep(movement) == SimulationStepResult::Advanced &&
                      world.PositionOf(*mover) == Position3{.x = 25, .y = -38, .z = 51},
                "repeated held-style commands apply identically on each fixed step");

            const auto before_rejected_state = world.Snapshot();
            const auto before_rejected_position = world.PositionOf(*mover);
            const EntityId forged{
                .index = mover->index,
                .generation = mover->generation + 1U,
            };
            const EntityId out_of_range{
                .index = std::numeric_limits<std::uint32_t>::max(),
                .generation = mover->generation,
            };
            const EntityId invalid_targets[]{EntityId{}, forged, out_of_range};
            bool invalid_targets_rejected = true;
            for (const EntityId invalid : invalid_targets)
            {
                const SimulationStepInput invalid_input{
                    .translation = EntityTranslation{
                        .entity = invalid,
                        .delta = Translation3{
                            .dx = std::numeric_limits<std::int64_t>::max(),
                        },
                    },
                };
                invalid_targets_rejected = invalid_targets_rejected &&
                                           world.AdvanceOneStep(invalid_input) ==
                                               SimulationStepResult::EntityNotAlive;
            }
            Check(invalid_targets_rejected,
                "default, forged, and out-of-range translation targets are not alive");

            const auto stale = world.CreatePositionedEntity(Position3{.x = 1});
            Check(stale && world.DestroyEntity(*stale) == EntityDestroyResult::Destroyed,
                "a stale translation-target fixture is created and destroyed");
            if (stale)
            {
                const SimulationStepInput stale_input{
                    .translation = EntityTranslation{
                        .entity = *stale,
                        .delta = Translation3{.dx = 1},
                    },
                };
                Check(world.AdvanceOneStep(stale_input) == SimulationStepResult::EntityNotAlive,
                    "a stale exact-generation translation target is not alive");
            }

            const SimulationStepInput missing_position{
                .translation = EntityTranslation{
                    .entity = *unpositioned,
                    .delta = Translation3{
                        .dx = std::numeric_limits<std::int64_t>::max(),
                    },
                },
            };
            Check(world.AdvanceOneStep(missing_position) ==
                      SimulationStepResult::PositionNotPresent,
                "a live unpositioned target fails before coordinate representation checks");
            const auto after_rejected_state = world.Snapshot();
            Check(world.PositionOf(*mover) == before_rejected_position &&
                      after_rejected_state.completed_steps ==
                          before_rejected_state.completed_steps &&
                      after_rejected_state.simulated_time ==
                          before_rejected_state.simulated_time,
                "all rejected target operations leave position and clock state unchanged");
        }
    }

    struct OverflowCase
    {
        Position3 position{};
        Translation3 delta{};
    };
    constexpr OverflowCase overflow_cases[]{
        {
            .position = Position3{.x = std::numeric_limits<std::int64_t>::max()},
            .delta = Translation3{.dx = 1},
        },
        {
            .position = Position3{.x = std::numeric_limits<std::int64_t>::min()},
            .delta = Translation3{.dx = -1},
        },
        {
            .position = Position3{
                .x = 10,
                .y = std::numeric_limits<std::int64_t>::max(),
                .z = 30,
            },
            .delta = Translation3{.dx = 5, .dy = 1, .dz = 7},
        },
        {
            .position = Position3{.y = std::numeric_limits<std::int64_t>::min()},
            .delta = Translation3{.dy = -1},
        },
        {
            .position = Position3{.z = std::numeric_limits<std::int64_t>::max()},
            .delta = Translation3{.dz = 1},
        },
        {
            .position = Position3{.z = std::numeric_limits<std::int64_t>::min()},
            .delta = Translation3{.dz = -1},
        },
    };
    bool coordinate_overflow_is_atomic = true;
    for (const OverflowCase& overflow : overflow_cases)
    {
        auto overflow_world = SimulationWorld::Create({
            .fixed_step = std::chrono::nanoseconds{1},
            .maximum_entities = 1U,
        });
        if (!overflow_world)
        {
            coordinate_overflow_is_atomic = false;
            continue;
        }
        const auto entity = overflow_world->CreatePositionedEntity(overflow.position);
        if (!entity)
        {
            coordinate_overflow_is_atomic = false;
            continue;
        }
        const auto before = overflow_world->Snapshot();
        const SimulationStepInput input{
            .translation = EntityTranslation{
                .entity = *entity,
                .delta = overflow.delta,
            },
        };
        const auto result = overflow_world->AdvanceOneStep(input);
        const auto after = overflow_world->Snapshot();
        coordinate_overflow_is_atomic =
            coordinate_overflow_is_atomic &&
            result == SimulationStepResult::PositionRepresentationExhausted &&
            overflow_world->PositionOf(*entity) == overflow.position &&
            after.completed_steps == before.completed_steps &&
            after.simulated_time == before.simulated_time;
    }
    Check(coordinate_overflow_is_atomic,
        "positive and negative overflow on every axis leaves every coordinate and clock unchanged");

    auto clock_priority = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds::max(),
        .maximum_entities = 1U,
    });
    Check(clock_priority.has_value(), "the clock-priority fixture constructs");
    if (clock_priority)
    {
        const Position3 maximum_x{.x = std::numeric_limits<std::int64_t>::max()};
        const auto entity = clock_priority->CreatePositionedEntity(maximum_x);
        Check(entity && clock_priority->AdvanceOneStep() == SimulationStepResult::Advanced,
            "the clock-priority fixture reaches its final representable clock state");
        if (entity)
        {
            const auto before = clock_priority->Snapshot();
            const SimulationStepInput bad_target{
                .translation = EntityTranslation{
                    .entity = EntityId{},
                    .delta = Translation3{.dx = 1},
                },
            };
            const SimulationStepInput overflowing_position{
                .translation = EntityTranslation{
                    .entity = *entity,
                    .delta = Translation3{.dx = 1},
                },
            };
            Check(clock_priority->AdvanceOneStep(bad_target) ==
                          SimulationStepResult::RepresentationExhausted &&
                      clock_priority->AdvanceOneStep(overflowing_position) ==
                          SimulationStepResult::RepresentationExhausted,
                "clock representation exhaustion has priority over target and position errors");
            const auto after = clock_priority->Snapshot();
            Check(clock_priority->PositionOf(*entity) == maximum_x &&
                      after.completed_steps == before.completed_steps &&
                      after.simulated_time == before.simulated_time,
                "clock-priority failures leave position and clock state unchanged");
        }
    }

    auto deterministic_left = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds{3},
        .maximum_entities = 2U,
        .maximum_positioned_entities = 1U,
    });
    auto deterministic_right = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds{3},
        .maximum_entities = 2U,
        .maximum_positioned_entities = 1U,
    });
    Check(deterministic_left && deterministic_right,
        "matching positioned deterministic worlds construct");
    if (deterministic_left && deterministic_right)
    {
        const Position3 origin{.x = -4, .y = 5, .z = -6};
        const auto left_entity = deterministic_left->CreatePositionedEntity(origin);
        const auto right_entity = deterministic_right->CreatePositionedEntity(origin);
        bool results_match = left_entity && right_entity && left_entity == right_entity;
        if (left_entity && right_entity)
        {
            constexpr Translation3 deltas[]{
                {.dx = 1, .dy = 2, .dz = 3},
                {.dx = -4, .dy = 0, .dz = 8},
                {.dx = 7, .dy = -6, .dz = -5},
            };
            for (const Translation3 delta : deltas)
            {
                const SimulationStepInput left_input{
                    .translation = EntityTranslation{.entity = *left_entity, .delta = delta},
                };
                const SimulationStepInput right_input{
                    .translation = EntityTranslation{.entity = *right_entity, .delta = delta},
                };
                results_match = results_match &&
                                deterministic_left->AdvanceOneStep(left_input) ==
                                    deterministic_right->AdvanceOneStep(right_input);
            }
            const auto left_state = deterministic_left->Snapshot();
            const auto right_state = deterministic_right->Snapshot();
            Check(results_match &&
                      deterministic_left->PositionOf(*left_entity) ==
                          deterministic_right->PositionOf(*right_entity) &&
                      left_state.completed_steps == right_state.completed_steps &&
                      left_state.simulated_time == right_state.simulated_time,
                "matching positioned worlds produce identical results, positions, and clocks");
        }
    }

    auto positioned_move_source = SimulationWorld::Create({
        .fixed_step = std::chrono::nanoseconds{2},
        .maximum_entities = 2U,
        .maximum_positioned_entities = 1U,
    });
    Check(positioned_move_source.has_value(), "the positioned move source constructs");
    if (positioned_move_source)
    {
        const auto entity =
            positioned_move_source->CreatePositionedEntity(Position3{.x = 3, .y = 4, .z = 5});
        SimulationStepInput input{};
        if (entity)
        {
            input.translation = EntityTranslation{
                .entity = *entity,
                .delta = Translation3{.dx = 6, .dy = 7, .dz = 8},
            };
        }
        Check(entity && positioned_move_source->AdvanceOneStep(input) ==
                            SimulationStepResult::Advanced,
            "the positioned move source owns translated state before transfer");
        SimulationWorld moved = std::move(*positioned_move_source);
        Check(entity && moved.PositionOf(*entity) == Position3{.x = 9, .y = 11, .z = 13} &&
                  moved.config().maximum_positioned_entities == 1U &&
                  moved.Snapshot().completed_steps == 1U,
            "world move construction transfers position storage, resolved capacity, and clock");
    }
    return failures;
}
