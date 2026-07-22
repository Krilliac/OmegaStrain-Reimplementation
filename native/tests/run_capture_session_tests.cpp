#include "omega/runtime/run_capture_session.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
using std::chrono::nanoseconds;
using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputSnapshot;
using omega::runtime::InputTraceErrorCode;
using omega::runtime::InputTracker;
using omega::runtime::PointerPositionQ16;
using omega::runtime::RunCaptureSession;
using omega::runtime::RunCaptureSessionConfig;
using omega::runtime::RunCaptureSessionError;
using omega::runtime::RunCaptureSessionErrorCode;
using omega::runtime::RunCaptureSessionOperation;
using omega::runtime::RunCaptureTerminalInput;
using omega::runtime::RunCaptureTracePair;
using omega::runtime::SchedulerElapsedFrameState;
using omega::runtime::SchedulerElapsedTraceErrorCode;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Value>
bool CheckSessionError(const std::expected<Value, RunCaptureSessionError>& result,
    const RunCaptureSessionOperation operation,
    const RunCaptureSessionErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().operation == operation &&
                         result.error().code == code &&
                         result.error().message ==
                             omega::runtime::RunCaptureSessionErrorMessage(code) &&
                         !result.error().input_trace_error_code &&
                         !result.error().scheduler_elapsed_trace_error_code;
    Check(matches, message);
    return matches;
}

template <typename Value>
bool CheckInputError(const std::expected<Value, RunCaptureSessionError>& result,
    const RunCaptureSessionOperation operation, const InputTraceErrorCode leaf_code,
    const std::string_view message)
{
    const bool matches =
        !result && result.error().operation == operation &&
        result.error().code == RunCaptureSessionErrorCode::InputTraceFailure &&
        result.error().message == omega::runtime::RunCaptureSessionErrorMessage(
                                      RunCaptureSessionErrorCode::InputTraceFailure) &&
        result.error().input_trace_error_code == leaf_code &&
        !result.error().scheduler_elapsed_trace_error_code;
    Check(matches, message);
    return matches;
}

[[nodiscard]] InputTracker MakeTracker(
    const std::span<const std::uint32_t> actions, const std::size_t event_budget = 16U)
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
        std::abort();
    auto tracker = InputTracker::Create(std::move(*table), event_budget);
    if (!tracker)
        std::abort();
    return std::move(*tracker);
}

bool Push(InputTracker& tracker, const std::uint16_t code, const bool pressed)
{
    return tracker
        .PushEvent(InputEvent{
            .device = InputDevice::Keyboard,
            .code = code,
            .pressed = pressed,
        })
        .has_value();
}

[[nodiscard]] RunCaptureSession TakeSession(
    std::expected<RunCaptureSession, RunCaptureSessionError>& created,
    const std::string_view message)
{
    if (!created)
    {
        Check(false, message);
        std::abort();
    }
    return std::move(*created);
}

[[nodiscard]] RunCaptureTracePair TakePair(
    std::expected<RunCaptureTracePair, RunCaptureSessionError>& finished,
    const std::string_view message)
{
    if (!finished)
    {
        Check(false, message);
        std::abort();
    }
    return std::move(*finished);
}

