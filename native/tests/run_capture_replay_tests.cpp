#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/run_capture_replay.h"
#include "omega/simulation/simulation_world.h"

#include <algorithm>
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

namespace replay_test_allocation
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
} // namespace replay_test_allocation

void* operator new(const std::size_t size)
{
    if (replay_test_allocation::allocations_before_failure !=
        replay_test_allocation::kDisabled)
    {
        if (replay_test_allocation::allocations_before_failure == 0U)
        {
            replay_test_allocation::Disarm();
            throw std::bad_alloc{};
        }
        --replay_test_allocation::allocations_before_failure;
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
using omega::runtime::FramePlan;
using omega::runtime::FrameScheduler;
using omega::runtime::FrameSchedulerConfig;
using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputSnapshot;
using omega::runtime::InputTracker;
using omega::runtime::PointerPositionQ16;
using omega::runtime::RunCaptureReplayError;
using omega::runtime::RunCaptureReplayErrorCode;
using omega::runtime::RunCaptureReplayFrame;
using omega::runtime::RunCaptureReplayOperation;
using omega::runtime::RunCaptureReplaySession;
using omega::runtime::RunCaptureSession;
using omega::runtime::RunCaptureSessionConfig;
using omega::runtime::RunCaptureSessionError;
using omega::runtime::RunCaptureTerminalInput;
using omega::runtime::RunCaptureTracePair;
using omega::simulation::SimulationStepResult;
using omega::simulation::SimulationWorld;

int failures = 0;

constexpr PointerPositionQ16 kFirstPointerPosition{.x = 111U, .y = 222U};
constexpr PointerPositionQ16 kSecondPointerPosition{
    .x = omega::runtime::kNormalizedInputExtent,
    .y = 0U,
};

[[noreturn]] void FailFixture(const std::string_view site) noexcept
{
    std::fputs("FAILED: replay test fixture: ", stderr);
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
bool CheckReplayError(const std::expected<Value, RunCaptureReplayError>& result,
    const RunCaptureReplayOperation operation,
    const RunCaptureReplayErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().operation == operation &&
                         result.error().code == code &&
                         result.error().message ==
                             omega::runtime::RunCaptureReplayErrorMessage(code);
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
        FailFixture("MakeTracker binding table creation");
    auto tracker = InputTracker::Create(std::move(*table), 16U);
    if (!tracker)
        FailFixture("MakeTracker input tracker creation");
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

[[nodiscard]] RunCaptureTracePair BuildPair(
    const std::span<const std::uint32_t> actions,
    const std::size_t maximum_frames,
    const std::span<const nanoseconds> elapsed_values,
    const std::optional<TerminalReasons> terminal = std::nullopt)
{
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = maximum_frames}, actions);
    if (!created)
        FailFixture("BuildPair capture session creation");
    RunCaptureSession capture = std::move(*created);
    InputTracker tracker = MakeTracker(actions);

    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        if (index == 0U)
        {
            if (!Push(tracker, 0U, true) ||
                !tracker.SetPointerPosition(kFirstPointerPosition))
            {
                FailFixture("BuildPair initial input");
            }
        }
        if (index == 1U)
        {
            if (!Push(tracker, 0U, false))
                FailFixture("BuildPair release");
            const auto rejected = tracker.PushEvent(InputEvent{
                .device = static_cast<InputDevice>(255),
                .code = 0U,
                .pressed = true,
            });
            if (rejected)
                FailFixture("BuildPair invalid event rejection");
            if (!tracker.SetPointerPosition(kSecondPointerPosition))
                FailFixture("BuildPair replacement pointer");
        }
        if (index == 2U)
            tracker.ClearPointerPosition();

        const InputSnapshot snapshot = tracker.EndFrame();
        if (!capture.AppendInput(snapshot) ||
            !capture.AppendElapsed(elapsed_values[index]))
        {
            FailFixture("BuildPair elapsed-frame append");
        }
    }

    if (terminal)
    {
        const InputSnapshot snapshot = tracker.EndFrame();
        if (!capture.AppendInput(snapshot) ||
            !capture.MarkTerminal(terminal->host_quit_requested,
                terminal->logical_quit_pressed))
        {
            FailFixture("BuildPair terminal-frame append");
        }
    }

    auto finished = std::move(capture).Finish();
    if (!finished)
        FailFixture("BuildPair capture finish");
    return std::move(*finished);
}

[[nodiscard]] RunCaptureReplaySession TakeReplay(
    std::expected<RunCaptureReplaySession, RunCaptureReplayError>& created,
    const std::string_view message)
{
    if (!created)
        FailFixture(message);
    return std::move(*created);
}

void CheckContract()
{
    static_assert(std::is_trivially_copyable_v<PointerPositionQ16>);
    static_assert(std::is_standard_layout_v<PointerPositionQ16>);
    static_assert(!std::is_copy_constructible_v<RunCaptureReplaySession>);
    static_assert(!std::is_copy_assignable_v<RunCaptureReplaySession>);
    static_assert(std::is_nothrow_move_constructible_v<RunCaptureReplaySession>);
    static_assert(!std::is_move_assignable_v<RunCaptureReplaySession>);
    static_assert(noexcept(RunCaptureReplaySession::Create(
        std::declval<RunCaptureTracePair&&>())));
    static_assert(noexcept(std::declval<RunCaptureReplaySession&>().Next()));
    using CreateResult = decltype(RunCaptureReplaySession::Create(
        std::declval<RunCaptureTracePair&&>()));
    using NextResult = decltype(std::declval<RunCaptureReplaySession&>().Next());
    static_assert(std::is_nothrow_move_constructible_v<CreateResult>);
    static_assert(std::is_nothrow_move_constructible_v<NextResult>);
    static_assert(noexcept(
        std::declval<const RunCaptureReplaySession&>().remaining_frames()));
    static_assert(noexcept(
        std::declval<const RunCaptureReplaySession&>().complete()));
    static_assert(!std::is_aggregate_v<RunCaptureReplayFrame>);
    static_assert(!std::is_default_constructible_v<RunCaptureReplayFrame>);
    static_assert(!std::is_copy_constructible_v<RunCaptureReplayFrame>);
    static_assert(!std::is_copy_assignable_v<RunCaptureReplayFrame>);
    static_assert(std::is_nothrow_move_constructible_v<RunCaptureReplayFrame>);
    static_assert(!std::is_move_assignable_v<RunCaptureReplayFrame>);
    static_assert(noexcept(
        std::declval<const RunCaptureReplayFrame&>().input()));
    static_assert(noexcept(
        std::declval<const RunCaptureReplayFrame&>().elapsed()));
    static_assert(noexcept(
        std::declval<const RunCaptureReplayFrame&>().terminal_input()));

    struct ErrorContract
    {
        RunCaptureReplayErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array errors{
        ErrorContract{RunCaptureReplayErrorCode::InvalidInputTrace,
            "invalid-input-trace", "run capture replay input trace is invalid"},
        ErrorContract{RunCaptureReplayErrorCode::InvalidSchedulerElapsedTrace,
            "invalid-scheduler-elapsed-trace",
            "run capture replay scheduler elapsed trace is invalid"},
        ErrorContract{RunCaptureReplayErrorCode::CapacityMismatch,
            "capacity-mismatch", "run capture replay trace capacities do not match"},
        ErrorContract{RunCaptureReplayErrorCode::FirstFrameIndexMismatch,
            "first-frame-index-mismatch",
            "run capture replay first frame indices do not match"},
        ErrorContract{RunCaptureReplayErrorCode::FrameCountMismatch,
            "frame-count-mismatch",
            "run capture replay frame counts do not match terminal policy"},
        ErrorContract{RunCaptureReplayErrorCode::InvalidTerminalInput,
            "invalid-terminal-input", "run capture replay terminal input is invalid"},
        ErrorContract{RunCaptureReplayErrorCode::AllocationFailed,
            "allocation-failed", "run capture replay snapshot allocation failed"},
        ErrorContract{RunCaptureReplayErrorCode::TraceReadFailed,
            "trace-read-failed", "run capture replay trace read failed"},
        ErrorContract{RunCaptureReplayErrorCode::ReplayComplete,
            "replay-complete", "run capture replay is complete"},
        ErrorContract{RunCaptureReplayErrorCode::InvalidReplayState,
            "invalid-replay-state", "run capture replay state is invalid"},
    };
    for (const ErrorContract& contract : errors)
    {
        Check(omega::runtime::RunCaptureReplayErrorCodeName(contract.code) ==
                      contract.name &&
                  omega::runtime::RunCaptureReplayErrorMessage(contract.code) ==
                      contract.message,
            "every replay error has fixed name and category text");
    }

    Check(omega::runtime::RunCaptureReplayOperationName(
              RunCaptureReplayOperation::Create) == "create" &&
              omega::runtime::RunCaptureReplayOperationName(
                  RunCaptureReplayOperation::Next) == "next",
        "replay operations have fixed names");
    const auto unknown_operation = static_cast<RunCaptureReplayOperation>(255);
    const auto unknown_error = static_cast<RunCaptureReplayErrorCode>(255);
    Check(omega::runtime::RunCaptureReplayOperationName(unknown_operation) ==
                  "unknown" &&
              omega::runtime::RunCaptureReplayErrorCodeName(unknown_error) ==
                  "unknown" &&
              omega::runtime::RunCaptureReplayErrorMessage(unknown_error) ==
                  "run capture replay error is unknown",
        "unknown replay categories fail closed to fixed text");
}

using ReplayMetadata = omega::runtime::detail::RunCaptureReplayMetadata;
using TraceMetadata = omega::runtime::detail::RunCaptureReplayTraceMetadata;

[[nodiscard]] ReplayMetadata ValidMetadata(
    const std::span<const std::uint32_t> actions)
{
    return ReplayMetadata{
        .input_trace = TraceMetadata{
            .maximum_frames = 4U,
            .first_frame_index = 20U,
            .frame_count = 2U,
        },
        .action_schema = actions,
        .scheduler_elapsed_trace = TraceMetadata{
            .maximum_frames = 4U,
            .first_frame_index = 20U,
            .frame_count = 2U,
        },
    };
}

void CheckPlannerPriorityAndBounds()
{
    constexpr std::array<std::uint32_t, 2U> actions{10U, 20U};
    ReplayMetadata metadata = ValidMetadata(actions);

    metadata.input_trace.maximum_frames = 0U;
    metadata.scheduler_elapsed_trace.maximum_frames = 0U;
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidInputTrace,
        "invalid input leaf state has first creation priority");

    metadata = ValidMetadata(actions);
    metadata.scheduler_elapsed_trace.maximum_frames = 0U;
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidSchedulerElapsedTrace,
        "invalid elapsed leaf state precedes pair comparisons");

    metadata = ValidMetadata(actions);
    metadata.scheduler_elapsed_trace.maximum_frames = 3U;
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::CapacityMismatch,
        "capacity mismatch precedes origin and count checks");

    metadata = ValidMetadata(actions);
    metadata.scheduler_elapsed_trace.first_frame_index = 21U;
    metadata.scheduler_elapsed_trace.frame_count = 1U;
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::FirstFrameIndexMismatch,
        "origin mismatch precedes count checks");

    metadata = ValidMetadata(actions);
    metadata.scheduler_elapsed_trace.frame_count = 0U;
    metadata.terminal_input = RunCaptureTerminalInput{};
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::FrameCountMismatch,
        "count mismatch precedes malformed terminal checks");

    metadata = ValidMetadata(actions);
    metadata.input_trace.frame_count = 3U;
    metadata.terminal_input = RunCaptureTerminalInput{.frame_index = 22U};
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidTerminalInput,
        "a terminal frame requires at least one fixed reason");

    metadata.terminal_input = RunCaptureTerminalInput{
        .frame_index = 21U,
        .host_quit_requested = true,
    };
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidTerminalInput,
        "a terminal index must name the exact final input frame");

    constexpr std::array<std::uint32_t, 2U> descending_actions{20U, 10U};
    metadata = ValidMetadata(descending_actions);
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidInputTrace,
        "nonascending action schema is invalid input leaf state");

    std::array<std::uint32_t, InputBindingTable::kMaxActions + 1U> excess_actions{};
    for (std::size_t index = 0U; index < excess_actions.size(); ++index)
        excess_actions[index] = static_cast<std::uint32_t>(index);
    metadata = ValidMetadata(excess_actions);
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidInputTrace,
        "an action schema above the fixed reconstruction bound is rejected");

    metadata = ValidMetadata(actions);
    metadata.input_trace = TraceMetadata{
        .maximum_frames = 2U,
        .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
        .frame_count = 1U,
    };
    CheckReplayError(omega::runtime::detail::PlanRunCaptureReplay(metadata),
        RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidInputTrace,
        "an overflowing configured input range is rejected");

    metadata = ReplayMetadata{
        .input_trace = TraceMetadata{
            .maximum_frames = 1U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
            .frame_count = 1U,
        },
        .action_schema = actions,
        .scheduler_elapsed_trace = TraceMetadata{
            .maximum_frames = 1U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
            .frame_count = 1U,
        },
    };
    const auto maximum_origin =
        omega::runtime::detail::PlanRunCaptureReplay(metadata);
    Check(maximum_origin && maximum_origin->frame_count == 1U &&
              maximum_origin->elapsed_frame_count == 1U &&
              !maximum_origin->has_terminal_frame,
        "one frame at the largest origin is a valid boundary plan");

    metadata = ReplayMetadata{
        .input_trace = TraceMetadata{
            .maximum_frames = omega::runtime::kMaximumInputTraceFrames,
            .frame_count = omega::runtime::kMaximumInputTraceFrames,
        },
        .action_schema = actions,
        .scheduler_elapsed_trace = TraceMetadata{
            .maximum_frames =
                omega::runtime::kMaximumSchedulerElapsedTraceFrames,
            .frame_count =
                omega::runtime::kMaximumSchedulerElapsedTraceFrames,
        },
    };
    const auto maximum_capacity =
        omega::runtime::detail::PlanRunCaptureReplay(metadata);
    Check(maximum_capacity &&
              maximum_capacity->frame_count ==
                  omega::runtime::kMaximumRunCaptureSessionFrames,
        "the synthetic hard maximum is accepted without allocating in the planner");
}

