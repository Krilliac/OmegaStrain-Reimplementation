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
using omega::gameplay::NpcAwarenessParams;
using omega::gameplay::NpcAwarenessState;
using omega::gameplay::NpcHoldsPositionToAim;
using omega::gameplay::NpcPursuing;
using omega::gameplay::NpcState;
using omega::gameplay::PlanNpcPatrol;
using omega::gameplay::PlanNpcPursuit;
using omega::gameplay::SegmentIntersectsTriangle;
using omega::gameplay::StepNpcAwareness;
using omega::gameplay::StepNpcAwarenessLoop;

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

    // --- Full stealth loop: Patrol -> Alerted -> Chasing -> Searching -> Patrol ---
    {
        const NpcAwarenessParams params{.alerted_seconds = 0.5F,
            .lose_sight_seconds = 2.0F, .search_seconds = 4.0F};
        const Float3IR player{.x = 100.0F, .y = 0.0F, .z = 0.0F};
        constexpr float dt = 0.5F;

        // Patrol -> Alerted on first sight, records last-seen.
        NpcAwarenessState s{};
        s = StepNpcAwarenessLoop(s, true, player, params, dt);
        Check(s.state == NpcState::Alerted && s.has_last_seen &&
                  s.last_seen_player_pos == player,
            "Patrol + sees -> Alerted, last-seen recorded");

        // Alerted -> Chasing after alerted_seconds of continued sight.
        s = StepNpcAwarenessLoop(s, true, player, params, dt); // timer 0.5 >= 0.5
        Check(s.state == NpcState::Chasing, "Alerted -> Chasing after reaction delay");
        Check(NpcPursuing(s.state), "Chasing pursues last-seen");

        // Chasing: lose sight; last-seen frozen; -> Searching after lose_sight_seconds.
        const Float3IR moved{.x = 140.0F, .y = 0.0F, .z = 0.0F};
        s = StepNpcAwarenessLoop(s, false, moved, params, dt); // timer 0.5
        Check(s.state == NpcState::Chasing &&
                  s.last_seen_player_pos == player,
            "Chasing without sight holds, last-seen frozen (not updated while unseen)");
        s = StepNpcAwarenessLoop(s, false, moved, params, dt); // 1.0
        s = StepNpcAwarenessLoop(s, false, moved, params, dt); // 1.5
        s = StepNpcAwarenessLoop(s, false, moved, params, dt); // 2.0 >= 2.0
        Check(s.state == NpcState::Searching, "Chasing -> Searching after losing sight");
        Check(NpcPursuing(s.state), "Searching still pursues last-seen");

        // Searching -> Chasing on re-sight (updates last-seen).
        NpcAwarenessState resight = StepNpcAwarenessLoop(s, true, moved, params, dt);
        Check(resight.state == NpcState::Chasing &&
                  resight.last_seen_player_pos == moved,
            "Searching + re-sight -> Chasing, last-seen updated");

        // Searching -> Patrol after search_seconds without sight.
        NpcAwarenessState give_up = s; // Searching, timer 0
        for (int i = 0; i < 8 && give_up.state == NpcState::Searching; ++i)
            give_up = StepNpcAwarenessLoop(give_up, false, moved, params, dt);
        Check(give_up.state == NpcState::Patrol,
            "Searching -> Patrol after the search times out");
    }

    // --- Pursuit planning ---
    {
        const Float3IR def{.x = 1.0F, .y = 0.0F, .z = 0.0F};
        const NpcPatrolPlan chase = PlanNpcPursuit(
            Float3IR{}, Float3IR{.x = 0.0F, .y = 10.0F, .z = 5.0F}, 1.0F, def);
        Check(std::abs(chase.move.x) < 1e-4F && std::abs(chase.move.y - 1.0F) < 1e-4F &&
                  std::abs(chase.move.z) < 1e-6F,
            "pursuit moves horizontally toward the target, ignoring z");
        const NpcPatrolPlan reached = PlanNpcPursuit(
            Float3IR{.x = 0.0F, .y = 10.0F, .z = 0.0F},
            Float3IR{.x = 0.0F, .y = 10.0F, .z = 40.0F}, 1.0F, def);
        Check(std::abs(reached.move.x) < 1e-6F && std::abs(reached.move.y) < 1e-6F &&
                  reached.facing == def,
            "within arrive radius (horizontally) -> zero move, default facing");
        Check(!NpcPursuing(NpcState::Patrol) && NpcPursuing(NpcState::Chasing),
            "NpcPursuing: Patrol no, Chasing yes");
    }

    // --- Hold position to aim (every state x sight combination) ---
    {
        // In sight + committed -> plant and aim, so the aim ramp can finish
        // before a walking player leaves the cone.
        Check(NpcHoldsPositionToAim(NpcState::Alerted, true),
            "Alerted + sees player -> holds position to aim");
        Check(NpcHoldsPositionToAim(NpcState::Chasing, true),
            "Chasing + sees player -> holds position to aim (shoots, not closes)");

        // Everything else keeps moving.
        Check(!NpcHoldsPositionToAim(NpcState::Chasing, false),
            "Chasing without sight -> moves (pursues the last-seen position)");
        Check(!NpcHoldsPositionToAim(NpcState::Alerted, false),
            "Alerted without sight -> moves");
        Check(!NpcHoldsPositionToAim(NpcState::Searching, true),
            "Searching -> moves even with sight (the loop re-alerts it first)");
        Check(!NpcHoldsPositionToAim(NpcState::Searching, false),
            "Searching without sight -> moves (it must hunt)");
        Check(!NpcHoldsPositionToAim(NpcState::Patrol, true),
            "Patrol -> keeps walking its route even with sight");
        Check(!NpcHoldsPositionToAim(NpcState::Patrol, false),
            "Patrol without sight -> keeps walking its route");
    }

    if (failures != 0)
    {
        std::cerr << failures << " npc-ai test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "npc-ai tests passed\n";
    return EXIT_SUCCESS;
}
