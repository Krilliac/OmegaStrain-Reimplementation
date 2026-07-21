#include "opening_movie_audio_clock.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace {
using omega::app::OpeningMovieAudioClockError;
using omega::app::OpeningMovieAudioClockObservation;
using omega::app::OpeningMovieAudioClockResult;
using omega::app::OpeningMovieAudioClockStartSignals;
using omega::app::OpeningMovieAudioClockState;
using omega::app::OpeningMovieAudioClockStep;

[[nodiscard]] bool
HasError(const OpeningMovieAudioClockResult &result,
         const OpeningMovieAudioClockError expected) noexcept {
  return !result && result.error() == expected;
}
} // namespace

int main() {
  int failures = 0;
  const auto Check = [&failures](const bool condition,
                                 const std::string_view message) {
    if (!condition) {
      std::cerr << "FAILED: " << message << '\n';
      ++failures;
    }
  };

  static_assert(omega::app::kOpeningMovieAudioClockRateHz == 48'000U);
  static_assert(omega::app::kOpeningMovieAudioClockNanosecondsPerSecond ==
                1'000'000'000U);
  static_assert(
      std::is_trivially_copyable_v<OpeningMovieAudioClockObservation>);
  static_assert(std::is_standard_layout_v<OpeningMovieAudioClockObservation>);
  static_assert(std::is_trivially_copyable_v<OpeningMovieAudioClockState>);
  static_assert(std::is_standard_layout_v<OpeningMovieAudioClockState>);
  static_assert(
      std::is_trivially_copyable_v<OpeningMovieAudioClockStartSignals>);
  static_assert(std::is_standard_layout_v<OpeningMovieAudioClockStartSignals>);
  static_assert(std::is_trivially_copyable_v<OpeningMovieAudioClockStep>);
  static_assert(std::is_standard_layout_v<OpeningMovieAudioClockStep>);
  static_assert(noexcept(omega::app::StartOpeningMovieAudioClock({}, {})));
  static_assert(noexcept(omega::app::AdvanceOpeningMovieAudioClock({}, {})));
  static_assert(noexcept(omega::app::OpeningMovieAudioClockErrorMessage(
      OpeningMovieAudioClockError::ConversionOverflow)));

  constexpr OpeningMovieAudioClockResult compile_time_start =
      omega::app::StartOpeningMovieAudioClock(
          {},
          OpeningMovieAudioClockStartSignals{
              .video_frame_available = true,
              .pcm_queue_accepted = true,
              .before_queue = {.session_generation = 4U,
                               .timeline_frames = 90U},
              .after_queue = {.session_generation = 5U, .timeline_frames = 91U},
          });
  static_assert(compile_time_start.has_value());
  static_assert(compile_time_start->state.started);
  static_assert(compile_time_start->state.baseline_frames == 90U);
  constexpr OpeningMovieAudioClockResult compile_time_advance =
      omega::app::AdvanceOpeningMovieAudioClock(
          compile_time_start->state, OpeningMovieAudioClockObservation{
                                         .session_generation = 5U,
                                         .timeline_frames = 91U,
                                     });
  static_assert(compile_time_advance.has_value());
  static_assert(compile_time_advance->presentation_elapsed ==
                std::chrono::nanoseconds{20'833});

  const OpeningMovieAudioClockState arbitrary_unstarted{
      .started = false,
      .session_generation = 17U,
      .baseline_frames = 31U,
      .applied_frames = 29U,
      .nanosecond_remainder = 47'999U,
  };
  const OpeningMovieAudioClockResult unstarted =
      omega::app::AdvanceOpeningMovieAudioClock(
          arbitrary_unstarted,
          OpeningMovieAudioClockObservation{
              .session_generation = std::numeric_limits<std::uint64_t>::max(),
              .timeline_frames = 0U,
          });
  Check(unstarted &&
            *unstarted ==
                OpeningMovieAudioClockStep{
                    .state = arbitrary_unstarted,
                    .presentation_elapsed = std::chrono::nanoseconds::zero(),
                },
        "an unstarted clock ignores arbitrary observations without changing "
        "state");

  const OpeningMovieAudioClockStartSignals invalid_snapshots{
      .video_frame_available = false,
      .pcm_queue_accepted = false,
      .before_queue = {.session_generation = 10U, .timeline_frames = 20U},
      .after_queue = {.session_generation = 10U, .timeline_frames = 19U},
  };
  const OpeningMovieAudioClockResult no_signals =
      omega::app::StartOpeningMovieAudioClock(arbitrary_unstarted,
                                              invalid_snapshots);
  OpeningMovieAudioClockStartSignals frame_only_signals = invalid_snapshots;
  frame_only_signals.video_frame_available = true;
  const OpeningMovieAudioClockResult frame_only =
      omega::app::StartOpeningMovieAudioClock(arbitrary_unstarted,
                                              frame_only_signals);
  Check(no_signals && frame_only &&
            *no_signals ==
                OpeningMovieAudioClockStep{
                    .state = arbitrary_unstarted,
                    .presentation_elapsed = std::chrono::nanoseconds::zero(),
                } &&
            *frame_only ==
                OpeningMovieAudioClockStep{
                    .state = arbitrary_unstarted,
                    .presentation_elapsed = std::chrono::nanoseconds::zero(),
                },
        "no accepted PCM queue leaves the clock unchanged regardless of frame "
        "availability");

  OpeningMovieAudioClockStartSignals queue_only_signals = invalid_snapshots;
  queue_only_signals.pcm_queue_accepted = true;
  const OpeningMovieAudioClockResult queue_only =
      omega::app::StartOpeningMovieAudioClock(arbitrary_unstarted,
                                              queue_only_signals);
  Check(
      HasError(queue_only, OpeningMovieAudioClockError::QueueBeforeFirstFrame),
      "an accepted queue before a video frame is rejected before snapshot "
      "validation");

  const OpeningMovieAudioClockStartSignals valid_start_signals{
      .video_frame_available = true,
      .pcm_queue_accepted = true,
      .before_queue = {.session_generation = 7U, .timeline_frames = 1'000U},
      .after_queue = {.session_generation = 8U, .timeline_frames = 1'012U},
  };
  const OpeningMovieAudioClockResult started =
      omega::app::StartOpeningMovieAudioClock({}, valid_start_signals);
  Check(started &&
            *started ==
                OpeningMovieAudioClockStep{
                    .state =
                        OpeningMovieAudioClockState{
                            .started = true,
                            .session_generation = 8U,
                            .baseline_frames = 1'000U,
                            .applied_frames = 0U,
                            .nanosecond_remainder = 0U,
                        },
                    .presentation_elapsed = std::chrono::nanoseconds::zero(),
                },
        "the first accepted queue captures its prequeue baseline and postqueue "
        "generation");
  if (started) {
    const OpeningMovieAudioClockResult after_queue_demand =
        omega::app::AdvanceOpeningMovieAudioClock(
            started->state, OpeningMovieAudioClockObservation{
                                .session_generation = 8U,
                                .timeline_frames = 1'012U,
                            });
    Check(after_queue_demand &&
              after_queue_demand->presentation_elapsed ==
                  std::chrono::nanoseconds{250'000} &&
              after_queue_demand->state.applied_frames == 12U,
          "callback demand concurrent with the first queue is retained on the "
          "next advance");

    const OpeningMovieAudioClockState started_before_error = started->state;
    const OpeningMovieAudioClockResult repeated =
        omega::app::StartOpeningMovieAudioClock(started_before_error,
                                                valid_start_signals);
    Check(HasError(repeated, OpeningMovieAudioClockError::AlreadyStarted) &&
              started_before_error == started->state,
          "a repeated start fails without mutating the caller's state");
  }

  OpeningMovieAudioClockStartSignals unchanged_generation = valid_start_signals;
  unchanged_generation.after_queue.session_generation =
      unchanged_generation.before_queue.session_generation;
  const OpeningMovieAudioClockState default_before_unchanged{};
  const OpeningMovieAudioClockResult unchanged_generation_result =
      omega::app::StartOpeningMovieAudioClock(default_before_unchanged,
                                              unchanged_generation);
  Check(HasError(unchanged_generation_result,
                 OpeningMovieAudioClockError::StartGenerationDidNotAdvance) &&
            default_before_unchanged == OpeningMovieAudioClockState{},
        "an unchanged start generation fails without caller-state mutation");

  OpeningMovieAudioClockStartSignals jumped_generation = valid_start_signals;
  jumped_generation.after_queue.session_generation =
      jumped_generation.before_queue.session_generation + 2U;
  const OpeningMovieAudioClockResult jumped_generation_result =
      omega::app::StartOpeningMovieAudioClock({}, jumped_generation);
  Check(HasError(jumped_generation_result,
                 OpeningMovieAudioClockError::StartGenerationDidNotAdvance),
        "a start generation jump larger than one is rejected");

  const OpeningMovieAudioClockResult wrapped_generation =
      omega::app::StartOpeningMovieAudioClock(
          {},
          OpeningMovieAudioClockStartSignals{
              .video_frame_available = true,
              .pcm_queue_accepted = true,
              .before_queue = {.session_generation =
                                   std::numeric_limits<std::uint64_t>::max(),
                               .timeline_frames = 400U},
              .after_queue = {.session_generation = 0U,
                              .timeline_frames = 400U},
          });
  Check(wrapped_generation && wrapped_generation->state.started &&
            wrapped_generation->state.session_generation == 0U,
        "the defined unsigned generation wrap from max to zero advances "
        "exactly once");

  OpeningMovieAudioClockStartSignals backward_start = valid_start_signals;
  backward_start.after_queue.timeline_frames =
      backward_start.before_queue.timeline_frames - 1U;
  const OpeningMovieAudioClockResult backward_start_result =
      omega::app::StartOpeningMovieAudioClock({}, backward_start);
  Check(HasError(backward_start_result,
                 OpeningMovieAudioClockError::StartTimelineMovedBackward),
        "the timeline cannot move backward across the first queue operation");

  const OpeningMovieAudioClockState running{
      .started = true,
      .session_generation = 3U,
      .baseline_frames = 1'000U,
      .applied_frames = 10U,
      .nanosecond_remainder = 0U,
  };
  const OpeningMovieAudioClockState running_before_errors = running;
  const OpeningMovieAudioClockResult changed_generation =
      omega::app::AdvanceOpeningMovieAudioClock(
          running, OpeningMovieAudioClockObservation{
                       .session_generation = 4U,
                       .timeline_frames = 999U,
                   });
  Check(HasError(changed_generation,
                 OpeningMovieAudioClockError::SessionGenerationChanged) &&
            running == running_before_errors,
        "generation mismatch has priority over timeline faults and preserves "
        "caller state");

  const OpeningMovieAudioClockResult below_baseline =
      omega::app::AdvanceOpeningMovieAudioClock(
          running, OpeningMovieAudioClockObservation{
                       .session_generation = 3U,
                       .timeline_frames = 999U,
                   });
  const OpeningMovieAudioClockResult moved_backward =
      omega::app::AdvanceOpeningMovieAudioClock(
          running, OpeningMovieAudioClockObservation{
                       .session_generation = 3U,
                       .timeline_frames = 1'009U,
                   });
  const OpeningMovieAudioClockResult unchanged_timeline =
      omega::app::AdvanceOpeningMovieAudioClock(
          running, OpeningMovieAudioClockObservation{
                       .session_generation = 3U,
                       .timeline_frames = 1'010U,
                   });
  Check(HasError(below_baseline,
                 OpeningMovieAudioClockError::TimelineBeforeBaseline),
        "a running timeline cannot move before its prequeue baseline");
  Check(HasError(moved_backward,
                 OpeningMovieAudioClockError::TimelineMovedBackward),
        "a relative timeline cannot move behind the last applied frame");
  Check(unchanged_timeline &&
            *unchanged_timeline ==
                OpeningMovieAudioClockStep{
                    .state = running,
                    .presentation_elapsed = std::chrono::nanoseconds::zero(),
                },
        "an equal running timeline emits exactly zero elapsed time");

  OpeningMovieAudioClockState exact_state{
      .started = true,
      .session_generation = 1U,
  };
  const OpeningMovieAudioClockResult exact_one =
      omega::app::AdvanceOpeningMovieAudioClock(
          exact_state, {.session_generation = 1U, .timeline_frames = 1U});
  Check(exact_one &&
            exact_one->presentation_elapsed ==
                std::chrono::nanoseconds{20'833} &&
            exact_one->state.nanosecond_remainder == 16'000U,
        "the first 48 kHz frame retains the exact 16000 numerator remainder");
  if (exact_one) {
    const OpeningMovieAudioClockResult exact_two =
        omega::app::AdvanceOpeningMovieAudioClock(
            exact_one->state,
            {.session_generation = 1U, .timeline_frames = 2U});
    if (exact_two) {
      const OpeningMovieAudioClockResult exact_three =
          omega::app::AdvanceOpeningMovieAudioClock(
              exact_two->state,
              {.session_generation = 1U, .timeline_frames = 3U});
      Check(
          exact_two->presentation_elapsed == std::chrono::nanoseconds{20'833} &&
              exact_two->state.nanosecond_remainder == 32'000U && exact_three &&
              exact_three->presentation_elapsed ==
                  std::chrono::nanoseconds{20'834} &&
              exact_three->state.nanosecond_remainder == 0U &&
              exact_one->presentation_elapsed +
                      exact_two->presentation_elapsed +
                      exact_three->presentation_elapsed ==
                  std::chrono::nanoseconds{62'500},
          "three one-frame reductions distribute exact 48 kHz remainder as "
          "20833, 20833, "
          "and 20834 nanoseconds");
    } else {
      Check(false, "the second exact-remainder advance succeeds");
    }
  }

  const OpeningMovieAudioClockState partition_start{
      .started = true,
      .session_generation = 2U,
  };
  const OpeningMovieAudioClockResult one_shot =
      omega::app::AdvanceOpeningMovieAudioClock(
          partition_start,
          {.session_generation = 2U, .timeline_frames = 48'000U});
  Check(one_shot && one_shot->presentation_elapsed == std::chrono::seconds{1} &&
            one_shot->state.nanosecond_remainder == 0U,
        "48000 frames reduce to exactly one second with no remainder");

  const OpeningMovieAudioClockResult partition_a1 =
      omega::app::AdvanceOpeningMovieAudioClock(
          partition_start, {.session_generation = 2U, .timeline_frames = 1U});
  if (partition_a1) {
    const OpeningMovieAudioClockResult partition_a2 =
        omega::app::AdvanceOpeningMovieAudioClock(
            partition_a1->state,
            {.session_generation = 2U, .timeline_frames = 3U});
    if (partition_a2) {
      const OpeningMovieAudioClockResult partition_a3 =
          omega::app::AdvanceOpeningMovieAudioClock(
              partition_a2->state,
              {.session_generation = 2U, .timeline_frames = 48'000U});
      Check(one_shot && partition_a3 &&
                partition_a1->presentation_elapsed +
                        partition_a2->presentation_elapsed +
                        partition_a3->presentation_elapsed ==
                    std::chrono::seconds{1} &&
                partition_a3->state == one_shot->state,
            "the 1+2+47997 partition is exactly equivalent to one-shot "
            "conversion");
    } else {
      Check(false, "the middle 1+2+47997 partition succeeds");
    }
  }

  const OpeningMovieAudioClockResult partition_b1 =
      omega::app::AdvanceOpeningMovieAudioClock(
          partition_start, {.session_generation = 2U, .timeline_frames = 17U});
  if (partition_b1) {
    const OpeningMovieAudioClockResult partition_b2 =
        omega::app::AdvanceOpeningMovieAudioClock(
            partition_b1->state,
            {.session_generation = 2U, .timeline_frames = 48'000U});
    Check(
        one_shot && partition_b2 &&
            partition_b1->presentation_elapsed +
                    partition_b2->presentation_elapsed ==
                std::chrono::seconds{1} &&
            partition_b2->state == one_shot->state,
        "the 17+47983 partition is exactly equivalent to one-shot conversion");
  }

  const OpeningMovieAudioClockState zero_remainder_overflow_state{
      .started = true,
      .session_generation = 9U,
  };
  constexpr std::uint64_t zero_remainder_max_delta =
      std::numeric_limits<std::uint64_t>::max() /
      omega::app::kOpeningMovieAudioClockNanosecondsPerSecond;
  const OpeningMovieAudioClockResult boundary_success =
      omega::app::AdvanceOpeningMovieAudioClock(
          zero_remainder_overflow_state,
          {.session_generation = 9U,
           .timeline_frames = zero_remainder_max_delta});
  const OpeningMovieAudioClockResult boundary_overflow =
      omega::app::AdvanceOpeningMovieAudioClock(
          zero_remainder_overflow_state,
          {.session_generation = 9U,
           .timeline_frames = zero_remainder_max_delta + 1U});
  Check(boundary_success.has_value(),
        "the largest zero-remainder multiplication boundary succeeds");
  Check(HasError(boundary_overflow,
                 OpeningMovieAudioClockError::ConversionOverflow) &&
            zero_remainder_overflow_state ==
                OpeningMovieAudioClockState{
                    .started = true,
                    .session_generation = 9U,
                },
        "one frame above the multiplication boundary fails without "
        "caller-state mutation");

  const OpeningMovieAudioClockState remainder_overflow_state{
      .started = true,
      .session_generation = 10U,
      .nanosecond_remainder = 47'999U,
  };
  constexpr std::uint64_t remainder_max_delta =
      (std::numeric_limits<std::uint64_t>::max() - 47'999U) /
      omega::app::kOpeningMovieAudioClockNanosecondsPerSecond;
  const OpeningMovieAudioClockResult remainder_boundary_success =
      omega::app::AdvanceOpeningMovieAudioClock(
          remainder_overflow_state,
          {.session_generation = 10U, .timeline_frames = remainder_max_delta});
  const OpeningMovieAudioClockResult remainder_boundary_overflow =
      omega::app::AdvanceOpeningMovieAudioClock(
          remainder_overflow_state,
          {.session_generation = 10U,
           .timeline_frames = remainder_max_delta + 1U});
  Check(remainder_boundary_success.has_value(),
        "the largest nonzero-remainder multiplication boundary succeeds");
  Check(HasError(remainder_boundary_overflow,
                 OpeningMovieAudioClockError::ConversionOverflow) &&
            remainder_overflow_state.nanosecond_remainder == 47'999U,
        "a nonzero remainder participates in overflow checking without "
        "caller-state mutation");

  constexpr std::array all_errors{
      OpeningMovieAudioClockError::QueueBeforeFirstFrame,
      OpeningMovieAudioClockError::AlreadyStarted,
      OpeningMovieAudioClockError::StartGenerationDidNotAdvance,
      OpeningMovieAudioClockError::StartTimelineMovedBackward,
      OpeningMovieAudioClockError::SessionGenerationChanged,
      OpeningMovieAudioClockError::TimelineBeforeBaseline,
      OpeningMovieAudioClockError::TimelineMovedBackward,
      OpeningMovieAudioClockError::ConversionOverflow,
  };
  bool all_messages_present = true;
  for (const OpeningMovieAudioClockError error : all_errors)
    all_messages_present =
        all_messages_present &&
        !omega::app::OpeningMovieAudioClockErrorMessage(error).empty();
  Check(all_messages_present,
        "every clock error has a static nonempty diagnostic message");

  if (failures != 0) {
    std::cerr << failures << " opening movie audio clock test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "opening movie audio clock tests passed\n";
  return EXIT_SUCCESS;
}
