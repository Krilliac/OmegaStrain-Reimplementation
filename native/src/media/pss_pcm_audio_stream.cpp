#include "omega/media/pss_pcm_audio_stream.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

namespace omega::media
{
namespace
{
constexpr std::uint8_t kPrivateStream1Id = 0xBDU;
constexpr std::uint32_t kSupportedEncodingTag = 1U;
constexpr std::uint32_t kSshdBodyBytes = 24U;
constexpr std::uint64_t kContainerFramingBytes = 40U;
constexpr std::uint64_t kPcm16SampleBytes = 2U;
constexpr std::uint64_t kMpegTimestampMaximum90Khz = (1ULL << 33U) - 1U;
constexpr std::array<std::byte, 4> kSshdMagic{std::byte{'S'}, std::byte{'S'}, std::byte{'h'},
                                              std::byte{'d'}};
constexpr std::array<std::byte, 4> kSsbdMagic{std::byte{'S'}, std::byte{'S'}, std::byte{'b'},
                                              std::byte{'d'}};

struct ContentRange
{
    std::uint64_t source_offset = 0;
    std::uint64_t byte_count = 0;
    std::optional<std::uint64_t> presentation_timestamp_90khz;
};

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
    if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] std::array<std::byte, 4> ReadPrefix(const std::span<const std::byte> source,
                                                  const std::uint64_t offset) noexcept
{
    return {source[static_cast<std::size_t>(offset)], source[static_cast<std::size_t>(offset + 1U)],
            source[static_cast<std::size_t>(offset + 2U)],
            source[static_cast<std::size_t>(offset + 3U)]};
}

class ContentCursor final
{
  public:
    ContentCursor(const std::span<const std::byte> source,
                  const std::span<const ContentRange> ranges) noexcept
        : source_(source), ranges_(ranges)
    {
    }

    [[nodiscard]] bool Read(std::byte& value) noexcept
    {
        while (range_index_ < ranges_.size() && range_offset_ >= ranges_[range_index_].byte_count)
        {
            ++range_index_;
            range_offset_ = 0;
        }
        if (range_index_ >= ranges_.size())
            return false;
        const auto& range = ranges_[range_index_];
        value = source_[static_cast<std::size_t>(range.source_offset + range_offset_)];
        ++range_offset_;
        ++logical_offset_;
        return true;
    }

    [[nodiscard]] std::uint64_t logical_offset() const noexcept
    {
        return logical_offset_;
    }

  private:
    std::span<const std::byte> source_;
    std::span<const ContentRange> ranges_;
    std::size_t range_index_ = 0;
    std::uint64_t range_offset_ = 0;
    std::uint64_t logical_offset_ = 0;
};

[[nodiscard]] asset::DecodeResult<std::array<std::byte, 4>> ReadMagic(ContentCursor& cursor)
{
    std::array<std::byte, 4> value{};
    for (auto& byte : value)
    {
        if (!cursor.Read(byte))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                         "PSS PCM container magic is truncated",
                                         cursor.logical_offset()));
        }
    }
    return value;
}

