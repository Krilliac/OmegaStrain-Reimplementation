#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/scheduler_elapsed_trace.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
using std::chrono::milliseconds;
using std::chrono::nanoseconds;
using omega::runtime::FramePlan;
using omega::runtime::FrameScheduler;
using omega::runtime::FrameSchedulerConfig;
using omega::runtime::SchedulerElapsedTrace;
using omega::runtime::SchedulerElapsedTraceConfig;
using omega::runtime::SchedulerElapsedTraceError;
using omega::runtime::SchedulerElapsedTraceErrorCode;
using omega::runtime::SchedulerElapsedFrameState;
using omega::runtime::SchedulerElapsedTraceRecorder;

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
bool CheckError(const std::expected<Value, SchedulerElapsedTraceError>& result,
    const SchedulerElapsedTraceErrorCode code, const std::string_view message)
{
    const bool matches = !result && result.error().code == code &&
                         result.error().message ==
                             omega::runtime::SchedulerElapsedTraceErrorMessage(code);
    Check(matches, message);
    return matches;
}

[[nodiscard]] SchedulerElapsedTrace TakeTrace(
    std::expected<SchedulerElapsedTrace, SchedulerElapsedTraceError>& finished,
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
    static_assert(std::is_trivially_copyable_v<SchedulerElapsedFrameState>);
    static_assert(std::is_standard_layout_v<SchedulerElapsedFrameState>);
    static_assert(!std::is_copy_constructible_v<SchedulerElapsedTraceRecorder>);
    static_assert(!std::is_copy_assignable_v<SchedulerElapsedTraceRecorder>);
    static_assert(std::is_nothrow_move_constructible_v<SchedulerElapsedTraceRecorder>);
    static_assert(!std::is_move_assignable_v<SchedulerElapsedTraceRecorder>);
    static_assert(!std::is_copy_constructible_v<SchedulerElapsedTrace>);
    static_assert(!std::is_copy_assignable_v<SchedulerElapsedTrace>);
    static_assert(std::is_nothrow_move_constructible_v<SchedulerElapsedTrace>);
    static_assert(!std::is_move_assignable_v<SchedulerElapsedTrace>);
    static_assert(noexcept(std::declval<SchedulerElapsedTraceRecorder&>().Append(
        0U, nanoseconds{0})));
    static_assert(noexcept(std::declval<SchedulerElapsedTraceRecorder&&>().Finish()));
    static_assert(noexcept(std::declval<const SchedulerElapsedTrace&>().FrameAt(0U)));
    static_assert(sizeof(std::int64_t) == 8U);

    Check(omega::runtime::kMaximumSchedulerElapsedTraceFrames == 65'536U,
        "the scheduler-elapsed trace frame hard maximum is fixed");
    Check(omega::runtime::kMaximumSchedulerElapsedTraceFrames * sizeof(std::int64_t) ==
              512U * 1'024U,
        "the hard-maximum logical record payload is exactly 512 KiB");
    Check(SchedulerElapsedTraceConfig{}.maximum_frames == 0U &&
              SchedulerElapsedTraceConfig{}.first_frame_index == 0U,
        "default scheduler-elapsed trace configuration is deliberately invalid");
    Check(SchedulerElapsedFrameState{} == SchedulerElapsedFrameState{},
        "the owned scheduler-elapsed query state defaults to zero");

    struct ErrorContract
    {
        SchedulerElapsedTraceErrorCode code;
        std::string_view name;
        std::string_view message;
    };
    constexpr std::array contracts{
        ErrorContract{SchedulerElapsedTraceErrorCode::InvalidConfiguration,
            "invalid-configuration", "scheduler elapsed trace configuration is invalid"},
        ErrorContract{SchedulerElapsedTraceErrorCode::AllocationFailed,
            "allocation-failed", "scheduler elapsed trace allocation failed"},
        ErrorContract{SchedulerElapsedTraceErrorCode::InvalidRecorderState,
            "invalid-recorder-state", "scheduler elapsed trace recorder is not open"},
        ErrorContract{SchedulerElapsedTraceErrorCode::CapacityExceeded,
            "capacity-exceeded", "scheduler elapsed trace frame capacity is exceeded"},
        ErrorContract{SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
            "frame-discontinuity",
            "scheduler elapsed trace frame index is not contiguous"},
    };
    for (const ErrorContract& contract : contracts)
    {
        Check(omega::runtime::SchedulerElapsedTraceErrorCodeName(contract.code) ==
                  contract.name,
            "every scheduler-elapsed trace error has its fixed code name");
        Check(omega::runtime::SchedulerElapsedTraceErrorMessage(contract.code) ==
                  contract.message,
            "every scheduler-elapsed trace error has its fixed category message");
        const SchedulerElapsedTraceError error{
            .code = contract.code,
            .message =
                omega::runtime::SchedulerElapsedTraceErrorMessage(contract.code),
        };
        Check(error.message == contract.message,
            "a scheduler-elapsed trace error carries only fixed category text");
    }

    constexpr auto unknown = static_cast<SchedulerElapsedTraceErrorCode>(255);
    Check(omega::runtime::SchedulerElapsedTraceErrorCodeName(unknown) == "unknown" &&
              omega::runtime::SchedulerElapsedTraceErrorMessage(unknown) ==
                  "scheduler elapsed trace error is unknown",
        "unknown scheduler-elapsed error values fail closed to fixed text");
}

