#include "run_replay_session.h"

#include "omega/runtime/input_tracker.h"
#include "omega/runtime/run_capture_session.h"
#include "omega/simulation/entity_registry.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace replay_session_test_allocation
{
inline constexpr std::size_t kDisabled = std::numeric_limits<std::size_t>::max();
std::size_t allocations_before_failure = kDisabled;

void Arm(const std::size_t allocations_to_allow) noexcept
{
    allocations_before_failure = allocations_to_allow;
}

void Disarm() noexcept
{
    allocations_before_failure = kDisabled;
}
} // namespace replay_session_test_allocation

void* operator new(const std::size_t size)
{
    if (replay_session_test_allocation::allocations_before_failure !=
        replay_session_test_allocation::kDisabled)
    {
        if (replay_session_test_allocation::allocations_before_failure == 0U)
        {
            replay_session_test_allocation::Disarm();
            throw std::bad_alloc{};
        }
        --replay_session_test_allocation::allocations_before_failure;
    }
    if (void* const memory = std::malloc(size == 0U ? 1U : size))
        return memory;
    throw std::bad_alloc{};
}

void operator delete(void* const memory) noexcept
{
    std::free(memory);
}

void operator delete(void* const memory, const std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using omega::app::FrontEndCapabilities;
using omega::app::FrontEndCharacterSlot;
using omega::app::FrontEndMode;
using omega::app::FrontEndMainRow;
using omega::app::FrontEndCommand;
using omega::app::FrontEndCommandType;
using omega::app::FrontEndProfileSlot;
using omega::app::FrontEndState;
using omega::app::RunReplayError;
using omega::app::RunReplayErrorCode;
using omega::app::RunReplayFrame;
using omega::app::RunReplayOperation;
using omega::app::RunReplaySession;
using omega::app::RunReplaySessionConfig;
using omega::app::RunReplaySessionState;
using omega::gameplay::DiagnosticProximityTriggerState;
using omega::gameplay::DiagnosticMissionLifecycleState;
using omega::gameplay::DiagnosticMissionStatus;
using omega::gameplay::DiagnosticTargetFireState;
using omega::runtime::FramePlan;
using omega::runtime::FrameScheduler;
using omega::runtime::FrameSchedulerState;
using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputTracker;
using omega::runtime::PointerPositionQ16;
using omega::runtime::RenderTargetRectQ16;
using omega::runtime::RunCaptureReplayErrorCode;
using omega::runtime::RunCaptureReplayOperation;
using omega::runtime::RunCaptureSession;
using omega::runtime::RunCaptureSessionConfig;
using omega::runtime::RunCaptureTerminalInput;
using omega::runtime::RunCaptureTracePair;
using omega::simulation::Position3;
using omega::simulation::SimulationState;
using omega::simulation::SimulationStepResult;
using omega::simulation::SimulationWorld;

template <typename T>
concept HasFrontEndActiveProfileConfirmationSeed =
    requires(T value) { value.front_end_active_profile_is_confirmed; };

template <typename T>
concept HasFrontEndActiveCharacterConfirmationSeed =
    requires(T value) { value.front_end_active_character_is_confirmed; };

int failures = 0;

constexpr RenderTargetRectQ16 kOriginMarkerDestination{
    .left = 31'744U,
    .top = 31'744U,
    .right = 33'792U,
    .bottom = 33'792U,
};
constexpr RenderTargetRectQ16 kForwardOneMarkerDestination{
    .left = 31'744U,
    .top = 30'720U,
    .right = 33'792U,
    .bottom = 32'768U,
};
constexpr RenderTargetRectQ16 kForwardTwoMarkerDestination{
    .left = 31'744U,
    .top = 29'696U,
    .right = 33'792U,
    .bottom = 31'744U,
};

// FrontEndState's default is the canonical player-facing Title. Replay fixtures
// that exercise gameplay must opt into gameplay explicitly; relying on a
// default-constructed state made the old synthetic menu contract silently
// change meaning when Title became the canonical startup surface.
constexpr FrontEndState kGameplayFrontEndState{
    .mode = FrontEndMode::Gameplay,
    .selected_main_row = FrontEndMainRow::CreateAgent,
};

static_assert(omega::app::PlanProjectDiagnosticActorMarkerDestination(Position3{}) ==
              kOriginMarkerDestination);
static_assert(omega::app::PlanProjectDiagnosticActorMarkerDestination(Position3{.z = 1}) ==
              kForwardOneMarkerDestination);
static_assert(omega::app::PlanProjectDiagnosticActorMarkerDestination(Position3{.z = 2}) ==
              kForwardTwoMarkerDestination);

[[noreturn]] void FailFixture(const std::string_view site) noexcept
{
    std::fputs("FAILED: run replay fixture: ", stderr);
    std::fwrite(site.data(), sizeof(char), site.size(), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::_Exit(EXIT_FAILURE);
}

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
bool CheckError(const std::expected<Value, RunReplayError>& result,
    const RunReplayOperation operation, const RunReplayErrorCode code,
    const std::string_view message,
    const std::optional<RunCaptureReplayErrorCode> replay_code = std::nullopt)
{
    bool matches = !result && result.error().operation == operation &&
                   result.error().code == code &&
                   result.error().message == omega::app::RunReplayErrorMessage(code);
    if (matches && replay_code)
    {
        const auto& nested = result.error().replay_error;
        const RunCaptureReplayOperation replay_operation =
            operation == RunReplayOperation::Create
                ? RunCaptureReplayOperation::Create
                : RunCaptureReplayOperation::Next;
        matches = nested && nested->operation == replay_operation &&
                  nested->code == *replay_code &&
                  nested->message ==
                      omega::runtime::RunCaptureReplayErrorMessage(*replay_code);
    }
    else if (matches)
    {
        matches = !result.error().replay_error;
    }
    Check(matches, message);
    return matches;
}

[[nodiscard]] InputTracker MakeTracker(
    const std::span<const std::uint32_t> actions)
{
    std::vector<InputBinding> bindings;
    bindings.reserve(actions.size());
    for (std::size_t index = 0U; index < actions.size(); ++index)
    {
        bindings.push_back(InputBinding{
            .device = InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(index),
            .action = actions[index],
        });
    }

    auto table = InputBindingTable::FromBindings(bindings);
    if (!table)
        FailFixture("input binding table creation");
    auto tracker = InputTracker::Create(std::move(*table), 16U);
    if (!tracker)
        FailFixture("input tracker creation");
    return std::move(*tracker);
}

[[nodiscard]] bool Push(
    InputTracker& tracker, const std::uint16_t code, const bool pressed)
{
    return tracker
        .PushEvent(InputEvent{
            .device = InputDevice::Keyboard,
            .code = code,
            .pressed = pressed,
        })
        .has_value();
}

struct TerminalReasons
{
    bool host_quit_requested = false;
    bool logical_quit_pressed = false;
};

struct InputTransition
{
    std::uint16_t code = 0U;
    bool pressed = false;
};

struct ScriptedElapsedFrame
{
    nanoseconds elapsed{};
    std::span<const InputTransition> transitions;
    std::optional<PointerPositionQ16> pointer_position;
};

[[nodiscard]] RunCaptureTracePair BuildPair(
    const std::span<const std::uint32_t> actions,
    const std::size_t maximum_frames,
    const std::span<const nanoseconds> elapsed_values,
    const std::optional<TerminalReasons> terminal = std::nullopt,
    const std::uint64_t first_frame_index = 0U)
{
    if ((!elapsed_values.empty() || terminal) && first_frame_index != 0U)
        FailFixture("nonempty fixture requested a nonzero tracker origin");

    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{
            .maximum_frames = maximum_frames,
            .first_frame_index = first_frame_index,
        },
        actions);
    if (!created)
        FailFixture("capture session creation");
    RunCaptureSession capture = std::move(*created);
    InputTracker tracker = MakeTracker(actions);

    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        if (index == 0U && !Push(tracker, 0U, true))
            FailFixture("initial input press");
        if (index == 1U && !Push(tracker, 0U, false))
            FailFixture("input release");

        const auto snapshot = tracker.EndFrame();
        if (!capture.AppendInput(snapshot) ||
            !capture.AppendElapsed(elapsed_values[index]))
        {
            FailFixture("elapsed frame append");
        }
    }

    if (terminal)
    {
        const auto snapshot = tracker.EndFrame();
        if (!capture.AppendInput(snapshot) ||
            !capture.MarkTerminal(terminal->host_quit_requested,
                terminal->logical_quit_pressed))
        {
            FailFixture("terminal frame append");
        }
    }

    auto finished = std::move(capture).Finish();
    if (!finished)
        FailFixture("capture session finish");
    return std::move(*finished);
}

[[nodiscard]] RunCaptureTracePair BuildScriptedPair(
    const std::span<const std::uint32_t> actions,
    const std::span<const ScriptedElapsedFrame> elapsed_frames,
    const std::optional<TerminalReasons> terminal = std::nullopt,
    const std::span<const InputTransition> terminal_transitions = {})
{
    const std::size_t maximum_frames = elapsed_frames.size() + (terminal ? 1U : 0U);
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = maximum_frames}, actions);
    if (!created)
        FailFixture("scripted capture session creation");
    RunCaptureSession capture = std::move(*created);
    InputTracker tracker = MakeTracker(actions);

    for (const ScriptedElapsedFrame& frame : elapsed_frames)
    {
        if (frame.pointer_position &&
            !tracker.SetPointerPosition(*frame.pointer_position))
        {
            FailFixture("scripted pointer position");
        }
        for (const InputTransition transition : frame.transitions)
        {
            if (!Push(tracker, transition.code, transition.pressed))
                FailFixture("scripted elapsed input transition");
        }
        if (!capture.AppendInput(tracker.EndFrame()) ||
            !capture.AppendElapsed(frame.elapsed))
        {
            FailFixture("scripted elapsed frame append");
        }
    }

    if (terminal)
    {
        for (const InputTransition transition : terminal_transitions)
        {
            if (!Push(tracker, transition.code, transition.pressed))
                FailFixture("scripted terminal input transition");
        }
        if (!capture.AppendInput(tracker.EndFrame()) ||
            !capture.MarkTerminal(
                terminal->host_quit_requested, terminal->logical_quit_pressed))
        {
            FailFixture("scripted terminal frame append");
        }
    }

    auto finished = std::move(capture).Finish();
    if (!finished)
        FailFixture("scripted capture session finish");
    return std::move(*finished);
}

[[nodiscard]] RunReplaySessionConfig ValidConfig()
{
    return RunReplaySessionConfig{
        .scheduler = {
            .simulation_step = milliseconds{10},
            .max_steps_per_frame = 2U,
            .max_frame_delta = milliseconds{25},
        },
        .maximum_entities = 1U,
    };
}

[[nodiscard]] RunReplaySessionConfig DiagnosticMissionConfig()
{
    RunReplaySessionConfig config = ValidConfig();
    config.scheduler.max_steps_per_frame = 6U;
    config.scheduler.max_frame_delta = milliseconds{60};
    config.enable_debug_locomotion = true;
    config.enable_debug_target_fire = true;
    config.enable_debug_mission_lifecycle = true;
    config.initial_front_end_state = FrontEndState{
        .mode = FrontEndMode::BriefingRoom,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
    };
    config.front_end_capabilities.can_start_diagnostic_campaign = true;
    config.front_end_capabilities.supports_character_selection = true;
    return config;
}

[[nodiscard]] RunReplaySession TakeSession(
    std::expected<RunReplaySession, RunReplayError>& created,
    const std::string_view message)
{
    if (!created)
        FailFixture(message);
    return std::move(*created);
}

struct PairObservation
{
    std::uint64_t input_first = 0U;
    std::size_t input_capacity = 0U;
    std::size_t input_frames = 0U;
    std::size_t action_count = 0U;
    std::uint64_t elapsed_first = 0U;
    std::size_t elapsed_capacity = 0U;
    std::size_t elapsed_frames = 0U;
    std::optional<RunCaptureTerminalInput> terminal;

    friend bool operator==(const PairObservation&, const PairObservation&) = default;
};

[[nodiscard]] PairObservation ObservePair(const RunCaptureTracePair& pair) noexcept
{
    return PairObservation{
        .input_first = pair.input_trace().first_frame_index(),
        .input_capacity = pair.input_trace().maximum_frames(),
        .input_frames = pair.input_trace().frame_count(),
        .action_count = pair.input_trace().actions().size(),
        .elapsed_first = pair.scheduler_elapsed_trace().first_frame_index(),
        .elapsed_capacity = pair.scheduler_elapsed_trace().maximum_frames(),
        .elapsed_frames = pair.scheduler_elapsed_trace().frame_count(),
        .terminal = pair.terminal_input(),
    };
}

[[nodiscard]] bool SamePlan(const FramePlan& left, const FramePlan& right) noexcept
{
    return left.simulation_steps == right.simulation_steps &&
           left.interpolation_alpha == right.interpolation_alpha &&
           left.clamped_delta == right.clamped_delta &&
           left.dropped_time == right.dropped_time;
}

[[nodiscard]] bool SameSimulation(
    const SimulationState& left, const SimulationState& right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
}

[[nodiscard]] bool SameSimulation(
    const std::optional<SimulationState>& left,
    const std::optional<SimulationState>& right) noexcept
{
    return left.has_value() == right.has_value() &&
           (!left || SameSimulation(*left, *right));
}

[[nodiscard]] bool SameDiagnosticProximityTrigger(
    const std::optional<DiagnosticProximityTriggerState>& state,
    const bool inside, const bool objective_complete) noexcept
{
    return state && state->inside == inside &&
           state->objective_complete == objective_complete;
}

[[nodiscard]] bool SameDiagnosticTargetFire(
    const std::optional<DiagnosticTargetFireState>& state,
    const bool acquired, const bool target_complete) noexcept
{
    return state && *state == DiagnosticTargetFireState{
                                 .acquired = acquired,
                                 .target_complete = target_complete,
                             };
}

[[nodiscard]] bool SameDiagnosticMissionLifecycle(
    const std::optional<DiagnosticMissionLifecycleState>& state,
    const DiagnosticMissionStatus status) noexcept
{
    return state && *state == DiagnosticMissionLifecycleState{.status = status};
}

[[nodiscard]] bool SameDiagnosticProximityTrigger(
    const std::optional<DiagnosticProximityTriggerState>& left,
    const std::optional<DiagnosticProximityTriggerState>& right) noexcept
{
    if (left.has_value() != right.has_value())
        return false;
    return !left || (left->inside == right->inside &&
                        left->objective_complete == right->objective_complete);
}

