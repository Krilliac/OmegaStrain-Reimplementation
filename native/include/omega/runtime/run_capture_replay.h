#pragma once

#include "omega/runtime/run_capture_session.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace omega::runtime
{
enum class RunCaptureReplayOperation
{
    Create,
    Next,
};

[[nodiscard]] constexpr std::string_view RunCaptureReplayOperationName(
    const RunCaptureReplayOperation operation) noexcept
{
    switch (operation)
    {
    case RunCaptureReplayOperation::Create:
        return "create";
    case RunCaptureReplayOperation::Next:
        return "next";
    }
    return "unknown";
}

enum class RunCaptureReplayErrorCode
{
    InvalidInputTrace,
    InvalidSchedulerElapsedTrace,
    CapacityMismatch,
    FirstFrameIndexMismatch,
    FrameCountMismatch,
    InvalidTerminalInput,
    AllocationFailed,
    TraceReadFailed,
    ReplayComplete,
    InvalidReplayState,
};

[[nodiscard]] constexpr std::string_view RunCaptureReplayErrorCodeName(
    const RunCaptureReplayErrorCode code) noexcept
{
    switch (code)
    {
    case RunCaptureReplayErrorCode::InvalidInputTrace:
        return "invalid-input-trace";
    case RunCaptureReplayErrorCode::InvalidSchedulerElapsedTrace:
        return "invalid-scheduler-elapsed-trace";
    case RunCaptureReplayErrorCode::CapacityMismatch:
        return "capacity-mismatch";
    case RunCaptureReplayErrorCode::FirstFrameIndexMismatch:
        return "first-frame-index-mismatch";
    case RunCaptureReplayErrorCode::FrameCountMismatch:
        return "frame-count-mismatch";
    case RunCaptureReplayErrorCode::InvalidTerminalInput:
        return "invalid-terminal-input";
    case RunCaptureReplayErrorCode::AllocationFailed:
        return "allocation-failed";
    case RunCaptureReplayErrorCode::TraceReadFailed:
        return "trace-read-failed";
    case RunCaptureReplayErrorCode::ReplayComplete:
        return "replay-complete";
    case RunCaptureReplayErrorCode::InvalidReplayState:
        return "invalid-replay-state";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RunCaptureReplayErrorMessage(
    const RunCaptureReplayErrorCode code) noexcept
{
    switch (code)
    {
    case RunCaptureReplayErrorCode::InvalidInputTrace:
        return "run capture replay input trace is invalid";
    case RunCaptureReplayErrorCode::InvalidSchedulerElapsedTrace:
        return "run capture replay scheduler elapsed trace is invalid";
    case RunCaptureReplayErrorCode::CapacityMismatch:
        return "run capture replay trace capacities do not match";
    case RunCaptureReplayErrorCode::FirstFrameIndexMismatch:
        return "run capture replay first frame indices do not match";
    case RunCaptureReplayErrorCode::FrameCountMismatch:
        return "run capture replay frame counts do not match terminal policy";
    case RunCaptureReplayErrorCode::InvalidTerminalInput:
        return "run capture replay terminal input is invalid";
    case RunCaptureReplayErrorCode::AllocationFailed:
        return "run capture replay snapshot allocation failed";
    case RunCaptureReplayErrorCode::TraceReadFailed:
        return "run capture replay trace read failed";
    case RunCaptureReplayErrorCode::ReplayComplete:
        return "run capture replay is complete";
    case RunCaptureReplayErrorCode::InvalidReplayState:
        return "run capture replay state is invalid";
    }
    return "run capture replay error is unknown";
}

struct RunCaptureReplayError
{
    RunCaptureReplayOperation operation = RunCaptureReplayOperation::Create;
    RunCaptureReplayErrorCode code = RunCaptureReplayErrorCode::InvalidInputTrace;
    // Fixed category text only. It contains no frame, action, clock, path, or owner data.
    std::string_view message = RunCaptureReplayErrorMessage(code);
};

namespace detail
{
// Metadata-only validation seam for structurally impossible public-pair states. It owns no
// backing and permits exact error-priority tests without weakening any trace constructor.
struct RunCaptureReplayTraceMetadata
{
    std::size_t maximum_frames = 0U;
    std::uint64_t first_frame_index = 0U;
    std::size_t frame_count = 0U;
};

struct RunCaptureReplayMetadata
{
    RunCaptureReplayTraceMetadata input_trace{};
    std::span<const std::uint32_t> action_schema{};
    RunCaptureReplayTraceMetadata scheduler_elapsed_trace{};
    std::optional<RunCaptureTerminalInput> terminal_input;
};

struct RunCaptureReplayPlan
{
    std::size_t frame_count = 0U;
    std::size_t elapsed_frame_count = 0U;
    bool has_terminal_frame = false;

    friend constexpr bool operator==(
        const RunCaptureReplayPlan&, const RunCaptureReplayPlan&) noexcept = default;
};

// [any thread; reentrant] Applies the complete fixed validation priority without allocation.
[[nodiscard]] std::expected<RunCaptureReplayPlan, RunCaptureReplayError>
PlanRunCaptureReplay(const RunCaptureReplayMetadata& metadata) noexcept;
} // namespace detail

class RunCaptureReplaySession;

// One owned immutable replay publication. Construction is restricted to the replay session, so
// exactly one timing/control variant exists: elapsed or terminal input, never both or neither.
class RunCaptureReplayFrame final
{
public:
    RunCaptureReplayFrame(RunCaptureReplayFrame&&) noexcept = default;
    RunCaptureReplayFrame& operator=(RunCaptureReplayFrame&&) = delete;
    RunCaptureReplayFrame(const RunCaptureReplayFrame&) = delete;
    RunCaptureReplayFrame& operator=(const RunCaptureReplayFrame&) = delete;
    ~RunCaptureReplayFrame() = default;

    // [any thread; reentrant after publication] Borrowed until this frame moves or is destroyed.
    // A moved-from frame may only be destroyed.
    [[nodiscard]] const InputSnapshot& input() const noexcept;

    // [any thread; reentrant after publication] Owned optional copies. Exactly one is engaged.
    [[nodiscard]] std::optional<std::chrono::nanoseconds> elapsed() const noexcept;
    [[nodiscard]] std::optional<RunCaptureTerminalInput> terminal_input() const noexcept;

private:
    friend class RunCaptureReplaySession;

    RunCaptureReplayFrame(
        std::unique_ptr<InputSnapshot>&& input, std::chrono::nanoseconds elapsed) noexcept;
    RunCaptureReplayFrame(
        std::unique_ptr<InputSnapshot>&& input, RunCaptureTerminalInput terminal) noexcept;

    std::unique_ptr<InputSnapshot> input_;
    std::optional<std::chrono::nanoseconds> elapsed_;
    std::optional<RunCaptureTerminalInput> terminal_input_;
};

// Exclusive game-thread cursor over one owned immutable pair. This is a runtime value object,
// not a service; it has no host, clock, scheduler, simulation, persistence, or injection state.
// The session and its owned trace backing are non-hot-reloadable.
class RunCaptureReplaySession final
{
public:
    // [any thread; reentrant before publication] Validates before moving. Failure leaves the
    // caller's rvalue-referenced pair unchanged; success transfers both trace owners zero-copy.
    [[nodiscard]] static std::expected<RunCaptureReplaySession, RunCaptureReplayError>
    Create(RunCaptureTracePair&& traces) noexcept;

    // [game thread; no concurrent use] Transfers the cursor and pair, leaving the source inert.
    RunCaptureReplaySession(RunCaptureReplaySession&& other) noexcept;
    RunCaptureReplaySession& operator=(RunCaptureReplaySession&&) = delete;
    RunCaptureReplaySession(const RunCaptureReplaySession&) = delete;
    RunCaptureReplaySession& operator=(const RunCaptureReplaySession&) = delete;
    ~RunCaptureReplaySession() = default;

    // [game thread; no concurrent use] Reconstructs one exact owned snapshot transactionally.
    // InputSnapshot reconstruction allocation or defensive trace-read failure leaves the cursor
    // unchanged. A successful publication contains elapsed or terminal input, never both and
    // never neither.
    [[nodiscard]] std::expected<RunCaptureReplayFrame, RunCaptureReplayError> Next() noexcept;

    // [game thread; no concurrent use] Inert and complete sessions both have zero remaining;
    // complete() distinguishes successful exhaustion from moved-from invalid state.
    [[nodiscard]] std::size_t remaining_frames() const noexcept;
    [[nodiscard]] bool complete() const noexcept;

private:
    enum class State
    {
        Inert,
        Ready,
        Complete,
    };

    RunCaptureReplaySession(
        RunCaptureTracePair&& traces, const detail::RunCaptureReplayPlan& plan) noexcept;
    void NormalizeInert() noexcept;

    std::optional<RunCaptureTracePair> traces_;
    std::size_t cursor_ = 0U;
    std::size_t frame_count_ = 0U;
    std::size_t elapsed_frame_count_ = 0U;
    State state_ = State::Inert;
};
} // namespace omega::runtime
