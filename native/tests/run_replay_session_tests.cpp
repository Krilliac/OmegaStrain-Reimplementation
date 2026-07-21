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
using omega::runtime::FramePlan;
using omega::runtime::FrameScheduler;
using omega::runtime::FrameSchedulerState;
using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputTracker;
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

int failures = 0;

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
    static_assert(noexcept(
        std::declval<const RunReplaySession&>().front_end_state()));

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
        decltype(std::declval<const RunReplaySession&>().front_end_state()),
        std::optional<FrontEndState>>);
    static_assert(std::is_nothrow_move_constructible_v<CreateResult>);
    static_assert(std::is_nothrow_move_constructible_v<NextResult>);

    constexpr RunReplaySessionConfig default_config;
    static_assert(default_config.scheduler == omega::runtime::FrameSchedulerConfig{});
    static_assert(default_config.maximum_entities == 65'536U);
    static_assert(!default_config.enable_debug_locomotion);
    static_assert(!default_config.initial_front_end_state);
    static_assert(default_config.front_end_visible_profile_slots == 0U);
    static_assert(default_config.front_end_total_profile_count == 0U);
    static_assert(!default_config.front_end_capabilities.can_create_first_profile);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::SimulationRepresentationExhausted) == 7);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DebugLocomotionEntityCreateFailed) == 8);
    static_assert(static_cast<int>(
                      RunReplayErrorCode::DebugLocomotionPlanFailed) == 9);

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
    Check(enabled.debug_locomotion_position() == Position3{} &&
              enabled_initial_state &&
              SameSimulation(*enabled_initial_state, SimulationState{
                  .alive_entities = 1U,
              }),
        "opt-in replay owns one positioned synthetic diagnostic entity at the origin");

    auto moved = enabled.Next();
    const auto moved_plan = moved ? moved->frame_plan() : std::nullopt;
    const auto moved_state = enabled.simulation_state();
    Check(moved && moved_plan && moved_plan->simulation_steps == 2U &&
              moved->input().IsHeld(omega::app::kDebugMoveForwardAction) &&
              !moved->input().IsHeld(omega::app::kDebugMoveBackwardAction) &&
              !moved->input().IsHeld(omega::app::kDebugMoveLeftAction) &&
              !moved->input().IsHeld(omega::app::kDebugMoveRightAction) &&
              enabled.debug_locomotion_position() == Position3{.z = 2} &&
              moved_state && SameSimulation(*moved_state, SimulationState{
                  .completed_steps = 2U,
                  .simulated_time = milliseconds{20},
                  .alive_entities = 1U,
              }) &&
              enabled.state() == RunReplaySessionState::Ready,
        "one held command is planned once and its same translation reaches every fixed step");

    const auto scheduler_before_move = enabled.scheduler_state();
    const auto simulation_before_move = enabled.simulation_state();
    const auto position_before_move = enabled.debug_locomotion_position();
    RunReplaySession moved_enabled = std::move(enabled);
    Check(enabled.state() == RunReplaySessionState::Inert &&
              enabled.remaining_frames() == 0U && !enabled.scheduler_state() &&
              !enabled.simulation_state() && !enabled.debug_locomotion_position(),
        "moving an enabled replay leaves the source inert and positionless");
    Check(moved_enabled.state() == RunReplaySessionState::Ready &&
              moved_enabled.remaining_frames() == 2U &&
              moved_enabled.scheduler_state() == scheduler_before_move &&
              SameSimulation(moved_enabled.simulation_state(), simulation_before_move) &&
              moved_enabled.debug_locomotion_position() == position_before_move,
        "moving an enabled replay transfers its exact positioned world and pending frames");

    auto released = moved_enabled.Next();
    const auto released_plan = released ? released->frame_plan() : std::nullopt;
    const auto released_state = moved_enabled.simulation_state();
    Check(released && released_plan && released_plan->simulation_steps == 1U &&
              released->input().WasReleased(omega::app::kDebugMoveForwardAction) &&
              !released->input().IsHeld(omega::app::kDebugMoveForwardAction) &&
              moved_enabled.debug_locomotion_position() == Position3{.z = 2} &&
              released_state && SameSimulation(*released_state, SimulationState{
                  .completed_steps = 3U,
                  .simulated_time = milliseconds{30},
                  .alive_entities = 1U,
              }) &&
              moved_enabled.state() == RunReplaySessionState::Ready,
        "a released action advances the clock with a neutral translation and stops movement");

    const auto scheduler_before_terminal = moved_enabled.scheduler_state();
    const auto simulation_before_terminal = moved_enabled.simulation_state();
    const auto position_before_terminal = moved_enabled.debug_locomotion_position();
    auto terminal = moved_enabled.Next();
    Check(terminal && terminal->terminal_input() && !terminal->elapsed() &&
              !terminal->frame_plan() &&
              !terminal->input().IsHeld(omega::app::kDebugMoveForwardAction) &&
              moved_enabled.state() == RunReplaySessionState::Complete &&
              moved_enabled.scheduler_state() == scheduler_before_terminal &&
              SameSimulation(moved_enabled.simulation_state(), simulation_before_terminal) &&
              moved_enabled.debug_locomotion_position() == position_before_terminal,
        "a terminal frame cannot mutate scheduler or synthetic position");

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
              disabled_state && SameSimulation(*disabled_state, SimulationState{
                  .completed_steps = 2U,
                  .simulated_time = milliseconds{20},
              }),
        "the default-disabled replay preserves clock-only E0059 behavior for the same schema");
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
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::StartDiagnostic},
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::Profiles},
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::Profiles},
        FrontEndState{.mode = FrontEndMode::Profiles,
            .selected_main_row = FrontEndMainRow::Profiles},
        FrontEndState{.mode = FrontEndMode::Profiles,
            .selected_main_row = FrontEndMainRow::Profiles},
        FrontEndState{.mode = FrontEndMode::Profiles,
            .selected_main_row = FrontEndMainRow::Profiles,
            .selected_profile_slot = FrontEndProfileSlot::Second},
        FrontEndState{.mode = FrontEndMode::Profiles,
            .selected_main_row = FrontEndMainRow::Profiles,
            .selected_profile_slot = FrontEndProfileSlot::Second},
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::Profiles},
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::Profiles},
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::StartDiagnostic},
        FrontEndState{.mode = FrontEndMode::Main,
            .selected_main_row = FrontEndMainRow::StartDiagnostic},
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
    auto modal_created = RunReplaySession::Create(std::move(modal_pair), modal_config);
    RunReplaySession modal =
        TakeSession(modal_created, "the main-menu modal replay is created");
    const auto modal_scheduler_origin = modal.scheduler_state();
    const auto modal_world_origin = modal.simulation_state();
    const auto modal_position_origin = modal.debug_locomotion_position();
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
                                 .type = FrontEndCommandType::SetActiveProfile,
                                 .profile_slot = FrontEndProfileSlot::Second,
                             }
                           : FrontEndCommand{}) &&
                  modal.scheduler_state() == modal_scheduler_origin &&
                  SameSimulation(modal.simulation_state(), modal_world_origin) &&
                  modal.debug_locomotion_position() == modal_position_origin,
            "main and profile-card edges preserve held-versus-pressed input, discard elapsed, and freeze every simulation owner");
    }
    auto activated = modal.Next();
    const auto activated_plan = activated ? activated->frame_plan() : std::nullopt;
    const auto activated_scheduler = modal.scheduler_state();
    const auto activated_world = modal.simulation_state();
    Check(activated && activated->elapsed() == milliseconds{15} && activated_plan &&
              activated_plan->simulation_steps == 1U &&
              activated_plan->interpolation_alpha == 0.5 &&
              !activated_plan->clamped_delta && !activated_plan->dropped_time &&
              modal.front_end_state() == FrontEndState{} &&
              activated_scheduler &&
              *activated_scheduler == FrameSchedulerState{
                  .config = modal_config.scheduler,
                  .accumulated_remainder = milliseconds{5},
                  .total_planned_steps = 1U,
              } &&
              activated_world && SameSimulation(*activated_world,
                  SimulationState{
                      .completed_steps = 1U,
                      .simulated_time = milliseconds{10},
                      .alive_entities = 1U,
                  }) &&
              modal.debug_locomotion_position() == Position3{},
        "activation schedules only its own elapsed sample after large modal time is discarded");

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
    constexpr FrontEndState topology_row{
        .mode = FrontEndMode::AssetTopology,
        .selected_main_row = FrontEndMainRow::AssetTopology,
    };
    constexpr FrontEndState main_asset_row{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::AssetTopology,
    };
    constexpr FrontEndState main_controls_row{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::Controls,
    };
    constexpr FrontEndState main_profiles_row{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::Profiles,
    };
    constexpr std::array expected_topology_modal_states{
        topology_row,
        topology_row,
        topology_row,
        main_asset_row,
        main_controls_row,
        main_controls_row,
        main_profiles_row,
        main_profiles_row,
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
            "asset-topology entry, held/release edges, return, and navigation discard elapsed and freeze every simulation owner");
    }
    auto topology_activated = topology.Next();
    const auto topology_activated_plan =
        topology_activated ? topology_activated->frame_plan() : std::nullopt;
    const auto topology_activated_scheduler = topology.scheduler_state();
    const auto topology_activated_world = topology.simulation_state();
    Check(topology_activated &&
              topology_activated->elapsed() == milliseconds{15} &&
              topology_activated_plan &&
              topology_activated_plan->simulation_steps == 1U &&
              topology_activated_plan->interpolation_alpha == 0.5 &&
              !topology_activated_plan->clamped_delta &&
              !topology_activated_plan->dropped_time &&
              topology.front_end_state() == FrontEndState{} &&
              topology_activated_scheduler &&
              *topology_activated_scheduler == FrameSchedulerState{
                  .config = topology_config.scheduler,
                  .accumulated_remainder = milliseconds{5},
                  .total_planned_steps = 1U,
              } &&
              topology_activated_world &&
              SameSimulation(*topology_activated_world,
                  SimulationState{
                      .completed_steps = 1U,
                      .simulated_time = milliseconds{10},
                      .alive_entities = 1U,
                  }) &&
              topology.debug_locomotion_position() == Position3{} &&
              topology.state() == RunReplaySessionState::Complete,
        "diagnostic play resumes with only its own elapsed after every asset-topology sample is discarded");

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
    reopen_config.initial_front_end_state = FrontEndState{};
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
    for (std::size_t index = 1U; index < 4U; ++index)
    {
        auto frame = reopen.Next();
        const auto plan = frame ? frame->frame_plan() : std::nullopt;
        Check(frame && frame->elapsed() == reopen_frames[index].elapsed && plan &&
                  plan->simulation_steps == 0U && !plan->clamped_delta &&
                  !plan->dropped_time &&
                  reopen.front_end_state() ==
                      omega::app::InitialFrontEndState() &&
                  reopen.scheduler_state() == scheduler_before_open &&
                  SameSimulation(reopen.simulation_state(), world_before_open) &&
                  reopen.debug_locomotion_position() == position_before_open,
            "opening with held movement and subsequent large modal frames call no locomotion or simulation work");
    }
    auto resumed = reopen.Next();
    const auto resumed_plan = resumed ? resumed->frame_plan() : std::nullopt;
    Check(resumed && resumed->elapsed() == milliseconds{5} && resumed_plan &&
              resumed_plan->simulation_steps == 1U &&
              resumed_plan->interpolation_alpha == 0.0 &&
              reopen.front_end_state() == FrontEndState{} &&
              reopen.scheduler_state() == FrameSchedulerState{
                  .config = reopen_config.scheduler,
                  .total_planned_steps = 1U,
              } &&
              reopen.debug_locomotion_position() == Position3{.z = 1},
        "reactivation combines the preserved remainder with only its own elapsed and then resumes held movement");

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
                  FrontEndState{
                      .mode = FrontEndMode::Main,
                      .selected_main_row = FrontEndMainRow::Profiles,
                      .selected_profile_slot = FrontEndProfileSlot::First,
                  } &&
              cancel_plan->simulation_steps == 0U &&
              cancel_priority.scheduler_state() == cancel_scheduler_origin &&
              SameSimulation(cancel_priority.simulation_state(), cancel_world_origin) &&
              cancel_priority.debug_locomotion_position() == cancel_position_origin,
        "replay cancel has priority over confirm and navigation, returns Profiles to its Main row, and emits no command");

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
        .mode = FrontEndMode::Profiles,
        .selected_main_row = FrontEndMainRow::Profiles,
        .selected_profile_slot = FrontEndProfileSlot::First,
    };
    constexpr FrontEndState main_profiles{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::Profiles,
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
              replay.front_end_state() == main_profiles &&
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
              legacy.front_end_state() == main_profiles &&
              legacy_frame->frame_plan()->simulation_steps == 0U &&
              legacy.scheduler_state() == legacy_scheduler_origin &&
              SameSimulation(legacy.simulation_state(), legacy_simulation_origin),
        "default-false replay capability preserves the legacy empty-profile return behavior");

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
              nonempty_total.front_end_state() == main_profiles,
        "a nonzero startup total keeps an explicit creation request closed");

    RunCaptureTracePair nonempty_visible_pair =
        BuildScriptedPair(primary_action, one_primary_frame);
    RunReplaySessionConfig nonempty_visible_config = ValidConfig();
    nonempty_visible_config.initial_front_end_state = profiles_first;
    nonempty_visible_config.front_end_visible_profile_slots = 1U;
    nonempty_visible_config.front_end_total_profile_count = 0U;
    nonempty_visible_config.front_end_capabilities = FrontEndCapabilities{
        .can_create_first_profile = true,
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
              nonempty_visible.front_end_state() == main_profiles,
        "a nonzero startup visible count keeps creation closed and preserves ordinary first-slot selection");

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
    config.initial_front_end_state = FrontEndState{};
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
    CheckFrontEndModalGate();
    CheckFirstProfileCreationReplay();
    CheckMoveLifecycle();
    CheckReplayAllocationRetry();

    if (failures == 0)
        std::cout << "omega_run_replay_session_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
