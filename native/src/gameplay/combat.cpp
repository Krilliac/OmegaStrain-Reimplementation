#include "omega/gameplay/combat.h"

#include <algorithm>
#include <cmath>

namespace omega::gameplay
{
NpcWeaponStep StepNpcWeapon(const NpcWeaponState prev, const bool engaging,
    const float dt, const WeaponParams& params) noexcept
{
    NpcWeaponStep step{.state = prev, .fired = false};
    if (!std::isfinite(dt) || dt <= 0.0F)
        return step;

    step.state.cooldown_seconds = std::max(0.0F, prev.cooldown_seconds - dt);

    if (!engaging)
    {
        // Lost the target / not pursuing: drop the aim, keep counting down cooldown.
        step.state.aim_seconds = 0.0F;
        return step;
    }

    step.state.aim_seconds =
        std::min(params.aim_up_seconds, prev.aim_seconds + dt);
    if (step.state.aim_seconds >= params.aim_up_seconds &&
        step.state.cooldown_seconds <= 0.0F)
    {
        step.fired = true;
        step.state.cooldown_seconds = std::max(0.0F, params.cooldown_seconds);
    }
    return step;
}

float ApplyDamage(const float hitpoints, const float amount) noexcept
{
    if (!std::isfinite(hitpoints) || !std::isfinite(amount))
        return hitpoints;
    return std::max(0.0F, hitpoints - amount);
}
} // namespace omega::gameplay
