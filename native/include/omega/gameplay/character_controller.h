#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/asset/level_spatial_ir.h"

#include <cstdint>
#include <span>
#include <vector>

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
    // is a 60-degree walkable slope limit. It also gates lifting: only a walkable
    // contact may raise the character. A steeper face is a blocker -- it pushes
    // the character out of itself horizontally and never upward.
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
//
// The resolve is deliberately one-directional about height, so that a character
// standing still stays still: (1) a contact shallower than the internal contact
// band is treated as touching -- it grounds and slides but moves nothing, and
// does not keep the iteration loop running; (2) only a walkable contact (see
// `walkable_normal_dot`) may raise the character, so faces too steep to stand on
// push it sideways out of themselves and never up; (3) contact may cancel motion
// into a surface but never reverse it, so the resolve cannot hand the character
// more upward velocity than it arrived with. Together these make the resting
// position non-accumulating even where the contact set is over-constrained (a
// sphere overlapping several faces that cannot all be satisfied at once, which
// the iteration budget can never clear).
//
// What this does NOT model: it is not a solver -- an over-constrained contact
// set is not resolved, only prevented from extruding the character upward, and
// the character may keep overlapping such geometry and jitter horizontally
// inside it. Contacts are resolved in triangle order (Gauss-Seidel), so the
// final position depends on the order of `triangles`. There is no sweep: a
// character moving further than its radius in one step can pass through a
// surface. There is no step-up, ledge-grab, crouch or jump.
[[nodiscard]] CharacterState StepCharacter(
    CharacterState state, const CharacterInput& input,
    const CharacterControllerParams& params,
    std::span<const CollisionTriangle> triangles, float dt) noexcept;

// [any thread; reentrant] Flattens a decoded level collision mesh
// (LevelSpatialIR: the COL terrain cells the renderer already uses) into the
// world-space CollisionTriangle list StepCharacter collides against. Each cell's
// SpatialTriangleIR indices are resolved against that cell's vertices; a triangle
// with any out-of-range index is skipped (fail-soft) rather than aborting. The
// output is in the level's world coordinate space. Allocation may throw
// std::bad_alloc; no other failure mode.
[[nodiscard]] std::vector<CollisionTriangle> BuildLevelCollisionTriangles(
    const asset::LevelSpatialIR& spatial);

// [any thread; reentrant] Broadphase cull: appends to `out` every triangle in
// `triangles` whose axis-aligned bounding box, expanded by `radius` on each
// axis, contains `center` -- i.e. the triangles a sphere of that radius at
// `center` could possibly touch. Cheap per-triangle AABB test (correct for large
// floor triangles whose vertices are far but whose surface is under the sphere).
// `out` is cleared first. A non-positive/non-finite radius yields an empty set.
void SelectNearbyCollisionTriangles(std::span<const CollisionTriangle> triangles,
    const asset::Float3IR& center, float radius,
    std::vector<CollisionTriangle>& out);
} // namespace omega::gameplay
