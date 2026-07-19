#include "run_capture.h"

#include <limits>
#include <utility>

namespace omega::app
{
namespace detail
{
std::expected<FiniteRunCapturePlan, FiniteRunCapturePlanError>
PlanFiniteRunCapture(const int frame_limit,
    const std::uint64_t next_frame_index) noexcept
{
    if (frame_limit < 0)
        return std::unexpected(FiniteRunCapturePlanError::NegativeFrameLimit);

    const auto requested_frames = static_cast<std::size_t>(frame_limit);
    if (requested_frames > runtime::kMaximumRunCaptureSessionFrames)
        return std::unexpected(FiniteRunCapturePlanError::FrameLimitExceeded);

    if (requested_frames == 0U)
    {
        return FiniteRunCapturePlan{
            .requested_frames = 0U,
            .session = {
                .maximum_frames = 1U,
                .first_frame_index = next_frame_index,
            },
        };
    }

    const auto requested_range = static_cast<std::uint64_t>(requested_frames);
    if (requested_range >
        std::numeric_limits<std::uint64_t>::max() - next_frame_index)
    {
        return std::unexpected(FiniteRunCapturePlanError::FrameRangeExhausted);
    }

    return FiniteRunCapturePlan{
        .requested_frames = requested_frames,
        .session = {
            .maximum_frames = requested_frames,
            .first_frame_index = next_frame_index,
        },
    };
}

std::string FormatRunCaptureSessionError(
    const runtime::RunCaptureSessionError& error)
{
    std::string formatted = "run capture ";
    formatted += runtime::RunCaptureSessionOperationName(error.operation);
    formatted += ": ";
    formatted += runtime::RunCaptureSessionErrorCodeName(error.code);
    if (error.input_trace_error_code)
    {
        formatted += " (input-trace: ";
        formatted += runtime::InputTraceErrorCodeName(*error.input_trace_error_code);
        formatted += ')';
    }
    if (error.scheduler_elapsed_trace_error_code)
    {
        formatted += " (scheduler-elapsed-trace: ";
        formatted += runtime::SchedulerElapsedTraceErrorCodeName(
            *error.scheduler_elapsed_trace_error_code);
        formatted += ')';
    }
    return formatted;
}
} // namespace detail

RunCaptureOutcome::RunCaptureOutcome(const std::size_t requested_frame_limit,
    const RunResult result, const RunCaptureCompletion completion,
    const runtime::FrameSchedulerState scheduler_state_before,
    const runtime::FrameSchedulerState scheduler_state_after,
    std::optional<std::string> failure,
    std::optional<runtime::RunCaptureTracePair> trace_pair) noexcept
    : requested_frame_limit_(requested_frame_limit), result_(result),
      completion_(completion), scheduler_state_before_(scheduler_state_before),
      scheduler_state_after_(scheduler_state_after), failure_(std::move(failure)),
      trace_pair_(std::move(trace_pair))
{
}

RunCaptureOutcome::RunCaptureOutcome(RunCaptureOutcome&& other) noexcept
    : requested_frame_limit_(
          std::exchange(other.requested_frame_limit_, std::size_t{0U})),
      result_(std::exchange(other.result_, RunResult{})),
      completion_(std::exchange(other.completion_, RunCaptureCompletion::Inert)),
      scheduler_state_before_(std::exchange(
          other.scheduler_state_before_, runtime::FrameSchedulerState{})),
      scheduler_state_after_(std::exchange(
          other.scheduler_state_after_, runtime::FrameSchedulerState{})),
      failure_(std::move(other.failure_)), trace_pair_(std::move(other.trace_pair_))
{
    other.NormalizeInert();
}

void RunCaptureOutcome::NormalizeInert() noexcept
{
    requested_frame_limit_ = 0U;
    result_ = {};
    completion_ = RunCaptureCompletion::Inert;
    scheduler_state_before_ = {};
    scheduler_state_after_ = {};
    failure_.reset();
    trace_pair_.reset();
}

std::size_t RunCaptureOutcome::requested_frame_limit() const noexcept
{
    return requested_frame_limit_;
}

RunResult RunCaptureOutcome::result() const noexcept
{
    return result_;
}

RunCaptureCompletion RunCaptureOutcome::completion() const noexcept
{
    return completion_;
}

runtime::FrameSchedulerState RunCaptureOutcome::scheduler_state_before() const noexcept
{
    return scheduler_state_before_;
}

runtime::FrameSchedulerState RunCaptureOutcome::scheduler_state_after() const noexcept
{
    return scheduler_state_after_;
}

std::optional<std::string_view> RunCaptureOutcome::failure() const noexcept
{
    if (!failure_)
        return std::nullopt;
    return std::string_view(*failure_);
}

bool RunCaptureOutcome::has_traces() const noexcept
{
    return trace_pair_.has_value();
}

const runtime::RunCaptureTracePair* RunCaptureOutcome::trace_pair() const noexcept
{
    return trace_pair_ ? &*trace_pair_ : nullptr;
}

std::optional<runtime::RunCaptureTerminalInput>
RunCaptureOutcome::terminal_input() const noexcept
{
    return trace_pair_ ? trace_pair_->terminal_input() : std::nullopt;
}
} // namespace omega::app
