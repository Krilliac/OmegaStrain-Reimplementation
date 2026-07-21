#include "run_capture.h"

#include "omega/runtime/input_tracker.h"

#include <array>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace omega::app::detail
{
struct RunCaptureOutcomeTestAccess final
{
    [[nodiscard]] static RunCaptureOutcome Create(
        const std::size_t requested_frame_limit, const RunResult result,
        const RunCaptureCompletion completion,
        const runtime::FrameSchedulerState scheduler_state_before,
        const runtime::FrameSchedulerState scheduler_state_after,
        std::optional<std::string> failure,
        std::optional<runtime::RunCaptureTracePair> trace_pair)
    {
        return RunCaptureOutcome(requested_frame_limit, result, completion,
            scheduler_state_before, scheduler_state_after, std::move(failure),
            std::move(trace_pair));
    }
};
} // namespace omega::app::detail

namespace
{
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using omega::app::RunCaptureCompletion;
using omega::app::RunCaptureOutcome;
using omega::app::RunResult;
using omega::app::detail::CheckedNextRenderedFrameCount;
using omega::app::detail::FiniteRunCapturePlanError;
using omega::app::detail::RunCaptureOutcomeTestAccess;
using omega::runtime::FrameSchedulerConfig;
using omega::runtime::FrameSchedulerState;
using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputTraceErrorCode;
using omega::runtime::InputTracker;
using omega::runtime::RunCaptureSession;
using omega::runtime::RunCaptureSessionConfig;
using omega::runtime::RunCaptureSessionError;
using omega::runtime::RunCaptureSessionErrorCode;
using omega::runtime::RunCaptureSessionOperation;
using omega::runtime::RunCaptureTerminalInput;
using omega::runtime::RunCaptureTracePair;
using omega::runtime::SchedulerElapsedTraceErrorCode;

template <typename T>
concept TracePairExtractable = requires(T&& outcome) {
    {
        std::forward<T>(outcome).TakeTracePair()
    } -> std::same_as<std::optional<RunCaptureTracePair>>;
};

constexpr std::uint32_t kQuitAction = 10U;
constexpr std::uint16_t kQuitCode = 7U;
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] InputTracker MakeTracker()
{
    constexpr std::array bindings{
        InputBinding{
            .device = InputDevice::Keyboard,
            .code = kQuitCode,
            .action = kQuitAction,
        },
    };
    auto table = InputBindingTable::FromBindings(bindings);
    if (!table)
        std::abort();
    auto tracker = InputTracker::Create(std::move(*table), 8U);
    if (!tracker)
        std::abort();
    return std::move(*tracker);
}

[[nodiscard]] RunCaptureSession TakeSession(
    std::expected<RunCaptureSession, RunCaptureSessionError>& created)
{
    if (!created)
        std::abort();
    return std::move(*created);
}

[[nodiscard]] RunCaptureTracePair TakePair(
    std::expected<RunCaptureTracePair, RunCaptureSessionError>& finished)
{
    if (!finished)
        std::abort();
    return std::move(*finished);
}

[[nodiscard]] RunCaptureTracePair BuildEmptyPair(
    const std::uint64_t first_frame_index = 0U)
{
    constexpr std::array actions{kQuitAction};
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{
            .maximum_frames = 1U,
            .first_frame_index = first_frame_index,
        },
        actions);
    RunCaptureSession session = TakeSession(created);
    auto finished = std::move(session).Finish();
    return TakePair(finished);
}

[[nodiscard]] RunCaptureTracePair BuildNonTerminalPair(
    const std::size_t maximum_frames, const std::size_t captured_frames)
{
    constexpr std::array elapsed_samples{milliseconds{3}, milliseconds{17}};
    if (maximum_frames == 0U || captured_frames > maximum_frames ||
        captured_frames > elapsed_samples.size())
    {
        std::abort();
    }

    constexpr std::array actions{kQuitAction};
    InputTracker tracker = MakeTracker();
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = maximum_frames}, actions);
    RunCaptureSession session = TakeSession(created);
    for (std::size_t frame = 0U; frame < captured_frames; ++frame)
    {
        const auto input = tracker.EndFrame();
        if (!session.AppendInput(input) ||
            !session.AppendElapsed(elapsed_samples[frame]))
        {
            std::abort();
        }
    }
    auto finished = std::move(session).Finish();
    return TakePair(finished);
}