void CheckContractAndTaxonomy()
{
    static_assert(!std::is_default_constructible_v<RunReplaySession>);
    static_assert(!std::is_copy_constructible_v<RunReplaySession>);
    static_assert(!std::is_copy_assignable_v<RunReplaySession>);
    static_assert(std::is_nothrow_move_constructible_v<RunReplaySession>);
    static_assert(!std::is_move_assignable_v<RunReplaySession>);
    static_assert(std::is_nothrow_destructible_v<RunReplaySession>);
    static_assert(!noexcept(RunReplaySession::Create(
        std::declval<RunCaptureTracePair&&>(), RunReplaySessionConfig{})));
    static_assert(noexcept(std::declval<RunReplaySession&>().Next()));
    static_assert(noexcept(std::declval<const RunReplaySession&>().state()));
    static_assert(noexcept(
        std::declval<const RunReplaySession&>().remaining_frames()));
    static_assert(noexcept(
        std::declval<const RunReplaySession&>().scheduler_state()));
    static_assert(noexcept(
        std::declval<const RunReplaySession&>().simulation_state()));
    static_assert(noexcept(
        std::declval<const RunReplaySession&>().debug_locomotion_position()));
    static_assert(noexcept(std::declval<const RunReplaySession&>()
                               .diagnostic_proximity_trigger_state()));
    static_assert(noexcept(std::declval<const RunReplaySession&>()
                               .diagnostic_target_fire_state()));
    static_assert(noexcept(std::declval<const RunReplaySession&>()
                               .diagnostic_mission_lifecycle_state()));
    static_assert(noexcept(std::declval<const RunReplaySession&>()
                               .diagnostic_actor_marker_destination()));
    static_assert(noexcept(
        std::declval<const RunReplaySession&>().front_end_state()));
    static_assert(noexcept(std::declval<const RunReplaySession&>()
                               .front_end_active_profile_is_confirmed()));
    static_assert(noexcept(std::declval<const RunReplaySession&>()
                               .front_end_active_character_is_confirmed()));

    static_assert(!std::is_aggregate_v<RunReplayFrame>);
    static_assert(!std::is_default_constructible_v<RunReplayFrame>);
    static_assert(!std::is_copy_constructible_v<RunReplayFrame>);
    static_assert(!std::is_copy_assignable_v<RunReplayFrame>);
    static_assert(std::is_nothrow_move_constructible_v<RunReplayFrame>);
    static_assert(!std::is_move_assignable_v<RunReplayFrame>);
    static_assert(noexcept(std::declval<const RunReplayFrame&>().input()));
    static_assert(noexcept(std::declval<const RunReplayFrame&>().elapsed()));
    static_assert(noexcept(
        std::declval<const RunReplayFrame&>().terminal_input()));
    static_assert(noexcept(std::declval<const RunReplayFrame&>().frame_plan()));
    static_assert(noexcept(std::declval<const RunReplayFrame&>().front_end_command()));

    using CreateResult = decltype(RunReplaySession::Create(
        std::declval<RunCaptureTracePair&&>(), RunReplaySessionConfig{}));
    using NextResult = decltype(std::declval<RunReplaySession&>().Next());
    static_assert(std::is_same_v<CreateResult,
        std::expected<RunReplaySession, RunReplayError>>);
    static_assert(std::is_same_v<NextResult,
        std::expected<RunReplayFrame, RunReplayError>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>().scheduler_state()),
        std::optional<FrameSchedulerState>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>().simulation_state()),
        std::optional<SimulationState>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>().debug_locomotion_position()),
        std::optional<Position3>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>()
                     .diagnostic_proximity_trigger_state()),
        std::optional<DiagnosticProximityTriggerState>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>()
                     .diagnostic_target_fire_state()),
        std::optional<DiagnosticTargetFireState>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>()
                     .diagnostic_mission_lifecycle_state()),
        std::optional<DiagnosticMissionLifecycleState>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>()
                     .diagnostic_actor_marker_destination()),
        std::optional<RenderTargetRectQ16>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>().front_end_state()),
        std::optional<FrontEndState>>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>()
                     .front_end_active_profile_is_confirmed()),
        bool>);
    static_assert(std::is_same_v<
        decltype(std::declval<const RunReplaySession&>()
                     .front_end_active_character_is_confirmed()),
        bool>);
    static_assert(std::is_nothrow_move_constructible_v<CreateResult>);
    static_assert(std::is_nothrow_move_constructible_v<NextResult>);

    constexpr RunReplaySessionConfig default_config;
    static_assert(default_config.scheduler == omega::runtime::FrameSchedulerConfig{});
    static_assert(default_config.maximum_entities == 65'536U);
    static_assert(!default_config.enable_debug_locomotion);
    static_assert(!default_config.enable_debug_target_fire);
    static_assert(!default_config.enable_debug_mission_lifecycle);
    static_assert(!default_config.initial_front_end_state);
    static_assert(default_config.front_end_visible_profile_slots == 0U);
    static_assert(default_config.front_end_total_profile_count == 0U);
    static_assert(default_config.front_end_visible_character_slots_by_profile ==
                  std::array<std::uint8_t,
                      omega::app::kFrontEndVisibleProfiles>{});
    static_assert(default_config.front_end_total_character_counts_by_profile ==
                  std::array<std::size_t,
                      omega::app::kFrontEndVisibleProfiles>{});
    static_assert(!default_config.front_end_capabilities.can_create_first_profile);
    static_assert(!default_config.front_end_capabilities
                       .can_start_diagnostic_campaign);
    static_assert(!default_config.front_end_capabilities
                       .requires_active_profile_for_diagnostic_play);
    static_assert(!default_config.front_end_capabilities
                       .supports_character_selection);
    static_assert(!default_config.front_end_capabilities
                       .can_create_first_character);
    static_assert(!default_config.front_end_capabilities
                       .requires_active_character_for_diagnostic_play);
    static_assert(
        !HasFrontEndActiveProfileConfirmationSeed<RunReplaySessionConfig>);
    static_assert(
        !HasFrontEndActiveCharacterConfirmationSeed<RunReplaySessionConfig>);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::SimulationRepresentationExhausted) == 7);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DebugLocomotionEntityCreateFailed) == 8);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DebugLocomotionPlanFailed) == 9);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DiagnosticProximityTriggerFailed) == 10);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DiagnosticTargetFireFailed) == 11);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::InvalidDiagnosticMissionLifecycleConfig) ==
                  12);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DiagnosticMissionLifecycleFailed) == 13);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DiagnosticMissionPositionResetFailed) ==
                  14);

    struct ErrorContract
    {
        RunReplayErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array contracts{
        ErrorContract{RunReplayErrorCode::InvalidSchedulerConfig,
            "invalid-scheduler-config",
            "run replay scheduler configuration is invalid"},
        ErrorContract{RunReplayErrorCode::InvalidEntityCapacity,
            "invalid-entity-capacity", "run replay entity capacity is invalid"},
        ErrorContract{RunReplayErrorCode::SimulationWorldCreateFailed,
            "simulation-world-create-failed",
            "run replay simulation world creation failed"},
        ErrorContract{RunReplayErrorCode::ReplayCreateFailed,
            "replay-create-failed", "run replay capture replay creation failed"},
        ErrorContract{RunReplayErrorCode::ReplayNextFailed,
            "replay-next-failed", "run replay capture replay advance failed"},
        ErrorContract{RunReplayErrorCode::ReplayComplete,
            "replay-complete", "run replay is complete"},
        ErrorContract{RunReplayErrorCode::InvalidSessionState,
            "invalid-session-state", "run replay session state is invalid"},
        ErrorContract{RunReplayErrorCode::SimulationRepresentationExhausted,
            "simulation-representation-exhausted",
            "run replay simulation representation is exhausted"},
        ErrorContract{RunReplayErrorCode::DebugLocomotionEntityCreateFailed,
            "debug-locomotion-entity-create-failed",
            "run replay debug locomotion entity creation failed"},
        ErrorContract{RunReplayErrorCode::DebugLocomotionPlanFailed,
            "debug-locomotion-plan-failed",
            "run replay debug locomotion planning failed"},
        ErrorContract{RunReplayErrorCode::DiagnosticProximityTriggerFailed,
            "diagnostic-proximity-trigger-failed",
            "run replay diagnostic proximity trigger failed"},
        ErrorContract{RunReplayErrorCode::DiagnosticTargetFireFailed,
            "diagnostic-target-fire-failed",
            "run replay diagnostic target fire failed"},
        ErrorContract{
            RunReplayErrorCode::InvalidDiagnosticMissionLifecycleConfig,
            "invalid-diagnostic-mission-lifecycle-config",
            "run replay diagnostic mission lifecycle configuration is invalid"},
        ErrorContract{RunReplayErrorCode::DiagnosticMissionLifecycleFailed,
            "diagnostic-mission-lifecycle-failed",
            "run replay diagnostic mission lifecycle evaluation failed"},
        ErrorContract{RunReplayErrorCode::DiagnosticMissionPositionResetFailed,
            "diagnostic-mission-position-reset-failed",
            "run replay diagnostic mission actor reset failed"},
    };
    for (const auto& contract : contracts)
    {
        Check(omega::app::RunReplayErrorCodeName(contract.code) == contract.name &&
                  omega::app::RunReplayErrorMessage(contract.code) == contract.message,
            "every replay-session error has fixed public text");
    }

    Check(omega::app::RunReplayOperationName(RunReplayOperation::Create) ==
                  "create" &&
              omega::app::RunReplayOperationName(RunReplayOperation::Next) == "next",
        "replay-session operation names are fixed");
    Check(omega::app::RunReplaySessionStateName(RunReplaySessionState::Inert) ==
                  "inert" &&
              omega::app::RunReplaySessionStateName(RunReplaySessionState::Ready) ==
                  "ready" &&
              omega::app::RunReplaySessionStateName(
                  RunReplaySessionState::Complete) == "complete" &&
              omega::app::RunReplaySessionStateName(RunReplaySessionState::Failed) ==
                  "failed",
        "all replay-session state names are fixed");

    const auto unknown_operation = static_cast<RunReplayOperation>(255);
    const auto unknown_error = static_cast<RunReplayErrorCode>(255);
    const auto unknown_state = static_cast<RunReplaySessionState>(255);
    Check(omega::app::RunReplayOperationName(unknown_operation) == "unknown" &&
              omega::app::RunReplayErrorCodeName(unknown_error) == "unknown" &&
              omega::app::RunReplayErrorMessage(unknown_error) ==
                  "run replay error is unknown" &&
              omega::app::RunReplaySessionStateName(unknown_state) == "unknown",
        "unknown replay-session enum values fail closed to fixed text");

    constexpr std::uint64_t bounded_steps =
        static_cast<std::uint64_t>(omega::runtime::kMaximumRunCaptureSessionFrames) *
        omega::runtime::kMaximumStepsPerFrame;
    static_assert(bounded_steps == 4'194'304U);
    static_assert(bounded_steps < std::numeric_limits<std::uint64_t>::max());
    static_assert(
        bounded_steps <= static_cast<std::uint64_t>(
                             std::numeric_limits<std::int64_t>::max() /
                             omega::runtime::kMaximumSimulationStep.count()));
}

void CheckCreatePriorityAndPairRetention()
{
    constexpr std::array<std::uint32_t, 1U> actions{7U};
    constexpr std::array<nanoseconds, 1U> elapsed{milliseconds{5}};

    RunCaptureTracePair invalid_source = BuildPair(actions, 1U, elapsed);
    RunCaptureTracePair retained_owner = std::move(invalid_source);
    const PairObservation inert_observation = ObservePair(invalid_source);

    RunReplaySessionConfig invalid_scheduler{};
    invalid_scheduler.maximum_entities = 0U;
    auto scheduler_priority =
        RunReplaySession::Create(std::move(invalid_source), invalid_scheduler);
    CheckError(scheduler_priority, RunReplayOperation::Create,
        RunReplayErrorCode::InvalidSchedulerConfig,
        "scheduler validation precedes capacity and replay validation");
    Check(ObservePair(invalid_source) == inert_observation,
        "scheduler rejection leaves observable pair metadata unchanged");

    RunReplaySessionConfig invalid_capacity = ValidConfig();
    invalid_capacity.maximum_entities = 0U;
    auto capacity_priority =
        RunReplaySession::Create(std::move(invalid_source), invalid_capacity);
    CheckError(capacity_priority, RunReplayOperation::Create,
        RunReplayErrorCode::InvalidEntityCapacity,
        "capacity validation precedes world creation and replay validation");
    Check(ObservePair(invalid_source) == inert_observation,
        "capacity rejection leaves observable pair metadata unchanged");

    replay_session_test_allocation::Arm(0U);
    auto world_priority =
        RunReplaySession::Create(std::move(invalid_source), ValidConfig());
    replay_session_test_allocation::Disarm();
    CheckError(world_priority, RunReplayOperation::Create,
        RunReplayErrorCode::SimulationWorldCreateFailed,
        "world creation precedes replay validation");
    Check(ObservePair(invalid_source) == inert_observation,
        "world failure leaves observable pair metadata unchanged");

    auto replay_failure =
        RunReplaySession::Create(std::move(invalid_source), ValidConfig());
    CheckError(replay_failure, RunReplayOperation::Create,
        RunReplayErrorCode::ReplayCreateFailed,
        "replay creation retains its exact nested leaf error",
        RunCaptureReplayErrorCode::InvalidInputTrace);
    Check(ObservePair(invalid_source) == inert_observation,
        "replay rejection leaves observable pair metadata unchanged");
    auto retained_created =
        RunReplaySession::Create(std::move(retained_owner), ValidConfig());
    Check(retained_created.has_value(),
        "the independently retained valid owner remains replayable");
    Check(ObservePair(retained_owner) == PairObservation{},
        "successful creation transfers the complete capture-pair owner");

    RunCaptureTracePair scheduler_pair = BuildPair(actions, 1U, elapsed);
    const PairObservation scheduler_before = ObservePair(scheduler_pair);
    auto scheduler_failure =
        RunReplaySession::Create(std::move(scheduler_pair), RunReplaySessionConfig{});
    CheckError(scheduler_failure, RunReplayOperation::Create,
        RunReplayErrorCode::InvalidSchedulerConfig,
        "a valid pair receives the fixed scheduler rejection");
    Check(ObservePair(scheduler_pair) == scheduler_before,
        "scheduler rejection preserves observable pair metadata");
    auto scheduler_retry =
        RunReplaySession::Create(std::move(scheduler_pair), ValidConfig());
    Check(scheduler_retry.has_value(),
        "the scheduler-rejected pair succeeds on exact retry");

    RunCaptureTracePair capacity_pair = BuildPair(actions, 1U, elapsed);
    const PairObservation capacity_before = ObservePair(capacity_pair);
    invalid_capacity = ValidConfig();
    invalid_capacity.maximum_entities =
        omega::simulation::EntityRegistry::kMaximumCapacity + 1U;
    auto capacity_failure =
        RunReplaySession::Create(std::move(capacity_pair), invalid_capacity);
    CheckError(capacity_failure, RunReplayOperation::Create,
        RunReplayErrorCode::InvalidEntityCapacity,
        "an above-limit entity capacity is rejected exactly");
    Check(ObservePair(capacity_pair) == capacity_before,
        "capacity rejection preserves observable pair metadata");
    auto capacity_retry =
        RunReplaySession::Create(std::move(capacity_pair), ValidConfig());
    Check(capacity_retry.has_value(),
        "the capacity-rejected pair succeeds on exact retry");

    RunCaptureTracePair world_pair = BuildPair(actions, 1U, elapsed);
    const PairObservation world_before = ObservePair(world_pair);
    replay_session_test_allocation::Arm(0U);
    auto world_failure =
        RunReplaySession::Create(std::move(world_pair), ValidConfig());
    replay_session_test_allocation::Disarm();
    CheckError(world_failure, RunReplayOperation::Create,
        RunReplayErrorCode::SimulationWorldCreateFailed,
        "a deterministic world allocation failure maps to its fixed category");
    Check(ObservePair(world_pair) == world_before,
        "world allocation failure preserves observable pair metadata");
    auto world_retry = RunReplaySession::Create(std::move(world_pair), ValidConfig());
    Check(world_retry.has_value(),
        "the world-allocation-rejected pair succeeds on exact retry");
}

void CheckEmptyAndMaximumOrigin()
{
    constexpr std::array<std::uint32_t, 1U> actions{9U};
    const std::span<const nanoseconds> no_elapsed;
    RunCaptureTracePair pair = BuildPair(actions, 1U, no_elapsed, std::nullopt,
        std::numeric_limits<std::uint64_t>::max());
    auto created = RunReplaySession::Create(std::move(pair), ValidConfig());
    RunReplaySession session =
        TakeSession(created, "the empty maximum-origin replay is created");

    const auto scheduler = session.scheduler_state();
    const auto simulation = session.simulation_state();
    Check(session.state() == RunReplaySessionState::Complete &&
              session.remaining_frames() == 0U && scheduler && simulation &&
              *scheduler == FrameSchedulerState{
                                .config = ValidConfig().scheduler,
                            } &&
              SameSimulation(*simulation, SimulationState{}),
        "an empty maximum-origin pair begins complete with fresh owned state");

    const auto completed = session.Next();
    CheckError(completed, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayComplete,
        "an empty replay reports stable completion");
    Check(session.state() == RunReplaySessionState::Complete &&
              session.remaining_frames() == 0U &&
              session.scheduler_state() == scheduler &&
              SameSimulation(session.simulation_state(), simulation),
        "completion cannot mutate the empty replay owners");
}

void CheckElapsedOracleAndPartialPrefix()
{
    constexpr std::array<std::uint32_t, 1U> actions{11U};
    constexpr std::array elapsed_values{
        nanoseconds::min(), nanoseconds{-1}, nanoseconds{milliseconds{5}},
        nanoseconds{milliseconds{25}}, nanoseconds{milliseconds{26}},
    };
    RunCaptureTracePair pair = BuildPair(actions, 8U, elapsed_values);
    const RunReplaySessionConfig config = ValidConfig();
    auto created = RunReplaySession::Create(std::move(pair), config);
    RunReplaySession session =
        TakeSession(created, "the elapsed oracle replay is created");

    auto reference_scheduler = FrameScheduler::Create(config.scheduler);
    auto reference_world = SimulationWorld::Create({
        .fixed_step = config.scheduler.simulation_step,
        .maximum_entities = config.maximum_entities,
    });
    if (!reference_scheduler || !reference_world)
        FailFixture("elapsed reference owners");

    struct ExpectedFrame
    {
        std::uint32_t steps;
        double alpha;
        bool clamped;
        bool dropped;
        nanoseconds remainder;
        std::uint64_t total_steps;
        nanoseconds total_dropped;
        std::uint64_t world_steps;
        nanoseconds world_time;
    };
    constexpr std::array expected{
        ExpectedFrame{0U, 0.0, false, false, nanoseconds{0}, 0U,
            nanoseconds{0}, 0U, nanoseconds{0}},
        ExpectedFrame{0U, 0.0, false, false, nanoseconds{0}, 0U,
            nanoseconds{0}, 0U, nanoseconds{0}},
        ExpectedFrame{0U, 0.5, false, false, milliseconds{5}, 0U,
            nanoseconds{0}, 0U, nanoseconds{0}},
        ExpectedFrame{2U, 0.0, false, true, nanoseconds{0}, 2U,
            milliseconds{10}, 2U, milliseconds{20}},
        ExpectedFrame{2U, 0.5, true, false, milliseconds{5}, 4U,
            milliseconds{10}, 4U, milliseconds{40}},
    };

    const auto initial_scheduler = session.scheduler_state();
    const auto initial_simulation = session.simulation_state();
    Check(session.state() == RunReplaySessionState::Ready &&
              session.remaining_frames() == elapsed_values.size(),
        "a partial prefix begins ready with only its recorded frame count");

    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        const FramePlan reference_plan =
            reference_scheduler->BeginFrame(elapsed_values[index]);
        for (std::uint32_t step = 0U; step < reference_plan.simulation_steps; ++step)
        {
            if (reference_world->AdvanceOneStep() != SimulationStepResult::Advanced)
                FailFixture("elapsed reference world advance");
        }

        auto frame = session.Next();
        if (!frame)
            FailFixture("elapsed replay frame publication");
        const auto plan = frame->frame_plan();
        const ExpectedFrame& wanted = expected[index];
        Check(frame->input().frame_index() == index &&
                  frame->elapsed() == elapsed_values[index] &&
                  !frame->terminal_input() && plan &&
                  SamePlan(*plan, reference_plan) &&
                  plan->simulation_steps == wanted.steps &&
                  plan->interpolation_alpha == wanted.alpha &&
                  plan->clamped_delta == wanted.clamped &&
                  plan->dropped_time == wanted.dropped,
            "an elapsed frame publishes exact input, timing, and fixed-step plan");

        const auto scheduler = session.scheduler_state();
        const auto simulation = session.simulation_state();
        Check(scheduler &&
                  *scheduler == FrameSchedulerState{
                                    .config = config.scheduler,
                                    .accumulated_remainder = wanted.remainder,
                                    .total_planned_steps = wanted.total_steps,
                                    .total_dropped_time = wanted.total_dropped,
                                } &&
                  *scheduler == reference_scheduler->Snapshot(),
            "each elapsed frame publishes the exact owned scheduler snapshot");
        Check(simulation &&
                  SameSimulation(*simulation,
                      SimulationState{
                          .completed_steps = wanted.world_steps,
                          .simulated_time = wanted.world_time,
                      }) &&
                  SameSimulation(*simulation, reference_world->Snapshot()),
            "each elapsed frame publishes the exact owned simulation snapshot");

        const std::size_t remaining = elapsed_values.size() - index - 1U;
        const RunReplaySessionState wanted_state = remaining == 0U
            ? RunReplaySessionState::Complete
            : RunReplaySessionState::Ready;
        Check(session.state() == wanted_state &&
                  session.remaining_frames() == remaining,
            "one successful elapsed publication advances the cursor exactly once");
    }

    Check(initial_scheduler && initial_simulation &&
              *initial_scheduler == FrameSchedulerState{
                                        .config = config.scheduler,
                                    } &&
              SameSimulation(*initial_simulation, SimulationState{}),
        "earlier owned snapshots remain unchanged after later replay advancement");
}

