#include "omega/media/mpeg_video_elementary_stream.h"

#include <array>
#include <bit>
#include <limits>
#include <new>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace omega::media
{
namespace
{
constexpr std::uint8_t kProgramEndStreamId = 0xB9;
constexpr std::uint8_t kPackHeaderStreamId = 0xBA;
constexpr std::uint8_t kSystemHeaderStreamId = 0xBB;
constexpr std::uint8_t kProgramStreamMapStreamId = 0xBC;
constexpr std::uint8_t kPrivateStream1Id = 0xBD;
constexpr std::uint8_t kPaddingStreamId = 0xBE;
constexpr std::uint8_t kPrivateStream2Id = 0xBF;
constexpr std::uint32_t kSequenceHeaderStartCode = 0x000001B3U;
constexpr std::uint32_t kExtensionStartCode = 0x000001B5U;
constexpr std::uint8_t kSequenceExtensionId = 1;

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

[[nodiscard]] constexpr bool IsVideoStreamId(const std::uint8_t stream_id) noexcept
{
    return stream_id >= 0xE0U && stream_id <= 0xEFU;
}

[[nodiscard]] constexpr bool IsAudioStreamId(const std::uint8_t stream_id) noexcept
{
    return stream_id >= 0xC0U && stream_id <= 0xDFU;
}

[[nodiscard]] constexpr MpegProgramStreamPayloadClass ClassifyPayload(
    const std::uint8_t stream_id) noexcept
{
    if (IsAudioStreamId(stream_id))
        return MpegProgramStreamPayloadClass::Audio;
    if (IsVideoStreamId(stream_id))
        return MpegProgramStreamPayloadClass::Video;
    if (stream_id == kPrivateStream1Id || stream_id == kPrivateStream2Id)
        return MpegProgramStreamPayloadClass::PrivateData;
    return MpegProgramStreamPayloadClass::Other;
}

struct DescriptorValidation
{
    std::uint16_t video_stream_mask = 0;
    std::array<std::uint64_t, 16> video_payload_counts{};
};

[[nodiscard]] std::optional<asset::DecodeError> ValidatePacketSemantics(
    const MpegProgramStreamPacketDescriptor& packet)
{
    if (packet.presentation_timestamp_90khz &&
        *packet.presentation_timestamp_90khz > kMpegTimestampMaximum90Khz)
    {
        return Error(asset::DecodeErrorCode::InvalidReference,
                     "MPEG video ES descriptor PTS is outside the 33-bit clock domain",
                     packet.packet_offset);
    }
    if (packet.decoding_timestamp_90khz &&
        *packet.decoding_timestamp_90khz > kMpegTimestampMaximum90Khz)
    {
        return Error(asset::DecodeErrorCode::InvalidReference,
                     "MPEG video ES descriptor DTS is outside the 33-bit clock domain",
                     packet.packet_offset);
    }
    if (packet.decoding_timestamp_90khz && !packet.presentation_timestamp_90khz)
    {
        return Error(asset::DecodeErrorCode::InvalidReference,
                     "MPEG video ES descriptor has DTS without PTS", packet.packet_offset);
    }

    const auto require_fixed_kind = [&](const std::uint8_t stream_id,
                                        const MpegProgramStreamPayloadClass payload_class)
        -> std::optional<asset::DecodeError> {
        if (packet.stream_id != stream_id || packet.payload_class != payload_class ||
            packet.zero_length_pes || packet.presentation_timestamp_90khz ||
            packet.decoding_timestamp_90khz)
        {
            return Error(asset::DecodeErrorCode::InvalidReference,
                         "MPEG video ES descriptor packet kind metadata is inconsistent",
                         packet.packet_offset);
        }
        return std::nullopt;
    };

    switch (packet.kind)
    {
    case MpegProgramStreamPacketKind::PackHeader:
        return require_fixed_kind(kPackHeaderStreamId, MpegProgramStreamPayloadClass::None);
    case MpegProgramStreamPacketKind::SystemHeader:
        return require_fixed_kind(kSystemHeaderStreamId, MpegProgramStreamPayloadClass::None);
    case MpegProgramStreamPacketKind::ProgramStreamMap:
        return require_fixed_kind(kProgramStreamMapStreamId, MpegProgramStreamPayloadClass::None);
    case MpegProgramStreamPacketKind::ProgramEnd:
        return require_fixed_kind(kProgramEndStreamId, MpegProgramStreamPayloadClass::None);
    case MpegProgramStreamPacketKind::TrailingZeroPadding:
        return require_fixed_kind(0U, MpegProgramStreamPayloadClass::None);
    case MpegProgramStreamPacketKind::Padding:
        return require_fixed_kind(kPaddingStreamId, MpegProgramStreamPayloadClass::None);
    case MpegProgramStreamPacketKind::Pes:
        if (packet.payload_class != ClassifyPayload(packet.stream_id))
        {
            return Error(asset::DecodeErrorCode::InvalidReference,
                         "MPEG video ES descriptor PES stream classification is inconsistent",
                         packet.packet_offset);
        }
        if (packet.zero_length_pes && packet.payload_class != MpegProgramStreamPayloadClass::Video)
        {
            return Error(asset::DecodeErrorCode::InvalidReference,
                         "MPEG video ES descriptor marks a non-video PES as zero-length",
                         packet.packet_offset);
        }
        return std::nullopt;
    case MpegProgramStreamPacketKind::OpaqueLengthDelimited:
        if (packet.payload_class != ClassifyPayload(packet.stream_id) ||
            IsVideoStreamId(packet.stream_id) || packet.zero_length_pes ||
            packet.presentation_timestamp_90khz || packet.decoding_timestamp_90khz)
        {
            return Error(asset::DecodeErrorCode::InvalidReference,
                         "MPEG video ES opaque packet classification is inconsistent",
                         packet.packet_offset);
        }
        return std::nullopt;
    }
    return Error(asset::DecodeErrorCode::InvalidReference,
                 "MPEG video ES descriptor has an unknown packet kind", packet.packet_offset);
}

[[nodiscard]] asset::DecodeResult<DescriptorValidation> ValidateDescriptor(
    const std::span<const std::byte> source, const MpegProgramStreamDescriptor& descriptor,
    const asset::DecodeLimits limits)
{
    const std::uint64_t source_bytes = source.size();
    if (source_bytes > kMpegProgramStreamMaximumInputBytes ||
        source_bytes > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "MPEG video ES source exceeds an input-byte limit"));
    }
    if (descriptor.packets.size() > kMpegProgramStreamMaximumPacketDescriptors)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "MPEG video ES descriptor packet count exceeds the hard limit"));
    }
    if (descriptor.physical_byte_count != source_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::InvalidReference,
                  "MPEG video ES descriptor physical size does not match source"));
    }
    if (descriptor.packets.empty())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES descriptor has no packets"));
    }
    if (descriptor.packets.front().kind != MpegProgramStreamPacketKind::PackHeader)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES descriptor does not begin with a pack header",
                                     0));
    }

    std::uint64_t cursor = 0;
    std::uint64_t pack_headers = 0;
    std::uint64_t pes_packets = 0;
    std::uint64_t audio_pes_packets = 0;
    std::uint64_t video_pes_packets = 0;
    std::uint64_t private_packets = 0;
    std::uint64_t program_end_packets = 0;
    DescriptorValidation validation;

    for (std::size_t index = 0; index < descriptor.packets.size(); ++index)
    {
        const auto& packet = descriptor.packets[index];
        if (packet.packet_offset != cursor || packet.packet_bytes == 0)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::InvalidReference,
                      "MPEG video ES descriptor packet extents are not contiguous",
                      packet.packet_offset));
        }

        std::uint64_t packet_end = 0;
        if (!Add(packet.packet_offset, packet.packet_bytes, packet_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "MPEG video ES descriptor packet extent overflows",
                                         packet.packet_offset));
        }
        if (packet_end > source_bytes)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "MPEG video ES descriptor packet reaches past source",
                                         packet.packet_offset));
        }
        if (packet.payload_offset < packet.packet_offset || packet.payload_offset > packet_end)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::InvalidReference,
                      "MPEG video ES descriptor payload begins outside its packet",
                      packet.packet_offset));
        }
        std::uint64_t payload_end = 0;
        if (!Add(packet.payload_offset, packet.payload_bytes, payload_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "MPEG video ES descriptor payload extent overflows",
                                         packet.payload_offset));
        }
        if (payload_end > packet_end)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "MPEG video ES descriptor payload reaches past its packet",
                                         packet.payload_offset));
        }
        if (auto semantics_error = ValidatePacketSemantics(packet))
            return std::unexpected(std::move(*semantics_error));

        switch (packet.kind)
        {
        case MpegProgramStreamPacketKind::PackHeader:
            ++pack_headers;
            break;
        case MpegProgramStreamPacketKind::Pes:
            ++pes_packets;
            if (packet.payload_class == MpegProgramStreamPayloadClass::Audio)
                ++audio_pes_packets;
            else if (packet.payload_class == MpegProgramStreamPayloadClass::Video)
            {
                ++video_pes_packets;
                const std::size_t video_index = packet.stream_id - 0xE0U;
                validation.video_stream_mask |= static_cast<std::uint16_t>(1U << video_index);
                ++validation.video_payload_counts[video_index];
            }
            break;
        case MpegProgramStreamPacketKind::ProgramEnd:
            ++program_end_packets;
            if (packet.packet_bytes != 4U ||
                (index + 1U != descriptor.packets.size() &&
                    (index + 2U != descriptor.packets.size() ||
                        descriptor.packets[index + 1U].kind !=
                            MpegProgramStreamPacketKind::TrailingZeroPadding)))
            {
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                             "MPEG video ES descriptor program end is "
                                             "not terminal before optional zero padding",
                                             packet.packet_offset));
            }
            break;
        case MpegProgramStreamPacketKind::TrailingZeroPadding:
            if (index == 0U || index + 1U != descriptor.packets.size() ||
                descriptor.packets[index - 1U].kind != MpegProgramStreamPacketKind::ProgramEnd ||
                program_end_packets != 1U ||
                packet.packet_bytes > kMpegProgramStreamMaximumTrailingZeroPaddingBytes ||
                packet.payload_offset != packet.packet_offset ||
                packet.payload_bytes != packet.packet_bytes)
            {
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "MPEG video ES trailing zero padding metadata is inconsistent",
                    packet.packet_offset));
            }
            for (std::uint64_t offset = packet.packet_offset; offset < packet_end; ++offset)
            {
                if (source[static_cast<std::size_t>(offset)] != std::byte{0})
                {
                    return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                        "MPEG video ES trailing padding byte is nonzero", offset));
                }
            }
            break;
        default:
            break;
        }
        if (packet.payload_class == MpegProgramStreamPayloadClass::PrivateData)
            ++private_packets;
        cursor = packet_end;
    }

    if (cursor != source_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES descriptor does not cover the complete source",
                                     cursor));
    }
    if (descriptor.pack_header_count != pack_headers ||
        descriptor.pes_packet_count != pes_packets ||
        descriptor.audio_pes_packet_count != audio_pes_packets ||
        descriptor.video_pes_packet_count != video_pes_packets ||
        descriptor.private_data_packet_count != private_packets ||
        descriptor.has_program_end != (program_end_packets == 1U) || program_end_packets > 1U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES descriptor summary counters are inconsistent"));
    }
    return validation;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> ValidatePlanBudget(
    const std::uint64_t source_bytes, const std::uint64_t range_count,
    const asset::DecodeLimits limits)
{
    if (source_bytes > kMpegProgramStreamMaximumInputBytes ||
        source_bytes > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "MPEG video ES source exceeds an input-byte limit"));
    }
    if (range_count > kMpegVideoElementaryStreamMaximumPayloadRanges)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "MPEG video ES payload-range count exceeds the hard limit"));
    }
    if (limits.maximum_items == 0 || range_count > limits.maximum_items - 1U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "MPEG video ES plan exceeds the item limit"));
    }
    std::uint64_t dynamic_output_bytes = 0;
    if (!Multiply(range_count, sizeof(MpegVideoElementaryStreamPayloadRange), dynamic_output_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "MPEG video ES plan output size overflows"));
    }
    std::uint64_t output_bytes = 0;
    if (!Add(sizeof(MpegVideoElementaryStreamPlan), dynamic_output_bytes, output_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "MPEG video ES plan output size overflows"));
    }
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "MPEG video ES plan exceeds the output-byte limit"));
    }
    return output_bytes;
}