[[nodiscard]] RunCaptureTracePair BuildNormalPair()
{
    return BuildNonTerminalPair(2U, 2U);
}

[[nodiscard]] RunCaptureTracePair BuildTerminalPair(
    const bool host_quit, const bool logical_quit)
{
    constexpr std::array actions{kQuitAction};
    InputTracker tracker = MakeTracker();
    if (logical_quit)
    {
        const auto pushed = tracker.PushEvent(InputEvent{
            .device = InputDevice::Keyboard,
            .code = kQuitCode,
            .pressed = true,
        });
        if (!pushed)
            std::abort();
    }

    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 1U}, actions);
    RunCaptureSession session = TakeSession(created);
    const auto terminal = tracker.EndFrame();
    if (!session.AppendInput(terminal) ||
        !session.MarkTerminal(host_quit, logical_quit))
    {
        std::abort();
    }
    auto finished = std::move(session).Finish();
    return TakePair(finished);
}

[[nodiscard]] RunCaptureTracePair BuildElapsedThenTerminalPair()
{
    constexpr std::array actions{kQuitAction};
    InputTracker tracker = MakeTracker();
    auto created = RunCaptureSession::Create(
        RunCaptureSessionConfig{.maximum_frames = 2U}, actions);
    RunCaptureSession session = TakeSession(created);

    const auto first = tracker.EndFrame();
    if (!session.AppendInput(first) ||
        !session.AppendElapsed(milliseconds{13}))
    {
        std::abort();
    }
    if (!tracker.PushEvent(InputEvent{
            .device = InputDevice::Keyboard,
            .code = kQuitCode,
            .pressed = true,
        }))
    {
        std::abort();
    }
    const auto terminal = tracker.EndFrame();
    if (!session.AppendInput(terminal) ||
        !session.MarkTerminal(false, true))
    {
        std::abort();
    }
    auto finished = std::move(session).Finish();
    return TakePair(finished);
}

[[nodiscard]] constexpr FrameSchedulerConfig SchedulerConfig()
{
    return FrameSchedulerConfig{
        .simulation_step = milliseconds{10},
        .max_steps_per_frame = 4U,
        .max_frame_delta = milliseconds{100},
    };
}

[[nodiscard]] constexpr FrameSchedulerState SchedulerBefore()
{
    return FrameSchedulerState{
        .config = SchedulerConfig(),
        .accumulated_remainder = milliseconds{3},
        .total_planned_steps = 5U,
        .total_dropped_time = milliseconds{20},
    };
}

[[nodiscard]] constexpr FrameSchedulerState SchedulerAfter()
{
    return FrameSchedulerState{
        .config = SchedulerConfig(),
        .accumulated_remainder = milliseconds{7},
        .total_planned_steps = 7U,
        .total_dropped_time = milliseconds{30},
    };
}

[[nodiscard]] RunCaptureOutcome MakeOutcome(const std::size_t requested_frames,
    const RunResult result, const RunCaptureCompletion completion,
    std::optional<std::string> failure,
    std::optional<RunCaptureTracePair> trace_pair)
{
    return RunCaptureOutcomeTestAccess::Create(requested_frames, result,
        completion, SchedulerBefore(), SchedulerAfter(), std::move(failure),
        std::move(trace_pair));
}

[[nodiscard]] bool IsFullyInert(const RunCaptureOutcome& outcome)
{
    return outcome.requested_frame_limit() == 0U &&
           outcome.result() == RunResult{} &&
           outcome.completion() == RunCaptureCompletion::Inert &&
           outcome.scheduler_state_before() == FrameSchedulerState{} &&
           outcome.scheduler_state_after() == FrameSchedulerState{} &&
           !outcome.failure() && !outcome.has_traces() &&
           outcome.trace_pair() == nullptr && !outcome.terminal_input();
}