void CheckTerminalCase(const std::span<const nanoseconds> elapsed_values,
    const TerminalReasons reasons, const std::string_view message)
{
    constexpr std::array<std::uint32_t, 1U> actions{13U};
    RunCaptureTracePair pair = BuildPair(
        actions, elapsed_values.size() + 1U, elapsed_values, reasons);
    auto created = RunReplaySession::Create(std::move(pair), ValidConfig());
    RunReplaySession session = TakeSession(created, message);

    for (const nanoseconds elapsed : elapsed_values)
    {
        auto prefix = session.Next();
        if (!prefix)
            FailFixture("terminal prefix publication");
        Check(prefix->elapsed() == elapsed && !prefix->terminal_input() &&
                  prefix->frame_plan().has_value(),
            "a terminal prefix remains a normal elapsed publication");
    }

    const auto scheduler_before = session.scheduler_state();
    const auto simulation_before = session.simulation_state();
    Check(session.state() == RunReplaySessionState::Ready &&
              session.remaining_frames() == 1U,
        "a terminal frame remains pending after its elapsed prefix");

    auto terminal = session.Next();
    if (!terminal)
        FailFixture("terminal publication");
    Check(terminal->input().frame_index() == elapsed_values.size() &&
              !terminal->elapsed() && !terminal->frame_plan() &&
              terminal->terminal_input() == RunCaptureTerminalInput{
                                                  .frame_index =
                                                      elapsed_values.size(),
                                                  .host_quit_requested =
                                                      reasons.host_quit_requested,
                                                  .logical_quit_pressed =
                                                      reasons.logical_quit_pressed,
                                              },
        message);
    Check(session.state() == RunReplaySessionState::Complete &&
              session.remaining_frames() == 0U &&
              session.scheduler_state() == scheduler_before &&
              SameSimulation(session.simulation_state(), simulation_before),
        "terminal consumption completes without scheduler or world mutation");

    const auto complete = session.Next();
    CheckError(complete, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayComplete,
        "terminal completion remains stable on repeated advance");
}

void CheckTerminalBehavior()
{
    const std::span<const nanoseconds> no_elapsed;
    CheckTerminalCase(no_elapsed,
        TerminalReasons{.host_quit_requested = true},
        "a host-only terminal frame publishes exact owned control data");

    constexpr std::array<nanoseconds, 1U> logical_prefix{milliseconds{5}};
    CheckTerminalCase(logical_prefix,
        TerminalReasons{.logical_quit_pressed = true},
        "a logical-only terminal frame publishes exact owned control data");

    constexpr std::array<nanoseconds, 2U> both_prefix{
        milliseconds{5}, milliseconds{25}};
    CheckTerminalCase(both_prefix,
        TerminalReasons{
            .host_quit_requested = true,
            .logical_quit_pressed = true,
        },
        "a dual-reason terminal frame publishes exact owned control data");
}

void CheckDebugLocomotionOptIn()
{
    constexpr std::array<std::uint32_t, 4U> actions{
        omega::app::kDebugMoveForwardAction,
        omega::app::kDebugMoveBackwardAction,
        omega::app::kDebugMoveLeftAction,
        omega::app::kDebugMoveRightAction,
    };
    constexpr std::array<nanoseconds, 2U> elapsed_values{
        milliseconds{25}, milliseconds{5}};

    RunCaptureTracePair enabled_pair = BuildPair(actions, 3U, elapsed_values,
        TerminalReasons{.host_quit_requested = true});
    RunReplaySessionConfig enabled_config = ValidConfig();
    enabled_config.enable_debug_locomotion = true;
    auto enabled_created =
        RunReplaySession::Create(std::move(enabled_pair), enabled_config);
    RunReplaySession enabled =
        TakeSession(enabled_created, "the debug-locomotion replay is created");

    const auto enabled_initial_state = enabled.simulation_state();
    const auto enabled_initial_position = enabled.debug_locomotion_position();
    const auto enabled_initial_marker =
        enabled.diagnostic_actor_marker_destination();
    Check(enabled_initial_position == Position3{} &&
              enabled_initial_marker == kOriginMarkerDestination &&
              enabled_initial_position &&
              enabled_initial_marker ==
                  omega::app::PlanProjectDiagnosticActorMarkerDestination(
                      *enabled_initial_position) &&
              enabled_initial_state &&
              SameSimulation(*enabled_initial_state, SimulationState{
                  .alive_entities = 1U,
              }),
        "opt-in replay derives the exact origin marker from its positioned synthetic actor");

    auto moved = enabled.Next();
    const auto moved_plan = moved ? moved->frame_plan() : std::nullopt;
    const auto moved_state = enabled.simulation_state();
    const auto moved_position = enabled.debug_locomotion_position();
    const auto moved_marker = enabled.diagnostic_actor_marker_destination();
    Check(moved && moved_plan && moved_plan->simulation_steps == 2U &&
              moved->input().IsHeld(omega::app::kDebugMoveForwardAction) &&
              !moved->input().IsHeld(omega::app::kDebugMoveBackwardAction) &&
              !moved->input().IsHeld(omega::app::kDebugMoveLeftAction) &&
              !moved->input().IsHeld(omega::app::kDebugMoveRightAction) &&
              moved_position == Position3{.z = 2} &&
              moved_marker == kForwardTwoMarkerDestination && moved_position &&
              moved_marker ==
                  omega::app::PlanProjectDiagnosticActorMarkerDestination(
                      *moved_position) &&
              moved_state && SameSimulation(*moved_state, SimulationState{
                  .completed_steps = 2U,
                  .simulated_time = milliseconds{20},
                  .alive_entities = 1U,
              }) &&
              enabled.state() == RunReplaySessionState::Ready,
        "one held command moves both the replay position and its derived marker exactly twice");

    const auto scheduler_before_move = enabled.scheduler_state();
    const auto simulation_before_move = enabled.simulation_state();
    const auto position_before_move = enabled.debug_locomotion_position();
    const auto marker_before_move =
        enabled.diagnostic_actor_marker_destination();
    RunReplaySession moved_enabled = std::move(enabled);
    Check(enabled.state() == RunReplaySessionState::Inert &&
              enabled.remaining_frames() == 0U && !enabled.scheduler_state() &&
              !enabled.simulation_state() && !enabled.debug_locomotion_position() &&
              !enabled.diagnostic_actor_marker_destination(),
        "moving an enabled replay leaves the source inert, positionless, and markerless");
    Check(moved_enabled.state() == RunReplaySessionState::Ready &&
              moved_enabled.remaining_frames() == 2U &&
              moved_enabled.scheduler_state() == scheduler_before_move &&
              SameSimulation(moved_enabled.simulation_state(), simulation_before_move) &&
              moved_enabled.debug_locomotion_position() == position_before_move &&
              moved_enabled.diagnostic_actor_marker_destination() == marker_before_move,
        "moving an enabled replay transfers the world from which the exact marker is derived");

    auto released = moved_enabled.Next();
    const auto released_plan = released ? released->frame_plan() : std::nullopt;
    const auto released_state = moved_enabled.simulation_state();
    const auto released_position = moved_enabled.debug_locomotion_position();
    const auto released_marker =
        moved_enabled.diagnostic_actor_marker_destination();
    Check(released && released_plan && released_plan->simulation_steps == 1U &&
              released->input().WasReleased(omega::app::kDebugMoveForwardAction) &&
              !released->input().IsHeld(omega::app::kDebugMoveForwardAction) &&
              released_position == Position3{.z = 2} &&
              released_marker == kForwardTwoMarkerDestination &&
              released_position &&
              released_marker ==
                  omega::app::PlanProjectDiagnosticActorMarkerDestination(
                      *released_position) &&
              released_state && SameSimulation(*released_state, SimulationState{
                  .completed_steps = 3U,
                  .simulated_time = milliseconds{30},
                  .alive_entities = 1U,
              }) &&
              moved_enabled.state() == RunReplaySessionState::Ready,
        "a released action advances only the clock and preserves the exact marker destination");

    const auto scheduler_before_terminal = moved_enabled.scheduler_state();
    const auto simulation_before_terminal = moved_enabled.simulation_state();
    const auto position_before_terminal = moved_enabled.debug_locomotion_position();
    const auto marker_before_terminal =
        moved_enabled.diagnostic_actor_marker_destination();
    auto terminal = moved_enabled.Next();
    Check(terminal && terminal->terminal_input() && !terminal->elapsed() &&
              !terminal->frame_plan() &&
              !terminal->input().IsHeld(omega::app::kDebugMoveForwardAction) &&
              moved_enabled.state() == RunReplaySessionState::Complete &&
              moved_enabled.scheduler_state() == scheduler_before_terminal &&
              SameSimulation(moved_enabled.simulation_state(), simulation_before_terminal) &&
              moved_enabled.debug_locomotion_position() == position_before_terminal &&
              moved_enabled.diagnostic_actor_marker_destination() ==
                  marker_before_terminal,
        "a terminal frame cannot mutate scheduler, synthetic position, or derived marker");

    constexpr std::array<nanoseconds, 1U> legacy_elapsed{milliseconds{25}};
    RunCaptureTracePair disabled_pair = BuildPair(actions, 1U, legacy_elapsed);
    auto disabled_created =
        RunReplaySession::Create(std::move(disabled_pair), ValidConfig());
    RunReplaySession disabled =
        TakeSession(disabled_created, "the legacy-neutral replay is created");
    auto neutral = disabled.Next();
    const auto disabled_state = disabled.simulation_state();
    Check(neutral && neutral->frame_plan() &&
              neutral->frame_plan()->simulation_steps == 2U &&
              !disabled.debug_locomotion_position() &&
              !disabled.diagnostic_proximity_trigger_state() &&
              !disabled.diagnostic_actor_marker_destination() &&
              disabled_state && SameSimulation(*disabled_state, SimulationState{
                  .completed_steps = 2U,
                  .simulated_time = milliseconds{20},
              }),
        "the default-disabled replay preserves clock-only E0059 behavior for the same schema");
}

void CheckDiagnosticProximityTriggerReplay()
{
    constexpr std::array<std::uint32_t, 1U> actions{
        omega::app::kDebugMoveRightAction,
    };
    constexpr std::array right_down{
        InputTransition{.code = 0U, .pressed = true},
    };
    const std::array path_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{5},
            .transitions = right_down},
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = {}},
        ScriptedElapsedFrame{.elapsed = milliseconds{10},
            .transitions = {}},
        ScriptedElapsedFrame{.elapsed = milliseconds{20},
            .transitions = {}},
        ScriptedElapsedFrame{.elapsed = milliseconds{10},
            .transitions = {}},
    };
    RunReplaySessionConfig path_config = ValidConfig();
    path_config.enable_debug_locomotion = true;
    auto path_created = RunReplaySession::Create(
        BuildScriptedPair(actions, path_frames,
            TerminalReasons{.host_quit_requested = true}),
        path_config);
    RunReplaySession path = TakeSession(
        path_created, "the diagnostic proximity path replay is created");
    Check(path.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  path.diagnostic_proximity_trigger_state(), false, false),
        "an enabled positioned replay creates one armed outside project trigger");

    auto zero_step = path.Next();
    Check(zero_step && zero_step->frame_plan() &&
              zero_step->frame_plan()->simulation_steps == 0U &&
              path.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  path.diagnostic_proximity_trigger_state(), false, false),
        "a zero-step frame cannot sample or mutate the project trigger");

    auto outside = path.Next();
    Check(outside && outside->frame_plan() &&
              outside->frame_plan()->simulation_steps == 2U &&
              path.debug_locomotion_position() == Position3{.x = 2} &&
              SameDiagnosticProximityTrigger(
                  path.diagnostic_proximity_trigger_state(), false, false),
        "successful outside steps preserve an armed incomplete project trigger");

    auto entered = path.Next();
    Check(entered && entered->frame_plan() &&
              entered->frame_plan()->simulation_steps == 1U &&
              path.debug_locomotion_position() == Position3{.x = 3} &&
              SameDiagnosticProximityTrigger(
                  path.diagnostic_proximity_trigger_state(), true, true),
        "the first inclusive-volume sample enters and latches the project objective");

    const auto trigger_before_move =
        path.diagnostic_proximity_trigger_state();
    RunReplaySession moved_path = std::move(path);
    Check(path.state() == RunReplaySessionState::Inert &&
              !path.debug_locomotion_position() &&
              !path.diagnostic_proximity_trigger_state() &&
              moved_path.debug_locomotion_position() == Position3{.x = 3} &&
              SameDiagnosticProximityTrigger(
                  moved_path.diagnostic_proximity_trigger_state(), trigger_before_move),
        "move construction transfers the latched project trigger and leaves the source inert");

    auto remained_inside = moved_path.Next();
    Check(remained_inside && remained_inside->frame_plan() &&
              remained_inside->frame_plan()->simulation_steps == 2U &&
              moved_path.debug_locomotion_position() == Position3{.x = 5} &&
              SameDiagnosticProximityTrigger(
                  moved_path.diagnostic_proximity_trigger_state(), true, true),
        "later inclusive-volume samples remain inside without clearing the objective latch");

    auto exited = moved_path.Next();
    Check(exited && exited->frame_plan() &&
              exited->frame_plan()->simulation_steps == 1U &&
              moved_path.debug_locomotion_position() == Position3{.x = 6} &&
              SameDiagnosticProximityTrigger(
                  moved_path.diagnostic_proximity_trigger_state(), false, true),
        "the first outside sample exits while preserving the completed objective latch");

    const auto trigger_before_terminal =
        moved_path.diagnostic_proximity_trigger_state();
    const auto position_before_terminal = moved_path.debug_locomotion_position();
    auto terminal = moved_path.Next();
    Check(terminal && terminal->terminal_input() && !terminal->frame_plan() &&
              moved_path.state() == RunReplaySessionState::Complete &&
              moved_path.debug_locomotion_position() == position_before_terminal &&
              SameDiagnosticProximityTrigger(
                  moved_path.diagnostic_proximity_trigger_state(),
                  trigger_before_terminal),
        "a terminal frame cannot sample or mutate the project trigger");

    const std::array crossing_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{60},
            .transitions = right_down},
    };
    RunReplaySessionConfig crossing_config = ValidConfig();
    crossing_config.enable_debug_locomotion = true;
    crossing_config.scheduler.max_steps_per_frame = 6U;
    crossing_config.scheduler.max_frame_delta = milliseconds{60};
    auto first_crossing_created = RunReplaySession::Create(
        BuildScriptedPair(actions, crossing_frames), crossing_config);
    auto second_crossing_created = RunReplaySession::Create(
        BuildScriptedPair(actions, crossing_frames), crossing_config);
    RunReplaySession first_crossing = TakeSession(first_crossing_created,
        "the first multi-step crossing replay is created");
    RunReplaySession second_crossing = TakeSession(second_crossing_created,
        "the second multi-step crossing replay is created");
    auto first_crossed = first_crossing.Next();
    auto second_crossed = second_crossing.Next();
    Check(first_crossed && second_crossed && first_crossed->frame_plan() &&
              second_crossed->frame_plan() &&
              first_crossed->frame_plan()->simulation_steps == 6U &&
              SamePlan(*first_crossed->frame_plan(),
                  *second_crossed->frame_plan()) &&
              first_crossing.debug_locomotion_position() == Position3{.x = 6} &&
              first_crossing.debug_locomotion_position() ==
                  second_crossing.debug_locomotion_position() &&
              SameSimulation(first_crossing.simulation_state(),
                  second_crossing.simulation_state()) &&
              first_crossing.state() == second_crossing.state() &&
              SameDiagnosticProximityTrigger(
                  first_crossing.diagnostic_proximity_trigger_state(), false, true) &&
              SameDiagnosticProximityTrigger(
                  first_crossing.diagnostic_proximity_trigger_state(),
                  second_crossing.diagnostic_proximity_trigger_state()),
        "each fixed step is sampled so two independent multi-step crossings deterministically latch enter then exit");

    RunReplaySessionConfig menu_config = ValidConfig();
    menu_config.enable_debug_locomotion = true;
    menu_config.initial_front_end_state = omega::app::InitialFrontEndState();
    const std::array menu_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{25},
            .transitions = right_down},
    };
    auto menu_created = RunReplaySession::Create(
        BuildScriptedPair(actions, menu_frames), menu_config);
    RunReplaySession menu = TakeSession(
        menu_created, "the menu-gated project trigger replay is created");
    auto gated = menu.Next();
    Check(gated && gated->frame_plan() &&
              gated->frame_plan()->simulation_steps == 0U &&
              menu.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  menu.diagnostic_proximity_trigger_state(), false, false),
        "menu-gated elapsed input cannot sample or mutate the project trigger");

    const std::array failure_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{10},
            .transitions = right_down},
    };
    auto failure_created = RunReplaySession::Create(
        BuildScriptedPair(actions, failure_frames), path_config);
    RunReplaySession failure = TakeSession(
        failure_created, "the allocation-failure project trigger replay is created");
    replay_session_test_allocation::Arm(0U);
    const auto failed = failure.Next();
    replay_session_test_allocation::Disarm();
    CheckError(failed, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayNextFailed,
        "a lower replay failure remains fixed before project-trigger advancement",
        RunCaptureReplayErrorCode::AllocationFailed);
    Check(failure.state() == RunReplaySessionState::Ready &&
              failure.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  failure.diagnostic_proximity_trigger_state(), false, false),
        "a replay failure cannot sample, mutate, or duplicate the enabled project trigger");

    RunReplaySessionConfig legacy_config = crossing_config;
    legacy_config.enable_debug_locomotion = false;
    auto legacy_created = RunReplaySession::Create(
        BuildScriptedPair(actions, crossing_frames), legacy_config);
    RunReplaySession legacy = TakeSession(
        legacy_created, "the legacy project-trigger-neutral replay is created");
    auto legacy_crossed = legacy.Next();
    Check(!legacy.diagnostic_proximity_trigger_state() && legacy_crossed &&
              legacy_crossed->frame_plan() &&
              legacy_crossed->frame_plan()->simulation_steps == 6U &&
              !legacy.debug_locomotion_position() &&
              !legacy.diagnostic_proximity_trigger_state(),
        "legacy locomotion-off replay never enables project trigger state");
}