void CheckContract()
{
    static_assert(std::is_trivially_copyable_v<RunCaptureTerminalInput>);
    static_assert(std::is_standard_layout_v<RunCaptureTerminalInput>);
    static_assert(!std::is_copy_constructible_v<RunCaptureSession>);
    static_assert(!std::is_copy_assignable_v<RunCaptureSession>);
    static_assert(std::is_nothrow_move_constructible_v<RunCaptureSession>);
    static_assert(!std::is_move_assignable_v<RunCaptureSession>);
    static_assert(!std::is_copy_constructible_v<RunCaptureTracePair>);
    static_assert(!std::is_copy_assignable_v<RunCaptureTracePair>);
    static_assert(std::is_nothrow_move_constructible_v<RunCaptureTracePair>);
    static_assert(!std::is_move_assignable_v<RunCaptureTracePair>);
    static_assert(noexcept(std::declval<RunCaptureSession&>().AppendInput(
        std::declval<const InputSnapshot&>())));
    static_assert(noexcept(std::declval<RunCaptureSession&>().AppendElapsed(
        std::declval<nanoseconds>())));
    static_assert(noexcept(
        std::declval<RunCaptureSession&>().MarkTerminal(false, false)));
    static_assert(noexcept(std::declval<RunCaptureSession&&>().Finish()));
    static_assert(noexcept(
        std::declval<const RunCaptureTracePair&>().input_trace()));
    static_assert(noexcept(
        std::declval<const RunCaptureTracePair&>().scheduler_elapsed_trace()));
    static_assert(noexcept(
        std::declval<const RunCaptureTracePair&>().terminal_input()));

    Check(omega::runtime::kMaximumRunCaptureSessionFrames == 65'536U,
        "the run-capture session frame maximum is fixed");
    Check(omega::runtime::kMaximumRunCaptureSessionElementPayloadBytes ==
              3'145'984U,
        "the hard-maximum combined element payload is exact");
    Check(RunCaptureSessionConfig{}.maximum_frames == 0U &&
              RunCaptureSessionConfig{}.first_frame_index == 0U,
        "the default session configuration is deliberately invalid");
    Check(RunCaptureTerminalInput{} == RunCaptureTerminalInput{},
        "the owned terminal value defaults to zero and false");

    struct OperationContract
    {
        RunCaptureSessionOperation operation;
        std::string_view name;
    };
    constexpr std::array operations{
        OperationContract{RunCaptureSessionOperation::CreateInputTrace,
            "create-input-trace"},
        OperationContract{RunCaptureSessionOperation::CreateSchedulerElapsedTrace,
            "create-scheduler-elapsed-trace"},
        OperationContract{RunCaptureSessionOperation::AppendInput, "append-input"},
        OperationContract{RunCaptureSessionOperation::AppendElapsed, "append-elapsed"},
        OperationContract{RunCaptureSessionOperation::MarkTerminal, "mark-terminal"},
        OperationContract{RunCaptureSessionOperation::FinishInputTrace,
            "finish-input-trace"},
        OperationContract{RunCaptureSessionOperation::FinishSchedulerElapsedTrace,
            "finish-scheduler-elapsed-trace"},
    };
    for (const OperationContract& contract : operations)
    {
        Check(omega::runtime::RunCaptureSessionOperationName(contract.operation) ==
                  contract.name,
            "every run-capture operation has its fixed name");
    }

    struct ErrorContract
    {
        RunCaptureSessionErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array errors{
        ErrorContract{RunCaptureSessionErrorCode::InvalidSessionState,
            "invalid-session-state", "run capture session state is invalid"},
        ErrorContract{RunCaptureSessionErrorCode::InvalidTerminalReason,
            "invalid-terminal-reason", "run capture terminal reason is invalid"},
        ErrorContract{RunCaptureSessionErrorCode::InputTraceFailure,
            "input-trace-failure", "run capture input trace operation failed"},
        ErrorContract{RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure,
            "scheduler-elapsed-trace-failure",
            "run capture scheduler elapsed trace operation failed"},
    };
    for (const ErrorContract& contract : errors)
    {
        Check(omega::runtime::RunCaptureSessionErrorCodeName(contract.code) ==
                  contract.name &&
                  omega::runtime::RunCaptureSessionErrorMessage(contract.code) ==
                      contract.message,
            "every run-capture error has fixed name and category text");
    }

    constexpr auto unknown_operation =
        static_cast<RunCaptureSessionOperation>(255);
    constexpr auto unknown_error = static_cast<RunCaptureSessionErrorCode>(255);
    Check(omega::runtime::RunCaptureSessionOperationName(unknown_operation) ==
                  "unknown" &&
              omega::runtime::RunCaptureSessionErrorCodeName(unknown_error) ==
                  "unknown" &&
              omega::runtime::RunCaptureSessionErrorMessage(unknown_error) ==
                  "run capture session error is unknown",
        "unknown operation and error values fail closed to fixed text");

    const RunCaptureSessionError scheduler_leaf{
        .operation = RunCaptureSessionOperation::AppendElapsed,
        .code = RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure,
        .message = omega::runtime::RunCaptureSessionErrorMessage(
            RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure),
        .scheduler_elapsed_trace_error_code =
            SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
    };
    Check(!scheduler_leaf.input_trace_error_code &&
              scheduler_leaf.scheduler_elapsed_trace_error_code ==
                  SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
        "a scheduler leaf category can be retained without input leaf data");
}

void CheckCreationValidation()
{
    constexpr std::array<std::uint32_t, 1U> actions{10U};
    const std::span<const std::uint32_t> empty_schema;

    CheckInputError(RunCaptureSession::Create(
                        RunCaptureSessionConfig{.maximum_frames = 0U}, actions),
        RunCaptureSessionOperation::CreateInputTrace,
        InputTraceErrorCode::InvalidConfiguration,
        "zero capacity fails at input-trace creation");
    CheckInputError(RunCaptureSession::Create(
                        RunCaptureSessionConfig{
                            .maximum_frames =
                                omega::runtime::kMaximumRunCaptureSessionFrames + 1U,
                        },
                        actions),
        RunCaptureSessionOperation::CreateInputTrace,
        InputTraceErrorCode::InvalidConfiguration,
        "capacity above the coordinator maximum is rejected");
    CheckInputError(RunCaptureSession::Create(
                        RunCaptureSessionConfig{
                            .maximum_frames = 2U,
                            .first_frame_index =
                                std::numeric_limits<std::uint64_t>::max(),
                        },
                        actions),
        RunCaptureSessionOperation::CreateInputTrace,
        InputTraceErrorCode::InvalidConfiguration,
        "a configured contiguous range cannot overflow uint64");
    CheckInputError(RunCaptureSession::Create(RunCaptureSessionConfig{}, empty_schema),
        RunCaptureSessionOperation::CreateInputTrace,
        InputTraceErrorCode::InvalidConfiguration,
        "configuration validation precedes action-schema validation");
    CheckInputError(RunCaptureSession::Create(
                        RunCaptureSessionConfig{.maximum_frames = 1U}, empty_schema),
        RunCaptureSessionOperation::CreateInputTrace,
        InputTraceErrorCode::InvalidActionSchema,
        "an empty schema retains the exact input leaf category");

    constexpr std::array<std::uint32_t, 2U> unsorted{20U, 10U};
    CheckInputError(RunCaptureSession::Create(
                        RunCaptureSessionConfig{.maximum_frames = 1U}, unsorted),
        RunCaptureSessionOperation::CreateInputTrace,
        InputTraceErrorCode::InvalidActionSchema,
        "an unsorted schema is rejected before elapsed backing allocation");

    auto terminal_range = RunCaptureSession::Create(
        RunCaptureSessionConfig{
            .maximum_frames = 2U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max() - 1U,
        },
        actions);
    Check(terminal_range.has_value(),
        "a leaf range ending exactly at uint64 max remains valid");
    if (terminal_range)
    {
        auto finished = std::move(*terminal_range).Finish();
        Check(finished && finished->input_trace().first_frame_index() ==
                               std::numeric_limits<std::uint64_t>::max() - 1U &&
                  finished->input_trace().maximum_frames() == 2U &&
                  finished->scheduler_elapsed_trace().maximum_frames() == 2U,
            "the terminal-range metadata is identical in both empty traces");
    }

    auto exact_maximum = RunCaptureSession::Create(
        RunCaptureSessionConfig{
            .maximum_frames = omega::runtime::kMaximumRunCaptureSessionFrames,
        },
        actions);
    Check(exact_maximum.has_value(),
        "the exact combined backing maximum preallocates successfully");
    if (exact_maximum)
    {
        auto finished = std::move(*exact_maximum).Finish();
        Check(finished && finished->input_trace().frame_count() == 0U &&
                  finished->scheduler_elapsed_trace().frame_count() == 0U,
            "an exact-maximum empty session finishes without another allocation");
    }
}

void CheckEmptyFinish()
{
    constexpr std::array<std::uint32_t, 1U> actions{10U};
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 1U, .first_frame_index = 12U},
        actions);
    RunCaptureSession session = TakeSession(created, "the empty session is created");
    auto finished = std::move(session).Finish();
    Check(finished && finished->input_trace().first_frame_index() == 12U &&
              finished->input_trace().maximum_frames() == 1U &&
              finished->input_trace().frame_count() == 0U &&
              finished->scheduler_elapsed_trace().first_frame_index() == 12U &&
              finished->scheduler_elapsed_trace().frame_count() == 0U &&
              !finished->terminal_input(),
        "an open capacity-one session publishes two empty aligned traces");
    auto finished_again = std::move(session).Finish();
    CheckSessionError(finished_again, RunCaptureSessionOperation::FinishInputTrace,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "a finished session rejects a second finish");
}

