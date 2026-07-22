#include "omega/runtime/run_capture_replay.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr RunCaptureReplayError Error(
    const RunCaptureReplayOperation operation,
    const RunCaptureReplayErrorCode code) noexcept
{
    return RunCaptureReplayError{
        .operation = operation,
        .code = code,
        .message = RunCaptureReplayErrorMessage(code),
    };
}

[[nodiscard]] bool IsValidTraceMetadata(
    const detail::RunCaptureReplayTraceMetadata& metadata,
    const std::size_t maximum_supported_frames) noexcept
{
    if (metadata.maximum_frames == 0U ||
        metadata.maximum_frames > maximum_supported_frames ||
        metadata.frame_count > metadata.maximum_frames)
    {
        return false;
    }

    const auto final_offset =
        static_cast<std::uint64_t>(metadata.maximum_frames - 1U);
    return metadata.first_frame_index <=
           std::numeric_limits<std::uint64_t>::max() - final_offset;
}

[[nodiscard]] bool IsValidActionSchema(
    const std::span<const std::uint32_t> actions) noexcept
{
    if (actions.empty() || actions.size() > InputBindingTable::kMaxActions)
        return false;

    for (std::size_t index = 1U; index < actions.size(); ++index)
    {
        if (actions[index - 1U] >= actions[index])
            return false;
    }
    return true;
}
} // namespace

std::expected<detail::RunCaptureReplayPlan, RunCaptureReplayError>
detail::PlanRunCaptureReplay(const RunCaptureReplayMetadata& metadata) noexcept
{
    if (!IsValidTraceMetadata(metadata.input_trace, kMaximumInputTraceFrames) ||
        !IsValidActionSchema(metadata.action_schema))
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Create,
            RunCaptureReplayErrorCode::InvalidInputTrace));
    }

    if (!IsValidTraceMetadata(metadata.scheduler_elapsed_trace,
            kMaximumSchedulerElapsedTraceFrames))
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Create,
            RunCaptureReplayErrorCode::InvalidSchedulerElapsedTrace));
    }

    if (metadata.input_trace.maximum_frames !=
        metadata.scheduler_elapsed_trace.maximum_frames)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Create,
            RunCaptureReplayErrorCode::CapacityMismatch));
    }

    if (metadata.input_trace.first_frame_index !=
        metadata.scheduler_elapsed_trace.first_frame_index)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Create,
            RunCaptureReplayErrorCode::FirstFrameIndexMismatch));
    }

    const bool has_terminal = metadata.terminal_input.has_value();
    const bool counts_match = has_terminal
                                  ? metadata.input_trace.frame_count ==
                                        metadata.scheduler_elapsed_trace.frame_count + 1U
                                  : metadata.input_trace.frame_count ==
                                        metadata.scheduler_elapsed_trace.frame_count;
    if (!counts_match)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Create,
            RunCaptureReplayErrorCode::FrameCountMismatch));
    }

    if (has_terminal)
    {
        const RunCaptureTerminalInput terminal = *metadata.terminal_input;
        const std::uint64_t expected_terminal_frame =
            metadata.input_trace.first_frame_index +
            static_cast<std::uint64_t>(metadata.input_trace.frame_count - 1U);
        if ((!terminal.host_quit_requested && !terminal.logical_quit_pressed) ||
            terminal.frame_index != expected_terminal_frame)
        {
            return std::unexpected(Error(RunCaptureReplayOperation::Create,
                RunCaptureReplayErrorCode::InvalidTerminalInput));
        }
    }

    return RunCaptureReplayPlan{
        .frame_count = metadata.input_trace.frame_count,
        .elapsed_frame_count = metadata.scheduler_elapsed_trace.frame_count,
        .has_terminal_frame = has_terminal,
    };
}

std::expected<RunCaptureReplaySession, RunCaptureReplayError>
RunCaptureReplaySession::Create(RunCaptureTracePair&& traces) noexcept
{
    const InputTrace& input_trace = traces.input_trace();
    const SchedulerElapsedTrace& elapsed_trace = traces.scheduler_elapsed_trace();
    const detail::RunCaptureReplayMetadata metadata{
        .input_trace = detail::RunCaptureReplayTraceMetadata{
            .maximum_frames = input_trace.maximum_frames(),
            .first_frame_index = input_trace.first_frame_index(),
            .frame_count = input_trace.frame_count(),
        },
        .action_schema = input_trace.actions(),
        .scheduler_elapsed_trace = detail::RunCaptureReplayTraceMetadata{
            .maximum_frames = elapsed_trace.maximum_frames(),
            .first_frame_index = elapsed_trace.first_frame_index(),
            .frame_count = elapsed_trace.frame_count(),
        },
        .terminal_input = traces.terminal_input(),
    };

    const auto plan = detail::PlanRunCaptureReplay(metadata);
    if (!plan)
        return std::unexpected(plan.error());

    RunCaptureReplaySession session(std::move(traces), *plan);
    return std::expected<RunCaptureReplaySession, RunCaptureReplayError>{
        std::move(session)};
}

RunCaptureReplayFrame::RunCaptureReplayFrame(
    std::unique_ptr<InputSnapshot>&& input,
    const std::chrono::nanoseconds elapsed) noexcept
    : input_(std::move(input)), elapsed_(elapsed)
{
}

