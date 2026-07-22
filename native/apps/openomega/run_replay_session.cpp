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

    if (config.enable_debug_mission_lifecycle &&
        (!config.enable_debug_locomotion || !config.enable_debug_target_fire ||
         !config.initial_front_end_state ||
         !IsValidFrontEndState(*config.initial_front_end_state) ||
         !config.front_end_capabilities.can_start_diagnostic_campaign ||
         !config.front_end_capabilities.supports_character_selection))
    {
        return std::unexpected(Error(RunReplayOperation::Create,
            RunReplayErrorCode::InvalidDiagnosticMissionLifecycleConfig));
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
    front_end_capabilities.can_create_first_character =
        front_end_capabilities.supports_character_selection &&
        front_end_capabilities.can_create_first_character;

    RunReplaySession session(
        std::move(*scheduler), std::move(*world), std::move(*replay),
        debug_locomotion_entity,
        debug_locomotion_entity
            ? std::optional<gameplay::DiagnosticProximityTriggerState>{
                  gameplay::DiagnosticProximityTriggerState{}}
            : std::nullopt,
        config.enable_debug_target_fire
            ? std::optional<gameplay::DiagnosticTargetFireState>{
                  gameplay::DiagnosticTargetFireState{}}
            : std::nullopt,
        config.enable_debug_mission_lifecycle
            ? std::optional<gameplay::DiagnosticMissionLifecycleState>{
                  gameplay::DiagnosticMissionLifecycleState{
                      .status = FrontEndAllowsSimulation(
                                    *config.initial_front_end_state,
                                    front_end_capabilities, false, false)
                                    ? gameplay::DiagnosticMissionStatus::Active
                                    : gameplay::DiagnosticMissionStatus::Ready,
                  }}
            : std::nullopt,
        config.initial_front_end_state,
        config.front_end_visible_profile_slots,
        config.front_end_total_profile_count,
        config.front_end_visible_character_slots_by_profile,
        config.front_end_total_character_counts_by_profile,
        front_end_capabilities);
    return std::expected<RunReplaySession, RunReplayError>{std::move(session)};
}

RunReplaySession::RunReplaySession(runtime::FrameScheduler&& scheduler,
    simulation::SimulationWorld&& simulation,
    runtime::RunCaptureReplaySession&& replay,
    const std::optional<simulation::EntityId> debug_locomotion_entity,
    const std::optional<gameplay::DiagnosticProximityTriggerState>
        diagnostic_proximity_trigger_state,
    const std::optional<gameplay::DiagnosticTargetFireState>
        diagnostic_target_fire_state,
    const std::optional<gameplay::DiagnosticMissionLifecycleState>
        diagnostic_mission_lifecycle_state,
    const std::optional<FrontEndState> front_end_state,
    const std::uint8_t front_end_visible_profile_slots,
    const std::size_t front_end_total_profile_count,
    std::array<std::uint8_t, kFrontEndVisibleProfiles>
        front_end_visible_character_slots_by_profile,
    std::array<std::size_t, kFrontEndVisibleProfiles>
        front_end_total_character_counts_by_profile,
    const FrontEndCapabilities front_end_capabilities) noexcept
    : scheduler_(std::in_place, std::move(scheduler)),
      simulation_(std::in_place, std::move(simulation)),
      replay_(std::in_place, std::move(replay)),
      debug_locomotion_entity_(debug_locomotion_entity),
      diagnostic_proximity_trigger_state_(diagnostic_proximity_trigger_state),
      diagnostic_target_fire_state_(diagnostic_target_fire_state),
      diagnostic_mission_lifecycle_state_(diagnostic_mission_lifecycle_state),
      front_end_state_(front_end_state),
      front_end_visible_profile_slots_(front_end_visible_profile_slots),
      front_end_total_profile_count_(front_end_total_profile_count),
      front_end_visible_character_slots_by_profile_(
          front_end_visible_character_slots_by_profile),
      front_end_total_character_counts_by_profile_(
          front_end_total_character_counts_by_profile),
      front_end_active_profile_slot_(std::nullopt),
      front_end_active_visible_character_slots_(0U),
      front_end_active_total_character_count_(0U),
      front_end_capabilities_(front_end_capabilities),
      front_end_active_profile_is_confirmed_(false),
      front_end_active_character_is_confirmed_(false),
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
      diagnostic_proximity_trigger_state_(std::exchange(
          other.diagnostic_proximity_trigger_state_, std::nullopt)),
      diagnostic_target_fire_state_(std::exchange(
          other.diagnostic_target_fire_state_, std::nullopt)),
      diagnostic_mission_lifecycle_state_(std::exchange(
          other.diagnostic_mission_lifecycle_state_, std::nullopt)),
      front_end_state_(std::exchange(
          other.front_end_state_, std::nullopt)),
      front_end_visible_profile_slots_(std::exchange(
          other.front_end_visible_profile_slots_, std::uint8_t{0U})),
      front_end_total_profile_count_(std::exchange(
          other.front_end_total_profile_count_, std::size_t{0U})),
      front_end_visible_character_slots_by_profile_(std::exchange(
          other.front_end_visible_character_slots_by_profile_,
          std::array<std::uint8_t, kFrontEndVisibleProfiles>{})),
      front_end_total_character_counts_by_profile_(std::exchange(
          other.front_end_total_character_counts_by_profile_,
          std::array<std::size_t, kFrontEndVisibleProfiles>{})),
      front_end_active_profile_slot_(std::exchange(
          other.front_end_active_profile_slot_, std::nullopt)),
      front_end_active_visible_character_slots_(std::exchange(
          other.front_end_active_visible_character_slots_, std::uint8_t{0U})),
      front_end_active_total_character_count_(std::exchange(
          other.front_end_active_total_character_count_, std::size_t{0U})),
      front_end_capabilities_(std::exchange(
          other.front_end_capabilities_, FrontEndCapabilities{})),
      front_end_active_profile_is_confirmed_(std::exchange(
          other.front_end_active_profile_is_confirmed_, false)),
      front_end_active_character_is_confirmed_(std::exchange(
          other.front_end_active_character_is_confirmed_, false)),
      state_(std::exchange(other.state_, RunReplaySessionState::Inert))
{
    other.NormalizeInert();
}