[[nodiscard]] RunCaptureTracePair BuildNormalPair()
{
    constexpr std::array<std::uint32_t, 2U> actions{10U, 20U};
    constexpr PointerPositionQ16 pointer{.x = 1'234U, .y = 56'789U};
    InputTracker tracker = MakeTracker(actions);
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 3U}, actions);
    RunCaptureSession session = TakeSession(created, "the normal session is created");

    if (!tracker.SetPointerPosition(pointer) || !Push(tracker, 0U, true))
        std::abort();
    const InputSnapshot first = tracker.EndFrame();
    if (!session.AppendInput(first) || !session.AppendElapsed(nanoseconds{-7}))
        std::abort();

    if (!Push(tracker, 0U, false) || !Push(tracker, 1U, true))
        std::abort();
    const InputSnapshot second = tracker.EndFrame();
    if (!session.AppendInput(second) ||
        !session.AppendElapsed(nanoseconds::max()))
    {
        std::abort();
    }

    tracker.ClearPointerPosition();
    const InputSnapshot third = tracker.EndFrame();
    if (!session.AppendInput(third) || !session.AppendElapsed(nanoseconds{0}))
        std::abort();

    auto finished = std::move(session).Finish();
    return TakePair(finished, "the normal session finishes");
}

void CheckNormalPair()
{
    RunCaptureTracePair pair = BuildNormalPair();
    const auto& input = pair.input_trace();
    const auto& elapsed = pair.scheduler_elapsed_trace();
    Check(input.first_frame_index() == 0U && elapsed.first_frame_index() == 0U &&
               input.maximum_frames() == 3U && elapsed.maximum_frames() == 3U &&
               input.frame_count() == 3U && elapsed.frame_count() == 3U &&
               !pair.terminal_input(),
        "a normal pair exposes equal aligned trace counts and no terminal");
    constexpr PointerPositionQ16 pointer{.x = 1'234U, .y = 56'789U};
    Check(input.PointerAt(0U) == pointer && input.ActionAt(0U, 10U) &&
              input.ActionAt(0U, 10U)->held &&
              input.ActionAt(0U, 10U)->pressed &&
              !input.ActionAt(0U, 10U)->released &&
              elapsed.FrameAt(0U) &&
              elapsed.FrameAt(0U)->frame_index == 0U &&
              elapsed.FrameAt(0U)->elapsed == nanoseconds{-7},
        "the first logical snapshot, pointer, and exact signed elapsed value remain paired");
    Check(input.PointerAt(1U) == pointer && input.ActionAt(1U, 10U) &&
              !input.ActionAt(1U, 10U)->held &&
              input.ActionAt(1U, 10U)->released && input.ActionAt(1U, 20U) &&
              input.ActionAt(1U, 20U)->pressed && elapsed.FrameAt(1U) &&
              elapsed.FrameAt(1U)->frame_index == 1U &&
              elapsed.FrameAt(1U)->elapsed == nanoseconds::max(),
        "the second snapshot preserves the pointer and representation-limit elapsed");
    Check(!input.PointerAt(2U) && input.ActionAt(2U, 20U) &&
              input.ActionAt(2U, 20U)->held && elapsed.FrameAt(2U) &&
              elapsed.FrameAt(2U)->frame_index == 2U &&
              elapsed.FrameAt(2U)->elapsed == nanoseconds{0},
        "the third paired snapshot preserves explicit pointer clearing");
}

