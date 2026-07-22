#include "omega/gameplay/diagnostic_mission_lifecycle.h"

namespace omega::gameplay
{
namespace
{
[[nodiscard]] constexpr bool IsValidStatus(
    const DiagnosticMissionStatus status) noexcept
{
    switch (status)
    {
    case DiagnosticMissionStatus::Ready:
    case DiagnosticMissionStatus::Active:
    case DiagnosticMissionStatus::Succeeded:
    case DiagnosticMissionStatus::Failed:
        return true;
    }
    return false;
}

[[nodiscard]] constexpr bool IsValidEvent(
    const DiagnosticMissionEvent event) noexcept
{
    switch (event)
    {
    case DiagnosticMissionEvent::None:
    case DiagnosticMissionEvent::Deploy:
    case DiagnosticMissionEvent::Complete:
    case DiagnosticMissionEvent::Abort:
        return true;
    }
    return false;
}
} // namespace

std::expected<DiagnosticMissionLifecycleStep, DiagnosticMissionLifecycleError>
AdvanceDiagnosticMissionLifecycle(const DiagnosticMissionLifecycleState prior_state,
    const DiagnosticMissionEvent event) noexcept
{
    if (!IsValidStatus(prior_state.status))
        return std::unexpected(DiagnosticMissionLifecycleError::InvalidState);
    if (!IsValidEvent(event))
        return std::unexpected(DiagnosticMissionLifecycleError::InvalidEvent);

    if (event == DiagnosticMissionEvent::None)
    {
        return DiagnosticMissionLifecycleStep{
            .state = prior_state,
        };
    }

    if (event == DiagnosticMissionEvent::Deploy)
    {
        if (prior_state.status == DiagnosticMissionStatus::Active)
            return std::unexpected(DiagnosticMissionLifecycleError::InvalidTransition);

        return DiagnosticMissionLifecycleStep{
            .state = {.status = DiagnosticMissionStatus::Active},
            .reset_gameplay_now = true,
        };
    }

    if (prior_state.status != DiagnosticMissionStatus::Active)
        return std::unexpected(DiagnosticMissionLifecycleError::InvalidTransition);

    if (event == DiagnosticMissionEvent::Complete)
    {
        return DiagnosticMissionLifecycleStep{
            .state = {.status = DiagnosticMissionStatus::Succeeded},
            .enter_briefing_now = true,
        };
    }

    return DiagnosticMissionLifecycleStep{
        .state = {.status = DiagnosticMissionStatus::Failed},
        .enter_briefing_now = true,
    };
}
} // namespace omega::gameplay
