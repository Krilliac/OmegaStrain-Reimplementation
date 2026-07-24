#include "omega/asset/geometry_ir.h"
#include "omega/gameplay/character_controller.h"
#include "omega/gameplay/npc_ai.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>

namespace
{
using omega::asset::Float3IR;
using omega::gameplay::CollisionTriangle;
using omega::gameplay::NpcAwareness;
using omega::gameplay::NpcPatrolPlan;
using omega::gameplay::NpcSeesPlayer;
using omega::gameplay::NpcVisionParams;
using omega::gameplay::PlanNpcPatrol;
using omega::gameplay::SegmentIntersectsTriangle;
using omega::gameplay::StepNpcAwareness;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

// A wall in the plane x = px spanning y,z in [-50,50] (two triangles as one big
// triangle is enough for the on-axis tests here).
constexpr CollisionTriangle Wall(const float px)
{
    return CollisionTriangle{
        .a = Float3IR{.x = px, .y = -50.0F, .z = -50.0F},
        .b = Float3IR{.x = px, .y = 50.0F, .z = -50.0F},
        .c = Float3IR{.x = px, .y = 0.0F, .z = 50.0F},
    };
}
} // namespace

int main()
{
    // --- SegmentIntersectsTriangle ---
    {
        const CollisionTriangle wall = Wall(0.0F);
        Check(SegmentIntersectsTriangle(Float3IR{.x = -5.0F}, Float3IR{.x = 5.0F}, wall),
            "segment crossing the wall plane inside the triangle intersects");
        Check(!SegmentIntersectsTriangle(
                  Float3IR{.x = -5.0F}, Float3IR{.x = -1.0F}, wall),
            "segment that stops before the plane does not intersect");
        Check(!SegmentIntersectsTriangle(
                  Float3IR{.x = -5.0F, .y = 100.0F, .z = 100.0F},
                  Float3IR{.x = 5.0F, .y = 100.0F, .z = 100.0F}, wall),
            "segment crossing the plane outside the triangle does not intersect");
        Check(!SegmentIntersectsTriangle(
                  Float3IR{.x = -5.0F, .y = 0.0F, .z = 0.0F},
                  Float3IR{.x = -5.0F, .y = 5.0F, .z = 0.0F}, wall),
            "segment parallel to the triangle plane does not intersect");
    }

    // --- Vision cone + range + LOS ---
    {
        const NpcVisionParams params{
            .range = 200.0F, .cos_half_angle = 0.5F, .eye_height = 8.0F};
        const Float3IR npc{.x = 0.0F, .y = 0.0F, .z = 0.0F};
        const Float3IR facing{.x = 1.0F, .y = 0.0F, .z = 0.0F};

        Check(NpcSeesPlayer(npc, facing, Float3IR{.x = 100.0F}, params, {}),
            "player in front, in range, clear -> seen");
        Check(!NpcSeesPlayer(npc, facing, Float3IR{.x = 300.0F}, params, {}),
            "player beyond range -> not seen");
        Check(!NpcSeesPlayer(npc, facing, Float3IR{.x = -100.0F}, params, {}),
            "player behind (outside the facing cone) -> not seen");
        Check(!NpcSeesPlayer(npc, facing, Float3IR{.x = 100.0F, .y = 300.0F}, params, {}),
            "player far off-axis (outside the cone) -> not seen");

        const std::array<CollisionTriangle, 1U> blockers{Wall(50.0F)};
        Check(!NpcSeesPlayer(npc, facing, Float3IR{.x = 100.0F}, params,
                  std::span<const CollisionTriangle>(blockers)),
            "player in cone + range but a wall between -> not seen (occluded)");
        Check(NpcSeesPlayer(npc, facing, Float3IR{.x = 30.0F}, params,
                  std::span<const CollisionTriangle>(blockers)),
            "player in front of the wall (nearer than it) -> still seen");
    }

    // --- Awareness transition ---
    {
        Check(StepNpcAwareness(NpcAwareness::Patrol, true) == NpcAwareness::Alerted,
            "Patrol + sees player -> Alerted");
        Check(StepNpcAwareness(NpcAwareness::Patrol, false) == NpcAwareness::Patrol,
            "Patrol + does not see -> Patrol");
        Check(StepNpcAwareness(NpcAwareness::Alerted, false) == NpcAwareness::Alerted,
            "Alerted latches even when the player is lost");
    }

    // --- Patrol planning ---
    {
        const std::array<Float3IR, 2U> waypoints{
            Float3IR{.x = 10.0F, .y = 0.0F, .z = 0.0F},
            Float3IR{.x = 0.0F, .y = 10.0F, .z = 0.0F}};
        const Float3IR def{.x = 1.0F, .y = 0.0F, .z = 0.0F};

        const NpcPatrolPlan moving = PlanNpcPatrol(
            Float3IR{}, std::span<const Float3IR>(waypoints), 0U, 1.0F, def);
        Check(moving.waypoint == 0U && std::abs(moving.move.x - 1.0F) < 1e-4F &&
                  std::abs(moving.move.y) < 1e-4F,
            "away from waypoint 0 -> unit move toward it, index unchanged");
        Check(std::abs(moving.facing.x - moving.move.x) < 1e-6F,
            "facing follows the move direction");

        const NpcPatrolPlan arrived = PlanNpcPatrol(
            Float3IR{.x = 10.0F, .y = 0.0F, .z = 0.0F},
            std::span<const Float3IR>(waypoints), 0U, 1.0F, def);
        Check(arrived.waypoint == 1U,
            "arriving at waypoint 0 advances to waypoint 1");
        Check(arrived.move.x < 0.0F && arrived.move.y > 0.0F,
            "after advancing, moves toward waypoint 1 (-x,+y)");

        const NpcPatrolPlan empty =
            PlanNpcPatrol(Float3IR{}, {}, 0U, 1.0F, def);
        Check(std::abs(empty.move.x) < 1e-6F && std::abs(empty.move.y) < 1e-6F &&
                  empty.facing == def,
            "empty waypoints -> zero move, facing falls back to default");
    }

    if (failures != 0)
    {
        std::cerr << failures << " npc-ai test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "npc-ai tests passed\n";
    return EXIT_SUCCESS;
}
