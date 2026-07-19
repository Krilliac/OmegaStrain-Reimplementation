#include "omega/gameplay/debug_locomotion.h"

#include <cstdlib>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string_view>
#include <type_traits>
#include <utility>

int main()
{
    using omega::gameplay::DebugLocomotionPlanError;
    using omega::gameplay::DigitalMoveCommand;
    using omega::gameplay::PlanDebugLocomotionStep;
    using omega::simulation::Translation3;

    int failures = 0;
    const auto Check = [&failures](
                           const bool condition, const std::string_view message) {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    };

    using PlanResult = std::expected<Translation3, DebugLocomotionPlanError>;
    static_assert(std::is_standard_layout_v<DigitalMoveCommand>);
    static_assert(std::is_trivially_copyable_v<DigitalMoveCommand>);
    static_assert(std::is_same_v<
        decltype(PlanDebugLocomotionStep(DigitalMoveCommand{})), PlanResult>);
    static_assert(noexcept(PlanDebugLocomotionStep(DigitalMoveCommand{})));

    for (std::int8_t lateral = -1; lateral <= 1; ++lateral)
    {
        for (std::int8_t longitudinal = -1; longitudinal <= 1; ++longitudinal)
        {
            const DigitalMoveCommand command{
                .lateral = lateral,
                .longitudinal = longitudinal,
            };
            const PlanResult first = PlanDebugLocomotionStep(command);
            const PlanResult second = PlanDebugLocomotionStep(command);
            Check(first.has_value() && second.has_value(),
                "every valid digital-axis pair produces a translation");
            if (!first || !second)
                continue;

            Check(first->dx == static_cast<std::int64_t>(lateral) &&
                      first->dy == 0 &&
                      first->dz == static_cast<std::int64_t>(longitudinal),
                "the exhaustive 3x3 matrix maps lateral/x, zero/y, and longitudinal/z");
            Check(first->dx == second->dx && first->dy == second->dy &&
                      first->dz == second->dz,
                "identical valid commands produce identical translations");
        }
    }

    constexpr std::int16_t kMinimumInt8 = -128;
    constexpr std::int16_t kMaximumInt8 = 127;
    for (std::int16_t lateral = kMinimumInt8; lateral <= kMaximumInt8; ++lateral)
    {
        for (std::int16_t longitudinal = kMinimumInt8;
             longitudinal <= kMaximumInt8; ++longitudinal)
        {
            const bool valid = lateral >= -1 && lateral <= 1 &&
                               longitudinal >= -1 && longitudinal <= 1;
            if (valid)
                continue;

            const PlanResult rejected = PlanDebugLocomotionStep(DigitalMoveCommand{
                .lateral = static_cast<std::int8_t>(lateral),
                .longitudinal = static_cast<std::int8_t>(longitudinal),
            });
            Check(!rejected &&
                      rejected.error() == DebugLocomotionPlanError::InvalidCommand,
                "every out-of-domain int8 axis pair reports InvalidCommand");
        }
    }

    constexpr DigitalMoveCommand invalid_repeat{
        .lateral = 2,
        .longitudinal = -2,
    };
    const PlanResult first_invalid = PlanDebugLocomotionStep(invalid_repeat);
    const PlanResult second_invalid = PlanDebugLocomotionStep(invalid_repeat);
    Check(!first_invalid && !second_invalid &&
              first_invalid.error() == second_invalid.error(),
        "identical invalid commands produce the same explicit error");

    if (failures != 0)
    {
        std::cerr << failures << " debug locomotion test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "debug locomotion tests passed\n";
    return EXIT_SUCCESS;
}
