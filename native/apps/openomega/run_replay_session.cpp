#include "run_replay_session.h"

#include "omega/gameplay/debug_locomotion.h"

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
    const runtime::FramePlan frame_plan, const FrontEndCommand front_end_command) noexcept
    : replay_frame_(std::move(replay_frame)), frame_plan_(frame_plan),
      front_end_command_(front_end_command)
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

FrontEndCommand RunReplayFrame::front_end_command() const noexcept
{
    return front_end_command_;
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

    std::optional<simulation::EntityId> debug_locomotion_entity;
    if (config.enable_debug_locomotion)
    {
        auto created_entity = world->CreatePositionedEntity(simulation::Position3{});
        if (!created_entity)
        {
            return std::unexpected(Error(RunReplayOperation::Create,
                RunReplayErrorCode::DebugLocomotionEntityCreateFailed));
        }
        debug_locomotion_entity = *created_entity;
    }

    auto replay = runtime::RunCaptureReplaySession::Create(std::move(traces));
    if (!replay)
    {
        return std::unexpected(ReplayError(RunReplayOperation::Create,
            RunReplayErrorCode::ReplayCreateFailed, replay.error()));
    }

    FrontEndCapabilities front_end_capabilities = config.front_end_capabilities;
    front_end_capabilities.can_create_first_profile =
        front_end_capabilities.can_create_first_profile &&
        config.front_end_visible_profile_slots == 0U &&
        config.front_end_total_profile_count == 0U;

    RunReplaySession session(
        std::move(*scheduler), std::move(*world), std::move(*replay),
        debug_locomotion_entity, config.initial_front_end_state,
        config.front_end_visible_profile_slots,
        config.front_end_total_profile_count, front_end_capabilities);
    return std::expected<RunReplaySession, RunReplayError>{std::move(session)};
}

RunReplaySession::RunReplaySession(runtime::FrameScheduler&& scheduler,
    simulation::SimulationWorld&& simulation,
    runtime::RunCaptureReplaySession&& replay,
    const std::optional<simulation::EntityId> debug_locomotion_entity,
    const std::optional<FrontEndState> front_end_state,
    const std::uint8_t front_end_visible_profile_slots,
    const std::size_t front_end_total_profile_count,
    const FrontEndCapabilities front_end_capabilities) noexcept
    : scheduler_(std::in_place, std::move(scheduler)),
      simulation_(std::in_place, std::move(simulation)),
      replay_(std::in_place, std::move(replay)),
      debug_locomotion_entity_(debug_locomotion_entity),
      front_end_state_(front_end_state),
      front_end_visible_profile_slots_(front_end_visible_profile_slots),
      front_end_total_profile_count_(front_end_total_profile_count),
      front_end_capabilities_(front_end_capabilities),
      front_end_active_profile_is_confirmed_(false),
      state_(replay_->complete()
                 ? RunReplaySessionState::Complete
                 : RunReplaySessionState::Ready)
{
}

RunReplaySession::RunReplaySession(RunReplaySession&& other) noexcept
    : scheduler_(std::move(other.scheduler_)),
      simulation_(std::move(other.simulation_)),
      replay_(std::move(other.replay_)),
      debug_locomotion_entity_(std::exchange(
          other.debug_locomotion_entity_, std::nullopt)),
      front_end_state_(std::exchange(
          other.front_end_state_, std::nullopt)),
      front_end_visible_profile_slots_(std::exchange(
          other.front_end_visible_profile_slots_, std::uint8_t{0U})),
      front_end_total_profile_count_(std::exchange(
          other.front_end_total_profile_count_, std::size_t{0U})),
      front_end_capabilities_(std::exchange(
          other.front_end_capabilities_, FrontEndCapabilities{})),
      front_end_active_profile_is_confirmed_(std::exchange(
          other.front_end_active_profile_is_confirmed_, false)),
      state_(std::exchange(other.state_, RunReplaySessionState::Inert))
{
    other.NormalizeInert();
}