void CheckContract()
{
    static_assert(std::is_nothrow_move_constructible_v<RunCaptureOutcome>);
    static_assert(!std::is_move_assignable_v<RunCaptureOutcome>);
    static_assert(!std::is_copy_constructible_v<RunCaptureOutcome>);
    static_assert(!std::is_copy_assignable_v<RunCaptureOutcome>);
    static_assert(noexcept(omega::app::detail::PlanFiniteRunCapture(0, 0U)));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>()
                               .requested_frame_limit()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>().result()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>().completion()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>()
                               .scheduler_state_before()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>()
                               .scheduler_state_after()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>().failure()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>().has_traces()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>().trace_pair()));
    static_assert(noexcept(std::declval<const RunCaptureOutcome&>().terminal_input()));
    using ExtractedTracePair = decltype(
        std::declval<RunCaptureOutcome&&>().TakeTracePair());
    static_assert(std::same_as<ExtractedTracePair,
        std::optional<RunCaptureTracePair>>);
    static_assert(std::is_nothrow_move_constructible_v<ExtractedTracePair>);
    static_assert(!std::is_copy_constructible_v<ExtractedTracePair>);
    static_assert(TracePairExtractable<RunCaptureOutcome>);
    static_assert(!TracePairExtractable<RunCaptureOutcome&>);
    static_assert(!TracePairExtractable<const RunCaptureOutcome>);
    static_assert(noexcept(
        std::declval<RunCaptureOutcome&&>().TakeTracePair()));
    static_assert(noexcept(omega::app::detail::IsCompleteRunCaptureOutcome(
        std::declval<const RunCaptureOutcome&>(), 1)));

    constexpr std::array completions{
        RunCaptureCompletion::Inert,
        RunCaptureCompletion::FrameLimitReached,
        RunCaptureCompletion::QuitRequested,
        RunCaptureCompletion::OperationalFailure,
        RunCaptureCompletion::CaptureFailure,
    };
    constexpr std::array<std::string_view, completions.size()> names{
        "inert",
        "frame-limit-reached",
        "quit-requested",
        "operational-failure",
        "capture-failure",
    };
    for (std::size_t index = 0U; index < completions.size(); ++index)
    {
        Check(omega::app::RunCaptureCompletionName(completions[index]) ==
                  names[index],
            "each completion has its fixed name");
    }
    Check(omega::app::RunCaptureCompletionName(
              static_cast<RunCaptureCompletion>(99)) == "unknown",
        "an unknown completion has the fixed fallback name");

    constexpr RunResult first{.rendered_frames = 2, .input_frames = 2U};
    constexpr RunResult second{.rendered_frames = 2, .input_frames = 2U};
    static_assert(first == second);
}

void CheckRenderedFrameCount()
{
    constexpr int minimum = std::numeric_limits<int>::min();
    constexpr int maximum = std::numeric_limits<int>::max();
    static_assert(noexcept(CheckedNextRenderedFrameCount(0)));

    constexpr auto after_minimum = CheckedNextRenderedFrameCount(minimum);
    constexpr auto after_negative_one = CheckedNextRenderedFrameCount(-1);
    constexpr auto after_zero = CheckedNextRenderedFrameCount(0);
    constexpr auto at_maximum = CheckedNextRenderedFrameCount(maximum - 1);
    constexpr auto exhausted = CheckedNextRenderedFrameCount(maximum);
    static_assert(after_minimum && *after_minimum == minimum + 1);
    static_assert(after_negative_one && *after_negative_one == 0);
    static_assert(after_zero && *after_zero == 1);
    static_assert(at_maximum && *at_maximum == maximum);
    static_assert(!exhausted);

    Check(after_minimum && *after_minimum == minimum + 1,
        "the rendered-frame counter increments from int minimum");
    Check(after_negative_one && *after_negative_one == 0,
        "the rendered-frame counter crosses from negative to zero");
    Check(after_zero && *after_zero == 1,
        "the rendered-frame counter increments from zero");
    Check(at_maximum && *at_maximum == maximum,
        "the rendered-frame counter reaches int maximum exactly");
    Check(!exhausted,
        "the rendered-frame counter rejects an increment beyond int maximum");
}

template <typename Value>
void CheckPlanError(const std::expected<Value, FiniteRunCapturePlanError>& result,
    const FiniteRunCapturePlanError expected, const std::string_view message)
{
    Check(!result && result.error() == expected, message);
}

