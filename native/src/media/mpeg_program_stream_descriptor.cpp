#include "omega/media/mpeg_program_stream_descriptor.h"

#include <algorithm>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
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
constexpr std::uint64_t kMpeg2PackHeaderBytes = 14;
constexpr std::uint64_t kLengthDelimitedHeaderBytes = 6;
constexpr std::uint64_t kMpeg2PesHeaderBytes = 9;

struct ScanSummary
{
    std::uint64_t packet_count = 0;
    std::uint64_t pack_header_count = 0;
    std::uint64_t pes_packet_count = 0;
    std::uint64_t audio_pes_packet_count = 0;
    std::uint64_t video_pes_packet_count = 0;
    std::uint64_t private_data_packet_count = 0;
    bool has_program_end = false;
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

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] std::uint8_t ReadU8(
    const std::span<const std::byte> bytes, const std::uint64_t offset) noexcept
{
    return std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset)]);
}

[[nodiscard]] std::uint16_t ReadU16BigEndian(
    const std::span<const std::byte> bytes, const std::uint64_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(ReadU8(bytes, offset)) << 8U) |
        static_cast<std::uint16_t>(ReadU8(bytes, offset + 1U)));
}

[[nodiscard]] bool HasStartCodePrefix(
    const std::span<const std::byte> bytes, const std::uint64_t offset) noexcept
{
    return offset <= bytes.size() && bytes.size() - offset >= 4U &&
           ReadU8(bytes, offset) == 0 && ReadU8(bytes, offset + 1U) == 0 &&
           ReadU8(bytes, offset + 2U) == 1;
}

[[nodiscard]] bool IsAudioStreamId(const std::uint8_t stream_id) noexcept
{
    return stream_id >= 0xC0U && stream_id <= 0xDFU;
}

[[nodiscard]] bool IsVideoStreamId(const std::uint8_t stream_id) noexcept
{
    return stream_id >= 0xE0U && stream_id <= 0xEFU;
}

[[nodiscard]] bool HasMpeg2PesOptionalHeader(const std::uint8_t stream_id) noexcept
{
    switch (stream_id)
    {
    case kProgramStreamMapStreamId:
    case kPaddingStreamId:
    case kPrivateStream2Id:
    case 0xF0U: // ECM stream
    case 0xF1U: // EMM stream
    case 0xF2U: // DSM-CC stream
    case 0xF8U: // H.222.1 type E stream
    case 0xFFU: // program stream directory
        return false;
    default:
        return stream_id >= kPrivateStream1Id;
    }
}

