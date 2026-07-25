#include "omega/gameplay/objective_tracker.h"

#include <cstddef>

namespace omega::gameplay
{
ObjectiveState InitialObjectiveState(const MissionData& mission) noexcept
{
    ObjectiveState state;
    const std::size_t count = mission.objectives.size() < kMaxMissionObjectives
                                  ? mission.objectives.size()
                                  : kMaxMissionObjectives;
    for (std::size_t index = 0U; index < count; ++index)
        state.status[index] = ObjectiveStatus::Inactive;
    state.count = static_cast<std::uint8_t>(count);
    return state;
}

std::optional<std::size_t> FindObjectiveIndex(
    const MissionData& mission, const std::uint16_t id) noexcept
{
    const std::size_t count = mission.objectives.size() < kMaxMissionObjectives
                                  ? mission.objectives.size()
                                  : kMaxMissionObjectives;
    for (std::size_t index = 0U; index < count; ++index)
    {
        if (mission.objectives[index].id == id)
            return index;
    }
    return std::nullopt;
}

std::expected<ObjectiveStep, ObjectiveError> AdvanceObjectives(
    const MissionData& mission, ObjectiveState prior,
    const ObjectiveEvent event) noexcept
{
    const std::size_t mission_count =
        mission.objectives.size() < kMaxMissionObjectives
            ? mission.objectives.size()
            : kMaxMissionObjectives;
    if (prior.count != mission_count)
        return std::unexpected(ObjectiveError::StateMissionMismatch);

    const auto found = FindObjectiveIndex(mission, event.id);
    if (!found)
        return std::unexpected(ObjectiveError::UnknownObjective);
    const std::size_t index = *found;
    const ObjectiveDef& def = mission.objectives[index];
    const ObjectiveStatus current = prior.status[index];

    ObjectiveStep step;
    switch (event.choice)
    {
    case ObjectiveChoice::Check:
        step.state = prior;   // pure query: no mutation, no effects
        return step;
    case ObjectiveChoice::Add:
        if (current == ObjectiveStatus::Active ||
            current == ObjectiveStatus::Complete)
        {
            step.state = prior;   // idempotent: already given / done
            return step;
        }
        if (current != ObjectiveStatus::Inactive)
            return std::unexpected(ObjectiveError::InvalidTransition);
        prior.status[index] = ObjectiveStatus::Active;
        step.state = prior;
        step.hud_add = def.menu_key;
        step.map_add = def.map_key;
        step.voice_cue = def.voice_cue;
        return step;
    case ObjectiveChoice::Pass:
        if (current == ObjectiveStatus::Complete)
        {
            step.state = prior;   // idempotent
            return step;
        }
        if (current != ObjectiveStatus::Active)
            return std::unexpected(ObjectiveError::InvalidTransition);
        prior.status[index] = ObjectiveStatus::Complete;
        step.state = prior;
        step.hud_remove = def.menu_key;
        step.map_remove = def.map_key;
        step.voice_cue = def.voice_cue;
        return step;
    case ObjectiveChoice::Fail:
        if (current == ObjectiveStatus::Failed)
        {
            step.state = prior;   // idempotent
            return step;
        }
        if (current != ObjectiveStatus::Active)
            return std::unexpected(ObjectiveError::InvalidTransition);
        prior.status[index] = ObjectiveStatus::Failed;
        step.state = prior;
        step.hud_remove = def.menu_key;
        step.map_remove = def.map_key;
        return step;
    case ObjectiveChoice::Remove:
        if (current == ObjectiveStatus::Inactive)
        {
            step.state = prior;   // idempotent
            return step;
        }
        prior.status[index] = ObjectiveStatus::Inactive;
        step.state = prior;
        step.hud_remove = def.menu_key;
        step.map_remove = def.map_key;
        return step;
    }
    return std::unexpected(ObjectiveError::InvalidTransition);
}

bool IsObjectiveComplete(const MissionData& mission, const ObjectiveState& state,
    const std::uint16_t id) noexcept
{
    const auto found = FindObjectiveIndex(mission, id);
    if (!found)
        return false;
    return state.status[*found] == ObjectiveStatus::Complete;
}

bool AreObjectivesComplete(
    const MissionData& mission, const ObjectiveState& state) noexcept
{
    const std::size_t count = mission.objectives.size() < kMaxMissionObjectives
                                  ? mission.objectives.size()
                                  : kMaxMissionObjectives;
    if (state.count != count)
        return false;
    // Strict "mission complete" semantics: every primary objective must be
    // Complete. An Inactive (not-yet-given), Active, or Failed primary means the
    // mission is not done. A mission with no primaries is never complete.
    bool has_primary = false;
    for (std::size_t index = 0U; index < count; ++index)
    {
        if (mission.objectives[index].kind != ObjectiveKind::Primary)
            continue;
        has_primary = true;
        if (state.status[index] != ObjectiveStatus::Complete)
            return false;
    }
    return has_primary;
}

MissionOutcome EvaluateMissionOutcome(
    const MissionData& mission, const ObjectiveState& state) noexcept
{
    const std::size_t count = mission.objectives.size() < kMaxMissionObjectives
                                  ? mission.objectives.size()
                                  : kMaxMissionObjectives;
    // Fail-soft: a state that does not describe this mission decides nothing.
    if (state.count != count)
        return MissionOutcome::InProgress;

    bool has_primary = false;
    bool all_primaries_complete = true;
    for (std::size_t index = 0U; index < count; ++index)
    {
        // Secondary/optional objectives never decide the mission -- neither
        // their completion nor their failure is read here.
        if (mission.objectives[index].kind != ObjectiveKind::Primary)
            continue;
        has_primary = true;
        const ObjectiveStatus status = state.status[index];
        // Failure dominates: a single failed primary is a failed mission
        // regardless of what the other objectives did, or of slot order.
        if (status == ObjectiveStatus::Failed)
            return MissionOutcome::Failed;
        if (status != ObjectiveStatus::Complete)
            all_primaries_complete = false;   // still Inactive or Active
    }
    // No primary objectives (an empty or unloaded objective set) is in progress,
    // NOT a vacuous success: a level that failed to load must never read as won.
    if (!has_primary || !all_primaries_complete)
        return MissionOutcome::InProgress;
    return MissionOutcome::Succeeded;
}
} // namespace omega::gameplay
