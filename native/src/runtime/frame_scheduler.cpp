#include "omega/runtime/frame_scheduler.h"

namespace omega::runtime
{
std::expected<FrameScheduler, std::string> FrameScheduler::Create(
    const FrameSchedulerConfig& config)
{
    if (config.simulation_step < kMinimumSimulationStep)
        return std::unexpected(
            "simulation_step must be at least 100 microseconds; the retail tick rate is "
            "evidence-driven and must be supplied explicitly, never defaulted here");
    if (config.simulation_step > kMaximumSimulationStep)
        return std::unexpected("simulation_step must not exceed 1 second");
    if (config.max_steps_per_frame < 1U)
        return std::unexpected("max_steps_per_frame must be at least 1");
    if (config.max_steps_per_frame > kMaximumStepsPerFrame)
        return std::unexpected("max_steps_per_frame must not exceed the budget of 64");
    if (config.max_frame_delta < config.simulation_step)
        return std::unexpected("max_frame_delta must be at least one simulation_step");
    if (config.max_frame_delta > kMaximumFrameDelta)
        return std::unexpected("max_frame_delta must not exceed 4 seconds");
    return FrameScheduler(config);
}

FrameScheduler::FrameScheduler(const FrameSchedulerConfig& config) noexcept
    : config_(config)
{
}

FramePlan FrameScheduler::BeginFrame(const std::chrono::nanoseconds elapsed) noexcept
{
    FramePlan plan;

    // Defensive clamp for non-steady caller clocks; documented in the header contract.
    std::chrono::nanoseconds delta = elapsed;
    if (delta < std::chrono::nanoseconds::zero())
        delta = std::chrono::nanoseconds::zero();
    if (delta > config_.max_frame_delta)
    {
        delta = config_.max_frame_delta;
        plan.clamped_delta = true;
    }

    // The retained remainder is always below one step and delta is clamped to
    // max_frame_delta, so the accumulator stays far below the int64 nanosecond range.
    accumulator_ += delta;

    const std::int64_t step = config_.simulation_step.count();
    const std::int64_t available = accumulator_.count() / step;
    const std::int64_t budget = static_cast<std::int64_t>(config_.max_steps_per_frame);
    std::int64_t planned = available;
    if (available > budget)
    {
        planned = budget;
        plan.dropped_time = true;
        const std::chrono::nanoseconds dropped{(available - budget) * step};
        total_dropped_time_ = detail::SaturatingAddNanoseconds(total_dropped_time_, dropped);
    }

    // Whole steps leave the accumulator (executed or dropped); the sub-step remainder stays.
    accumulator_ = std::chrono::nanoseconds{accumulator_.count() % step};

    plan.simulation_steps = static_cast<std::uint32_t>(planned);
    total_planned_steps_ += static_cast<std::uint64_t>(planned);
    plan.interpolation_alpha =
        static_cast<double>(accumulator_.count()) / static_cast<double>(step);
    return plan;
}

std::chrono::nanoseconds FrameScheduler::accumulated_remainder() const noexcept
{
    return accumulator_;
}

std::uint64_t FrameScheduler::total_planned_steps() const noexcept
{
    return total_planned_steps_;
}

std::chrono::nanoseconds FrameScheduler::total_dropped_time() const noexcept
{
    return total_dropped_time_;
}

const FrameSchedulerConfig& FrameScheduler::config() const noexcept
{
    return config_;
}
} // namespace omega::runtime