void CheckPlanning()
{
    using omega::app::detail::PlanFiniteRunCapture;
    constexpr std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();
    constexpr std::size_t session_maximum =
        omega::runtime::kMaximumRunCaptureSessionFrames;

    CheckPlanError(PlanFiniteRunCapture(-1, maximum),
        FiniteRunCapturePlanError::NegativeFrameLimit,
        "negative validation has priority at the terminal index");
    CheckPlanError(PlanFiniteRunCapture(
                       static_cast<int>(session_maximum + 1U), 0U),
        FiniteRunCapturePlanError::FrameLimitExceeded,
        "a limit above the session maximum is rejected");

    const auto zero = PlanFiniteRunCapture(0, maximum);
    Check(zero && zero->requested_frames == 0U &&
              zero->session.maximum_frames == 1U &&
              zero->session.first_frame_index == maximum,
        "zero frames use capacity one at any next index");

    const auto one = PlanFiniteRunCapture(1, 0U);
    Check(one && one->requested_frames == 1U &&
              one->session.maximum_frames == 1U &&
              one->session.first_frame_index == 0U,
        "one requested frame maps to one session frame");
    CheckPlanError(PlanFiniteRunCapture(1, maximum),
        FiniteRunCapturePlanError::FrameRangeExhausted,
        "a positive run cannot wrap the next terminal index");
    const auto terminal_one = PlanFiniteRunCapture(1, maximum - 1U);
    Check(terminal_one && terminal_one->session.first_frame_index == maximum - 1U,
        "one frame immediately before uint64 max keeps a representable next index");

    const int exact_maximum = static_cast<int>(session_maximum);
    const auto maximum_from_zero = PlanFiniteRunCapture(exact_maximum, 0U);
    Check(maximum_from_zero &&
              maximum_from_zero->session.maximum_frames == session_maximum,
        "the exact hard maximum is accepted from zero");
    const std::uint64_t exact_boundary = maximum - session_maximum;
    const auto maximum_at_boundary =
        PlanFiniteRunCapture(exact_maximum, exact_boundary);
    Check(maximum_at_boundary &&
              maximum_at_boundary->session.first_frame_index == exact_boundary,
        "the exact maximum may end with a representable uint64 next index");
    CheckPlanError(PlanFiniteRunCapture(exact_maximum, exact_boundary + 1U),
        FiniteRunCapturePlanError::FrameRangeExhausted,
        "one index beyond the maximum range boundary is rejected");

    constexpr std::array errors{
        FiniteRunCapturePlanError::NegativeFrameLimit,
        FiniteRunCapturePlanError::FrameLimitExceeded,
        FiniteRunCapturePlanError::FrameRangeExhausted,
    };
    constexpr std::array<std::string_view, errors.size()> names{
        "negative-frame-limit",
        "frame-limit-exceeded",
        "frame-range-exhausted",
    };
    constexpr std::array<std::string_view, errors.size()> messages{
        "run capture frame limit is negative",
        "run capture frame limit exceeds the session maximum",
        "run capture frame range exhausts uint64",
    };
    for (std::size_t index = 0U; index < errors.size(); ++index)
    {
        Check(omega::app::detail::FiniteRunCapturePlanErrorName(errors[index]) ==
                  names[index] &&
              omega::app::detail::FiniteRunCapturePlanErrorMessage(errors[index]) ==
                  messages[index],
            "each planning error has fixed name and message text");
    }
    const auto unknown = static_cast<FiniteRunCapturePlanError>(99);
    Check(omega::app::detail::FiniteRunCapturePlanErrorName(unknown) == "unknown" &&
              omega::app::detail::FiniteRunCapturePlanErrorMessage(unknown) ==
                  "run capture plan error is unknown",
        "an unknown planning error has fixed fallback text");
}