void CheckCreationValidation()
{
    CheckError(SchedulerElapsedTraceRecorder::Create(
                   SchedulerElapsedTraceConfig{.maximum_frames = 0U}),
        SchedulerElapsedTraceErrorCode::InvalidConfiguration,
        "zero frame capacity is rejected before allocation");
    CheckError(SchedulerElapsedTraceRecorder::Create(SchedulerElapsedTraceConfig{
                   .maximum_frames =
                       omega::runtime::kMaximumSchedulerElapsedTraceFrames + 1U,
               }),
        SchedulerElapsedTraceErrorCode::InvalidConfiguration,
        "frame capacity above the hard maximum is rejected");
    CheckError(SchedulerElapsedTraceRecorder::Create(SchedulerElapsedTraceConfig{
                   .maximum_frames = 2U,
                   .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
               }),
        SchedulerElapsedTraceErrorCode::InvalidConfiguration,
        "the configured contiguous frame range cannot overflow uint64");

    auto terminal_range = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = 2U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max() - 1U,
        });
    Check(terminal_range && terminal_range->first_frame_index() ==
                                std::numeric_limits<std::uint64_t>::max() - 1U &&
              terminal_range->maximum_frames() == 2U,
        "a contiguous range ending exactly at uint64 max is valid");

    auto final_index = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = 1U,
            .first_frame_index = std::numeric_limits<std::uint64_t>::max(),
        });
    Check(final_index && final_index->first_frame_index() ==
                             std::numeric_limits<std::uint64_t>::max() &&
              final_index->maximum_frames() == 1U && final_index->frame_count() == 0U,
        "one frame beginning at the final uint64 index is valid");
    if (final_index)
    {
        auto trace = std::move(*final_index).Finish();
        Check(trace && trace->first_frame_index() ==
                           std::numeric_limits<std::uint64_t>::max() &&
                  trace->maximum_frames() == 1U && trace->frame_count() == 0U,
            "an empty final-index trace retains exact validated metadata");
    }

    auto exact_maximum = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = omega::runtime::kMaximumSchedulerElapsedTraceFrames,
        });
    Check(exact_maximum && exact_maximum->maximum_frames() ==
                               omega::runtime::kMaximumSchedulerElapsedTraceFrames,
        "the exact frame hard maximum preallocates successfully");
    if (exact_maximum)
    {
        auto trace = std::move(*exact_maximum).Finish();
        Check(trace && trace->frame_count() == 0U,
            "an exact-maximum empty recorder finishes without another allocation");
    }
}

