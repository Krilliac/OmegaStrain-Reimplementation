#include "omega/runtime/frame_scheduler.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>

namespace
{
using std::chrono::microseconds;
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using std::chrono::seconds;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] omega::runtime::FrameSchedulerConfig MakeConfig(
    const nanoseconds step = milliseconds{10},
    const std::uint32_t max_steps = 8,
    const nanoseconds max_delta = milliseconds{100})
{
    omega::runtime::FrameSchedulerConfig config;
    config.simulation_step = step;
    config.max_steps_per_frame = max_steps;
    config.max_frame_delta = max_delta;
    return config;
}
} // namespace

int FrameSchedulerFailureCount()
{
    using omega::runtime::FrameScheduler;

    // Configuration rejection matrix: every bound rejects instead of adjusting.
    Check(!FrameScheduler::Create(MakeConfig(nanoseconds{0})),
        "a zero simulation step is rejected, never defaulted");
    Check(!FrameScheduler::Create(MakeConfig(nanoseconds{-1})),
        "a negative simulation step is rejected");
    Check(!FrameScheduler::Create(MakeConfig(microseconds{100} - nanoseconds{1})),
        "a simulation step below the 100 microsecond floor is rejected");
    Check(!FrameScheduler::Create(
              MakeConfig(seconds{1} + nanoseconds{1}, 1, seconds{2})),
        "a simulation step above the 1 second ceiling is rejected");
    Check(!FrameScheduler::Create(MakeConfig(milliseconds{10}, 0)),
        "a zero step budget is rejected");
    Check(!FrameScheduler::Create(MakeConfig(milliseconds{10}, 65)),
        "a step budget above 64 is rejected");
    Check(!FrameScheduler::Create(
              MakeConfig(milliseconds{10}, 8, milliseconds{10} - nanoseconds{1})),
        "a frame-delta clamp below one simulation step is rejected");
    Check(!FrameScheduler::Create(
              MakeConfig(milliseconds{10}, 8, seconds{4} + nanoseconds{1})),
        "a frame-delta clamp above the 4 second ceiling is rejected");
    Check(FrameScheduler::Create(MakeConfig(microseconds{100}, 1, microseconds{100}))
              .has_value(),
        "the exact lower bounds compose into a valid configuration");
    Check(FrameScheduler::Create(MakeConfig(seconds{1}, 64, seconds{4})).has_value(),
        "the exact upper bounds compose into a valid configuration");

    // The cumulative dropped-time diagnostic saturates instead of overflowing its signed
    // nanosecond representation. Exercise the exact boundary directly rather than spending
    // billions of calls reaching it through BeginFrame.
    const nanoseconds maximum = nanoseconds::max();
    Check(omega::runtime::detail::SaturatingAddNanoseconds(
              maximum - nanoseconds{1}, nanoseconds{1}) == maximum,
        "dropped-time accumulation may land exactly on the representation maximum");
    Check(omega::runtime::detail::SaturatingAddNanoseconds(
              maximum - nanoseconds{1}, nanoseconds{2}) == maximum,
        "dropped-time accumulation saturates when it would exceed the maximum by one");

    auto scheduler = FrameScheduler::Create(MakeConfig());
    Check(scheduler.has_value(), "the reference configuration is accepted");
    if (!scheduler)
        return failures;
    Check(scheduler->accumulated_remainder() == nanoseconds{0} &&
              scheduler->total_planned_steps() == 0 &&
              scheduler->total_dropped_time() == nanoseconds{0},
        "a fresh scheduler starts with an empty accumulator and zero counters");
    Check(scheduler->config().simulation_step == milliseconds{10},
        "the validated configuration is preserved verbatim");

    // Uneven deltas around a 10 ms step: 3 ms, 25 ms, 2 ms.
    auto plan = scheduler->BeginFrame(milliseconds{3});
    Check(plan.simulation_steps == 0 && !plan.clamped_delta && !plan.dropped_time,
        "a sub-step delta plans zero steps and raises no flags");
    Check(plan.interpolation_alpha == 0.3 &&
              scheduler->accumulated_remainder() == milliseconds{3},
        "a 3 ms remainder of a 10 ms step yields alpha 0.3 exactly");
    plan = scheduler->BeginFrame(milliseconds{25});
    Check(plan.simulation_steps == 2 && plan.interpolation_alpha == 0.8 &&
              scheduler->accumulated_remainder() == milliseconds{8},
        "3 ms + 25 ms drains two whole steps and retains an 8 ms remainder");
    plan = scheduler->BeginFrame(milliseconds{2});
    Check(plan.simulation_steps == 1 && plan.interpolation_alpha == 0.0 &&
              scheduler->accumulated_remainder() == nanoseconds{0},
        "topping the remainder up to one exact step leaves alpha at exactly zero");
    Check(scheduler->total_planned_steps() == 3,
        "the planned-step counter matches the emitted steps");

    // Zero-elapsed frames are inert.
    plan = scheduler->BeginFrame(milliseconds{3});
    for (int frame = 0; frame < 3; ++frame)
        plan = scheduler->BeginFrame(nanoseconds{0});
    Check(plan.simulation_steps == 0 && plan.interpolation_alpha == 0.3 &&
              !plan.clamped_delta && !plan.dropped_time &&
              scheduler->accumulated_remainder() == milliseconds{3},
        "zero-elapsed frames change neither the remainder nor the alpha");

    // Negative elapsed is clamped to zero without corrupting state.
    plan = scheduler->BeginFrame(milliseconds{-5});
    Check(plan.simulation_steps == 0 && plan.interpolation_alpha == 0.3 &&
              !plan.clamped_delta && !plan.dropped_time &&
              scheduler->accumulated_remainder() == milliseconds{3},
        "a negative delta is treated as zero elapsed time");
    plan = scheduler->BeginFrame(nanoseconds::min());
    Check(plan.simulation_steps == 0 &&
              scheduler->accumulated_remainder() == milliseconds{3},
        "the most negative representable delta is also treated as zero");

    // Alpha stays strictly below one at the near-step boundary.
    plan = scheduler->BeginFrame(milliseconds{7} - nanoseconds{1});
    Check(plan.simulation_steps == 0 && plan.interpolation_alpha < 1.0 &&
              plan.interpolation_alpha > 0.99 &&
              scheduler->accumulated_remainder() == milliseconds{10} - nanoseconds{1},
        "one nanosecond below a full step keeps alpha strictly inside [0, 1)");
    plan = scheduler->BeginFrame(nanoseconds{1});
    Check(plan.simulation_steps == 1 && plan.interpolation_alpha == 0.0,
        "the final nanosecond completes exactly one step");

    // Clamp plus drop: 150 ms clamps to 100 ms; ten available steps exceed the budget of
    // eight, so two whole steps (20 ms) are dropped and the remainder stays sub-step.
    plan = scheduler->BeginFrame(milliseconds{150});
    Check(plan.clamped_delta && plan.dropped_time && plan.simulation_steps == 8,
        "an oversized delta reports both the clamp and the drop");
    Check(scheduler->accumulated_remainder() == nanoseconds{0} &&
              plan.interpolation_alpha == 0.0,
        "the clamp-and-drop path leaves only the sub-step remainder");
    Check(scheduler->total_dropped_time() == milliseconds{20},
        "the drop counter records exactly the discarded whole-step time");

    // Drop without clamp: 95 ms fits under the clamp but exceeds the step budget.
    plan = scheduler->BeginFrame(milliseconds{95});
    Check(!plan.clamped_delta && plan.dropped_time && plan.simulation_steps == 8 &&
              plan.interpolation_alpha == 0.5 &&
              scheduler->accumulated_remainder() == milliseconds{5},
        "an in-clamp overload drops one step and keeps the 5 ms remainder");
    Check(scheduler->total_dropped_time() == milliseconds{30},
        "dropped time accumulates across frames");

    // Clamp without drop: a wider budget absorbs the full clamped delta.
    auto wide = FrameScheduler::Create(MakeConfig(milliseconds{10}, 12));
    Check(wide.has_value(), "the wide-budget configuration is accepted");
    if (wide)
    {
        plan = wide->BeginFrame(milliseconds{150});
        Check(plan.clamped_delta && !plan.dropped_time && plan.simulation_steps == 10 &&
                  wide->accumulated_remainder() == nanoseconds{0},
            "a clamped delta inside the step budget drops nothing");
        plan = wide->BeginFrame(nanoseconds::max());
        Check(plan.clamped_delta && !plan.dropped_time && plan.simulation_steps == 10 &&
                  wide->total_planned_steps() == 20,
            "the largest representable delta clamps safely without overflow");
    }

    // Long-run drift: alternating odd deltas that pair into exact steps must leave zero
    // accumulated error after thousands of frames.
    auto drift = FrameScheduler::Create(MakeConfig(milliseconds{10}, 64));
    Check(drift.has_value(), "the drift configuration is accepted");
    if (drift)
    {
        std::uint64_t emitted = 0;
        bool any_flag = false;
        for (int frame = 0; frame < 4000; ++frame)
        {
            const auto step_plan =
                drift->BeginFrame((frame % 2) == 0 ? milliseconds{7} : milliseconds{13});
            emitted += step_plan.simulation_steps;
            any_flag = any_flag || step_plan.clamped_delta || step_plan.dropped_time;
        }
        Check(emitted == 4000 && drift->total_planned_steps() == 4000,
            "40 seconds of paired odd deltas emit exactly 4000 steps");
        Check(!any_flag && drift->accumulated_remainder() == nanoseconds{0} &&
                  drift->total_dropped_time() == nanoseconds{0},
            "the paired-delta run ends with zero remainder and no clamp or drop");

        // A prime nanosecond delta proves the integer accumulator is exact, not rounded.
        std::uint64_t prime_steps = 0;
        for (int frame = 0; frame < 1000; ++frame)
            prime_steps += drift->BeginFrame(nanoseconds{999983}).simulation_steps;
        Check(prime_steps == 99 &&
                  drift->accumulated_remainder() == nanoseconds{9983000},
            "1000 prime deltas leave the exact integer remainder with zero drift");
    }
    return failures;
}