void CheckFormatter()
{
    const RunCaptureSessionError base{
        .operation = RunCaptureSessionOperation::AppendInput,
        .code = RunCaptureSessionErrorCode::InvalidSessionState,
        .message = "ignored",
    };
    Check(omega::app::detail::FormatRunCaptureSessionError(base) ==
              "run capture append-input: invalid-session-state",
        "the formatter preserves operation and session category");

    constexpr std::array input_codes{
        InputTraceErrorCode::InvalidConfiguration,
        InputTraceErrorCode::InvalidActionSchema,
        InputTraceErrorCode::AllocationFailed,
        InputTraceErrorCode::InvalidRecorderState,
        InputTraceErrorCode::CapacityExceeded,
        InputTraceErrorCode::FrameDiscontinuity,
        InputTraceErrorCode::ActionSchemaMismatch,
    };
    for (const InputTraceErrorCode leaf : input_codes)
    {
        const RunCaptureSessionError error{
            .operation = RunCaptureSessionOperation::CreateInputTrace,
            .code = RunCaptureSessionErrorCode::InputTraceFailure,
            .message = "ignored",
            .input_trace_error_code = leaf,
        };
        const std::string expected =
            "run capture create-input-trace: input-trace-failure (input-trace: " +
            std::string(omega::runtime::InputTraceErrorCodeName(leaf)) + ')';
        Check(omega::app::detail::FormatRunCaptureSessionError(error) == expected,
            "every input leaf family is formatted exactly");
    }

    constexpr std::array elapsed_codes{
        SchedulerElapsedTraceErrorCode::InvalidConfiguration,
        SchedulerElapsedTraceErrorCode::AllocationFailed,
        SchedulerElapsedTraceErrorCode::InvalidRecorderState,
        SchedulerElapsedTraceErrorCode::CapacityExceeded,
        SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
    };
    for (const SchedulerElapsedTraceErrorCode leaf : elapsed_codes)
    {
        const RunCaptureSessionError error{
            .operation = RunCaptureSessionOperation::AppendElapsed,
            .code = RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure,
            .message = "ignored",
            .scheduler_elapsed_trace_error_code = leaf,
        };
        const std::string expected =
            "run capture append-elapsed: scheduler-elapsed-trace-failure "
            "(scheduler-elapsed-trace: " +
            std::string(
                omega::runtime::SchedulerElapsedTraceErrorCodeName(leaf)) + ')';
        Check(omega::app::detail::FormatRunCaptureSessionError(error) == expected,
            "every scheduler-elapsed leaf family is formatted exactly");
    }

    const RunCaptureSessionError both{
        .operation = RunCaptureSessionOperation::FinishInputTrace,
        .code = RunCaptureSessionErrorCode::InputTraceFailure,
        .message = "ignored",
        .input_trace_error_code = InputTraceErrorCode::CapacityExceeded,
        .scheduler_elapsed_trace_error_code =
            SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
    };
    Check(omega::app::detail::FormatRunCaptureSessionError(both) ==
              "run capture finish-input-trace: input-trace-failure "
              "(input-trace: capacity-exceeded) "
              "(scheduler-elapsed-trace: frame-discontinuity)",
        "malformed dual leaf metadata is retained in deterministic family order");
}

void CheckEmptyAndNormalOutcomes()
{
    RunCaptureTracePair empty_pair = BuildEmptyPair(12U);
    RunCaptureOutcome empty = MakeOutcome(0U, RunResult{},
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{std::move(empty_pair)});
    Check(empty.requested_frame_limit() == 0U &&
              empty.result() == RunResult{} &&
              empty.completion() == RunCaptureCompletion::FrameLimitReached &&
              empty.scheduler_state_before() == SchedulerBefore() &&
              empty.scheduler_state_after() == SchedulerAfter() &&
              !empty.failure() && empty.has_traces() &&
              empty.trace_pair() != nullptr &&
              empty.trace_pair()->input_trace().first_frame_index() == 12U &&
              empty.trace_pair()->input_trace().frame_count() == 0U &&
              empty.trace_pair()->scheduler_elapsed_trace().frame_count() == 0U &&
              !empty.terminal_input(),
        "an empty outcome owns complete metadata and two empty traces");

    const RunResult normal_result{
        .rendered_frames = 2,
        .planned_simulation_steps = 2U,
        .executed_simulation_steps = 2U,
        .input_frames = 2U,
    };
    RunCaptureTracePair normal_pair = BuildNormalPair();
    RunCaptureOutcome normal = MakeOutcome(2U, normal_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{std::move(normal_pair)});
    Check(normal.requested_frame_limit() == 2U &&
              normal.result() == normal_result && !normal.failure() &&
              normal.has_traces() && normal.trace_pair() != nullptr &&
              normal.trace_pair()->input_trace().frame_count() == 2U &&
              normal.trace_pair()->scheduler_elapsed_trace().frame_count() == 2U &&
              normal.trace_pair()->scheduler_elapsed_trace().FrameAt(0U) &&
              normal.trace_pair()->scheduler_elapsed_trace().FrameAt(0U)->elapsed ==
                  milliseconds{3} &&
              !normal.terminal_input(),
        "a normal outcome owns aligned input and exact elapsed traces");

    RunCaptureOutcome moved = std::move(normal);
    Check(normal.requested_frame_limit() == 0U && normal.result() == RunResult{} &&
              normal.completion() == RunCaptureCompletion::Inert &&
              normal.scheduler_state_before() == FrameSchedulerState{} &&
              normal.scheduler_state_after() == FrameSchedulerState{} &&
              !normal.failure() && !normal.has_traces() &&
              normal.trace_pair() == nullptr && !normal.terminal_input(),
        "outcome move construction normalizes every source field");
    Check(moved.requested_frame_limit() == 2U && moved.result() == normal_result &&
              moved.completion() == RunCaptureCompletion::FrameLimitReached &&
              moved.has_traces() && moved.trace_pair() != nullptr &&
              moved.trace_pair()->input_trace().frame_count() == 2U,
        "outcome move construction transfers complete ownership");
}

