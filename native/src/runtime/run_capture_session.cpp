#include "omega/runtime/run_capture_session.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr RunCaptureSessionError SessionError(
    const RunCaptureSessionOperation operation,
    const RunCaptureSessionErrorCode code) noexcept
{
    return RunCaptureSessionError{
        .operation = operation,
        .code = code,
        .message = RunCaptureSessionErrorMessage(code),
    };
}

[[nodiscard]] constexpr RunCaptureSessionError InputTraceFailure(
    const RunCaptureSessionOperation operation,
    const InputTraceErrorCode leaf_code) noexcept
{
    return RunCaptureSessionError{
        .operation = operation,
        .code = RunCaptureSessionErrorCode::InputTraceFailure,
        .message = RunCaptureSessionErrorMessage(
            RunCaptureSessionErrorCode::InputTraceFailure),
        .input_trace_error_code = leaf_code,
    };
}

[[nodiscard]] constexpr RunCaptureSessionError SchedulerElapsedTraceFailure(
    const RunCaptureSessionOperation operation,
    const SchedulerElapsedTraceErrorCode leaf_code) noexcept
{
    return RunCaptureSessionError{
        .operation = operation,
        .code = RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure,
        .message = RunCaptureSessionErrorMessage(
            RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure),
        .scheduler_elapsed_trace_error_code = leaf_code,
    };
}
} // namespace

RunCaptureTracePair::RunCaptureTracePair(InputTrace&& input_trace,
    SchedulerElapsedTrace&& scheduler_elapsed_trace,
    std::optional<RunCaptureTerminalInput> terminal_input) noexcept
    : input_trace_(std::move(input_trace)),
      scheduler_elapsed_trace_(std::move(scheduler_elapsed_trace)),
      terminal_input_(terminal_input)
{
}

RunCaptureTracePair::RunCaptureTracePair(RunCaptureTracePair&& other) noexcept
    : input_trace_(std::move(other.input_trace_)),
      scheduler_elapsed_trace_(std::move(other.scheduler_elapsed_trace_)),
      terminal_input_(std::exchange(other.terminal_input_, std::nullopt))
{
}

const InputTrace& RunCaptureTracePair::input_trace() const noexcept
{
    return input_trace_;
}

const SchedulerElapsedTrace& RunCaptureTracePair::scheduler_elapsed_trace() const noexcept
{
    return scheduler_elapsed_trace_;
}

std::optional<RunCaptureTerminalInput> RunCaptureTracePair::terminal_input() const noexcept
{
    return terminal_input_;
}

std::expected<RunCaptureSession, RunCaptureSessionError> RunCaptureSession::Create(
    const RunCaptureSessionConfig config,
    const std::span<const std::uint32_t> action_schema)
{
    auto input_result = InputTraceRecorder::Create(
        InputTraceConfig{
            .maximum_frames = config.maximum_frames,
            .first_frame_index = config.first_frame_index,
        },
        action_schema);
    if (!input_result)
    {
        return std::unexpected(InputTraceFailure(
            RunCaptureSessionOperation::CreateInputTrace,
            input_result.error().code));
    }

    auto elapsed_result = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = config.maximum_frames,
            .first_frame_index = config.first_frame_index,
        });
    if (!elapsed_result)
    {
        return std::unexpected(SchedulerElapsedTraceFailure(
            RunCaptureSessionOperation::CreateSchedulerElapsedTrace,
            elapsed_result.error().code));
    }

    RunCaptureSession session(
        config, std::move(*input_result), std::move(*elapsed_result));
    return std::expected<RunCaptureSession, RunCaptureSessionError>{
        std::move(session)};
}

RunCaptureSession::RunCaptureSession(const RunCaptureSessionConfig config,
    InputTraceRecorder&& input_recorder,
    SchedulerElapsedTraceRecorder&& scheduler_elapsed_recorder) noexcept
    : config_(config), phase_(Phase::AwaitInput),
      input_recorder_(std::move(input_recorder)),
      scheduler_elapsed_recorder_(std::move(scheduler_elapsed_recorder))
{
}

