#pragma once

#include "omega/media/mpeg_program_stream_descriptor.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace omega::media
{
inline constexpr std::uint64_t kPssPcmAudioMaximumPayloadRanges =
    kMpegProgramStreamMaximumPacketDescriptors;
inline constexpr std::uint32_t kPssPcmAudioMaximumSampleRateHz = 384'000U;
inline constexpr std::uint32_t kPssPcmAudioMaximumChannelCount = 8U;
inline constexpr std::uint32_t kPssPcmAudioMaximumInterleaveBlockBytes = 1U << 20U;

[[nodiscard]] constexpr asset::DecodeLimits DefaultPssPcmAudioDecodeLimits() noexcept
{
    return DefaultMpegProgramStreamDecodeLimits();
}

// One borrowed PCM extent in the original MPEG-PS source. sample_byte_offset is
// relative to the first PCM byte after the SShd/SSbd framing. PTS belongs to
// the containing private-stream PES and can therefore precede this extent when
// the container header shares the first packet.
struct PssPcmAudioPayloadRange
{
    std::uint64_t source_offset = 0;
    std::uint64_t byte_count = 0;
    std::uint64_t sample_byte_offset = 0;
    std::optional<std::uint64_t> presentation_timestamp_90khz;

    [[nodiscard]] bool operator==(const PssPcmAudioPayloadRange&) const = default;
};

// Owned metadata for the supported SShd encoding tag 1 variant: signed 16-bit
// little-endian PCM. Encoded sample blocks repeat as one interleave_block_bytes
// extent per channel. The plan owns no media bytes and retains no pointer into
// the source.
struct PssPcmAudioStreamPlan
{
    std::array<std::byte, 4> private_packet_prefix{};
    std::uint64_t source_byte_count = 0;
    std::uint32_t sample_rate_hz = 0;
    std::uint32_t channel_count = 0;
    std::uint32_t interleave_block_bytes = 0;
    std::uint64_t total_sample_bytes = 0;
    std::uint64_t total_frame_count = 0;
    std::optional<std::uint64_t> first_packet_presentation_timestamp_90khz;
    std::optional<std::uint64_t> last_packet_presentation_timestamp_90khz;
    std::vector<PssPcmAudioPayloadRange> payloads;

    [[nodiscard]] bool operator==(const PssPcmAudioStreamPlan&) const = default;
};

// [any worker thread; stateless/reentrant] Revalidates the complete MPEG-PS
// descriptor against the source, selects one exact four-byte private_stream_1
// packet prefix, and parses a complete SShd/SSbd stream without joining its PES
// payloads. With no requested prefix, exactly one distinct prefix must be
// present. The currently supported narrow variant has SShd size 24, encoding
// tag 1, non-looping sentinels, signed 16-bit little-endian samples, and
// complete channel-interleave rounds. maximum_items charges one result root
// plus one selected PCM payload range; maximum_output_bytes charges the
// returned plan, while descriptor reinspection is charged to
// maximum_scratch_bytes.
[[nodiscard]] asset::DecodeResult<PssPcmAudioStreamPlan> BuildPssPcmAudioStreamPlan(
    std::span<const std::byte> source, const MpegProgramStreamDescriptor& descriptor,
    std::optional<std::array<std::byte, 4>> requested_private_packet_prefix = std::nullopt,
    asset::DecodeLimits limits = DefaultPssPcmAudioDecodeLimits());

// [any worker thread; stateless/reentrant] Revalidates the offset-only plan,
// then deinterleaves an exact frame interval into caller-owned, host-endian
// signed 16-bit samples. Output is sample-major interleaved (frame 0 channel 0,
// frame 0 channel 1, ...). output_samples.size() must be divisible by
// channel_count and the requested interval must be fully in range. No
// allocation or scratch occurs.
[[nodiscard]] asset::DecodeResult<std::uint64_t> DecodePssPcm16Interleaved(
    const PssPcmAudioStreamPlan& plan, std::span<const std::byte> source, std::uint64_t first_frame,
    std::span<std::int16_t> output_samples,
    asset::DecodeLimits limits = DefaultPssPcmAudioDecodeLimits());
} // namespace omega::media
