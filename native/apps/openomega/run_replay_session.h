#pragma once

#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/run_capture_replay.h"
#include "omega/simulation/simulation_world.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

namespace omega::app
{
// Project-owned logical action identifiers used only by the synthetic native diagnostic host.
// They are not retail input bindings or control-layout claims.
inline constexpr std::uint32_t kDebugMoveForwardAction = 2U;
inline constexpr std::uint32_t kDebugMoveBackwardAction = 3U;
inline constexpr std::uint32_t kDebugMoveLeftAction = 4U;
inline constexpr std::uint32_t kDebugMoveRightAction = 5U;

struct RunReplaySessionConfig
{
    runtime::FrameSchedulerConfig scheduler{};
    std::uint32_t maximum_entities = 65'536U;
    // Project-owned diagnostic input policy. Disabled preserves the E0059 replay behavior even
    // when a trace happens to contain the synthetic movement action identifiers.
    bool enable_debug_locomotion = false;

    friend constexpr bool operator==(
        const RunReplaySessionConfig&, const RunReplaySessionConfig&) noexcept = default;
};

enum class RunReplayOperation
{
    Create,
    Next,
};

[[nodiscard]] constexpr std::string_view RunReplayOperationName(
    const RunReplayOperation operation) noexcept
{
    switch (operation)
    {
    case RunReplayOperation::Create:
        return "create";
    case RunReplayOperation::Next:
        return "next";
    }
    return "unknown";
}

enum class RunReplayErrorCode
{
    InvalidSchedulerConfig,
    InvalidEntityCapacity,
    SimulationWorldCreateFailed,
    ReplayCreateFailed,
    ReplayNextFailed,
    ReplayComplete,
    InvalidSessionState,
    SimulationRepresentationExhausted,
    DebugLocomotionEntityCreateFailed,
    DebugLocomotionPlanFailed,
};

[[nodiscard]] constexpr std::string_view RunReplayErrorCodeName(
    const RunReplayErrorCode code) noexcept
{
    switch (code)
    {
    case RunReplayErrorCode::InvalidSchedulerConfig:
        return "invalid-scheduler-config";
    case RunReplayErrorCode::InvalidEntityCapacity:
        return "invalid-entity-capacity";
    case RunReplayErrorCode::SimulationWorldCreateFailed:
        return "simulation-world-create-failed";
    case RunReplayErrorCode::ReplayCreateFailed:
        return "replay-create-failed";
    case RunReplayErrorCode::ReplayNextFailed:
        return "replay-next-failed";
    case RunReplayErrorCode::ReplayComplete:
        return "replay-complete";
    case RunReplayErrorCode::InvalidSessionState:
        return "invalid-session-state";
    case RunReplayErrorCode::SimulationRepresentationExhausted:
        return "simulation-representation-exhausted";
    case RunReplayErrorCode::DebugLocomotionEntityCreateFailed:
        return "debug-locomotion-entity-create-failed";
    case RunReplayErrorCode::DebugLocomotionPlanFailed:
        return "debug-locomotion-plan-failed";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view RunReplayErrorMessage(
    const RunReplayErrorCode code) noexcept
{
    switch (code)
    {
    case RunReplayErrorCode::InvalidSchedulerConfig:
        return "run replay scheduler configuration is invalid";
    case RunReplayErrorCode::InvalidEntityCapacity:
        return "run replay entity capacity is invalid";
    case RunReplayErrorCode::SimulationWorldCreateFailed:
        return "run replay simulation world creation failed";
    case RunReplayErrorCode::ReplayCreateFailed:
        return "run replay capture replay creation failed";
    case RunReplayErrorCode::ReplayNextFailed:
        return "run replay capture replay advance failed";
    case RunReplayErrorCode::ReplayComplete:
        return "run replay is complete";
    case RunReplayErrorCode::InvalidSessionState:
        return "run replay session state is invalid";
    case RunReplayErrorCode::SimulationRepresentationExhausted:
        return "run replay simulation representation is exhausted";
    case RunReplayErrorCode::DebugLocomotionEntityCreateFailed:
        return "run replay debug locomotion entity creation failed";
    case RunReplayErrorCode::DebugLocomotionPlanFailed:
        return "run replay debug locomotion planning failed";
    }
    return "run replay error is unknown";
}

struct RunReplayError
{
    RunReplayOperation operation = RunReplayOperation::Create;
    RunReplayErrorCode code = RunReplayErrorCode::InvalidSchedulerConfig;
    // Fixed category text only. It contains no frame, action, clock, path, or owner data.
    std::string_view message = RunReplayErrorMessage(code);
    std::optional<runtime::RunCaptureReplayError> replay_error;
};

enum class RunReplaySessionState
{
    Inert,
    Ready,
    Complete,
    Failed,
};

[[nodiscard]] constexpr std::string_view RunReplaySessionStateName(
    const RunReplaySessionState state) noexcept
{
    switch (state)
    {
    case RunReplaySessionState::Inert:
        return "inert";
    case RunReplaySessionState::Ready:
        return "ready";
    case RunReplaySessionState::Complete:
        return "complete";
    case RunReplaySessionState::Failed:
        return "failed";
    }
    return "unknown";
}

class RunReplaySession;

// One owned immutable app-layer replay publication. An elapsed publication has one frame plan;
// a terminal publication has none. This value owns the lower replay frame and its input snapshot.
class RunReplayFrame final
{
public:
    RunReplayFrame(RunReplayFrame&&) noexcept = default;
    RunReplayFrame& operator=(RunReplayFrame&&) = delete;
    RunReplayFrame(const RunReplayFrame&) = delete;
    RunReplayFrame& operator=(const RunReplayFrame&) = delete;
    ~RunReplayFrame() = default;

    // [any thread; reentrant after publication] Borrowed until this frame moves or is destroyed.
    // A moved-from frame may only be destroyed.
    [[nodiscard]] const runtime::InputSnapshot& input() const noexcept;

    // [any thread; reentrant after publication] Owned optional copies.
    [[nodiscard]] std::optional<std::chrono::nanoseconds> elapsed() const noexcept;
    [[nodiscard]] std::optional<runtime::RunCaptureTerminalInput>
    terminal_input() const noexcept;
    [[nodiscard]] std::optional<runtime::FramePlan> frame_plan() const noexcept;

private:
    friend class RunReplaySession;

    RunReplayFrame(runtime::RunCaptureReplayFrame&& replay_frame,
        runtime::FramePlan frame_plan) noexcept;
    explicit RunReplayFrame(runtime::RunCaptureReplayFrame&& replay_frame) noexcept;

    runtime::RunCaptureReplayFrame replay_frame_;
    std::optional<runtime::FramePlan> frame_plan_;
};

// Exclusive game-thread composition of a fresh scheduler, fresh simulation world, and one owned
// replay cursor. This is an app-layer value, not a service; it is SDL-free and non-hot-reloadable.
class RunReplaySession final
{
public:
    // [any thread; reentrant before publication] Validation and creation occur before trace
    // transfer. Any reported failure leaves the caller's pair unchanged; success transfers it.
    [[nodiscard]] static std::expected<RunReplaySession, RunReplayError> Create(
        runtime::RunCaptureTracePair&& traces, RunReplaySessionConfig config);

    // [game thread; no concurrent use] Transfers every owner and leaves the source inert.
    RunReplaySession(RunReplaySession&& other) noexcept;
    RunReplaySession& operator=(RunReplaySession&&) = delete;
    RunReplaySession(const RunReplaySession&) = delete;
    RunReplaySession& operator=(const RunReplaySession&) = delete;
    ~RunReplaySession() = default;

    // [game thread; no concurrent use] Consumes one replay frame. Lower replay failures are
    // retryable and transactional. After a normal replay frame and scheduler plan are consumed,
    // diagnostic locomotion-planning or simulation-step failure permanently fails this session.
    [[nodiscard]] std::expected<RunReplayFrame, RunReplayError> Next() noexcept;

    // [game thread; no concurrent use] Returned snapshots are owned copies.
    [[nodiscard]] RunReplaySessionState state() const noexcept;
    [[nodiscard]] std::size_t remaining_frames() const noexcept;
    [[nodiscard]] std::optional<runtime::FrameSchedulerState>
    scheduler_state() const noexcept;
    [[nodiscard]] std::optional<simulation::SimulationState>
    simulation_state() const noexcept;
    // [game thread; no concurrent use] Returns an owned copy only when the explicit synthetic
    // locomotion option created its positioned diagnostic entity.
    [[nodiscard]] std::optional<simulation::Position3>
    debug_locomotion_position() const noexcept;

private:
    RunReplaySession(runtime::FrameScheduler&& scheduler,
        simulation::SimulationWorld&& simulation,
        runtime::RunCaptureReplaySession&& replay,
        std::optional<simulation::EntityId> debug_locomotion_entity) noexcept;
    void NormalizeInert() noexcept;

    std::optional<runtime::FrameScheduler> scheduler_;
    std::optional<simulation::SimulationWorld> simulation_;
    std::optional<runtime::RunCaptureReplaySession> replay_;
    std::optional<simulation::EntityId> debug_locomotion_entity_;
    RunReplaySessionState state_ = RunReplaySessionState::Inert;
};
} // namespace omega::app
