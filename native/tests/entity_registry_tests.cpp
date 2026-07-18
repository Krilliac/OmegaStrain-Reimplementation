#include "omega/simulation/entity_registry.h"

#include <cstdint>
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

int EntityRegistryFailureCount()
{
    using omega::simulation::EntityCreateError;
    using omega::simulation::EntityDestroyResult;
    using omega::simulation::EntityId;
    using omega::simulation::EntityRegistry;

    Check(!EntityRegistry::Create(0U), "zero entity capacity is rejected");
    Check(!EntityRegistry::Create(EntityRegistry::kMaximumCapacity + 1U),
          "entity capacity above the hard limit is rejected");

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
        Check(!exhausted && exhausted.error() == EntityCreateError::CapacityExhausted,
              "capacity exhaustion is explicit and non-mutating");

        const auto full = registry.Snapshot();
        Check(full.capacity == 2U && full.alive == 2U && full.reusable == 0U && full.retired == 0U,
              "the full-registry aggregate snapshot is exact");

        if (first)
        {
            const EntityId forged{.index = first->index, .generation = first->generation + 1U};
            Check(registry.DestroyEntity(forged) == EntityDestroyResult::NotAlive &&
                      registry.IsAlive(*first),
                  "a forged generation cannot destroy a live entity");
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
    }
    return failures;
}
