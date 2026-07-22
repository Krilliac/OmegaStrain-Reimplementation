#pragma once

#include <cstdint>
#include <expected>

namespace omega::gameplay
{
// Project-owned diagnostic mission states. They describe only the native
// shell's deterministic deployment loop and do not claim a recovered retail
// mission-state representation or transition contract.
enum class DiagnosticMissionStatus : std::uint8_t
{
    Ready,
    Active,
    Succeeded,
    Failed,
};

enum class DiagnosticMissionEvent : std::uint8_t
{
    None,
    Deploy,
    Complete,
    Abort,
};

struct DiagnosticMissionLifecycleState
{
    DiagnosticMissionStatus status = DiagnosticMissionStatus::Ready;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticMissionLifecycleState&,
        const DiagnosticMissionLifecycleState&) noexcept = default;
};

struct DiagnosticMissionLifecycleStep
{
    DiagnosticMissionLifecycleState state{};
    bool reset_gameplay_now = false;
    bool enter_briefing_now = false;

    [[nodiscard]] friend constexpr bool operator==(
        const DiagnosticMissionLifecycleStep&,
        const DiagnosticMissionLifecycleStep&) noexcept = default;
};

enum class DiagnosticMissionLifecycleError : std::uint8_t
{
    InvalidState,
    InvalidEvent,
    InvalidTransition,
};

// [any thread; reentrant] Advances one project-owned diagnostic mission from
// owned values. Invalid state, event, or state/event combinations are rejected
// before a successor is formed. The reducer allocates nothing and retains no
// state or references.
[[nodiscard]] std::expected<DiagnosticMissionLifecycleStep,
    DiagnosticMissionLifecycleError>
AdvanceDiagnosticMissionLifecycle(DiagnosticMissionLifecycleState prior_state,
    DiagnosticMissionEvent event) noexcept;
} // namespace omega::gameplay