void CheckTerminalAndFailureOutcomes()
{
    std::optional<RunCaptureTerminalInput> owned_terminal;
    {
        RunCaptureTracePair terminal_pair = BuildTerminalPair(false, true);
        RunCaptureOutcome terminal = MakeOutcome(1U,
            RunResult{.input_frames = 1U, .quit_requested = true},
            RunCaptureCompletion::QuitRequested, std::nullopt,
            std::optional<RunCaptureTracePair>{std::move(terminal_pair)});
        owned_terminal = terminal.terminal_input();
        Check(terminal.has_traces() && terminal.trace_pair() != nullptr &&
                  terminal.trace_pair()->input_trace().frame_count() == 1U &&
                  terminal.trace_pair()->scheduler_elapsed_trace().frame_count() == 0U &&
                  owned_terminal == RunCaptureTerminalInput{
                                        .frame_index = 0U,
                                        .host_quit_requested = false,
                                        .logical_quit_pressed = true,
                                    },
            "a quit outcome owns one exact unpaired terminal input");
    }
    Check(owned_terminal && owned_terminal->logical_quit_pressed,
        "owned terminal metadata survives outcome destruction");

    RunCaptureTracePair operational_pair = BuildTerminalPair(true, false);
    RunCaptureOutcome operational = MakeOutcome(3U,
        RunResult{.input_frames = 1U, .quit_requested = true},
        RunCaptureCompletion::OperationalFailure,
        std::optional<std::string>{"render failed"},
        std::optional<RunCaptureTracePair>{std::move(operational_pair)});
    Check(operational.failure() == std::optional<std::string_view>{"render failed"} &&
              operational.has_traces() && operational.trace_pair() != nullptr &&
              operational.terminal_input() == RunCaptureTerminalInput{
                                                  .frame_index = 0U,
                                                  .host_quit_requested = true,
                                                  .logical_quit_pressed = false,
                                              },
        "an operational failure can retain owned failure text and finished traces");

    RunCaptureOutcome capture_failure = MakeOutcome(4U, RunResult{},
        RunCaptureCompletion::CaptureFailure,
        std::optional<std::string>{"capture finalization failed"}, std::nullopt);
    Check(capture_failure.requested_frame_limit() == 4U &&
              capture_failure.completion() == RunCaptureCompletion::CaptureFailure &&
              capture_failure.failure() ==
                  std::optional<std::string_view>{"capture finalization failed"} &&
              !capture_failure.has_traces() &&
              capture_failure.trace_pair() == nullptr &&
              !capture_failure.terminal_input(),
        "a capture failure can own failure text without publishing partial traces");

    RunCaptureOutcome moved_failure = std::move(capture_failure);
    Check(!capture_failure.failure() && !capture_failure.has_traces() &&
              capture_failure.completion() == RunCaptureCompletion::Inert &&
              moved_failure.failure() ==
                  std::optional<std::string_view>{"capture finalization failed"},
        "failure ownership transfers while the source becomes inert");
}