class ElementaryByteCursor final
{
public:
    explicit ElementaryByteCursor(const MpegVideoElementaryStreamView& stream) noexcept
        : current_(stream.begin()), end_(stream.end())
    {
    }

    [[nodiscard]] bool Read(std::uint8_t& value) noexcept
    {
        while (payload_offset_ >= payload_.size())
        {
            if (current_ == end_)
                return false;
            payload_ = (*current_).payload;
            ++current_;
            payload_offset_ = 0;
        }
        value = std::to_integer<std::uint8_t>(payload_[payload_offset_]);
        ++payload_offset_;
        ++logical_offset_;
        return true;
    }

    [[nodiscard]] std::uint64_t logical_offset() const noexcept
    {
        return logical_offset_;
    }

private:
    MpegVideoElementaryStreamView::const_iterator current_;
    MpegVideoElementaryStreamView::const_iterator end_;
    std::span<const std::byte> payload_;
    std::size_t payload_offset_ = 0;
    std::uint64_t logical_offset_ = 0;
};

template <std::size_t Size>
[[nodiscard]] asset::DecodeResult<std::array<std::uint8_t, Size>> ReadFixed(
    ElementaryByteCursor& cursor, const std::string_view truncated_message)
{
    std::array<std::uint8_t, Size> bytes{};
    for (std::uint8_t& value : bytes)
    {
        if (!cursor.Read(value))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                         std::string(truncated_message), cursor.logical_offset()));
        }
    }
    return bytes;
}

