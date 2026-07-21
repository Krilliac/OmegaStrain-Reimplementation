#include "opening_movie_safety.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
constexpr std::uint64_t kMpegTimestampWrap = 1ULL << 33U;
constexpr std::uint64_t kMpegTimestampTicksPerSecond = 90'000U;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

omega::media::MpegVideoElementaryStreamPlan
MakePlan(const std::initializer_list<std::optional<std::uint64_t>> timestamps) {
  omega::media::MpegVideoElementaryStreamPlan plan;
  for (const std::optional<std::uint64_t> timestamp : timestamps) {
    plan.payloads.push_back(omega::media::MpegVideoElementaryStreamPayloadRange{
        .presentation_timestamp_90khz = timestamp,
    });
  }
  return plan;
}
} // namespace

int main() {
  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(MakePlan({})) ==
            omega::app::kOpeningMovieMaximumSafetyTicks,
        "an empty timestamp set uses the fixed watchdog maximum");
  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(MakePlan({42U})) ==
            omega::app::kOpeningMovieMaximumSafetyTicks,
        "one timestamp uses the fixed watchdog maximum");
  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(
            MakePlan({42U, std::nullopt, 42U})) ==
            omega::app::kOpeningMovieMaximumSafetyTicks,
        "a degenerate timestamp span uses the fixed watchdog maximum");

  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(
            MakePlan({0U, kMpegTimestampTicksPerSecond})) ==
            omega::app::kOpeningMovieSafetySlackTicks +
                omega::app::kBootSequenceTicksPerSecond,
        "a one-second span receives only the fixed safety slack");
  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(MakePlan(
            {0U, 1U})) == omega::app::kOpeningMovieSafetySlackTicks + 12U,
        "sub-tick MPEG timestamps round the watchdog upward");

  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(
            MakePlan({kMpegTimestampWrap - kMpegTimestampTicksPerSecond / 2U,
                      kMpegTimestampTicksPerSecond / 2U})) ==
            omega::app::kOpeningMovieSafetySlackTicks +
                omega::app::kBootSequenceTicksPerSecond,
        "one MPEG timestamp wrap preserves the short relative span");

  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(
            MakePlan({0U, 60U * 60U * kMpegTimestampTicksPerSecond})) ==
            omega::app::kOpeningMovieMaximumSafetyTicks,
        "an extreme valid timestamp span cannot weaken the watchdog ceiling");
  Check(omega::app::CalculateOpeningMovieSafetyDurationTicks(MakePlan(
            {kMpegTimestampWrap - 1U, 0U, kMpegTimestampWrap - 1U, 0U})) ==
            omega::app::kOpeningMovieMaximumSafetyTicks,
        "repeated timestamp wraps saturate at the fixed watchdog ceiling");

  if (failures != 0)
    std::cerr << failures << " opening-movie safety test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
