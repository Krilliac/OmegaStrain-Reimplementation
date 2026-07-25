#include "omega/gameplay/combat.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>

namespace
{
using omega::asset::Float3IR;
using omega::gameplay::ApplyDamage;
using omega::gameplay::ApplyDamageToHealth;
using omega::gameplay::CollisionTriangle;
using omega::gameplay::HealthState;
using omega::gameplay::HitscanResult;
using omega::gameplay::ResolveHitscan;
using omega::gameplay::StepNpcWeapon;
using omega::gameplay::StepPlayerWeapon;
using omega::gameplay::WeaponParams;
using omega::gameplay::WeaponState;
using omega::gameplay::WeaponStep;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

[[nodiscard]] bool Near(const float a, const float b) noexcept
{
    return std::fabs(a - b) <= 1e-3F;
}

// Run the weapon for `steps` fixed dt steps while `engaging`, counting shots.
int CountShots(WeaponState& state, const bool engaging, const float dt,
    const WeaponParams& params, const int steps)
{
    int shots = 0;
    for (int i = 0; i < steps; ++i)
    {
        const WeaponStep step = StepNpcWeapon(state, engaging, dt, params);
        state = step.state;
        if (step.fired)
            ++shots;
    }
    return shots;
}

// The player equivalent: `steps` fixed dt steps with the trigger held or not.
int CountPlayerShots(WeaponState& state, const bool trigger_held, const float dt,
    const WeaponParams& params, const int steps)
{
    int shots = 0;
    for (int i = 0; i < steps; ++i)
    {
        const WeaponStep step =
            StepPlayerWeapon(state, trigger_held, dt, params);
        state = step.state;
        if (step.fired)
            ++shots;
    }
    return shots;
}

// A large occluding triangle in the plane x == px (same shape npc_ai_tests uses
// for line-of-sight walls), spanning the y/z origin so an +x ray crosses it.
[[nodiscard]] CollisionTriangle Wall(const float px)
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
    constexpr float dt = 1.0F / 60.0F;
    constexpr WeaponParams params{.damage = 8.0F,
        .aim_up_seconds = 0.5F,
        .cooldown_seconds = 0.5F,
        .hit_chance = 1.0F};

    // Aim ramps only while engaging; no shot before the aim is acquired.
    {
        const WeaponState s{};
        // 0.5s aim / dt = 30 steps to be armed; step 29 (0.4833s) is not yet aimed.
        const WeaponStep pre = StepNpcWeapon(s, true, dt, params);
        Check(!pre.fired && pre.state.aim_seconds > 0.0F,
            "engaging ramps aim but does not fire before aim-up completes");
    }

    // Engaging long enough fires; the first shot lands right after aim-up.
    {
        WeaponState s{};
        const int shots = CountShots(s, true, dt, params, 30);
        Check(shots == 1, "fires exactly once when aim-up completes");
    }

    // Cooldown gates the rate: over 1s at 0.5s aim-up + 0.5s cooldown -> ~2 shots.
    {
        WeaponState s{};
        const int shots = CountShots(s, true, dt, params, 60);
        Check(shots == 2, "cooldown limits the fire rate (2 shots in 1s)");
    }

    // Not engaging never fires and resets the aim.
    {
        const WeaponState s{.aim_seconds = 0.4F, .cooldown_seconds = 0.0F};
        const WeaponStep step = StepNpcWeapon(s, false, dt, params);
        Check(!step.fired && step.state.aim_seconds == 0.0F,
            "not engaging holds fire and loses aim");
    }

    // Losing sight mid-aim resets aim, so re-acquiring takes the full aim-up again.
    {
        WeaponState s{};
        CountShots(s, true, dt, params, 20); // partial aim, no shot yet
        const WeaponStep lost = StepNpcWeapon(s, false, dt, params);
        Check(lost.state.aim_seconds == 0.0F,
            "aim is lost when the target is lost");
    }