void CheckTracePairExtraction()
{
    RunCaptureOutcome present = MakeOutcome(2U,
        RunResult{.input_frames = 2U, .quit_requested = true},
        RunCaptureCompletion::OperationalFailure,
        std::optional<std::string>{"owned failure"},
        std::optional<RunCaptureTracePair>{BuildElapsedThenTerminalPair()});
    const auto borrowed_failure = present.failure();
    const RunCaptureTracePair* const borrowed_pair = present.trace_pair();
    Check(borrowed_failure == std::optional<std::string_view>{"owned failure"} &&
              borrowed_pair != nullptr,
        "the extraction fixture exposes the borrows that extraction invalidates");

    auto extracted = std::move(present).TakeTracePair();
    Check(IsFullyInert(present),
        "present extraction normalizes every source field to inert");
    const auto input_frame = extracted
        ? extracted->input_trace().FrameAt(1U)
        : std::nullopt;
    const auto quit_action = extracted
        ? extracted->input_trace().ActionAt(1U, kQuitAction)
        : std::nullopt;
    const auto elapsed_frame = extracted
        ? extracted->scheduler_elapsed_trace().FrameAt(0U)
        : std::nullopt;
    const auto terminal = extracted
        ? extracted->terminal_input()
        : std::nullopt;
    Check(extracted &&
              extracted->input_trace().first_frame_index() == 0U &&
              extracted->input_trace().maximum_frames() == 2U &&
              extracted->input_trace().frame_count() == 2U &&
              extracted->input_trace().actions().size() == 1U &&
              extracted->input_trace().actions()[0] == kQuitAction &&
              input_frame && input_frame->frame_index == 1U &&
              input_frame->accepted_event_count == 1U &&
              input_frame->rejected_event_count == 0U &&
              quit_action && quit_action->held && quit_action->pressed &&
              !quit_action->released &&
              extracted->scheduler_elapsed_trace().first_frame_index() == 0U &&
              extracted->scheduler_elapsed_trace().maximum_frames() == 2U &&
              extracted->scheduler_elapsed_trace().frame_count() == 1U &&
              elapsed_frame && elapsed_frame->frame_index == 0U &&
              elapsed_frame->elapsed == milliseconds{13} &&
              !extracted->scheduler_elapsed_trace().FrameAt(1U) &&
              terminal == RunCaptureTerminalInput{
                              .frame_index = 1U,
                              .host_quit_requested = false,
                              .logical_quit_pressed = true,
                          },
        "present extraction preserves exact trace schema, frame, action, and terminal data");

    auto second_extraction = std::move(present).TakeTracePair();
    Check(!second_extraction && IsFullyInert(present),
        "an already extracted inert outcome remains absent and inert");

    RunCaptureOutcome absent = MakeOutcome(9U,
        RunResult{.rendered_frames = 4, .input_frames = 4U},
        RunCaptureCompletion::CaptureFailure,
        std::optional<std::string>{"no trace pair"}, std::nullopt);
    Check(absent.failure() == std::optional<std::string_view>{"no trace pair"} &&
              !absent.has_traces(),
        "the absent extraction fixture begins with owned nontrace state");
    auto absent_extraction = std::move(absent).TakeTracePair();
    Check(!absent_extraction && IsFullyInert(absent),
        "absent extraction returns none and normalizes every source field");

    RunCaptureOutcome move_source = MakeOutcome(0U, RunResult{},
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildEmptyPair(23U)});
    RunCaptureOutcome move_target = std::move(move_source);
    auto moved_source_extraction = std::move(move_source).TakeTracePair();
    Check(!moved_source_extraction && IsFullyInert(move_source) &&
              move_target.has_traces() && move_target.trace_pair() != nullptr &&
              move_target.trace_pair()->input_trace().first_frame_index() == 23U,
        "a moved-from outcome extracts none without disturbing the new owner");
}

