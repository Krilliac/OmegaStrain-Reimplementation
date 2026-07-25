#pragma once

namespace omega::gameplay
{
// Combat S1: an alerted/chasing enemy with clear line-of-sight aims up, then
// hitscan-fires at the player; a hit removes hitpoints; at zero the player dies.
// All pure value math -- no allocation, no retained global state; the caller
// owns the per-NPC NpcWeaponState and the player's hitpoints, and gates firing on
// the existing npc_ai awareness + NpcSeesPlayer line-of-sight.
//
// The retail model (from the .SO symbol table: SetAimMode/GetAimPercent aim ramp
// -> ForceWeaponFire -> OnAIWeaponFiredMsg; Damage/Kill; SetHitPoints) is
// aim-then-fire, awareness-driven, hitscan-with-aim-time. Authentic weapon STATS
// live in the undecoded GAMEDATA/COMMON/WEAPONS.WDB (a later RE, S4); the values
// here are PROJECT weapon params until then. Accuracy falloff (skill x distance,
// hit_chance < 1) is deferred to S5 -- S1 fires only with clear LOS, so a shot
// that leaves the muzzle hits.

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

// Retained per-NPC firing state: accumulated aim time while engaging, and the
// remaining cooldown before the next shot may leave the muzzle.
struct NpcWeaponState
{
    float aim_seconds = 0.0F;
    float cooldown_seconds = 0.0F;

    [[nodiscard]] bool operator==(const NpcWeaponState&) const = default;
};

// The result of one weapon step: the advanced state and whether a shot fired
// this step (S1: a fired shot hits, since firing is gated on clear LOS).
struct NpcWeaponStep
{
    NpcWeaponState state{};
    bool fired = false;

    [[nodiscard]] bool operator==(const NpcWeaponStep&) const = default;
};

// [any thread; reentrant] Advances the NPC weapon by `dt`. When `engaging` (the
// NPC is alerted/chasing AND has clear line-of-sight to the player -- the caller's
// gate), the aim ramps toward `aim_up_seconds`; once fully aimed and off cooldown
// it fires (setting `fired`, resetting the cooldown). Not engaging decays the aim
// to zero and holds fire. The cooldown always ticks down. Non-finite/negative dt
// -> no change, no fire. Pure; deterministic; no allocation.
[[nodiscard]] NpcWeaponStep StepNpcWeapon(
    NpcWeaponState prev, bool engaging, float dt,
    const WeaponParams& params) noexcept;

// [any thread; reentrant] Removes `amount` hitpoints, clamped at zero. Non-finite
// inputs pass the current value through unchanged (fail-soft). Pure.
[[nodiscard]] float ApplyDamage(float hitpoints, float amount) noexcept;
} // namespace omega::gameplay