void CheckNormalAndPartialReplay()
{
    constexpr std::array<std::uint32_t, 2U> actions{10U, 20U};
    constexpr std::array elapsed_values{nanoseconds{5}, nanoseconds{-7}};
    RunCaptureTracePair pair = BuildPair(actions, elapsed_values.size(), elapsed_values);
    auto created = RunCaptureReplaySession::Create(std::move(pair));
    RunCaptureReplaySession replay =
        TakeReplay(created, "a normal two-frame replay is created");
    Check(replay.remaining_frames() == 2U && !replay.complete(),
        "normal replay begins ready with the exact remaining count");

    auto first = replay.Next();
    Check(first && first->input().frame_index() == 0U &&
              first->input().actions().size() == 2U &&
              first->input().actions()[0] == 10U &&
              first->input().actions()[1] == 20U &&
              first->input().accepted_event_count() == 1U &&
              first->input().rejected_event_count() == 0U &&
              first->input().pointer_position() == kFirstPointerPosition &&
              first->input().IsHeld(10U) && first->input().WasPressed(10U) &&
              !first->input().WasReleased(10U) && !first->input().IsHeld(20U) &&
              first->elapsed() == elapsed_values[0] && !first->terminal_input(),
        "the first replay frame reconstructs exact schema, counters, states, and elapsed");
    Check(replay.remaining_frames() == 1U && !replay.complete(),
        "one successful publication advances the cursor exactly once");

    auto second = replay.Next();
    Check(second && second->input().frame_index() == 1U &&
              second->input().accepted_event_count() == 1U &&
              second->input().rejected_event_count() == 1U &&
              second->input().pointer_position() == kSecondPointerPosition &&
              !second->input().IsHeld(10U) &&
              !second->input().WasPressed(10U) &&
              second->input().WasReleased(10U) &&
              second->elapsed() == elapsed_values[1] && !second->terminal_input(),
        "the second replay frame preserves release, rejection, and signed elapsed data");
    Check(replay.remaining_frames() == 0U && replay.complete(),
        "the final successful publication enters complete state");
    CheckReplayError(replay.Next(), RunCaptureReplayOperation::Next,
        RunCaptureReplayErrorCode::ReplayComplete,
        "exhaustion is a stable complete error distinct from invalid state");

    RunCaptureTracePair partial_pair = BuildPair(actions, 5U, elapsed_values);
    auto partial_created =
        RunCaptureReplaySession::Create(std::move(partial_pair));
    RunCaptureReplaySession partial =
        TakeReplay(partial_created, "an open partial-prefix replay is accepted");
    Check(partial.remaining_frames() == elapsed_values.size() &&
              partial.Next() && partial.Next() && partial.complete(),
        "a structurally valid partial capture replays only its active prefix");
}

