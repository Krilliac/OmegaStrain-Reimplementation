#include "omega/simulation/simulation_world.h"
#include "omega/debug/subsystem_entry_break.h"

#include <exception>
#include <limits>
#include <utility>

namespace omega::simulation
{
namespace
{
[[nodiscard]] bool TryAddCoordinate(const std::int64_t value, const std::int64_t delta,
    std::int64_t& sum) noexcept
{
    if ((delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta))
    {
        return false;
    }

    sum = value + delta;
    return true;
}
} // namespace

std::expected<SimulationWorld, std::string> SimulationWorld::Create(
    const SimulationWorldConfig& config)
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_simulation");
    if (config.fixed_step <= std::chrono::nanoseconds::zero())
        return std::unexpected("simulation fixed step must be positive");
    auto entities = EntityRegistry::Create(config.maximum_entities);
    if (!entities)
        return std::unexpected("entity registry: " + entities.error());

    SimulationWorldConfig resolved_config = config;
    if (resolved_config.maximum_positioned_entities == 0U)
        resolved_config.maximum_positioned_entities = resolved_config.maximum_entities;

    auto positions = ComponentStore<Position3>::Create(
        *entities, resolved_config.maximum_positioned_entities);
    if (!positions)
        return std::unexpected("position store: " + positions.error());

    return SimulationWorld(
        resolved_config, std::move(*entities), std::move(*positions));
}

SimulationWorld::SimulationWorld(const SimulationWorldConfig& config, EntityRegistry entities,
    ComponentStore<Position3> positions) noexcept
    : config_(config), entities_(std::move(entities)), positions_(std::move(positions))
{
}

std::expected<EntityId, EntityCreateError> SimulationWorld::CreateEntity() noexcept
{
    return entities_.CreateEntity();
}

std::expected<EntityId, PositionedEntityCreateError> SimulationWorld::CreatePositionedEntity(
    Position3 initial_position) noexcept
{
    if (entities_.Snapshot().reusable == 0U)
        return std::unexpected(PositionedEntityCreateError::EntityCapacityExhausted);

    const ComponentStoreSnapshot positions = positions_.Snapshot(entities_);
    if (positions.occupied >= positions.value_capacity)
        return std::unexpected(PositionedEntityCreateError::PositionCapacityExhausted);

    const auto entity = entities_.CreateEntity();
    if (!entity)
        std::terminate();

    if (positions_.Store(entities_, *entity, std::move(initial_position)) !=
        ComponentStoreWriteResult::Inserted)
    {
        std::terminate();
    }
    return *entity;
}

EntityDestroyResult SimulationWorld::DestroyEntity(const EntityId entity) noexcept
{
    if (!entities_.IsAlive(entity))
        return EntityDestroyResult::NotAlive;

    const ComponentStoreEraseResult erased = positions_.Erase(entities_, entity);
    if (erased != ComponentStoreEraseResult::Erased &&
        erased != ComponentStoreEraseResult::NotPresent)
    {
        std::terminate();
    }

    const EntityDestroyResult destroyed = entities_.DestroyEntity(entity);
    if (destroyed != EntityDestroyResult::Destroyed)
        std::terminate();
    return destroyed;
}

bool SimulationWorld::IsAlive(const EntityId entity) const noexcept
{
    return entities_.IsAlive(entity);
}

EntityRegistrySnapshot SimulationWorld::EntitySnapshot() const noexcept
{
    return entities_.Snapshot();
}

std::optional<Position3> SimulationWorld::PositionOf(const EntityId entity) const noexcept
{
    const Position3* const position = positions_.Find(entities_, entity);
    if (position == nullptr)
        return std::nullopt;
    return *position;
}

PositionResetResult SimulationWorld::ResetPosition(
    const EntityId entity, const Position3 position) noexcept
{
    if (!entities_.IsAlive(entity))
        return PositionResetResult::EntityNotAlive;

    Position3* const existing = positions_.Find(entities_, entity);
    if (existing == nullptr)
        return PositionResetResult::PositionNotPresent;

    *existing = position;
    return PositionResetResult::Reset;
}

SimulationStepResult SimulationWorld::AdvanceOneStep() noexcept
{
    return AdvanceOneStep(SimulationStepInput{});
}

SimulationStepResult SimulationWorld::AdvanceOneStep(const SimulationStepInput& input) noexcept
{
    if (state_.completed_steps == std::numeric_limits<std::uint64_t>::max() ||
        state_.simulated_time > std::chrono::nanoseconds::max() - config_.fixed_step)
        return SimulationStepResult::RepresentationExhausted;

    Position3* position = nullptr;
    Position3 translated{};
    if (input.translation)
    {
        const EntityTranslation& translation = *input.translation;
        if (!entities_.IsAlive(translation.entity))
            return SimulationStepResult::EntityNotAlive;

        position = positions_.Find(entities_, translation.entity);
        if (position == nullptr)
            return SimulationStepResult::PositionNotPresent;

        if (!TryAddCoordinate(position->x, translation.delta.dx, translated.x) ||
            !TryAddCoordinate(position->y, translation.delta.dy, translated.y) ||
            !TryAddCoordinate(position->z, translation.delta.dz, translated.z))
        {
            return SimulationStepResult::PositionRepresentationExhausted;
        }
    }

    if (position != nullptr)
        *position = translated;
    ++state_.completed_steps;
    state_.simulated_time += config_.fixed_step;
    return SimulationStepResult::Advanced;
}

SimulationState SimulationWorld::Snapshot() const noexcept
{
    SimulationState snapshot = state_;
    snapshot.alive_entities = EntitySnapshot().alive;
    return snapshot;
}

const SimulationWorldConfig& SimulationWorld::config() const noexcept
{
    return config_;
}
} // namespace omega::simulation