void RunReplaySession::NormalizeInert() noexcept
{
    replay_.reset();
    debug_locomotion_entity_.reset();
    front_end_state_.reset();
    front_end_visible_profile_slots_ = 0U;
    front_end_total_profile_count_ = 0U;
    front_end_capabilities_ = {};
    front_end_active_profile_is_confirmed_ = false;
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

    // One capability value authorizes both the reducer and the simulation check,
    // so replay cannot open the gate for one and leave it closed for the other.
    const FrontEndCapabilities front_end_capabilities{
        .can_create_first_profile =
            front_end_capabilities_.can_create_first_profile &&
            front_end_visible_profile_slots_ == 0U &&
            front_end_total_profile_count_ == 0U,
        .requires_active_profile_for_diagnostic_play =
            front_end_capabilities_.requires_active_profile_for_diagnostic_play,
    };
    FrontEndCommand front_end_command{};
    if (front_end_state_)
    {
        const FrontEndReduction front_end = ReduceFrontEnd(*front_end_state_,
            FrontEndInputEdges{
                .primary_pressed =
                    replay_frame->input().WasPressed(kFrontEndPrimaryAction),
                .previous_pressed =
                    replay_frame->input().WasPressed(kFrontEndPreviousAction),
                .next_pressed =
                    replay_frame->input().WasPressed(kFrontEndNextAction),
                .cancel_pressed =
                    replay_frame->input().WasPressed(kFrontEndCancelAction),
            },
            front_end_visible_profile_slots_, front_end_capabilities,
            front_end_active_profile_is_confirmed_);
        *front_end_state_ = front_end.state;
        front_end_command = front_end.command;
        if (front_end_command.type == FrontEndCommandType::CreateFirstProfile)
        {
            // Replay models the successful bounded command only. No identifier,
            // clock, persistence owner, catalog, renderer, or GPU work is touched.
            // Creation deliberately leaves the authorization mirror closed: it
            // publishes no confirmation, so a created profile still cannot reach
            // diagnostic play until a later selection is replayed.
            front_end_visible_profile_slots_ = 1U;
            front_end_total_profile_count_ = 1U;
            front_end_capabilities_.can_create_first_profile = false;
        }
        else if (front_end_command.type == FrontEndCommandType::SetActiveProfile)
        {
            // The reducer publishes this command only for a selectable bounded
            // position, which is the replay-local counterpart of the app's
            // durable confirmation. The mirror opens after that command, before
            // this frame's simulation authorization, exactly as the app applies
            // its command before consulting the gate.
            front_end_active_profile_is_confirmed_ = true;
        }
    }
    const bool simulation_allowed = !front_end_state_ ||
                                    FrontEndAllowsSimulation(
                                        *front_end_state_, front_end_capabilities,
                                        front_end_active_profile_is_confirmed_);
    const std::optional<std::chrono::nanoseconds> elapsed = replay_frame->elapsed();
    const std::chrono::nanoseconds effective_elapsed = simulation_allowed
        ? *elapsed
        : std::chrono::nanoseconds::zero();
    const runtime::FramePlan plan = scheduler_->BeginFrame(effective_elapsed);

    simulation::SimulationStepInput simulation_input{};
    if (simulation_allowed && debug_locomotion_entity_)
    {
        const auto translation = gameplay::PlanDebugLocomotionStep(
            gameplay::DigitalMoveCommand{
                .lateral = static_cast<std::int8_t>(
                    (replay_frame->input().IsHeld(kDebugMoveRightAction) ? 1 : 0) -
                    (replay_frame->input().IsHeld(kDebugMoveLeftAction) ? 1 : 0)),
                .longitudinal = static_cast<std::int8_t>(
                    (replay_frame->input().IsHeld(kDebugMoveForwardAction) ? 1 : 0) -
                    (replay_frame->input().IsHeld(kDebugMoveBackwardAction) ? 1 : 0)),
            });
        if (!translation)
        {
            state_ = RunReplaySessionState::Failed;
            return std::unexpected(Error(RunReplayOperation::Next,
                RunReplayErrorCode::DebugLocomotionPlanFailed));
        }
        simulation_input.translation = simulation::EntityTranslation{
            .entity = *debug_locomotion_entity_,
            .delta = *translation,
        };
    }

    for (std::uint32_t step = 0U; step < plan.simulation_steps; ++step)
    {
        if (simulation_->AdvanceOneStep(simulation_input) !=
            simulation::SimulationStepResult::Advanced)
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
        RunReplayFrame(std::move(*replay_frame), plan, front_end_command)};
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

std::optional<simulation::Position3>
RunReplaySession::debug_locomotion_position() const noexcept
{
    if (!simulation_ || !debug_locomotion_entity_)
        return std::nullopt;
    return simulation_->PositionOf(*debug_locomotion_entity_);
}

std::optional<runtime::RenderTargetRectQ16>
RunReplaySession::diagnostic_actor_marker_destination() const noexcept
{
    const std::optional<simulation::Position3> position =
        debug_locomotion_position();
    if (!position)
        return std::nullopt;
    return PlanProjectDiagnosticActorMarkerDestination(*position);
}

std::optional<FrontEndState>
RunReplaySession::front_end_state() const noexcept
{
    return front_end_state_;
}

bool RunReplaySession::front_end_active_profile_is_confirmed() const noexcept
{
    return front_end_active_profile_is_confirmed_;
}
} // namespace omega::app
