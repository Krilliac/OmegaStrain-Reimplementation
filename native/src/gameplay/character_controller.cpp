#include "omega/gameplay/character_controller.h"

#include <algorithm>
#include <cmath>

namespace omega::gameplay
{
namespace
{
using asset::Float3IR;

[[nodiscard]] Float3IR Add(const Float3IR& a, const Float3IR& b) noexcept
{
    return Float3IR{.x = a.x + b.x, .y = a.y + b.y, .z = a.z + b.z};
}

[[nodiscard]] Float3IR Sub(const Float3IR& a, const Float3IR& b) noexcept
{
    return Float3IR{.x = a.x - b.x, .y = a.y - b.y, .z = a.z - b.z};
}

[[nodiscard]] Float3IR Scale(const Float3IR& a, const float s) noexcept
{
    return Float3IR{.x = a.x * s, .y = a.y * s, .z = a.z * s};
}

[[nodiscard]] float Dot(const Float3IR& a, const Float3IR& b) noexcept
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Float3IR Cross(const Float3IR& a, const Float3IR& b) noexcept
{
    return Float3IR{
        .x = a.y * b.z - a.z * b.y,
        .y = a.z * b.x - a.x * b.z,
        .z = a.x * b.y - a.y * b.x,
    };
}

[[nodiscard]] float Length(const Float3IR& a) noexcept
{
    return std::sqrt(Dot(a, a));
}

[[nodiscard]] Float3IR Normalized(const Float3IR& a, const Float3IR& fallback) noexcept
{
    const float length = Length(a);
    if (!(length > 1.0e-8F))
        return fallback;
    return Scale(a, 1.0F / length);
}

[[nodiscard]] bool IsFinite(const Float3IR& a) noexcept
{
    return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z);
}

// Contact band in world units. A sphere whose surface is within this of a
// triangle is touching it, not penetrating it: the resolve records the contact
// (grounding, sliding) but moves the character nowhere. Without the band every
// settled character counts as "still colliding" and the resolve loop spends its
// whole iteration budget on it on every step. This is a project value chosen to
// sit far below the per-step sink of gravity (radius-relative, ~1e-4 of a body);
// there is no retail figure for it.
constexpr float kContactSkin = 1.0e-4F;
} // namespace

Float3IR ClosestPointOnTriangle(
    const Float3IR& p, const CollisionTriangle& t) noexcept
{
    // Ericson, Real-Time Collision Detection, section 5.1.5. Barycentric
    // Voronoi-region classification: vertex, edge, then interior face.
    const Float3IR ab = Sub(t.b, t.a);
    const Float3IR ac = Sub(t.c, t.a);
    const Float3IR ap = Sub(p, t.a);
    const float d1 = Dot(ab, ap);
    const float d2 = Dot(ac, ap);
    if (d1 <= 0.0F && d2 <= 0.0F)
        return t.a; // vertex region A

    const Float3IR bp = Sub(p, t.b);
    const float d3 = Dot(ab, bp);
    const float d4 = Dot(ac, bp);
    if (d3 >= 0.0F && d4 <= d3)
        return t.b; // vertex region B

    const float vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0F && d1 >= 0.0F && d3 <= 0.0F)
    {
        const float denom = d1 - d3;
        const float v = denom != 0.0F ? d1 / denom : 0.0F;
        return Add(t.a, Scale(ab, v)); // edge AB
    }

    const Float3IR cp = Sub(p, t.c);
    const float d5 = Dot(ab, cp);
    const float d6 = Dot(ac, cp);
    if (d6 >= 0.0F && d5 <= d6)
        return t.c; // vertex region C

    const float vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0F && d2 >= 0.0F && d6 <= 0.0F)
    {
        const float denom = d2 - d6;
        const float w = denom != 0.0F ? d2 / denom : 0.0F;
        return Add(t.a, Scale(ac, w)); // edge AC
    }

    const float va = d3 * d6 - d5 * d4;
    if (va <= 0.0F && (d4 - d3) >= 0.0F && (d5 - d6) >= 0.0F)
    {
        const float denom = (d4 - d3) + (d5 - d6);
        const float w = denom != 0.0F ? (d4 - d3) / denom : 0.0F;
        return Add(t.b, Scale(Sub(t.c, t.b), w)); // edge BC
    }

    // Interior face region: barycentric (u, v, w) via the triangle normal.
    const float denom = va + vb + vc;
    if (!(std::fabs(denom) > 1.0e-12F))
        return t.a; // fully degenerate
    const float v = vb / denom;
    const float w = vc / denom;
    return Add(t.a, Add(Scale(ab, v), Scale(ac, w)));
}

