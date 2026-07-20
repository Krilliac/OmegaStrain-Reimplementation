#include "omega/retail/vag_adpcm_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <vector>

namespace vag_test_allocation
{
inline constexpr std::size_t kDisabled = std::numeric_limits<std::size_t>::max();
std::size_t allocations_before_failure = kDisabled;

void Arm(const std::size_t allocations_to_allow) noexcept
{
    allocations_before_failure = allocations_to_allow;
}

void Disarm() noexcept
{
    allocations_before_failure = kDisabled;
}
} // namespace vag_test_allocation

void* operator new(const std::size_t size)
{
    if (vag_test_allocation::allocations_before_failure != vag_test_allocation::kDisabled)
    {
        if (vag_test_allocation::allocations_before_failure == 0U)
        {
            vag_test_allocation::Disarm();
            throw std::bad_alloc{};
        }
        --vag_test_allocation::allocations_before_failure;
    }
    if (void* const memory = std::malloc(size == 0U ? 1U : size))
        return memory;
    throw std::bad_alloc{};
}

void operator delete(void* const memory) noexcept
{
    std::free(memory);
}

void operator delete(void* const memory, const std::size_t) noexcept
{
    std::free(memory);
}

namespace
{
using Frame = std::array<std::byte, 16>;
using Payload = std::array<std::uint8_t, 14>;

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
void CheckError(const omega::asset::DecodeResult<Value>& result,
                const omega::asset::DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}

template <typename Value>
void CheckErrorAt(const omega::asset::DecodeResult<Value>& result,
                  const omega::asset::DecodeErrorCode code, const std::uint64_t byte_offset,
                  const std::string_view message)
{
    Check(!result && result.error().code == code && result.error().byte_offset == byte_offset,
          message);
}

void WriteBe32(std::vector<std::byte>& bytes, const std::size_t offset, const std::uint32_t value)
{
    bytes[offset] = static_cast<std::byte>((value >> 24U) & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 16U) & 0xFFU);
    bytes[offset + 2] = static_cast<std::byte>((value >> 8U) & 0xFFU);
    bytes[offset + 3] = static_cast<std::byte>(value & 0xFFU);
}

Frame MakeFrame(const std::uint8_t predictor, const std::uint8_t shift, const std::uint8_t flags,
                const Payload& payload)
{
    Frame frame{};
    frame[0] = static_cast<std::byte>((predictor << 4U) | shift);
    frame[1] = static_cast<std::byte>(flags);
    for (std::size_t index = 0; index < payload.size(); ++index)
        frame[index + 2] = static_cast<std::byte>(payload[index]);
    return frame;
}

std::vector<std::byte> MakeVag(const std::uint32_t version, const std::span<const Frame> frames,
                               const std::size_t zero_tail = 0,
                               const std::uint32_t sample_rate = 22050,
                               const std::uint32_t reserved = 0)
{
    std::vector<std::byte> bytes(48U + frames.size() * 16U + zero_tail, std::byte{0});
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'A'};
    bytes[2] = std::byte{'G'};
    bytes[3] = std::byte{'p'};
    WriteBe32(bytes, 0x04, version);
    WriteBe32(bytes, 0x08, reserved);
    WriteBe32(bytes, 0x0C, static_cast<std::uint32_t>(frames.size() * 16U));
    WriteBe32(bytes, 0x10, sample_rate);
    for (std::size_t index = 0; index < frames.size(); ++index)
        std::ranges::copy(frames[index], bytes.begin() + 48U + index * 16U);
    return bytes;
}

std::vector<std::byte> MakeVag(const std::uint32_t version,
                               const std::initializer_list<Frame> frames,
                               const std::size_t zero_tail = 0,
                               const std::uint32_t sample_rate = 22050,
                               const std::uint32_t reserved = 0)
{
    return MakeVag(version, std::span<const Frame>(frames.begin(), frames.size()), zero_tail,
                   sample_rate, reserved);
}