[[nodiscard]] MpegProgramStreamPayloadClass ClassifyPayload(
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

[[nodiscard]] std::uint64_t FindNextSystemsStartCode(
    const std::span<const std::byte> bytes, const std::uint64_t begin) noexcept
{
    if (begin >= bytes.size())
        return bytes.size();

    for (std::uint64_t offset = begin; bytes.size() - offset >= 4U; ++offset)
    {
        if (HasStartCodePrefix(bytes, offset) &&
            ReadU8(bytes, offset + 3U) >= kProgramEndStreamId)
        {
            return offset;
        }
    }
    return bytes.size();
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> DecodeTimestamp(
    const std::span<const std::byte> bytes, const std::uint64_t offset,
    const std::uint8_t expected_prefix)
{
    const std::uint8_t first = ReadU8(bytes, offset);
    const std::uint8_t third = ReadU8(bytes, offset + 2U);
    const std::uint8_t fifth = ReadU8(bytes, offset + 4U);
    if ((first >> 4U) != expected_prefix)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS timestamp prefix is malformed", offset));
    }
    if ((first & 1U) == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS timestamp marker is missing", offset));
    }
    if ((third & 1U) == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS timestamp marker is missing", offset + 2U));
    }
    if ((fifth & 1U) == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS timestamp marker is missing", offset + 4U));
    }

    return (static_cast<std::uint64_t>((first >> 1U) & 0x07U) << 30U) |
           (static_cast<std::uint64_t>(ReadU8(bytes, offset + 1U)) << 22U) |
           (static_cast<std::uint64_t>((third >> 1U) & 0x7FU) << 15U) |
           (static_cast<std::uint64_t>(ReadU8(bytes, offset + 3U)) << 7U) |
           static_cast<std::uint64_t>((fifth >> 1U) & 0x7FU);
}

[[nodiscard]] asset::DecodeResult<MpegProgramStreamPacketDescriptor> ParsePackHeader(
    const std::span<const std::byte> bytes, const std::uint64_t offset)
{
    if (bytes.size() - offset < kMpeg2PackHeaderBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS pack header is truncated", bytes.size()));
    }

    const std::uint8_t first = ReadU8(bytes, offset + 4U);
    if ((first & 0xC0U) != 0x40U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "MPEG-PS pack header is not MPEG-2 syntax", offset + 4U));
    }
    const struct Marker
    {
        std::uint64_t relative_offset;
        std::uint8_t mask;
        std::uint8_t expected;
    } markers[]{{4U, 0x04U, 0x04U}, {6U, 0x04U, 0x04U}, {8U, 0x04U, 0x04U},
                {9U, 0x01U, 0x01U}, {12U, 0x03U, 0x03U}, {13U, 0xF8U, 0xF8U}};
    for (const Marker marker : markers)
    {
        if ((ReadU8(bytes, offset + marker.relative_offset) & marker.mask) != marker.expected)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "MPEG-PS pack-header marker is malformed", offset + marker.relative_offset));
        }
    }

    const std::uint64_t stuffing_bytes = ReadU8(bytes, offset + 13U) & 0x07U;
    std::uint64_t packet_end = 0;
    if (!Add(offset, kMpeg2PackHeaderBytes + stuffing_bytes, packet_end))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "MPEG-PS pack-header extent overflows", offset));
    }
    if (packet_end > bytes.size())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS pack stuffing is truncated", bytes.size()));
    }
    for (std::uint64_t stuffing_offset = offset + kMpeg2PackHeaderBytes;
         stuffing_offset < packet_end; ++stuffing_offset)
    {
        if (ReadU8(bytes, stuffing_offset) != 0xFFU)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "MPEG-PS pack stuffing byte is malformed", stuffing_offset));
        }
    }

    return MpegProgramStreamPacketDescriptor{
        .kind = MpegProgramStreamPacketKind::PackHeader,
        .payload_class = MpegProgramStreamPayloadClass::None,
        .stream_id = kPackHeaderStreamId,
        .packet_offset = offset,
        .packet_bytes = packet_end - offset,
        .payload_offset = packet_end,
        .payload_bytes = 0,
    };
}

