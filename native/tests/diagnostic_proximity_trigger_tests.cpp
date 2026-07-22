#include "omega/gameplay/diagnostic_proximity_trigger.h"

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace
{
using omega::gameplay::DiagnosticProximityTransition;
using omega::gameplay::DiagnosticProximityTriggerState;
using omega::gameplay::DiagnosticProximityTriggerStep;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;

    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

[[nodiscard]] bool SameState(const DiagnosticProximityTriggerState left,
    const DiagnosticProximityTriggerState right) noexcept
{
    return left.inside == right.inside &&
           left.objective_complete == right.objective_complete;
}

[[nodiscard]] bool SameStep(const DiagnosticProximityTriggerStep left,
    const DiagnosticProximityTriggerStep right) noexcept
{
    return SameState(left.state, right.state) &&
           left.transition == right.transition &&
           left.activated_now == right.activated_now;
}
} // namespace

int main()
{
    using omega::gameplay::AdvanceDiagnosticProximityTrigger;
    using omega::gameplay::DiagnosticProximityTriggerError;
    using omega::gameplay::DiagnosticProximityVolumeXZ;
    using omega::gameplay::kProjectDiagnosticObjectiveVolume;
    using omega::simulation::Position3;

    using StepResult = std::expected<DiagnosticProximityTriggerStep,
        DiagnosticProximityTriggerError>;
    static_assert(std::is_aggregate_v<DiagnosticProximityVolumeXZ>);
    static_assert(std::is_standard_layout_v<DiagnosticProximityVolumeXZ>);
    static_assert(std::is_trivially_copyable_v<DiagnosticProximityVolumeXZ>);
    static_assert(std::is_aggregate_v<DiagnosticProximityTriggerState>);
    static_assert(std::is_standard_layout_v<DiagnosticProximityTriggerState>);
    static_assert(std::is_trivially_copyable_v<DiagnosticProximityTriggerState>);
    static_assert(std::is_aggregate_v<DiagnosticProximityTriggerStep>);
    static_assert(std::is_standard_layout_v<DiagnosticProximityTriggerStep>);
    static_assert(std::is_trivially_copyable_v<DiagnosticProximityTriggerStep>);
    static_assert(std::is_same_v<
        std::underlying_type_t<DiagnosticProximityTransition>, std::uint8_t>);
    static_assert(std::is_same_v<
        std::underlying_type_t<DiagnosticProximityTriggerError>, std::uint8_t>);
    static_assert(std::is_same_v<decltype(AdvanceDiagnosticProximityTrigger(
                                     DiagnosticProximityVolumeXZ{},
                                     DiagnosticProximityTriggerState{},
                                     Position3{})),
        StepResult>);
    static_assert(noexcept(AdvanceDiagnosticProximityTrigger(
        DiagnosticProximityVolumeXZ{}, DiagnosticProximityTriggerState{},
        Position3{})));
    static_assert(kProjectDiagnosticObjectiveVolume.min_x == 3);
    static_assert(kProjectDiagnosticObjectiveVolume.max_x == 5);
    static_assert(kProjectDiagnosticObjectiveVolume.min_z == -1);
    static_assert(kProjectDiagnosticObjectiveVolume.max_z == 1);

    const DiagnosticProximityVolumeXZ invalid_x{
        .min_x = 1,
        .max_x = 0,
        .min_z = -1,
        .max_z = 1,
    };
    const DiagnosticProximityVolumeXZ invalid_z{
        .min_x = 0,
        .max_x = 1,
        .min_z = 1,
        .max_z = 0,
    };
    const DiagnosticProximityTriggerState invalid_prior{
        .inside = true,
        .objective_complete = true,
    };
    const Position3 invalid_position{.x = 7, .y = -9, .z = 11};
    const StepResult rejected_x = AdvanceDiagnosticProximityTrigger(
        invalid_x, invalid_prior, invalid_position);
    const StepResult rejected_z = AdvanceDiagnosticProximityTrigger(
        invalid_z, invalid_prior, invalid_position);
    Check(!rejected_x &&
              rejected_x.error() ==
                  DiagnosticProximityTriggerError::InvalidVolume,
        "reversed X bounds report InvalidVolume");
    Check(!rejected_z &&
              rejected_z.error() ==
                  DiagnosticProximityTriggerError::InvalidVolume,
        "reversed Z bounds report InvalidVolume");
    Check(invalid_x.min_x == 1 && invalid_x.max_x == 0 &&
              invalid_x.min_z == -1 && invalid_x.max_z == 1 &&
              invalid_z.min_x == 0 && invalid_z.max_x == 1 &&
              invalid_z.min_z == 1 && invalid_z.max_z == 0 &&
              SameState(invalid_prior,
                  DiagnosticProximityTriggerState{
                      .inside = true,
                      .objective_complete = true,
                  }) &&
              invalid_position == Position3{.x = 7, .y = -9, .z = 11},
        "invalid-volume rejection leaves every caller-owned input unchanged");

    constexpr Position3 inclusive_points[] = {
        {.x = 3, .y = 0, .z = 0},
        {.x = 5, .y = 0, .z = 0},
        {.x = 4, .y = 0, .z = -1},
        {.x = 4, .y = 0, .z = 1},
        {.x = 3, .y = 0, .z = -1},
        {.x = 3, .y = 0, .z = 1},
        {.x = 5, .y = 0, .z = -1},
        {.x = 5, .y = 0, .z = 1},
    };
    for (const Position3 point : inclusive_points)
    {
        const StepResult result = AdvanceDiagnosticProximityTrigger(
            kProjectDiagnosticObjectiveVolume, {}, point);
        Check(result && result->state.inside &&
                  result->state.objective_complete &&
                  result->transition ==
                      DiagnosticProximityTransition::Entered &&
                  result->activated_now,
            "every X/Z face and corner is inclusively inside");
    }

    constexpr Position3 outside_points[] = {
        {.x = 2, .y = 0, .z = 0},
        {.x = 6, .y = 0, .z = 0},
        {.x = 4, .y = 0, .z = -2},
        {.x = 4, .y = 0, .z = 2},
    };
    for (const Position3 point : outside_points)
    {
        const StepResult result = AdvanceDiagnosticProximityTrigger(
            kProjectDiagnosticObjectiveVolume, {}, point);
        Check(result && !result->state.inside &&
                  !result->state.objective_complete &&
                  result->transition ==
                      DiagnosticProximityTransition::Outside &&
                  !result->activated_now,
            "points beyond each X/Z face remain outside");
    }

    const Position3 inside_position{.x = 4, .y = 0, .z = 0};
    const Position3 outside_position{.x = 2, .y = 0, .z = 0};
    const StepResult entered = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume, {}, inside_position);
    Check(entered &&
              SameStep(*entered,
                  DiagnosticProximityTriggerStep{
                      .state = {.inside = true, .objective_complete = true},
                      .transition = DiagnosticProximityTransition::Entered,
                      .activated_now = true,
                  }),
        "an outside-to-inside step enters, completes, and activates");

    const StepResult inside = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = true, .objective_complete = true}, inside_position);
    Check(inside &&
              SameStep(*inside,
                  DiagnosticProximityTriggerStep{
                      .state = {.inside = true, .objective_complete = true},
                      .transition = DiagnosticProximityTransition::Inside,
                      .activated_now = false,
                  }),
        "an inside-to-inside step remains inside without reactivation");

    const StepResult exited = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = true, .objective_complete = true}, outside_position);
    Check(exited &&
              SameStep(*exited,
                  DiagnosticProximityTriggerStep{
                      .state = {.inside = false, .objective_complete = true},
                      .transition = DiagnosticProximityTransition::Exited,
                      .activated_now = false,
                  }),
        "an inside-to-outside step exits while preserving completion");

    const StepResult latched_outside = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = false, .objective_complete = true}, outside_position);
    const StepResult latched_reentry = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = false, .objective_complete = true}, inside_position);
    Check(latched_outside && !latched_outside->state.inside &&
              latched_outside->state.objective_complete &&
              latched_outside->transition ==
                  DiagnosticProximityTransition::Outside &&
              !latched_outside->activated_now && latched_reentry &&
              latched_reentry->state.inside &&
              latched_reentry->state.objective_complete &&
              latched_reentry->transition ==
                  DiagnosticProximityTransition::Entered &&
              !latched_reentry->activated_now,
        "completion remains latched after exit and re-entry never reactivates it");

    const StepResult inconsistent_exit = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = true, .objective_complete = false}, outside_position);
    const StepResult inconsistent_inside = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = true, .objective_complete = false}, inside_position);
    Check(inconsistent_exit && !inconsistent_exit->state.inside &&
              !inconsistent_exit->state.objective_complete &&
              inconsistent_exit->transition ==
                  DiagnosticProximityTransition::Exited &&
              !inconsistent_exit->activated_now,
        "representable inside-but-incomplete state exits without inventing completion");
    Check(inconsistent_inside && inconsistent_inside->state.inside &&
              inconsistent_inside->state.objective_complete &&
              inconsistent_inside->transition ==
                  DiagnosticProximityTransition::Inside &&
              inconsistent_inside->activated_now,
        "representable inside-but-incomplete state activates from current occupancy");

    const StepResult minimum_y = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume, {},
        {.x = 4, .y = std::numeric_limits<std::int64_t>::min(), .z = 0});
    const StepResult maximum_y = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume, {},
        {.x = 4, .y = std::numeric_limits<std::int64_t>::max(), .z = 0});
    Check(minimum_y && maximum_y && SameStep(*minimum_y, *maximum_y),
        "Y is ignored across its complete signed range");

    constexpr DiagnosticProximityVolumeXZ extreme_volume{
        .min_x = std::numeric_limits<std::int64_t>::min(),
        .max_x = std::numeric_limits<std::int64_t>::min(),
        .min_z = std::numeric_limits<std::int64_t>::max(),
        .max_z = std::numeric_limits<std::int64_t>::max(),
    };
    const StepResult extreme_inside = AdvanceDiagnosticProximityTrigger(
        extreme_volume, {},
        {.x = std::numeric_limits<std::int64_t>::min(),
            .y = 0,
            .z = std::numeric_limits<std::int64_t>::max()});
    const StepResult extreme_outside = AdvanceDiagnosticProximityTrigger(
        extreme_volume, {},
        {.x = std::numeric_limits<std::int64_t>::max(),
            .y = 0,
            .z = std::numeric_limits<std::int64_t>::min()});
    Check(extreme_inside && extreme_inside->state.inside &&
              extreme_inside->activated_now && extreme_outside &&
              !extreme_outside->state.inside &&
              extreme_outside->transition ==
                  DiagnosticProximityTransition::Outside,
        "inclusive comparisons handle signed coordinate extremes without arithmetic");

    const StepResult deterministic_first = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = false, .objective_complete = false}, inside_position);
    const StepResult deterministic_second = AdvanceDiagnosticProximityTrigger(
        kProjectDiagnosticObjectiveVolume,
        {.inside = false, .objective_complete = false}, inside_position);
    Check(deterministic_first && deterministic_second &&
              SameStep(*deterministic_first, *deterministic_second),
        "two runs with identical inputs produce identical successor values");

    if (failures != 0)
    {
        std::cerr << failures << " diagnostic proximity trigger test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "omega_diagnostic_proximity_trigger_tests: all checks passed\n";
    return EXIT_SUCCESS;
}