[[nodiscard]] asset::DecodeResult<std::uint32_t> ReadU32LittleEndian(ContentCursor& cursor)
{
    std::array<std::byte, 4> bytes{};
    for (auto& byte : bytes)
    {
        if (!cursor.Read(byte))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                         "PSS PCM container field is truncated",
                                         cursor.logical_offset()));
        }
    }
    return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[0])) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[1])) << 8U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[2])) << 16U) |
           (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[3])) << 24U);
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> ValidatePlanOutputBudget(
    const std::uint64_t range_count, const asset::DecodeLimits limits)
{
    if (range_count > kPssPcmAudioMaximumPayloadRanges)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM payload-range count exceeds the hard limit"));
    }
    if (limits.maximum_items == 0U || range_count > limits.maximum_items - 1U)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "PSS PCM plan exceeds the item limit"));
    }

    std::uint64_t dynamic_bytes = 0;
    std::uint64_t output_bytes = 0;
    if (!Multiply(range_count, sizeof(PssPcmAudioPayloadRange), dynamic_bytes) ||
        !Add(sizeof(PssPcmAudioStreamPlan), dynamic_bytes, output_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM plan output size overflows"));
    }
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM plan exceeds the output-byte limit"));
    }
    return output_bytes;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> ValidateAndMeasurePlan(
    const PssPcmAudioStreamPlan& plan, const std::span<const std::byte> source,
    const asset::DecodeLimits limits)
{
    if (source.size() > kMpegProgramStreamMaximumInputBytes ||
        source.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "PSS PCM source exceeds an input limit"));
    }
    if (plan.source_byte_count != source.size())
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::InvalidReference,
                  "PSS PCM plan source size does not match the borrowed source"));
    }
    if (plan.sample_rate_hz == 0U || plan.sample_rate_hz > kPssPcmAudioMaximumSampleRateHz ||
        plan.channel_count == 0U || plan.channel_count > kPssPcmAudioMaximumChannelCount ||
        plan.interleave_block_bytes == 0U ||
        plan.interleave_block_bytes > kPssPcmAudioMaximumInterleaveBlockBytes ||
        (plan.interleave_block_bytes % kPcm16SampleBytes) != 0U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "PSS PCM plan format facts are invalid"));
    }
    if (plan.payloads.empty())
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::InvalidReference, "PSS PCM plan has no payload ranges"));
    }
    auto budget = ValidatePlanOutputBudget(plan.payloads.size(), limits);
    if (!budget)
        return std::unexpected(budget.error());

    std::uint64_t bytes_per_frame = 0;
    std::uint64_t measured_bytes = 0;
    if (!Multiply(plan.channel_count, kPcm16SampleBytes, bytes_per_frame) ||
        !Multiply(plan.total_frame_count, bytes_per_frame, measured_bytes) ||
        measured_bytes != plan.total_sample_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "PSS PCM plan frame and byte totals are inconsistent"));
    }
    std::uint64_t interleave_round_bytes = 0;
    if (!Multiply(plan.channel_count, plan.interleave_block_bytes, interleave_round_bytes) ||
        interleave_round_bytes == 0U || plan.total_sample_bytes == 0U ||
        (plan.total_sample_bytes % interleave_round_bytes) != 0U)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::InvalidReference,
                  "PSS PCM plan does not contain complete channel-interleave rounds"));
    }

    std::uint64_t source_cursor = 0;
    std::uint64_t sample_cursor = 0;
    for (const auto& range : plan.payloads)
    {
        if (range.byte_count == 0U || range.source_offset < source_cursor ||
            range.sample_byte_offset != sample_cursor)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "PSS PCM plan payload ranges overlap or are out of order",
                                         range.source_offset));
        }
        std::uint64_t source_end = 0;
        if (!Add(range.source_offset, range.byte_count, source_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "PSS PCM plan payload range overflows",
                                         range.source_offset));
        }
        if (source_end > source.size())
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "PSS PCM plan payload reaches past the borrowed source",
                                         range.source_offset));
        }
        if (range.presentation_timestamp_90khz &&
            *range.presentation_timestamp_90khz > kMpegTimestampMaximum90Khz)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "PSS PCM plan PTS is outside the 33-bit clock domain",
                                         range.source_offset));
        }
        if (!Add(sample_cursor, range.byte_count, sample_cursor))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "PSS PCM plan payload total overflows",
                                         range.source_offset));
        }
        source_cursor = source_end;
    }
    if (sample_cursor != plan.total_sample_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "PSS PCM plan payload total is inconsistent"));
    }
    return bytes_per_frame;
}

class PlanByteCursor final
{
  public:
    PlanByteCursor(const PssPcmAudioStreamPlan& plan, const std::span<const std::byte> source,
                   const std::uint64_t logical_offset) noexcept
        : ranges_(plan.payloads), source_(source)
    {
        auto found =
            std::upper_bound(ranges_.begin(), ranges_.end(), logical_offset,
                             [](const std::uint64_t value, const PssPcmAudioPayloadRange& range)
                             { return value < range.sample_byte_offset; });
        if (found != ranges_.begin())
            --found;
        range_index_ = static_cast<std::size_t>(found - ranges_.begin());
        if (range_index_ < ranges_.size())
            range_offset_ = logical_offset - ranges_[range_index_].sample_byte_offset;
    }