void CheckExactElapsedPreservation()
{
    constexpr std::array elapsed_values{
        nanoseconds::min(), nanoseconds{-1}, nanoseconds{0}, nanoseconds{1},
        nanoseconds::max(),
    };
    auto created = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = elapsed_values.size(),
            .first_frame_index = 90U,
        });
    Check(created.has_value(), "the exact-value fixture recorder is created");
    if (!created)
        return;
    SchedulerElapsedTraceRecorder recorder = std::move(*created);

    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        const nanoseconds original = elapsed_values[index];
        Check(recorder.Append(90U + static_cast<std::uint64_t>(index), original) &&
                  original == elapsed_values[index] &&
                  recorder.frame_count() == index + 1U,
            "each signed nanosecond boundary appends verbatim without caller mutation");
    }

    auto finished = std::move(recorder).Finish();
    Check(finished && finished->first_frame_index() == 90U &&
              finished->maximum_frames() == elapsed_values.size() &&
              finished->frame_count() == elapsed_values.size(),
        "the full exact-value trace retains its complete metadata");
    if (!finished)
        return;

    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        Check(finished->FrameAt(index) == SchedulerElapsedFrameState{
                                              .frame_index =
                                                  90U + static_cast<std::uint64_t>(index),
                                              .elapsed = elapsed_values[index],
                                          },
            "negative, zero, positive, minimum, and maximum elapsed values round-trip");
    }
    Check(!finished->FrameAt(elapsed_values.size()) &&
              !finished->FrameAt(std::numeric_limits<std::size_t>::max()),
        "queries outside the active frame range return no value");
}

void CheckFailurePriorityAndAtomicity()
{
    auto created = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{.maximum_frames = 2U, .first_frame_index = 7U});
    Check(created.has_value(), "the failure-priority fixture recorder is created");
    if (!created)
        return;
    SchedulerElapsedTraceRecorder recorder = std::move(*created);

    CheckError(recorder.Append(8U, nanoseconds::max()),
        SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
        "frame continuity is checked before accepting any elapsed value");
    Check(recorder.frame_count() == 0U,
        "a discontinuity leaves an empty recorder unchanged");
    Check(recorder.Append(7U, nanoseconds::min()) && recorder.frame_count() == 1U,
        "the expected first frame remains appendable after failure");
    CheckError(recorder.Append(9U, nanoseconds{-7}),
        SchedulerElapsedTraceErrorCode::FrameDiscontinuity,
        "a later discontinuity is rejected without consuming capacity");
    Check(recorder.frame_count() == 1U,
        "a later discontinuity preserves the prior frame and next index");
    Check(recorder.Append(8U, nanoseconds{0}) && recorder.frame_count() == 2U,
        "the expected second frame remains appendable after failure");
    CheckError(recorder.Append(100U, nanoseconds::max()),
        SchedulerElapsedTraceErrorCode::CapacityExceeded,
        "capacity exhaustion has priority over frame discontinuity");
    Check(recorder.frame_count() == 2U,
        "capacity failure leaves a full recorder unchanged");

    auto finished = std::move(recorder).Finish();
    Check(finished &&
              finished->FrameAt(0U) == SchedulerElapsedFrameState{
                                               .frame_index = 7U,
                                               .elapsed = nanoseconds::min(),
                                           } &&
              finished->FrameAt(1U) == SchedulerElapsedFrameState{
                                               .frame_index = 8U,
                                               .elapsed = nanoseconds{0},
                                           },
        "successful frames survive intervening failures in exact contiguous order");
    CheckError(recorder.Append(0U, nanoseconds{0}),
        SchedulerElapsedTraceErrorCode::InvalidRecorderState,
        "finished state has priority over all append validation");
    auto finished_again = std::move(recorder).Finish();
    CheckError(finished_again, SchedulerElapsedTraceErrorCode::InvalidRecorderState,
        "a recorder cannot finish twice");
    Check(recorder.first_frame_index() == 0U && recorder.maximum_frames() == 0U &&
              recorder.frame_count() == 0U,
        "a finished recorder is observably inert");
}