void CheckDiagnosticTargetFireReplay()
{
    constexpr std::array<std::uint32_t, 4U> actions{
        omega::app::kDebugMoveRightAction,
        omega::app::kFrontEndPrimaryAction,
        omega::app::kDebugFireAction,
        omega::app::kDebugTargetAction,
    };
    constexpr PointerPositionQ16 target_center{
        .x = 49'152U,
        .y = 32'768U,
    };
    constexpr PointerPositionQ16 target_inclusive_corner{
        .x = 51'200U,
        .y = 34'816U,
    };
    constexpr std::array crossing_fire_down{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 2U, .pressed = true},
        InputTransition{.code = 3U, .pressed = true},
    };
    constexpr std::array fire_up{
        InputTransition{.code = 2U, .pressed = false},
    };
    constexpr std::array fire_down{
        InputTransition{.code = 2U, .pressed = true},
    };
    const std::array target_frames{
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = crossing_fire_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = fire_up,
            .pointer_position = target_inclusive_corner,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = fire_down,
            .pointer_position = target_inclusive_corner,
        },
    };
    RunReplaySessionConfig target_config = ValidConfig();
    target_config.enable_debug_locomotion = true;
    target_config.enable_debug_target_fire = true;
    target_config.scheduler.max_steps_per_frame = 6U;
    target_config.scheduler.max_frame_delta = milliseconds{60};

    auto target_created = RunReplaySession::Create(
        BuildScriptedPair(actions, target_frames), target_config);
    RunReplaySession target = TakeSession(
        target_created, "the diagnostic target/fire replay is created");
    Check(SameDiagnosticTargetFire(
              target.diagnostic_target_fire_state(), false, false),
        "the explicit target/fire option creates one incomplete unacquired target");

    auto crossing = target.Next();
    Check(crossing && crossing->frame_plan() &&
              crossing->frame_plan()->simulation_steps == 6U &&
              crossing->input().pointer_position() == target_center &&
              crossing->input().WasPressed(omega::app::kDebugFireAction) &&
              crossing->input().IsHeld(omega::app::kDebugTargetAction) &&
              target.debug_locomotion_position() == Position3{.x = 6} &&
              SameDiagnosticProximityTrigger(
                  target.diagnostic_proximity_trigger_state(), false, true) &&
              SameDiagnosticTargetFire(
                  target.diagnostic_target_fire_state(), false, false),
        "one multi-step crossing evaluates fire only once against the incomplete frame-start proximity state");

    auto acquired = target.Next();
    const auto simulation_before_hit = target.simulation_state();
    Check(acquired && acquired->frame_plan() &&
              acquired->frame_plan()->simulation_steps == 0U &&
              acquired->input().pointer_position() == target_inclusive_corner &&
              SameDiagnosticTargetFire(
                  target.diagnostic_target_fire_state(), true, false),
        "a later zero-step frame acquires the exact inclusive target corner after proximity completed earlier");

    auto hit = target.Next();
    Check(hit && hit->frame_plan() &&
              hit->frame_plan()->simulation_steps == 0U &&
              hit->input().pointer_position() == target_inclusive_corner &&
              hit->input().WasPressed(omega::app::kDebugFireAction) &&
              SameSimulation(target.simulation_state(), simulation_before_hit) &&
              SameDiagnosticTargetFire(
                  target.diagnostic_target_fire_state(), false, true),
        "an exact pointer fire edge commits a hit on a successful zero-step frame without mutating simulation");

    RunReplaySessionConfig disabled_config = target_config;
    disabled_config.enable_debug_target_fire = false;
    auto disabled_created = RunReplaySession::Create(
        BuildScriptedPair(actions, target_frames), disabled_config);
    RunReplaySession disabled = TakeSession(
        disabled_created, "the target/fire-disabled compatibility replay is created");
    const auto disabled_first = disabled.Next();
    const auto disabled_second = disabled.Next();
    const auto disabled_third = disabled.Next();
    Check(disabled_first && disabled_second && disabled_third &&
              !disabled.diagnostic_target_fire_state() &&
              disabled.debug_locomotion_position() == Position3{.x = 6} &&
              SameDiagnosticProximityTrigger(
                  disabled.diagnostic_proximity_trigger_state(), false, true),
        "the default-disabled target/fire option preserves existing locomotion and proximity replay behavior");

    constexpr std::array right_target_down{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 3U, .pressed = true},
    };
    constexpr std::array right_up{
        InputTransition{.code = 0U, .pressed = false},
    };
    const std::array post_completion_multi_step_frames{
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = right_target_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = fire_down,
            .pointer_position = target_center,
        },
    };
    auto post_completion_multi_step_created = RunReplaySession::Create(
        BuildScriptedPair(actions, post_completion_multi_step_frames),
        target_config);
    RunReplaySession post_completion_multi_step = TakeSession(
        post_completion_multi_step_created,
        "the post-completion multi-step target/fire replay is created");
    const auto completed_proximity = post_completion_multi_step.Next();
    const auto one_fire_edge = post_completion_multi_step.Next();
    Check(completed_proximity && completed_proximity->frame_plan() &&
              completed_proximity->frame_plan()->simulation_steps == 6U &&
              one_fire_edge && one_fire_edge->frame_plan() &&
              one_fire_edge->frame_plan()->simulation_steps == 6U &&
              post_completion_multi_step.debug_locomotion_position() ==
                  Position3{.x = 12} &&
              SameDiagnosticProximityTrigger(
                  post_completion_multi_step
                      .diagnostic_proximity_trigger_state(),
                  false, true) &&
              SameDiagnosticTargetFire(
                  post_completion_multi_step.diagnostic_target_fire_state(),
                  false, true),
        "one fire edge is evaluated deterministically once before a post-proximity multi-step frame");

    constexpr std::array exit_fire_down{
        InputTransition{.code = 2U, .pressed = true},
        InputTransition{.code = 1U, .pressed = true},
    };
    constexpr std::array exit_controls_up{
        InputTransition{.code = 2U, .pressed = false},
        InputTransition{.code = 1U, .pressed = false},
    };
    const std::array menu_frames{
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = right_target_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = right_up,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = exit_fire_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = exit_controls_up,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = fire_down,
            .pointer_position = target_center,
        },
    };
    RunReplaySessionConfig menu_config = target_config;
    menu_config.initial_front_end_state = kGameplayFrontEndState;
    menu_config.front_end_capabilities.can_start_diagnostic_campaign = true;
    menu_config.front_end_capabilities.supports_character_selection = true;
    auto menu_created = RunReplaySession::Create(
        BuildScriptedPair(actions, menu_frames), menu_config);
    RunReplaySession menu = TakeSession(
        menu_created, "the target/fire menu click-through replay is created");
    const auto menu_crossing = menu.Next();
    const auto menu_acquired = menu.Next();
    const auto acquired_before_exit = menu.diagnostic_target_fire_state();
    const auto left_play = menu.Next();
    const auto target_after_exit = menu.diagnostic_target_fire_state();
    const auto controls_released = menu.Next();
    const auto redeployed = menu.Next();
    Check(menu_crossing && menu_acquired && left_play && controls_released &&
              redeployed &&
              menu_crossing->frame_plan() &&
              menu_crossing->frame_plan()->simulation_steps == 6U &&
              menu_acquired->frame_plan() &&
              menu_acquired->frame_plan()->simulation_steps == 0U &&
              SameDiagnosticTargetFire(acquired_before_exit, true, false) &&
              left_play->frame_plan() &&
              left_play->frame_plan()->simulation_steps == 0U &&
              left_play->input().WasPressed(omega::app::kDebugFireAction) &&
              SameDiagnosticTargetFire(target_after_exit, false, false) &&
              controls_released->frame_plan() &&
              controls_released->frame_plan()->simulation_steps == 0U &&
              redeployed->front_end_command().type ==
                  FrontEndCommandType::StartDiagnosticCampaign &&
              menu.front_end_state() == kGameplayFrontEndState &&
              SameDiagnosticProximityTrigger(
                  menu.diagnostic_proximity_trigger_state(), false, true) &&
              SameDiagnosticTargetFire(
                  menu.diagnostic_target_fire_state(), false, false),
        "post-reduction exit clears an acquired target without hitting, and a later menu fire edge redeploys without click-through");

    const std::array terminal_frames{
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = right_target_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = right_up,
            .pointer_position = target_center,
        },
    };
    auto terminal_created = RunReplaySession::Create(
        BuildScriptedPair(actions, terminal_frames,
            TerminalReasons{.host_quit_requested = true}, fire_down),
        target_config);
    RunReplaySession terminal_source = TakeSession(
        terminal_created, "the target/fire terminal replay is created");
    const auto terminal_crossing = terminal_source.Next();
    const auto terminal_acquired = terminal_source.Next();
    const auto target_before_move =
        terminal_source.diagnostic_target_fire_state();
    RunReplaySession terminal = std::move(terminal_source);
    Check(terminal_crossing && terminal_acquired &&
              terminal_source.state() == RunReplaySessionState::Inert &&
              !terminal_source.diagnostic_target_fire_state() &&
              SameDiagnosticTargetFire(target_before_move, true, false) &&
              terminal.diagnostic_target_fire_state() == target_before_move,
        "move construction transfers acquired target/fire state and leaves the source inert");

    const auto terminal_position = terminal.debug_locomotion_position();
    const auto terminal_trigger = terminal.diagnostic_proximity_trigger_state();
    const std::size_t terminal_remaining = terminal.remaining_frames();
    replay_session_test_allocation::Arm(0U);
    const auto failed_terminal = terminal.Next();
    replay_session_test_allocation::Disarm();
    CheckError(failed_terminal, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayNextFailed,
        "a lower terminal-frame replay failure precedes target/fire mutation",
        RunCaptureReplayErrorCode::AllocationFailed);
    Check(terminal.state() == RunReplaySessionState::Ready &&
              terminal.remaining_frames() == terminal_remaining &&
              terminal.debug_locomotion_position() == terminal_position &&
              SameDiagnosticProximityTrigger(
                  terminal.diagnostic_proximity_trigger_state(), terminal_trigger) &&
              terminal.diagnostic_target_fire_state() == target_before_move,
        "a retryable lower terminal failure leaves acquired target/fire state unchanged");

    auto terminal_frame = terminal.Next();
    Check(terminal_frame && terminal_frame->terminal_input() &&
              !terminal_frame->frame_plan() &&
              terminal_frame->input().pointer_position() == target_center &&
              terminal_frame->input().WasPressed(
                  omega::app::kDebugFireAction) &&
              terminal.state() == RunReplaySessionState::Complete &&
              terminal.debug_locomotion_position() == terminal_position &&
              SameDiagnosticProximityTrigger(
                  terminal.diagnostic_proximity_trigger_state(), terminal_trigger) &&
              terminal.diagnostic_target_fire_state() == target_before_move,
        "a terminal fire frame preserves exact pointer publication without mutating target, trigger, or position state");
}

