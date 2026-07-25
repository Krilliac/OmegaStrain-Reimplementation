#include "omega/gameplay/combat.h"

#include "omega/gameplay/npc_ai.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace omega::gameplay
{
namespace
{
[[nodiscard]] asset::Float3IR Sub(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return asset::Float3IR{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

[[nodiscard]] float Dot(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] bool Finite(const asset::Float3IR& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

WeaponStep StepNpcWeapon(const WeaponState prev, const bool engaging,
    const float dt, const WeaponParams& params) noexcept
{
    WeaponStep step{.state = prev, .fired = false};
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

WeaponStep StepPlayerWeapon(const WeaponState prev, const bool trigger_held,
    const float dt, const WeaponParams& params) noexcept
{
    WeaponStep step{.state = prev, .fired = false};
    if (!std::isfinite(dt) || dt <= 0.0F)
        return step;

    // No aim ramp on the player side: `aim_seconds` is carried through untouched
    // (it is zero for a player-owned state) and only the cooldown ticks down.
    step.state.cooldown_seconds = std::max(0.0F, prev.cooldown_seconds - dt);

    if (trigger_held && step.state.cooldown_seconds <= 0.0F)
    {
        step.fired = true;
        step.state.cooldown_seconds = std::max(0.0F, params.cooldown_seconds);
    }
    return step;
}

HitscanResult ResolveHitscan(const asset::Float3IR& origin,
    const asset::Float3IR& direction,
    const std::span<const asset::Float3IR> target_positions,
    const std::span<const bool> alive, const float target_radius,
    const float max_range,
    const std::span<const CollisionTriangle> occluders) noexcept
{
    HitscanResult result{};
    if (!Finite(origin) || !Finite(direction))
        return result;
    if (!std::isfinite(max_range) || max_range <= 0.0F)
        return result;
    if (!std::isfinite(target_radius) || target_radius < 0.0F)
        return result;

    const float dir_len2 = Dot(direction, direction);
    if (!std::isfinite(dir_len2) || !(dir_len2 > 0.0F))
        return result; // zero-length / degenerate aim: nothing to trace

    // Unit ray, so `distance` is a world distance and directly comparable to
    // `max_range` regardless of how the caller scaled `direction`.
    const float inv_len = 1.0F / std::sqrt(dir_len2);
    const asset::Float3IR ray{.x = direction.x * inv_len,
        .y = direction.y * inv_len,
        .z = direction.z * inv_len};

    const float radius2 = target_radius * target_radius;
    float best = max_range;
    for (std::size_t i = 0; i < target_positions.size(); ++i)
    {
        // A short `alive` span is fail-soft: entries it does not cover are alive.
        if (i < alive.size() && !alive[i])
            continue;
        const asset::Float3IR& centre = target_positions[i];
        if (!Finite(centre))
            continue;

        // Ray/sphere: project the centre onto the ray, then the half-chord.
        const asset::Float3IR to_centre = Sub(centre, origin);
        const float along = Dot(to_centre, ray);
        // The centre must lie ahead of the origin. Without this a target whose
        // sphere merely CONTAINS the origin is hit no matter which way the shot
        // was aimed: the entry point is behind, so the exit point below is taken
        // instead, and the exit of a sphere centred behind you is still in front
        // of you. With a hit radius comparable to the actor spacing that let a
        // shot kill an enemy standing at the shooter's back.
        if (!(along > 0.0F))
            continue;
        const float perp2 = Dot(to_centre, to_centre) - along * along;
        if (perp2 > radius2)
            continue; // the ray passes outside the sphere
        const float half_chord = std::sqrt(std::max(0.0F, radius2 - perp2));

        // Nearest entry point strictly in front of the origin; if the origin is
        // inside the sphere the entry is behind it, so take the exit instead.
        float distance = along - half_chord;
        if (distance <= 0.0F)
            distance = along + half_chord;
        if (!(distance > 0.0F) || !(distance <= max_range))
            continue;
        // Strictly nearer wins, so an exact tie keeps the lower index.
        if (result.hit && !(distance < best))
            continue;

        // Occlusion is tested per candidate, against that candidate's own impact
        // point: a target behind a wall is skipped, not turned into a global miss.
        const asset::Float3IR impact{.x = origin.x + ray.x * distance,
            .y = origin.y + ray.y * distance,
            .z = origin.z + ray.z * distance};
        const bool blocked =
            std::ranges::any_of(occluders, [&](const CollisionTriangle& t) {
                return SegmentIntersectsTriangle(origin, impact, t);
            });
        if (blocked)
            continue;

        best = distance;
        result.hit = true;
        result.target = static_cast<std::uint32_t>(i);
        result.distance = distance;
    }
    return result;
}

float ApplyDamage(const float hitpoints, const float amount) noexcept
{
    if (!std::isfinite(hitpoints) || !std::isfinite(amount))
        return hitpoints;
    return std::max(0.0F, hitpoints - amount);
}

HealthState ApplyDamageToHealth(const HealthState prev, const float amount) noexcept
{
    if (!prev.alive)
        return prev; // death latches; respawn is a fresh HealthState
    if (!std::isfinite(amount) || !std::isfinite(prev.hitpoints))
        return prev;

    HealthState next{
        .hitpoints = ApplyDamage(prev.hitpoints, amount), .alive = true};
    next.alive = next.hitpoints > 0.0F;
    return next;
}
} // namespace omega::gameplay
