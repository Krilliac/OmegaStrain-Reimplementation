#include "omega/retail/vag_adpcm_decoder.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kHeaderBytes = 48;
constexpr std::uint64_t kFrameBytes = 16;
constexpr std::uint64_t kSamplesPerFrame = 28;
constexpr std::uint64_t kMaximumTailBytes = 2032;
constexpr std::uint64_t kMaximumDataBytes = 4ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumOutputBytes = 32ULL * 1024ULL * 1024ULL;
constexpr std::uint32_t kObservedSampleRateHz = 22050;

constexpr std::array<std::array<std::int32_t, 2>, 5> kPredictorCoefficients{{
    {0, 0},
    {60, 0},
    {115, -52},
    {98, -55},
    {122, -60},
}};

[[nodiscard]] asset::DecodeError Error(
    const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] std::uint32_t ReadBe32(const std::span<const std::byte> bytes,
                                     const std::size_t offset) noexcept
{
    return (std::to_integer<std::uint32_t>(bytes[offset]) << 24U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 8U) |
           std::to_integer<std::uint32_t>(bytes[offset + 3]);
}

[[nodiscard]] bool IsSupportedVersion(const std::uint32_t version) noexcept
{
    return version == 0 || version == 4 || version == 0x20;
}

[[nodiscard]] std::int64_t DivideFloorBy64(const std::int64_t value) noexcept
{
    if (value >= 0)
        return value / 64;
    return -((-value + 63) / 64);
}

[[nodiscard]] std::int32_t SignedNibble(const std::uint8_t value) noexcept
{
    return value < 8U ? static_cast<std::int32_t>(value) : static_cast<std::int32_t>(value) - 16;
}
} // namespace