[[nodiscard]] RunCaptureTracePair BuildTerminalPair(const std::size_t prior_pairs,
    const bool host_quit_requested, const bool logical_quit_pressed)
{
    constexpr std::array<std::uint32_t, 1U> actions{10U};
    InputTracker tracker = MakeTracker(actions);
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = prior_pairs + 1U}, actions);
    RunCaptureSession session = TakeSession(created, "the terminal session is created");
    for (std::size_t index = 0U; index < prior_pairs; ++index)
    {
        const InputSnapshot snapshot = tracker.EndFrame();
        if (!session.AppendInput(snapshot) ||
            !session.AppendElapsed(nanoseconds{static_cast<std::int64_t>(index)}))
        {
            std::abort();
        }
    }

    const InputSnapshot terminal = tracker.EndFrame();
    if (!session.AppendInput(terminal) ||
        !session.MarkTerminal(host_quit_requested, logical_quit_pressed))
    {
        std::abort();
    }
    auto finished = std::move(session).Finish();
    return TakePair(finished, "the terminal session finishes");
}

void CheckTerminalPairs()
{
    RunCaptureTracePair host = BuildTerminalPair(0U, true, false);
    Check(host.input_trace().frame_count() == 1U &&
              host.scheduler_elapsed_trace().frame_count() == 0U &&
              host.terminal_input() == RunCaptureTerminalInput{
                                           .frame_index = 0U,
                                           .host_quit_requested = true,
                                           .logical_quit_pressed = false,
                                       },
        "a first-frame host quit owns one unpaired terminal input");

    RunCaptureTracePair logical = BuildTerminalPair(1U, false, true);
    Check(logical.input_trace().frame_count() == 2U &&
              logical.scheduler_elapsed_trace().frame_count() == 1U &&
              logical.terminal_input() == RunCaptureTerminalInput{
                                              .frame_index = 1U,
                                              .host_quit_requested = false,
                                              .logical_quit_pressed = true,
                                          },
        "a later logical quit is the final input after one paired frame");

    RunCaptureTracePair both = BuildTerminalPair(2U, true, true);
    Check(both.input_trace().frame_count() == 3U &&
              both.scheduler_elapsed_trace().frame_count() == 2U &&
              both.terminal_input() == RunCaptureTerminalInput{
                                           .frame_index = 2U,
                                           .host_quit_requested = true,
                                           .logical_quit_pressed = true,
                                       },
        "simultaneous host and logical quit reasons are both retained");
}

