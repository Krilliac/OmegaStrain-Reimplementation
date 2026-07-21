#pragma once

#include "boot_sequence.h"

#include "omega/media/mpeg_video_elementary_stream.h"

#include <cstdint>
#include <limits>
#include <optional>

namespace omega::app {
inline constexpr std::uint64_t kOpeningMovieSafetySlackTicks =
    10U * kBootSequenceTicksPerSecond;
inline constexpr std::uint64_t kOpeningMovieMaximumSafetyTicks =
    5U * 60U * kBootSequenceTicksPerSecond;
inline constexpr std::uint64_t kMpegPresentationTicksPerSecond = 90'000U;
// Platform decoder presentation domain. Timestamps handed to the decoder are
// signed, so the conversion must reject anything that cannot be represented.
inline constexpr std::uint64_t kOpeningMovieDecoderTicksPerSecond = 10'000'000U;

// [any thread; stateless/reentrant] Converts one MPEG presentation timestamp
// into the platform decoder's signed 100-nanosecond domain. A value outside the
// 33-bit MPEG clock domain has no defined conversion, so it yields nullopt - an
// untimed chunk the decoder resolves from its own cadence - rather than a
// wrapped or negative decoder timestamp. No allocation, no I/O, no borrowed
// lifetime.
[[nodiscard]] constexpr std::optional<std::int64_t>
ConvertMpegTimestampToDecoderTicks(
    const std::optional<std::uint64_t> timestamp_90khz) noexcept {
  if (!timestamp_90khz ||
      *timestamp_90khz > media::kMpegTimestampMaximum90Khz) {
    return std::nullopt;
  }
  const std::uint64_t whole_seconds =
      *timestamp_90khz / kMpegPresentationTicksPerSecond;
  const std::uint64_t remainder =
      *timestamp_90khz % kMpegPresentationTicksPerSecond;
  const std::uint64_t converted =
      whole_seconds * kOpeningMovieDecoderTicksPerSecond +
      remainder * kOpeningMovieDecoderTicksPerSecond /
          kMpegPresentationTicksPerSecond;
  return static_cast<std::int64_t>(converted);
}

// The whole 33-bit MPEG clock domain converts inside the signed decoder domain,
// so the guarded cast above can never produce a negative timestamp.
static_assert(media::kMpegTimestampMaximum90Khz /
                      kMpegPresentationTicksPerSecond *
                      kOpeningMovieDecoderTicksPerSecond +
                  kOpeningMovieDecoderTicksPerSecond <
              static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max()));

// [any thread; stateless/reentrant] Derives a bounded boot watchdog from the
// selected video stream's presentation timestamps. Sparse, degenerate, or
// adversarial timestamp sets use the fixed maximum instead of weakening the
// fail-open boot boundary.
[[nodiscard]] std::uint64_t CalculateOpeningMovieSafetyDurationTicks(
    const media::MpegVideoElementaryStreamPlan &plan) noexcept;
} // namespace omega::app