    // Non-finite / non-positive dt is a no-op (no fire, no state change).
    {
        const WeaponState s{.aim_seconds = 0.5F, .cooldown_seconds = 0.0F};
        const WeaponStep zero = StepNpcWeapon(s, true, 0.0F, params);
        Check(!zero.fired && zero.state == s, "dt<=0 is a no-op");
    }

    // --- StepPlayerWeapon (S2) ---

    // No aim ramp: the very first trigger-held step fires and arms the cooldown.
    {
        const WeaponState s{};
        const WeaponStep first = StepPlayerWeapon(s, true, dt, params);
        Check(first.fired, "player fires on the first trigger-held step");
        Check(Near(first.state.cooldown_seconds, params.cooldown_seconds),
            "player shot arms the full cooldown");
        Check(first.state.aim_seconds == 0.0F,
            "player weapon leaves aim_seconds at zero (the human aims)");
    }

    // Holding the trigger does not fire again inside the cooldown window.
    {
        WeaponState s{};
        const int first = CountPlayerShots(s, true, dt, params, 1);
        const int within = CountPlayerShots(s, true, dt, params, 20); // 0.333s
        Check(first == 1 && within == 0,
            "held trigger does not re-fire before cooldown_seconds elapses");
    }

    // Automatic fire: 1s of held trigger at a 0.5s cooldown is 2 shots.
    {
        WeaponState s{};
        const int shots = CountPlayerShots(s, true, dt, params, 60);
        Check(shots == 2, "held trigger fires once per cooldown (2 shots in 1s)");
    }

    // A released trigger never fires, but the cooldown still drains.
    {
        WeaponState s{.aim_seconds = 0.0F, .cooldown_seconds = 0.5F};
        const int shots = CountPlayerShots(s, false, dt, params, 60);
        Check(shots == 0, "released trigger never fires");
        Check(s.cooldown_seconds == 0.0F,
            "cooldown drains even while the trigger is released");
    }

    // Non-finite / non-positive dt is a no-op for the player weapon too.
    {
        const WeaponState s{.aim_seconds = 0.0F, .cooldown_seconds = 0.0F};
        const WeaponStep nan_step = StepPlayerWeapon(
            s, true, std::numeric_limits<float>::quiet_NaN(), params);
        Check(!nan_step.fired && nan_step.state == s,
            "player weapon: non-finite dt is a no-op");
        const WeaponStep zero = StepPlayerWeapon(s, true, 0.0F, params);
        Check(!zero.fired && zero.state == s,
            "player weapon: dt<=0 is a no-op");
    }