[[nodiscard]] asset::DecodeResult<MpegProgramStreamPacketDescriptor> ParseLengthDelimitedPacket(
    const std::span<const std::byte> bytes, const std::uint64_t offset,
    const std::uint8_t stream_id)
{
    if (bytes.size() - offset < kLengthDelimitedHeaderBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS packet length is truncated", bytes.size()));
    }

    const std::uint64_t declared_payload_bytes = ReadU16BigEndian(bytes, offset + 4U);
    const bool has_pes_header = HasMpeg2PesOptionalHeader(stream_id);
    const bool zero_length_pes = has_pes_header && declared_payload_bytes == 0;
    if (zero_length_pes && !IsVideoStreamId(stream_id))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS zero packet length is limited to video PES", offset + 4U));
    }

    std::uint64_t packet_end = bytes.size();
    if (!zero_length_pes)
    {
        if (!Add(offset, kLengthDelimitedHeaderBytes + declared_payload_bytes, packet_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "MPEG-PS packet extent overflows", offset));
        }
        if (packet_end > bytes.size())
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                "MPEG-PS declared packet reaches past EOF", bytes.size()));
        }
    }

    MpegProgramStreamPacketDescriptor descriptor{
        .kind = MpegProgramStreamPacketKind::OpaqueLengthDelimited,
        .payload_class = MpegProgramStreamPayloadClass::None,
        .stream_id = stream_id,
        .packet_offset = offset,
        .packet_bytes = packet_end - offset,
        .payload_offset = offset + kLengthDelimitedHeaderBytes,
        .payload_bytes = zero_length_pes ? 0 : declared_payload_bytes,
        .zero_length_pes = zero_length_pes,
    };

    if (stream_id == kSystemHeaderStreamId)
    {
        descriptor.kind = MpegProgramStreamPacketKind::SystemHeader;
        return descriptor;
    }
    if (stream_id == kProgramStreamMapStreamId)
    {
        descriptor.kind = MpegProgramStreamPacketKind::ProgramStreamMap;
        return descriptor;
    }
    if (stream_id == kPaddingStreamId)
    {
        descriptor.kind = MpegProgramStreamPacketKind::Padding;
        for (std::uint64_t padding_offset = descriptor.payload_offset;
             padding_offset < packet_end; ++padding_offset)
        {
            if (ReadU8(bytes, padding_offset) != 0xFFU)
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "MPEG-PS padding-stream byte is malformed", padding_offset));
            }
        }
        return descriptor;
    }

    descriptor.payload_class = ClassifyPayload(stream_id);
    if (!has_pes_header)
        return descriptor;

    descriptor.kind = MpegProgramStreamPacketKind::Pes;
    const std::uint64_t available_end = zero_length_pes ? bytes.size() : packet_end;
    if (available_end - offset < kMpeg2PesHeaderBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS PES optional header is truncated", available_end));
    }
    if ((ReadU8(bytes, offset + 6U) & 0xC0U) != 0x80U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "MPEG-PS PES packet is not MPEG-2 syntax", offset + 6U));
    }

    const std::uint8_t flags = ReadU8(bytes, offset + 7U);
    const std::uint8_t timestamp_flags = flags & 0xC0U;
    if (timestamp_flags == 0x40U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS PES timestamp flags are reserved", offset + 7U));
    }

    const std::uint64_t optional_header_bytes = ReadU8(bytes, offset + 8U);
    std::uint64_t payload_offset = 0;
    if (!Add(offset, kMpeg2PesHeaderBytes + optional_header_bytes, payload_offset))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "MPEG-PS PES header extent overflows", offset + 8U));
    }
    if (payload_offset > available_end)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS PES optional data reaches past its packet", available_end));
    }

    const std::uint64_t required_timestamp_bytes =
        timestamp_flags == 0xC0U ? 10U : (timestamp_flags == 0x80U ? 5U : 0U);
    if (optional_header_bytes < required_timestamp_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS PES timestamp fields exceed optional data", offset + 8U));
    }
    if (timestamp_flags == 0x80U || timestamp_flags == 0xC0U)
    {
        auto timestamp = DecodeTimestamp(
            bytes, offset + kMpeg2PesHeaderBytes, timestamp_flags == 0x80U ? 0x02U : 0x03U);
        if (!timestamp)
            return std::unexpected(timestamp.error());
        descriptor.presentation_timestamp_90khz = *timestamp;
    }
    if (timestamp_flags == 0xC0U)
    {
        auto timestamp = DecodeTimestamp(bytes, offset + kMpeg2PesHeaderBytes + 5U, 0x01U);
        if (!timestamp)
            return std::unexpected(timestamp.error());
        descriptor.decoding_timestamp_90khz = *timestamp;
    }

    if (zero_length_pes)
        packet_end = FindNextSystemsStartCode(bytes, payload_offset);
    descriptor.packet_bytes = packet_end - offset;
    descriptor.payload_offset = payload_offset;
    descriptor.payload_bytes = packet_end - payload_offset;
    return descriptor;
}

[[nodiscard]] asset::DecodeResult<MpegProgramStreamPacketDescriptor> ParsePacket(
    const std::span<const std::byte> bytes, const std::uint64_t offset)
{
    if (bytes.size() - offset < 4U)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS start code is truncated", bytes.size()));
    }
    constexpr std::uint8_t prefix[]{0, 0, 1};
    for (std::uint64_t index = 0; index < 3U; ++index)
    {
        if (ReadU8(bytes, offset + index) != prefix[index])
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "MPEG-PS packet start-code prefix is malformed", offset + index));
        }
    }

    const std::uint8_t stream_id = ReadU8(bytes, offset + 3U);
    if (stream_id == kProgramEndStreamId)
    {
        return MpegProgramStreamPacketDescriptor{
            .kind = MpegProgramStreamPacketKind::ProgramEnd,
            .payload_class = MpegProgramStreamPayloadClass::None,
            .stream_id = stream_id,
            .packet_offset = offset,
            .packet_bytes = 4,
            .payload_offset = offset + 4U,
            .payload_bytes = 0,
        };
    }
    if (stream_id == kPackHeaderStreamId)
        return ParsePackHeader(bytes, offset);
    if (stream_id < kSystemHeaderStreamId)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "MPEG-PS elementary start code appears at packet boundary", offset + 3U));
    }
    return ParseLengthDelimitedPacket(bytes, offset, stream_id);
}

[[nodiscard]] asset::DecodeResult<MpegProgramStreamPacketDescriptor> ParseTrailingZeroPadding(
    const std::span<const std::byte> bytes, const std::uint64_t offset)
{
    const std::uint64_t padding_bytes = bytes.size() - offset;
    if (padding_bytes == 0U || padding_bytes > kMpegProgramStreamMaximumTrailingZeroPaddingBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS trailing zero padding exceeds the fixed optical-sector remainder limit",
            offset + kMpegProgramStreamMaximumTrailingZeroPaddingBytes));
    }
    for (std::uint64_t index = offset; index < bytes.size(); ++index)
    {
        if (ReadU8(bytes, index) != 0U)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "MPEG-PS trailing padding byte is nonzero", index));
        }
    }
    return MpegProgramStreamPacketDescriptor{
        .kind = MpegProgramStreamPacketKind::TrailingZeroPadding,
        .payload_class = MpegProgramStreamPayloadClass::None,
        .stream_id = 0U,
        .packet_offset = offset,
        .packet_bytes = padding_bytes,
        .payload_offset = offset,
        .payload_bytes = padding_bytes,
    };
}

