#pragma once

#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/run_capture_session.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace omega::app
{
struct RunResult
{
    int rendered_frames = 0;
    std::uint64_t planned_simulation_steps = 0;
    std::uint64_t executed_simulation_steps = 0;
    std::uint64_t input_frames = 0;
    std::uint64_t clamped_frame_count = 0;
    std::uint64_t dropped_time_frame_count = 0;
    std::uint64_t audio_callback_count = 0;
    std::uint64_t audio_frames_provided = 0;
    bool quit_requested = false;

    friend constexpr bool operator==(const RunResult&, const RunResult&) noexcept = default;
};

enum class RunCaptureCompletion
{
    Inert,
    FrameLimitReached,
    QuitRequested,
    OperationalFailure,
    CaptureFailure,
};

[[nodiscard]] constexpr std::string_view RunCaptureCompletionName(
    const RunCaptureCompletion completion) noexcept
{
    switch (completion)
    {
    case RunCaptureCompletion::Inert:
        return "inert";
    case RunCaptureCompletion::FrameLimitReached:
        return "frame-limit-reached";
    case RunCaptureCompletion::QuitRequested:
        return "quit-requested";
    case RunCaptureCompletion::OperationalFailure:
        return "operational-failure";
    case RunCaptureCompletion::CaptureFailure:
        return "capture-failure";
    }
    return "unknown";
}

namespace detail
{
struct FiniteRunCapturePlan
{
    std::size_t requested_frames = 0U;
    runtime::RunCaptureSessionConfig session{};

    friend constexpr bool operator==(const FiniteRunCapturePlan& left,
        const FiniteRunCapturePlan& right) noexcept
    {
        return left.requested_frames == right.requested_frames &&
               left.session.maximum_frames == right.session.maximum_frames &&
               left.session.first_frame_index == right.session.first_frame_index;
    }
};

enum class FiniteRunCapturePlanError
{
    NegativeFrameLimit,
    FrameLimitExceeded,
    FrameRangeExhausted,
};

[[nodiscard]] constexpr std::string_view FiniteRunCapturePlanErrorName(
    const FiniteRunCapturePlanError error) noexcept
{
    switch (error)
    {
    case FiniteRunCapturePlanError::NegativeFrameLimit:
        return "negative-frame-limit";
    case FiniteRunCapturePlanError::FrameLimitExceeded:
        return "frame-limit-exceeded";
    case FiniteRunCapturePlanError::FrameRangeExhausted:
        return "frame-range-exhausted";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view FiniteRunCapturePlanErrorMessage(
    const FiniteRunCapturePlanError error) noexcept
{
    switch (error)
    {
    case FiniteRunCapturePlanError::NegativeFrameLimit:
        return "run capture frame limit is negative";
    case FiniteRunCapturePlanError::FrameLimitExceeded:
        return "run capture frame limit exceeds the session maximum";
    case FiniteRunCapturePlanError::FrameRangeExhausted:
        return "run capture frame range exhausts uint64";
    }
    return "run capture plan error is unknown";
}

[[nodiscard]] std::expected<FiniteRunCapturePlan, FiniteRunCapturePlanError>
PlanFiniteRunCapture(int frame_limit, std::uint64_t next_frame_index) noexcept;

[[nodiscard]] std::string FormatRunCaptureSessionError(
    const runtime::RunCaptureSessionError& error);

struct RunCaptureOutcomeTestAccess;
} // namespace detail

class OmegaApp;

// Immutable in-process result of one finite captured host-loop invocation. It owns no app,
// service, clock, SDL object, or borrowed scheduler state. After publication, const reads are
// [any thread; reentrant] when no read races outcome move or destruction. Failure views, trace
// references, and trace pointers are invalidated by outcome move or destruction. This value is
// non-hot-reloadable.
class RunCaptureOutcome final
{
public:
    RunCaptureOutcome(RunCaptureOutcome&& other) noexcept;
    RunCaptureOutcome& operator=(RunCaptureOutcome&&) = delete;
    RunCaptureOutcome(const RunCaptureOutcome&) = delete;
    RunCaptureOutcome& operator=(const RunCaptureOutcome&) = delete;
    ~RunCaptureOutcome() = default;

    [[nodiscard]] std::size_t requested_frame_limit() const noexcept;
    [[nodiscard]] RunResult result() const noexcept;
    [[nodiscard]] RunCaptureCompletion completion() const noexcept;
    [[nodiscard]] runtime::FrameSchedulerState scheduler_state_before() const noexcept;
    [[nodiscard]] runtime::FrameSchedulerState scheduler_state_after() const noexcept;
    // Borrowed until this outcome moves or is destroyed.
    [[nodiscard]] std::optional<std::string_view> failure() const noexcept;
    [[nodiscard]] bool has_traces() const noexcept;

    // Borrowed until this outcome moves or is destroyed.
    [[nodiscard]] const runtime::RunCaptureTracePair* trace_pair() const noexcept;

    // Owned copy independent of subsequent outcome move or destruction.
    [[nodiscard]] std::optional<runtime::RunCaptureTerminalInput> terminal_input() const noexcept;

private:
    friend class OmegaApp;
    friend struct detail::RunCaptureOutcomeTestAccess;

    RunCaptureOutcome(std::size_t requested_frame_limit, RunResult result,
        RunCaptureCompletion completion,
        runtime::FrameSchedulerState scheduler_state_before,
        runtime::FrameSchedulerState scheduler_state_after,
        std::optional<std::string> failure,
        std::optional<runtime::RunCaptureTracePair> trace_pair) noexcept;
    void NormalizeInert() noexcept;

    std::size_t requested_frame_limit_ = 0U;
    RunResult result_{};
    RunCaptureCompletion completion_ = RunCaptureCompletion::Inert;
    runtime::FrameSchedulerState scheduler_state_before_{};
    runtime::FrameSchedulerState scheduler_state_after_{};
    std::optional<std::string> failure_;
    std::optional<runtime::RunCaptureTracePair> trace_pair_;
};
} // namespace omega::app