struct H262BaseHeader
{
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint8_t aspect_ratio_code = 0;
    std::uint8_t frame_rate_code = 0;
};

struct H262SequenceExtension
{
    std::uint8_t profile_and_level = 0;
    std::uint8_t horizontal_size_extension = 0;
    std::uint8_t vertical_size_extension = 0;
    std::uint8_t frame_rate_extension_n = 0;
    std::uint8_t frame_rate_extension_d = 0;

    [[nodiscard]] bool operator==(const H262SequenceExtension&) const = default;
};

struct PendingH262Header
{
    H262BaseHeader base;
    std::optional<H262SequenceExtension> extension;
};

[[nodiscard]] asset::DecodeResult<H262BaseHeader> ParseH262BaseHeader(ElementaryByteCursor cursor)
{
    auto fixed = ReadFixed<8>(cursor, "H.262 sequence header is truncated");
    if (!fixed)
        return std::unexpected(fixed.error());

    H262BaseHeader header{
        .width = static_cast<std::uint16_t>(((*fixed)[0] << 4U) | ((*fixed)[1] >> 4U)),
        .height = static_cast<std::uint16_t>((((*fixed)[1] & 0x0FU) << 8U) | (*fixed)[2]),
        .aspect_ratio_code = static_cast<std::uint8_t>((*fixed)[3] >> 4U),
        .frame_rate_code = static_cast<std::uint8_t>((*fixed)[3] & 0x0FU),
    };
    if (header.width == 0 || header.height == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "H.262 sequence header has a zero picture dimension",
                                     cursor.logical_offset() - 8U));
    }
    if (header.aspect_ratio_code < 1U || header.aspect_ratio_code > 4U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "H.262 sequence header has a reserved aspect-ratio code",
                                     cursor.logical_offset() - 5U));
    }
    if (header.frame_rate_code < 1U || header.frame_rate_code > 8U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "H.262 sequence header has a reserved frame-rate code",
                                     cursor.logical_offset() - 5U));
    }
    if (((*fixed)[6] & 0x20U) == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "H.262 sequence-header bit-rate marker is missing",
                                     cursor.logical_offset() - 2U));
    }

    const std::uint64_t matrix_bytes =
        (((*fixed)[7] & 0x02U) != 0 ? 64U : 0U) + (((*fixed)[7] & 0x01U) != 0 ? 64U : 0U);
    for (std::uint64_t index = 0; index < matrix_bytes; ++index)
    {
        std::uint8_t ignored = 0;
        if (!cursor.Read(ignored))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                         "H.262 sequence-header quantizer matrix is truncated",
                                         cursor.logical_offset()));
        }
    }
    return header;
}