void CheckDiagnosticMissionLifecycleReplay()
{
    constexpr std::array<std::uint32_t, 5U> actions{
        omega::app::kDebugMoveRightAction,
        omega::app::kFrontEndPrimaryAction,
        omega::app::kFrontEndCancelAction,
        omega::app::kDebugFireAction,
        omega::app::kDebugTargetAction,
    };
    constexpr PointerPositionQ16 target_center{
        .x = 49'152U,
        .y = 32'768U,
    };

    const std::span<const nanoseconds> no_elapsed;
    const auto check_incoherent_config = [&](RunReplaySessionConfig config,
                                               const std::string_view message)
    {
        RunCaptureTracePair pair = BuildPair(actions, 1U, no_elapsed);
        const PairObservation before = ObservePair(pair);
        const auto rejected =
            RunReplaySession::Create(std::move(pair), config);
        CheckError(rejected, RunReplayOperation::Create,
            RunReplayErrorCode::InvalidDiagnosticMissionLifecycleConfig,
            message);
        Check(ObservePair(pair) == before,
            "mission-lifecycle configuration rejection preserves the caller's trace pair");
    };

    const RunReplaySessionConfig coherent_config = DiagnosticMissionConfig();
    RunReplaySessionConfig incoherent_config = coherent_config;
    incoherent_config.enable_debug_locomotion = false;
    check_incoherent_config(incoherent_config,
        "mission lifecycle rejects a missing locomotion and proximity owner");
    incoherent_config = coherent_config;
    incoherent_config.enable_debug_target_fire = false;
    check_incoherent_config(incoherent_config,
        "mission lifecycle rejects a missing target/fire owner");
    incoherent_config = coherent_config;
    incoherent_config.initial_front_end_state.reset();
    check_incoherent_config(incoherent_config,
        "mission lifecycle rejects a missing modal front-end owner");
    incoherent_config = coherent_config;
    incoherent_config.initial_front_end_state->mode =
        static_cast<FrontEndMode>(255U);
    check_incoherent_config(incoherent_config,
        "mission lifecycle rejects an invalid modal front-end state");
    incoherent_config = coherent_config;
    incoherent_config.front_end_capabilities.can_start_diagnostic_campaign =
        false;
    check_incoherent_config(incoherent_config,
        "mission lifecycle rejects a front end that cannot publish deploy");
    incoherent_config = coherent_config;
    incoherent_config.front_end_capabilities.supports_character_selection =
        false;
    check_incoherent_config(incoherent_config,
        "mission lifecycle rejects a front end without the briefing-room route");

    auto ready_created = RunReplaySession::Create(
        BuildPair(actions, 1U, no_elapsed), coherent_config);
    RunReplaySession ready = TakeSession(
        ready_created, "the coherent empty mission replay is created");
    Check(ready.state() == RunReplaySessionState::Complete &&
              SameDiagnosticMissionLifecycle(
                  ready.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Ready),
        "an enabled empty replay owns one exact ready mission state");
    const auto ready_complete = ready.Next();
    CheckError(ready_complete, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayComplete,
        "completed mission replay rejects advancement before any lifecycle mutation");
    Check(SameDiagnosticMissionLifecycle(
              ready.diagnostic_mission_lifecycle_state(),
              DiagnosticMissionStatus::Ready),
        "the replay-complete error preserves the exact ready mission state");

    RunReplaySessionConfig already_deployed_config = coherent_config;
    already_deployed_config.initial_front_end_state = kGameplayFrontEndState;
    auto already_deployed_created = RunReplaySession::Create(
        BuildPair(actions, 1U, no_elapsed), already_deployed_config);
    RunReplaySession already_deployed = TakeSession(already_deployed_created,
        "the coherent already-deployed empty mission replay is created");
    Check(already_deployed.state() == RunReplaySessionState::Complete &&
              SameDiagnosticMissionLifecycle(
                  already_deployed.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Active),
        "an enabled replay that begins in DiagnosticPlay owns one exact active mission state");

    const std::array required_gate_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{20}},
    };
    RunReplaySessionConfig required_gate_config = already_deployed_config;
    required_gate_config.front_end_capabilities
        .requires_active_profile_for_diagnostic_play = true;
    required_gate_config.front_end_capabilities
        .requires_active_character_for_diagnostic_play = true;
    auto required_gate_created = RunReplaySession::Create(
        BuildScriptedPair(actions, required_gate_frames), required_gate_config);
    RunReplaySession required_gate = TakeSession(required_gate_created,
        "the confirmation-gated initial DiagnosticPlay replay is created");
    Check(SameDiagnosticMissionLifecycle(
              required_gate.diagnostic_mission_lifecycle_state(),
              DiagnosticMissionStatus::Ready) &&
              required_gate.front_end_state() == kGameplayFrontEndState &&
              !required_gate.front_end_active_profile_is_confirmed() &&
              !required_gate.front_end_active_character_is_confirmed(),
        "closed confirmation mirrors keep an initial DiagnosticPlay mission ready");
    const auto required_gate_frame = required_gate.Next();
    Check(required_gate_frame && required_gate_frame->frame_plan() &&
              required_gate_frame->frame_plan()->simulation_steps == 0U &&
              required_gate.front_end_state() ==
                  omega::app::InitialFrontEndState() &&
              SameDiagnosticMissionLifecycle(
                  required_gate.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Ready) &&
              !required_gate.front_end_active_profile_is_confirmed() &&
              !required_gate.front_end_active_character_is_confirmed(),
        "a gated initial DiagnosticPlay frame fails closed without synthesizing a mission abort");

    constexpr std::array deploy_down{
        InputTransition{.code = 1U, .pressed = true},
    };
    constexpr std::array deploy_up_move_target_down{
        InputTransition{.code = 1U, .pressed = false},
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 4U, .pressed = true},
    };
    constexpr std::array move_up_fire_down{
        InputTransition{.code = 0U, .pressed = false},
        InputTransition{.code = 3U, .pressed = true},
    };
    constexpr std::array objective_controls_up{
        InputTransition{.code = 3U, .pressed = false},
        InputTransition{.code = 4U, .pressed = false},
    };
    const std::array success_and_redeploy_frames{
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = deploy_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = deploy_up_move_target_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = move_up_fire_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = objective_controls_up,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = milliseconds{20},
            .transitions = deploy_down,
            .pointer_position = target_center,
        },
    };

    auto success_created = RunReplaySession::Create(
        BuildScriptedPair(actions, success_and_redeploy_frames),
        coherent_config);
    RunReplaySession success = TakeSession(
        success_created, "the successful diagnostic mission replay is created");
    Check(SameDiagnosticMissionLifecycle(
              success.diagnostic_mission_lifecycle_state(),
              DiagnosticMissionStatus::Ready) &&
              success.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  success.diagnostic_proximity_trigger_state(), false, false) &&
              SameDiagnosticTargetFire(
                  success.diagnostic_target_fire_state(), false, false),
        "mission replay begins ready with exact neutral gameplay state");

    const auto deployed = success.Next();
    Check(deployed && deployed->frame_plan() &&
              deployed->frame_plan()->simulation_steps == 0U &&
              deployed->front_end_command().type ==
                  FrontEndCommandType::StartDiagnosticCampaign &&
              success.front_end_state() == kGameplayFrontEndState &&
              success.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  success.diagnostic_proximity_trigger_state(), false, false) &&
              SameDiagnosticTargetFire(
                  success.diagnostic_target_fire_state(), false, false) &&
              SameDiagnosticMissionLifecycle(
                  success.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Active),
        "briefing deploy resets actor, proximity, and target as one active mission publication");

    const auto crossed = success.Next();
    Check(crossed && crossed->frame_plan() &&
              crossed->frame_plan()->simulation_steps == 6U &&
              success.debug_locomotion_position() == Position3{.x = 6} &&
              SameDiagnosticProximityTrigger(
                  success.diagnostic_proximity_trigger_state(), false, true) &&
              SameDiagnosticTargetFire(
                  success.diagnostic_target_fire_state(), false, false) &&
              SameDiagnosticMissionLifecycle(
                  success.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Active),
        "fixed-step crossing completes proximity while the deployed mission remains active");

    const auto completed = success.Next();
    Check(completed && completed->frame_plan() &&
              completed->frame_plan()->simulation_steps == 0U &&
              SameDiagnosticTargetFire(
                  success.diagnostic_target_fire_state(), false, true) &&
              SameDiagnosticMissionLifecycle(
                  success.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Succeeded) &&
              success.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::BriefingRoom,
                  .selected_main_row = FrontEndMainRow::StartDiagnostic,
              },
        "a false-to-true target edge succeeds on a zero-step batch and returns to briefing in the same frame");

    const auto succeeded_neutral = success.Next();
    Check(succeeded_neutral &&
              SameDiagnosticMissionLifecycle(
                  success.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Succeeded),
        "a neutral briefing frame leaves terminal success immutable");
    const auto redeployed_after_success = success.Next();
    Check(redeployed_after_success &&
              redeployed_after_success->frame_plan() &&
              redeployed_after_success->frame_plan()->simulation_steps == 2U &&
              redeployed_after_success->front_end_command().type ==
                  FrontEndCommandType::StartDiagnosticCampaign &&
              success.front_end_state() == kGameplayFrontEndState &&
              success.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  success.diagnostic_proximity_trigger_state(), false, false) &&
              SameDiagnosticTargetFire(
                  success.diagnostic_target_fire_state(), false, false) &&
              SameDiagnosticMissionLifecycle(
                  success.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Active),
        "redeploy from success atomically restores the mission gameplay origin");

    RunReplaySessionConfig disabled_config = coherent_config;
    disabled_config.enable_debug_mission_lifecycle = false;
    const std::span<const ScriptedElapsedFrame> success_prefix{
        success_and_redeploy_frames.data(), 3U};
    auto disabled_created = RunReplaySession::Create(
        BuildScriptedPair(actions, success_prefix), disabled_config);
    RunReplaySession disabled = TakeSession(disabled_created,
        "the mission-lifecycle-disabled compatibility replay is created");
    const auto disabled_deploy = disabled.Next();
    const auto disabled_crossing = disabled.Next();
    const auto disabled_hit = disabled.Next();
    Check(disabled_deploy && disabled_crossing && disabled_hit &&
              !disabled.diagnostic_mission_lifecycle_state() &&
              disabled.front_end_state() == kGameplayFrontEndState &&
              SameDiagnosticTargetFire(
                  disabled.diagnostic_target_fire_state(), false, true),
        "the default-disabled option preserves the prior target completion and play-mode behavior");

    constexpr std::array primary_up{
        InputTransition{.code = 1U, .pressed = false},
    };
    const std::array primary_abort_redeploy_frames{
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = deploy_down,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = primary_up,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = deploy_down,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = primary_up,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = deploy_down,
        },
    };
    auto primary_abort_created = RunReplaySession::Create(
        BuildScriptedPair(actions, primary_abort_redeploy_frames),
        coherent_config);
    RunReplaySession primary_abort = TakeSession(primary_abort_created,
        "the primary-abort mission replay is created");
    const auto primary_deployed = primary_abort.Next();
    const auto primary_released = primary_abort.Next();
    const auto primary_aborted = primary_abort.Next();
    Check(primary_deployed && primary_released && primary_aborted &&
              primary_aborted->front_end_command().type ==
                  FrontEndCommandType::None &&
              primary_abort.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::BriefingRoom,
                  .selected_main_row = FrontEndMainRow::StartDiagnostic,
              } &&
              SameDiagnosticMissionLifecycle(
                  primary_abort.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Failed),
        "active primary exits play and aborts the mission before completion");
    const auto failed_neutral = primary_abort.Next();
    Check(failed_neutral &&
              SameDiagnosticMissionLifecycle(
                  primary_abort.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Failed),
        "a neutral briefing frame leaves terminal failure immutable");
    const auto redeployed_after_abort = primary_abort.Next();
    Check(redeployed_after_abort &&
              redeployed_after_abort->front_end_command().type ==
                  FrontEndCommandType::StartDiagnosticCampaign &&
              primary_abort.debug_locomotion_position() == Position3{} &&
              SameDiagnosticProximityTrigger(
                  primary_abort.diagnostic_proximity_trigger_state(), false,
                  false) &&
              SameDiagnosticTargetFire(
                  primary_abort.diagnostic_target_fire_state(), false, false) &&
              SameDiagnosticMissionLifecycle(
                  primary_abort.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Active),
        "redeploy from abort resets every gameplay value and starts a fresh active mission");

    constexpr std::array cancel_primary_fire_down{
        InputTransition{.code = 0U, .pressed = false},
        InputTransition{.code = 1U, .pressed = true},
        InputTransition{.code = 2U, .pressed = true},
        InputTransition{.code = 3U, .pressed = true},
    };
    const std::array cancel_precedence_frames{
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = deploy_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = milliseconds{60},
            .transitions = deploy_up_move_target_down,
            .pointer_position = target_center,
        },
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = cancel_primary_fire_down,
            .pointer_position = target_center,
        },
    };
    auto cancel_created = RunReplaySession::Create(
        BuildScriptedPair(actions, cancel_precedence_frames), coherent_config);
    RunReplaySession cancel = TakeSession(
        cancel_created, "the cancel-precedence mission replay is created");
    const auto cancel_deployed = cancel.Next();
    const auto cancel_crossed = cancel.Next();
    const auto cancel_aborted = cancel.Next();
    Check(cancel_deployed && cancel_crossed && cancel_aborted &&
              cancel_aborted->front_end_command().type ==
                  FrontEndCommandType::None &&
              cancel_aborted->frame_plan() &&
              cancel_aborted->frame_plan()->simulation_steps == 0U &&
              cancel.debug_locomotion_position() == Position3{.x = 6} &&
              SameDiagnosticProximityTrigger(
                  cancel.diagnostic_proximity_trigger_state(), false, true) &&
              SameDiagnosticTargetFire(
                  cancel.diagnostic_target_fire_state(), false, false) &&
              SameDiagnosticMissionLifecycle(
                  cancel.diagnostic_mission_lifecycle_state(),
                  DiagnosticMissionStatus::Failed) &&
              cancel.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::BriefingRoom,
                  .selected_main_row = FrontEndMainRow::StartDiagnostic,
              },
        "cancel wins over simultaneous primary and fire, aborting without a click-through completion");

    constexpr std::array deploy_up_move_down{
        InputTransition{.code = 1U, .pressed = false},
        InputTransition{.code = 0U, .pressed = true},
    };
    constexpr std::array terminal_cancel{
        InputTransition{.code = 0U, .pressed = false},
        InputTransition{.code = 2U, .pressed = true},
    };
    const std::array terminal_prefix_frames{
        ScriptedElapsedFrame{
            .elapsed = nanoseconds::zero(),
            .transitions = deploy_down,
        },
        ScriptedElapsedFrame{
            .elapsed = milliseconds{10},
            .transitions = deploy_up_move_down,
        },
    };
    auto terminal_created = RunReplaySession::Create(
        BuildScriptedPair(actions, terminal_prefix_frames,
            TerminalReasons{.host_quit_requested = true}, terminal_cancel),
        coherent_config);
    RunReplaySession terminal_source = TakeSession(terminal_created,
        "the terminal-neutral mission replay is created");
    const auto terminal_deployed = terminal_source.Next();
    const auto terminal_moved = terminal_source.Next();
    const auto mission_before_move =
        terminal_source.diagnostic_mission_lifecycle_state();
    const auto position_before_move =
        terminal_source.debug_locomotion_position();
    const auto trigger_before_move =
        terminal_source.diagnostic_proximity_trigger_state();
    const auto target_before_move =
        terminal_source.diagnostic_target_fire_state();
    const auto front_end_before_move = terminal_source.front_end_state();
    const auto scheduler_before_move = terminal_source.scheduler_state();
    const auto simulation_before_move = terminal_source.simulation_state();
    RunReplaySession terminal = std::move(terminal_source);
    Check(terminal_deployed && terminal_moved &&
              SameDiagnosticMissionLifecycle(
                  mission_before_move, DiagnosticMissionStatus::Active) &&
              position_before_move == Position3{.x = 1} &&
              terminal_source.state() == RunReplaySessionState::Inert &&
              !terminal_source.diagnostic_mission_lifecycle_state() &&
              terminal.diagnostic_mission_lifecycle_state() ==
                  mission_before_move &&
              terminal.debug_locomotion_position() == position_before_move &&
              terminal.front_end_state() == front_end_before_move,
        "move construction transfers the exact active mission and leaves the source inert");
    const auto inert_next = terminal_source.Next();
    CheckError(inert_next, RunReplayOperation::Next,
        RunReplayErrorCode::InvalidSessionState,
        "the mission moved-from source reports fixed invalid state");
    Check(!terminal_source.diagnostic_mission_lifecycle_state(),
        "invalid advancement cannot repopulate a moved-from mission owner");

    const std::size_t terminal_remaining = terminal.remaining_frames();
    replay_session_test_allocation::Arm(0U);
    const auto failed_terminal = terminal.Next();
    replay_session_test_allocation::Disarm();
    CheckError(failed_terminal, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayNextFailed,
        "a lower terminal replay failure precedes mission mutation",
        RunCaptureReplayErrorCode::AllocationFailed);
    Check(terminal.state() == RunReplaySessionState::Ready &&
              terminal.remaining_frames() == terminal_remaining &&
              terminal.diagnostic_mission_lifecycle_state() ==
                  mission_before_move &&
              terminal.debug_locomotion_position() == position_before_move &&
              SameDiagnosticProximityTrigger(
                  terminal.diagnostic_proximity_trigger_state(),
                  trigger_before_move) &&
              terminal.diagnostic_target_fire_state() == target_before_move &&
              terminal.front_end_state() == front_end_before_move &&
              terminal.scheduler_state() == scheduler_before_move &&
              SameSimulation(
                  terminal.simulation_state(), simulation_before_move),
        "a retryable lower failure preserves every mission-composition owner");

    const auto terminal_frame = terminal.Next();
    Check(terminal_frame && terminal_frame->terminal_input() &&
              !terminal_frame->frame_plan() &&
              terminal_frame->input().WasPressed(
                  omega::app::kFrontEndCancelAction) &&
              terminal.state() == RunReplaySessionState::Complete &&
              terminal.diagnostic_mission_lifecycle_state() ==
                  mission_before_move &&
              terminal.debug_locomotion_position() == position_before_move &&
              SameDiagnosticProximityTrigger(
                  terminal.diagnostic_proximity_trigger_state(),
                  trigger_before_move) &&
              terminal.diagnostic_target_fire_state() == target_before_move &&
              terminal.front_end_state() == front_end_before_move &&
              terminal.scheduler_state() == scheduler_before_move &&
              SameSimulation(
                  terminal.simulation_state(), simulation_before_move),
        "terminal cancel input is publication-only and preserves an active mission exactly");
}