RunCaptureSession::RunCaptureSession(RunCaptureSession&& other) noexcept
    : config_(std::exchange(other.config_, RunCaptureSessionConfig{})),
      phase_(std::exchange(other.phase_, Phase::Inert)),
      pending_frame_index_(std::exchange(other.pending_frame_index_, 0U)),
      terminal_input_(std::exchange(other.terminal_input_, std::nullopt)),
      input_recorder_(std::move(other.input_recorder_)),
      scheduler_elapsed_recorder_(std::move(other.scheduler_elapsed_recorder_))
{
    other.NormalizeInert();
}

void RunCaptureSession::NormalizeInert() noexcept
{
    config_ = {};
    phase_ = Phase::Inert;
    pending_frame_index_ = 0U;
    terminal_input_.reset();
}

std::expected<void, RunCaptureSessionError> RunCaptureSession::AppendInput(
    const InputSnapshot& snapshot) noexcept
{
    if (phase_ != Phase::AwaitInput)
    {
        return std::unexpected(SessionError(
            RunCaptureSessionOperation::AppendInput,
            RunCaptureSessionErrorCode::InvalidSessionState));
    }

    const auto append_result = input_recorder_.Append(snapshot);
    if (!append_result)
    {
        return std::unexpected(InputTraceFailure(
            RunCaptureSessionOperation::AppendInput,
            append_result.error().code));
    }

    pending_frame_index_ = snapshot.frame_index();
    phase_ = Phase::AwaitElapsedOrTerminal;
    return {};
}

std::expected<void, RunCaptureSessionError> RunCaptureSession::AppendElapsed(
    const std::chrono::nanoseconds elapsed) noexcept
{
    if (phase_ != Phase::AwaitElapsedOrTerminal)
    {
        return std::unexpected(SessionError(
            RunCaptureSessionOperation::AppendElapsed,
            RunCaptureSessionErrorCode::InvalidSessionState));
    }

    const auto append_result =
        scheduler_elapsed_recorder_.Append(pending_frame_index_, elapsed);
    if (!append_result)
    {
        return std::unexpected(SchedulerElapsedTraceFailure(
            RunCaptureSessionOperation::AppendElapsed,
            append_result.error().code));
    }

    pending_frame_index_ = 0U;
    phase_ = Phase::AwaitInput;
    return {};
}

std::expected<void, RunCaptureSessionError> RunCaptureSession::MarkTerminal(
    const bool host_quit_requested, const bool logical_quit_pressed) noexcept
{
    if (phase_ != Phase::AwaitElapsedOrTerminal)
    {
        return std::unexpected(SessionError(
            RunCaptureSessionOperation::MarkTerminal,
            RunCaptureSessionErrorCode::InvalidSessionState));
    }
    if (!host_quit_requested && !logical_quit_pressed)
    {
        return std::unexpected(SessionError(
            RunCaptureSessionOperation::MarkTerminal,
            RunCaptureSessionErrorCode::InvalidTerminalReason));
    }

    terminal_input_ = RunCaptureTerminalInput{
        .frame_index = pending_frame_index_,
        .host_quit_requested = host_quit_requested,
        .logical_quit_pressed = logical_quit_pressed,
    };
    pending_frame_index_ = 0U;
    phase_ = Phase::Terminal;
    return {};
}

std::expected<RunCaptureTracePair, RunCaptureSessionError>
RunCaptureSession::Finish() && noexcept
{
    if (phase_ != Phase::AwaitInput && phase_ != Phase::Terminal)
    {
        return std::unexpected(SessionError(
            RunCaptureSessionOperation::FinishInputTrace,
            RunCaptureSessionErrorCode::InvalidSessionState));
    }

    const std::optional<RunCaptureTerminalInput> terminal_input = terminal_input_;
    NormalizeInert();

    auto input_result = std::move(input_recorder_).Finish();
    auto elapsed_result = std::move(scheduler_elapsed_recorder_).Finish();

    if (!input_result)
    {
        return std::unexpected(InputTraceFailure(
            RunCaptureSessionOperation::FinishInputTrace,
            input_result.error().code));
    }
    if (!elapsed_result)
    {
        return std::unexpected(SchedulerElapsedTraceFailure(
            RunCaptureSessionOperation::FinishSchedulerElapsedTrace,
            elapsed_result.error().code));
    }

    RunCaptureTracePair pair(
        std::move(*input_result), std::move(*elapsed_result), terminal_input);
    return std::expected<RunCaptureTracePair, RunCaptureSessionError>{
        std::move(pair)};
}
} // namespace omega::runtime
