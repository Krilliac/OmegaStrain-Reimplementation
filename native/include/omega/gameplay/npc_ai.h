#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/gameplay/character_controller.h"

#include <cstdint>
#include <span>

namespace omega::gameplay
{
// First-slice enemy AI: a kinematic patrolling NPC with a vision cone + line-of-
// sight awareness, reproducing the retail stealth model named by the .SO symbol
// table (AI:GameGobjController; Patrol -> SuspectBox/vision Awareness ->
// OnAINewAwarenessMsg -> BlowCover/GeneralAlert). All pure value math -- no
// allocation, no retained state; the caller owns the NPC's CharacterState (moved
// via the shared character_controller) and awareness. NPC/waypoint PLACEMENT is
// project-placed here: the authentic spawns/nodes live in the undecoded POP GOB:
// section (a later decode slice), not the .SO (which gives only structure).

// The NPC's coarse awareness state. One-way latch to Alerted for this slice
// (BlowCover/GeneralAlert do not de-escalate); a lose-sight timer is a follow-up.
enum class NpcAwareness : std::uint8_t
{
    Patrol = 0U,
    Alerted = 1U,
};

// Vision parameters. `range` and the cone half-angle (as its cosine) gate what
// the NPC can perceive; `eye_height` raises the ray origin along the up-axis so
// the line-of-sight test isn't buried in the floor.
struct NpcVisionParams
{
    float range = 200.0F;
    float cos_half_angle = 0.5F; // 60-degree half-angle cone
    float eye_height = 8.0F;     // along the world up-axis (+Z for MINSK)
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

// [any thread; reentrant] Can the NPC (at `npc_pos`, looking along unit
// `npc_facing`) currently see the player (at `player_pos`)? True iff the player
// is within `range`, inside the facing cone (dot(dir_to_player, facing) >=
// cos_half_angle), AND no `triangles` block the eye->player segment. Non-finite
// / zero-length facing -> false (cannot see). Brute-force LOS over `triangles`
// (caller pre-culls). No allocation.
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