[[nodiscard]] asset::DecodeResult<H262SequenceExtension> ParseH262SequenceExtension(
    ElementaryByteCursor cursor)
{
    auto fixed = ReadFixed<6>(cursor, "H.262 sequence extension is truncated");
    if (!fixed)
        return std::unexpected(fixed.error());

    std::uint64_t packed = 0;
    for (const std::uint8_t value : *fixed)
        packed = (packed << 8U) | value;
    if (((packed >> 44U) & 0x0FU) != kSequenceExtensionId)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "H.262 sequence extension identifier is inconsistent",
                                     cursor.logical_offset() - 6U));
    }
    const std::uint8_t chroma_format = static_cast<std::uint8_t>((packed >> 33U) & 0x03U);
    if (chroma_format == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "H.262 sequence extension has a reserved chroma format",
                                     cursor.logical_offset() - 5U));
    }
    if (((packed >> 16U) & 1U) == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "H.262 sequence-extension bit-rate marker is missing",
                                     cursor.logical_offset() - 3U));
    }
    return H262SequenceExtension{
        .profile_and_level = static_cast<std::uint8_t>((packed >> 36U) & 0xFFU),
        .horizontal_size_extension = static_cast<std::uint8_t>((packed >> 31U) & 0x03U),
        .vertical_size_extension = static_cast<std::uint8_t>((packed >> 29U) & 0x03U),
        .frame_rate_extension_n = static_cast<std::uint8_t>((packed >> 5U) & 0x03U),
        .frame_rate_extension_d = static_cast<std::uint8_t>(packed & 0x1FU),
    };
}