void CheckFrontEndModalGate()
{
    constexpr std::array<std::uint32_t, 4U> menu_actions{
        omega::app::kFrontEndPreviousAction,
        omega::app::kFrontEndNextAction,
        omega::app::kFrontEndPrimaryAction,
        omega::app::kFrontEndCancelAction,
    };
    constexpr std::array<InputTransition, 0U> no_transitions{};

    constexpr std::array legacy_transitions{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 2U, .pressed = true},
    };
    const std::array legacy_frames{
        ScriptedElapsedFrame{
            .elapsed = milliseconds{25},
            .transitions = legacy_transitions,
        },
    };
    RunCaptureTracePair legacy_pair = BuildScriptedPair(menu_actions, legacy_frames);
    RunReplaySessionConfig legacy_config = ValidConfig();
    legacy_config.enable_debug_locomotion = true;
    auto legacy_created =
        RunReplaySession::Create(std::move(legacy_pair), legacy_config);
    RunReplaySession legacy =
        TakeSession(legacy_created, "the default-disabled menu replay is created");
    auto legacy_frame = legacy.Next();
    const auto legacy_plan = legacy_frame ? legacy_frame->frame_plan() : std::nullopt;
    const auto legacy_simulation = legacy.simulation_state();
    Check(legacy_frame && legacy_plan && legacy_frame->elapsed() == milliseconds{25} &&
              legacy_frame->input().actions().size() == menu_actions.size() &&
              legacy_frame->input().actions()[0] ==
                  omega::app::kFrontEndPreviousAction &&
              legacy_frame->input().actions()[1] ==
                  omega::app::kFrontEndNextAction &&
              legacy_frame->input().actions()[2] ==
                  omega::app::kFrontEndPrimaryAction &&
              legacy_frame->input().actions()[3] ==
                  omega::app::kFrontEndCancelAction &&
              legacy_frame->input().WasPressed(
                  omega::app::kFrontEndPreviousAction) &&
              legacy_frame->input().WasPressed(
                  omega::app::kFrontEndPrimaryAction) &&
              !legacy_frame->input().WasPressed(
                  omega::app::kFrontEndCancelAction) &&
              legacy_plan->simulation_steps == 2U &&
              !legacy.front_end_state() &&
              legacy.debug_locomotion_position() == Position3{.z = 2} &&
              legacy.diagnostic_actor_marker_destination() ==
                  kForwardTwoMarkerDestination &&
              legacy_simulation && SameSimulation(*legacy_simulation,
                  SimulationState{
                      .completed_steps = 2U,
                      .simulated_time = milliseconds{20},
                      .alive_entities = 1U,
                  }),
        "default-null menu ownership leaves actions 2, 3, 6, and 7 on legacy nonmodal replay");

    constexpr std::array next_down{
        InputTransition{.code = 1U, .pressed = true}};
    constexpr std::array next_up{
        InputTransition{.code = 1U, .pressed = false}};
    constexpr std::array primary_down{
        InputTransition{.code = 2U, .pressed = true}};
    constexpr std::array primary_up_next_up{
        InputTransition{.code = 2U, .pressed = false},
        InputTransition{.code = 1U, .pressed = false},
    };
    constexpr std::array primary_up_previous_down{
        InputTransition{.code = 2U, .pressed = false},
        InputTransition{.code = 0U, .pressed = true},
    };
    constexpr std::array previous_up{
        InputTransition{.code = 0U, .pressed = false}};

    const std::array closed_start_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
    };
    RunCaptureTracePair closed_start_pair =
        BuildScriptedPair(menu_actions, closed_start_frames);
    RunReplaySessionConfig closed_start_config = ValidConfig();
    closed_start_config.initial_front_end_state =
        omega::app::InitialFrontEndState();
    auto closed_start_created = RunReplaySession::Create(
        std::move(closed_start_pair), closed_start_config);
    RunReplaySession closed_start = TakeSession(
        closed_start_created, "the capability-closed start replay is created");
    auto suppressed_start = closed_start.Next();
    Check(suppressed_start && suppressed_start->frame_plan() &&
              suppressed_start->front_end_command() == FrontEndCommand{} &&
              suppressed_start->frame_plan()->simulation_steps == 0U &&
              closed_start.front_end_state() ==
                  omega::app::InitialFrontEndState(),
        "replay preserves an unconfirmed capability-closed Start as an inert modal frame");

    const std::array modal_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = no_transitions},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = next_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = next_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = no_transitions},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = next_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_up_next_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = no_transitions},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_up_previous_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = previous_up},
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = primary_down},
    };
    constexpr std::array expected_modal_states{
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::CreateAgent},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::LoadAgent},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::LoadAgent},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::LoadAgent},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::LoadAgent},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::Quit},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::Quit},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::Quit},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::Quit},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::LoadAgent},
        FrontEndState{.mode = FrontEndMode::Title,
            .selected_main_row = FrontEndMainRow::LoadAgent},
    };
    constexpr std::array expected_primary_held{
        false, false, false, true, true, true, false, true, true, false, false,
    };
    constexpr std::array expected_primary_pressed{
        false, false, false, true, false, false, false, true, false, false, false,
    };
    RunCaptureTracePair modal_pair = BuildScriptedPair(menu_actions, modal_frames);
    RunReplaySessionConfig modal_config = ValidConfig();
    modal_config.enable_debug_locomotion = true;
    modal_config.initial_front_end_state =
        omega::app::InitialFrontEndState();
    modal_config.front_end_visible_profile_slots = 3U;
    modal_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = false,
        .can_start_diagnostic_campaign = true,
    };
    auto modal_created = RunReplaySession::Create(std::move(modal_pair), modal_config);
    RunReplaySession modal =
        TakeSession(modal_created, "the main-menu modal replay is created");
    const auto modal_scheduler_origin = modal.scheduler_state();
    const auto modal_world_origin = modal.simulation_state();
    const auto modal_position_origin = modal.debug_locomotion_position();
    const auto modal_marker_origin =
        modal.diagnostic_actor_marker_destination();
    Check(modal_marker_origin == kOriginMarkerDestination &&
              modal_position_origin &&
              modal_marker_origin ==
                  omega::app::PlanProjectDiagnosticActorMarkerDestination(
                      *modal_position_origin),
        "modal replay begins with the exact derived origin marker");
    for (std::size_t index = 0U; index < expected_modal_states.size(); ++index)
    {
        auto frame = modal.Next();
        const auto plan = frame ? frame->frame_plan() : std::nullopt;
        Check(frame && frame->elapsed() == std::chrono::seconds{4} && plan &&
                  frame->input().IsHeld(
                      omega::app::kFrontEndPrimaryAction) ==
                      expected_primary_held[index] &&
                  frame->input().WasPressed(
                      omega::app::kFrontEndPrimaryAction) ==
                      expected_primary_pressed[index] &&
                  plan->simulation_steps == 0U &&
                  plan->interpolation_alpha == 0.0 && !plan->clamped_delta &&
                  !plan->dropped_time &&
                  modal.front_end_state() == expected_modal_states[index] &&
                  frame->front_end_command() ==
                      (index == 7U
                           ? FrontEndCommand{
                                 .type = FrontEndCommandType::RequestQuit,
                             }
                           : FrontEndCommand{}) &&
                  modal.scheduler_state() == modal_scheduler_origin &&
                  SameSimulation(modal.simulation_state(), modal_world_origin) &&
                  modal.debug_locomotion_position() == modal_position_origin &&
                  modal.diagnostic_actor_marker_destination() ==
                      modal_marker_origin,
            "canonical Title edges discard elapsed and freeze every "
            "simulation and marker value");
    }
    auto activated = modal.Next();
    const auto activated_plan = activated ? activated->frame_plan() : std::nullopt;
    const auto activated_scheduler = modal.scheduler_state();
    const auto activated_world = modal.simulation_state();
    Check(activated && activated->elapsed() == milliseconds{15} && activated_plan &&
              activated->front_end_command() == FrontEndCommand{} &&
              activated_plan->simulation_steps == 0U &&
              activated_plan->interpolation_alpha == 0.0 &&
              !activated_plan->clamped_delta && !activated_plan->dropped_time &&
              modal.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::Title,
                  .selected_main_row = FrontEndMainRow::LoadAgent,
              } &&
              activated_scheduler == modal_scheduler_origin &&
              SameSimulation(activated_world, modal_world_origin) &&
              modal.debug_locomotion_position() == Position3{} &&
              modal.diagnostic_actor_marker_destination() ==
                  kOriginMarkerDestination,
        "Load Agent remains modal without a character-capable owner route");

    constexpr std::array primary_up{
        InputTransition{.code = 2U, .pressed = false}};
    constexpr std::array previous_down{
        InputTransition{.code = 0U, .pressed = true}};
    constexpr std::array previous_up_primary_down{
        InputTransition{.code = 0U, .pressed = false},
        InputTransition{.code = 2U, .pressed = true},
    };
    const std::array topology_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = no_transitions},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_up_previous_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = previous_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = previous_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = previous_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = previous_down},
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = previous_up_primary_down},
    };
    constexpr FrontEndState main_asset_row{
        .mode = FrontEndMode::Title,
        .selected_main_row = FrontEndMainRow::AssetTopology,
    };
    constexpr std::array expected_topology_modal_states{
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
        omega::app::InitialFrontEndState(),
    };
    constexpr std::array expected_topology_primary_held{
        true, true, false, true, false, false, false, false, false,
    };
    constexpr std::array expected_topology_primary_pressed{
        true, false, false, true, false, false, false, false, false,
    };
    constexpr std::array expected_topology_primary_released{
        false, false, true, false, true, false, false, false, false,
    };
    RunCaptureTracePair topology_pair =
        BuildScriptedPair(menu_actions, topology_frames);
    RunReplaySessionConfig topology_config = ValidConfig();
    topology_config.enable_debug_locomotion = true;
    topology_config.initial_front_end_state = main_asset_row;
    topology_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = false,
        .can_start_diagnostic_campaign = true,
    };
    auto topology_created =
        RunReplaySession::Create(std::move(topology_pair), topology_config);
    RunReplaySession topology =
        TakeSession(topology_created, "the asset-topology modal replay is created");
    const auto topology_scheduler_origin = topology.scheduler_state();
    const auto topology_world_origin = topology.simulation_state();
    const auto topology_position_origin = topology.debug_locomotion_position();
    for (std::size_t index = 0U;
         index < expected_topology_modal_states.size(); ++index)
    {
        auto frame = topology.Next();
        const auto plan = frame ? frame->frame_plan() : std::nullopt;
        Check(frame && frame->elapsed() == std::chrono::seconds{4} && plan &&
                  frame->input().IsHeld(
                      omega::app::kFrontEndPrimaryAction) ==
                      expected_topology_primary_held[index] &&
                  frame->input().WasPressed(
                      omega::app::kFrontEndPrimaryAction) ==
                      expected_topology_primary_pressed[index] &&
                  frame->input().WasReleased(
                      omega::app::kFrontEndPrimaryAction) ==
                      expected_topology_primary_released[index] &&
                  plan->simulation_steps == 0U &&
                  plan->interpolation_alpha == 0.0 && !plan->clamped_delta &&
                  !plan->dropped_time &&
                  topology.front_end_state() ==
                      expected_topology_modal_states[index] &&
                  topology.scheduler_state() == topology_scheduler_origin &&
                  SameSimulation(topology.simulation_state(), topology_world_origin) &&
                  topology.debug_locomotion_position() == topology_position_origin,
            "an obsolete diagnostic Title row normalizes once and every later Title edge remains modal");
    }
    auto topology_activated = topology.Next();
    const auto topology_activated_plan =
        topology_activated ? topology_activated->frame_plan() : std::nullopt;
    const auto topology_activated_scheduler = topology.scheduler_state();
    const auto topology_activated_world = topology.simulation_state();
    Check(topology_activated &&
              topology_activated->elapsed() == milliseconds{15} &&
              topology_activated_plan &&
              topology_activated_plan->simulation_steps == 0U &&
              topology_activated_plan->interpolation_alpha == 0.0 &&
              !topology_activated_plan->clamped_delta &&
              !topology_activated_plan->dropped_time &&
              topology.front_end_state() == omega::app::InitialFrontEndState() &&
              topology_activated_scheduler == topology_scheduler_origin &&
              SameSimulation(topology_activated_world, topology_world_origin) &&
              topology.debug_locomotion_position() == Position3{} &&
              topology.state() == RunReplaySessionState::Complete,
        "obsolete diagnostic rows cannot bypass canonical Title or schedule simulation");

    constexpr std::array open_with_forward{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 2U, .pressed = true},
    };
    constexpr std::array release_primary{
        InputTransition{.code = 2U, .pressed = false}};
    const std::array reopen_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{5},
            .transitions = no_transitions},
        ScriptedElapsedFrame{.elapsed = milliseconds{25},
            .transitions = open_with_forward},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = release_primary},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = no_transitions},
        ScriptedElapsedFrame{.elapsed = milliseconds{5},
            .transitions = primary_down},
    };
    RunCaptureTracePair reopen_pair = BuildScriptedPair(menu_actions, reopen_frames);
    RunReplaySessionConfig reopen_config = ValidConfig();
    reopen_config.enable_debug_locomotion = true;
    reopen_config.initial_front_end_state = kGameplayFrontEndState;
    reopen_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = false,
        .can_start_diagnostic_campaign = true,
        .supports_character_selection = true,
    };
    auto reopen_created =
        RunReplaySession::Create(std::move(reopen_pair), reopen_config);
    RunReplaySession reopen =
        TakeSession(reopen_created, "the diagnostic-play modal transition replay is created");
    auto remainder_frame = reopen.Next();
    Check(remainder_frame && remainder_frame->frame_plan() &&
              remainder_frame->frame_plan()->simulation_steps == 0U &&
              reopen.scheduler_state() == FrameSchedulerState{
                  .config = reopen_config.scheduler,
                  .accumulated_remainder = milliseconds{5},
              },
        "diagnostic play can retain a nonzero scheduler remainder before opening the menu");
    const auto scheduler_before_open = reopen.scheduler_state();
    const auto world_before_open = reopen.simulation_state();
    const auto position_before_open = reopen.debug_locomotion_position();
    const auto marker_before_open =
        reopen.diagnostic_actor_marker_destination();
    Check(marker_before_open == kOriginMarkerDestination,
        "the pre-modal scheduler remainder leaves the marker at its exact origin");
    for (std::size_t index = 1U; index < 4U; ++index)
    {
        auto frame = reopen.Next();
        const auto plan = frame ? frame->frame_plan() : std::nullopt;
        Check(frame && frame->elapsed() == reopen_frames[index].elapsed && plan &&
                  plan->simulation_steps == 0U && !plan->clamped_delta &&
                  !plan->dropped_time &&
                   reopen.front_end_state() == FrontEndState{
                       .mode = FrontEndMode::BriefingRoom,
                       .selected_main_row = FrontEndMainRow::CreateAgent,
                   } &&
                  reopen.scheduler_state() == scheduler_before_open &&
                  SameSimulation(reopen.simulation_state(), world_before_open) &&
                  reopen.debug_locomotion_position() == position_before_open &&
                  reopen.diagnostic_actor_marker_destination() ==
                      marker_before_open,
        "opening Briefing Room with held movement and later modal frames freezes locomotion and its marker");
    }
    auto resumed = reopen.Next();
    const auto resumed_plan = resumed ? resumed->frame_plan() : std::nullopt;
    Check(resumed && resumed->elapsed() == milliseconds{5} && resumed_plan &&
              resumed->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::StartDiagnosticCampaign,
              } &&
              resumed_plan->simulation_steps == 1U &&
              resumed_plan->interpolation_alpha == 0.0 &&
              reopen.front_end_state() == kGameplayFrontEndState &&
              reopen.scheduler_state() == FrameSchedulerState{
                  .config = reopen_config.scheduler,
                  .total_planned_steps = 1U,
              } &&
              reopen.debug_locomotion_position() == Position3{} &&
              reopen.diagnostic_actor_marker_destination() ==
                  kOriginMarkerDestination,
        "reactivation uses only its elapsed and does not leak held modal movement into the transition step");

    constexpr std::array cancel_priority_transitions{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 1U, .pressed = true},
        InputTransition{.code = 2U, .pressed = true},
        InputTransition{.code = 3U, .pressed = true},
    };
    const std::array cancel_priority_frames{
        ScriptedElapsedFrame{
            .elapsed = std::chrono::seconds{4},
            .transitions = cancel_priority_transitions,
        },
    };
    RunCaptureTracePair cancel_priority_pair =
        BuildScriptedPair(menu_actions, cancel_priority_frames);
    RunReplaySessionConfig cancel_priority_config = ValidConfig();
    cancel_priority_config.enable_debug_locomotion = true;
    cancel_priority_config.initial_front_end_state = FrontEndState{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
        .selected_profile_slot = FrontEndProfileSlot::Second,
    };
    cancel_priority_config.front_end_visible_profile_slots = 3U;
    auto cancel_priority_created = RunReplaySession::Create(
        std::move(cancel_priority_pair), cancel_priority_config);
    RunReplaySession cancel_priority = TakeSession(
        cancel_priority_created, "the cancel-priority menu replay is created");
    const auto cancel_scheduler_origin = cancel_priority.scheduler_state();
    const auto cancel_world_origin = cancel_priority.simulation_state();
    const auto cancel_position_origin = cancel_priority.debug_locomotion_position();
    auto cancel_frame = cancel_priority.Next();
    const auto cancel_plan = cancel_frame ? cancel_frame->frame_plan() : std::nullopt;
    Check(cancel_frame && cancel_plan &&
              cancel_frame->input().WasPressed(
                  omega::app::kFrontEndCancelAction) &&
              cancel_frame->input().WasPressed(
                  omega::app::kFrontEndPrimaryAction) &&
              cancel_frame->input().WasPressed(
                  omega::app::kFrontEndPreviousAction) &&
              cancel_frame->input().WasPressed(
                  omega::app::kFrontEndNextAction) &&
              cancel_frame->front_end_command() == FrontEndCommand{} &&
              cancel_priority.front_end_state() ==
                  omega::app::InitialFrontEndState() &&
              cancel_plan->simulation_steps == 0U &&
              cancel_priority.scheduler_state() == cancel_scheduler_origin &&
              SameSimulation(cancel_priority.simulation_state(), cancel_world_origin) &&
              cancel_priority.debug_locomotion_position() == cancel_position_origin,
        "replay cancel has priority over confirm and navigation, returns staging to canonical Title, and emits no command");

    constexpr std::array terminal_transitions{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 1U, .pressed = true},
    };
    const std::span<const ScriptedElapsedFrame> no_elapsed_frames;
    RunCaptureTracePair terminal_pair = BuildScriptedPair(
        std::span<const std::uint32_t>{menu_actions}.subspan(2U), no_elapsed_frames,
        TerminalReasons{.host_quit_requested = true}, terminal_transitions);
    RunReplaySessionConfig terminal_config = ValidConfig();
    terminal_config.enable_debug_locomotion = true;
    terminal_config.initial_front_end_state = FrontEndState{
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::Second,
    };
    terminal_config.front_end_visible_profile_slots = 3U;
    auto terminal_created =
        RunReplaySession::Create(std::move(terminal_pair), terminal_config);
    RunReplaySession terminal =
        TakeSession(terminal_created, "the terminal menu-edge replay is created");
    const auto terminal_menu_before = terminal.front_end_state();
    const auto terminal_scheduler_before = terminal.scheduler_state();
    const auto terminal_world_before = terminal.simulation_state();
    const auto terminal_position_before = terminal.debug_locomotion_position();
    auto terminal_frame = terminal.Next();
    Check(terminal_frame && terminal_frame->terminal_input() &&
              terminal_frame->input().WasPressed(
                  omega::app::kFrontEndPrimaryAction) &&
              terminal_frame->input().WasPressed(
                  omega::app::kFrontEndCancelAction) &&
              terminal_frame->front_end_command() == FrontEndCommand{} &&
              !terminal_frame->elapsed() && !terminal_frame->frame_plan() &&
              terminal.front_end_state() == terminal_menu_before &&
              terminal.scheduler_state() == terminal_scheduler_before &&
              SameSimulation(terminal.simulation_state(), terminal_world_before) &&
              terminal.debug_locomotion_position() == terminal_position_before &&
              terminal.state() == RunReplaySessionState::Complete &&
              terminal.remaining_frames() == 0U,
        "terminal resolution precedes the reducer and cannot cancel, select the highlighted profile, or mutate any simulation owner");
}