std::vector<std::byte> MakeDeclaredData(const std::uint32_t data_bytes)
{
    std::vector<std::byte> bytes(48ULL + data_bytes, std::byte{0});
    bytes[0] = std::byte{'V'};
    bytes[1] = std::byte{'A'};
    bytes[2] = std::byte{'G'};
    bytes[3] = std::byte{'p'};
    WriteBe32(bytes, 0x0C, data_bytes);
    WriteBe32(bytes, 0x10, 22050);
    return bytes;
}

void CheckSamples(const omega::asset::DecodeResult<omega::asset::MonoPcm16IR>& decoded,
                  const std::span<const std::int16_t> expected, const std::string_view message)
{
    Check(decoded && std::ranges::equal(decoded->samples, expected), message);
}

constexpr Payload kGoldenPayload{
    0x71, 0x8F, 0x20, 0xDE, 0x43, 0xA5, 0x6C, 0x9B, 0x10, 0xFE, 0x87, 0x35, 0xD2, 0x4A,
};

// These complete frame results are frozen independent golden vectors, not
// values produced by the implementation under test.
constexpr std::array<std::array<std::int16_t, 28>, 5> kPredictorGolden{{
    {256,   1792,  -256, -2048, 0,    512,  -512, -768,  768,  1024, 1280, -1536, -1024, 1536,
     -1280, -1792, 0,    256,   -512, -256, 1792, -2048, 1280, 768,  512,  -768,  -1536, 1024},
    {256,  2032,  1649,  -502,  -471,  70,    -446, -1186, -344, 702,  1938, 281,  -761,  823,
     -508, -2268, -2126, -1737, -2140, -2262, -329, -2356, -929, -103, 415,  -379, -1891, -749},
    {256,   2252,  3583,  2560,  1689,  1467,  752,   -609,  -937,  -165,
     1745,  1734,  674,   1338,  577,   -1842, -3779, -5038, -6494, -7832,
     -7005, -8272, -7892, -6692, -5100, -4495, -5469, -5151},
    {256,   2184,  2868,  467,   -1750, -2569, -2942, -3065, -1397, 1519, 4807, 4519, 1765,  355,
     -2253, -5547, -6558, -5019, -2562, 134,   4199,  4267,  4205,  3540, 2319, -259, -3925, -4764},
    {256,    2280,   3850,   3154,  2403,  2136,  1307,  -279,  -989,  -600,
     1063,   1053,   -13,    524,   -269,  -2796, -5078, -6803, -8720, -10501,
     -10051, -11363, -10958, -9468, -7263, -5737, -5663, -4393},
}};

constexpr std::array<std::int16_t, 28> kPredictorFourSecondFrameGolden{
    -2809, 556,    3437,   3983,   4370,  5108,  5128,  4219,  4003,  4699,
    6485,  6421,   5136,   5307,   4021,  898,   -2058, -4509, -7178, -9712,
    -9992, -11990, -12208, -11263, -9513, -8343, -8521, -7398,
};
} // namespace

