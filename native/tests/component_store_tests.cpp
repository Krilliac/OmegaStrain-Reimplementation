#include "omega/simulation/component_store.h"

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

struct Position
{
    std::int32_t x = 0;
    std::int32_t y = 0;

    [[nodiscard]] friend constexpr bool operator==(const Position&, const Position&) = default;
};

struct ThrowingMove
{
    std::int32_t value = 0;

    ThrowingMove() = default;
    ThrowingMove(ThrowingMove&& other) noexcept(false) : value(other.value) {}
    ThrowingMove& operator=(ThrowingMove&& other) noexcept(false)
    {
        value = other.value;
        return *this;
    }
};

struct PolymorphicValue
{
    virtual ~PolymorphicValue() = default;
};

struct TrackedValue
{
    static inline std::int32_t alive = 0;

    std::int32_t value = 0;

    explicit TrackedValue(const std::int32_t initial = 0) noexcept : value(initial)
    {
        ++alive;
    }

    TrackedValue(TrackedValue&& other) noexcept : value(other.value)
    {
        ++alive;
    }

    TrackedValue& operator=(TrackedValue&& other) noexcept
    {
        value = other.value;
        return *this;
    }

    ~TrackedValue() noexcept
    {
        --alive;
    }
};
} // namespace

int ComponentStoreFailureCount()
{
    using omega::simulation::ComponentStore;
    using omega::simulation::ComponentStoreEraseResult;
    using omega::simulation::ComponentStoreSnapshot;
    using omega::simulation::ComponentStoreWriteResult;
    using omega::simulation::ComponentValue;
    using omega::simulation::EntityDestroyResult;
    using omega::simulation::EntityId;
    using omega::simulation::EntityRegistry;

    using PositionStore = ComponentStore<Position>;
    static_assert(ComponentValue<Position>);
    static_assert(ComponentValue<TrackedValue>);
    static_assert(!ComponentValue<std::int32_t*>);
    static_assert(!ComponentValue<ThrowingMove>);
    static_assert(!ComponentValue<PolymorphicValue>);
    static_assert(!std::is_copy_constructible_v<PositionStore>);
    static_assert(!std::is_copy_assignable_v<PositionStore>);
    static_assert(std::is_nothrow_move_constructible_v<PositionStore>);
    static_assert(std::is_nothrow_move_assignable_v<PositionStore>);
    static_assert(std::is_nothrow_destructible_v<PositionStore>);
    static_assert(noexcept(std::declval<PositionStore&>().Store(
        std::declval<const EntityRegistry&>(), EntityId{}, Position{})));
    static_assert(noexcept(std::declval<PositionStore&>().Find(
        std::declval<const EntityRegistry&>(), EntityId{})));
    static_assert(noexcept(std::declval<const PositionStore&>().Find(
        std::declval<const EntityRegistry&>(), EntityId{})));
    static_assert(noexcept(std::declval<const PositionStore&>().Contains(
        std::declval<const EntityRegistry&>(), EntityId{})));
    static_assert(noexcept(std::declval<PositionStore&>().Erase(
        std::declval<const EntityRegistry&>(), EntityId{})));
    static_assert(noexcept(std::declval<PositionStore&>().EraseRetained(EntityId{})));
    static_assert(noexcept(std::declval<PositionStore&>().Clear()));
    static_assert(noexcept(std::declval<const PositionStore&>().Snapshot(
        std::declval<const EntityRegistry&>())));

    auto creation_registry = EntityRegistry::Create(4U);
    Check(creation_registry.has_value(), "the component-store creation registry constructs");
    if (creation_registry)
    {
        Check(!PositionStore::Create(*creation_registry, 0U),
            "zero component value capacity is rejected");
        Check(!PositionStore::Create(*creation_registry, 5U),
            "component value capacity above the registry capacity is rejected");
        auto exact = PositionStore::Create(*creation_registry, 4U);
        Check(exact && exact->Snapshot(*creation_registry) == ComponentStoreSnapshot{
                                                               .registry_slots = 4U,
                                                               .value_capacity = 4U,
                                                               .occupied = 0U,
                                                               .accessible = 0U,
                                                               .registry_compatible = true,
                                                           },
            "the exact registry capacity is accepted and fully preallocated");

        EntityRegistry live_registry = std::move(*creation_registry);
        Check(!PositionStore::Create(*creation_registry, 1U) &&
                  live_registry.Snapshot().capacity == 4U,
            "an inert moved-from registry cannot seed component storage");
    }

    auto basic_registry = EntityRegistry::Create(4U);
    Check(basic_registry.has_value(), "the basic component registry constructs");
    if (basic_registry)
    {
        auto store = PositionStore::Create(*basic_registry, 2U);
        const auto first = basic_registry->CreateEntity();
        const auto second = basic_registry->CreateEntity();
        const auto third = basic_registry->CreateEntity();
        Check(store && first && second && third,
            "the basic store and three deterministic identities construct");
        if (store && first && second && third)
        {
            Check(store->Store(*basic_registry, *first, Position{.x = 10, .y = 20}) ==
                      ComponentStoreWriteResult::Inserted,
                "a live entity receives its first component");
            const Position* const found_first = store->Find(*basic_registry, *first);
            Check(store->Contains(*basic_registry, *first) && found_first != nullptr &&
                      *found_first == Position{.x = 10, .y = 20},
                "an exact live generation exposes its component");
            Position* const mutable_position = store->Find(*basic_registry, *first);
            if (mutable_position != nullptr)
                mutable_position->x = 11;
            const PositionStore& const_store = *store;
            const Position* const const_position = const_store.Find(*basic_registry, *first);
            Check(const_position != nullptr && const_position->x == 11,
                "the mutable and const borrowed views observe the owned value");
            Check(store->Store(*basic_registry, *first, Position{.x = 30, .y = 40}) ==
                          ComponentStoreWriteResult::Replaced &&
                      store->Snapshot(*basic_registry) == ComponentStoreSnapshot{
                                                              .registry_slots = 4U,
                                                              .value_capacity = 2U,
                                                              .occupied = 1U,
                                                              .accessible = 1U,
                                                              .registry_compatible = true,
                                                          },
                "replacement preserves occupancy and updates the exact value");

            const EntityId forged_generation{
                .index = first->index,
                .generation = first->generation + 1U,
            };
            const EntityId out_of_range{
                .index = std::numeric_limits<std::uint32_t>::max(),
                .generation = first->generation,
            };
            const auto before_hostile = store->Snapshot(*basic_registry);
            Check(store->Store(*basic_registry, EntityId{}, Position{}) ==
                          ComponentStoreWriteResult::EntityNotAlive &&
                      store->Store(*basic_registry, forged_generation, Position{}) ==
                          ComponentStoreWriteResult::EntityNotAlive &&
                      store->Store(*basic_registry, out_of_range, Position{}) ==
                          ComponentStoreWriteResult::EntityNotAlive,
                "default, forged-generation, and out-of-range writes are inert");
            Check(store->Find(*basic_registry, forged_generation) == nullptr &&
                      !store->Contains(*basic_registry, out_of_range) &&
                      store->Erase(*basic_registry, EntityId{}) ==
                          ComponentStoreEraseResult::EntityNotAlive &&
                      !store->EraseRetained(EntityId{}) &&
                      !store->EraseRetained(forged_generation) &&
                      !store->EraseRetained(out_of_range) &&
                      store->Snapshot(*basic_registry) == before_hostile,
                "hostile reads, erases, and retained cleanups preserve aggregate component state");

            Check(store->Store(*basic_registry, *second, Position{.x = 50, .y = 60}) ==
                          ComponentStoreWriteResult::Inserted &&
                      store->Store(*basic_registry, *third, Position{.x = 70, .y = 80}) ==
                          ComponentStoreWriteResult::CapacityExhausted,
                "bounded value capacity rejects a third live component");
            Check(store->Snapshot(*basic_registry).occupied == 2U &&
                      !store->Contains(*basic_registry, *third),
                "capacity exhaustion leaves all component state unchanged");
            Check(store->Erase(*basic_registry, *third) ==
                      ComponentStoreEraseResult::NotPresent,
                "erasing a live entity without a component is explicit");
            Check(store->Erase(*basic_registry, *first) == ComponentStoreEraseResult::Erased &&
                      store->Erase(*basic_registry, *first) ==
                          ComponentStoreEraseResult::NotPresent,
                "erase is exact and repeated erase is inert");
            store->Clear();
            store->Clear();
            Check(store->Snapshot(*basic_registry) == ComponentStoreSnapshot{
                                                          .registry_slots = 4U,
                                                          .value_capacity = 2U,
                                                          .occupied = 0U,
                                                          .accessible = 0U,
                                                          .registry_compatible = true,
                                                      },
                "clear is idempotent without releasing startup capacity");
        }
    }

    auto scoped_registry = EntityRegistry::Create(2U);
    auto wrong_capacity_registry = EntityRegistry::Create(3U);
    Check(scoped_registry && wrong_capacity_registry,
        "registry-mismatch fixtures construct");
    if (scoped_registry && wrong_capacity_registry)
    {
        auto store = PositionStore::Create(*scoped_registry, 1U);
        const auto entity = scoped_registry->CreateEntity();
        const auto wrong_entity = wrong_capacity_registry->CreateEntity();
        Check(store && entity && wrong_entity, "registry-mismatch identities construct");
        if (store && entity && wrong_entity)
        {
            Check(store->Store(*scoped_registry, *entity, Position{.x = 1, .y = 2}) ==
                      ComponentStoreWriteResult::Inserted,
                "the issuing registry can populate its store");
            Check(store->Store(*wrong_capacity_registry, *wrong_entity, Position{}) ==
                          ComponentStoreWriteResult::RegistryMismatch &&
                      store->Find(*wrong_capacity_registry, *wrong_entity) == nullptr &&
                      !store->Contains(*wrong_capacity_registry, *wrong_entity) &&
                      store->Erase(*wrong_capacity_registry, *wrong_entity) ==
                          ComponentStoreEraseResult::RegistryMismatch,
                "a structurally incompatible registry is rejected by every operation");
            Check(store->Snapshot(*wrong_capacity_registry) == ComponentStoreSnapshot{
                                                                   .registry_slots = 2U,
                                                                   .value_capacity = 1U,
                                                                   .occupied = 1U,
                                                                   .accessible = 0U,
                                                                   .registry_compatible = false,
                                                               },
                "a mismatched snapshot distinguishes retained from accessible values");
        }
    }

    auto same_shape_left = EntityRegistry::Create(2U);
    auto same_shape_right = EntityRegistry::Create(2U);
    Check(same_shape_left && same_shape_right, "same-shaped registry fixtures construct");
    if (same_shape_left && same_shape_right)
    {
        auto store = PositionStore::Create(*same_shape_left, 1U);
        const auto left_entity = same_shape_left->CreateEntity();
        const auto right_entity = same_shape_right->CreateEntity();
        Check(store && left_entity && right_entity && *left_entity == *right_entity,
            "same-shaped registries can issue identical numeric entity values");
        if (store && left_entity && right_entity)
        {
            const auto write = store->Store(
                *same_shape_left, *left_entity, Position{.x = 41, .y = 42});
            const Position* const foreign_view =
                store->Find(*same_shape_right, *right_entity);
            Check(write == ComponentStoreWriteResult::Inserted && foreign_view != nullptr &&
                      foreign_view->x == 41,
                "plain EntityId cannot detect a foreign same-shaped registry; world scope is required");
        }
    }

    auto stale_registry = EntityRegistry::Create(2U);
    Check(stale_registry.has_value(), "the stale-generation registry constructs");
    if (stale_registry)
    {
        auto store = PositionStore::Create(*stale_registry, 1U);
        const auto stale = stale_registry->CreateEntity();
        Check(store && stale, "the stale-generation fixture constructs");
        if (store && stale)
        {
            Check(store->Store(*stale_registry, *stale, Position{.x = 7, .y = 8}) ==
                          ComponentStoreWriteResult::Inserted &&
                      stale_registry->DestroyEntity(*stale) == EntityDestroyResult::Destroyed,
                "destroy-before-erase creates a retained inaccessible payload");
            Check(store->Find(*stale_registry, *stale) == nullptr &&
                      !store->Contains(*stale_registry, *stale) &&
                      store->Erase(*stale_registry, *stale) ==
                          ComponentStoreEraseResult::EntityNotAlive &&
                      store->Snapshot(*stale_registry).occupied == 1U &&
                      store->Snapshot(*stale_registry).accessible == 0U,
                "a destroyed generation is inert while its retained payload remains observable only in aggregate");

            const auto replacement = stale_registry->CreateEntity();
            const auto unrelated = stale_registry->CreateEntity();
            Check(replacement && unrelated && replacement->index == stale->index &&
                      replacement->generation == stale->generation + 1U,
                "registry reuse produces a distinct replacement before the unrelated identity");
            if (replacement && unrelated)
            {
                Check(store->Erase(*stale_registry, *replacement) ==
                              ComponentStoreEraseResult::NotPresent &&
                          store->Snapshot(*stale_registry).occupied == 1U &&
                          store->Snapshot(*stale_registry).accessible == 0U,
                    "a live replacement generation cannot erase its predecessor's retained payload");
                Check(store->Store(*stale_registry, *unrelated, Position{.x = 9, .y = 10}) ==
                              ComponentStoreWriteResult::CapacityExhausted &&
                          store->Snapshot(*stale_registry) == ComponentStoreSnapshot{
                                                                  .registry_slots = 2U,
                                                                  .value_capacity = 1U,
                                                                  .occupied = 1U,
                                                                  .accessible = 0U,
                                                                  .registry_compatible = true,
                                                              },
                    "unrelated stale occupancy fails capacity closed without a registry-wide sweep");
                Check(!store->EraseRetained(*replacement) &&
                          store->EraseRetained(*stale) &&
                          !store->EraseRetained(*stale) &&
                          store->Snapshot(*stale_registry).occupied == 0U,
                    "exact-generation retained cleanup works after destroy and is idempotent");
                Check(store->Store(*stale_registry, *unrelated, Position{.x = 9, .y = 10}) ==
                          ComponentStoreWriteResult::Inserted,
                    "explicit retained cleanup releases capacity for an unrelated live entity");
                const Position* const unrelated_value =
                    store->Find(*stale_registry, *unrelated);
                Check(!store->Contains(*stale_registry, *stale) && unrelated_value != nullptr &&
                          unrelated_value->x == 9 &&
                          store->Store(*stale_registry, *replacement, Position{}) ==
                              ComponentStoreWriteResult::CapacityExhausted,
                    "stale generations never alias replacements after explicit cleanup");
                Check(store->Erase(*stale_registry, *unrelated) ==
                              ComponentStoreEraseResult::Erased &&
                          stale_registry->DestroyEntity(*unrelated) ==
                              EntityDestroyResult::Destroyed,
                    "the documented erase-before-destroy ordering removes payloads immediately");
                Check(store->Store(
                          *stale_registry, *replacement, Position{.x = 12, .y = 13}) ==
                              ComponentStoreWriteResult::Inserted &&
                          !store->Contains(*stale_registry, *stale) &&
                          store->Contains(*stale_registry, *replacement),
                    "the replacement generation can own the reused sparse slot without stale aliasing");
                Check(!store->EraseRetained(*stale),
                    "an old exact-generation handle cannot erase the replacement component");
            }
        }
    }

    auto same_slot_registry = EntityRegistry::Create(1U);
    Check(same_slot_registry.has_value(), "the same-slot replacement registry constructs");
    if (same_slot_registry)
    {
        auto store = PositionStore::Create(*same_slot_registry, 1U);
        const auto original = same_slot_registry->CreateEntity();
        Check(store && original, "the same-slot replacement fixture constructs");
        if (store && original)
        {
            Check(store->Store(*same_slot_registry, *original, Position{.x = 2, .y = 3}) ==
                          ComponentStoreWriteResult::Inserted &&
                      same_slot_registry->DestroyEntity(*original) ==
                          EntityDestroyResult::Destroyed,
                "the same-slot fixture retains one destroyed generation");
            const auto replacement = same_slot_registry->CreateEntity();
            Check(replacement && replacement->index == original->index &&
                      replacement->generation == original->generation + 1U,
                "the same sparse slot is reused at a new generation");
            if (replacement)
            {
                Check(store->Store(
                          *same_slot_registry, *replacement, Position{.x = 4, .y = 5}) ==
                              ComponentStoreWriteResult::Inserted &&
                          store->Snapshot(*same_slot_registry) == ComponentStoreSnapshot{
                                                                      .registry_slots = 1U,
                                                                      .value_capacity = 1U,
                                                                      .occupied = 1U,
                                                                      .accessible = 1U,
                                                                      .registry_compatible = true,
                                                                  },
                    "same-slot generation replacement is constant-time and preserves occupancy");
                const Position* const replacement_value =
                    store->Find(*same_slot_registry, *replacement);
                Check(replacement_value != nullptr && replacement_value->x == 4 &&
                          !store->EraseRetained(*original),
                    "same-slot replacement publishes only the new generation");
                Check(same_slot_registry->DestroyEntity(*replacement) ==
                              EntityDestroyResult::Destroyed &&
                          store->EraseRetained(*replacement) &&
                          store->Snapshot(*same_slot_registry).occupied == 0U,
                    "exact-generation cleanup remains available immediately after replacement destroy");
            }
        }
    }

    auto deterministic_left = EntityRegistry::Create(3U);
    auto deterministic_right = EntityRegistry::Create(3U);
    Check(deterministic_left && deterministic_right,
        "deterministic component-store registries construct");
    if (deterministic_left && deterministic_right)
    {
        auto left_store = PositionStore::Create(*deterministic_left, 2U);
        auto right_store = PositionStore::Create(*deterministic_right, 2U);
        const auto left_zero = deterministic_left->CreateEntity();
        const auto right_zero = deterministic_right->CreateEntity();
        const auto left_one = deterministic_left->CreateEntity();
        const auto right_one = deterministic_right->CreateEntity();
        Check(left_store && right_store && left_zero && right_zero && left_one && right_one,
            "matching component-store sequences can be executed");
        if (left_store && right_store && left_zero && right_zero && left_one && right_one)
        {
            const auto left_first = left_store->Store(
                *deterministic_left, *left_zero, Position{.x = 1, .y = 2});
            const auto right_first = right_store->Store(
                *deterministic_right, *right_zero, Position{.x = 1, .y = 2});
            const auto left_second = left_store->Store(
                *deterministic_left, *left_one, Position{.x = 3, .y = 4});
            const auto right_second = right_store->Store(
                *deterministic_right, *right_one, Position{.x = 3, .y = 4});
            const Position* const left_value =
                left_store->Find(*deterministic_left, *left_zero);
            const Position* const right_value =
                right_store->Find(*deterministic_right, *right_zero);
            Check(left_first == right_first && left_second == right_second &&
                      left_store->Snapshot(*deterministic_left) ==
                          right_store->Snapshot(*deterministic_right) &&
                      left_value != nullptr && right_value != nullptr &&
                      *left_value == *right_value,
                "identical registry and component calls produce identical observable state");
        }
    }

    auto move_registry = EntityRegistry::Create(2U);
    Check(move_registry.has_value(), "the component-store move registry constructs");
    if (move_registry)
    {
        auto source = PositionStore::Create(*move_registry, 2U);
        const auto retained = move_registry->CreateEntity();
        const auto discarded = move_registry->CreateEntity();
        Check(source && retained && discarded, "the component-store move fixture constructs");
        if (source && retained && discarded)
        {
            Check(source->Store(*move_registry, *retained, Position{.x = 5, .y = 6}) ==
                      ComponentStoreWriteResult::Inserted,
                "the move source owns a component");
            PositionStore moved = std::move(*source);
            Check(source->Snapshot(*move_registry) == ComponentStoreSnapshot{} &&
                      source->Store(*move_registry, *retained, Position{}) ==
                          ComponentStoreWriteResult::RegistryMismatch &&
                      source->Find(*move_registry, *retained) == nullptr &&
                      !source->Contains(*move_registry, *retained) &&
                      source->Erase(*move_registry, *retained) ==
                          ComponentStoreEraseResult::RegistryMismatch &&
                      !source->EraseRetained(*retained),
                "move construction leaves an inert logical zero-capacity source");
            source->Clear();
            const Position* const moved_value = moved.Find(*move_registry, *retained);
            Check(moved_value != nullptr && moved_value->x == 5,
                "move construction transfers the exact owned component");

            auto destination = PositionStore::Create(*move_registry, 2U);
            Check(destination.has_value(), "the move-assignment destination constructs");
            if (destination)
            {
                Check(destination->Store(
                          *move_registry, *discarded, Position{.x = 90, .y = 91}) ==
                          ComponentStoreWriteResult::Inserted,
                    "the old move-assignment destination owns distinct state");
                *destination = std::move(moved);
                Check(destination->Contains(*move_registry, *retained) &&
                          !destination->Contains(*move_registry, *discarded) &&
                          moved.Snapshot(*move_registry) == ComponentStoreSnapshot{},
                    "move assignment replaces ownership and leaves its source inert");

                const auto before_self_move = destination->Snapshot(*move_registry);
                PositionStore* const alias = &*destination;
                *destination = std::move(*alias);
                Check(destination->Snapshot(*move_registry) == before_self_move &&
                          destination->Contains(*move_registry, *retained),
                    "component-store self move-assignment is explicitly non-destructive");
            }
        }
    }

    Check(TrackedValue::alive == 0, "tracked component fixtures start balanced");
    auto lifecycle_registry = EntityRegistry::Create(1U);
    Check(lifecycle_registry.has_value(), "the component lifecycle registry constructs");
    if (lifecycle_registry)
    {
        const auto entity = lifecycle_registry->CreateEntity();
        Check(entity.has_value(), "the component lifecycle identity constructs");
        if (entity)
        {
            {
                auto tracked = ComponentStore<TrackedValue>::Create(*lifecycle_registry, 1U);
                Check(tracked.has_value(), "the tracked component store constructs");
                if (tracked)
                {
                    const auto inserted =
                        tracked->Store(*lifecycle_registry, *entity, TrackedValue{17});
                    Check(inserted == ComponentStoreWriteResult::Inserted &&
                              TrackedValue::alive == 1,
                        "insertion retains exactly one owned component value");
                    const auto replaced_result =
                        tracked->Store(*lifecycle_registry, *entity, TrackedValue{23});
                    Check(replaced_result == ComponentStoreWriteResult::Replaced &&
                              TrackedValue::alive == 1,
                        "replacement neither leaks nor duplicates component lifetime");
                    const TrackedValue* const replaced =
                        tracked->Find(*lifecycle_registry, *entity);
                    Check(replaced != nullptr && replaced->value == 23,
                        "replacement publishes the new component value");
                    auto moved = std::move(*tracked);
                    Check(TrackedValue::alive == 1 && moved.Contains(*lifecycle_registry, *entity),
                        "store movement transfers rather than duplicates component lifetime");
                    moved.Clear();
                    Check(TrackedValue::alive == 0,
                        "clear deterministically destroys every retained component value");
                    const auto reused =
                        moved.Store(*lifecycle_registry, *entity, TrackedValue{31});
                    Check(reused == ComponentStoreWriteResult::Inserted &&
                              TrackedValue::alive == 1,
                        "cleared startup storage remains reusable without lifetime imbalance");
                }
            }
            Check(TrackedValue::alive == 0,
                "component-store destruction releases every remaining owned value");
        }
    }

    return failures;
}