[[nodiscard]] std::pair<std::uint32_t, std::uint32_t> BaseFrameRate(
    const std::uint8_t code) noexcept
{
    switch (code)
    {
    case 1:
        return {24'000U, 1'001U};
    case 2:
        return {24U, 1U};
    case 3:
        return {25U, 1U};
    case 4:
        return {30'000U, 1'001U};
    case 5:
        return {30U, 1U};
    case 6:
        return {50U, 1U};
    case 7:
        return {60'000U, 1'001U};
    case 8:
        return {60U, 1U};
    default:
        return {0U, 1U};
    }
}

[[nodiscard]] H262SequenceHeaderFacts MakeH262Facts(const PendingH262Header& pending)
{
    const auto [base_numerator, base_denominator] = BaseFrameRate(pending.base.frame_rate_code);
    std::uint32_t numerator = base_numerator;
    std::uint32_t denominator = base_denominator;
    std::uint32_t width = pending.base.width;
    std::uint32_t height = pending.base.height;
    std::optional<std::uint8_t> profile_and_level;
    if (pending.extension)
    {
        width |= static_cast<std::uint32_t>(pending.extension->horizontal_size_extension) << 12U;
        height |= static_cast<std::uint32_t>(pending.extension->vertical_size_extension) << 12U;
        numerator *= static_cast<std::uint32_t>(pending.extension->frame_rate_extension_n) + 1U;
        denominator *= static_cast<std::uint32_t>(pending.extension->frame_rate_extension_d) + 1U;
        profile_and_level = pending.extension->profile_and_level;
    }
    const std::uint32_t divisor = std::gcd(numerator, denominator);
    return H262SequenceHeaderFacts{
        .width = width,
        .height = height,
        .aspect_ratio_code = pending.base.aspect_ratio_code,
        .frame_rate_numerator = numerator / divisor,
        .frame_rate_denominator = denominator / divisor,
        .profile_and_level_indication = profile_and_level,
        .sequence_header_count = 1,
    };
}

