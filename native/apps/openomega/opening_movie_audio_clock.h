#pragma once

#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <string_view>
#include <type_traits>

namespace omega::app {
// Project-owned presentation timebase. Audio-service observations are reduced
// into exact 48 kHz elapsed intervals without depending on SDL or wall time.
inline constexpr std::uint64_t kOpeningMovieAudioClockRateHz = 48'000U;
inline constexpr std::uint64_t kOpeningMovieAudioClockNanosecondsPerSecond =
    1'000'000'000U;

struct OpeningMovieAudioClockObservation {
  std::uint64_t session_generation = 0U;
  std::uint64_t timeline_frames = 0U;

  friend constexpr bool
  operator==(const OpeningMovieAudioClockObservation &,
             const OpeningMovieAudioClockObservation &) noexcept = default;
};

struct OpeningMovieAudioClockState {
  bool started = false;
  std::uint64_t session_generation = 0U;
  std::uint64_t baseline_frames = 0U;
  std::uint64_t applied_frames = 0U;
  std::uint64_t nanosecond_remainder = 0U;

  friend constexpr bool
  operator==(const OpeningMovieAudioClockState &,
             const OpeningMovieAudioClockState &) noexcept = default;
};

struct OpeningMovieAudioClockStartSignals {
  bool video_frame_available = false;
  bool pcm_queue_accepted = false;
  OpeningMovieAudioClockObservation before_queue{};
  OpeningMovieAudioClockObservation after_queue{};

  friend constexpr bool
  operator==(const OpeningMovieAudioClockStartSignals &,
             const OpeningMovieAudioClockStartSignals &) noexcept = default;
};

enum class OpeningMovieAudioClockError : std::uint8_t {
  QueueBeforeFirstFrame = 0U,
  AlreadyStarted = 1U,
  StartGenerationDidNotAdvance = 2U,
  StartTimelineMovedBackward = 3U,
  SessionGenerationChanged = 4U,
  TimelineBeforeBaseline = 5U,
  TimelineMovedBackward = 6U,
  ConversionOverflow = 7U,
};

struct OpeningMovieAudioClockStep {
  OpeningMovieAudioClockState state{};
  std::chrono::nanoseconds presentation_elapsed{0};