void CheckFirstProfileCreationReplay()
{
    constexpr std::array<std::uint32_t, 1U> primary_action{
        omega::app::kFrontEndPrimaryAction};
    constexpr std::array primary_down{
        InputTransition{.code = 0U, .pressed = true}};
    constexpr std::array primary_up{
        InputTransition{.code = 0U, .pressed = false}};
    const std::array create_release_select_frames{
        ScriptedElapsedFrame{
            .elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{
            .elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{
            .elapsed = std::chrono::seconds{4}, .transitions = primary_down},
    };
    constexpr FrontEndState profiles_first{
        .mode = FrontEndMode::ProfileOwnerStaging,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    constexpr FrontEndState main_profiles{
        .mode = FrontEndMode::Title,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    constexpr FrontEndState selection_first{
        .mode = FrontEndMode::AgentSelection,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };

    RunCaptureTracePair create_pair =
        BuildScriptedPair(primary_action, create_release_select_frames);
    RunReplaySessionConfig create_config = ValidConfig();
    create_config.initial_front_end_state = profiles_first;
    create_config.front_end_visible_profile_slots = 0U;
    create_config.front_end_total_profile_count = 0U;
    create_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = true,
        .supports_character_selection = true,
    };
    auto create_result =
        RunReplaySession::Create(std::move(create_pair), create_config);
    RunReplaySession source = TakeSession(
        create_result, "the first-profile creation replay is created");
    const auto scheduler_origin = source.scheduler_state();
    const auto simulation_origin = source.simulation_state();

    auto create_frame = source.Next();
    const auto create_plan = create_frame ? create_frame->frame_plan() : std::nullopt;
    Check(create_frame && create_plan &&
              create_frame->input().WasPressed(omega::app::kFrontEndPrimaryAction) &&
              create_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::CreateFirstProfile,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              source.front_end_state() == profiles_first &&
              create_plan->simulation_steps == 0U &&
              create_plan->interpolation_alpha == 0.0 &&
              !create_plan->clamped_delta && !create_plan->dropped_time &&
              source.scheduler_state() == scheduler_origin &&
              SameSimulation(source.simulation_state(), simulation_origin),
        "an explicitly enabled empty replay publishes one logical first-profile creation without advancing time or simulation");

    RunReplaySession replay = std::move(source);
    Check(source.state() == RunReplaySessionState::Inert &&
              source.remaining_frames() == 0U && !source.scheduler_state() &&
              !source.simulation_state() && !source.front_end_state(),
        "moving a first-profile replay leaves the source observably inert");
    const auto inert_next = source.Next();
    CheckError(inert_next, RunReplayOperation::Next,
        RunReplayErrorCode::InvalidSessionState,
        "moved-from first-profile replay input cannot mutate logical creation state");

    auto release_frame = replay.Next();
    const auto release_plan = release_frame ? release_frame->frame_plan() : std::nullopt;
    Check(release_frame && release_plan &&
              release_frame->input().WasReleased(omega::app::kFrontEndPrimaryAction) &&
              release_frame->front_end_command() == FrontEndCommand{} &&
              replay.front_end_state() == profiles_first &&
              release_plan->simulation_steps == 0U &&
              replay.scheduler_state() == scheduler_origin &&
              SameSimulation(replay.simulation_state(), simulation_origin),
        "the release after logical creation is inert and preserves the new selectable first slot across session move");

    auto select_frame = replay.Next();
    const auto select_plan = select_frame ? select_frame->frame_plan() : std::nullopt;
    Check(select_frame && select_plan &&
              select_frame->input().WasPressed(omega::app::kFrontEndPrimaryAction) &&
              select_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::SetActiveProfile,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              replay.front_end_state() == selection_first &&
              select_plan->simulation_steps == 0U &&
              replay.scheduler_state() == scheduler_origin &&
              SameSimulation(replay.simulation_state(), simulation_origin) &&
              replay.state() == RunReplaySessionState::Complete &&
              replay.remaining_frames() == 0U,
        "only a later primary press selects the logically created first profile and creation cannot repeat");

    const std::array one_primary_frame{
        ScriptedElapsedFrame{
            .elapsed = std::chrono::seconds{4}, .transitions = primary_down},
    };
    RunCaptureTracePair legacy_pair =
        BuildScriptedPair(primary_action, one_primary_frame);
    RunReplaySessionConfig legacy_config = ValidConfig();
    legacy_config.initial_front_end_state = profiles_first;
    auto legacy_created =
        RunReplaySession::Create(std::move(legacy_pair), legacy_config);
    RunReplaySession legacy = TakeSession(
        legacy_created, "the default-closed empty-profile replay is created");
    const auto legacy_scheduler_origin = legacy.scheduler_state();
    const auto legacy_simulation_origin = legacy.simulation_state();
    auto legacy_frame = legacy.Next();
    Check(legacy_frame && legacy_frame->frame_plan() &&
              legacy_frame->front_end_command() == FrontEndCommand{} &&
              legacy.front_end_state() == profiles_first &&
              legacy_frame->frame_plan()->simulation_steps == 0U &&
              legacy.scheduler_state() == legacy_scheduler_origin &&
              SameSimulation(legacy.simulation_state(), legacy_simulation_origin),
        "default-closed replay capability leaves empty owner staging inert");

    RunCaptureTracePair nonempty_total_pair =
        BuildScriptedPair(primary_action, one_primary_frame);
    RunReplaySessionConfig nonempty_total_config = ValidConfig();
    nonempty_total_config.initial_front_end_state = profiles_first;
    nonempty_total_config.front_end_visible_profile_slots = 0U;
    nonempty_total_config.front_end_total_profile_count = 1U;
    nonempty_total_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = true,
    };
    auto nonempty_total_created = RunReplaySession::Create(
        std::move(nonempty_total_pair), nonempty_total_config);
    RunReplaySession nonempty_total = TakeSession(nonempty_total_created,
        "the nonempty-total creation-gate replay is created");
    auto nonempty_total_frame = nonempty_total.Next();
    Check(nonempty_total_frame &&
              nonempty_total_frame->front_end_command() == FrontEndCommand{} &&
              nonempty_total.front_end_state() == profiles_first,
        "a nonzero startup total keeps an explicit creation request closed");

    RunCaptureTracePair nonempty_visible_pair =
        BuildScriptedPair(primary_action, one_primary_frame);
    RunReplaySessionConfig nonempty_visible_config = ValidConfig();
    nonempty_visible_config.initial_front_end_state = profiles_first;
    nonempty_visible_config.front_end_visible_profile_slots = 1U;
    nonempty_visible_config.front_end_total_profile_count = 0U;
    nonempty_visible_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = true,
        .requires_active_profile_for_diagnostic_play = true,
    };
    auto nonempty_visible_created = RunReplaySession::Create(
        std::move(nonempty_visible_pair), nonempty_visible_config);
    RunReplaySession nonempty_visible = TakeSession(nonempty_visible_created,
        "the nonempty-visible creation-gate replay is created");
    auto nonempty_visible_frame = nonempty_visible.Next();
    Check(nonempty_visible_frame &&
              nonempty_visible_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::SetActiveProfile,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              nonempty_visible.front_end_state() == selection_first &&
              !nonempty_visible.front_end_active_profile_is_confirmed(),
        "a nonzero startup visible count keeps creation closed and an impossible zero-total selection cannot open the confirmation gate");

    constexpr std::array<std::uint32_t, 2U> primary_cancel_actions{
        omega::app::kFrontEndPrimaryAction,
        omega::app::kFrontEndCancelAction,
    };
    constexpr std::array cancel_and_primary_down{
        InputTransition{.code = 0U, .pressed = true},
        InputTransition{.code = 1U, .pressed = true},
    };
    const std::array cancel_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = cancel_and_primary_down},
    };
    RunCaptureTracePair cancel_pair =
        BuildScriptedPair(primary_cancel_actions, cancel_frames);
    RunReplaySessionConfig cancel_config = create_config;
    auto cancel_created =
        RunReplaySession::Create(std::move(cancel_pair), cancel_config);
    RunReplaySession cancel = TakeSession(
        cancel_created, "the first-profile cancel-priority replay is created");
    const auto cancel_scheduler_origin = cancel.scheduler_state();
    const auto cancel_simulation_origin = cancel.simulation_state();
    auto cancel_frame = cancel.Next();
    Check(cancel_frame && cancel_frame->frame_plan() &&
              cancel_frame->front_end_command() == FrontEndCommand{} &&
              cancel.front_end_state() == main_profiles &&
              cancel_frame->frame_plan()->simulation_steps == 0U &&
              cancel.scheduler_state() == cancel_scheduler_origin &&
              SameSimulation(cancel.simulation_state(), cancel_simulation_origin),
        "cancel remains terminal for the modal edge and suppresses first-profile creation without advancing owners");

    const std::span<const ScriptedElapsedFrame> no_elapsed_frames;
    RunCaptureTracePair terminal_pair = BuildScriptedPair(
        primary_cancel_actions, no_elapsed_frames,
        TerminalReasons{.host_quit_requested = true}, cancel_and_primary_down);
    RunReplaySessionConfig terminal_config = create_config;
    auto terminal_created =
        RunReplaySession::Create(std::move(terminal_pair), terminal_config);
    RunReplaySession terminal = TakeSession(
        terminal_created, "the first-profile terminal replay is created");
    const auto terminal_menu_origin = terminal.front_end_state();
    const auto terminal_scheduler_origin = terminal.scheduler_state();
    const auto terminal_simulation_origin = terminal.simulation_state();
    auto terminal_frame = terminal.Next();
    Check(terminal_frame && terminal_frame->terminal_input() &&
              terminal_frame->front_end_command() == FrontEndCommand{} &&
              !terminal_frame->elapsed() && !terminal_frame->frame_plan() &&
              terminal.front_end_state() == terminal_menu_origin &&
              terminal.scheduler_state() == terminal_scheduler_origin &&
              SameSimulation(terminal.simulation_state(), terminal_simulation_origin) &&
              terminal.state() == RunReplaySessionState::Complete,
        "terminal input resolves before first-profile creation and leaves every logical and simulation owner unchanged");
}

// The replay session mirrors the app's diagnostic-play gate with one identity-free
// bool. These fixtures pin the three behaviors that separates it from a second
// selection identity: it starts closed, only a replayed selection opens it, and a
// replayed creation leaves it closed.
void CheckDiagnosticPlayGateReplay()
{
    constexpr std::array<std::uint32_t, 1U> primary_action{
        omega::app::kFrontEndPrimaryAction};
    constexpr std::array<InputTransition, 0U> no_transitions{};
    constexpr std::array primary_down{
        InputTransition{.code = 0U, .pressed = true}};
    constexpr std::array primary_up{
        InputTransition{.code = 0U, .pressed = false}};

    constexpr FrontEndState title_load{
        .mode = FrontEndMode::Title,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    constexpr FrontEndState staging_create{
        .mode = FrontEndMode::ProfileOwnerStaging,
        .selected_main_row = FrontEndMainRow::CreateAgent,
    };
    constexpr FrontEndState selection_load{
        .mode = FrontEndMode::AgentSelection,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    constexpr FrontEndState briefing_load{
        .mode = FrontEndMode::BriefingRoom,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    constexpr FrontEndState gameplay_load{
        .mode = FrontEndMode::Gameplay,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };
    constexpr FrontEndCapabilities gated_route{
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
        .supports_character_selection = true,
        .requires_active_character_for_diagnostic_play = true,
    };

    const std::array hostile_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = no_transitions},
    };
    RunReplaySessionConfig hostile_config = ValidConfig();
    hostile_config.enable_debug_locomotion = true;
    hostile_config.initial_front_end_state = gameplay_load;
    hostile_config.front_end_visible_profile_slots = 1U;
    hostile_config.front_end_total_profile_count = 1U;
    hostile_config.front_end_visible_character_slots_by_profile[0U] = 1U;
    hostile_config.front_end_total_character_counts_by_profile[0U] = 1U;
    hostile_config.front_end_capabilities = gated_route;
    auto hostile_created = RunReplaySession::Create(
        BuildScriptedPair(primary_action, hostile_frames), hostile_config);
    RunReplaySession hostile = TakeSession(
        hostile_created, "the hostile preconfirmed gameplay replay is created");
    const auto hostile_scheduler = hostile.scheduler_state();
    const auto hostile_world = hostile.simulation_state();
    auto hostile_frame = hostile.Next();
    Check(hostile_frame && hostile_frame->frame_plan() &&
              hostile_frame->front_end_command() == FrontEndCommand{} &&
              hostile.front_end_state() == omega::app::InitialFrontEndState() &&
              !hostile.front_end_active_profile_is_confirmed() &&
              !hostile.front_end_active_character_is_confirmed() &&
              hostile_frame->frame_plan()->simulation_steps == 0U &&
              hostile.scheduler_state() == hostile_scheduler &&
              SameSimulation(hostile.simulation_state(), hostile_world),
        "an initial Gameplay state cannot pre-open either replay authorization mirror");

    const std::array creation_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = primary_down},
    };
    RunReplaySessionConfig creation_config = ValidConfig();
    creation_config.initial_front_end_state = staging_create;
    creation_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = true,
        .supports_character_selection = true,
    };
    auto creation_created = RunReplaySession::Create(
        BuildScriptedPair(primary_action, creation_frames), creation_config);
    RunReplaySession creation = TakeSession(
        creation_created, "the focused owner-creation replay is created");
    auto creation_frame = creation.Next();
    Check(creation_frame && creation_frame->frame_plan() &&
              creation_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::CreateProfileOwner,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              creation.front_end_state() == staging_create &&
              !creation.front_end_active_profile_is_confirmed() &&
              !creation.front_end_active_character_is_confirmed() &&
              creation_frame->frame_plan()->simulation_steps == 0U,
        "owner creation advances the bounded model but cannot publish confirmation");

    const std::array route_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = primary_down},
    };
    RunReplaySessionConfig route_config = ValidConfig();
    route_config.enable_debug_locomotion = true;
    route_config.initial_front_end_state = title_load;
    route_config.front_end_visible_profile_slots = 1U;
    route_config.front_end_total_profile_count = 1U;
    route_config.front_end_visible_character_slots_by_profile[0U] = 1U;
    route_config.front_end_total_character_counts_by_profile[0U] = 1U;
    route_config.front_end_capabilities = gated_route;
    auto route_created = RunReplaySession::Create(
        BuildScriptedPair(primary_action, route_frames), route_config);
    RunReplaySession source = TakeSession(
        route_created, "the canonical Load Agent replay is created");
    const auto route_scheduler_origin = source.scheduler_state();
    const auto route_world_origin = source.simulation_state();

    auto profile_frame = source.Next();
    Check(profile_frame && profile_frame->frame_plan() &&
              profile_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmProfileOwner,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              source.front_end_state() == selection_load &&
              source.front_end_active_profile_is_confirmed() &&
              !source.front_end_active_character_is_confirmed() &&
              profile_frame->frame_plan()->simulation_steps == 0U &&
              source.scheduler_state() == route_scheduler_origin &&
              SameSimulation(source.simulation_state(), route_world_origin),
        "Load Agent confirms exactly one bounded owner before agent selection");

    RunReplaySession route = std::move(source);
    Check(source.state() == RunReplaySessionState::Inert &&
              !source.front_end_state() &&
              !source.front_end_active_profile_is_confirmed() &&
              !source.front_end_active_character_is_confirmed() &&
              route.front_end_state() == selection_load &&
              route.front_end_active_profile_is_confirmed() &&
              !route.front_end_active_character_is_confirmed(),
        "session move transfers the opened owner mirror and leaves the source inert");

    auto profile_release = route.Next();
    auto character_frame = route.Next();
    Check(profile_release && profile_release->frame_plan() &&
              profile_release->front_end_command() == FrontEndCommand{} &&
              character_frame && character_frame->frame_plan() &&
              character_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmAgent,
                  .profile_slot = FrontEndProfileSlot::First,
                  .character_slot = FrontEndCharacterSlot::First,
              } &&
              route.front_end_state() == briefing_load &&
              route.front_end_active_profile_is_confirmed() &&
              route.front_end_active_character_is_confirmed() &&
              character_frame->frame_plan()->simulation_steps == 0U &&
              route.scheduler_state() == route_scheduler_origin &&
              SameSimulation(route.simulation_state(), route_world_origin),
        "agent selection opens only the agent mirror and remains modal");

    auto character_release = route.Next();
    auto play_frame = route.Next();
    Check(character_release && character_release->frame_plan() &&
              character_release->front_end_command() == FrontEndCommand{} &&
              play_frame && play_frame->frame_plan() &&
              play_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::StartCampaign,
              } &&
              route.front_end_state() == gameplay_load &&
              route.front_end_active_profile_is_confirmed() &&
              route.front_end_active_character_is_confirmed() &&
              play_frame->frame_plan()->simulation_steps == 1U &&
              play_frame->frame_plan()->interpolation_alpha == 0.5 &&
              route.scheduler_state() == FrameSchedulerState{
                  .config = route_config.scheduler,
                  .accumulated_remainder = milliseconds{5},
                  .total_planned_steps = 1U,
              } &&
              route.debug_locomotion_position() == Position3{} &&
              route.state() == RunReplaySessionState::Complete,
        "only the confirmed canonical route enters Gameplay and schedules its own elapsed value");

    const std::array closed_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = no_transitions},
    };
    RunReplaySessionConfig closed_config = ValidConfig();
    closed_config.initial_front_end_state = briefing_load;
    closed_config.front_end_capabilities.supports_character_selection = true;
    auto closed_created = RunReplaySession::Create(
        BuildScriptedPair(primary_action, closed_frames), closed_config);
    RunReplaySession closed = TakeSession(
        closed_created, "the closed campaign-support replay is created");
    const auto closed_scheduler = closed.scheduler_state();
    const auto closed_world = closed.simulation_state();
    auto closed_frame = closed.Next();
    Check(closed_frame && closed_frame->frame_plan() &&
              closed_frame->front_end_command() == FrontEndCommand{} &&
              closed.front_end_state() == omega::app::InitialFrontEndState() &&
              closed_frame->frame_plan()->simulation_steps == 0U &&
              closed.scheduler_state() == closed_scheduler &&
              SameSimulation(closed.simulation_state(), closed_world),
        "closed campaign support fails Briefing Room to Title before scheduling");
}

