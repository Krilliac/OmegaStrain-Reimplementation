#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/gameplay/character_controller.h"

#include <cstdint>
#include <span>

namespace omega::gameplay
{
// Combat S1: an alerted/chasing enemy with clear line-of-sight aims up, then
// hitscan-fires at the player; a hit removes hitpoints; at zero the player dies.
// All pure value math -- no allocation, no retained global state; the caller
// owns the per-NPC WeaponState and the player's hitpoints, and gates firing on
// the existing npc_ai awareness + NpcSeesPlayer line-of-sight.
//
// The retail model (from the .SO symbol table: SetAimMode/GetAimPercent aim ramp
// -> ForceWeaponFire -> OnAIWeaponFiredMsg; Damage/Kill; SetHitPoints) is
// aim-then-fire, awareness-driven, hitscan-with-aim-time. Authentic weapon STATS
// live in the undecoded GAMEDATA/COMMON/WEAPONS.WDB (a later RE, S4); the values
// here are PROJECT weapon params until then. Accuracy falloff (skill x distance,
// hit_chance < 1) is deferred to S5 -- S1 fires only with clear LOS, so a shot
// that leaves the muzzle hits.
//
// Combat S2/S3 extends the same model to the player side of the trigger: a
// player weapon step (no aim ramp -- the human is doing the aiming), hitscan
// target selection against the level collision mesh, and retained per-actor
// health with a death latch. The .SO symbol table names that shape too
// (ForceWeaponFire; Damage/Kill/SetHitPoints) but it carries no numbers: every
// stat here remains a PROJECT value until WEAPONS.WDB is decoded (S4). Not
// modelled at this stage: accuracy falloff, damage types, armour, hit zones
// (head/limb multipliers), penetration, and ragdoll/death animation -- all S5.

// Project weapon parameters (authentic stats await the WEAPONS.WDB decode).
struct WeaponParams
{
    float damage = 8.0F;           // hitpoints removed per hit
    float aim_up_seconds = 0.8F;   // aim-acquire time before the first shot
    float cooldown_seconds = 0.6F; // minimum time between shots
    // Accuracy roll (skill x distance) is S5; S1 fires only with clear LOS.
    float hit_chance = 1.0F;

    [[nodiscard]] bool operator==(const WeaponParams&) const = default;
};

// Retained per-actor firing state, shared by the NPC and the player: accumulated
// aim time while engaging, and the remaining cooldown before the next shot may
// leave the muzzle. `aim_seconds` is NPC-only -- the player weapon leaves it at
// zero, since the human, not the simulation, does the aiming.
struct WeaponState
{
    float aim_seconds = 0.0F;
    float cooldown_seconds = 0.0F;

    [[nodiscard]] bool operator==(const WeaponState&) const = default;
};

// The result of one weapon step: the advanced state and whether a shot fired
// this step (S1: a fired shot hits, since firing is gated on clear LOS).
struct WeaponStep
{
    WeaponState state{};
    bool fired = false;

    [[nodiscard]] bool operator==(const WeaponStep&) const = default;
};

// [any thread; reentrant] Advances the NPC weapon by `dt`. When `engaging` (the
// NPC is alerted/chasing AND has clear line-of-sight to the player -- the caller's
// gate), the aim ramps toward `aim_up_seconds`; once fully aimed and off cooldown
// it fires (setting `fired`, resetting the cooldown). Not engaging decays the aim
// to zero and holds fire. The cooldown always ticks down. Non-finite/negative dt
// -> no change, no fire. Pure; deterministic; no allocation.
[[nodiscard]] WeaponStep StepNpcWeapon(
    WeaponState prev, bool engaging, float dt,
    const WeaponParams& params) noexcept;

// [any thread; reentrant] Advances the PLAYER weapon by `dt`. Unlike the NPC
// weapon there is no aim ramp -- the human is doing the aiming -- so a shot
// fires on the step the trigger is held and the cooldown has expired. Holding
// the trigger produces one shot per `cooldown_seconds` (automatic fire);
// `aim_seconds` is left at zero and carries no meaning for the player.
// Non-finite/non-positive dt -> no change, no fire. Pure; deterministic.
//
// Not modelled here (S5): recoil, spread/accuracy falloff, magazine capacity,
// reloads, burst/semi/full fire-mode selection, and weapon switching.
[[nodiscard]] WeaponStep StepPlayerWeapon(
    WeaponState prev, bool trigger_held, float dt,
    const WeaponParams& params) noexcept;

// The result of one hitscan query: whether anything was hit, which target
// (index into the caller's parallel arrays) and how far along the ray.
struct HitscanResult
{
    bool hit = false;
    std::uint32_t target = 0U;
    float distance = 0.0F;

    [[nodiscard]] bool operator==(const HitscanResult&) const = default;
};

// [any thread; reentrant] Nearest sphere target struck by the ray
// `origin` + t*`direction` for t in (0, max_range], subject to level occlusion:
// a target is only reported if no triangle in `occluders` blocks the segment
// from `origin` to the impact point. Targets are spheres of `target_radius`
// centred on `target_positions[i]`; `alive[i] == false` targets are skipped.
// A zero-length/non-finite direction, an empty target list, or a non-finite
// max_range yields `hit == false`. Ties resolve to the smaller distance, then
// the lower index, so the result is deterministic. Brute force over targets and
// occluders (the caller pre-culls). No allocation.
//
// `alive` may be shorter than `target_positions`; missing entries count as
// alive (fail-soft -- never an out-of-bounds read). A non-finite or negative
// `target_radius`, a non-positive `max_range`, and a non-finite target position
// are likewise fail-soft (no hit / that target skipped).
//
// A target is only considered when its centre lies AHEAD of the origin along
// the ray. That check is load-bearing rather than an optimisation: a sphere
// large enough to contain the origin has its entry point behind the shooter, so
// the exit point would be taken instead -- and the exit of a sphere centred
// behind you is still in front of you. Without the guard a shot could kill an
// enemy standing at the shooter's back whenever the hit radius was comparable
// to the spacing between actors.
//
// A single sphere per target is the whole hit model at this stage: no capsules,
// no per-bone hit zones, no penetration through occluders, and no falloff of
// damage with distance -- those are S5.
[[nodiscard]] HitscanResult ResolveHitscan(
    const asset::Float3IR& origin, const asset::Float3IR& direction,
    std::span<const asset::Float3IR> target_positions,
    std::span<const bool> alive, float target_radius, float max_range,
    std::span<const CollisionTriangle> occluders) noexcept;

// [any thread; reentrant] Removes `amount` hitpoints, clamped at zero. Non-finite
// inputs pass the current value through unchanged (fail-soft). Pure.
[[nodiscard]] float ApplyDamage(float hitpoints, float amount) noexcept;

// Retained per-actor health. `alive` latches false once hitpoints reach zero
// and never recovers on its own -- respawn is the caller's decision, made by
// constructing a fresh HealthState. Models no armour, damage type, hit zone,
// or resistance; those are S5.
struct HealthState
{
    float hitpoints = 100.0F;
    bool alive = true;

    [[nodiscard]] bool operator==(const HealthState&) const = default;
};

// [any thread; reentrant] Applies `amount` damage. Hitpoints clamp at zero and
// `alive` latches false there. A non-finite amount, or damage to an already
// dead actor, returns `prev` unchanged (fail-soft). Pure.
//
// Non-finite `prev.hitpoints` is also returned unchanged rather than being
// resolved into a death, so a corrupt value never silently kills an actor.
[[nodiscard]] HealthState ApplyDamageToHealth(
    HealthState prev, float amount) noexcept;
} // namespace omega::gameplay