void RunReplaySession::NormalizeInert() noexcept
{
    replay_.reset();
    debug_locomotion_entity_.reset();
    diagnostic_proximity_trigger_state_.reset();
    diagnostic_target_fire_state_.reset();
    diagnostic_mission_lifecycle_state_.reset();
    front_end_state_.reset();
    front_end_visible_profile_slots_ = 0U;
    front_end_total_profile_count_ = 0U;
    front_end_visible_character_slots_by_profile_ = {};
    front_end_total_character_counts_by_profile_ = {};
    front_end_active_profile_slot_.reset();
    front_end_active_visible_character_slots_ = 0U;
    front_end_active_total_character_count_ = 0U;
    front_end_capabilities_ = {};
    front_end_active_profile_is_confirmed_ = false;
    front_end_active_character_is_confirmed_ = false;
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

    std::optional<FrontEndState> next_front_end_state = front_end_state_;
    std::optional<gameplay::DiagnosticProximityTriggerState>
        next_diagnostic_proximity_trigger_state =
            diagnostic_proximity_trigger_state_;
    std::optional<gameplay::DiagnosticTargetFireState>
        next_diagnostic_target_fire_state = diagnostic_target_fire_state_;
    std::optional<gameplay::DiagnosticMissionLifecycleState>
        next_diagnostic_mission_lifecycle_state =
            diagnostic_mission_lifecycle_state_;

    // One capability value authorizes both the reducer and the simulation check,
    // so replay cannot open the gate for one and leave it closed for the other.
    const FrontEndCapabilities front_end_capabilities{
        .can_create_first_profile =
            front_end_capabilities_.can_create_first_profile &&
            front_end_visible_profile_slots_ == 0U &&
            front_end_total_profile_count_ == 0U,
        .can_start_diagnostic_campaign =
            front_end_capabilities_.can_start_diagnostic_campaign,
        .requires_active_profile_for_diagnostic_play =
            front_end_capabilities_.requires_active_profile_for_diagnostic_play,
        .supports_character_selection =
            front_end_capabilities_.supports_character_selection,
        .can_create_first_character =
            front_end_capabilities_.supports_character_selection &&
            front_end_capabilities_.can_create_first_character &&
            front_end_active_visible_character_slots_ == 0U &&
            front_end_active_total_character_count_ == 0U,
        .requires_active_character_for_diagnostic_play =
            front_end_capabilities_.supports_character_selection &&
            front_end_capabilities_.requires_active_character_for_diagnostic_play,
    };
    FrontEndCommand front_end_command{};
    const bool gameplay_input_context =
        !front_end_state_ ||
        front_end_state_->mode == FrontEndMode::DiagnosticPlay;
    if (next_front_end_state)
    {
        const FrontEndReduction front_end = ReduceFrontEnd(*next_front_end_state,
            ResolveFrontEndInputEdges(next_front_end_state->mode,
                FrontEndInputEdges{
                    .primary_pressed = replay_frame->input().WasPressed(
                        kFrontEndPrimaryAction),
                    .previous_pressed = replay_frame->input().WasPressed(
                        kFrontEndPreviousAction),
                    .next_pressed = replay_frame->input().WasPressed(
                        kFrontEndNextAction),
                    .cancel_pressed = replay_frame->input().WasPressed(
                        kFrontEndCancelAction),
                },
                replay_frame->input().WasPressed(kDebugMoveLeftAction),
                replay_frame->input().WasPressed(kDebugMoveRightAction),
                replay_frame->input().WasPressed(kDebugFireAction),
                replay_frame->input().WasPressed(kDebugTargetAction)),
            front_end_visible_profile_slots_, front_end_capabilities,
            front_end_active_profile_is_confirmed_,
            front_end_active_visible_character_slots_,
            front_end_active_character_is_confirmed_);
        *next_front_end_state = front_end.state;
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
            front_end_visible_character_slots_by_profile_[0U] = 0U;
            front_end_total_character_counts_by_profile_[0U] = 0U;
            front_end_capabilities_.can_create_first_profile = false;
        }
        else if (front_end_command.type == FrontEndCommandType::SetActiveProfile)
        {
            // The reducer publishes this command only for a selectable bounded
            // visible position. The live app also resolves the position against
            // the complete startup model before publishing durable confirmation,
            // so replay must keep an impossible visible/total combination closed.
            // The mirror opens after the accepted command, before this frame's
            // simulation authorization, exactly as the app applies its command
            // before consulting the gate.
            const std::size_t selected_profile_slot =
                static_cast<std::size_t>(front_end_command.profile_slot);
            front_end_active_profile_is_confirmed_ =
                selected_profile_slot < front_end_visible_profile_slots_ &&
                selected_profile_slot < front_end_total_profile_count_;
            front_end_active_character_is_confirmed_ = false;
            front_end_active_profile_slot_.reset();
            front_end_active_visible_character_slots_ = 0U;
            front_end_active_total_character_count_ = 0U;
            if (front_end_active_profile_is_confirmed_ &&
                front_end_capabilities_.supports_character_selection)
            {
                front_end_active_profile_slot_ = front_end_command.profile_slot;
                front_end_active_visible_character_slots_ =
                    front_end_visible_character_slots_by_profile_[selected_profile_slot];
                front_end_active_total_character_count_ =
                    front_end_total_character_counts_by_profile_[selected_profile_slot];
            }
        }
        else if (front_end_command.type ==
                 FrontEndCommandType::CreateFirstCharacter)
        {
            // Character creation is a replay-local logical effect only. It
            // publishes no identity and therefore cannot open confirmation.
            front_end_active_visible_character_slots_ = 1U;
            front_end_active_total_character_count_ = 1U;
            if (front_end_active_profile_slot_)
            {
                const std::size_t selected_profile_slot =
                    static_cast<std::size_t>(*front_end_active_profile_slot_);
                front_end_visible_character_slots_by_profile_[selected_profile_slot] = 1U;
                front_end_total_character_counts_by_profile_[selected_profile_slot] = 1U;
            }
        }
        else if (front_end_command.type ==
                 FrontEndCommandType::SetActiveCharacter)
        {
            const std::size_t selected_character_slot =
                static_cast<std::size_t>(front_end_command.character_slot);
            front_end_active_character_is_confirmed_ =
                front_end_active_profile_is_confirmed_ &&
                selected_character_slot < front_end_active_visible_character_slots_ &&
                selected_character_slot < front_end_active_total_character_count_;
        }
        // StartDiagnosticCampaign remains identity- and persistence-free. The
        // optional project diagnostic lifecycle below consumes only the bounded
        // command and owns no durable game-session marker.
    }
    const bool diagnostic_mission_aborted_now =
        next_diagnostic_mission_lifecycle_state && gameplay_input_context &&
        next_diagnostic_mission_lifecycle_state->status ==
            gameplay::DiagnosticMissionStatus::Active &&
        next_front_end_state &&
        next_front_end_state->mode != FrontEndMode::DiagnosticPlay;
    bool diagnostic_mission_deployment_reset_pending = false;

    if (next_diagnostic_mission_lifecycle_state &&
        front_end_command.type == FrontEndCommandType::StartDiagnosticCampaign)
    {
        const auto deployed = gameplay::AdvanceDiagnosticMissionLifecycle(
            *next_diagnostic_mission_lifecycle_state,
            gameplay::DiagnosticMissionEvent::Deploy);
        if (!deployed || !deployed->reset_gameplay_now ||
            deployed->enter_briefing_now)
        {
            state_ = RunReplaySessionState::Failed;
            return std::unexpected(Error(RunReplayOperation::Next,
                RunReplayErrorCode::DiagnosticMissionLifecycleFailed));
        }
        if (!debug_locomotion_entity_ ||
            !simulation_->PositionOf(*debug_locomotion_entity_))
        {
            state_ = RunReplaySessionState::Failed;
            return std::unexpected(Error(RunReplayOperation::Next,
                RunReplayErrorCode::DiagnosticMissionPositionResetFailed));
        }
        diagnostic_mission_deployment_reset_pending = true;
        *next_diagnostic_mission_lifecycle_state = deployed->state;
        next_diagnostic_proximity_trigger_state =
            gameplay::DiagnosticProximityTriggerState{};
        next_diagnostic_target_fire_state =
            gameplay::DiagnosticTargetFireState{};
    }

    const bool simulation_allowed =
        !next_front_end_state ||
        FrontEndAllowsSimulation(*next_front_end_state, front_end_capabilities,
            front_end_active_profile_is_confirmed_,
            front_end_active_character_is_confirmed_);
    const bool target_complete_at_frame_start =
        next_diagnostic_target_fire_state &&
        next_diagnostic_target_fire_state->target_complete;
    if (next_diagnostic_target_fire_state)
    {
        std::optional<gameplay::DiagnosticAimPointQ16> pointer;
        if (const std::optional<runtime::PointerPositionQ16> captured_pointer =
                replay_frame->input().pointer_position())
        {
            pointer = gameplay::DiagnosticAimPointQ16{
                .x = captured_pointer->x,
                .y = captured_pointer->y,
            };
        }
        const bool proximity_complete_at_frame_start =
            diagnostic_proximity_trigger_state_ &&
            diagnostic_proximity_trigger_state_->objective_complete;
        const auto advanced = gameplay::AdvanceDiagnosticTargetFire(
            gameplay::kProjectDiagnosticAimTarget,
            *next_diagnostic_target_fire_state,
            gameplay::DiagnosticTargetFireInput{
                .pointer = pointer,
                .enabled = gameplay_input_context && simulation_allowed &&
                           proximity_complete_at_frame_start,
                .target_held =
                    replay_frame->input().IsHeld(kDebugTargetAction),
                .fire_pressed =
                    replay_frame->input().WasPressed(kDebugFireAction),
            });
        if (!advanced)
        {
            state_ = RunReplaySessionState::Failed;
            return std::unexpected(Error(RunReplayOperation::Next,
                RunReplayErrorCode::DiagnosticTargetFireFailed));
        }
        *next_diagnostic_target_fire_state = advanced->state;
    }
    const std::optional<std::chrono::nanoseconds> elapsed = replay_frame->elapsed();
    const std::chrono::nanoseconds effective_elapsed = simulation_allowed
        ? *elapsed
        : std::chrono::nanoseconds::zero();
    const runtime::FramePlan plan = scheduler_->BeginFrame(effective_elapsed);

    simulation::SimulationStepInput simulation_input{};
    if (simulation_allowed && gameplay_input_context &&
        debug_locomotion_entity_)
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
        if (next_diagnostic_proximity_trigger_state)
        {
            if (!debug_locomotion_entity_)
            {
                state_ = RunReplaySessionState::Failed;
                return std::unexpected(Error(RunReplayOperation::Next,
                    RunReplayErrorCode::DiagnosticProximityTriggerFailed));
            }
            const std::optional<simulation::Position3> position =
                diagnostic_mission_deployment_reset_pending
                ? std::optional<simulation::Position3>{simulation::Position3{}}
                : simulation_->PositionOf(*debug_locomotion_entity_);
            if (!position)
            {
                state_ = RunReplaySessionState::Failed;
                return std::unexpected(Error(RunReplayOperation::Next,
                    RunReplayErrorCode::DiagnosticProximityTriggerFailed));
            }
            const auto advanced = gameplay::AdvanceDiagnosticProximityTrigger(
                gameplay::kProjectDiagnosticObjectiveVolume,
                *next_diagnostic_proximity_trigger_state, *position);
            if (!advanced)
            {
                state_ = RunReplaySessionState::Failed;
                return std::unexpected(Error(RunReplayOperation::Next,
                    RunReplayErrorCode::DiagnosticProximityTriggerFailed));
            }
            *next_diagnostic_proximity_trigger_state = advanced->state;
        }
    }

    if (next_diagnostic_mission_lifecycle_state)
    {
        gameplay::DiagnosticMissionEvent mission_event =
            gameplay::DiagnosticMissionEvent::None;
        if (diagnostic_mission_aborted_now)
        {
            mission_event = gameplay::DiagnosticMissionEvent::Abort;
        }
        else if (next_diagnostic_mission_lifecycle_state->status ==
                     gameplay::DiagnosticMissionStatus::Active &&
                 !target_complete_at_frame_start &&
                 next_diagnostic_target_fire_state &&
                 next_diagnostic_target_fire_state->target_complete)
        {
            mission_event = gameplay::DiagnosticMissionEvent::Complete;
        }

        const auto advanced = gameplay::AdvanceDiagnosticMissionLifecycle(
            *next_diagnostic_mission_lifecycle_state, mission_event);
        if (!advanced)
        {
            state_ = RunReplaySessionState::Failed;
            return std::unexpected(Error(RunReplayOperation::Next,
                RunReplayErrorCode::DiagnosticMissionLifecycleFailed));
        }
        *next_diagnostic_mission_lifecycle_state = advanced->state;
        if (advanced->enter_briefing_now &&
            mission_event == gameplay::DiagnosticMissionEvent::Complete)
        {
            if (!next_front_end_state)
            {
                state_ = RunReplaySessionState::Failed;
                return std::unexpected(Error(RunReplayOperation::Next,
                    RunReplayErrorCode::DiagnosticMissionLifecycleFailed));
            }
            if (front_end_capabilities.supports_character_selection)
            {
                next_front_end_state->mode = FrontEndMode::BriefingRoom;
                next_front_end_state->selected_main_row =
                    FrontEndMainRow::StartDiagnostic;
            }
            else
            {
                *next_front_end_state = InitialFrontEndState();
            }
        }
    }

    if (diagnostic_mission_deployment_reset_pending &&
        (!debug_locomotion_entity_ ||
            simulation_->ResetPosition(
                *debug_locomotion_entity_, simulation::Position3{}) !=
                simulation::PositionResetResult::Reset))
    {
        state_ = RunReplaySessionState::Failed;
        return std::unexpected(Error(RunReplayOperation::Next,
            RunReplayErrorCode::DiagnosticMissionPositionResetFailed));
    }

    front_end_state_ = next_front_end_state;
    diagnostic_proximity_trigger_state_ =
        next_diagnostic_proximity_trigger_state;
    diagnostic_target_fire_state_ = next_diagnostic_target_fire_state;
    diagnostic_mission_lifecycle_state_ =
        next_diagnostic_mission_lifecycle_state;

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

std::optional<gameplay::DiagnosticProximityTriggerState>
RunReplaySession::diagnostic_proximity_trigger_state() const noexcept
{
    return diagnostic_proximity_trigger_state_;
}

std::optional<gameplay::DiagnosticTargetFireState>
RunReplaySession::diagnostic_target_fire_state() const noexcept
{
    return diagnostic_target_fire_state_;
}

std::optional<gameplay::DiagnosticMissionLifecycleState>
RunReplaySession::diagnostic_mission_lifecycle_state() const noexcept
{
    return diagnostic_mission_lifecycle_state_;
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

bool RunReplaySession::front_end_active_character_is_confirmed() const noexcept
{
    return front_end_active_character_is_confirmed_;
}
} // namespace omega::app
