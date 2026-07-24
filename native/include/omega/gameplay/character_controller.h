#pragma once

#include "omega/asset/geometry_ir.h"

#include <cstdint>
#include <span>

namespace omega::gameplay
{
// A world-space collision triangle: three ordered vertices in the level's
// decoded coordinate space (the same Float3IR positions the level renderer and
// the COL SpatialMeshIR use). Winding is not relied upon; the closest-point
// query and unsigned penetration resolve are winding-independent.
struct CollisionTriangle
{
    asset::Float3IR a{};
    asset::Float3IR b{};
    asset::Float3IR c{};

    [[nodiscard]] bool operator==(const CollisionTriangle&) const = default;
};

// Kinematic character state: a sphere with position + velocity, plus whether it
// is resting on a walkable surface. This is a project-owned kinematic model
// reproducing the retail movement contract (kinematic controller vs the level
// collision mesh, per the .SO host-API which drives movement, not dynamics); it
// is not a rigid body.
struct CharacterState
{
    asset::Float3IR position{};
    asset::Float3IR velocity{};
    bool grounded = false;

    [[nodiscard]] bool operator==(const CharacterState&) const = default;
};

// Per-step control already resolved from input: a desired horizontal move
// direction/magnitude in world units, expressed in the plane perpendicular to
// `up`. The controller scales it by move_speed and re-projects it perpendicular
// to `up` before applying.
struct CharacterInput
{
    asset::Float3IR move{};

    [[nodiscard]] bool operator==(const CharacterInput&) const = default;
};

// Fixed kinematic parameters. `up` is the level's world up-axis (a unit vector;
// for MINSK the vertical axis is world +Z). Gravity accelerates along -up.
struct CharacterControllerParams
{
    float radius = 4.0F;
    asset::Float3IR up{.x = 0.0F, .y = 0.0F, .z = 1.0F};
    float gravity = 30.0F;
    float move_speed = 40.0F;
    // A contact surface is "walkable" (grounds the character, cancelling downward
    // velocity) when its resolve normal dotted with `up` is at least this. ~0.5
    // is a 60-degree walkable slope limit.
    float walkable_normal_dot = 0.5F;
    // Sphere-vs-mesh penetration resolve passes per step (multiple contacts).
    std::uint32_t resolve_iterations = 4U;

    [[nodiscard]] bool operator==(const CharacterControllerParams&) const = default;
};

// [any thread; reentrant] The closest point on triangle `t` to `p` (Ericson,
// Real-Time Collision Detection: Voronoi-region barycentric test). Handles
// vertex, edge, and face regions; degenerate triangles collapse to their
// closest vertex/edge. No allocation, no state.
[[nodiscard]] asset::Float3IR ClosestPointOnTriangle(
    const asset::Float3IR& p, const CollisionTriangle& t) noexcept;

// [any thread; reentrant] Advances the character by one fixed step of `dt`
// seconds: applies the horizontal input velocity (re-projected perpendicular to
// up) plus gravity along -up, integrates position, then resolves the sphere
// against `triangles` -- for each penetrating triangle it pushes the sphere out
// along the contact normal and removes the velocity component INTO that surface
// (so motion slides along walls/floors rather than sticking or tunnelling).
// Contact with an up-facing surface sets `grounded` and zeroes the along-up
// velocity. Brute-force over all triangles (v1; caller may pre-cull). Non-finite
// inputs are ignored per-component; the state passes through safely. Pure value
// math -- no allocation, no retained state.
[[nodiscard]] CharacterState StepCharacter(
    CharacterState state, const CharacterInput& input,
    const CharacterControllerParams& params,
    std::span<const CollisionTriangle> triangles, float dt) noexcept;
} // namespace omega::gameplay