void CheckPhasePriorityAndAtomicity()
{
    constexpr std::array<std::uint32_t, 1U> actions{10U};
    InputTracker tracker = MakeTracker(actions);
    const InputSnapshot first = tracker.EndFrame();
    const InputSnapshot second = tracker.EndFrame();

    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 2U}, actions);
    RunCaptureSession session = TakeSession(created, "the phase fixture is created");

    CheckSessionError(session.AppendElapsed(nanoseconds{1}),
        RunCaptureSessionOperation::AppendElapsed,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "elapsed before input fails without changing the open phase");
    CheckSessionError(session.MarkTerminal(false, false),
        RunCaptureSessionOperation::MarkTerminal,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "phase validation precedes terminal-reason validation");
    Check(session.AppendInput(first).has_value(),
        "the first input remains appendable after open-phase failures");
    CheckSessionError(session.AppendInput(second),
        RunCaptureSessionOperation::AppendInput,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "a second input cannot replace the pending frame");
    CheckSessionError(session.MarkTerminal(false, false),
        RunCaptureSessionOperation::MarkTerminal,
        RunCaptureSessionErrorCode::InvalidTerminalReason,
        "an empty terminal reason leaves the pending input unchanged");
    auto pending_finish = std::move(session).Finish();
    CheckSessionError(pending_finish, RunCaptureSessionOperation::FinishInputTrace,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "finish rejects an unpaired input without consuming the session");
    Check(session.AppendElapsed(nanoseconds::min()).has_value(),
        "elapsed can pair the same pending input after all failures");
    CheckSessionError(session.MarkTerminal(true, false),
        RunCaptureSessionOperation::MarkTerminal,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "terminal marking requires a pending input frame");
    Check(session.AppendInput(second).has_value() &&
              session.MarkTerminal(true, true).has_value(),
        "a later input can terminate after the paired frame");
    CheckSessionError(session.AppendElapsed(nanoseconds{0}),
        RunCaptureSessionOperation::AppendElapsed,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "terminal state rejects elapsed capture");
    CheckSessionError(session.AppendInput(second),
        RunCaptureSessionOperation::AppendInput,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "terminal state rejects further input capture");
    CheckSessionError(session.MarkTerminal(true, false),
        RunCaptureSessionOperation::MarkTerminal,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "terminal state rejects a second terminal mark");

    auto finished = std::move(session).Finish();
    std::optional<SchedulerElapsedFrameState> finished_elapsed;
    std::optional<RunCaptureTerminalInput> finished_terminal;
    if (finished)
    {
        finished_elapsed = finished->scheduler_elapsed_trace().FrameAt(0U);
        finished_terminal = finished->terminal_input();
    }
    Check(finished && finished->input_trace().frame_count() == 2U &&
              finished->scheduler_elapsed_trace().frame_count() == 1U &&
              finished_elapsed && finished_elapsed->elapsed == nanoseconds::min() &&
              finished_terminal && finished_terminal->frame_index == 1U,
        "successful records survive every invalid transition unchanged");
}

