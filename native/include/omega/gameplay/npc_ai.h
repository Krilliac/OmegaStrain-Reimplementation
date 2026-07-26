#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/gameplay/character_controller.h"

#include <cstdint>
#include <span>

namespace omega::gameplay
{
// Project-owned enemy AI: a kinematic patrolling NPC with a vision cone + line-of-
// sight awareness, informed by the retail stealth model named by the .SO symbol
// table (AI:GameGobjController; Patrol -> SuspectBox/vision Awareness ->
// OnAINewAwarenessMsg -> BlowCover/GeneralAlert). All pure value math -- no
// allocation, no retained state; the caller owns the NPC's CharacterState (moved
// via the shared character_controller) and awareness. Placement is caller-owned:
// the app can use decoded POP GOB spawns and NOD nav nodes, while synthetic tests
// remain free to supply project-authored positions. The .SO supplies structural
// names, not recovered placement or tuning constants.

// Legacy two-state awareness helper retained for focused callers and tests. It
// latches Alerted; the full NpcAwarenessState loop below models reaction, chase,
// lose-sight search, reacquisition, and return to patrol.
enum class NpcAwareness : std::uint8_t
{
    Patrol = 0U,
    Alerted = 1U,
};

// Vision parameters. `range` and the cone half-angle (as its cosine) gate what
// the NPC can perceive; `eye_height` raises the NPC's sighting origin along the
// up-axis so the tests aren't buried in the floor. BOTH the cone and the
// line-of-sight ray are taken from that one eye point (see NpcSeesPlayer), so
// the two gates always agree about where the NPC is looking from. The 8.0 is a
// PROJECT value -- retail's sensing numbers are undecoded.
struct NpcVisionParams
{
    float range = 200.0F;
    float cos_half_angle = 0.5F; // 60-degree half-angle cone
    float eye_height = 8.0F;     // above npc_pos, along up (+Z for MINSK)
    asset::Float3IR up{.x = 0.0F, .y = 0.0F, .z = 1.0F};

    [[nodiscard]] bool operator==(const NpcVisionParams&) const = default;
};

// The result of one patrol-planning step: a horizontal move direction to feed
// StepCharacter, the facing (unit) the NPC now looks along, and the (possibly
// advanced) current waypoint index.
struct NpcPatrolPlan
{
    asset::Float3IR move{};
    asset::Float3IR facing{.x = 1.0F, .y = 0.0F, .z = 0.0F};
    std::uint32_t waypoint = 0U;

    [[nodiscard]] bool operator==(const NpcPatrolPlan&) const = default;
};

// [any thread; reentrant] Does the segment `a`->`b` intersect triangle `t`?
// Moeller-Trumbore, clamped to the segment [0,1] (a ray that hits the triangle
// plane beyond the endpoints does not count). Parallel/degenerate -> false. Used
// for line-of-sight occlusion against the level collision mesh. No allocation.
[[nodiscard]] bool SegmentIntersectsTriangle(
    const asset::Float3IR& a, const asset::Float3IR& b,
    const CollisionTriangle& t) noexcept;

// [any thread; reentrant] Can the NPC (at `npc_pos`, its CENTRE, looking along
// unit `npc_facing`) currently see the player (at `player_pos`, the player's
// centre)? Three gates, applied in order:
//   1. range -- |player_pos - npc_pos| <= `range`, measured CENTRE to CENTRE;
//      the eye offset below is a sighting detail and grants no extra reach.
//   2. cone -- dot(normalize(player_pos - eye), normalize(npc_facing)) >=
//      `cos_half_angle`, judged FROM THE EYE (npc_pos + up*eye_height), not
//      from the chest.
//   3. line of sight -- no triangle in `triangles` intersects the segment
//      eye -> player_pos, i.e. the exact segment the cone gate just accepted.
// Gates 2 and 3 deliberately share one origin (the eye) and one target (the
// player's centre) so the function is geometrically coherent: it can no longer
// accept an angle measured along one line and then trace occlusion along
// another. Aiming at the player's CENTRE rather than at player_pos +
// up*eye_height is a PROJECT decision (retail's sensing model is undecoded):
// the raised point lies above the player's head -- the kinematic controller's
// sphere is only radius-tall -- where overhead geometry blocks sight lines a
// plausible eye-to-torso look clears. One consequence is documented rather
// than special-cased: at very short range the eye->centre direction tips
// steeply downward, so a target almost underfoot can fall outside the cone.
// Non-finite inputs, a zero-length facing, a non-positive `range` or a
// non-finite eye (non-finite `up`/`eye_height`) -> false (cannot see);
// coincident centres, or a player centre exactly at the eye, -> true.
// Brute-force LOS over `triangles` (caller pre-culls). No allocation.
// What this does NOT model: no peripheral falloff (the cone edge is a hard
// binary cut, with no confidence ramp toward it); no per-limb or partial
// visibility (the player is a single point, so cover either blocks the whole
// sighting or none of it); no lighting, shadow, crouch or other stealth
// modifiers; no hearing; and no time-to-notice -- awareness ramping is
// StepNpcAwarenessLoop's job, not this predicate's.
[[nodiscard]] bool NpcSeesPlayer(
    const asset::Float3IR& npc_pos, const asset::Float3IR& npc_facing,
    const asset::Float3IR& player_pos, const NpcVisionParams& params,
    std::span<const CollisionTriangle> triangles) noexcept;

// [any thread; reentrant] The awareness transition: Alerted latches (stays
// Alerted); Patrol -> Alerted when the player is seen this step, else stays
// Patrol.
[[nodiscard]] NpcAwareness StepNpcAwareness(
    NpcAwareness prev, bool sees_player) noexcept;

// The full stealth loop state (the .SO Awareness -> BlowCover -> GeneralAlert
// model with de-escalation): Patrol -> Alerted (just spotted, brief reaction)
// -> Chasing (pursue the player's last-seen position while sight is fresh) ->
// Searching (lost sight; hunt around last-seen) -> Patrol (search timed out).
enum class NpcState : std::uint8_t
{
    Patrol = 0U,
    Alerted = 1U,
    Chasing = 2U,
    Searching = 3U,
};

// Fixed-time thresholds for the loop transitions.
struct NpcAwarenessParams
{
    float alerted_seconds = 0.5F;    // Alerted -> Chasing (reaction delay)
    float lose_sight_seconds = 2.0F; // Chasing (no sight) -> Searching
    float search_seconds = 4.0F;     // Searching -> Patrol (give up)