void CheckEmptyAndMoveLifecycle()
{
    auto empty_created = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{.maximum_frames = 3U, .first_frame_index = 17U});
    Check(empty_created.has_value(), "an empty-finish recorder is created");
    if (!empty_created)
        return;
    SchedulerElapsedTraceRecorder empty_recorder = std::move(*empty_created);
    auto empty_finished = std::move(empty_recorder).Finish();
    Check(empty_finished && empty_finished->first_frame_index() == 17U &&
              empty_finished->maximum_frames() == 3U &&
              empty_finished->frame_count() == 0U && !empty_finished->FrameAt(0U),
        "an open zero-frame recorder publishes an immutable empty trace");

    auto source_created = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{.maximum_frames = 2U, .first_frame_index = 30U});
    Check(source_created && source_created->Append(30U, nanoseconds{-13}),
        "the recorder move fixture captures one frame");
    if (!source_created)
        return;

    SchedulerElapsedTraceRecorder source = std::move(*source_created);
    SchedulerElapsedTraceRecorder destination = std::move(source);
    Check(source.first_frame_index() == 0U && source.maximum_frames() == 0U &&
              source.frame_count() == 0U,
        "move construction leaves the recorder source inert");
    CheckError(source.Append(0U, nanoseconds{0}),
        SchedulerElapsedTraceErrorCode::InvalidRecorderState,
        "a moved-from recorder rejects append");
    auto source_finish = std::move(source).Finish();
    CheckError(source_finish, SchedulerElapsedTraceErrorCode::InvalidRecorderState,
        "a moved-from recorder rejects finish");
    Check(destination.first_frame_index() == 30U &&
              destination.maximum_frames() == 2U && destination.frame_count() == 1U,
        "recorder move construction transfers complete logical ownership");

    std::optional<SchedulerElapsedFrameState> owned_frame;
    {
        auto destination_finish = std::move(destination).Finish();
        SchedulerElapsedTrace trace =
            TakeTrace(destination_finish, "the moved recorder finishes");
        owned_frame = trace.FrameAt(0U);
        SchedulerElapsedTrace moved_trace = std::move(trace);
        Check(trace.first_frame_index() == 0U && trace.maximum_frames() == 0U &&
                  trace.frame_count() == 0U && !trace.FrameAt(0U),
            "move construction leaves the trace source inert");
        Check(moved_trace.first_frame_index() == 30U &&
                  moved_trace.maximum_frames() == 2U && moved_trace.frame_count() == 1U &&
                  moved_trace.FrameAt(0U) == owned_frame,
            "trace move construction transfers all immutable query state");
    }
    Check(owned_frame == SchedulerElapsedFrameState{
                             .frame_index = 30U,
                             .elapsed = nanoseconds{-13},
                         },
        "the owned query value survives destruction of the final owning trace");
}

[[nodiscard]] SchedulerElapsedTrace BuildDeterministicTrace()
{
    constexpr std::array elapsed_values{
        nanoseconds::min(), nanoseconds{-23}, nanoseconds{0}, nanoseconds{17},
        nanoseconds{milliseconds{150}}, nanoseconds::max(),
    };
    auto created = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = elapsed_values.size(),
            .first_frame_index = 600U,
        });
    if (!created)
        std::abort();
    SchedulerElapsedTraceRecorder recorder = std::move(*created);
    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        if (!recorder.Append(
                600U + static_cast<std::uint64_t>(index), elapsed_values[index]))
        {
            std::abort();
        }
    }
    auto finished = std::move(recorder).Finish();
    if (!finished)
        std::abort();
    return std::move(*finished);
}

void CheckTwoRunDeterminism()
{
    SchedulerElapsedTrace first = BuildDeterministicTrace();
    SchedulerElapsedTrace second = BuildDeterministicTrace();
    bool identical = first.first_frame_index() == second.first_frame_index() &&
                     first.maximum_frames() == second.maximum_frames() &&
                     first.frame_count() == second.frame_count();
    for (std::size_t index = 0U; identical && index < first.frame_count(); ++index)
        identical = first.FrameAt(index) == second.FrameAt(index);
    Check(identical,
        "two independent recorder runs produce identical immutable elapsed trace queries");
}