void CheckEmptyTerminalAndBothReasons()
{
    constexpr std::array<std::uint32_t, 1U> actions{3U};
    const std::span<const nanoseconds> no_elapsed;

    RunCaptureTracePair empty_pair = BuildPair(actions, 3U, no_elapsed);
    auto empty_created = RunCaptureReplaySession::Create(std::move(empty_pair));
    RunCaptureReplaySession empty =
        TakeReplay(empty_created, "an empty pair creates a replay");
    Check(empty.complete() && empty.remaining_frames() == 0U,
        "an empty valid pair begins complete");
    CheckReplayError(empty.Next(), RunCaptureReplayOperation::Next,
        RunCaptureReplayErrorCode::ReplayComplete,
        "empty replay reports complete rather than invalid state");

    RunCaptureTracePair terminal_pair = BuildPair(
        actions, 1U, no_elapsed,
        TerminalReasons{.host_quit_requested = true});
    auto terminal_created =
        RunCaptureReplaySession::Create(std::move(terminal_pair));
    RunCaptureReplaySession terminal =
        TakeReplay(terminal_created, "a terminal-only replay is created");
    auto terminal_frame = terminal.Next();
    Check(terminal_frame && terminal_frame->input().frame_index() == 0U &&
              !terminal_frame->input().pointer_position() &&
              !terminal_frame->elapsed() && terminal_frame->terminal_input() ==
                  RunCaptureTerminalInput{
                      .frame_index = 0U,
                      .host_quit_requested = true,
                  } &&
              terminal.complete(),
        "terminal-only replay publishes input plus terminal and no elapsed");

    constexpr std::array prior_elapsed{nanoseconds::min(), nanoseconds::max()};
    RunCaptureTracePair both_pair = BuildPair(
        actions, 3U, prior_elapsed,
        TerminalReasons{
            .host_quit_requested = true,
            .logical_quit_pressed = true,
        });
    auto both_created = RunCaptureReplaySession::Create(std::move(both_pair));
    RunCaptureReplaySession both =
        TakeReplay(both_created, "a later dual-reason terminal replay is created");
    auto first = both.Next();
    auto second = both.Next();
    auto last = both.Next();
    Check(first && first->elapsed() == nanoseconds::min() && !first->terminal_input() &&
              second && second->elapsed() == nanoseconds::max() &&
              !second->terminal_input() && last && !last->elapsed() &&
              last->input().pointer_position() == kSecondPointerPosition &&
              last->terminal_input() == RunCaptureTerminalInput{
                                          .frame_index = 2U,
                                          .host_quit_requested = true,
                                          .logical_quit_pressed = true,
                                      } &&
              both.complete(),
        "elapsed boundaries and both terminal reasons survive exact disjoint publications");
}

