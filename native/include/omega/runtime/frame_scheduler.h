#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <string>

namespace omega::runtime
{
// Validation bounds for FrameSchedulerConfig. These are synthetic-shell rejection budgets that
// keep the accumulator arithmetic trivially overflow-free; they are engineering limits of this
// reimplementation shell, not measurements or claims about the retail engine.
inline constexpr std::chrono::nanoseconds kMinimumSimulationStep{
    std::chrono::microseconds{100}};
inline constexpr std::chrono::nanoseconds kMaximumSimulationStep{std::chrono::seconds{1}};
inline constexpr std::uint32_t kMaximumStepsPerFrame = 64;
inline constexpr std::chrono::nanoseconds kMaximumFrameDelta{std::chrono::seconds{4}};

namespace detail
{
// Internal arithmetic helper kept here so representation-limit behavior can be tested exactly
// without driving a scheduler through billions of synthetic overload frames.
[[nodiscard]] constexpr std::chrono::nanoseconds SaturatingAddNanoseconds(
    const std::chrono::nanoseconds left, const std::chrono::nanoseconds right) noexcept
{
    if (right > std::chrono::nanoseconds::zero() &&
        left > std::chrono::nanoseconds::max() - right)
        return std::chrono::nanoseconds::max();
    if (right < std::chrono::nanoseconds::zero() &&
        left < std::chrono::nanoseconds::min() - right)
        return std::chrono::nanoseconds::min();
    return left + right;
}
} // namespace detail

// Fixed-step accumulator configuration. Every field is required and validated; there is no
// meaningful default. In particular `simulation_step` is deliberately zero-initialized to an
// invalid value: per docs/02, SimulationWorld uses a measured fixed step and the retail tick
// rate remains evidence-driven. Any step the synthetic debug shell wires in is a documented
// synthetic-shell placeholder, never a retail-tick-rate claim.
struct FrameSchedulerConfig
{
    // Duration of one simulation step. Must lie in
    // [kMinimumSimulationStep, kMaximumSimulationStep].
    std::chrono::nanoseconds simulation_step{0};
    // Hard per-frame step budget. Must lie in [1, kMaximumStepsPerFrame]; accumulated time
    // beyond this budget is dropped so a slow frame cannot death-spiral.
    std::uint32_t max_steps_per_frame = 0;
    // Per-frame elapsed-time clamp. Must lie in [simulation_step, kMaximumFrameDelta].
    std::chrono::nanoseconds max_frame_delta{0};

    friend constexpr bool operator==(const FrameSchedulerConfig&,
        const FrameSchedulerConfig&) noexcept = default;
};

// Small owned scheduler snapshot. It contains no clock or borrowed storage and remains valid
// after the source scheduler advances, moves, or is destroyed.
struct FrameSchedulerState
{
    FrameSchedulerConfig config{};
    std::chrono::nanoseconds accumulated_remainder{0};
    std::uint64_t total_planned_steps = 0U;
    std::chrono::nanoseconds total_dropped_time{0};

    friend constexpr bool operator==(const FrameSchedulerState&,
        const FrameSchedulerState&) noexcept = default;
};

// One frame's plan. `simulation_steps` is how many fixed steps the caller must execute this
// frame; `interpolation_alpha` is the retained sub-step remainder divided by the step, always
// in [0, 1), for render-side interpolation between the previous and current simulation states.
struct FramePlan
{
    std::uint32_t simulation_steps = 0;
    double interpolation_alpha = 0.0;
    // True when this frame's elapsed input exceeded max_frame_delta and was clamped.
    bool clamped_delta = false;
    // True when the anti-death-spiral policy discarded whole steps beyond max_steps_per_frame.
    bool dropped_time = false;
};

// Deterministic fixed-step frame planner for the future OmegaApp game loop. The scheduler
// contains no clock: the caller measures elapsed wall time on a steady clock and feeds it in,
// so planning is pure integer-nanosecond arithmetic with zero floating-point drift, trivially
// testable, and replay-friendly for the M4 capture/replay milestone. `interpolation_alpha` is
// the only derived floating-point value and never feeds back into state. The type is
// single-threaded by contract: one game-thread owner, no internal synchronization.
class FrameScheduler final
{
public:
    // [any thread; reentrant] Validates the configuration against the explicit bounds above
    // and returns a scheduler with an empty accumulator. Rejects instead of adjusting.
    [[nodiscard]] static std::expected<FrameScheduler, std::string> Create(
        const FrameSchedulerConfig& config);

    // [game thread] Plans one frame from the elapsed wall time since the previous call.
    // Policy, in order:
    // - Negative elapsed is clamped to zero (documented defensive choice: callers use a
    //   steady clock, so negative input is a caller bug that must not corrupt state).
    // - Elapsed above max_frame_delta is clamped to it (clamped_delta = true).
    // - Whole steps are drained from the accumulator; if more than max_steps_per_frame are
    //   available, exactly max_steps_per_frame are planned and the excess whole-step time is
    //   dropped (dropped_time = true). The sub-step remainder is always retained so
    //   interpolation stays continuous and no time is lost on the normal path.
    [[nodiscard]] FramePlan BeginFrame(std::chrono::nanoseconds elapsed) noexcept;

    // [game thread] Sub-step remainder currently held; always in [0, simulation_step).
    [[nodiscard]] std::chrono::nanoseconds accumulated_remainder() const noexcept;

    // [game thread] Total steps planned across all frames since construction.
    [[nodiscard]] std::uint64_t total_planned_steps() const noexcept;

    // [game thread] Total whole-step time discarded by the anti-death-spiral drop policy.
    // Saturates at nanoseconds::max() if the diagnostic counter exhausts its representation.
    [[nodiscard]] std::chrono::nanoseconds total_dropped_time() const noexcept;

    // [game thread] The validated, immutable configuration.
    [[nodiscard]] const FrameSchedulerConfig& config() const noexcept;

    // [game thread] Returns one owned exact copy of the configuration and current accumulator
    // diagnostics. Later scheduler mutations do not change the returned value.
    [[nodiscard]] FrameSchedulerState Snapshot() const noexcept;

private:
    explicit FrameScheduler(const FrameSchedulerConfig& config) noexcept;

    FrameSchedulerConfig config_{};
    std::chrono::nanoseconds accumulator_{0};
    std::uint64_t total_planned_steps_ = 0;
    std::chrono::nanoseconds total_dropped_time_{0};
};
} // namespace omega::runtime