void CheckCompleteOutcomePolicy()
{
    using omega::app::detail::IsCompleteRunCaptureOutcome;
    constexpr RunResult complete_result{
        .rendered_frames = 2,
        .planned_simulation_steps = 3U,
        .executed_simulation_steps = 3U,
        .input_frames = 2U,
    };

    RunCaptureOutcome complete = MakeOutcome(2U, complete_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(IsCompleteRunCaptureOutcome(complete, 2),
        "a complete positive finite capture satisfies the app-private policy");
    Check(!IsCompleteRunCaptureOutcome(complete, 0) &&
              !IsCompleteRunCaptureOutcome(complete, -1),
        "nonpositive requested frame counts fail closed");

    RunCaptureOutcome requested_mismatch = MakeOutcome(1U, complete_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(!IsCompleteRunCaptureOutcome(requested_mismatch, 2),
        "outcome and command requested-frame mismatch fails closed");

    constexpr std::array incomplete_completions{
        RunCaptureCompletion::Inert,
        RunCaptureCompletion::QuitRequested,
        RunCaptureCompletion::OperationalFailure,
        RunCaptureCompletion::CaptureFailure,
    };
    for (const RunCaptureCompletion completion : incomplete_completions)
    {
        RunCaptureOutcome incomplete = MakeOutcome(2U, complete_result, completion,
            std::nullopt, std::optional<RunCaptureTracePair>{BuildNormalPair()});
        Check(!IsCompleteRunCaptureOutcome(incomplete, 2),
            "every non-frame-limit completion fails closed");
    }

    RunCaptureOutcome failed = MakeOutcome(2U, complete_result,
        RunCaptureCompletion::FrameLimitReached,
        std::optional<std::string>{"owned failure"},
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(!IsCompleteRunCaptureOutcome(failed, 2),
        "owned failure text fails closed without changing borrow semantics");

    RunResult rendered_mismatch_result = complete_result;
    rendered_mismatch_result.rendered_frames = 1;
    RunCaptureOutcome rendered_mismatch = MakeOutcome(2U, rendered_mismatch_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(!IsCompleteRunCaptureOutcome(rendered_mismatch, 2),
        "rendered-frame mismatch fails closed");

    RunResult input_mismatch_result = complete_result;
    input_mismatch_result.input_frames = 1U;
    RunCaptureOutcome input_mismatch = MakeOutcome(2U, input_mismatch_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(!IsCompleteRunCaptureOutcome(input_mismatch, 2),
        "input-frame mismatch fails closed");

    RunResult quit_result = complete_result;
    quit_result.quit_requested = true;
    RunCaptureOutcome quit = MakeOutcome(2U, quit_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(!IsCompleteRunCaptureOutcome(quit, 2),
        "a quit result fails closed even with otherwise complete traces");

    RunResult step_mismatch_result = complete_result;
    step_mismatch_result.executed_simulation_steps = 2U;
    RunCaptureOutcome step_mismatch = MakeOutcome(2U, step_mismatch_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNormalPair()});
    Check(!IsCompleteRunCaptureOutcome(step_mismatch, 2),
        "planned and executed simulation-step mismatch fails closed");

    RunCaptureOutcome absent_pair = MakeOutcome(2U, complete_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt, std::nullopt);
    Check(!IsCompleteRunCaptureOutcome(absent_pair, 2),
        "an absent trace pair fails closed");

    constexpr RunResult terminal_result{
        .rendered_frames = 1,
        .planned_simulation_steps = 1U,
        .executed_simulation_steps = 1U,
        .input_frames = 1U,
    };
    RunCaptureOutcome terminal_pair = MakeOutcome(1U, terminal_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildTerminalPair(true, false)});
    Check(!IsCompleteRunCaptureOutcome(terminal_pair, 1),
        "a terminal trace pair fails closed");

    RunCaptureOutcome capacity_mismatch = MakeOutcome(2U, complete_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNonTerminalPair(3U, 2U)});
    Check(!IsCompleteRunCaptureOutcome(capacity_mismatch, 2),
        "trace capacity mismatch fails closed even when active counts match");

    RunCaptureOutcome count_mismatch = MakeOutcome(2U, complete_result,
        RunCaptureCompletion::FrameLimitReached, std::nullopt,
        std::optional<RunCaptureTracePair>{BuildNonTerminalPair(2U, 1U)});
    Check(!IsCompleteRunCaptureOutcome(count_mismatch, 2),
        "trace active-count mismatch fails closed even when capacities match");
}
} // namespace

int main()
{
    CheckContract();
    CheckRenderedFrameCount();
    CheckPlanning();
    CheckFormatter();
    CheckEmptyAndNormalOutcomes();
    CheckTerminalAndFailureOutcomes();
    CheckTracePairExtraction();
    CheckCompleteOutcomePolicy();

    if (failures == 0)
        std::cout << "omega_run_capture_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