  friend constexpr bool
  operator==(const OpeningMovieAudioClockStep &,
             const OpeningMovieAudioClockStep &) noexcept = default;
};

using OpeningMovieAudioClockResult =
    std::expected<OpeningMovieAudioClockStep, OpeningMovieAudioClockError>;

[[nodiscard]] constexpr std::string_view OpeningMovieAudioClockErrorMessage(
    const OpeningMovieAudioClockError error) noexcept {
  switch (error) {
  case OpeningMovieAudioClockError::QueueBeforeFirstFrame:
    return "opening movie audio queued before the first video frame";
  case OpeningMovieAudioClockError::AlreadyStarted:
    return "opening movie audio clock was started more than once";
  case OpeningMovieAudioClockError::StartGenerationDidNotAdvance:
    return "opening movie audio timeline generation did not advance exactly "
           "once";
  case OpeningMovieAudioClockError::StartTimelineMovedBackward:
    return "opening movie audio timeline moved backwards while starting";
  case OpeningMovieAudioClockError::SessionGenerationChanged:
    return "opening movie audio timeline generation changed unexpectedly";
  case OpeningMovieAudioClockError::TimelineBeforeBaseline:
    return "opening movie audio timeline moved before its prequeue baseline";
  case OpeningMovieAudioClockError::TimelineMovedBackward:
    return "opening movie audio timeline moved backwards";
  case OpeningMovieAudioClockError::ConversionOverflow:
    return "opening movie audio timeline exceeded its bounded clock";
  }
  return "unknown opening movie audio clock failure";
}

// [any thread; reentrant] A rejected queue is a no-op. An accepted first queue
// may start only after a video frame exists, and must advance the audio-service
// generation exactly once. The prequeue timeline is the baseline so callback
// demand concurrent with the queue operation is retained.
[[nodiscard]] constexpr OpeningMovieAudioClockResult
StartOpeningMovieAudioClock(
    OpeningMovieAudioClockState state,
    const OpeningMovieAudioClockStartSignals signals) noexcept {
  if (!signals.pcm_queue_accepted) {
    return OpeningMovieAudioClockStep{
        .state = state,
        .presentation_elapsed = std::chrono::nanoseconds::zero(),
    };
  }
  if (!signals.video_frame_available)
    return std::unexpected(OpeningMovieAudioClockError::QueueBeforeFirstFrame);
  if (state.started)
    return std::unexpected(OpeningMovieAudioClockError::AlreadyStarted);
  if (signals.after_queue.session_generation !=
      signals.before_queue.session_generation + std::uint64_t{1U}) {
    return std::unexpected(
        OpeningMovieAudioClockError::StartGenerationDidNotAdvance);
  }
  if (signals.after_queue.timeline_frames <
      signals.before_queue.timeline_frames)
    return std::unexpected(
        OpeningMovieAudioClockError::StartTimelineMovedBackward);

  state = OpeningMovieAudioClockState{
      .started = true,
      .session_generation = signals.after_queue.session_generation,
      .baseline_frames = signals.before_queue.timeline_frames,
      .applied_frames = 0U,
      .nanosecond_remainder = 0U,
  };
  return OpeningMovieAudioClockStep{
      .state = state,
      .presentation_elapsed = std::chrono::nanoseconds::zero(),
  };
}

// [any thread; reentrant] Reduces a monotonic device-demand observation to one
// exact elapsed interval. Errors never expose a partially updated state.
[[nodiscard]] constexpr OpeningMovieAudioClockResult
AdvanceOpeningMovieAudioClock(
    OpeningMovieAudioClockState state,
    const OpeningMovieAudioClockObservation observation) noexcept {
  if (!state.started) {
    return OpeningMovieAudioClockStep{
        .state = state,
        .presentation_elapsed = std::chrono::nanoseconds::zero(),
    };
  }
  if (observation.session_generation != state.session_generation)
    return std::unexpected(
        OpeningMovieAudioClockError::SessionGenerationChanged);
  if (observation.timeline_frames < state.baseline_frames)
    return std::unexpected(OpeningMovieAudioClockError::TimelineBeforeBaseline);

  const std::uint64_t relative_frames =
      observation.timeline_frames - state.baseline_frames;
  if (relative_frames < state.applied_frames)
    return std::unexpected(OpeningMovieAudioClockError::TimelineMovedBackward);

  const std::uint64_t delta_frames = relative_frames - state.applied_frames;
  if (delta_frames >
      (std::numeric_limits<std::uint64_t>::max() - state.nanosecond_remainder) /
          kOpeningMovieAudioClockNanosecondsPerSecond) {
    return std::unexpected(OpeningMovieAudioClockError::ConversionOverflow);
  }

  const std::uint64_t numerator =
      delta_frames * kOpeningMovieAudioClockNanosecondsPerSecond +
      state.nanosecond_remainder;
  const std::uint64_t elapsed_nanoseconds =
      numerator / kOpeningMovieAudioClockRateHz;
  state.applied_frames = relative_frames;
  state.nanosecond_remainder = numerator % kOpeningMovieAudioClockRateHz;

  return OpeningMovieAudioClockStep{
      .state = state,
      .presentation_elapsed =
          std::chrono::nanoseconds{
              static_cast<std::chrono::nanoseconds::rep>(elapsed_nanoseconds)},
  };
}

static_assert(std::numeric_limits<std::chrono::nanoseconds::rep>::max() >=
              static_cast<std::chrono::nanoseconds::rep>(
                  std::numeric_limits<std::uint64_t>::max() /
                  kOpeningMovieAudioClockRateHz));
static_assert(std::is_trivially_copyable_v<OpeningMovieAudioClockObservation>);
static_assert(std::is_standard_layout_v<OpeningMovieAudioClockObservation>);
static_assert(std::is_trivially_copyable_v<OpeningMovieAudioClockState>);
static_assert(std::is_standard_layout_v<OpeningMovieAudioClockState>);
static_assert(std::is_trivially_copyable_v<OpeningMovieAudioClockStartSignals>);
static_assert(std::is_standard_layout_v<OpeningMovieAudioClockStartSignals>);
static_assert(std::is_trivially_copyable_v<OpeningMovieAudioClockStep>);
static_assert(std::is_standard_layout_v<OpeningMovieAudioClockStep>);
static_assert(sizeof(OpeningMovieAudioClockError) == 1U);
} // namespace omega::app