int main()
{
    for (std::uint8_t predictor = 0; predictor < kPredictorGolden.size(); ++predictor)
    {
        const auto bytes = MakeVag(0, {MakeFrame(predictor, 4, predictor, kGoldenPayload)});
        const auto decoded = omega::retail::DecodeVagAdpcm(bytes);
        CheckSamples(decoded, kPredictorGolden[predictor],
                     "every supported predictor matches its independent full-frame "
                     "golden vector");
        Check(decoded && decoded->source_frames.size() == 1 &&
                  decoded->source_frames[0].sample_offset == 0 &&
                  decoded->source_frames[0].source_flags == predictor,
              "predictor fixtures preserve their source flag byte independently of "
              "PCM");
    }

    const auto history_bytes =
        MakeVag(4, {MakeFrame(4, 4, 0xA5, kGoldenPayload), MakeFrame(4, 4, 0x07, kGoldenPayload)});
    const auto history = omega::retail::DecodeVagAdpcm(history_bytes);
    Check(history && history->samples.size() == 56 &&
              std::ranges::equal(std::span(history->samples).subspan(0, 28), kPredictorGolden[4]) &&
              std::ranges::equal(std::span(history->samples).subspan(28, 28),
                                 kPredictorFourSecondFrameGolden),
          "predictor history crosses frame boundaries with independently frozen "
          "samples");
    Check(history && history->source_frames ==
                         std::vector<omega::asset::AudioSourceFrameIR>{
                             {.sample_offset = 0, .source_flags = 0xA5},
                             {.sample_offset = 28, .source_flags = 0x07},
                         },
          "all source flag bits and exact marker sample offsets are preserved");
    Check(history && history->samples.size() == 56,
          "source end and repeat flag bits do not apply automatic playback policy");

    Payload boundary_payload{};
    boundary_payload.fill(0x78);
    constexpr std::array<std::int16_t, 4> shift_zero{-32768, 28672, -32768, 28672};
    constexpr std::array<std::int16_t, 4> shift_twelve{-8, 7, -8, 7};
    const auto lower_shift =
        omega::retail::DecodeVagAdpcm(MakeVag(0x20, {MakeFrame(0, 0, 0, boundary_payload)}));
    const auto upper_shift =
        omega::retail::DecodeVagAdpcm(MakeVag(0x20, {MakeFrame(0, 12, 0, boundary_payload)}));
    Check(lower_shift && std::ranges::equal(std::span(lower_shift->samples).first(4), shift_zero),
          "shift zero preserves low-nibble-first sign expansion at the PCM16 "
          "boundary");
    Check(upper_shift && std::ranges::equal(std::span(upper_shift->samples).first(4), shift_twelve),
          "shift twelve preserves low-nibble-first signed unit samples");

    Payload positive_history{};
    positive_history.back() = 0x78;
    Payload negative_history{};
    negative_history.back() = 0x87;
    Payload positive_overflow{};
    positive_overflow.front() = 0x07;
    Payload negative_overflow{};
    negative_overflow.front() = 0x08;
    const auto positive_clamp = omega::retail::DecodeVagAdpcm(
        MakeVag(0, {MakeFrame(0, 0, 0, positive_history), MakeFrame(2, 12, 0, positive_overflow)}));
    const auto negative_clamp = omega::retail::DecodeVagAdpcm(
        MakeVag(0, {MakeFrame(0, 0, 0, negative_history), MakeFrame(2, 12, 0, negative_overflow)}));
    Check(positive_clamp && positive_clamp->samples[28] == 32767,
          "positive predictor accumulation clamps before entering later history");
    Check(negative_clamp && negative_clamp->samples[28] == -32768,
          "negative predictor accumulation clamps before entering later history");

    const auto canonical_bytes = MakeVag(0, {MakeFrame(3, 4, 0x62, kGoldenPayload)});
    auto version_four_bytes = MakeVag(4, {MakeFrame(3, 4, 0x62, kGoldenPayload)}, 16);
    auto version_twenty_bytes = MakeVag(0x20, {MakeFrame(3, 4, 0x62, kGoldenPayload)}, 2032);
    for (std::size_t offset = 0x14; offset < 0x30; ++offset)
        version_twenty_bytes[offset] = static_cast<std::byte>((offset * 17U) & 0xFFU);
    const auto canonical = omega::retail::DecodeVagAdpcm(canonical_bytes);
    const auto version_four = omega::retail::DecodeVagAdpcm(version_four_bytes);
    const auto seventeen_byte_tail =
        omega::retail::DecodeVagAdpcm(MakeVag(4, {MakeFrame(3, 4, 0x62, kGoldenPayload)}, 17));
    const auto version_twenty = omega::retail::DecodeVagAdpcm(version_twenty_bytes);
    Check(canonical && version_four && seventeen_byte_tail && version_twenty &&
              *canonical == *version_four && *canonical == *seventeen_byte_tail &&
              *canonical == *version_twenty,
          "observed versions, opaque header bytes, and legal zero tails share "
          "canonical output");

    auto owned_source = canonical_bytes;
    const auto owned = omega::retail::DecodeVagAdpcm(owned_source);
    std::ranges::fill(owned_source, std::byte{0});
    Check(owned && canonical && *owned == *canonical,
          "decoded PCM and markers remain owned after replacing all source bytes");
    const auto deterministic = omega::retail::DecodeVagAdpcm(canonical_bytes);
    Check(canonical && deterministic && *canonical == *deterministic,
          "repeated decoding is deterministic and independently owned");

    const std::span<const std::byte> canonical_span = canonical_bytes;
    vag_test_allocation::Arm(0);
    const auto first_output_allocation_failure = omega::retail::DecodeVagAdpcm(canonical_span);
    vag_test_allocation::Disarm();
    CheckError(first_output_allocation_failure, omega::asset::DecodeErrorCode::LimitExceeded,
               "the first output allocation failure is returned as a typed decode error");
    vag_test_allocation::Arm(1);
    const auto later_output_allocation_failure = omega::retail::DecodeVagAdpcm(canonical_span);
    vag_test_allocation::Disarm();
    CheckError(later_output_allocation_failure, omega::asset::DecodeErrorCode::LimitExceeded,
               "a later output allocation failure is returned without throwing");

    const auto empty_audio = omega::retail::DecodeVagAdpcm(MakeVag(0, {}));
    Check(empty_audio && empty_audio->sample_rate_hz == 22050 && empty_audio->samples.empty() &&
              empty_audio->source_frames.empty(),
          "an aligned empty payload produces a valid owned empty mono stream");

    CheckErrorAt(omega::retail::DecodeVagAdpcm(std::span<const std::byte>{}),
                 omega::asset::DecodeErrorCode::Truncated, 0,
                 "an empty input is a typed truncation");
    auto short_header = MakeVag(0, {});
    short_header.resize(47);
    CheckErrorAt(omega::retail::DecodeVagAdpcm(short_header),
                 omega::asset::DecodeErrorCode::Truncated, 47,
                 "a partial header reports its exact truncation offset");

    auto wrong_magic = canonical_bytes;
    wrong_magic[0] = std::byte{'v'};
    CheckErrorAt(omega::retail::DecodeVagAdpcm(wrong_magic),
                 omega::asset::DecodeErrorCode::Malformed, 0, "wrong VAG magic is malformed");
    CheckErrorAt(omega::retail::DecodeVagAdpcm(
                     MakeVag(0, {MakeFrame(0, 4, 0, kGoldenPayload)}, 0, 22050, 1)),
                 omega::asset::DecodeErrorCode::Malformed, 0x08,
                 "the observed reserved-zero envelope is strict");

    for (const std::uint32_t version : {1U, 0xFFFFFFFFU})
    {
        CheckErrorAt(
            omega::retail::DecodeVagAdpcm(MakeVag(version, {MakeFrame(0, 4, 0, kGoldenPayload)})),
            omega::asset::DecodeErrorCode::UnsupportedVariant, 0x04,
            "unobserved VAG versions are rejected");
    }
    CheckErrorAt(
        omega::retail::DecodeVagAdpcm(MakeVag(0, {MakeFrame(0, 4, 0, kGoldenPayload)}, 0, 48000)),
        omega::asset::DecodeErrorCode::UnsupportedVariant, 0x10,
        "unobserved sample rates are rejected");

    auto unaligned_size = MakeVag(0, {});
    WriteBe32(unaligned_size, 0x0C, 1);
    CheckErrorAt(omega::retail::DecodeVagAdpcm(unaligned_size),
                 omega::asset::DecodeErrorCode::Malformed, 0x0C,
                 "a non-frame-aligned declared payload is malformed before extent use");
    auto truncated_data = canonical_bytes;
    truncated_data.pop_back();
    CheckErrorAt(omega::retail::DecodeVagAdpcm(truncated_data),
                 omega::asset::DecodeErrorCode::Truncated, truncated_data.size(),
                 "truncated declared frame data is rejected without implicit samples");

    for (const std::size_t tail_bytes : {1U, 15U, 2033U})
    {
        CheckErrorAt(omega::retail::DecodeVagAdpcm(
                         MakeVag(0, {MakeFrame(0, 4, 0, kGoldenPayload)}, tail_bytes)),
                     omega::asset::DecodeErrorCode::UnsupportedVariant, 64,
                     "zero tails outside the observed length envelope are rejected");
    }
    auto dirty_tail = MakeVag(0, {MakeFrame(0, 4, 0, kGoldenPayload)}, 16);
    dirty_tail[69] = std::byte{1};
    CheckErrorAt(omega::retail::DecodeVagAdpcm(dirty_tail),
                 omega::asset::DecodeErrorCode::UnsupportedVariant, 69,
                 "the first nonzero tail byte is rejected at its exact offset");

    constexpr std::array<std::uint8_t, 2> unsupported_predictors{5U, 15U};
    for (const std::uint8_t predictor : unsupported_predictors)
    {
        CheckErrorAt(
            omega::retail::DecodeVagAdpcm(MakeVag(0, {MakeFrame(predictor, 4, 0, kGoldenPayload)})),
            omega::asset::DecodeErrorCode::UnsupportedVariant, 48,
            "unsupported predictor identifiers fail before allocation");
    }
    constexpr std::array<std::uint8_t, 2> unsupported_shifts{13U, 15U};
    for (const std::uint8_t shift : unsupported_shifts)
    {
        CheckErrorAt(
            omega::retail::DecodeVagAdpcm(MakeVag(0, {MakeFrame(4, shift, 0, kGoldenPayload)})),
            omega::asset::DecodeErrorCode::UnsupportedVariant, 48,
            "unsupported shift values fail before allocation");
    }
    CheckErrorAt(omega::retail::DecodeVagAdpcm(MakeVag(0, {MakeFrame(5, 15, 0, kGoldenPayload)})),
                 omega::asset::DecodeErrorCode::UnsupportedVariant, 48,
                 "predictor validation has deterministic priority over shift validation");

    auto limits = omega::asset::DecodeLimits{};
    limits.maximum_input_bytes = canonical_bytes.size();
    Check(omega::retail::DecodeVagAdpcm(canonical_bytes, limits).has_value(),
          "the exact caller input-byte budget succeeds");
    --limits.maximum_input_bytes;
    CheckError(omega::retail::DecodeVagAdpcm(canonical_bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "a one-below caller input-byte budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_items = 2;
    Check(omega::retail::DecodeVagAdpcm(canonical_bytes, limits).has_value(),
          "the root-plus-source-frame item budget succeeds exactly");
    --limits.maximum_items;
    CheckError(omega::retail::DecodeVagAdpcm(canonical_bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "a one-below logical item budget fails");

    const std::uint64_t exact_output_bytes = sizeof(omega::asset::MonoPcm16IR) +
                                             28U * sizeof(std::int16_t) +
                                             sizeof(omega::asset::AudioSourceFrameIR);
    limits = omega::asset::DecodeLimits{};
    limits.maximum_output_bytes = exact_output_bytes;
    Check(omega::retail::DecodeVagAdpcm(canonical_bytes, limits).has_value(),
          "the exact logical output-byte budget succeeds");
    --limits.maximum_output_bytes;
    CheckError(omega::retail::DecodeVagAdpcm(canonical_bytes, limits),
               omega::asset::DecodeErrorCode::LimitExceeded,
               "a one-below logical output-byte budget fails");

    limits = omega::asset::DecodeLimits{};
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::retail::DecodeVagAdpcm(canonical_bytes, limits).has_value(),
          "flat VAG decoding needs no dynamic scratch or nesting budget");

    constexpr std::uint32_t over_hard_data_bytes = 4U * 1024U * 1024U + 16U;
    CheckErrorAt(omega::retail::DecodeVagAdpcm(MakeDeclaredData(over_hard_data_bytes)),
                 omega::asset::DecodeErrorCode::LimitExceeded, 0x0C,
                 "the fixed ADPCM-data ceiling cannot be widened by caller defaults");
    auto huge_declared_extent = MakeVag(0, {});
    WriteBe32(huge_declared_extent, 0x0C, 0xFFFFFFF0U);
    CheckErrorAt(omega::retail::DecodeVagAdpcm(huge_declared_extent),
                 omega::asset::DecodeErrorCode::LimitExceeded, 0x0C,
                 "a huge aligned declared extent is rejected without arithmetic "
                 "wrap or allocation");

    return failures;
}
