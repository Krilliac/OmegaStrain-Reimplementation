#include "omega/gameplay/character_controller.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace
{
using omega::asset::Float3IR;
using omega::gameplay::CharacterControllerParams;
using omega::gameplay::CharacterInput;
using omega::gameplay::CharacterState;
using omega::gameplay::ClosestPointOnTriangle;
using omega::gameplay::CollisionTriangle;
using omega::gameplay::StepCharacter;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

[[nodiscard]] bool Near(const float a, const float b, const float eps) noexcept
{
    return std::fabs(a - b) <= eps;
}

[[nodiscard]] bool NearPoint(
    const Float3IR& a, const Float3IR& b, const float eps) noexcept
{
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

// A large +Z-facing floor triangle spanning the XY origin at z = 0.
constexpr CollisionTriangle kFloor{
    .a = {.x = -500.0F, .y = -500.0F, .z = 0.0F},
    .b = {.x = 500.0F, .y = -500.0F, .z = 0.0F},
    .c = {.x = 0.0F, .y = 500.0F, .z = 0.0F},
};

// A large vertical wall plane at x = 20 (normal along X).
constexpr CollisionTriangle kWall{
    .a = {.x = 20.0F, .y = -500.0F, .z = -300.0F},
    .b = {.x = 20.0F, .y = 500.0F, .z = -300.0F},
    .c = {.x = 20.0F, .y = 0.0F, .z = 400.0F},
};

// A nook: four near-vertical faces around the vertical axis, each with its base
// 3 units out and leaning outward to 10 units at z = 400 -- about 1 degree off
// vertical. A default-radius (4.0) character standing on kFloor inside this
// overlaps all four faces at once, and because the nook is narrower than the
// character no order of depenetration clears all four: the contact set is
// over-constrained and the resolve's iteration budget can never satisfy it.
// This is a synthetic reproduction of that shape, chosen to be the smallest
// geometry that exhibits it -- it is not decoded level data.
constexpr CollisionTriangle kNookPlusX{
    .a = {.x = 3.0F, .y = -500.0F, .z = 0.0F},
    .b = {.x = 3.0F, .y = 500.0F, .z = 0.0F},
    .c = {.x = 10.0F, .y = 0.0F, .z = 400.0F},
};
constexpr CollisionTriangle kNookMinusX{
    .a = {.x = -3.0F, .y = -500.0F, .z = 0.0F},
    .b = {.x = -3.0F, .y = 500.0F, .z = 0.0F},
    .c = {.x = -10.0F, .y = 0.0F, .z = 400.0F},
};
constexpr CollisionTriangle kNookPlusY{
    .a = {.x = -500.0F, .y = 3.0F, .z = 0.0F},
    .b = {.x = 500.0F, .y = 3.0F, .z = 0.0F},
    .c = {.x = 0.0F, .y = 10.0F, .z = 400.0F},
};
constexpr CollisionTriangle kNookMinusY{
    .a = {.x = -500.0F, .y = -3.0F, .z = 0.0F},
    .b = {.x = 500.0F, .y = -3.0F, .z = 0.0F},
    .c = {.x = 0.0F, .y = -10.0F, .z = 400.0F},
};

void TestClosestPointOnTriangle()
{
    // A point above the interior projects straight down onto the face.
    Check(NearPoint(ClosestPointOnTriangle({.x = 0.0F, .y = 0.0F, .z = 10.0F}, kFloor),
              {.x = 0.0F, .y = 0.0F, .z = 0.0F}, 1.0e-4F),
        "closest point of an interior point is its projection on the face");
    // A point well past vertex B clamps to B.
    Check(NearPoint(
              ClosestPointOnTriangle({.x = 900.0F, .y = -900.0F, .z = 3.0F}, kFloor),
              kFloor.b, 1.0e-4F),
        "closest point past a vertex is that vertex");
    // A point beyond edge AB (below y=-500, within x span) lands on the edge.
    const Float3IR on_edge =
        ClosestPointOnTriangle({.x = 0.0F, .y = -900.0F, .z = 0.0F}, kFloor);
    Check(Near(on_edge.y, -500.0F, 1.0e-3F) && Near(on_edge.z, 0.0F, 1.0e-3F) &&
              on_edge.x >= -500.0F && on_edge.x <= 500.0F,
        "closest point beyond an edge lands on that edge");
}

void TestFallsAndRestsOnFloor()
{
    const std::array<CollisionTriangle, 1U> tris{kFloor};
    CharacterControllerParams params{};
    params.radius = 2.0F;
    params.gravity = 30.0F;
    params.move_speed = 40.0F;

    CharacterState state{.position = {.x = 0.0F, .y = 0.0F, .z = 20.0F}};
    float min_z = state.position.z;
    for (int step = 0; step < 300; ++step)
    {
        state = StepCharacter(state, CharacterInput{}, params,
            std::span<const CollisionTriangle>{tris}, 1.0F / 60.0F);
        min_z = std::min(min_z, state.position.z);
    }
    Check(Near(state.position.z, params.radius, 0.05F),
        "a falling character settles at rest one radius above the floor");
    Check(state.grounded, "the settled character is grounded");
    Check(Near(state.velocity.z, 0.0F, 0.5F),
        "the grounded character has ~zero vertical velocity");
    Check(min_z >= params.radius - 0.2F,
        "the character never sinks meaningfully through the floor");
}

void TestStationaryCharacterDoesNotDriftUpward()
{
    // Regression. A character with zero move input, resting in an
    // over-constrained contact set, was extruded upward a little on every step:
    // the faces cannot all be satisfied, so the resolve never converged, ran its
    // whole iteration budget every step, and each fixed-order push cycle netted
    // the small upward component the near-vertical faces share. On TORONTO3 a
    // guard holding position climbed several units over ~100 frames with x and y
    // unchanged, rising out of its own vision cone.
    //
    // This pins NON-ACCUMULATION, not an exact z. Where the character settles is
    // a property of the geometry and is not asserted; what is asserted is that
    // wherever it settles, it stays there -- 600 further steps of standing still
    // must not move it.
    const std::array<CollisionTriangle, 5U> tris{
        kFloor, kNookPlusX, kNookMinusX, kNookPlusY, kNookMinusY};
    const CharacterControllerParams params{}; // defaults: radius 4, gravity 30

    // Start already at rest height (one radius above kFloor), so the settle
    // window only has to absorb the first contact with the nook faces.
    CharacterState state{.position = {.x = 0.0F, .y = 0.0F, .z = 4.0F}};
    constexpr int kSettleSteps = 10;
    float settled_z = state.position.z;
    float highest_z = state.position.z;
    float lowest_z = state.position.z;
    for (int step = 0; step < 600; ++step)
    {
        state = StepCharacter(state, CharacterInput{}, params,
            std::span<const CollisionTriangle>{tris}, 1.0F / 60.0F);
        if (step < kSettleSteps)
            continue;
        if (step == kSettleSteps)
        {
            settled_z = state.position.z;
            highest_z = settled_z;
            lowest_z = settled_z;
            continue;
        }
        if (state.position.z > highest_z)
            highest_z = state.position.z;
        if (state.position.z < lowest_z)
            lowest_z = state.position.z;
    }

    Check(highest_z - settled_z <= 0.25F,
        "a stationary character wedged in a nook does not creep upward");
    Check(settled_z - lowest_z <= 0.25F,
        "a stationary character wedged in a nook does not creep downward");
    Check(state.grounded,
        "the stationary character is still standing on the floor after 600 steps");
    Check(state.position.z <= params.radius + 0.25F,
        "the character rests on the floor, not part-way up the nook faces");
}

void TestStopsAtWall()
{
    const std::array<CollisionTriangle, 2U> tris{kFloor, kWall};
    CharacterControllerParams params{};
    params.radius = 2.0F;
    params.gravity = 30.0F;
    params.move_speed = 40.0F;

    // On the floor, driving straight into the +X wall.
    CharacterState state{.position = {.x = 0.0F, .y = 0.0F, .z = 2.0F}};
    const CharacterInput into_wall{.move = {.x = 1.0F, .y = 0.0F, .z = 0.0F}};
    for (int step = 0; step < 200; ++step)
        state = StepCharacter(state, into_wall, params,
            std::span<const CollisionTriangle>{tris}, 1.0F / 60.0F);

    Check(state.position.x <= 20.0F - params.radius + 0.1F,
        "the character stops at the wall and does not tunnel through it");
    Check(state.position.x > 10.0F,
        "the character did travel toward the wall before stopping");
    Check(state.grounded, "the character remains grounded against the wall");
}

void TestSlidesAlongWall()
{
    const std::array<CollisionTriangle, 2U> tris{kFloor, kWall};
    CharacterControllerParams params{};
    params.radius = 2.0F;
    params.move_speed = 40.0F;

    // Push diagonally into the wall (+X) and along it (+Y): X is blocked, Y slides.
    CharacterState state{.position = {.x = 0.0F, .y = 0.0F, .z = 2.0F}};
    const CharacterInput diagonal{.move = {.x = 1.0F, .y = 1.0F, .z = 0.0F}};
    for (int step = 0; step < 200; ++step)
        state = StepCharacter(state, diagonal, params,
            std::span<const CollisionTriangle>{tris}, 1.0F / 60.0F);

    Check(state.position.x <= 20.0F - params.radius + 0.1F,
        "the blocked axis is clamped at the wall");
    Check(state.position.y > 20.0F,
        "the character slides along the wall on the free axis");
}

void TestAirborneNotGrounded()
{
    const std::array<CollisionTriangle, 1U> tris{kFloor};
    CharacterControllerParams params{};
    params.radius = 2.0F;
    params.gravity = 30.0F;

    // High above the floor: one step leaves it airborne and accelerating down.
    CharacterState state{.position = {.x = 0.0F, .y = 0.0F, .z = 500.0F}};
    const CharacterState after = StepCharacter(state, CharacterInput{}, params,
        std::span<const CollisionTriangle>{tris}, 1.0F / 60.0F);
    Check(!after.grounded, "a character high above the floor is not grounded");
    Check(after.velocity.z < 0.0F, "gravity accelerates the airborne character downward");
    Check(after.position.z < 500.0F, "the airborne character falls");
}

void TestNonFinitePassThrough()
{
    const std::array<CollisionTriangle, 1U> tris{kFloor};
    CharacterState state{
        .position = {.x = std::nanf(""), .y = 0.0F, .z = 0.0F}};
    const CharacterState after = StepCharacter(state, CharacterInput{},
        CharacterControllerParams{}, std::span<const CollisionTriangle>{tris},
        1.0F / 60.0F);
    // A NaN struct never compares equal to itself, so verify pass-through
    // field-by-field: the NaN survives and the finite fields are unchanged.
    Check(std::isnan(after.position.x) && after.position.y == state.position.y &&
              after.position.z == state.position.z &&
              after.velocity == state.velocity && after.grounded == state.grounded,
        "a non-finite state passes through unchanged");
}
void TestBuildLevelCollisionTriangles()
{
    omega::asset::LevelSpatialIR spatial;
    // Cell 0: one valid triangle over 3 vertices.
    omega::asset::SpatialMeshIR cell0;
    cell0.vertices = {Float3IR{0.0F, 0.0F, 0.0F}, Float3IR{1.0F, 0.0F, 0.0F},
                      Float3IR{0.0F, 1.0F, 0.0F}};
    cell0.triangles = {omega::asset::SpatialTriangleIR{.vertex_indices = {0U, 1U, 2U}}};
    // Cell 1: one valid + one with an out-of-range index (must be skipped).
    omega::asset::SpatialMeshIR cell1;
    cell1.vertices = {Float3IR{5.0F, 0.0F, 0.0F}, Float3IR{6.0F, 0.0F, 0.0F},
                      Float3IR{5.0F, 1.0F, 0.0F}};
    cell1.triangles = {
        omega::asset::SpatialTriangleIR{.vertex_indices = {0U, 1U, 2U}},
        omega::asset::SpatialTriangleIR{.vertex_indices = {0U, 1U, 99U}},
    };
    spatial.terrain_cells = {cell0, cell1};

    const auto triangles = omega::gameplay::BuildLevelCollisionTriangles(spatial);
    Check(triangles.size() == 2U,
        "level collision build keeps valid triangles and skips out-of-range ones");
    Check(!triangles.empty() && triangles[0].a == cell0.vertices[0] &&
              triangles[0].b == cell0.vertices[1] &&
              triangles[0].c == cell0.vertices[2],
        "level collision triangle resolves cell vertices by index");
    Check(triangles.size() > 1U && triangles[1].a == cell1.vertices[0],
        "level collision spans multiple cells");
}
void TestSelectNearbyCollisionTriangles()
{
    const CollisionTriangle near_tri{
        .a = {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        .b = {.x = 10.0F, .y = 0.0F, .z = 0.0F},
        .c = {.x = 0.0F, .y = 10.0F, .z = 0.0F}};
    const CollisionTriangle far_tri{
        .a = {.x = 500.0F, .y = 500.0F, .z = 500.0F},
        .b = {.x = 510.0F, .y = 500.0F, .z = 500.0F},
        .c = {.x = 500.0F, .y = 510.0F, .z = 500.0F}};
    const std::array<CollisionTriangle, 2> tris{near_tri, far_tri};
    std::vector<CollisionTriangle> out;
    omega::gameplay::SelectNearbyCollisionTriangles(
        tris, Float3IR{.x = 3.0F, .y = 3.0F, .z = 5.0F}, 8.0F, out);
    Check(out.size() == 1U && out[0] == near_tri,
          "broadphase keeps the near triangle and drops the far one");
    omega::gameplay::SelectNearbyCollisionTriangles(
        tris, Float3IR{.x = 3.0F, .y = 3.0F, .z = 5.0F}, 0.0F, out);
    Check(out.empty(), "broadphase with radius 0 selects nothing");
}
} // namespace

int main()
{
    TestClosestPointOnTriangle();
    TestFallsAndRestsOnFloor();
    TestStationaryCharacterDoesNotDriftUpward();
    TestStopsAtWall();
    TestSlidesAlongWall();
    TestAirborneNotGrounded();
    TestNonFinitePassThrough();
    TestBuildLevelCollisionTriangles();
    TestSelectNearbyCollisionTriangles();

    if (failures != 0)
    {
        std::cerr << failures << " character controller test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "character controller tests passed\n";
    return EXIT_SUCCESS;
}