asset::DecodeResult<asset::MonoPcm16IR> DecodeVagAdpcm(const std::span<const std::byte> bytes,
                                                       const asset::DecodeLimits limits)
{
    constexpr std::uint64_t hard_maximum_input_bytes =
        kHeaderBytes + kMaximumDataBytes + kMaximumTailBytes;
    if (bytes.size() > limits.maximum_input_bytes || bytes.size() > hard_maximum_input_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "VAG input exceeds the decode limit"));
    if (bytes.size() < kHeaderBytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "VAG header is truncated", bytes.size()));

    constexpr std::array<std::byte, 4> magic{std::byte{'V'}, std::byte{'A'}, std::byte{'G'},
                                             std::byte{'p'}};
    if (!std::equal(magic.begin(), magic.end(), bytes.begin()))
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed, "VAG magic is invalid", 0));

    const std::uint32_t version = ReadBe32(bytes, 0x04);
    if (!IsSupportedVersion(version))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "VAG version is outside the observed envelope", 0x04));
    if (ReadBe32(bytes, 0x08) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "VAG reserved header word must be zero", 0x08));

    const std::uint32_t data_bytes = ReadBe32(bytes, 0x0C);
    if (data_bytes % kFrameBytes != 0)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "VAG data size is not frame aligned", 0x0C));
    if (data_bytes > kMaximumDataBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "VAG data exceeds the fixed decode limit", 0x0C));
    if (ReadBe32(bytes, 0x10) != kObservedSampleRateHz)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "VAG sample rate is outside the observed envelope", 0x10));

    std::uint64_t declared_end = 0;
    if (!Add(kHeaderBytes, data_bytes, declared_end))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "VAG declared extent overflows", 0x0C));
    if (declared_end > bytes.size())
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "VAG frame data is truncated", bytes.size()));

    const std::uint64_t tail_bytes = bytes.size() - declared_end;
    if (tail_bytes != 0 && (tail_bytes < 16 || tail_bytes > kMaximumTailBytes))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "VAG tail is outside the observed envelope", declared_end));
    const auto tail = bytes.subspan(static_cast<std::size_t>(declared_end));
    const auto nonzero_tail =
        std::ranges::find_if(tail, [](const std::byte value) { return value != std::byte{0}; });
    if (nonzero_tail != tail.end())
    {
        const std::uint64_t local_offset =
            static_cast<std::uint64_t>(std::distance(tail.begin(), nonzero_tail));
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "VAG tail contains unobserved nonzero data",
                                     declared_end + local_offset));
    }

    const std::uint64_t frame_count = data_bytes / kFrameBytes;
    std::uint64_t sample_count = 0;
    if (!Multiply(frame_count, kSamplesPerFrame, sample_count))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "VAG decoded sample count overflows", 0x0C));
    std::uint64_t decoded_items = 0;
    if (!Add(1, frame_count, decoded_items))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "VAG decoded item count overflows", 0x0C));
    if (decoded_items > limits.maximum_items)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "VAG decoded items exceed the decode limit", 0x0C));

    std::uint64_t sample_bytes = 0;
    std::uint64_t frame_metadata_bytes = 0;
    std::uint64_t logical_output_bytes = sizeof(asset::MonoPcm16IR);
    if (!Multiply(sample_count, sizeof(std::int16_t), sample_bytes) ||
        !Multiply(frame_count, sizeof(asset::AudioSourceFrameIR), frame_metadata_bytes) ||
        !Add(logical_output_bytes, sample_bytes, logical_output_bytes) ||
        !Add(logical_output_bytes, frame_metadata_bytes, logical_output_bytes))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "VAG decoded output size overflows", 0x0C));
    if (logical_output_bytes > limits.maximum_output_bytes ||
        logical_output_bytes > kMaximumOutputBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "VAG decoded output exceeds the decode limit", 0x0C));

    for (std::uint64_t frame_index = 0; frame_index < frame_count; ++frame_index)
    {
        const std::uint64_t frame_offset = kHeaderBytes + frame_index * kFrameBytes;
        const std::uint8_t control =
            std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(frame_offset)]);
        if ((control >> 4U) >= kPredictorCoefficients.size())
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                         "VAG frame predictor is not supported", frame_offset));
        if ((control & 0x0FU) > 12U)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                         "VAG frame shift is not supported", frame_offset));
    }

    try
    {
        asset::MonoPcm16IR decoded{
            .sample_rate_hz = kObservedSampleRateHz,
            .samples = std::vector<std::int16_t>(static_cast<std::size_t>(sample_count)),
            .source_frames =
                std::vector<asset::AudioSourceFrameIR>(static_cast<std::size_t>(frame_count)),
        };

        std::int32_t previous_sample = 0;
        std::int32_t second_previous_sample = 0;
        std::size_t output_index = 0;
        for (std::size_t frame_index = 0; frame_index < decoded.source_frames.size(); ++frame_index)
        {
            const std::size_t frame_offset =
                static_cast<std::size_t>(kHeaderBytes) + frame_index * kFrameBytes;
            const std::uint8_t control = std::to_integer<std::uint8_t>(bytes[frame_offset]);
            const std::size_t predictor = control >> 4U;
            const std::uint8_t shift = control & 0x0FU;
            decoded.source_frames[frame_index] = asset::AudioSourceFrameIR{
                .sample_offset = static_cast<std::uint64_t>(output_index),
                .source_flags = std::to_integer<std::uint8_t>(bytes[frame_offset + 1]),
            };

            for (std::size_t frame_sample = 0; frame_sample < kSamplesPerFrame; ++frame_sample)
            {
                const std::uint8_t packed =
                    std::to_integer<std::uint8_t>(bytes[frame_offset + 2 + frame_sample / 2]);
                const std::uint8_t nibble = frame_sample % 2 == 0 ? packed & 0x0FU : packed >> 4U;
                const std::int64_t scaled = static_cast<std::int64_t>(SignedNibble(nibble)) *
                                            (std::int64_t{1} << (12U - shift));
                const std::int64_t prediction_numerator =
                    static_cast<std::int64_t>(kPredictorCoefficients[predictor][0]) *
                        previous_sample +
                    static_cast<std::int64_t>(kPredictorCoefficients[predictor][1]) *
                        second_previous_sample +
                    32;
                const std::int64_t sample = scaled + DivideFloorBy64(prediction_numerator);
                const auto clamped = static_cast<std::int32_t>(
                    std::clamp<std::int64_t>(sample, std::numeric_limits<std::int16_t>::min(),
                                             std::numeric_limits<std::int16_t>::max()));
                decoded.samples[output_index++] = static_cast<std::int16_t>(clamped);
                second_previous_sample = previous_sample;
                previous_sample = clamped;
            }
        }

        return decoded;
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow, "VAG allocation size"));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "VAG allocation"));
    }
}
} // namespace omega::retail