    [[nodiscard]] bool ReadU16LittleEndian(std::int16_t& value) noexcept
    {
        std::byte low{};
        std::byte high{};
        if (!Read(low) || !Read(high))
            return false;
        const std::uint16_t bits = static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(low)) |
            static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(high)) << 8U));
        value = std::bit_cast<std::int16_t>(bits);
        return true;
    }

  private:
    [[nodiscard]] bool Read(std::byte& value) noexcept
    {
        while (range_index_ < ranges_.size() && range_offset_ >= ranges_[range_index_].byte_count)
        {
            ++range_index_;
            range_offset_ = 0;
        }
        if (range_index_ >= ranges_.size())
            return false;
        value =
            source_[static_cast<std::size_t>(ranges_[range_index_].source_offset + range_offset_)];
        ++range_offset_;
        return true;
    }

    std::span<const PssPcmAudioPayloadRange> ranges_;
    std::span<const std::byte> source_;
    std::size_t range_index_ = 0;
    std::uint64_t range_offset_ = 0;
};
} // namespace

asset::DecodeResult<PssPcmAudioStreamPlan> BuildPssPcmAudioStreamPlan(
    const std::span<const std::byte> source, const MpegProgramStreamDescriptor& descriptor,
    const std::optional<std::array<std::byte, 4>> requested_private_packet_prefix,
    const asset::DecodeLimits limits)
{
    std::uint64_t temporary_range_bytes = 0;
    if (!Multiply(descriptor.packets.size(), sizeof(ContentRange), temporary_range_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM validation scratch size overflows"));
    }
    if (temporary_range_bytes > limits.maximum_scratch_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM validation exceeds the scratch limit"));
    }
    asset::DecodeLimits inspection_limits = limits;
    inspection_limits.maximum_output_bytes = limits.maximum_scratch_bytes - temporary_range_bytes;
    auto inspected = InspectMpegProgramStream(source, inspection_limits);
    if (!inspected)
        return std::unexpected(inspected.error());
    if (*inspected != descriptor)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::InvalidReference,
                  "PSS PCM descriptor does not match a fresh source inspection"));
    }

    std::optional<std::array<std::byte, 4>> selected_prefix = requested_private_packet_prefix;
    std::uint64_t selected_packet_count = 0;
    std::uint64_t selected_content_bytes = 0;
    std::optional<std::uint64_t> first_pts;
    std::optional<std::uint64_t> last_pts;
    for (const auto& packet : inspected->packets)
    {
        if (packet.kind != MpegProgramStreamPacketKind::Pes ||
            packet.stream_id != kPrivateStream1Id)
        {
            continue;
        }
        if (packet.payload_bytes < 4U)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                         "PSS private audio packet prefix is truncated",
                                         packet.payload_offset));
        }
        const auto prefix = ReadPrefix(source, packet.payload_offset);
        if (!requested_private_packet_prefix)
        {
            if (!selected_prefix)
                selected_prefix = prefix;
            else if (*selected_prefix != prefix)
            {
                return std::unexpected(
                    Error(asset::DecodeErrorCode::DuplicateReference,
                          "PSS PCM automatic private-packet prefix selection is ambiguous",
                          packet.payload_offset));
            }
        }
        if (prefix != *selected_prefix)
            continue;

        ++selected_packet_count;
        if (!Add(selected_content_bytes, packet.payload_bytes - 4U, selected_content_bytes))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "PSS PCM selected content size overflows",
                                         packet.payload_offset));
        }
        if (selected_packet_count == 1U)
            first_pts = packet.presentation_timestamp_90khz;
        last_pts = packet.presentation_timestamp_90khz;
    }
    if (!selected_prefix || selected_packet_count == 0U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "PSS PCM descriptor has no selected private audio stream"));
    }
    if (selected_content_bytes < kContainerFramingBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "PSS PCM SShd/SSbd framing is truncated"));
    }

    auto budget = ValidatePlanOutputBudget(selected_packet_count, limits);
    if (!budget)
        return std::unexpected(budget.error());

    std::vector<ContentRange> content_ranges;
    try
    {
        content_ranges.reserve(static_cast<std::size_t>(selected_packet_count));
        for (const auto& packet : inspected->packets)
        {
            if (packet.kind != MpegProgramStreamPacketKind::Pes ||
                packet.stream_id != kPrivateStream1Id ||
                ReadPrefix(source, packet.payload_offset) != *selected_prefix)
            {
                continue;
            }
            if (packet.payload_bytes > 4U)
            {
                content_ranges.push_back(ContentRange{
                    .source_offset = packet.payload_offset + 4U,
                    .byte_count = packet.payload_bytes - 4U,
                    .presentation_timestamp_90khz = packet.presentation_timestamp_90khz,
                });
            }
        }
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "PSS PCM content-range allocation failed within configured limits"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM content-range count exceeds container capacity"));
    }
    if (content_ranges.empty())
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "PSS PCM selected stream is empty"));
    }

    ContentCursor cursor(source, content_ranges);
    auto sshd_magic = ReadMagic(cursor);
    auto sshd_bytes = ReadU32LittleEndian(cursor);
    auto encoding_tag = ReadU32LittleEndian(cursor);
    auto sample_rate_hz = ReadU32LittleEndian(cursor);
    auto channel_count = ReadU32LittleEndian(cursor);
    auto interleave_block_bytes = ReadU32LittleEndian(cursor);
    auto loop_start = ReadU32LittleEndian(cursor);
    auto loop_end = ReadU32LittleEndian(cursor);
    auto ssbd_magic = ReadMagic(cursor);
    auto sample_bytes = ReadU32LittleEndian(cursor);
    if (!sshd_magic || !sshd_bytes || !encoding_tag || !sample_rate_hz || !channel_count ||
        !interleave_block_bytes || !loop_start || !loop_end || !ssbd_magic || !sample_bytes)
    {
        if (!sshd_magic)
            return std::unexpected(sshd_magic.error());
        if (!sshd_bytes)
            return std::unexpected(sshd_bytes.error());
        if (!encoding_tag)
            return std::unexpected(encoding_tag.error());
        if (!sample_rate_hz)
            return std::unexpected(sample_rate_hz.error());
        if (!channel_count)
            return std::unexpected(channel_count.error());
        if (!interleave_block_bytes)
            return std::unexpected(interleave_block_bytes.error());
        if (!loop_start)
            return std::unexpected(loop_start.error());
        if (!loop_end)
            return std::unexpected(loop_end.error());
        if (!ssbd_magic)
            return std::unexpected(ssbd_magic.error());
        return std::unexpected(sample_bytes.error());
    }
    if (*sshd_magic != kSshdMagic)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "PSS PCM SShd magic is missing", 0));
    }
    if (*sshd_bytes != kSshdBodyBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PSS PCM SShd body size is unsupported", 4));
    }
    if (*encoding_tag != kSupportedEncodingTag)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PSS PCM encoding tag is unsupported", 8));
    }
    if (*sample_rate_hz == 0U || *sample_rate_hz > kPssPcmAudioMaximumSampleRateHz)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM sample rate exceeds the supported limit", 12));
    }
    if (*channel_count == 0U || *channel_count > kPssPcmAudioMaximumChannelCount)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM channel count exceeds the supported limit", 16));
    }
    if (*interleave_block_bytes == 0U ||
        *interleave_block_bytes > kPssPcmAudioMaximumInterleaveBlockBytes ||
        (*interleave_block_bytes % kPcm16SampleBytes) != 0U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PSS PCM channel-interleave block size is unsupported", 20));
    }
    if (*loop_start != std::numeric_limits<std::uint32_t>::max() ||
        *loop_end != std::numeric_limits<std::uint32_t>::max())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PSS PCM looped streams are not supported", 24));
    }
    if (*ssbd_magic != kSsbdMagic)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "PSS PCM SSbd magic is missing", 32));
    }
    std::uint64_t expected_content_bytes = 0;
    if (!Add(kContainerFramingBytes, *sample_bytes, expected_content_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM declared sample size overflows", 36));
    }
    if (expected_content_bytes != selected_content_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "PSS PCM declared sample size does not reach exact stream end",
                                     36));
    }

    std::uint64_t interleave_round_bytes = 0;
    std::uint64_t bytes_per_frame = 0;
    if (!Multiply(*channel_count, *interleave_block_bytes, interleave_round_bytes) ||
        !Multiply(*channel_count, kPcm16SampleBytes, bytes_per_frame))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM format size overflows"));
    }
    if (*sample_bytes == 0U || (*sample_bytes % interleave_round_bytes) != 0U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PSS PCM sample data does not contain "
                                     "complete channel-interleave rounds",
                                     36));
    }

    PssPcmAudioStreamPlan plan{
        .private_packet_prefix = *selected_prefix,
        .source_byte_count = source.size(),
        .sample_rate_hz = *sample_rate_hz,
        .channel_count = *channel_count,
        .interleave_block_bytes = *interleave_block_bytes,
        .total_sample_bytes = *sample_bytes,
        .total_frame_count = *sample_bytes / bytes_per_frame,
        .first_packet_presentation_timestamp_90khz = first_pts,
        .last_packet_presentation_timestamp_90khz = last_pts,
    };
    try
    {
        plan.payloads.reserve(content_ranges.size());
        std::uint64_t skip = kContainerFramingBytes;
        std::uint64_t sample_offset = 0;
        for (const auto& range : content_ranges)
        {
            const std::uint64_t range_skip = std::min(skip, range.byte_count);
            skip -= range_skip;
            if (range_skip == range.byte_count)
                continue;
            const std::uint64_t retained_bytes = range.byte_count - range_skip;
            plan.payloads.push_back(PssPcmAudioPayloadRange{
                .source_offset = range.source_offset + range_skip,
                .byte_count = retained_bytes,
                .sample_byte_offset = sample_offset,
                .presentation_timestamp_90khz = range.presentation_timestamp_90khz,
            });
            sample_offset += retained_bytes;
        }
        if (skip != 0U || sample_offset != plan.total_sample_bytes)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::InvalidReference,
                      "PSS PCM framing trim produced an inconsistent sample extent"));
        }
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM plan allocation failed within configured limits"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM plan range count exceeds container capacity"));
    }
    return plan;
}