    [[nodiscard]] bool operator==(const NpcAwarenessParams&) const = default;
};

// Retained per-NPC awareness. `last_seen_player_pos` is the pursuit target while
// Chasing/Searching (updated only on the steps the player is actually seen).
// `timer` is the elapsed time in the current phase (reset on each transition).
struct NpcAwarenessState
{
    NpcState state = NpcState::Patrol;
    asset::Float3IR last_seen_player_pos{};
    bool has_last_seen = false;
    float timer = 0.0F;

    [[nodiscard]] bool operator==(const NpcAwarenessState&) const = default;
};

// [any thread; reentrant] Advances the stealth loop by `dt`. Records
// `last_seen_player_pos` whenever `sees_player`. Transitions: Patrol->Alerted on
// first sight; Alerted->Chasing after `alerted_seconds` of continued sight (or
// immediately if sight is lost during the reaction); Chasing stays while seen and
// ->Searching after `lose_sight_seconds` without sight; Searching->Chasing on
// re-sight, or ->Patrol after `search_seconds`. Pure; no allocation.
[[nodiscard]] NpcAwarenessState StepNpcAwarenessLoop(
    NpcAwarenessState prev, bool sees_player,
    const asset::Float3IR& player_pos, const NpcAwarenessParams& params,
    float dt) noexcept;

// [any thread; reentrant] True while the NPC should pursue its last-seen target
// (Chasing or Searching) rather than follow its patrol route.
[[nodiscard]] bool NpcPursuing(NpcState state) noexcept;

// [any thread; reentrant] True when the NPC should plant itself and aim rather
// than keep moving: Alerted or Chasing WITH the player in sight. A guard that
// has committed to the player stops to bring its weapon up, which is what keeps
// the player inside the cone long enough for the reaction delay plus the aim
// ramp to finish -- a player who keeps walking crosses a cone in less time than
// that, so a guard that also keeps walking can never complete a shot. Chasing
// WITHOUT sight is false (it must walk to the last-seen spot to re-acquire,
// which is what lets it get sight back); Searching is false (it must move to
// hunt); Patrol is false. The .SO symbol table names SetAimMode / AimAt /
// GetAimPercent as an aim behaviour distinct from locomotion, which is the
// grounding for aiming being a separate mode -- it does NOT tell us retail's
// exact stopping rule, which stays undecoded, so this policy is project-chosen.
// Does NOT model strafing, cover, crouching, leaning or suppression, and does
// not itself gate firing (line of sight and the weapon ramp remain the
// caller's).
[[nodiscard]] bool NpcHoldsPositionToAim(NpcState state, bool sees_player) noexcept;

// [any thread; reentrant] Plans one pursuit step toward `target`: the horizontal
// unit move (and matching facing), ignoring the up/z component so a z-climb
// doesn't stall. Within `arrive_radius` horizontally (or non-finite) yields a
// zero move with `default_facing`. No allocation.
[[nodiscard]] NpcPatrolPlan PlanNpcPursuit(
    const asset::Float3IR& pos, const asset::Float3IR& target,
    float arrive_radius, const asset::Float3IR& default_facing) noexcept;

// [any thread; reentrant] Plans one patrol step toward `waypoints[current]`:
// returns the horizontal unit move toward it (and the matching facing), advancing
// to the next waypoint (wrapping) when within `arrive_radius` horizontally.
// Empty waypoints, or a NPC already on its target, yields a zero move (the facing
// falls back to the prior `default_facing`). No allocation.
[[nodiscard]] NpcPatrolPlan PlanNpcPatrol(
    const asset::Float3IR& pos, std::span<const asset::Float3IR> waypoints,
    std::uint32_t current, float arrive_radius,
    const asset::Float3IR& default_facing) noexcept;
} // namespace omega::gameplay