[[nodiscard]] asset::DecodeResult<ScanSummary> ScanProgramStream(
    const std::span<const std::byte> bytes, const std::uint64_t maximum_packets,
    MpegProgramStreamPacketDescriptor* const output)
{
    ScanSummary summary;
    std::uint64_t offset = 0;
    while (offset < bytes.size())
    {
        auto packet = summary.has_program_end ? ParseTrailingZeroPadding(bytes, offset)
                                              : ParsePacket(bytes, offset);
        if (!packet)
            return std::unexpected(packet.error());
        if (summary.packet_count >= maximum_packets)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "MPEG-PS packet count exceeds a decoder limit", offset));
        }
        if (summary.packet_count == 0 &&
            packet->kind != MpegProgramStreamPacketKind::PackHeader)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "MPEG-PS input does not begin with a pack header", offset));
        }

        if (output != nullptr)
            output[static_cast<std::size_t>(summary.packet_count)] = *packet;
        ++summary.packet_count;

        switch (packet->kind)
        {
        case MpegProgramStreamPacketKind::PackHeader:
            ++summary.pack_header_count;
            break;
        case MpegProgramStreamPacketKind::Pes:
            ++summary.pes_packet_count;
            if (packet->payload_class == MpegProgramStreamPayloadClass::Audio)
                ++summary.audio_pes_packet_count;
            else if (packet->payload_class == MpegProgramStreamPayloadClass::Video)
                ++summary.video_pes_packet_count;
            break;
        case MpegProgramStreamPacketKind::ProgramEnd:
            summary.has_program_end = true;
            break;
        default:
            break;
        }
        if (packet->payload_class == MpegProgramStreamPayloadClass::PrivateData)
            ++summary.private_data_packet_count;

        if (packet->packet_bytes == 0)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "MPEG-PS packet has an empty physical extent", offset));
        }
        if (!Add(offset, packet->packet_bytes, offset))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "MPEG-PS packet traversal overflows"));
        }
    }

    if (summary.pack_header_count == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "MPEG-PS input contains no complete pack header", bytes.size()));
    }
    return summary;
}
} // namespace

asset::DecodeResult<MpegProgramStreamDescriptor> InspectMpegProgramStream(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > kMpegProgramStreamMaximumInputBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS input exceeds the fixed byte ceiling"));
    }
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS input exceeds the caller byte limit"));
    }
    if (limits.maximum_items == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS root exceeds the caller item limit"));
    }
    if (limits.maximum_output_bytes < sizeof(MpegProgramStreamDescriptor))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS root exceeds the caller output limit"));
    }

    const std::uint64_t maximum_by_items = limits.maximum_items - 1U;
    const std::uint64_t maximum_by_output =
        (limits.maximum_output_bytes - sizeof(MpegProgramStreamDescriptor)) /
        sizeof(MpegProgramStreamPacketDescriptor);
    const std::uint64_t maximum_packets = std::min(
        kMpegProgramStreamMaximumPacketDescriptors, std::min(maximum_by_items, maximum_by_output));

    auto scan = ScanProgramStream(bytes, maximum_packets, nullptr);
    if (!scan)
        return std::unexpected(scan.error());

    try
    {
        std::vector<MpegProgramStreamPacketDescriptor> packets(
            static_cast<std::size_t>(scan->packet_count));
        auto populated = ScanProgramStream(bytes, maximum_packets, packets.data());
        if (!populated)
            return std::unexpected(populated.error());

        return MpegProgramStreamDescriptor{
            .packets = std::move(packets),
            .physical_byte_count = bytes.size(),
            .pack_header_count = populated->pack_header_count,
            .pes_packet_count = populated->pes_packet_count,
            .audio_pes_packet_count = populated->audio_pes_packet_count,
            .video_pes_packet_count = populated->video_pes_packet_count,
            .private_data_packet_count = populated->private_data_packet_count,
            .has_program_end = populated->has_program_end,
        };
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS descriptor allocation failed"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "MPEG-PS descriptor allocation failed"));
    }
}
} // namespace omega::media