void CheckMaximumSchemaAndMoveLifecycle()
{
    std::array<std::uint32_t, InputBindingTable::kMaxActions> actions{};
    for (std::size_t index = 0U; index < actions.size(); ++index)
        actions[index] = static_cast<std::uint32_t>(index * 3U + 1U);
    constexpr std::array elapsed_values{nanoseconds{9}};

    RunCaptureTracePair schema_pair = BuildPair(actions, 1U, elapsed_values);
    auto schema_created = RunCaptureReplaySession::Create(std::move(schema_pair));
    RunCaptureReplaySession schema =
        TakeReplay(schema_created, "a maximum-schema replay is created");
    auto schema_frame = schema.Next();
    Check(schema_frame &&
              std::equal(actions.begin(), actions.end(),
                  schema_frame->input().actions().begin(),
                  schema_frame->input().actions().end()) &&
              schema_frame->input().pointer_position() == kFirstPointerPosition &&
              schema_frame->input().IsHeld(actions.front()) &&
              !schema_frame->input().IsHeld(actions.back()),
        "the maximum 64-action schema reconstructs in exact ascending order");

    RunCaptureTracePair move_pair = BuildPair(actions, 2U, elapsed_values);
    auto move_created = RunCaptureReplaySession::Create(std::move(move_pair));
    RunCaptureReplaySession source =
        TakeReplay(move_created, "the replay move fixture is created");
    RunCaptureReplaySession destination = std::move(source);
    Check(source.remaining_frames() == 0U && !source.complete(),
        "a moved-from replay is observably inert, not complete");
    CheckReplayError(source.Next(), RunCaptureReplayOperation::Next,
        RunCaptureReplayErrorCode::InvalidReplayState,
        "a moved-from replay returns the fixed invalid-state error");
    Check(destination.remaining_frames() == 1U && destination.Next() &&
              destination.complete(),
        "move construction transfers the pair and exact cursor state");

    RunCaptureTracePair invalid_source = BuildPair(actions, 1U, elapsed_values);
    RunCaptureTracePair retained = std::move(invalid_source);
    const std::size_t before_input_count = invalid_source.input_trace().frame_count();
    const std::size_t before_elapsed_count =
        invalid_source.scheduler_elapsed_trace().frame_count();
    auto invalid_created =
        RunCaptureReplaySession::Create(std::move(invalid_source));
    CheckReplayError(invalid_created, RunCaptureReplayOperation::Create,
        RunCaptureReplayErrorCode::InvalidInputTrace,
        "an inert pair receives the fixed invalid-input rejection");
    Check(invalid_source.input_trace().frame_count() == before_input_count &&
              invalid_source.scheduler_elapsed_trace().frame_count() ==
                  before_elapsed_count &&
              !invalid_source.terminal_input(),
        "rejected inert pair retains its observable inert state");
    auto retained_created = RunCaptureReplaySession::Create(std::move(retained));
    Check(retained_created.has_value(),
        "the independently retained valid owner remains replayable");
}

