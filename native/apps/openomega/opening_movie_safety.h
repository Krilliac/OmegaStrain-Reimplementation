#pragma once

#include "boot_sequence.h"

#include "omega/media/mpeg_video_elementary_stream.h"

#include <cstdint>

namespace omega::app {
inline constexpr std::uint64_t kOpeningMovieSafetySlackTicks =
    10U * kBootSequenceTicksPerSecond;
inline constexpr std::uint64_t kOpeningMovieMaximumSafetyTicks =
    5U * 60U * kBootSequenceTicksPerSecond;

// [any thread; stateless/reentrant] Derives a bounded boot watchdog from the
// selected video stream's presentation timestamps. Sparse, degenerate, or
// adversarial timestamp sets use the fixed maximum instead of weakening the
// fail-open boot boundary.
[[nodiscard]] std::uint64_t CalculateOpeningMovieSafetyDurationTicks(
    const media::MpegVideoElementaryStreamPlan &plan) noexcept;
} // namespace omega::app
