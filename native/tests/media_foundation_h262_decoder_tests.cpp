#include "omega/media/media_foundation_h262_decoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace {
using omega::media::H262SequenceHeaderFacts;
using omega::media::MediaFoundationH262Decoder;
using omega::media::MediaFoundationH262DecoderErrorCode;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] H262SequenceHeaderFacts SyntheticSequenceFacts() noexcept {
  return H262SequenceHeaderFacts{
      .width = 16U,
      .height = 16U,
      .aspect_ratio_code = 2U,
      .frame_rate_numerator = 30'000U,
      .frame_rate_denominator = 1'001U,
      .profile_and_level_indication = static_cast<std::uint8_t>(0x48U),
      .sequence_header_count = 1U,
  };
}

void RunConfigurationChecks() {
  static_assert(!std::is_copy_constructible_v<MediaFoundationH262Decoder>);
  static_assert(!std::is_copy_assignable_v<MediaFoundationH262Decoder>);
  static_assert(
      std::is_nothrow_move_constructible_v<MediaFoundationH262Decoder>);
  static_assert(!std::is_move_assignable_v<MediaFoundationH262Decoder>);
  static_assert(std::is_nothrow_destructible_v<MediaFoundationH262Decoder>);

  const auto defaults = omega::media::DefaultMediaFoundationH262DecoderLimits();
  Check(defaults.maximum_input_chunk_bytes != 0U &&
            defaults.maximum_total_input_bytes != 0U &&
            defaults.maximum_total_input_bytes <=
                omega::media::kMediaFoundationH262MaximumTotalInputBytes &&
            defaults.maximum_output_bytes_per_call >= 16U * 16U * 3U / 2U &&
            defaults.maximum_frames_per_call != 0U &&
            defaults.maximum_total_frames != 0U &&
            defaults.maximum_width <=
                omega::media::kMediaFoundationH262MaximumWidth &&
            defaults.maximum_height <=
                omega::media::kMediaFoundationH262MaximumHeight,
        "default decoder limits are finite and can hold one synthetic NV12 "
        "frame");

  auto missing = MediaFoundationH262Decoder::Create({});
  Check(!missing &&
            missing.error().code ==
                MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "incomplete H.262 sequence facts are rejected before platform setup");

  H262SequenceHeaderFacts odd = SyntheticSequenceFacts();
  odd.width = 15U;
  auto odd_result = MediaFoundationH262Decoder::Create(odd);
  Check(!odd_result &&
            odd_result.error().code ==
                MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "odd NV12 dimensions are rejected before platform setup");

  auto limits = defaults;
  limits.maximum_output_bytes_per_call = 16U * 16U * 3U / 2U - 1U;
  auto too_small =
      MediaFoundationH262Decoder::Create(SyntheticSequenceFacts(), limits);
  Check(!too_small &&
            too_small.error().code ==
                MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "an output budget smaller than one frame is rejected before platform "
        "setup");

  limits = defaults;
  limits.maximum_total_input_bytes =
      omega::media::kMediaFoundationH262MaximumTotalInputBytes + 1U;
  auto unbounded_input =
      MediaFoundationH262Decoder::Create(SyntheticSequenceFacts(), limits);
  Check(!unbounded_input &&
            unbounded_input.error().code ==
                MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "callers cannot widen the hard lifetime input ceiling");

  limits = defaults;
  limits.maximum_width = omega::media::kMediaFoundationH262MaximumWidth + 1U;
  auto widened =
      MediaFoundationH262Decoder::Create(SyntheticSequenceFacts(), limits);
  Check(!widened &&
            widened.error().code ==
                MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "callers cannot widen the hard Media Foundation dimension ceiling");

  H262SequenceHeaderFacts excessive_rate = SyntheticSequenceFacts();
  excessive_rate.frame_rate_numerator = 10'000'001U;
  excessive_rate.frame_rate_denominator = 1U;
  auto zero_cadence = MediaFoundationH262Decoder::Create(excessive_rate);
  Check(!zero_cadence &&
            zero_cadence.error().code ==
                MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "frame rates that synthesize a zero 100ns duration are rejected before "
        "platform setup");
}

void RunThreadAffinityPolicyChecks() {
  using omega::media::detail::MediaFoundationH262LifetimeThreadMatches;

  static_assert(MediaFoundationH262LifetimeThreadMatches(17U, 17U));
  static_assert(!MediaFoundationH262LifetimeThreadMatches(17U, 29U));
  Check(MediaFoundationH262LifetimeThreadMatches(1U, 1U),
        "live decoder lifetime operations are allowed on the creator thread");
  Check(!MediaFoundationH262LifetimeThreadMatches(1U, 2U),
        "wrong-thread lifetime operations select the fail-fast path before "
        "COM/MF state is touched");
}