[[nodiscard]] bool SamePlan(const FramePlan& left, const FramePlan& right) noexcept
{
    return left.simulation_steps == right.simulation_steps &&
           left.interpolation_alpha == right.interpolation_alpha &&
           left.clamped_delta == right.clamped_delta &&
           left.dropped_time == right.dropped_time;
}

void CheckFrameSchedulerEquivalence()
{
    constexpr std::array elapsed_values{
        nanoseconds::min(), nanoseconds{0}, nanoseconds{milliseconds{3}},
        nanoseconds{milliseconds{25}}, nanoseconds{milliseconds{150}},
        nanoseconds{milliseconds{95}}, nanoseconds::max(), nanoseconds{1},
    };
    auto created = SchedulerElapsedTraceRecorder::Create(
        SchedulerElapsedTraceConfig{
            .maximum_frames = elapsed_values.size(),
            .first_frame_index = 400U,
        });
    Check(created.has_value(), "the scheduler-equivalence trace recorder is created");
    if (!created)
        return;
    SchedulerElapsedTraceRecorder recorder = std::move(*created);
    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        if (!recorder.Append(
                400U + static_cast<std::uint64_t>(index), elapsed_values[index]))
        {
            Check(false, "the scheduler-equivalence elapsed sequence is captured");
            return;
        }
    }
    auto finished = std::move(recorder).Finish();
    SchedulerElapsedTrace trace =
        TakeTrace(finished, "the scheduler-equivalence trace finishes");

    const FrameSchedulerConfig config{
        .simulation_step = milliseconds{10},
        .max_steps_per_frame = 8U,
        .max_frame_delta = milliseconds{100},
    };
    auto original_scheduler = FrameScheduler::Create(config);
    auto traced_scheduler = FrameScheduler::Create(config);
    Check(original_scheduler && traced_scheduler,
        "two schedulers accept the identical equivalence configuration");
    if (!original_scheduler || !traced_scheduler)
        return;

    bool equivalent = true;
    for (std::size_t index = 0U; index < elapsed_values.size(); ++index)
    {
        const auto traced_frame = trace.FrameAt(index);
        if (!traced_frame)
        {
            Check(false, "every captured scheduler frame remains queryable");
            return;
        }
        const FramePlan original_plan =
            original_scheduler->BeginFrame(elapsed_values[index]);
        const FramePlan traced_plan = traced_scheduler->BeginFrame(traced_frame->elapsed);
        equivalent = equivalent &&
                     traced_frame->frame_index ==
                         400U + static_cast<std::uint64_t>(index) &&
                     traced_frame->elapsed == elapsed_values[index] &&
                     SamePlan(original_plan, traced_plan) &&
                     original_scheduler->accumulated_remainder() ==
                         traced_scheduler->accumulated_remainder() &&
                     original_scheduler->total_planned_steps() ==
                         traced_scheduler->total_planned_steps() &&
                     original_scheduler->total_dropped_time() ==
                         traced_scheduler->total_dropped_time();
    }
    Check(equivalent,
        "direct and trace-retrieved elapsed values yield identical per-frame plans and state");
    Check(original_scheduler->total_planned_steps() ==
                  traced_scheduler->total_planned_steps() &&
              original_scheduler->accumulated_remainder() ==
                  traced_scheduler->accumulated_remainder() &&
              original_scheduler->total_dropped_time() ==
                  traced_scheduler->total_dropped_time(),
        "scheduler totals remain identical after the complete elapsed sequence");
}
} // namespace

int main()
{
    CheckContract();
    CheckCreationValidation();
    CheckExactElapsedPreservation();
    CheckFailurePriorityAndAtomicity();
    CheckEmptyAndMoveLifecycle();
    CheckTwoRunDeterminism();
    CheckFrameSchedulerEquivalence();

    if (failures == 0)
        std::cout << "omega_scheduler_elapsed_trace_tests: passed\n";
    return failures == 0 ? 0 : 1;
}
