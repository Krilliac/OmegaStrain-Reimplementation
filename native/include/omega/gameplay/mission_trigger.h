#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/gameplay/mission_data.h"
#include "omega/gameplay/objective_tracker.h"

#include <cstdint>
#include <span>

namespace omega::gameplay
{
// A world-space objective-trigger volume: when the player enters the sphere,
// `choice` is fired once on objective `objective_id`. These are PROJECT-PLACED
// volumes over the real .SO-extracted objective ids -- the retail beacon /
// checkpoint WORLD coordinates are not recovered (the .SO gives the objective
// structure, not positions), so the placement is project-owned, not retail.
struct MissionTrigger
{
    std::uint16_t objective_id = 0U;
    asset::Float3IR position{};
    float radius = 0.0F;
    ObjectiveChoice choice = ObjectiveChoice::Pass;
    bool fired = false;

    [[nodiscard]] bool operator==(const MissionTrigger&) const = default;
};

struct MissionTriggerResult
{
    ObjectiveState state{};
    bool changed = false;

    [[nodiscard]] bool operator==(const MissionTriggerResult&) const = default;
};

// [any thread; reentrant] Tests `player_position` against each un-fired trigger;
// on entry (squared distance <= radius^2, radius finite and positive) it marks
// the trigger fired (one-shot -- so it never re-triggers, even if the transition
// is a no-op) and advances the linked objective via AdvanceObjectives, chaining
// across multiple triggers hit the same step. Returns the (possibly) updated
// state and whether it changed (the caller rebuilds the HUD only when changed).
// Fail-soft: a rejected/no-op transition still marks the trigger fired but leaves
// the state unchanged. The `triggers` span is mutated in place (fired flags);
// no allocation, deterministic.
[[nodiscard]] MissionTriggerResult StepMissionTriggers(
    const MissionData& mission, ObjectiveState state,
    std::span<MissionTrigger> triggers,
    const asset::Float3IR& player_position) noexcept;
} // namespace omega::gameplay