void RunOutputPolicyChecks() {
  using omega::media::detail::MediaFoundationH262FrameRateHasPositiveCadence;
  using omega::media::detail::MediaFoundationH262OutputProgress;

  static_assert(
      MediaFoundationH262FrameRateHasPositiveCadence(10'000'000U, 1U));
  static_assert(
      !MediaFoundationH262FrameRateHasPositiveCadence(10'000'001U, 1U));
  static_assert(!MediaFoundationH262FrameRateHasPositiveCadence(0U, 1U));
  static_assert(!MediaFoundationH262FrameRateHasPositiveCadence(1U, 0U));

  MediaFoundationH262OutputProgress progress;
  bool accepted_limit = true;
  for (std::uint32_t index = 0U;
       index < omega::media::detail::
                   kMediaFoundationH262MaximumConsecutiveFormatChanges;
       ++index) {
    accepted_limit =
        accepted_limit && progress.RecordFormatChangeWithoutOutput();
  }
  Check(accepted_limit,
        "the bounded number of consecutive format changes is accepted");
  Check(!progress.RecordFormatChangeWithoutOutput(),
        "a format-change loop is rejected at the deterministic policy limit");
  progress.RecordOutput();
  Check(progress.consecutive_format_changes() == 0U &&
            progress.RecordFormatChangeWithoutOutput(),
        "only real output resets the consecutive format-change policy");
}

void RunPlatformChecks() {
  auto created = MediaFoundationH262Decoder::Create(SyntheticSequenceFacts());
#ifndef _WIN32
  Check(!created &&
            created.error().code ==
                MediaFoundationH262DecoderErrorCode::UnsupportedPlatform,
        "non-Windows builds expose a clean unsupported factory result");
#else
  if (!created) {
    const auto code = created.error().code;
    Check(code == MediaFoundationH262DecoderErrorCode::DecoderUnavailable ||
              code == MediaFoundationH262DecoderErrorCode::MediaTypeRejected ||
              code ==
                  MediaFoundationH262DecoderErrorCode::OutputTypeUnavailable,
          "Windows without an MPEG-2 component fails with a bounded capability "
          "error");
    return;
  }

  MediaFoundationH262Decoder decoder = std::move(*created);
  const auto moved_from = created->Push({});
  Check(!moved_from && moved_from.error().code ==
                           MediaFoundationH262DecoderErrorCode::DecoderPoisoned,
        "a moved-from decoder rejects use without touching Media Foundation");

  MediaFoundationH262DecoderErrorCode worker_code =
      MediaFoundationH262DecoderErrorCode::TransformFailure;
  std::thread wrong_thread([&decoder, &worker_code] {
    constexpr std::array<std::byte, 1> byte{std::byte{0}};
    const auto result = decoder.Push(byte);
    if (!result)
      worker_code = result.error().code;
  });
  wrong_thread.join();
  Check(worker_code == MediaFoundationH262DecoderErrorCode::WrongThread,
        "decoder thread affinity is checked before input is touched");

  const auto empty = decoder.Push({});
  Check(!empty && empty.error().code ==
                      MediaFoundationH262DecoderErrorCode::InvalidConfiguration,
        "empty elementary chunks are rejected without poisoning the decoder");

  // Synthetic sequence header (16x16, aspect 2, 30000/1001) plus sequence end.
  // It contains no proprietary byte and deliberately cannot produce a picture.
  constexpr std::array<std::byte, 16> header_only{
      std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0xB3},
      std::byte{0x01}, std::byte{0x00}, std::byte{0x10}, std::byte{0x24},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x60}, std::byte{0x00},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x01}, std::byte{0xB7},
  };
  const auto pushed = decoder.Push(header_only, 0);
  Check(pushed && pushed->empty(), "a synthetic header-only chunk is copied "
                                   "and accepted without a decoded frame");
  const auto drained = decoder.Drain();
  Check(drained && drained->empty(), "draining a synthetic stream drops its "
                                     "incomplete picture without output");
  const auto drained_twice = decoder.Drain();
  Check(!drained_twice &&
            drained_twice.error().code ==
                MediaFoundationH262DecoderErrorCode::AlreadyDrained,
        "drain is an explicit one-shot lifecycle transition");

  auto tight_limits = omega::media::DefaultMediaFoundationH262DecoderLimits();
  tight_limits.maximum_input_chunk_bytes = 4U;
  auto bounded = MediaFoundationH262Decoder::Create(SyntheticSequenceFacts(),
                                                    tight_limits);
  if (bounded) {
    constexpr std::array<std::byte, 5> oversized{};
    const auto rejected = bounded->Push(oversized);
    Check(!rejected &&
              rejected.error().code ==
                  MediaFoundationH262DecoderErrorCode::InputLimitExceeded,
          "input chunk limit is enforced before an MF allocation");
    const auto poisoned = bounded->Drain();
    Check(!poisoned && poisoned.error().code ==
                           MediaFoundationH262DecoderErrorCode::DecoderPoisoned,
          "a hard limit failure poisons subsequent decoder operations");
  }

  tight_limits = omega::media::DefaultMediaFoundationH262DecoderLimits();
  tight_limits.maximum_total_input_bytes = 4U;
  auto lifetime_bounded = MediaFoundationH262Decoder::Create(
      SyntheticSequenceFacts(), tight_limits);
  if (lifetime_bounded) {
    constexpr std::array<std::byte, 5> oversized_lifetime{};
    const auto rejected = lifetime_bounded->Push(oversized_lifetime);
    Check(!rejected &&
              rejected.error().code ==
                  MediaFoundationH262DecoderErrorCode::InputLimitExceeded,
          "lifetime input limit is enforced before an MF allocation");
  }
#endif
}
} // namespace

int main() {
  RunConfigurationChecks();
  RunThreadAffinityPolicyChecks();
  RunOutputPolicyChecks();
  RunPlatformChecks();
  if (failures != 0) {
    std::cerr << failures << " Media Foundation H.262 decoder test(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "Media Foundation H.262 decoder tests passed\n";
  return EXIT_SUCCESS;
}