[[nodiscard]] bool SameH262Facts(const H262SequenceHeaderFacts& left,
                                 const H262SequenceHeaderFacts& right) noexcept
{
    return left.width == right.width && left.height == right.height &&
           left.aspect_ratio_code == right.aspect_ratio_code &&
           left.frame_rate_numerator == right.frame_rate_numerator &&
           left.frame_rate_denominator == right.frame_rate_denominator &&
           left.profile_and_level_indication == right.profile_and_level_indication;
}

[[nodiscard]] std::optional<asset::DecodeError> MergeH262Facts(
    const PendingH262Header& pending, std::optional<H262SequenceHeaderFacts>& facts)
{
    H262SequenceHeaderFacts candidate = MakeH262Facts(pending);
    if (!facts)
    {
        facts = candidate;
        return std::nullopt;
    }
    if (!SameH262Facts(*facts, candidate))
    {
        return Error(asset::DecodeErrorCode::DuplicateReference,
                     "H.262 repeated sequence headers publish inconsistent facts");
    }
    ++facts->sequence_header_count;
    return std::nullopt;
}
} // namespace

MpegVideoElementaryStreamView::const_iterator::const_iterator(
    const std::byte* const source, const MpegVideoElementaryStreamPayloadRange* const ranges,
    const std::size_t range_count, const std::size_t index) noexcept
    : source_(source), ranges_(ranges), range_count_(range_count), index_(index)
{
}

MpegVideoElementaryStreamView::const_iterator::value_type MpegVideoElementaryStreamView::
    const_iterator::operator*() const noexcept
{
    const auto& range = ranges_[index_];
    return MpegVideoElementaryStreamChunk{
        .payload =
            std::span<const std::byte>(source_ + static_cast<std::size_t>(range.source_offset),
                                       static_cast<std::size_t>(range.byte_count)),
        .presentation_timestamp_90khz = range.presentation_timestamp_90khz,
        .source_offset = range.source_offset,
    };
}

MpegVideoElementaryStreamView::const_iterator& MpegVideoElementaryStreamView::const_iterator::
operator++() noexcept
{
    ++index_;
    return *this;
}

MpegVideoElementaryStreamView::const_iterator MpegVideoElementaryStreamView::const_iterator::
operator++(int) noexcept
{
    const_iterator previous = *this;
    ++(*this);
    return previous;
}

MpegVideoElementaryStreamView::MpegVideoElementaryStreamView(
    const std::span<const std::byte> source,
    const std::span<const MpegVideoElementaryStreamPayloadRange> ranges,
    const std::uint8_t stream_id, const std::uint64_t total_payload_bytes) noexcept
    : source_(source), ranges_(ranges), stream_id_(stream_id),
      total_payload_bytes_(total_payload_bytes)
{
}

MpegVideoElementaryStreamView::const_iterator MpegVideoElementaryStreamView::begin() const noexcept
{
    return const_iterator(source_.data(), ranges_.data(), ranges_.size(), 0);
}

MpegVideoElementaryStreamView::const_iterator MpegVideoElementaryStreamView::end() const noexcept
{
    return const_iterator(source_.data(), ranges_.data(), ranges_.size(), ranges_.size());
}

std::size_t MpegVideoElementaryStreamView::size() const noexcept
{
    return ranges_.size();
}

bool MpegVideoElementaryStreamView::empty() const noexcept
{
    return ranges_.empty();
}

std::uint8_t MpegVideoElementaryStreamView::stream_id() const noexcept
{
    return stream_id_;
}

std::uint64_t MpegVideoElementaryStreamView::total_payload_bytes() const noexcept
{
    return total_payload_bytes_;
}

