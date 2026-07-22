#pragma once

#include "omega/simulation/component_store.h"
#include "omega/simulation/entity_registry.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>

namespace omega::simulation
{
// Signed synthetic project units. Their scale, axes, and handedness are host
// policy until retail behavior is established from evidence.
struct Position3
{
    std::int64_t x = 0;
    std::int64_t y = 0;
    std::int64_t z = 0;

    [[nodiscard]] friend constexpr bool operator==(const Position3&, const Position3&) = default;
};

struct Translation3
{
    std::int64_t dx = 0;
    std::int64_t dy = 0;
    std::int64_t dz = 0;

    [[nodiscard]] friend constexpr bool operator==(
        const Translation3&, const Translation3&) = default;
};

struct EntityTranslation
{
    EntityId entity{};
    Translation3 delta{};

    [[nodiscard]] friend constexpr bool operator==(
        const EntityTranslation&, const EntityTranslation&) = default;
};

struct SimulationStepInput
{
    // E0060 deliberately admits at most one project-owned translation command
    // per fixed step. Absence is the neutral input used by the legacy overload.
    std::optional<EntityTranslation> translation;

    [[nodiscard]] friend constexpr bool operator==(
        const SimulationStepInput&, const SimulationStepInput&) = default;
};

struct SimulationWorldConfig
{
    // Project-owned fixed step supplied by the composition root. It must be positive; its value is
    // a synthetic-shell choice until retail timing is established from evidence.
    std::chrono::nanoseconds fixed_step{0};
    // Project-owned bounded identity capacity. It is host infrastructure, not a retail limit.
    std::uint32_t maximum_entities = 65'536U;
    // Project-owned bounded position capacity. Zero resolves to maximum_entities
    // so existing aggregate configurations retain their source behavior.
    std::uint32_t maximum_positioned_entities = 0U;
};

struct SimulationState
{
    std::uint64_t completed_steps = 0;
    std::chrono::nanoseconds simulated_time{0};
    std::uint32_t alive_entities = 0;
};

enum class SimulationStepResult : std::uint8_t
{
    Advanced,
    RepresentationExhausted,
    EntityNotAlive,
    PositionNotPresent,
    PositionRepresentationExhausted,
};

enum class PositionedEntityCreateError : std::uint8_t
{
    EntityCapacityExhausted,
    PositionCapacityExhausted,
};

enum class PositionResetResult : std::uint8_t
{
    Reset,
    EntityNotAlive,
    PositionNotPresent,
};

// App-owned, non-hot-reloadable deterministic game-thread state boundary. This
// implementation owns the canonical simulation clock, entity identity storage,
// and the first justified project-owned component store. Entity lifecycle stays
// behind this boundary so cleanup remains deterministic without making the SDL
// host or frame scheduler depend on mutable gameplay storage.
class SimulationWorld final
{
public:
    // [any thread; reentrant] Validates a positive fixed step and returns an empty world.
    [[nodiscard]] static std::expected<SimulationWorld, std::string> Create(
        const SimulationWorldConfig& config);

    // [game thread, lifecycle] Transfers world ownership without allocation.
    // A moved-from world may only be destroyed.
    SimulationWorld(SimulationWorld&&) noexcept = default;
    SimulationWorld& operator=(SimulationWorld&&) noexcept = delete;
    SimulationWorld(const SimulationWorld&) = delete;
    SimulationWorld& operator=(const SimulationWorld&) = delete;

    // [game thread, lifecycle] Releases all world-owned state. Component stores
    // declared after the registry are destroyed before the registry itself.
    ~SimulationWorld() noexcept = default;

    // [game thread] Returns the next deterministic identity or
    // CapacityExhausted. All identity storage was allocated during Create().
    [[nodiscard]] std::expected<EntityId, EntityCreateError> CreateEntity() noexcept;

    // [game thread] Atomically creates an identity and initial position from
    // preallocated storage. Reported capacity failures leave identity reuse,
    // component storage, and clock state unchanged.
    [[nodiscard]] std::expected<EntityId, PositionedEntityCreateError> CreatePositionedEntity(
        Position3 initial_position) noexcept;

    // [game thread, lifecycle] This is the sole in-place world-owned entity
    // destruction path. An exact live generation is destroyed; every other
    // handle is inert.
    // Future direct component stores must erase this exact generation in
    // deterministic declaration order before the registry advances it.
    [[nodiscard]] EntityDestroyResult DestroyEntity(EntityId entity) noexcept;

    // [game thread] Tests an exact generation in this world's registry. EntityId
    // remains a plain world-scoped value, not a cross-world capability token.
    [[nodiscard]] bool IsAlive(EntityId entity) const noexcept;

    // [game thread] Returns aggregate identity state by value. No mutable
    // registry reference or storage view escapes SimulationWorld ownership.
    [[nodiscard]] EntityRegistrySnapshot EntitySnapshot() const noexcept;

    // [game thread] Returns an owned position copy only for an exact live
    // generation with a position. No component-store pointer escapes the world.
    [[nodiscard]] std::optional<Position3> PositionOf(EntityId entity) const noexcept;

    // [game thread] Replaces the position of one exact live positioned entity.
    // This operation never inserts a component and never changes clock,
    // identity, or entity-count state. Stale, dead, and unpositioned targets
    // are rejected atomically.
    [[nodiscard]] PositionResetResult ResetPosition(
        EntityId entity, Position3 position) noexcept;

    // [game thread] Advances exactly one fixed step. If either diagnostic representation is full,
    // returns RepresentationExhausted and leaves the complete state unchanged.
    [[nodiscard]] SimulationStepResult AdvanceOneStep() noexcept;

    // [game thread] Applies at most one translation and advances exactly one
    // fixed step as a single transaction. Result priority is clock
    // representation, entity liveness, position presence, then coordinate
    // representation; every failure leaves position and clock state unchanged.
    [[nodiscard]] SimulationStepResult AdvanceOneStep(const SimulationStepInput& input) noexcept;

    // [game thread] Returns an owned immutable value suitable for render/debug packet assembly.
    [[nodiscard]] SimulationState Snapshot() const noexcept;

    // [game thread; immutable after Create()]
    [[nodiscard]] const SimulationWorldConfig& config() const noexcept;

private:
    SimulationWorld(const SimulationWorldConfig& config, EntityRegistry entities,
        ComponentStore<Position3> positions) noexcept;

    SimulationWorldConfig config_{};
    EntityRegistry entities_;
    ComponentStore<Position3> positions_;
    SimulationState state_{};
};
} // namespace omega::simulation