    // --- ResolveHitscan (S2) ---
    {
        constexpr float radius = 1.0F;
        const std::array<Float3IR, 2> targets{
            Float3IR{.x = 10.0F, .y = 0.0F, .z = 0.0F},
            Float3IR{.x = 20.0F, .y = 0.0F, .z = 0.0F},
        };
        const std::array<bool, 2> both_alive{true, true};
        const Float3IR origin{};
        const Float3IR forward{.x = 1.0F, .y = 0.0F, .z = 0.0F};
        const std::span<const CollisionTriangle> no_walls{};

        // The nearer of two targets on the ray wins; the distance is to the
        // sphere surface (10 - radius), not to the centre.
        {
            const HitscanResult r = ResolveHitscan(origin, forward, targets,
                both_alive, radius, 100.0F, no_walls);
            Check(r.hit && r.target == 0U && Near(r.distance, 9.0F),
                "hitscan reports the nearer of two targets on the ray");
        }

        // An unnormalised direction gives the same world distance.
        {
            const Float3IR scaled{.x = 7.0F, .y = 0.0F, .z = 0.0F};
            const HitscanResult r = ResolveHitscan(origin, scaled, targets,
                both_alive, radius, 100.0F, no_walls);
            Check(r.hit && r.target == 0U && Near(r.distance, 9.0F),
                "hitscan normalises the direction before measuring distance");
        }

        // max_range is inclusive at the impact distance and excludes beyond it.
        {
            const HitscanResult on_edge = ResolveHitscan(origin, forward,
                targets, both_alive, radius, 9.0F, no_walls);
            Check(on_edge.hit && on_edge.target == 0U,
                "a target exactly at max_range is still hit (t <= max_range)");
            const HitscanResult past = ResolveHitscan(origin, forward, targets,
                both_alive, radius, 8.5F, no_walls);
            Check(!past.hit, "a target beyond max_range is missed");
        }

        // A wall between the shooter and the target blocks the shot entirely.
        {
            const std::array<CollisionTriangle, 1> walls{Wall(5.0F)};
            const HitscanResult r = ResolveHitscan(origin, forward, targets,
                both_alive, radius, 100.0F, walls);
            Check(!r.hit, "an occluding triangle blocks the hitscan");
        }

        // A wall BEHIND both targets does not block anything.
        {
            const std::array<CollisionTriangle, 1> walls{Wall(30.0F)};
            const HitscanResult r = ResolveHitscan(origin, forward, targets,
                both_alive, radius, 100.0F, walls);
            Check(r.hit && r.target == 0U,
                "a triangle past the impact point does not occlude");
        }

        // A dead target is skipped and the live one behind it is reported.
        {
            const std::array<bool, 2> first_dead{false, true};
            const HitscanResult r = ResolveHitscan(origin, forward, targets,
                first_dead, radius, 100.0F, no_walls);
            Check(r.hit && r.target == 1U && Near(r.distance, 19.0F),
                "a dead target is skipped and the live one behind it is hit");
        }

        // A target whose sphere contains the origin but whose centre sits
        // BEHIND it is not hit. Regression: the entry point of such a sphere is
        // behind the shooter, so the exit point used to be taken instead, and
        // the exit of a sphere centred behind you is still in front of you --
        // which let a forward shot kill an enemy standing at the shooter's back.
        {
            const std::array<Float3IR, 1> behind{
                Float3IR{.x = -6.0F, .y = 0.0F, .z = 0.0F}};
            const std::array<bool, 1> live{true};
            const HitscanResult r = ResolveHitscan(origin, forward, behind,
                live, 20.0F, 100.0F, no_walls);
            Check(!r.hit,
                "an enemy behind the shooter is not hit even when its hit "
                "sphere contains the muzzle");

            // The same enemy, the same overlapping radius, in front: still hit,
            // so the guard rejects only what is behind rather than everything
            // the origin happens to be inside of.
            const std::array<Float3IR, 1> ahead{
                Float3IR{.x = 6.0F, .y = 0.0F, .z = 0.0F}};
            const HitscanResult front = ResolveHitscan(origin, forward, ahead,
                live, 20.0F, 100.0F, no_walls);
            Check(front.hit && front.target == 0U,
                "an enemy ahead is still hit when its hit sphere contains the "
                "muzzle");
        }

        // A short `alive` span is fail-soft: uncovered entries count as alive.
        {
            const std::array<bool, 1> only_first{false};
            const HitscanResult r = ResolveHitscan(origin, forward, targets,
                only_first, radius, 100.0F, no_walls);
            Check(r.hit && r.target == 1U,
                "a short alive span treats missing entries as alive");
        }

        // A zero-length direction cannot be traced.
        {
            const HitscanResult r = ResolveHitscan(origin, Float3IR{}, targets,
                both_alive, radius, 100.0F, no_walls);
            Check(!r.hit, "a zero-length direction yields no hit");
        }

        // Non-finite inputs are fail-soft, never a crash or a bogus hit.
        {
            constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
            const HitscanResult bad_dir = ResolveHitscan(origin,
                Float3IR{.x = kNaN, .y = 0.0F, .z = 0.0F}, targets, both_alive,
                radius, 100.0F, no_walls);
            Check(!bad_dir.hit, "a non-finite direction yields no hit");
            const HitscanResult bad_range = ResolveHitscan(origin, forward,
                targets, both_alive, radius, kNaN, no_walls);
            Check(!bad_range.hit, "a non-finite max_range yields no hit");
        }

        // No targets at all: no hit, and the default-constructed result.
        {
            const std::span<const Float3IR> none{};
            const HitscanResult r = ResolveHitscan(
                origin, forward, none, both_alive, radius, 100.0F, no_walls);
            Check(r == HitscanResult{}, "an empty target list yields no hit");
        }

        // Exact ties resolve to the lower index, deterministically.
        {
            const std::array<Float3IR, 2> coincident{
                Float3IR{.x = 10.0F, .y = 0.0F, .z = 0.0F},
                Float3IR{.x = 10.0F, .y = 0.0F, .z = 0.0F},
            };
            const HitscanResult a = ResolveHitscan(origin, forward, coincident,
                both_alive, radius, 100.0F, no_walls);
            const HitscanResult b = ResolveHitscan(origin, forward, coincident,
                both_alive, radius, 100.0F, no_walls);
            Check(a.hit && a.target == 0U && a == b,
                "an exact distance tie resolves to the lower index, repeatably");
        }
    }