asset::DecodeResult<MpegVideoElementaryStreamPlan> BuildMpegVideoElementaryStreamPlan(
    const std::span<const std::byte> source, const MpegProgramStreamDescriptor& descriptor,
    const std::optional<std::uint8_t> requested_stream_id, const asset::DecodeLimits limits)
{
    if (requested_stream_id && !IsVideoStreamId(*requested_stream_id))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "MPEG video ES requested stream_id is outside E0-EF"));
    }

    auto validation = ValidateDescriptor(source, descriptor, limits);
    if (!validation)
        return std::unexpected(validation.error());

    std::uint8_t selected_stream_id = 0;
    if (requested_stream_id)
    {
        const std::uint16_t requested_bit =
            static_cast<std::uint16_t>(1U << (*requested_stream_id - 0xE0U));
        if ((validation->video_stream_mask & requested_bit) == 0)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "MPEG video ES requested stream_id is missing"));
        }
        selected_stream_id = *requested_stream_id;
    }
    else
    {
        const int stream_count = std::popcount(validation->video_stream_mask);
        if (stream_count == 0)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "MPEG video ES descriptor has no video stream"));
        }
        if (stream_count != 1)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::DuplicateReference,
                                         "MPEG video ES automatic selection is ambiguous"));
        }
        selected_stream_id =
            static_cast<std::uint8_t>(0xE0U + std::countr_zero(validation->video_stream_mask));
    }

    const std::uint64_t selected_count =
        validation->video_payload_counts[selected_stream_id - 0xE0U];
    auto budget = ValidatePlanBudget(source.size(), selected_count, limits);
    if (!budget)
        return std::unexpected(budget.error());

    MpegVideoElementaryStreamPlan plan{
        .stream_id = selected_stream_id,
        .source_byte_count = source.size(),
    };
    try
    {
        plan.payloads.reserve(static_cast<std::size_t>(selected_count));
        for (const auto& packet : descriptor.packets)
        {
            if (packet.kind != MpegProgramStreamPacketKind::Pes ||
                packet.payload_class != MpegProgramStreamPayloadClass::Video ||
                packet.stream_id != selected_stream_id)
            {
                continue;
            }
            std::uint64_t next_total = 0;
            if (!Add(plan.total_payload_bytes, packet.payload_bytes, next_total))
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                             "MPEG video ES selected payload total overflows",
                                             packet.payload_offset));
            }
            plan.total_payload_bytes = next_total;
            plan.payloads.push_back(MpegVideoElementaryStreamPayloadRange{
                .source_offset = packet.payload_offset,
                .byte_count = packet.payload_bytes,
                .presentation_timestamp_90khz = packet.presentation_timestamp_90khz,
            });
        }
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "MPEG video ES plan allocation failed within configured limits"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "MPEG video ES plan range count exceeds container capacity"));
    }
    if (plan.payloads.size() != selected_count)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES descriptor changed during plan construction"));
    }
    return plan;
}

asset::DecodeResult<MpegVideoElementaryStreamView> BorrowMpegVideoElementaryStream(
    const MpegVideoElementaryStreamPlan& plan, const std::span<const std::byte> source,
    const asset::DecodeLimits limits)
{
    if (!IsVideoStreamId(plan.stream_id))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES plan stream_id is outside E0-EF"));
    }
    if (plan.source_byte_count != source.size())
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::InvalidReference,
                  "MPEG video ES plan source size does not match borrowed source"));
    }
    if (plan.payloads.empty())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES plan has no payload ranges"));
    }
    auto budget = ValidatePlanBudget(source.size(), plan.payloads.size(), limits);
    if (!budget)
        return std::unexpected(budget.error());

    std::uint64_t previous_end = 0;
    std::uint64_t total_payload_bytes = 0;
    for (const auto& range : plan.payloads)
    {
        if (range.source_offset < previous_end)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::InvalidReference,
                      "MPEG video ES plan payload ranges overlap or are out of order",
                      range.source_offset));
        }
        std::uint64_t range_end = 0;
        if (!Add(range.source_offset, range.byte_count, range_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "MPEG video ES plan payload range overflows",
                                         range.source_offset));
        }
        if (range_end > source.size())
        {
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                         "MPEG video ES plan payload reaches past borrowed source",
                                         range.source_offset));
        }
        if (range.presentation_timestamp_90khz &&
            *range.presentation_timestamp_90khz > kMpegTimestampMaximum90Khz)
        {
            return std::unexpected(Error(
                asset::DecodeErrorCode::InvalidReference,
                "MPEG video ES plan PTS is outside the 33-bit clock domain", range.source_offset));
        }
        if (!Add(total_payload_bytes, range.byte_count, total_payload_bytes))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "MPEG video ES plan payload total overflows",
                                         range.source_offset));
        }
        previous_end = range_end;
    }
    if (total_payload_bytes != plan.total_payload_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "MPEG video ES plan payload total is inconsistent"));
    }
    return MpegVideoElementaryStreamView(source, plan.payloads, plan.stream_id,
                                         plan.total_payload_bytes);
}