void CheckFrontEndActionAliasReplay()
{
    constexpr std::array actions{
        omega::app::kDebugMoveLeftAction,
        omega::app::kDebugMoveRightAction,
        omega::app::kDebugFireAction,
        omega::app::kDebugTargetAction,
    };
    constexpr std::array left_down{
        InputTransition{.code = 0U, .pressed = true}};
    constexpr std::array right_down{
        InputTransition{.code = 1U, .pressed = true}};
    constexpr std::array fire_down{
        InputTransition{.code = 2U, .pressed = true}};
    constexpr std::array target_down{
        InputTransition{.code = 3U, .pressed = true}};
    constexpr std::array fire_and_target_down{
        InputTransition{.code = 2U, .pressed = true},
        InputTransition{.code = 3U, .pressed = true},
    };
    constexpr FrontEndState title_load{
        .mode = FrontEndMode::Title,
        .selected_main_row = FrontEndMainRow::LoadAgent,
    };

    const std::array left_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = left_down},
    };
    RunReplaySessionConfig left_config = ValidConfig();
    left_config.initial_front_end_state = title_load;
    auto left_created = RunReplaySession::Create(
        BuildScriptedPair(actions, left_frames), left_config);
    RunReplaySession left = TakeSession(
        left_created, "the modal left-alias replay is created");
    auto left_frame = left.Next();
    Check(left_frame && left_frame->frame_plan() &&
              left.front_end_state() == omega::app::InitialFrontEndState() &&
              left_frame->front_end_command() == FrontEndCommand{} &&
              left_frame->frame_plan()->simulation_steps == 0U,
        "action 4 replays as canonical Title previous without physical provenance");

    const std::array right_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = right_down},
    };
    RunReplaySessionConfig right_config = ValidConfig();
    right_config.initial_front_end_state = title_load;
    auto right_created = RunReplaySession::Create(
        BuildScriptedPair(actions, right_frames), right_config);
    RunReplaySession right = TakeSession(
        right_created, "the modal right-alias replay is created");
    auto right_frame = right.Next();
    Check(right_frame && right_frame->frame_plan() &&
              right.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::Title,
                  .selected_main_row = FrontEndMainRow::Quit,
              } &&
              right_frame->front_end_command() == FrontEndCommand{} &&
              right_frame->frame_plan()->simulation_steps == 0U,
        "action 5 replays as canonical Title next without physical provenance");

    const std::array fire_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = fire_down},
    };
    RunReplaySessionConfig fire_config = ValidConfig();
    fire_config.initial_front_end_state = title_load;
    fire_config.front_end_visible_profile_slots = 1U;
    fire_config.front_end_total_profile_count = 1U;
    fire_config.front_end_capabilities.supports_character_selection = true;
    auto fire_created = RunReplaySession::Create(
        BuildScriptedPair(actions, fire_frames), fire_config);
    RunReplaySession fire = TakeSession(
        fire_created, "the modal fire-select replay is created");
    auto fire_frame = fire.Next();
    Check(fire_frame && fire_frame->frame_plan() &&
              fire_frame->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmProfileOwner,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              fire.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::AgentSelection,
                  .selected_main_row = FrontEndMainRow::LoadAgent,
              } &&
              fire.front_end_active_profile_is_confirmed() &&
              fire_frame->frame_plan()->simulation_steps == 0U,
        "action 8 replays as modal select without physical-device provenance");

    const std::array target_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4},
            .transitions = target_down},
    };
    RunReplaySessionConfig target_config = ValidConfig();
    target_config.initial_front_end_state = FrontEndState{
        .mode = FrontEndMode::AgentCreation,
        .selected_main_row = FrontEndMainRow::CreateAgent,
    };
    target_config.front_end_capabilities.supports_character_selection = true;
    auto target_created = RunReplaySession::Create(
        BuildScriptedPair(actions, target_frames), target_config);
    RunReplaySession target = TakeSession(
        target_created, "the modal target-back replay is created");
    auto target_frame = target.Next();
    Check(target_frame && target_frame->frame_plan() &&
              target_frame->front_end_command() == FrontEndCommand{} &&
              target.front_end_state() == omega::app::InitialFrontEndState() &&
              target_frame->frame_plan()->simulation_steps == 0U,
        "action 9 replays as modal back without physical-device provenance");

    const std::array gameplay_frames{
        ScriptedElapsedFrame{.elapsed = milliseconds{15},
            .transitions = fire_and_target_down},
    };
    RunReplaySessionConfig gameplay_config = ValidConfig();
    gameplay_config.initial_front_end_state = kGameplayFrontEndState;
    gameplay_config.front_end_capabilities.can_start_diagnostic_campaign = true;
    auto gameplay_created = RunReplaySession::Create(
        BuildScriptedPair(actions, gameplay_frames), gameplay_config);
    RunReplaySession gameplay = TakeSession(
        gameplay_created, "the gameplay fire/target replay is created");
    auto gameplay_frame = gameplay.Next();
    Check(gameplay_frame &&
              gameplay_frame->front_end_command() == FrontEndCommand{} &&
              gameplay.front_end_state() == kGameplayFrontEndState &&
              gameplay_frame->frame_plan() &&
              gameplay_frame->frame_plan()->simulation_steps == 1U,
        "actions 8 and 9 remain gameplay-only in Gameplay");
}

void CheckCharacterFlowReplay()
{
    constexpr std::array actions{
        omega::app::kFrontEndPrimaryAction,
        omega::app::kFrontEndCancelAction,
        omega::app::kDebugFireAction,
    };
    constexpr std::array primary_down{
        InputTransition{.code = 0U, .pressed = true}};
    constexpr std::array primary_up{
        InputTransition{.code = 0U, .pressed = false}};
    constexpr std::array cancel_down{
        InputTransition{.code = 1U, .pressed = true}};
    constexpr std::array cancel_up{
        InputTransition{.code = 1U, .pressed = false}};
    constexpr std::array fire_down{
        InputTransition{.code = 2U, .pressed = true}};

    const std::array creation_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = cancel_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = cancel_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = milliseconds{15}, .transitions = fire_down},
    };
    RunReplaySessionConfig config = ValidConfig();
    config.enable_debug_locomotion = true;
    config.initial_front_end_state = omega::app::InitialFrontEndState();
    config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = true,
        .can_start_diagnostic_campaign = true,
        .requires_active_profile_for_diagnostic_play = true,
        .supports_character_selection = true,
        .can_create_first_character = true,
        .requires_active_character_for_diagnostic_play = true,
    };
    auto created = RunReplaySession::Create(
        BuildScriptedPair(actions, creation_frames), config);
    RunReplaySession source = TakeSession(
        created, "the canonical Create Agent replay is created");
    const auto scheduler_origin = source.scheduler_state();
    const auto world_origin = source.simulation_state();

    auto owner_create = source.Next();
    auto owner_create_release = source.Next();
    auto owner_confirm = source.Next();
    Check(owner_create && owner_create->frame_plan() &&
              owner_create->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::CreateProfileOwner,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              owner_create_release && owner_create_release->frame_plan() &&
              owner_create_release->front_end_command() == FrontEndCommand{} &&
              owner_confirm && owner_confirm->frame_plan() &&
              owner_confirm->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmProfileOwner,
                  .profile_slot = FrontEndProfileSlot::First,
              } &&
              source.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::AgentCreation,
                  .selected_main_row = FrontEndMainRow::CreateAgent,
              } &&
              source.front_end_active_profile_is_confirmed() &&
              !source.front_end_active_character_is_confirmed() &&
              source.scheduler_state() == scheduler_origin &&
              SameSimulation(source.simulation_state(), world_origin),
        "Create Agent creates then explicitly confirms its owner without scheduling");

    auto owner_confirm_release = source.Next();
    auto agent_create = source.Next();
    auto agent_create_release = source.Next();
    auto agent_confirm = source.Next();
    Check(owner_confirm_release && owner_confirm_release->frame_plan() &&
              agent_create && agent_create->frame_plan() &&
              agent_create->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::CreateAgent,
                  .profile_slot = FrontEndProfileSlot::First,
                  .character_slot = FrontEndCharacterSlot::First,
              } &&
              agent_create_release && agent_create_release->frame_plan() &&
              agent_confirm && agent_confirm->frame_plan() &&
              agent_confirm->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmAgent,
                  .profile_slot = FrontEndProfileSlot::First,
                  .character_slot = FrontEndCharacterSlot::First,
              } &&
              source.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::BriefingRoom,
                  .selected_main_row = FrontEndMainRow::CreateAgent,
              } &&
              source.front_end_active_profile_is_confirmed() &&
              source.front_end_active_character_is_confirmed() &&
              source.scheduler_state() == scheduler_origin &&
              SameSimulation(source.simulation_state(), world_origin),
        "agent creation advances only its selected bounded snapshot and confirmation enters Briefing Room");

    RunReplaySession replay = std::move(source);
    Check(source.state() == RunReplaySessionState::Inert &&
              !source.front_end_state() &&
              !source.front_end_active_profile_is_confirmed() &&
              !source.front_end_active_character_is_confirmed() &&
              replay.front_end_active_profile_is_confirmed() &&
              replay.front_end_active_character_is_confirmed(),
        "move construction transfers both replay mirrors and leaves the source inert");

    auto selection_release = replay.Next();
    auto back_to_selection = replay.Next();
    auto cancel_release_frame = replay.Next();
    auto reselect = replay.Next();
    Check(selection_release && selection_release->frame_plan() &&
              back_to_selection && back_to_selection->frame_plan() &&
              back_to_selection->front_end_command() == FrontEndCommand{} &&
              back_to_selection->frame_plan()->simulation_steps == 0U &&
              cancel_release_frame && cancel_release_frame->frame_plan() &&
              reselect && reselect->frame_plan() &&
              reselect->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmAgent,
                  .profile_slot = FrontEndProfileSlot::First,
                  .character_slot = FrontEndCharacterSlot::First,
              } &&
              replay.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::BriefingRoom,
                  .selected_main_row = FrontEndMainRow::CreateAgent,
              } &&
              replay.front_end_active_profile_is_confirmed() &&
              replay.front_end_active_character_is_confirmed() &&
              replay.scheduler_state() == scheduler_origin &&
              SameSimulation(replay.simulation_state(), world_origin),
        "Briefing cancel and explicit reselection preserve authorization while remaining modal");

    auto reselect_release = replay.Next();
    auto started = replay.Next();
    Check(reselect_release && reselect_release->frame_plan() &&
              started && started->frame_plan() &&
              started->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::StartCampaign,
              } &&
              replay.front_end_state() == kGameplayFrontEndState &&
              replay.front_end_active_profile_is_confirmed() &&
              replay.front_end_active_character_is_confirmed() &&
              started->frame_plan()->simulation_steps == 1U &&
              started->frame_plan()->interpolation_alpha == 0.5 &&
              replay.debug_locomotion_position() == Position3{} &&
              replay.state() == RunReplaySessionState::Complete,
        "fire-select mission confirmation enters Gameplay without leaking modal input");

    const std::array load_frames{
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_up},
        ScriptedElapsedFrame{.elapsed = std::chrono::seconds{4}, .transitions = primary_down},
    };
    RunReplaySessionConfig load_config = ValidConfig();
    load_config.initial_front_end_state = FrontEndState{
        .mode = FrontEndMode::Title,
        .selected_main_row = FrontEndMainRow::LoadAgent,
        .selected_profile_slot = FrontEndProfileSlot::Second,
    };
    load_config.front_end_visible_profile_slots = 2U;
    load_config.front_end_total_profile_count = 2U;
    load_config.front_end_visible_character_slots_by_profile[1U] = 1U;
    load_config.front_end_total_character_counts_by_profile[1U] = 1U;
    load_config.front_end_capabilities.supports_character_selection = true;
    auto load_created = RunReplaySession::Create(
        BuildScriptedPair(actions, load_frames), load_config);
    RunReplaySession load = TakeSession(
        load_created, "the profile-scoped Load Agent replay is created");
    auto load_owner = load.Next();
    auto load_release = load.Next();
    auto load_agent = load.Next();
    Check(load_owner && load_owner->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmProfileOwner,
                  .profile_slot = FrontEndProfileSlot::Second,
              } &&
              load_release && load_agent &&
              load_agent->front_end_command() == FrontEndCommand{
                  .type = FrontEndCommandType::ConfirmAgent,
                  .profile_slot = FrontEndProfileSlot::Second,
                  .character_slot = FrontEndCharacterSlot::First,
              } &&
              load.front_end_state() == FrontEndState{
                  .mode = FrontEndMode::BriefingRoom,
                  .selected_main_row = FrontEndMainRow::LoadAgent,
                  .selected_profile_slot = FrontEndProfileSlot::Second,
              } &&
              load.front_end_active_profile_is_confirmed() &&
              load.front_end_active_character_is_confirmed(),
        "Load Agent resolves only the selected profile's bounded character snapshot");
}

void CheckMoveLifecycle()
{
    constexpr std::array<std::uint32_t, 1U> actions{17U};
    constexpr std::array<nanoseconds, 2U> elapsed_values{
        milliseconds{5}, milliseconds{25}};
    RunCaptureTracePair pair = BuildPair(actions, 4U, elapsed_values);
    RunReplaySessionConfig config = ValidConfig();
    config.initial_front_end_state =
        omega::app::InitialFrontEndState();
    auto created = RunReplaySession::Create(std::move(pair), config);
    RunReplaySession source = TakeSession(created, "the move fixture is created");

    auto first_result = source.Next();
    if (!first_result)
        FailFixture("move fixture first frame");
    RunReplayFrame first = std::move(*first_result);
    RunReplayFrame moved_frame = std::move(first);
    Check(moved_frame.input().frame_index() == 0U &&
              moved_frame.elapsed() == elapsed_values[0] &&
              moved_frame.frame_plan().has_value() &&
              !moved_frame.terminal_input(),
        "frame move construction transfers the complete owned publication");

    const auto scheduler_before_move = source.scheduler_state();
    const auto simulation_before_move = source.simulation_state();
    const auto menu_before_move = source.front_end_state();
    RunReplaySession destination = std::move(source);
    Check(source.state() == RunReplaySessionState::Inert &&
              source.remaining_frames() == 0U && !source.scheduler_state() &&
              !source.simulation_state() && !source.front_end_state(),
        "a moved-from replay session is observably inert without owners");
    const auto inert_next = source.Next();
    CheckError(inert_next, RunReplayOperation::Next,
        RunReplayErrorCode::InvalidSessionState,
        "a moved-from replay session returns the fixed invalid-state error");

    Check(destination.state() == RunReplaySessionState::Ready &&
              destination.remaining_frames() == 1U &&
              destination.scheduler_state() == scheduler_before_move &&
              SameSimulation(destination.simulation_state(), simulation_before_move) &&
              destination.front_end_state() == menu_before_move,
        "session move construction transfers cursor, menu, and both fresh owners exactly");
    auto final = destination.Next();
    Check(final && final->input().frame_index() == 1U &&
              final->elapsed() == elapsed_values[1] && final->frame_plan() &&
              destination.front_end_state() == menu_before_move &&
              destination.state() == RunReplaySessionState::Complete &&
              destination.remaining_frames() == 0U,
        "the moved-to session resumes the exact remaining frame and completes");
}

void CheckReplayAllocationRetry()
{
    constexpr std::array<std::uint32_t, 1U> actions{19U};
    constexpr std::array<nanoseconds, 1U> elapsed_values{milliseconds{31}};
    RunCaptureTracePair pair = BuildPair(actions, 1U, elapsed_values);
    RunReplaySessionConfig config = ValidConfig();
    config.initial_front_end_state = kGameplayFrontEndState;
    config.front_end_capabilities.can_start_diagnostic_campaign = true;
    auto created = RunReplaySession::Create(std::move(pair), config);
    RunReplaySession session =
        TakeSession(created, "the allocation-retry fixture is created");
    const auto scheduler_before = session.scheduler_state();
    const auto simulation_before = session.simulation_state();
    const auto menu_before = session.front_end_state();

    replay_session_test_allocation::Arm(0U);
    const auto object_failure = session.Next();
    replay_session_test_allocation::Disarm();
    CheckError(object_failure, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayNextFailed,
        "snapshot object allocation maps through the app replay layer",
        RunCaptureReplayErrorCode::AllocationFailed);
    Check(session.state() == RunReplaySessionState::Ready &&
              session.remaining_frames() == 1U &&
              session.scheduler_state() == scheduler_before &&
              SameSimulation(session.simulation_state(), simulation_before) &&
              session.front_end_state() == menu_before,
        "snapshot object allocation failure changes no app replay or menu state");

    replay_session_test_allocation::Arm(1U);
    const auto backing_failure = session.Next();
    replay_session_test_allocation::Disarm();
    CheckError(backing_failure, RunReplayOperation::Next,
        RunReplayErrorCode::ReplayNextFailed,
        "snapshot backing allocation maps through the app replay layer",
        RunCaptureReplayErrorCode::AllocationFailed);
    Check(session.state() == RunReplaySessionState::Ready &&
              session.remaining_frames() == 1U &&
              session.scheduler_state() == scheduler_before &&
              SameSimulation(session.simulation_state(), simulation_before) &&
              session.front_end_state() == menu_before,
        "snapshot backing allocation failure changes no app replay or menu state");

    auto retried = session.Next();
    const auto plan = retried ? retried->frame_plan() : std::nullopt;
    const auto scheduler_after = session.scheduler_state();
    const auto simulation_after = session.simulation_state();
    Check(retried && retried->input().frame_index() == 0U &&
              retried->elapsed() == elapsed_values[0] &&
              !retried->terminal_input() && plan &&
              plan->simulation_steps == 2U && plan->interpolation_alpha == 0.5 &&
              plan->clamped_delta && !plan->dropped_time && scheduler_after &&
              *scheduler_after == FrameSchedulerState{
                                      .config = ValidConfig().scheduler,
                                      .accumulated_remainder = milliseconds{5},
                                      .total_planned_steps = 2U,
                                  } &&
              simulation_after &&
              SameSimulation(*simulation_after,
                  SimulationState{
                      .completed_steps = 2U,
                      .simulated_time = milliseconds{20},
                  }) &&
              session.front_end_state() == menu_before &&
              session.state() == RunReplaySessionState::Complete,
        "the exact failed frame retries once and advances fresh owners exactly");
}
} // namespace

int main()
{
    CheckContractAndTaxonomy();
    CheckCreatePriorityAndPairRetention();
    CheckEmptyAndMaximumOrigin();
    CheckElapsedOracleAndPartialPrefix();
    CheckTerminalBehavior();
    CheckDebugLocomotionOptIn();
    CheckDiagnosticProximityTriggerReplay();
    CheckDiagnosticTargetFireReplay();
    CheckDiagnosticMissionLifecycleReplay();
    CheckFrontEndModalGate();
    CheckFirstProfileCreationReplay();
    CheckDiagnosticPlayGateReplay();
    CheckFrontEndActionAliasReplay();
    CheckCharacterFlowReplay();
    CheckMoveLifecycle();
    CheckReplayAllocationRetry();

    if (failures == 0)
        std::cout << "omega_run_replay_session_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
