#include "omega/gameplay/mission_trigger.h"

#include <cmath>

namespace omega::gameplay
{
namespace
{
[[nodiscard]] bool WithinRadius(const asset::Float3IR& a,
    const asset::Float3IR& b, const float radius) noexcept
{
    if (!(radius > 0.0F) || !std::isfinite(radius))
        return false;
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    const float distance_squared = dx * dx + dy * dy + dz * dz;
    return std::isfinite(distance_squared) &&
           distance_squared <= radius * radius;
}
} // namespace

MissionTriggerResult StepMissionTriggers(const MissionData& mission,
    ObjectiveState state, const std::span<MissionTrigger> triggers,
    const asset::Float3IR& player_position) noexcept
{
    MissionTriggerResult result{.state = state, .changed = false};
    for (MissionTrigger& trigger : triggers)
    {
        if (trigger.fired)
            continue;
        if (!WithinRadius(player_position, trigger.position, trigger.radius))
            continue;
        // One-shot regardless of the transition outcome, so a no-op/rejected
        // transition does not re-evaluate every frame the player stands here.
        trigger.fired = true;
        const auto step = AdvanceObjectives(
            mission, result.state, {trigger.choice, trigger.objective_id});
        if (step && step->state != result.state)
        {
            result.state = step->state;
            result.changed = true;
        }
    }
    return result;
}
} // namespace omega::gameplay
