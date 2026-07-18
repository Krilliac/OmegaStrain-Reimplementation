#pragma once

#include "omega/simulation/entity_registry.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

namespace omega::simulation
{
struct SimulationWorldConfig
{
    // Project-owned fixed step supplied by the composition root. It must be positive; its value is
    // a synthetic-shell choice until retail timing is established from evidence.
    std::chrono::nanoseconds fixed_step{0};
    // Project-owned bounded identity capacity. It is host infrastructure, not a retail limit.
    std::uint32_t maximum_entities = 65'536U;
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
};

// App-owned, non-hot-reloadable deterministic game-thread state boundary. This
// first implementation owns only the canonical simulation clock and entity
// identity storage. Entity lifecycle remains behind this boundary so future
// direct component stores can be cleaned in deterministic declaration order
// without making the SDL host or frame scheduler depend on gameplay state.
class SimulationWorld final
{
public:
    // [any thread; reentrant] Validates a positive fixed step and returns an empty world.
    [[nodiscard]] static std::expected<SimulationWorld, std::string> Create(
        const SimulationWorldConfig& config);

    // [game thread, lifecycle] Transfers world ownership without allocation.
    // A moved-from world may only be destroyed or move-assigned.
    SimulationWorld(SimulationWorld&&) noexcept = default;
    SimulationWorld& operator=(SimulationWorld&&) noexcept = default;
    SimulationWorld(const SimulationWorld&) = delete;
    SimulationWorld& operator=(const SimulationWorld&) = delete;

    // [game thread] Returns the next deterministic identity or
    // CapacityExhausted. All identity storage was allocated during Create().
    [[nodiscard]] std::expected<EntityId, EntityCreateError> CreateEntity() noexcept;

    // [game thread, lifecycle] This is the sole world-owned entity destruction
    // path. An exact live generation is destroyed; every other handle is inert.
    // Future direct component stores must erase this exact generation in
    // deterministic declaration order before the registry advances it.
    [[nodiscard]] EntityDestroyResult DestroyEntity(EntityId entity) noexcept;

    // [game thread] Tests an exact generation in this world's registry. EntityId
    // remains a plain world-scoped value, not a cross-world capability token.
    [[nodiscard]] bool IsAlive(EntityId entity) const noexcept;

    // [game thread] Returns aggregate identity state by value. No mutable
    // registry reference or storage view escapes SimulationWorld ownership.
    [[nodiscard]] EntityRegistrySnapshot EntitySnapshot() const noexcept;

    // [game thread] Advances exactly one fixed step. If either diagnostic representation is full,
    // returns RepresentationExhausted and leaves the complete state unchanged.
    [[nodiscard]] SimulationStepResult AdvanceOneStep() noexcept;

    // [game thread] Returns an owned immutable value suitable for render/debug packet assembly.
    [[nodiscard]] SimulationState Snapshot() const noexcept;

    // [game thread; immutable after Create()]
    [[nodiscard]] const SimulationWorldConfig& config() const noexcept;

private:
    SimulationWorld(const SimulationWorldConfig& config, EntityRegistry entities) noexcept;

    SimulationWorldConfig config_{};
    EntityRegistry entities_;
    SimulationState state_{};
};
} // namespace omega::simulation
