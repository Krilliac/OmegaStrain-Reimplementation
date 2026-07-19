#include "run_replay_session.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace omega::app
{
namespace
{
[[nodiscard]] constexpr RunReplayError Error(
    const RunReplayOperation operation, const RunReplayErrorCode code) noexcept
{
    return RunReplayError{
        .operation = operation,
        .code = code,
        .message = RunReplayErrorMessage(code),
        .replay_error = std::nullopt,
    };
}

[[nodiscard]] constexpr RunReplayError ReplayError(
    const RunReplayOperation operation,
    const RunReplayErrorCode code,
    const runtime::RunCaptureReplayError& replay_error) noexcept
{
    return RunReplayError{
        .operation = operation,
        .code = code,
        .message = RunReplayErrorMessage(code),
        .replay_error = replay_error,
    };
}
} // namespace

RunReplayFrame::RunReplayFrame(runtime::RunCaptureReplayFrame&& replay_frame,
    const runtime::FramePlan frame_plan) noexcept
    : replay_frame_(std::move(replay_frame)), frame_plan_(frame_plan)
{
}

RunReplayFrame::RunReplayFrame(runtime::RunCaptureReplayFrame&& replay_frame) noexcept
    : replay_frame_(std::move(replay_frame))
{
}

const runtime::InputSnapshot& RunReplayFrame::input() const noexcept
{
    return replay_frame_.input();
}

std::optional<std::chrono::nanoseconds> RunReplayFrame::elapsed() const noexcept
{
    return replay_frame_.elapsed();
}

std::optional<runtime::RunCaptureTerminalInput>
RunReplayFrame::terminal_input() const noexcept
{
    return replay_frame_.terminal_input();
}

std::optional<runtime::FramePlan> RunReplayFrame::frame_plan() const noexcept
{
    return frame_plan_;
}

std::expected<RunReplaySession, RunReplayError> RunReplaySession::Create(
    runtime::RunCaptureTracePair&& traces, const RunReplaySessionConfig config)
{
    auto scheduler = runtime::FrameScheduler::Create(config.scheduler);
    if (!scheduler)
    {
        return std::unexpected(Error(RunReplayOperation::Create,
            RunReplayErrorCode::InvalidSchedulerConfig));
    }

    if (config.maximum_entities == 0U ||
        config.maximum_entities > simulation::EntityRegistry::kMaximumCapacity)
    {
        return std::unexpected(Error(RunReplayOperation::Create,
            RunReplayErrorCode::InvalidEntityCapacity));
    }

    auto world = simulation::SimulationWorld::Create(
        simulation::SimulationWorldConfig{
            .fixed_step = scheduler->config().simulation_step,
            .maximum_entities = config.maximum_entities,
        });
    if (!world)
    {
        return std::unexpected(Error(RunReplayOperation::Create,
            RunReplayErrorCode::SimulationWorldCreateFailed));
    }

    auto replay = runtime::RunCaptureReplaySession::Create(std::move(traces));
    if (!replay)
    {
        return std::unexpected(ReplayError(RunReplayOperation::Create,
            RunReplayErrorCode::ReplayCreateFailed, replay.error()));
    }

    RunReplaySession session(
        std::move(*scheduler), std::move(*world), std::move(*replay));
    return std::expected<RunReplaySession, RunReplayError>{std::move(session)};
}

RunReplaySession::RunReplaySession(runtime::FrameScheduler&& scheduler,
    simulation::SimulationWorld&& simulation,
    runtime::RunCaptureReplaySession&& replay) noexcept
    : scheduler_(std::in_place, std::move(scheduler)),
      simulation_(std::in_place, std::move(simulation)),
      replay_(std::in_place, std::move(replay)),
      state_(replay_->complete()
                 ? RunReplaySessionState::Complete
                 : RunReplaySessionState::Ready)
{
}

RunReplaySession::RunReplaySession(RunReplaySession&& other) noexcept
    : scheduler_(std::move(other.scheduler_)),
      simulation_(std::move(other.simulation_)),
      replay_(std::move(other.replay_)),
      state_(std::exchange(other.state_, RunReplaySessionState::Inert))
{
    other.NormalizeInert();
}

void RunReplaySession::NormalizeInert() noexcept
{
    replay_.reset();
    simulation_.reset();
    scheduler_.reset();
    state_ = RunReplaySessionState::Inert;
}

std::expected<RunReplayFrame, RunReplayError> RunReplaySession::Next() noexcept
{
    if (state_ == RunReplaySessionState::Inert ||
        state_ == RunReplaySessionState::Failed || !scheduler_ || !simulation_ || !replay_)
    {
        return std::unexpected(Error(RunReplayOperation::Next,
            RunReplayErrorCode::InvalidSessionState));
    }
    if (state_ == RunReplaySessionState::Complete)
    {
        return std::unexpected(Error(
            RunReplayOperation::Next, RunReplayErrorCode::ReplayComplete));
    }

    auto replay_frame = replay_->Next();
    if (!replay_frame)
    {
        return std::unexpected(ReplayError(RunReplayOperation::Next,
            RunReplayErrorCode::ReplayNextFailed, replay_frame.error()));
    }

    if (replay_frame->terminal_input())
    {
        state_ = RunReplaySessionState::Complete;
        return std::expected<RunReplayFrame, RunReplayError>{
            RunReplayFrame(std::move(*replay_frame))};
    }

    const std::optional<std::chrono::nanoseconds> elapsed = replay_frame->elapsed();
    const runtime::FramePlan plan = scheduler_->BeginFrame(*elapsed);
    for (std::uint32_t step = 0U; step < plan.simulation_steps; ++step)
    {
        if (simulation_->AdvanceOneStep() ==
            simulation::SimulationStepResult::RepresentationExhausted)
        {
            state_ = RunReplaySessionState::Failed;
            return std::unexpected(Error(RunReplayOperation::Next,
                RunReplayErrorCode::SimulationRepresentationExhausted));
        }
    }

    state_ = replay_->complete()
                 ? RunReplaySessionState::Complete
                 : RunReplaySessionState::Ready;
    return std::expected<RunReplayFrame, RunReplayError>{
        RunReplayFrame(std::move(*replay_frame), plan)};
}

RunReplaySessionState RunReplaySession::state() const noexcept
{
    return state_;
}

std::size_t RunReplaySession::remaining_frames() const noexcept
{
    if (!replay_ || state_ == RunReplaySessionState::Inert ||
        state_ == RunReplaySessionState::Complete)
    {
        return 0U;
    }
    return replay_->remaining_frames();
}

std::optional<runtime::FrameSchedulerState>
RunReplaySession::scheduler_state() const noexcept
{
    if (!scheduler_)
        return std::nullopt;
    return scheduler_->Snapshot();
}

std::optional<simulation::SimulationState>
RunReplaySession::simulation_state() const noexcept
{
    if (!simulation_)
        return std::nullopt;
    return simulation_->Snapshot();
}
} // namespace omega::app
