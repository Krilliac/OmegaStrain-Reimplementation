#include "opening_movie_safety.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>

namespace omega::app {
namespace {
constexpr std::uint64_t kMpegTimestampWrap = 1ULL << 33U;
constexpr std::uint64_t kMpegTimestampHalfWrap = kMpegTimestampWrap / 2U;
constexpr std::uint64_t kMpegTimestampTicksPerSecond =
    kMpegPresentationTicksPerSecond;

[[nodiscard]] constexpr std::uint64_t
SaturatingAdd(const std::uint64_t left, const std::uint64_t right) noexcept {
  return right > std::numeric_limits<std::uint64_t>::max() - left
             ? std::numeric_limits<std::uint64_t>::max()
             : left + right;
}

[[nodiscard]] constexpr std::uint64_t
Convert90KhzToTicksCeil(const std::uint64_t timestamp) noexcept {
  const std::uint64_t whole_seconds = timestamp / kMpegTimestampTicksPerSecond;
  const std::uint64_t remainder = timestamp % kMpegTimestampTicksPerSecond;
  if (whole_seconds >
      std::numeric_limits<std::uint64_t>::max() / kBootSequenceTicksPerSecond) {
    return std::numeric_limits<std::uint64_t>::max();
  }
  const std::uint64_t whole_ticks = whole_seconds * kBootSequenceTicksPerSecond;
  const std::uint64_t partial_ticks = (remainder * kBootSequenceTicksPerSecond +
                                       kMpegTimestampTicksPerSecond - 1U) /
                                      kMpegTimestampTicksPerSecond;
  return SaturatingAdd(whole_ticks, partial_ticks);
}
} // namespace

std::uint64_t CalculateOpeningMovieSafetyDurationTicks(
    const media::MpegVideoElementaryStreamPlan &plan) noexcept {
  std::optional<std::uint64_t> minimum;
  std::optional<std::uint64_t> maximum;
  std::optional<std::uint64_t> previous_raw;
  std::uint64_t wrap_base = 0U;
  std::size_t timestamp_count = 0U;

  for (const media::MpegVideoElementaryStreamPayloadRange &payload :
       plan.payloads) {
    if (!payload.presentation_timestamp_90khz)
      continue;
    const std::uint64_t raw = *payload.presentation_timestamp_90khz;
    if (previous_raw && *previous_raw > raw &&
        *previous_raw - raw > kMpegTimestampHalfWrap) {
      wrap_base = SaturatingAdd(wrap_base, kMpegTimestampWrap);
    }
    const std::uint64_t unwrapped = SaturatingAdd(wrap_base, raw);
    minimum = minimum ? std::min(*minimum, unwrapped) : unwrapped;
    maximum = maximum ? std::max(*maximum, unwrapped) : unwrapped;
    previous_raw = raw;
    ++timestamp_count;
  }

  if (timestamp_count < 2U || !minimum || !maximum || *maximum <= *minimum)
    return kOpeningMovieMaximumSafetyTicks;
  const std::uint64_t derived =
      SaturatingAdd(Convert90KhzToTicksCeil(*maximum - *minimum),
                    kOpeningMovieSafetySlackTicks);
  return std::min(derived, kOpeningMovieMaximumSafetyTicks);
}
} // namespace omega::app