asset::DecodeResult<H262SequenceHeaderFacts> InspectH262SequenceHeaderFacts(
    const MpegVideoElementaryStreamView& stream, const asset::DecodeLimits limits)
{
    if (stream.total_payload_bytes() > kMpegProgramStreamMaximumInputBytes ||
        stream.total_payload_bytes() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "H.262 elementary stream exceeds an input-byte limit"));
    }
    if (limits.maximum_items == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "H.262 sequence-header scan has no root-item budget"));
    }
    if (sizeof(H262SequenceHeaderFacts) > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "H.262 sequence-header facts exceed the output-byte limit"));
    }

    ElementaryByteCursor cursor(stream);
    std::uint32_t window = 0;
    std::uint64_t byte_count = 0;
    std::uint64_t start_code_count = 0;
    std::optional<PendingH262Header> pending;
    std::optional<H262SequenceHeaderFacts> facts;
    std::uint8_t value = 0;
    while (cursor.Read(value))
    {
        ++byte_count;
        window = (window << 8U) | value;
        if (byte_count < 4U || (window & 0xFFFFFF00U) != 0x00000100U)
            continue;

        if (start_code_count >= kH262MaximumStartCodes ||
            start_code_count >= limits.maximum_items - 1U)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "H.262 start-code count exceeds a decoder limit",
                                         cursor.logical_offset() - 4U));
        }
        ++start_code_count;

        if (window == kSequenceHeaderStartCode)
        {
            if (pending)
            {
                if (auto merge_error = MergeH262Facts(*pending, facts))
                    return std::unexpected(std::move(*merge_error));
            }
            auto base = ParseH262BaseHeader(cursor);
            if (!base)
                return std::unexpected(base.error());
            pending = PendingH262Header{.base = *base};
        }
        else if (window == kExtensionStartCode)
        {
            ElementaryByteCursor extension_probe = cursor;
            std::uint8_t extension_first = 0;
            if (!extension_probe.Read(extension_first))
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                             "H.262 extension identifier is truncated",
                                             cursor.logical_offset()));
            }
            if ((extension_first >> 4U) != kSequenceExtensionId)
                continue;
            if (!pending)
            {
                return std::unexpected(
                    Error(asset::DecodeErrorCode::InvalidReference,
                          "H.262 sequence extension appears before a sequence header",
                          cursor.logical_offset() - 4U));
            }
            if (pending->extension)
            {
                return std::unexpected(
                    Error(asset::DecodeErrorCode::DuplicateReference,
                          "H.262 sequence header has multiple sequence extensions",
                          cursor.logical_offset() - 4U));
            }
            auto extension = ParseH262SequenceExtension(cursor);
            if (!extension)
                return std::unexpected(extension.error());
            pending->extension = *extension;
        }
    }

    if (pending)
    {
        if (auto merge_error = MergeH262Facts(*pending, facts))
            return std::unexpected(std::move(*merge_error));
    }
    if (!facts)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                                     "H.262 elementary stream has no sequence header"));
    }
    return *facts;
}
} // namespace omega::media