void CheckLeafFailuresAndCapacity()
{
    constexpr std::array<std::uint32_t, 1U> actions{10U};
    constexpr std::array<std::uint32_t, 1U> other_actions{20U};

    InputTracker frame_tracker = MakeTracker(actions);
    const InputSnapshot frame_zero = frame_tracker.EndFrame();
    const InputSnapshot frame_one = frame_tracker.EndFrame();
    auto discontinuous_created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 1U, .first_frame_index = 1U},
        actions);
    RunCaptureSession discontinuous =
        TakeSession(discontinuous_created, "the discontinuity fixture is created");
    CheckInputError(discontinuous.AppendInput(frame_zero),
        RunCaptureSessionOperation::AppendInput,
        InputTraceErrorCode::FrameDiscontinuity,
        "input frame discontinuity retains its exact leaf category");
    Check(discontinuous.AppendInput(frame_one).has_value() &&
              discontinuous.AppendElapsed(nanoseconds{9}).has_value(),
        "the expected frame remains appendable after discontinuity");

    InputTracker wrong_tracker = MakeTracker(other_actions);
    InputTracker correct_tracker = MakeTracker(actions);
    const InputSnapshot wrong_schema = wrong_tracker.EndFrame();
    const InputSnapshot correct_schema = correct_tracker.EndFrame();
    auto schema_created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 1U}, actions);
    RunCaptureSession schema =
        TakeSession(schema_created, "the schema fixture is created");
    CheckInputError(schema.AppendInput(wrong_schema),
        RunCaptureSessionOperation::AppendInput,
        InputTraceErrorCode::ActionSchemaMismatch,
        "snapshot schema mismatch retains its exact leaf category");
    Check(schema.AppendInput(correct_schema).has_value() &&
              schema.AppendElapsed(nanoseconds{-2}).has_value(),
        "a correct snapshot remains appendable after schema failure");

    const InputSnapshot capacity_second = correct_tracker.EndFrame();
    CheckInputError(schema.AppendInput(capacity_second),
        RunCaptureSessionOperation::AppendInput,
        InputTraceErrorCode::CapacityExceeded,
        "coordinator capacity exhaustion retains the input leaf category");
    auto schema_finished = std::move(schema).Finish();
    Check(schema_finished && schema_finished->input_trace().frame_count() == 1U &&
              schema_finished->scheduler_elapsed_trace().frame_count() == 1U,
        "capacity failure preserves the complete prior pair");

    auto discontinuous_finished = std::move(discontinuous).Finish();
    std::optional<SchedulerElapsedFrameState> discontinuous_elapsed;
    if (discontinuous_finished)
    {
        discontinuous_elapsed =
            discontinuous_finished->scheduler_elapsed_trace().FrameAt(0U);
    }
    Check(discontinuous_finished &&
              discontinuous_finished->input_trace().first_frame_index() == 1U &&
              discontinuous_elapsed && discontinuous_elapsed->elapsed == nanoseconds{9},
        "discontinuity retry publishes the exact accepted pair");
}

