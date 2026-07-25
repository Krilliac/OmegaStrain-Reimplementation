#include "omega/gameplay/combat.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
using omega::gameplay::ApplyDamage;
using omega::gameplay::NpcWeaponState;
using omega::gameplay::NpcWeaponStep;
using omega::gameplay::StepNpcWeapon;
using omega::gameplay::WeaponParams;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

// Run the weapon for `steps` fixed dt steps while `engaging`, counting shots.
int CountShots(NpcWeaponState& state, const bool engaging, const float dt,
    const WeaponParams& params, const int steps)
{
    int shots = 0;
    for (int i = 0; i < steps; ++i)
    {
        const NpcWeaponStep step = StepNpcWeapon(state, engaging, dt, params);
        state = step.state;
        if (step.fired)
            ++shots;
    }
    return shots;
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
        const NpcWeaponState s{};
        // 0.5s aim / dt = 30 steps to be armed; step 29 (0.4833s) is not yet aimed.
        const NpcWeaponStep pre = StepNpcWeapon(s, true, dt, params);
        Check(!pre.fired && pre.state.aim_seconds > 0.0F,
            "engaging ramps aim but does not fire before aim-up completes");
    }

    // Engaging long enough fires; the first shot lands right after aim-up.
    {
        NpcWeaponState s{};
        const int shots = CountShots(s, true, dt, params, 30);
        Check(shots == 1, "fires exactly once when aim-up completes");
    }

    // Cooldown gates the rate: over 1s at 0.5s aim-up + 0.5s cooldown -> ~2 shots.
    {
        NpcWeaponState s{};
        const int shots = CountShots(s, true, dt, params, 60);
        Check(shots == 2, "cooldown limits the fire rate (2 shots in 1s)");
    }

    // Not engaging never fires and resets the aim.
    {
        const NpcWeaponState s{.aim_seconds = 0.4F, .cooldown_seconds = 0.0F};
        const NpcWeaponStep step = StepNpcWeapon(s, false, dt, params);
        Check(!step.fired && step.state.aim_seconds == 0.0F,
            "not engaging holds fire and loses aim");
    }

    // Losing sight mid-aim resets aim, so re-acquiring takes the full aim-up again.
    {
        NpcWeaponState s{};
        CountShots(s, true, dt, params, 20); // partial aim, no shot yet
        const NpcWeaponStep lost = StepNpcWeapon(s, false, dt, params);
        Check(lost.state.aim_seconds == 0.0F,
            "aim is lost when the target is lost");
    }

    // Non-finite / non-positive dt is a no-op (no fire, no state change).
    {
        const NpcWeaponState s{.aim_seconds = 0.5F, .cooldown_seconds = 0.0F};
        const NpcWeaponStep zero = StepNpcWeapon(s, true, 0.0F, params);
        Check(!zero.fired && zero.state == s, "dt<=0 is a no-op");
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

    if (failures != 0)
    {
        std::cerr << failures << " combat test(s) failed\n";
        return 1;
    }
    std::cout << "combat tests passed\n";
    return 0;
}