CharacterState StepCharacter(
    CharacterState state, const CharacterInput& input,
    const CharacterControllerParams& params,
    const std::span<const CollisionTriangle> triangles, const float dt) noexcept
{
    if (!IsFinite(state.position) || !IsFinite(state.velocity) ||
        !std::isfinite(dt))
        return state;

    const float radius = params.radius > 0.0F ? params.radius : 1.0F;
    const Float3IR up =
        Normalized(params.up, Float3IR{.x = 0.0F, .y = 0.0F, .z = 1.0F});

    // Horizontal input velocity: the component of the requested move
    // perpendicular to up, renormalized to move_speed.
    Float3IR horizontal{};
    if (IsFinite(input.move))
    {
        const Float3IR perpendicular =
            Sub(input.move, Scale(up, Dot(input.move, up)));
        if (Length(perpendicular) > 1.0e-6F)
            horizontal = Scale(Normalized(perpendicular, Float3IR{}),
                params.move_speed);
    }

    // Velocity = the requested horizontal + the retained along-up (gravity)
    // component, then apply gravity for this step.
    const float along_up = Dot(state.velocity, up);
    Float3IR velocity = Add(horizontal, Scale(up, along_up));
    velocity = Sub(velocity, Scale(up, params.gravity * dt));

    Float3IR position = Add(state.position, Scale(velocity, dt));
    // The along-up velocity entering the resolve, kept for the no-launch clamp
    // after the loop.
    const float entry_along_up = Dot(velocity, up);

    // Resolve the sphere against the mesh: push out of penetration along the
    // contact normal and remove the into-surface velocity (slide).
    bool grounded = false;
    const std::uint32_t iterations =
        params.resolve_iterations > 0U ? params.resolve_iterations : 1U;
    for (std::uint32_t iteration = 0U; iteration < iterations; ++iteration)
    {
        bool any_penetration = false;
        for (const CollisionTriangle& triangle : triangles)
        {
            const Float3IR closest = ClosestPointOnTriangle(position, triangle);
            const Float3IR offset = Sub(position, closest);
            const float distance = Length(offset);
            if (distance >= radius)
                continue;

            Float3IR face_normal = Normalized(
                Cross(Sub(triangle.b, triangle.a),
                    Sub(triangle.c, triangle.a)),
                up);
            const Float3IR normal =
                distance > 1.0e-6F ? Scale(offset, 1.0F / distance) : face_normal;

            const bool walkable = Dot(normal, up) >= params.walkable_normal_dot;
            if (walkable)
                grounded = true;
            const float into = Dot(velocity, normal);
            if (into < 0.0F)
                velocity = Sub(velocity, Scale(normal, into));

            // Touching, not penetrating: grounded and sliding already recorded
            // above; move nothing, and do not keep the iteration loop alive for
            // it.
            const float depth = radius - distance;
            if (depth <= kContactSkin)
                continue;

            Float3IR push = Scale(normal, depth);
            if (!walkable)
            {
                // A face too steep to stand on is a blocker, not a lift: it may
                // push the character sideways out of itself, never upward. That
                // is what keeps a stationary character in a tight spot still.
                // When several near-vertical faces overlap the sphere at once
                // their constraints cannot all be satisfied, so no iteration
                // count ever clears them and the fixed-order push cycle repeats
                // in full on every step; each cycle nets the small upward
                // component those faces share, so the character is extruded
                // upward a little every step, without bound, while its
                // horizontal position only cycles. Requiring a walkable normal
                // before any lift removes that upward bias -- the residual
                // cycling is horizontal, and the floor pins the height.
                const float lift = Dot(push, up);
                if (lift > 0.0F)
                    push = Sub(push, Scale(up, lift));
            }
            position = Add(position, push);
            any_penetration = true;
        }
        if (!any_penetration)
            break;
    }

    // On the ground, clamp any residual downward drift so the character rests.
    if (grounded)
    {
        const float residual = Dot(velocity, up);
        if (residual < 0.0F)
            velocity = Sub(velocity, Scale(up, residual));
    }
    // Contact removes velocity into a surface; it must never reverse it into
    // velocity out of one. Chaining the per-contact removals above can rotate a
    // purely downward velocity into an upward one (two contacts whose normals
    // are not parallel are enough), and the grounded clamp only cancels a
    // DOWNWARD residual, so that gained upward velocity survives into the next
    // step through `along_up` and contact keeps feeding a resting character.
    const float exit_along_up = Dot(velocity, up);
    const float allowed_along_up = entry_along_up > 0.0F ? entry_along_up : 0.0F;
    if (exit_along_up > allowed_along_up)
        velocity = Sub(velocity, Scale(up, exit_along_up - allowed_along_up));

    return CharacterState{
        .position = position, .velocity = velocity, .grounded = grounded};
}

std::vector<CollisionTriangle> BuildLevelCollisionTriangles(
    const asset::LevelSpatialIR& spatial)
{
    std::vector<CollisionTriangle> triangles;
    for (const asset::SpatialMeshIR& cell : spatial.terrain_cells)
    {
        const std::size_t vertex_count = cell.vertices.size();
        for (const asset::SpatialTriangleIR& triangle : cell.triangles)
        {
            const std::uint32_t i0 = triangle.vertex_indices[0];
            const std::uint32_t i1 = triangle.vertex_indices[1];
            const std::uint32_t i2 = triangle.vertex_indices[2];
            if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count)
                continue; // fail-soft: skip a triangle with an out-of-range index
            triangles.push_back(CollisionTriangle{
                .a = cell.vertices[i0],
                .b = cell.vertices[i1],
                .c = cell.vertices[i2],
            });
        }
    }
    return triangles;
}

void SelectNearbyCollisionTriangles(
    const std::span<const CollisionTriangle> triangles, const Float3IR& center,
    const float radius, std::vector<CollisionTriangle>& out)
{
    out.clear();
    if (!(radius > 0.0F) || !std::isfinite(radius))
        return;
    for (const CollisionTriangle& t : triangles)
    {
        const float min_x = std::min({t.a.x, t.b.x, t.c.x}) - radius;
        const float max_x = std::max({t.a.x, t.b.x, t.c.x}) + radius;
        const float min_y = std::min({t.a.y, t.b.y, t.c.y}) - radius;
        const float max_y = std::max({t.a.y, t.b.y, t.c.y}) + radius;
        const float min_z = std::min({t.a.z, t.b.z, t.c.z}) - radius;
        const float max_z = std::max({t.a.z, t.b.z, t.c.z}) + radius;
        if (center.x >= min_x && center.x <= max_x && center.y >= min_y &&
            center.y <= max_y && center.z >= min_z && center.z <= max_z)
        {
            out.push_back(t);
        }
    }
}
} // namespace omega::gameplay