void CheckMoveLifecycleAndOwnedTerminal()
{
    constexpr std::array<std::uint32_t, 1U> actions{10U};
    InputTracker tracker = MakeTracker(actions);
    const InputSnapshot snapshot = tracker.EndFrame();
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 1U}, actions);
    RunCaptureSession source = TakeSession(created, "the move fixture is created");
    RunCaptureSession destination = std::move(source);
    CheckSessionError(source.AppendInput(snapshot),
        RunCaptureSessionOperation::AppendInput,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "a moved-from session rejects input");
    auto source_finish = std::move(source).Finish();
    CheckSessionError(source_finish, RunCaptureSessionOperation::FinishInputTrace,
        RunCaptureSessionErrorCode::InvalidSessionState,
        "a moved-from session rejects finish");
    Check(destination.AppendInput(snapshot).has_value() &&
              destination.MarkTerminal(false, true).has_value(),
        "the moved-to session retains complete capture ownership");

    std::optional<RunCaptureTerminalInput> owned_terminal;
    {
        auto destination_finish = std::move(destination).Finish();
        RunCaptureTracePair pair =
            TakePair(destination_finish, "the moved-to session finishes");
        owned_terminal = pair.terminal_input();
        RunCaptureTracePair moved_pair = std::move(pair);
        Check(pair.input_trace().frame_count() == 0U &&
                  pair.scheduler_elapsed_trace().frame_count() == 0U &&
                  !pair.terminal_input(),
            "pair move construction leaves both leaf traces and terminal inert");
        Check(moved_pair.input_trace().frame_count() == 1U &&
                  moved_pair.scheduler_elapsed_trace().frame_count() == 0U &&
                  moved_pair.terminal_input() == owned_terminal,
            "pair move transfers both trace owners and terminal metadata");
    }
    Check(owned_terminal == RunCaptureTerminalInput{
                                .frame_index = 0U,
                                .host_quit_requested = false,
                                .logical_quit_pressed = true,
                            },
        "owned terminal metadata survives destruction of the final pair");
}

void CheckTwoRunDeterminism()
{
    RunCaptureTracePair first = BuildNormalPair();
    RunCaptureTracePair second = BuildNormalPair();
    bool identical =
        first.input_trace().first_frame_index() ==
            second.input_trace().first_frame_index() &&
        first.input_trace().maximum_frames() ==
            second.input_trace().maximum_frames() &&
        first.input_trace().actions().size() ==
            second.input_trace().actions().size() &&
        first.input_trace().frame_count() == second.input_trace().frame_count() &&
        first.scheduler_elapsed_trace().frame_count() ==
            second.scheduler_elapsed_trace().frame_count() &&
        first.terminal_input() == second.terminal_input();
    for (std::size_t index = 0U;
         identical && index < first.input_trace().frame_count(); ++index)
    {
        identical = first.input_trace().FrameAt(index) ==
                        second.input_trace().FrameAt(index) &&
                    first.input_trace().PointerAt(index) ==
                        second.input_trace().PointerAt(index) &&
                    first.scheduler_elapsed_trace().FrameAt(index) ==
                        second.scheduler_elapsed_trace().FrameAt(index);
        for (const std::uint32_t action : first.input_trace().actions())
        {
            identical = identical &&
                        first.input_trace().ActionAt(index, action) ==
                            second.input_trace().ActionAt(index, action);
        }
    }
    Check(identical,
        "two independent coordinator runs publish identical trace queries");
}
} // namespace

int main()
{
    CheckContract();
    CheckCreationValidation();
    CheckEmptyFinish();
    CheckNormalPair();
    CheckTerminalPairs();
    CheckPhasePriorityAndAtomicity();
    CheckLeafFailuresAndCapacity();
    CheckMoveLifecycleAndOwnedTerminal();
    CheckTwoRunDeterminism();

    if (failures == 0)
        std::cout << "omega_run_capture_session_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
