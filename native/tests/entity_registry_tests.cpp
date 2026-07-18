#include "omega/simulation/entity_registry.h"

#include <cstdint>
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

bool IsConsistent(const omega::simulation::EntityRegistrySnapshot snapshot)
{
    return snapshot.capacity == snapshot.alive + snapshot.reusable + snapshot.retired;
}
} // namespace

int EntityRegistryFailureCount()
{
    using omega::simulation::EntityCreateError;
    using omega::simulation::EntityDestroyResult;
    using omega::simulation::EntityId;
    using omega::simulation::EntityRegistry;

    static_assert(!std::is_copy_constructible_v<EntityRegistry>);
    static_assert(!std::is_copy_assignable_v<EntityRegistry>);
    static_assert(std::is_nothrow_move_constructible_v<EntityRegistry>);
    static_assert(std::is_nothrow_move_assignable_v<EntityRegistry>);
    static_assert(noexcept(std::declval<EntityRegistry&>().CreateEntity()));
    static_assert(noexcept(std::declval<EntityRegistry&>().DestroyEntity(EntityId{})));
    static_assert(noexcept(std::declval<const EntityRegistry&>().IsAlive(EntityId{})));
    static_assert(noexcept(std::declval<const EntityRegistry&>().Snapshot()));

    Check(!EntityRegistry::Create(0U), "zero entity capacity is rejected");
    Check(!EntityRegistry::Create(EntityRegistry::kMaximumCapacity + 1U),
          "entity capacity above the hard limit is rejected");
    auto maximum_capacity = EntityRegistry::Create(EntityRegistry::kMaximumCapacity);
    Check(maximum_capacity &&
              maximum_capacity->Snapshot().capacity == EntityRegistry::kMaximumCapacity &&
              IsConsistent(maximum_capacity->Snapshot()),
        "the exact hard capacity remains accepted and internally consistent");

    auto created = EntityRegistry::Create(2U);
    Check(created.has_value(), "a bounded entity registry is created");
    if (created)
    {
        EntityRegistry registry = std::move(*created);
        const auto first = registry.CreateEntity();
        const auto second = registry.CreateEntity();
        Check(first && second, "every preallocated entity slot can be created");
        Check(first && first->index == 0U && first->generation == 1U,
              "initial identities are deterministic and generation one");
        Check(second && second->index == 1U && second->generation == 1U,
              "initial identities advance in deterministic slot order");
        const auto exhausted = registry.CreateEntity();
        const auto before_exhaustion = registry.Snapshot();
        Check(!exhausted && exhausted.error() == EntityCreateError::CapacityExhausted,
              "capacity exhaustion is explicit and non-mutating");
        Check(registry.Snapshot() == before_exhaustion && IsConsistent(registry.Snapshot()),
            "capacity exhaustion preserves every registry counter");

        const auto full = registry.Snapshot();
        Check(full.capacity == 2U && full.alive == 2U && full.reusable == 0U && full.retired == 0U,
              "the full-registry aggregate snapshot is exact");

        if (first)
        {
            const EntityId forged{.index = first->index, .generation = first->generation + 1U};
            const auto before_forgery = registry.Snapshot();
            Check(registry.DestroyEntity(forged) == EntityDestroyResult::NotAlive &&
                      registry.IsAlive(*first),
                  "a forged generation cannot destroy a live entity");
            Check(registry.Snapshot() == before_forgery &&
                      registry.DestroyEntity(EntityId{}) == EntityDestroyResult::NotAlive &&
                      registry.DestroyEntity(EntityId{
                          .index = std::numeric_limits<std::uint32_t>::max(),
                          .generation = first->generation,
                      }) == EntityDestroyResult::NotAlive &&
                      registry.DestroyEntity(EntityId{
                          .index = first->index,
                          .generation = std::numeric_limits<std::uint64_t>::max(),
                      }) == EntityDestroyResult::NotAlive,
                "default, out-of-range, and extreme forged handles are inert");
            Check(registry.Snapshot() == before_forgery,
                "all forged destroy attempts leave aggregate state unchanged");
            Check(registry.DestroyEntity(*first) == EntityDestroyResult::Destroyed,
                  "an exact live generation is destroyed");
            Check(!registry.IsAlive(*first) &&
                      registry.DestroyEntity(*first) == EntityDestroyResult::NotAlive,
                  "a destroyed handle stays stale and double destroy is inert");

            const auto replacement = registry.CreateEntity();
            Check(replacement && replacement->index == first->index &&
                      replacement->generation == first->generation + 1U,
                  "a reused slot receives a new generation");
            Check(replacement && registry.IsAlive(*replacement) && !registry.IsAlive(*first),
                  "new and stale generations cannot alias");
        }
    }

    auto left = EntityRegistry::Create(4U);
    auto right = EntityRegistry::Create(4U);
    Check(left && right, "matching deterministic registries construct");
    if (left && right)
    {
        for (std::uint32_t index = 0; index < 4U; ++index)
            Check(left->CreateEntity() == right->CreateEntity(),
                  "matching create sequences return identical handles");
        Check(left->Snapshot().alive == right->Snapshot().alive &&
                  left->Snapshot().reusable == right->Snapshot().reusable,
              "matching registry sequences retain matching aggregate state");
        const EntityId shared_value{.index = 0U, .generation = 1U};
        Check(left->IsAlive(shared_value) && right->IsAlive(shared_value),
            "entity values are deterministic but explicitly scoped to their issuing registry");
    }

    auto reuse_created = EntityRegistry::Create(3U);
    Check(reuse_created.has_value(), "the deterministic reuse registry constructs");
    if (reuse_created)
    {
        EntityRegistry registry = std::move(*reuse_created);
        const auto zero = registry.CreateEntity();
        const auto one = registry.CreateEntity();
        const auto two = registry.CreateEntity();
        Check(zero && one && two, "the deterministic reuse registry fills");
        if (zero && one && two)
        {
            Check(registry.DestroyEntity(*zero) == EntityDestroyResult::Destroyed &&
                      registry.DestroyEntity(*two) == EntityDestroyResult::Destroyed,
                "two nonadjacent identities are released in a known order");
            const auto first_reuse = registry.CreateEntity();
            const auto second_reuse = registry.CreateEntity();
            Check(first_reuse && *first_reuse == EntityId{.index = 2U, .generation = 2U} &&
                      second_reuse && *second_reuse == EntityId{.index = 0U, .generation = 2U},
                "released identities are reused deterministically in LIFO order");
            Check(registry.IsAlive(*one) && IsConsistent(registry.Snapshot()),
                "reuse does not disturb an unrelated live identity");
        }
    }

    auto churn_created = EntityRegistry::Create(1U);
    Check(churn_created.has_value(), "the single-slot churn registry constructs");
    if (churn_created)
    {
        EntityRegistry registry = std::move(*churn_created);
        bool generations_advance = true;
        constexpr std::uint64_t churn_count = 10'000U;
        for (std::uint64_t iteration = 0; iteration < churn_count; ++iteration)
        {
            const auto entity = registry.CreateEntity();
            if (!entity || entity->index != 0U || entity->generation != iteration + 1U ||
                registry.DestroyEntity(*entity) != EntityDestroyResult::Destroyed)
            {
                generations_advance = false;
                break;
            }
        }
        Check(generations_advance,
            "repeated reuse advances generations without losing the only slot");
        Check(registry.Snapshot() == omega::simulation::EntityRegistrySnapshot{
                                          .capacity = 1U,
                                          .alive = 0U,
                                          .reusable = 1U,
                                          .retired = 0U,
                                      },
            "bounded churn preserves exact aggregate state");
    }

    auto move_source = EntityRegistry::Create(2U);
    Check(move_source.has_value(), "the move source registry constructs");
    if (move_source)
    {
        const auto live = move_source->CreateEntity();
        Check(live.has_value(), "the move source owns one live identity");
        EntityRegistry moved = std::move(*move_source);
        Check(move_source->Snapshot() == omega::simulation::EntityRegistrySnapshot{} &&
                  !move_source->CreateEntity() && live && moved.IsAlive(*live),
            "move construction transfers storage and leaves an inert zero-capacity source");

        auto destination = EntityRegistry::Create(3U);
        Check(destination.has_value(), "the move-assignment destination constructs");
        if (destination && live)
        {
            const auto old_zero = destination->CreateEntity();
            const auto old_one = destination->CreateEntity();
            Check(old_zero && old_one, "the old destination owns distinct identities");
            *destination = std::move(moved);
            Check(destination->IsAlive(*live) &&
                      (!old_one || !destination->IsAlive(*old_one)) &&
                      moved.Snapshot() == omega::simulation::EntityRegistrySnapshot{},
                "move assignment replaces ownership and makes the source inert");

            const auto before_self_move = destination->Snapshot();
            EntityRegistry* const alias = &*destination;
            *destination = std::move(*alias);
            Check(destination->Snapshot() == before_self_move && destination->IsAlive(*live),
                "self move-assignment is explicitly non-destructive");
        }
    }
    return failures;
}