asset::DecodeResult<std::uint64_t> DecodePssPcm16Interleaved(
    const PssPcmAudioStreamPlan& plan, const std::span<const std::byte> source,
    const std::uint64_t first_frame, const std::span<std::int16_t> output_samples,
    const asset::DecodeLimits limits)
{
    auto bytes_per_frame = ValidateAndMeasurePlan(plan, source, limits);
    if (!bytes_per_frame)
        return std::unexpected(bytes_per_frame.error());
    if ((output_samples.size() % plan.channel_count) != 0U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "PSS PCM output sample count is not frame aligned"));
    }

    std::uint64_t output_bytes = 0;
    if (!Multiply(output_samples.size(), sizeof(std::int16_t), output_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM decoded output size overflows"));
    }
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PSS PCM decoded output exceeds the output-byte limit"));
    }

    const std::uint64_t frame_count = output_samples.size() / plan.channel_count;
    std::uint64_t frame_end = 0;
    if (!Add(first_frame, frame_count, frame_end))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM requested frame interval overflows"));
    }
    if (frame_end > plan.total_frame_count)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "PSS PCM requested frame interval reaches past stream end"));
    }
    if (frame_count == 0U)
        return 0U;

    const std::uint64_t frames_per_interleave_block =
        plan.interleave_block_bytes / kPcm16SampleBytes;
    std::uint64_t interleave_round_bytes = 0;
    if (!Multiply(plan.channel_count, plan.interleave_block_bytes, interleave_round_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PSS PCM interleave round size overflows"));
    }

    std::uint64_t current_frame = first_frame;
    std::uint64_t output_frame = 0;
    while (current_frame < frame_end)
    {
        const std::uint64_t round_index = current_frame / frames_per_interleave_block;
        const std::uint64_t frame_in_round = current_frame % frames_per_interleave_block;
        const std::uint64_t frames_this_round =
            std::min(frame_end - current_frame, frames_per_interleave_block - frame_in_round);
        const std::uint64_t round_byte_offset = round_index * interleave_round_bytes;
        for (std::uint64_t channel = 0; channel < plan.channel_count; ++channel)
        {
            const std::uint64_t channel_byte_offset = round_byte_offset +
                                                      channel * plan.interleave_block_bytes +
                                                      frame_in_round * kPcm16SampleBytes;
            PlanByteCursor cursor(plan, source, channel_byte_offset);
            for (std::uint64_t index = 0; index < frames_this_round; ++index)
            {
                std::int16_t sample = 0;
                if (!cursor.ReadU16LittleEndian(sample))
                {
                    return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                                 "PSS PCM plan ended during sample deinterleave",
                                                 channel_byte_offset));
                }
                output_samples[static_cast<std::size_t>(
                    (output_frame + index) * plan.channel_count + channel)] = sample;
            }
        }
        current_frame += frames_this_round;
        output_frame += frames_this_round;
    }
    return frame_count;
}
} // namespace omega::media
