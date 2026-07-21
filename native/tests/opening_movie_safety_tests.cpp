#include "opening_movie_safety.h"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
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

  Check(!omega::app::ConvertMpegTimestampToDecoderTicks(std::nullopt),
        "an absent presentation timestamp stays absent");
  Check(omega::app::ConvertMpegTimestampToDecoderTicks(0U) ==
            std::optional<std::int64_t>{0},
        "the zero presentation timestamp converts to the decoder origin");
  Check(omega::app::ConvertMpegTimestampToDecoderTicks(
            kMpegTimestampTicksPerSecond) ==
            std::optional<std::int64_t>{static_cast<std::int64_t>(
                omega::app::kOpeningMovieDecoderTicksPerSecond)},
        "one presentation second converts to one decoder second");
  Check(omega::app::ConvertMpegTimestampToDecoderTicks(1U) ==
            std::optional<std::int64_t>{111},
        "a sub-tick presentation timestamp truncates toward the origin");

  // The 33-bit MPEG clock domain is the whole defined input range. Everything
  // above it must decline to produce a decoder timestamp rather than wrap into
  // a negative or otherwise meaningless signed value.
  const std::optional<std::int64_t> domain_maximum =
      omega::app::ConvertMpegTimestampToDecoderTicks(
          omega::media::kMpegTimestampMaximum90Khz);
  Check(domain_maximum && *domain_maximum > 0,
        "the largest in-domain presentation timestamp converts positively");
  for (const std::uint64_t out_of_domain :
       {omega::media::kMpegTimestampMaximum90Khz + 1U, kMpegTimestampWrap + 1U,
        std::numeric_limits<std::uint64_t>::max(),
        std::numeric_limits<std::uint64_t>::max() / 2U}) {
    Check(!omega::app::ConvertMpegTimestampToDecoderTicks(out_of_domain),
          "a presentation timestamp outside the 33-bit clock domain converts "
          "to no decoder timestamp");
  }

  // Monotonicity across the defined domain is what keeps decoder-side frame
  // ordering derived from these timestamps stable.
  std::optional<std::int64_t> previous;
  for (std::uint64_t timestamp = 0U;
       timestamp <= omega::media::kMpegTimestampMaximum90Khz;
       timestamp += omega::media::kMpegTimestampMaximum90Khz / 977U + 1U) {
    const std::optional<std::int64_t> converted =
        omega::app::ConvertMpegTimestampToDecoderTicks(timestamp);
    Check(converted.has_value() && *converted >= 0,
          "every in-domain presentation timestamp converts non-negatively");
    if (converted && previous)
      Check(*converted > *previous, "conversion is strictly increasing");
    previous = converted;
  }

  if (failures != 0)
    std::cerr << failures << " opening-movie safety test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
