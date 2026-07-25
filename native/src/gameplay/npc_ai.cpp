#include "omega/gameplay/npc_ai.h"

#include <algorithm>
#include <cmath>

namespace omega::gameplay
{
namespace
{
[[nodiscard]] asset::Float3IR Sub(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return asset::Float3IR{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

[[nodiscard]] asset::Float3IR Add(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return asset::Float3IR{.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

[[nodiscard]] float Dot(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] asset::Float3IR Cross(
    const asset::Float3IR& a, const asset::Float3IR& b) noexcept
{
    return asset::Float3IR{
        .x = a.y * b.z - a.z * b.y,
        .y = a.z * b.x - a.x * b.z,
        .z = a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] bool Finite(const asset::Float3IR& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

bool SegmentIntersectsTriangle(const asset::Float3IR& a, const asset::Float3IR& b,
    const CollisionTriangle& t) noexcept
{
    if (!Finite(a) || !Finite(b) || !Finite(t.a) || !Finite(t.b) || !Finite(t.c))
        return false;

    constexpr float kEpsilon = 1e-7F;
    const asset::Float3IR dir = Sub(b, a);
    const asset::Float3IR edge1 = Sub(t.b, t.a);
    const asset::Float3IR edge2 = Sub(t.c, t.a);
    const asset::Float3IR pvec = Cross(dir, edge2);
    const float det = Dot(edge1, pvec);
    if (det > -kEpsilon && det < kEpsilon)
        return false; // segment parallel to the triangle plane

    const float inv_det = 1.0F / det;
    const asset::Float3IR tvec = Sub(a, t.a);
    const float u = Dot(tvec, pvec) * inv_det;
    if (u < 0.0F || u > 1.0F)
        return false;

    const asset::Float3IR qvec = Cross(tvec, edge1);
    const float v = Dot(dir, qvec) * inv_det;
    if (v < 0.0F || u + v > 1.0F)
        return false;

    const float s = Dot(edge2, qvec) * inv_det; // parametric position along a->b
    return s >= 0.0F && s <= 1.0F;
}

bool NpcSeesPlayer(const asset::Float3IR& npc_pos,
    const asset::Float3IR& npc_facing, const asset::Float3IR& player_pos,
    const NpcVisionParams& params,
    std::span<const CollisionTriangle> triangles) noexcept
{
    if (!Finite(npc_pos) || !Finite(npc_facing) || !Finite(player_pos))
        return false;
    if (!(params.range > 0.0F))
        return false;

    const float facing_len2 = Dot(npc_facing, npc_facing);
    if (!(facing_len2 > 0.0F))
        return false;

    // Range is measured centre-to-centre, unchanged by the eye offset below:
    // the eye is a sighting detail, not extra reach.
    const asset::Float3IR to_player = Sub(player_pos, npc_pos);
    const float dist2 = Dot(to_player, to_player);
    if (dist2 > params.range * params.range)
        return false; // out of range
    if (!(dist2 > 0.0F))
        return true; // coincident: trivially seen

    // One origin for BOTH remaining gates: the eye, `eye_height` along `up`
    // from the NPC's centre. The cone used to be judged from the centre while
    // the ray was traced from the eye, so the two disagreed by eye_height and
    // the function could accept an angle it then tested along a different line.
    const asset::Float3IR eye = Add(npc_pos,
        asset::Float3IR{.x = params.up.x * params.eye_height,
            .y = params.up.y * params.eye_height,
            .z = params.up.z * params.eye_height});
    if (!Finite(eye))
        return false; // non-finite up / eye_height: no usable sighting origin

    // ...and one target: the player's centre, which is what the kinematic
    // controller tracks. The ray used to aim at player_pos + up*eye_height,
    // which is ABOVE the player (the controller's sphere is radius-tall, so its
    // top sits well below centre + eye_height), so overhead geometry could
    // block a sight line that an eye-to-torso look clears. PROJECT decision:
    // retail's sensing model is undecoded, so centre-mass is chosen because it
    // is the one point on the player this code already has.
    const asset::Float3IR to_aim = Sub(player_pos, eye);
    const float aim_dist2 = Dot(to_aim, to_aim);
    if (!(aim_dist2 > 0.0F))
        return true; // the player's centre sits exactly at the eye: seen

    // cos(angle) between the facing and the eye->player-centre direction.
    const float cos_angle = Dot(to_aim, npc_facing) /
        (std::sqrt(aim_dist2) * std::sqrt(facing_len2));
    if (cos_angle < params.cos_half_angle)
        return false; // outside the vision cone

    // Line of sight along the very segment the cone just accepted. Blocked if
    // any triangle intersects it.
    return std::ranges::none_of(triangles, [&](const CollisionTriangle& triangle) {
        return SegmentIntersectsTriangle(eye, player_pos, triangle); // occluded
    });
}

NpcAwareness StepNpcAwareness(const NpcAwareness prev, const bool sees_player) noexcept
{
    if (prev == NpcAwareness::Alerted)
        return NpcAwareness::Alerted; // latch
    return sees_player ? NpcAwareness::Alerted : NpcAwareness::Patrol;
}

NpcAwarenessState StepNpcAwarenessLoop(NpcAwarenessState prev,
    const bool sees_player, const asset::Float3IR& player_pos,
    const NpcAwarenessParams& params, const float dt) noexcept
{
    NpcAwarenessState next = prev;
    const float step = std::isfinite(dt) && dt > 0.0F ? dt : 0.0F;
    if (sees_player && Finite(player_pos))
    {
        next.last_seen_player_pos = player_pos;
        next.has_last_seen = true;
    }

    switch (prev.state)
    {
    case NpcState::Patrol:
        if (sees_player)
        {
            next.state = NpcState::Alerted;
            next.timer = 0.0F;
        }
        break;
    case NpcState::Alerted:
        if (!sees_player)
        {
            // Lost the target during the reaction: pursue its last-seen spot.
            next.state = NpcState::Chasing;
            next.timer = 0.0F;
        }
        else
        {
            next.timer = prev.timer + step;
            if (next.timer >= params.alerted_seconds)
            {
                next.state = NpcState::Chasing;
                next.timer = 0.0F;
            }
        }
        break;
    case NpcState::Chasing:
        if (sees_player)
        {
            next.timer = 0.0F; // sight fresh; keep chasing the live position
        }
        else
        {
            next.timer = prev.timer + step;
            if (next.timer >= params.lose_sight_seconds)
            {
                next.state = NpcState::Searching;
                next.timer = 0.0F;
            }
        }
        break;
    case NpcState::Searching:
        if (sees_player)
        {
            next.state = NpcState::Chasing;
            next.timer = 0.0F;
        }
        else
        {
            next.timer = prev.timer + step;
            if (next.timer >= params.search_seconds)
            {
                next.state = NpcState::Patrol;
                next.timer = 0.0F;
            }
        }
        break;
    }
    return next;
}

bool NpcPursuing(const NpcState state) noexcept
{
    return state == NpcState::Chasing || state == NpcState::Searching;
}

bool NpcHoldsPositionToAim(const NpcState state, const bool sees_player) noexcept
{
    if (!sees_player)
        return false; // no target in sight: keep patrolling / re-acquiring
    return state == NpcState::Alerted || state == NpcState::Chasing;
}

NpcPatrolPlan PlanNpcPursuit(const asset::Float3IR& pos,
    const asset::Float3IR& target, const float arrive_radius,
    const asset::Float3IR& default_facing) noexcept
{
    NpcPatrolPlan plan{.facing = default_facing, .waypoint = 0U};
    if (!Finite(pos) || !Finite(target))
        return plan;
    const asset::Float3IR delta{
        .x = target.x - pos.x, .y = target.y - pos.y, .z = 0.0F};
    const float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    constexpr float kEpsilon = 1e-4F;
    if (len <= arrive_radius || len <= kEpsilon)
        return plan;
    const asset::Float3IR unit{.x = delta.x / len, .y = delta.y / len, .z = 0.0F};
    plan.move = unit;
    plan.facing = unit;
    return plan;
}

NpcPatrolPlan PlanNpcPatrol(const asset::Float3IR& pos,
    std::span<const asset::Float3IR> waypoints, std::uint32_t current,
    const float arrive_radius, const asset::Float3IR& default_facing) noexcept
{
    NpcPatrolPlan plan{.facing = default_facing, .waypoint = current};
    if (waypoints.empty() || !Finite(pos))
    {
        plan.waypoint = 0U;
        return plan;
    }
    if (current >= waypoints.size())
        current = 0U;

    // Horizontal distance to the current target (ignore the up/z component so a
    // z-climb along the path doesn't stall arrival). up is +Z here.
    const auto horizontal_delta = [](const asset::Float3IR& target,
                                      const asset::Float3IR& from) {
        return asset::Float3IR{
            .x = target.x - from.x, .y = target.y - from.y, .z = 0.0F};
    };
    asset::Float3IR delta = horizontal_delta(waypoints[current], pos);
    float len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (len <= arrive_radius)
    {
        current = static_cast<std::uint32_t>((current + 1U) % waypoints.size());
        delta = horizontal_delta(waypoints[current], pos);
        len = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    }
    plan.waypoint = current;
    constexpr float kEpsilon = 1e-4F;
    if (len > kEpsilon)
    {
        const asset::Float3IR unit{
            .x = delta.x / len, .y = delta.y / len, .z = 0.0F};
        plan.move = unit;
        plan.facing = unit;
    }
    return plan;
}
} // namespace omega::gameplay