RunCaptureReplayFrame::RunCaptureReplayFrame(
    std::unique_ptr<InputSnapshot>&& input,
    const RunCaptureTerminalInput terminal) noexcept
    : input_(std::move(input)), terminal_input_(terminal)
{
}

const InputSnapshot& RunCaptureReplayFrame::input() const noexcept
{
    return *input_;
}

std::optional<std::chrono::nanoseconds> RunCaptureReplayFrame::elapsed() const noexcept
{
    return elapsed_;
}

std::optional<RunCaptureTerminalInput>
RunCaptureReplayFrame::terminal_input() const noexcept
{
    return terminal_input_;
}

RunCaptureReplaySession::RunCaptureReplaySession(
    RunCaptureTracePair&& traces, const detail::RunCaptureReplayPlan& plan) noexcept
    : traces_(std::in_place, std::move(traces)), frame_count_(plan.frame_count),
      elapsed_frame_count_(plan.elapsed_frame_count),
      state_(plan.frame_count == 0U ? State::Complete : State::Ready)
{
}

RunCaptureReplaySession::RunCaptureReplaySession(
    RunCaptureReplaySession&& other) noexcept
    : traces_(std::move(other.traces_)),
      cursor_(std::exchange(other.cursor_, 0U)),
      frame_count_(std::exchange(other.frame_count_, 0U)),
      elapsed_frame_count_(std::exchange(other.elapsed_frame_count_, 0U)),
      state_(std::exchange(other.state_, State::Inert))
{
    other.traces_.reset();
    other.NormalizeInert();
}

void RunCaptureReplaySession::NormalizeInert() noexcept
{
    traces_.reset();
    cursor_ = 0U;
    frame_count_ = 0U;
    elapsed_frame_count_ = 0U;
    state_ = State::Inert;
}

std::expected<RunCaptureReplayFrame, RunCaptureReplayError>
RunCaptureReplaySession::Next() noexcept
{
    if (state_ == State::Inert || !traces_)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Next,
            RunCaptureReplayErrorCode::InvalidReplayState));
    }
    if (state_ == State::Complete)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Next,
            RunCaptureReplayErrorCode::ReplayComplete));
    }

    const InputTrace& input_trace = traces_->input_trace();
    const auto input_frame = input_trace.FrameAt(cursor_);
    if (!input_frame)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Next,
            RunCaptureReplayErrorCode::TraceReadFailed));
    }

    const std::span<const std::uint32_t> actions = input_trace.actions();
    std::array<InputSnapshot::ActionRow, InputBindingTable::kMaxActions> rows{};
    for (std::size_t index = 0U; index < actions.size(); ++index)
    {
        const auto action_state = input_trace.ActionAt(cursor_, actions[index]);
        if (!action_state)
        {
            return std::unexpected(Error(RunCaptureReplayOperation::Next,
                RunCaptureReplayErrorCode::TraceReadFailed));
        }
        rows[index] = InputSnapshot::ActionRow{
            .action = actions[index],
            .held = action_state->held,
            .pressed = action_state->pressed,
            .released = action_state->released,
        };
    }

    std::optional<std::chrono::nanoseconds> elapsed;
    std::optional<RunCaptureTerminalInput> terminal;
    if (cursor_ < elapsed_frame_count_)
    {
        const auto elapsed_frame = traces_->scheduler_elapsed_trace().FrameAt(cursor_);
        if (!elapsed_frame || elapsed_frame->frame_index != input_frame->frame_index)
        {
            return std::unexpected(Error(RunCaptureReplayOperation::Next,
                RunCaptureReplayErrorCode::TraceReadFailed));
        }
        elapsed = elapsed_frame->elapsed;
    }
    else
    {
        terminal = traces_->terminal_input();
        if (!terminal || terminal->frame_index != input_frame->frame_index ||
            (!terminal->host_quit_requested && !terminal->logical_quit_pressed))
        {
            return std::unexpected(Error(RunCaptureReplayOperation::Next,
                RunCaptureReplayErrorCode::TraceReadFailed));
        }
    }
    const std::optional<PointerPositionQ16> pointer_position =
        input_trace.PointerAt(cursor_);

    std::unique_ptr<InputSnapshot> snapshot;
    try
    {
        snapshot.reset(new InputSnapshot(input_frame->frame_index, actions,
            std::span<const InputSnapshot::ActionRow>{rows.data(), actions.size()},
            pointer_position,
            input_frame->accepted_event_count, input_frame->rejected_event_count));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Next,
            RunCaptureReplayErrorCode::AllocationFailed));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(RunCaptureReplayOperation::Next,
            RunCaptureReplayErrorCode::AllocationFailed));
    }

    std::expected<RunCaptureReplayFrame, RunCaptureReplayError> published = elapsed
        ? std::expected<RunCaptureReplayFrame, RunCaptureReplayError>{
              RunCaptureReplayFrame(std::move(snapshot), *elapsed)}
        : std::expected<RunCaptureReplayFrame, RunCaptureReplayError>{
              RunCaptureReplayFrame(std::move(snapshot), *terminal)};
    ++cursor_;
    if (cursor_ == frame_count_)
        state_ = State::Complete;
    return published;
}

std::size_t RunCaptureReplaySession::remaining_frames() const noexcept
{
    if (state_ == State::Inert || cursor_ >= frame_count_)
        return 0U;
    return frame_count_ - cursor_;
}

bool RunCaptureReplaySession::complete() const noexcept
{
    return state_ == State::Complete;
}
} // namespace omega::runtime