void CheckAllocationFailureAtomicity()
{
    constexpr std::array<std::uint32_t, 1U> actions{11U};
    constexpr std::array elapsed_values{nanoseconds{31}};
    RunCaptureTracePair pair = BuildPair(actions, 1U, elapsed_values);
    auto created = RunCaptureReplaySession::Create(std::move(pair));
    RunCaptureReplaySession replay =
        TakeReplay(created, "the allocation-failure replay fixture is created");

    replay_test_allocation::Arm(0U);
    const auto object_failure = replay.Next();
    replay_test_allocation::Disarm();
    CheckReplayError(object_failure, RunCaptureReplayOperation::Next,
        RunCaptureReplayErrorCode::AllocationFailed,
        "snapshot object allocation failure reports its fixed category");
    Check(replay.remaining_frames() == 1U && !replay.complete(),
        "snapshot object allocation failure leaves the cursor unchanged");

    replay_test_allocation::Arm(1U);
    const auto backing_failure = replay.Next();
    replay_test_allocation::Disarm();
    CheckReplayError(backing_failure, RunCaptureReplayOperation::Next,
        RunCaptureReplayErrorCode::AllocationFailed,
        "snapshot backing allocation failure reports its fixed category");
    Check(replay.remaining_frames() == 1U && !replay.complete(),
        "snapshot backing allocation failure leaves the cursor unchanged");

    auto retried = replay.Next();
    Check(retried && retried->input().frame_index() == 0U &&
              retried->input().pointer_position() == kFirstPointerPosition &&
              retried->elapsed() == elapsed_values[0] &&
              !retried->terminal_input() && replay.complete(),
        "the exact failed frame remains retryable after allocation recovers");
}

