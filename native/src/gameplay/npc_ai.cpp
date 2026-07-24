#include "omega/gameplay/npc_ai.h"

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

    const asset::Float3IR to_player = Sub(player_pos, npc_pos);
    const float dist2 = Dot(to_player, to_player);
    if (dist2 > params.range * params.range)
        return false; // out of range
    if (!(dist2 > 0.0F))
        return true; // coincident: trivially seen

    const float dist = std::sqrt(dist2);
    // cos(angle) between the facing and the direction to the player.
    const float cos_angle =
        Dot(to_player, npc_facing) / (dist * std::sqrt(facing_len2));
    if (cos_angle < params.cos_half_angle)
        return false; // outside the vision cone

    // Line of sight: eye (raised along up) -> player. Blocked if any triangle
    // intersects the segment.
    const asset::Float3IR eye = Add(npc_pos,
        asset::Float3IR{.x = params.up.x * params.eye_height,
            .y = params.up.y * params.eye_height,
            .z = params.up.z * params.eye_height});
    const asset::Float3IR target = Add(player_pos,
        asset::Float3IR{.x = params.up.x * params.eye_height,
            .y = params.up.y * params.eye_height,
            .z = params.up.z * params.eye_height});
    for (const CollisionTriangle& triangle : triangles)
    {
        if (SegmentIntersectsTriangle(eye, target, triangle))
            return false; // occluded
    }
    return true;
}

NpcAwareness StepNpcAwareness(const NpcAwareness prev, const bool sees_player) noexcept
{
    if (prev == NpcAwareness::Alerted)
        return NpcAwareness::Alerted; // latch
    return sees_player ? NpcAwareness::Alerted : NpcAwareness::Patrol;
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
