#include "omega/gameplay/diagnostic_target_fire.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <optional>
#include <string_view>
#include <type_traits>

namespace
{
using omega::gameplay::DiagnosticAimPointQ16;
using omega::gameplay::DiagnosticAimTargetQ16;
using omega::gameplay::DiagnosticTargetFireError;
using omega::gameplay::DiagnosticTargetFireInput;
using omega::gameplay::DiagnosticTargetFireState;
using omega::gameplay::DiagnosticTargetFireStep;

using StepResult =
    std::expected<DiagnosticTargetFireStep, DiagnosticTargetFireError>;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;

    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

[[nodiscard]] DiagnosticTargetFireInput EnabledInput(
    const std::optional<DiagnosticAimPointQ16> pointer,
    const bool target_held = false, const bool fire_pressed = false) noexcept
{
    return DiagnosticTargetFireInput{
        .pointer = pointer,
        .enabled = true,
        .target_held = target_held,
        .fire_pressed = fire_pressed,
    };
}
} // namespace

int main()
{
    using omega::gameplay::AdvanceDiagnosticTargetFire;
    using omega::gameplay::kDiagnosticAimExtent;
    using omega::gameplay::kProjectDiagnosticAimTarget;

    static_assert(kDiagnosticAimExtent == 65'536U);
    static_assert(kProjectDiagnosticAimTarget ==
                  DiagnosticAimTargetQ16{
                      .left = 47'104U,
                      .top = 30'720U,
                      .right = 51'200U,
                      .bottom = 34'816U,
                  });
    static_assert(std::is_aggregate_v<DiagnosticAimPointQ16>);
    static_assert(std::is_standard_layout_v<DiagnosticAimPointQ16>);
    static_assert(std::is_trivially_copyable_v<DiagnosticAimPointQ16>);
    static_assert(std::is_aggregate_v<DiagnosticAimTargetQ16>);
    static_assert(std::is_standard_layout_v<DiagnosticAimTargetQ16>);
    static_assert(std::is_trivially_copyable_v<DiagnosticAimTargetQ16>);
    static_assert(std::is_aggregate_v<DiagnosticTargetFireInput>);
    static_assert(std::is_aggregate_v<DiagnosticTargetFireState>);
    static_assert(std::is_standard_layout_v<DiagnosticTargetFireState>);
    static_assert(std::is_trivially_copyable_v<DiagnosticTargetFireState>);
    static_assert(std::is_aggregate_v<DiagnosticTargetFireStep>);
    static_assert(std::is_standard_layout_v<DiagnosticTargetFireStep>);
    static_assert(std::is_trivially_copyable_v<DiagnosticTargetFireStep>);
    static_assert(std::is_same_v<
        std::underlying_type_t<DiagnosticTargetFireError>, std::uint8_t>);
    static_assert(std::is_same_v<decltype(AdvanceDiagnosticTargetFire(
                                     DiagnosticAimTargetQ16{},
                                     DiagnosticTargetFireState{},
                                     DiagnosticTargetFireInput{})),
        StepResult>);
    static_assert(noexcept(AdvanceDiagnosticTargetFire(
        DiagnosticAimTargetQ16{}, DiagnosticTargetFireState{},
        DiagnosticTargetFireInput{})));

    constexpr DiagnosticTargetFireState caller_state{
        .acquired = true,
        .target_complete = true,
    };
    constexpr DiagnosticTargetFireInput caller_input{
        .pointer = DiagnosticAimPointQ16{
            .x = kDiagnosticAimExtent + 1U,
            .y = kDiagnosticAimExtent + 1U,
        },
        .enabled = false,
        .target_held = true,
        .fire_pressed = true,
    };
    constexpr std::array invalid_targets{
        DiagnosticAimTargetQ16{.left = 2U, .top = 0U, .right = 1U, .bottom = 0U},
        DiagnosticAimTargetQ16{.left = 0U, .top = 0U,
            .right = kDiagnosticAimExtent + 1U, .bottom = 0U},
        DiagnosticAimTargetQ16{.left = 0U, .top = 2U, .right = 0U, .bottom = 1U},
        DiagnosticAimTargetQ16{.left = 0U, .top = 0U, .right = 0U,
            .bottom = kDiagnosticAimExtent + 1U},
    };
    for (const DiagnosticAimTargetQ16 target : invalid_targets)
    {
        const StepResult rejected = AdvanceDiagnosticTargetFire(
            target, caller_state, caller_input);
        Check(!rejected &&
                  rejected.error() == DiagnosticTargetFireError::InvalidTarget,
            "invalid target validation has priority over disabled, complete, and invalid pointer inputs");
    }
    Check(caller_state == DiagnosticTargetFireState{.acquired = true,
                              .target_complete = true} &&
              caller_input == DiagnosticTargetFireInput{
                                  .pointer = DiagnosticAimPointQ16{
                                      .x = kDiagnosticAimExtent + 1U,
                                      .y = kDiagnosticAimExtent + 1U,
                                  },
                                  .enabled = false,
                                  .target_held = true,
                                  .fire_pressed = true,
                              },
        "invalid target rejection leaves caller-owned values unchanged");

    const DiagnosticTargetFireInput invalid_x = EnabledInput(
        DiagnosticAimPointQ16{.x = kDiagnosticAimExtent + 1U, .y = 0U});
    const DiagnosticTargetFireInput invalid_y = EnabledInput(
        DiagnosticAimPointQ16{.x = 0U, .y = kDiagnosticAimExtent + 1U});
    DiagnosticTargetFireInput invalid_disabled = invalid_x;
    invalid_disabled.enabled = false;
    const StepResult rejected_x = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, invalid_x);
    const StepResult rejected_y = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, invalid_y);
    const StepResult rejected_disabled = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, invalid_disabled);
    const StepResult rejected_complete = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget,
        {.acquired = true, .target_complete = true}, invalid_y);
    Check(!rejected_x &&
              rejected_x.error() == DiagnosticTargetFireError::PointerOutOfRange &&
              !rejected_y &&
              rejected_y.error() == DiagnosticTargetFireError::PointerOutOfRange &&
              !rejected_disabled &&
              rejected_disabled.error() ==
                  DiagnosticTargetFireError::PointerOutOfRange &&
              !rejected_complete &&
              rejected_complete.error() ==
                  DiagnosticTargetFireError::PointerOutOfRange,
        "pointer validation precedes disabled and completed-state handling");

    const StepResult disabled = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget,
        {.acquired = true, .target_complete = false},
        DiagnosticTargetFireInput{
            .pointer = DiagnosticAimPointQ16{.x = 49'152U, .y = 32'768U},
            .enabled = false,
            .target_held = true,
            .fire_pressed = true,
        });
    Check(disabled &&
              disabled->state == DiagnosticTargetFireState{} &&
              !disabled->attempted_now && !disabled->hit_now,
        "disabled input is inert and clears prior acquisition");

    const StepResult completed = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget,
        {.acquired = true, .target_complete = true},
        EnabledInput(DiagnosticAimPointQ16{.x = 49'152U, .y = 32'768U},
            true, true));
    Check(completed &&
              completed->state == DiagnosticTargetFireState{
                                      .acquired = false,
                                      .target_complete = true,
                                  } &&
              !completed->attempted_now && !completed->hit_now,
        "completed target is inert, remains complete, and clears acquisition");

    constexpr std::array inclusive_points{
        DiagnosticAimPointQ16{.x = 47'104U, .y = 30'720U},
        DiagnosticAimPointQ16{.x = 51'200U, .y = 30'720U},
        DiagnosticAimPointQ16{.x = 47'104U, .y = 34'816U},
        DiagnosticAimPointQ16{.x = 51'200U, .y = 34'816U},
    };
    for (const DiagnosticAimPointQ16 point : inclusive_points)
    {
        const StepResult acquired = AdvanceDiagnosticTargetFire(
            kProjectDiagnosticAimTarget, {}, EnabledInput(point, true));
        const StepResult hit = AdvanceDiagnosticTargetFire(
            kProjectDiagnosticAimTarget, {}, EnabledInput(point, true, true));
        Check(acquired && acquired->state ==
                              DiagnosticTargetFireState{
                                  .acquired = true,
                                  .target_complete = false,
                              } &&
                  !acquired->attempted_now && !acquired->hit_now && hit &&
                  hit->state == DiagnosticTargetFireState{
                                    .acquired = false,
                                    .target_complete = true,
                                } &&
                  hit->attempted_now && hit->hit_now,
            "every fixed target corner is inclusively acquirable and hittable");
    }

    constexpr std::array outside_points{
        DiagnosticAimPointQ16{.x = 47'103U, .y = 32'768U},
        DiagnosticAimPointQ16{.x = 51'201U, .y = 32'768U},
        DiagnosticAimPointQ16{.x = 49'152U, .y = 30'719U},
        DiagnosticAimPointQ16{.x = 49'152U, .y = 34'817U},
    };
    for (const DiagnosticAimPointQ16 point : outside_points)
    {
        const StepResult miss = AdvanceDiagnosticTargetFire(
            kProjectDiagnosticAimTarget, {}, EnabledInput(point, true, true));
        Check(miss && miss->state == DiagnosticTargetFireState{} &&
                  miss->attempted_now && !miss->hit_now,
            "a targeted fire edge just beyond each target face attempts and misses");
    }

    const DiagnosticAimPointQ16 center{.x = 49'152U, .y = 32'768U};
    const StepResult acquired = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, EnabledInput(center, true));
    const StepResult released = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget,
        acquired ? acquired->state : DiagnosticTargetFireState{},
        EnabledInput(center));
    Check(acquired && acquired->state.acquired && released &&
              released->state == DiagnosticTargetFireState{},
        "releasing target clears acquisition without completing the target");

    const StepResult missing_pointer = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, EnabledInput(std::nullopt, true, true));
    const StepResult untargeted_fire = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, EnabledInput(center, false, true));
    Check(missing_pointer && missing_pointer->state == DiagnosticTargetFireState{} &&
              missing_pointer->attempted_now && !missing_pointer->hit_now,
        "an enabled fire edge with missing pointer attempts and misses");
    Check(untargeted_fire && untargeted_fire->state == DiagnosticTargetFireState{} &&
              untargeted_fire->attempted_now && !untargeted_fire->hit_now,
        "an enabled fire edge without held targeting attempts and misses");

    constexpr DiagnosticAimTargetQ16 full_extent_target{
        .left = 0U,
        .top = 0U,
        .right = kDiagnosticAimExtent,
        .bottom = kDiagnosticAimExtent,
    };
    const StepResult origin_acquired = AdvanceDiagnosticTargetFire(
        full_extent_target, {},
        EnabledInput(DiagnosticAimPointQ16{.x = 0U, .y = 0U}, true));
    const StepResult maximum_acquired = AdvanceDiagnosticTargetFire(
        full_extent_target, {},
        EnabledInput(DiagnosticAimPointQ16{
            .x = kDiagnosticAimExtent,
            .y = kDiagnosticAimExtent,
        }, true));
    Check(origin_acquired && origin_acquired->state.acquired &&
              maximum_acquired && maximum_acquired->state.acquired,
        "the complete normalized origin and maximum boundaries are valid and inclusive");

    constexpr DiagnosticAimTargetQ16 single_point_target{
        .left = 7U,
        .top = 11U,
        .right = 7U,
        .bottom = 11U,
    };
    const StepResult single_point_hit = AdvanceDiagnosticTargetFire(
        single_point_target, {},
        EnabledInput(DiagnosticAimPointQ16{.x = 7U, .y = 11U}, true, true));
    Check(single_point_hit && single_point_hit->hit_now &&
              single_point_hit->state.target_complete,
        "equal inclusive bounds form a valid one-point target");

    const StepResult hit = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, {}, EnabledInput(center, true, true));
    const StepResult latched = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget,
        hit ? hit->state : DiagnosticTargetFireState{},
        EnabledInput(center, true, true));
    Check(hit && hit->attempted_now && hit->hit_now &&
              hit->state == DiagnosticTargetFireState{
                                .acquired = false,
                                .target_complete = true,
                            } &&
              latched && latched->state == hit->state &&
              !latched->attempted_now && !latched->hit_now,
        "a hit completes once and every later enabled fire edge is inert");

    const DiagnosticTargetFireState deterministic_prior{
        .acquired = true,
        .target_complete = false,
    };
    const DiagnosticTargetFireInput deterministic_input =
        EnabledInput(center, true, true);
    const StepResult deterministic_first = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, deterministic_prior, deterministic_input);
    const StepResult deterministic_second = AdvanceDiagnosticTargetFire(
        kProjectDiagnosticAimTarget, deterministic_prior, deterministic_input);
    Check(deterministic_first && deterministic_second &&
              *deterministic_first == *deterministic_second,
        "identical owned inputs produce identical successor values");

    if (failures != 0)
    {
        std::cerr << failures << " diagnostic target fire test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "omega_diagnostic_target_fire_tests: all checks passed\n";
    return EXIT_SUCCESS;
}