[[nodiscard]] bool SamePlan(const FramePlan& left, const FramePlan& right) noexcept
{
    return left.simulation_steps == right.simulation_steps &&
           left.interpolation_alpha == right.interpolation_alpha &&
           left.clamped_delta == right.clamped_delta &&
           left.dropped_time == right.dropped_time;
}

void AdvanceWorld(SimulationWorld& world, const FramePlan& plan,
    const std::string_view message)
{
    for (std::uint32_t step = 0U; step < plan.simulation_steps; ++step)
    {
        Check(world.AdvanceOneStep() == SimulationStepResult::Advanced, message);
    }
}

void CheckSchedulerWorldHarness()
{
    constexpr std::array<std::uint32_t, 1U> actions{7U};
    constexpr std::array<nanoseconds, 4U> elapsed_values{
        nanoseconds{milliseconds{5}}, nanoseconds{milliseconds{17}},
        nanoseconds{milliseconds{40}}, nanoseconds{-3}};
    constexpr std::array<std::optional<PointerPositionQ16>, 4U> expected_pointers{
        kFirstPointerPosition, kSecondPointerPosition, std::nullopt, std::nullopt};
    RunCaptureTracePair pair = BuildPair(
        actions, 8U, elapsed_values,
        TerminalReasons{.logical_quit_pressed = true});
    auto replay_created = RunCaptureReplaySession::Create(std::move(pair));
    RunCaptureReplaySession replay =
        TakeReplay(replay_created, "the scheduler/world replay harness is created");

    const FrameSchedulerConfig config{
        .simulation_step = milliseconds{10},
        .max_steps_per_frame = 4U,
        .max_frame_delta = milliseconds{50},
    };
    auto direct_scheduler = FrameScheduler::Create(config);
    auto replay_scheduler = FrameScheduler::Create(config);
    auto direct_world = SimulationWorld::Create({.fixed_step = config.simulation_step});
    auto replay_world = SimulationWorld::Create({.fixed_step = config.simulation_step});
    Check(direct_scheduler && replay_scheduler && direct_world && replay_world,
        "real scheduler and simulation harness owners construct");
    if (!direct_scheduler || !replay_scheduler || !direct_world || !replay_world)
        return;

    bool all_frames_match = true;
    std::size_t frame_offset = 0U;
    for (const nanoseconds elapsed : elapsed_values)
    {
        auto frame = replay.Next();
        if (!frame || !frame->elapsed() || frame->terminal_input())
        {
            Check(false, "the harness receives one elapsed-only replay frame");
            return;
        }

        const FramePlan direct_plan = direct_scheduler->BeginFrame(elapsed);
        const FramePlan replay_plan = replay_scheduler->BeginFrame(*frame->elapsed());
        all_frames_match = all_frames_match && SamePlan(direct_plan, replay_plan) &&
                           frame->input().pointer_position() ==
                               expected_pointers[frame_offset];
        ++frame_offset;
        AdvanceWorld(*direct_world, direct_plan,
            "the direct harness world advances each planned step");
        AdvanceWorld(*replay_world, replay_plan,
            "the replay harness world advances each planned step");
    }

    const auto direct_state = direct_world->Snapshot();
    const auto replay_state = replay_world->Snapshot();
    Check(all_frames_match && !replay.complete() &&
              direct_scheduler->Snapshot() == replay_scheduler->Snapshot() &&
              direct_state.completed_steps == replay_state.completed_steps &&
              direct_state.simulated_time == replay_state.simulated_time &&
              direct_state.alive_entities == replay_state.alive_entities,
        "elapsed replay drives independent real scheduler and world owners identically");

    const auto scheduler_before_terminal = replay_scheduler->Snapshot();
    const auto world_before_terminal = replay_world->Snapshot();
    auto terminal = replay.Next();
    const auto scheduler_after_terminal = replay_scheduler->Snapshot();
    const auto world_after_terminal = replay_world->Snapshot();
    Check(terminal && !terminal->elapsed() &&
              !terminal->input().pointer_position() &&
              terminal->terminal_input() == RunCaptureTerminalInput{
                                                  .frame_index = 4U,
                                                  .logical_quit_pressed = true,
                                              } &&
              replay.complete() &&
              scheduler_before_terminal == scheduler_after_terminal &&
              world_before_terminal.completed_steps ==
                  world_after_terminal.completed_steps &&
              world_before_terminal.simulated_time ==
                  world_after_terminal.simulated_time &&
              world_before_terminal.alive_entities ==
                  world_after_terminal.alive_entities,
        "logical-only terminal replay bypasses and preserves real scheduler/world state");
}
} // namespace

int main()
{
    CheckContract();
    CheckPlannerPriorityAndBounds();
    CheckNormalAndPartialReplay();
    CheckEmptyTerminalAndBothReasons();
    CheckMaximumSchemaAndMoveLifecycle();
    CheckAllocationFailureAtomicity();
    CheckSchedulerWorldHarness();

    if (failures == 0)
        std::cout << "omega_run_capture_replay_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