    // ApplyDamage removes hitpoints and clamps at zero.
    Check(ApplyDamage(100.0F, 8.0F) == 92.0F, "damage subtracts hitpoints");
    Check(ApplyDamage(5.0F, 8.0F) == 0.0F, "damage clamps at zero");
    Check(ApplyDamage(0.0F, 8.0F) == 0.0F, "already-dead stays at zero");

    // Damage stacks toward death (multiple hits).
    {
        float hp = 100.0F;
        for (int i = 0; i < 20; ++i)
            hp = ApplyDamage(hp, 8.0F);
        Check(hp == 0.0F, "repeated hits drive hitpoints to zero (death)");
    }

    // --- ApplyDamageToHealth (S3) ---

    // A survivable hit reduces hitpoints and leaves the actor alive.
    {
        const HealthState hurt = ApplyDamageToHealth(HealthState{}, 8.0F);
        Check(hurt == HealthState{.hitpoints = 92.0F, .alive = true},
            "damage reduces hitpoints and leaves a survivor alive");
    }

    // Overkill clamps at zero and latches `alive` false.
    {
        const HealthState dead = ApplyDamageToHealth(
            HealthState{.hitpoints = 5.0F, .alive = true}, 8.0F);
        Check(dead == HealthState{.hitpoints = 0.0F, .alive = false},
            "overkill clamps hitpoints at zero and latches death");
    }

    // Boundary: exactly lethal damage kills -- zero hitpoints is dead, not alive.
    {
        const HealthState dead = ApplyDamageToHealth(
            HealthState{.hitpoints = 8.0F, .alive = true}, 8.0F);
        Check(dead == HealthState{.hitpoints = 0.0F, .alive = false},
            "exactly lethal damage kills (zero hitpoints is dead)");
    }

    // Damage to an already dead actor changes nothing (the latch holds).
    {
        const HealthState corpse{.hitpoints = 0.0F, .alive = false};
        Check(ApplyDamageToHealth(corpse, 8.0F) == corpse,
            "damage to a dead actor is a no-op");
    }

    // Non-finite damage is fail-soft: the actor is returned untouched.
    {
        const HealthState s{.hitpoints = 40.0F, .alive = true};
        Check(ApplyDamageToHealth(
                  s, std::numeric_limits<float>::quiet_NaN()) == s,
            "non-finite damage is a no-op");
        Check(ApplyDamageToHealth(
                  s, std::numeric_limits<float>::infinity()) == s,
            "infinite damage is a no-op");
    }

    // Repeated hits drive a full-health actor to death exactly once.
    {
        HealthState h{};
        for (int i = 0; i < 20; ++i)
            h = ApplyDamageToHealth(h, 8.0F);
        Check(h.hitpoints == 0.0F && !h.alive,
            "repeated hits kill, and the death latch survives further hits");
    }

    if (failures != 0)
    {
        std::cerr << failures << " combat test(s) failed\n";
        return 1;
    }
    std::cout << "combat tests passed\n";
    return 0;
}
