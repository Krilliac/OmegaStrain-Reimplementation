#include "omega/gameplay/diagnostic_mission_lifecycle.h"

#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace
{
using omega::gameplay::DiagnosticMissionEvent;
using omega::gameplay::DiagnosticMissionLifecycleError;
using omega::gameplay::DiagnosticMissionLifecycleState;
using omega::gameplay::DiagnosticMissionLifecycleStep;
using omega::gameplay::DiagnosticMissionStatus;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;

    ++failures;
    std::cerr << "FAILED: " << message << '\n';
}

struct TransitionCase
{
    DiagnosticMissionStatus prior = DiagnosticMissionStatus::Ready;
    DiagnosticMissionEvent event = DiagnosticMissionEvent::None;
    bool valid = false;
    DiagnosticMissionStatus successor = DiagnosticMissionStatus::Ready;
    bool reset_gameplay_now = false;
    bool enter_briefing_now = false;
};
} // namespace

int main()
{
    using omega::gameplay::AdvanceDiagnosticMissionLifecycle;

    using StepResult = std::expected<DiagnosticMissionLifecycleStep,
        DiagnosticMissionLifecycleError>;
    static_assert(std::is_same_v<
        std::underlying_type_t<DiagnosticMissionStatus>, std::uint8_t>);
    static_assert(std::is_same_v<
        std::underlying_type_t<DiagnosticMissionEvent>, std::uint8_t>);
    static_assert(std::is_same_v<
        std::underlying_type_t<DiagnosticMissionLifecycleError>, std::uint8_t>);
    static_assert(std::is_aggregate_v<DiagnosticMissionLifecycleState>);
    static_assert(std::is_standard_layout_v<DiagnosticMissionLifecycleState>);
    static_assert(std::is_trivially_copyable_v<DiagnosticMissionLifecycleState>);
    static_assert(std::is_aggregate_v<DiagnosticMissionLifecycleStep>);
    static_assert(std::is_standard_layout_v<DiagnosticMissionLifecycleStep>);
    static_assert(std::is_trivially_copyable_v<DiagnosticMissionLifecycleStep>);
    static_assert(std::is_same_v<decltype(AdvanceDiagnosticMissionLifecycle(
                                     DiagnosticMissionLifecycleState{},
                                     DiagnosticMissionEvent::None)),
        StepResult>);
    static_assert(noexcept(AdvanceDiagnosticMissionLifecycle(
        DiagnosticMissionLifecycleState{}, DiagnosticMissionEvent::None)));

    constexpr TransitionCase cases[]{
        {DiagnosticMissionStatus::Ready, DiagnosticMissionEvent::None, true,
            DiagnosticMissionStatus::Ready},
        {DiagnosticMissionStatus::Ready, DiagnosticMissionEvent::Deploy, true,
            DiagnosticMissionStatus::Active, true, false},
        {DiagnosticMissionStatus::Ready, DiagnosticMissionEvent::Complete, false},
        {DiagnosticMissionStatus::Ready, DiagnosticMissionEvent::Abort, false},
        {DiagnosticMissionStatus::Active, DiagnosticMissionEvent::None, true,
            DiagnosticMissionStatus::Active},
        {DiagnosticMissionStatus::Active, DiagnosticMissionEvent::Deploy, false},
        {DiagnosticMissionStatus::Active, DiagnosticMissionEvent::Complete, true,
            DiagnosticMissionStatus::Succeeded, false, true},
        {DiagnosticMissionStatus::Active, DiagnosticMissionEvent::Abort, true,
            DiagnosticMissionStatus::Failed, false, true},
        {DiagnosticMissionStatus::Succeeded, DiagnosticMissionEvent::None, true,
            DiagnosticMissionStatus::Succeeded},
        {DiagnosticMissionStatus::Succeeded, DiagnosticMissionEvent::Deploy, true,
            DiagnosticMissionStatus::Active, true, false},
        {DiagnosticMissionStatus::Succeeded, DiagnosticMissionEvent::Complete, false},
        {DiagnosticMissionStatus::Succeeded, DiagnosticMissionEvent::Abort, false},
        {DiagnosticMissionStatus::Failed, DiagnosticMissionEvent::None, true,
            DiagnosticMissionStatus::Failed},
        {DiagnosticMissionStatus::Failed, DiagnosticMissionEvent::Deploy, true,
            DiagnosticMissionStatus::Active, true, false},
        {DiagnosticMissionStatus::Failed, DiagnosticMissionEvent::Complete, false},
        {DiagnosticMissionStatus::Failed, DiagnosticMissionEvent::Abort, false},
    };

    for (const TransitionCase test : cases)
    {
        const DiagnosticMissionLifecycleState prior{.status = test.prior};
        const StepResult result =
            AdvanceDiagnosticMissionLifecycle(prior, test.event);
        if (test.valid)
        {
            Check(result &&
                      *result == DiagnosticMissionLifecycleStep{
                                     .state = {.status = test.successor},
                                     .reset_gameplay_now = test.reset_gameplay_now,
                                     .enter_briefing_now = test.enter_briefing_now,
                                 },
                "every valid status/event pair produces its exact successor and effects");
        }
        else
        {
            Check(!result && result.error() ==
                                 DiagnosticMissionLifecycleError::InvalidTransition,
                "every unsupported valid status/event pair reports InvalidTransition");
        }
        Check(prior.status == test.prior,
            "every transition leaves its caller-owned prior state unchanged");
    }

    constexpr DiagnosticMissionStatus invalid_statuses[]{
        static_cast<DiagnosticMissionStatus>(4U),
        static_cast<DiagnosticMissionStatus>(0xFFU),
    };
    constexpr DiagnosticMissionEvent all_events[]{
        DiagnosticMissionEvent::None,
        DiagnosticMissionEvent::Deploy,
        DiagnosticMissionEvent::Complete,
        DiagnosticMissionEvent::Abort,
        static_cast<DiagnosticMissionEvent>(4U),
        static_cast<DiagnosticMissionEvent>(0xFFU),
    };
    for (const DiagnosticMissionStatus status : invalid_statuses)
    {
        for (const DiagnosticMissionEvent event : all_events)
        {
            const DiagnosticMissionLifecycleState invalid{.status = status};
            const StepResult result = AdvanceDiagnosticMissionLifecycle(invalid, event);
            Check(!result &&
                      result.error() == DiagnosticMissionLifecycleError::InvalidState &&
                      invalid.status == status,
                "invalid states have fixed priority and remain caller-owned");
        }
    }

    constexpr DiagnosticMissionEvent invalid_events[]{
        static_cast<DiagnosticMissionEvent>(4U),
        static_cast<DiagnosticMissionEvent>(0xFFU),
    };
    constexpr DiagnosticMissionStatus all_statuses[]{
        DiagnosticMissionStatus::Ready,
        DiagnosticMissionStatus::Active,
        DiagnosticMissionStatus::Succeeded,
        DiagnosticMissionStatus::Failed,
    };
    for (const DiagnosticMissionStatus status : all_statuses)
    {
        for (const DiagnosticMissionEvent event : invalid_events)
        {
            const DiagnosticMissionLifecycleState prior{.status = status};
            const StepResult result = AdvanceDiagnosticMissionLifecycle(prior, event);
            Check(!result &&
                      result.error() == DiagnosticMissionLifecycleError::InvalidEvent &&
                      prior.status == status,
                "invalid events are rejected after valid-state validation without mutation");
        }
    }

    const StepResult deterministic_first = AdvanceDiagnosticMissionLifecycle(
        {.status = DiagnosticMissionStatus::Active},
        DiagnosticMissionEvent::Complete);
    const StepResult deterministic_second = AdvanceDiagnosticMissionLifecycle(
        {.status = DiagnosticMissionStatus::Active},
        DiagnosticMissionEvent::Complete);
    Check(deterministic_first && deterministic_second &&
              *deterministic_first == *deterministic_second,
        "identical owned inputs produce identical successor values");

    if (failures != 0)
    {
        std::cerr << failures << " diagnostic mission lifecycle test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "omega_diagnostic_mission_lifecycle_tests: all checks passed\n";
    return EXIT_SUCCESS;
}
