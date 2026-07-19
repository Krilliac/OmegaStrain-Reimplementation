#pragma once

#include "omega/runtime/input_trace.h"
#include "omega/runtime/scheduler_elapsed_trace.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::runtime
{
// Synthetic in-process diagnostic policy, not a retail limit or timing claim.
inline constexpr std::size_t kMaximumRunCaptureSessionFrames = 65'536U;
static_assert(kMaximumRunCaptureSessionFrames == kMaximumInputTraceFrames);
static_assert(kMaximumRunCaptureSessionFrames == kMaximumSchedulerElapsedTraceFrames);

// Input frame records, fixed action-schema backing, and elapsed frame records at the hard
// maximum. This excludes excess vector capacity, allocator/object overhead, and process RSS.
inline constexpr std::size_t kMaximumRunCaptureSessionElementPayloadBytes =
    kMaximumRunCaptureSessionFrames * 32U +
    InputBindingTable::kMaxActions * sizeof(std::uint32_t) +
    kMaximumRunCaptureSessionFrames * sizeof(std::int64_t);
static_assert(kMaximumRunCaptureSessionElementPayloadBytes == 2'621'696U);

struct RunCaptureSessionConfig
{
    std::size_t maximum_frames = 0U;
    std::uint64_t first_frame_index = 0U;
};

enum class RunCaptureSessionOperation
{
    CreateInputTrace,
    CreateSchedulerElapsedTrace,
    AppendInput,
    AppendElapsed,
    MarkTerminal,
    FinishInputTrace,
    FinishSchedulerElapsedTrace,
};

[[nodiscard]] constexpr std::string_view RunCaptureSessionOperationName(
    const RunCaptureSessionOperation operation) noexcept
{
    switch (operation)
    {
    case RunCaptureSessionOperation::CreateInputTrace:
        return "create-input-trace";
    case RunCaptureSessionOperation::CreateSchedulerElapsedTrace:
        return "create-scheduler-elapsed-trace";
    case RunCaptureSessionOperation::AppendInput:
        return "append-input";
    case RunCaptureSessionOperation::AppendElapsed:
        return "append-elapsed";
    case RunCaptureSessionOperation::MarkTerminal:
        return "mark-terminal";
    case RunCaptureSessionOperation::FinishInputTrace:
        return "finish-input-trace";
    case RunCaptureSessionOperation::FinishSchedulerElapsedTrace:
        return "finish-scheduler-elapsed-trace";
    }
    return "unknown";
}

enum class RunCaptureSessionErrorCode
{
    InvalidSessionState,
    InvalidTerminalReason,
    InputTraceFailure,
    SchedulerElapsedTraceFailure,
};

[[nodiscard]] constexpr std::string_view RunCaptureSessionErrorCodeName(
    const RunCaptureSessionErrorCode code) noexcept
{
    switch (code)
    {
    case RunCaptureSessionErrorCode::InvalidSessionState:
        return "invalid-session-state";
    case RunCaptureSessionErrorCode::InvalidTerminalReason:
        return "invalid-terminal-reason";
    case RunCaptureSessionErrorCode::InputTraceFailure:
        return "input-trace-failure";
    case RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure:
        return "scheduler-elapsed-trace-failure";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RunCaptureSessionErrorMessage(
    const RunCaptureSessionErrorCode code) noexcept
{
    switch (code)
    {
    case RunCaptureSessionErrorCode::InvalidSessionState:
        return "run capture session state is invalid";
    case RunCaptureSessionErrorCode::InvalidTerminalReason:
        return "run capture terminal reason is invalid";
    case RunCaptureSessionErrorCode::InputTraceFailure:
        return "run capture input trace operation failed";
    case RunCaptureSessionErrorCode::SchedulerElapsedTraceFailure:
        return "run capture scheduler elapsed trace operation failed";
    }
    return "run capture session error is unknown";
}

struct RunCaptureSessionError
{
    RunCaptureSessionOperation operation =
        RunCaptureSessionOperation::CreateInputTrace;
    RunCaptureSessionErrorCode code =
        RunCaptureSessionErrorCode::InvalidSessionState;
    // Fixed category text only. It contains no frame, action, clock, path, or owner data.
    std::string_view message = RunCaptureSessionErrorMessage(code);
    std::optional<InputTraceErrorCode> input_trace_error_code;
    std::optional<SchedulerElapsedTraceErrorCode>
        scheduler_elapsed_trace_error_code;
};

// Small owned terminal value. It remains valid after the source pair moves or is destroyed.
struct RunCaptureTerminalInput
{
    std::uint64_t frame_index = 0U;
    bool host_quit_requested = false;
    bool logical_quit_pressed = false;

    friend constexpr bool operator==(const RunCaptureTerminalInput&,
        const RunCaptureTerminalInput&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<RunCaptureTerminalInput>);
static_assert(std::is_standard_layout_v<RunCaptureTerminalInput>);

class RunCaptureSession;

// Immutable, move-only, in-process capture result. After ownership publication, const reads are
// [any thread; reentrant] provided no reader races a move or destruction. The borrowed trace
// references are invalidated by pair move or destruction. This value is non-hot-reloadable.
class RunCaptureTracePair final
{
public:
    RunCaptureTracePair(RunCaptureTracePair&& other) noexcept;
    RunCaptureTracePair& operator=(RunCaptureTracePair&&) = delete;
    RunCaptureTracePair(const RunCaptureTracePair&) = delete;
    RunCaptureTracePair& operator=(const RunCaptureTracePair&) = delete;
    ~RunCaptureTracePair() = default;

    // [any thread; reentrant after publication] Borrowed until pair move or destruction.
    [[nodiscard]] const InputTrace& input_trace() const noexcept;
    [[nodiscard]] const SchedulerElapsedTrace& scheduler_elapsed_trace() const noexcept;

    // [any thread; reentrant after publication] Returns an owned optional terminal value.
    [[nodiscard]] std::optional<RunCaptureTerminalInput> terminal_input() const noexcept;

private:
    friend class RunCaptureSession;

    RunCaptureTracePair(InputTrace&& input_trace,
        SchedulerElapsedTrace&& scheduler_elapsed_trace,
        std::optional<RunCaptureTerminalInput> terminal_input) noexcept;

    InputTrace input_trace_;
    SchedulerElapsedTrace scheduler_elapsed_trace_;
    std::optional<RunCaptureTerminalInput> terminal_input_;
};

// Exclusive game-thread coordinator for bounded post-binding input and caller-supplied elapsed
// capture. It is not a service and has no SDL, host-clock, scheduler, app, RunResult, simulation,
// injection, playback, persistence, or wire-format dependency.
class RunCaptureSession final
{
public:
    // [any thread; reentrant] Creates input backing first and elapsed backing second. Failure
    // publishes no session and retains the exact failing leaf error category.
    [[nodiscard]] static std::expected<RunCaptureSession, RunCaptureSessionError> Create(
        RunCaptureSessionConfig config,
        std::span<const std::uint32_t> action_schema);

    // [game thread; no concurrent use] Transfers the complete session and leaves the source
    // inert. The session is non-hot-reloadable.
    RunCaptureSession(RunCaptureSession&& other) noexcept;
    RunCaptureSession& operator=(RunCaptureSession&&) = delete;
    RunCaptureSession(const RunCaptureSession&) = delete;
    RunCaptureSession& operator=(const RunCaptureSession&) = delete;
    ~RunCaptureSession() = default;

    // [game thread] Appends one exact logical snapshot, then requires elapsed or terminal input.
    // Failure changes neither the session nor the caller-owned snapshot.
    [[nodiscard]] std::expected<void, RunCaptureSessionError> AppendInput(
        const InputSnapshot& snapshot) noexcept;

    // [game thread] Appends elapsed for the pending snapshot's frame without interpreting it.
    // Failure leaves that snapshot pending and changes no coordinator state.
    [[nodiscard]] std::expected<void, RunCaptureSessionError> AppendElapsed(
        std::chrono::nanoseconds elapsed) noexcept;

    // [game thread] Ends capture on the pending input frame. At least one reason must be true.
    // Phase validation has priority over reason validation.
    [[nodiscard]] std::expected<void, RunCaptureSessionError> MarkTerminal(
        bool host_quit_requested, bool logical_quit_pressed) noexcept;

    // [game thread] Move-finalizes an open boundary or terminal session. An open empty session
    // succeeds; a pending unpaired input or inert session fails without consuming the session.
    // Once leaf finalization begins, this rvalue is consumed even on a leaf failure.
    [[nodiscard]] std::expected<RunCaptureTracePair, RunCaptureSessionError>
    Finish() && noexcept;

private:
    enum class Phase
    {
        Inert,
        AwaitInput,
        AwaitElapsedOrTerminal,
        Terminal,
    };

    RunCaptureSession(RunCaptureSessionConfig config,
        InputTraceRecorder&& input_recorder,
        SchedulerElapsedTraceRecorder&& scheduler_elapsed_recorder) noexcept;
    void NormalizeInert() noexcept;

    RunCaptureSessionConfig config_{};
    Phase phase_ = Phase::Inert;
    std::uint64_t pending_frame_index_ = 0U;
    std::optional<RunCaptureTerminalInput> terminal_input_;
    InputTraceRecorder input_recorder_;
    SchedulerElapsedTraceRecorder scheduler_elapsed_recorder_;
};
} // namespace omega::runtime
