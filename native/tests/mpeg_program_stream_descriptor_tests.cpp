#include "omega/media/mpeg_program_stream_descriptor.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using omega::asset::DecodeErrorCode;
using omega::media::MpegProgramStreamDescriptor;
using omega::media::MpegProgramStreamPacketKind;
using omega::media::MpegProgramStreamPayloadClass;

struct PacketLocation
{
    std::size_t packet_offset = 0;
    std::size_t payload_offset = 0;
};

struct CanonicalFixture
{
    std::vector<std::byte> bytes;
    PacketLocation video;
    PacketLocation audio;
    PacketLocation private_stream_1;
    PacketLocation private_stream_2;
};

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (condition)
        return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

template <typename Result>
void CheckError(const Result& result, const DecodeErrorCode code, const std::string_view message)
{
    if (result)
    {
        Check(false, message);
        return;
    }
    Check(result.error().code == code, message);
    Check(result.error().message.starts_with("MPEG-PS"),
        "MPEG-PS errors use the stable subsystem prefix");
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
        "MPEG-PS errors contain no filesystem path");
}

void AppendByte(std::vector<std::byte>& bytes, const std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void AppendStartCode(std::vector<std::byte>& bytes, const std::uint8_t stream_id)
{
    AppendByte(bytes, 0);
    AppendByte(bytes, 0);
    AppendByte(bytes, 1);
    AppendByte(bytes, stream_id);
}

void AppendU16BigEndian(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    AppendByte(bytes, static_cast<std::uint8_t>(value >> 8U));
    AppendByte(bytes, static_cast<std::uint8_t>(value & 0xFFU));
}

void AppendPackHeader(std::vector<std::byte>& bytes, const std::uint8_t stuffing_bytes = 0)
{
    AppendStartCode(bytes, 0xBA);
    // Project-generated MPEG-2 pack header with all required marker bits and neutral zero-valued
    // clock/mux-rate fields. No owner bytes participate in this fixture.
    constexpr std::array<std::uint8_t, 9> fixed{{
        0x44, 0x00, 0x04, 0x00, 0x04, 0x01, 0x00, 0x00, 0x03,
    }};
    for (const std::uint8_t value : fixed)
        AppendByte(bytes, value);
    AppendByte(bytes, static_cast<std::uint8_t>(0xF8U | stuffing_bytes));
    for (std::uint8_t index = 0; index < stuffing_bytes; ++index)
        AppendByte(bytes, 0xFF);
}

PacketLocation AppendLengthDelimited(std::vector<std::byte>& bytes, const std::uint8_t stream_id,
    const std::span<const std::uint8_t> payload)
{
    const PacketLocation location{.packet_offset = bytes.size(), .payload_offset = bytes.size() + 6U};
    AppendStartCode(bytes, stream_id);
    AppendU16BigEndian(bytes, static_cast<std::uint16_t>(payload.size()));
    for (const std::uint8_t value : payload)
        AppendByte(bytes, value);
    return location;
}

void AppendTimestamp(std::vector<std::byte>& bytes, const std::uint8_t prefix,
    const std::uint64_t timestamp_90khz)
{
    AppendByte(bytes, static_cast<std::uint8_t>(
                          (prefix << 4U) | ((timestamp_90khz >> 29U) & 0x0EU) | 1U));
    AppendByte(bytes, static_cast<std::uint8_t>(timestamp_90khz >> 22U));
    AppendByte(bytes,
        static_cast<std::uint8_t>(((timestamp_90khz >> 14U) & 0xFEU) | 1U));
    AppendByte(bytes, static_cast<std::uint8_t>(timestamp_90khz >> 7U));
    AppendByte(bytes, static_cast<std::uint8_t>(((timestamp_90khz << 1U) & 0xFEU) | 1U));
}

PacketLocation AppendPes(std::vector<std::byte>& bytes, const std::uint8_t stream_id,
    const std::span<const std::uint8_t> payload, const std::optional<std::uint64_t> pts = std::nullopt,
    const std::optional<std::uint64_t> dts = std::nullopt, const bool zero_length = false)
{
    const std::size_t packet_offset = bytes.size();
    AppendStartCode(bytes, stream_id);
    const std::uint8_t timestamp_bytes = dts ? 10U : (pts ? 5U : 0U);
    const std::uint16_t declared_bytes = zero_length
                                             ? 0
                                             : static_cast<std::uint16_t>(
                                                   3U + timestamp_bytes + payload.size());
    AppendU16BigEndian(bytes, declared_bytes);
    AppendByte(bytes, 0x80);
    AppendByte(bytes, dts ? 0xC0 : (pts ? 0x80 : 0x00));
    AppendByte(bytes, timestamp_bytes);
    if (pts)
        AppendTimestamp(bytes, dts ? 0x03U : 0x02U, *pts);
    if (dts)
        AppendTimestamp(bytes, 0x01U, *dts);
    const std::size_t payload_offset = bytes.size();
    for (const std::uint8_t value : payload)
        AppendByte(bytes, value);
    return PacketLocation{.packet_offset = packet_offset, .payload_offset = payload_offset};
}

[[nodiscard]] CanonicalFixture BuildCanonical()
{
    CanonicalFixture fixture;
    AppendPackHeader(fixture.bytes, 2);
    constexpr std::array<std::uint8_t, 3> system_header{{0x80, 0x00, 0x01}};
    AppendLengthDelimited(fixture.bytes, 0xBB, system_header);
    constexpr std::array<std::uint8_t, 2> program_map{{0x12, 0x34}};
    AppendLengthDelimited(fixture.bytes, 0xBC, program_map);
    constexpr std::array<std::uint8_t, 4> video_payload{{0x11, 0x22, 0x33, 0x44}};
    fixture.video = AppendPes(fixture.bytes, 0xE0, video_payload, 90'123U);
    constexpr std::array<std::uint8_t, 2> audio_payload{{0x55, 0x66}};
    fixture.audio = AppendPes(fixture.bytes, 0xC1, audio_payload, 180'456U, 179'000U);
    constexpr std::array<std::uint8_t, 3> private_payload{{0x77, 0x88, 0x99}};
    fixture.private_stream_1 = AppendPes(fixture.bytes, 0xBD, private_payload);
    constexpr std::array<std::uint8_t, 2> private_stream_2_payload{{0xA1, 0xB2}};
    fixture.private_stream_2 =
        AppendLengthDelimited(fixture.bytes, 0xBF, private_stream_2_payload);
    constexpr std::array<std::uint8_t, 4> padding{{0xFF, 0xFF, 0xFF, 0xFF}};
    AppendLengthDelimited(fixture.bytes, 0xBE, padding);
    AppendStartCode(fixture.bytes, 0xB9);
    return fixture;
}

[[nodiscard]] std::vector<std::byte> BuildPackOnly()
{
    std::vector<std::byte> bytes;
    AppendPackHeader(bytes);
    return bytes;
}

void RunCanonicalChecks()
{
    const CanonicalFixture fixture = BuildCanonical();
    const auto result = omega::media::InspectMpegProgramStream(fixture.bytes);
    Check(result.has_value(), "MPEG-PS accepts the canonical generated stream");
    if (!result)
        return;

    const MpegProgramStreamDescriptor& descriptor = *result;
    Check(descriptor.physical_byte_count == fixture.bytes.size() &&
              descriptor.packets.size() == 9 && descriptor.pack_header_count == 1 &&
              descriptor.pes_packet_count == 3 && descriptor.audio_pes_packet_count == 1 &&
              descriptor.video_pes_packet_count == 1 &&
              descriptor.private_data_packet_count == 2 && descriptor.has_program_end,
        "MPEG-PS publishes exact bounded packet and stream-class counts");

    const std::array expected_kinds{
        MpegProgramStreamPacketKind::PackHeader,
        MpegProgramStreamPacketKind::SystemHeader,
        MpegProgramStreamPacketKind::ProgramStreamMap,
        MpegProgramStreamPacketKind::Pes,
        MpegProgramStreamPacketKind::Pes,
        MpegProgramStreamPacketKind::Pes,
        MpegProgramStreamPacketKind::OpaqueLengthDelimited,
        MpegProgramStreamPacketKind::Padding,
        MpegProgramStreamPacketKind::ProgramEnd,
    };
    bool kinds_match = descriptor.packets.size() == expected_kinds.size();
    for (std::size_t index = 0; kinds_match && index < expected_kinds.size(); ++index)
        kinds_match = descriptor.packets[index].kind == expected_kinds[index];
    Check(kinds_match, "MPEG-PS preserves source-order systems/PES packet kinds");

    const auto& video = descriptor.packets[3];
    Check(video.stream_id == 0xE0 &&
              video.payload_class == MpegProgramStreamPayloadClass::Video &&
              video.packet_offset == fixture.video.packet_offset &&
              video.payload_offset == fixture.video.payload_offset && video.payload_bytes == 4 &&
              video.presentation_timestamp_90khz == 90'123U &&
              !video.decoding_timestamp_90khz && !video.zero_length_pes,
        "MPEG-PS describes video payload boundaries and a validated PTS without decoding bytes");

    const auto& audio = descriptor.packets[4];
    Check(audio.stream_id == 0xC1 &&
              audio.payload_class == MpegProgramStreamPayloadClass::Audio &&
              audio.packet_offset == fixture.audio.packet_offset &&
              audio.payload_offset == fixture.audio.payload_offset && audio.payload_bytes == 2 &&
              audio.presentation_timestamp_90khz == 180'456U &&
              audio.decoding_timestamp_90khz == 179'000U,
        "MPEG-PS describes audio payload boundaries and validated PTS/DTS values");

    const auto& private_1 = descriptor.packets[5];
    const auto& private_2 = descriptor.packets[6];
    Check(private_1.payload_class == MpegProgramStreamPayloadClass::PrivateData &&
              private_1.kind == MpegProgramStreamPacketKind::Pes &&
              private_1.payload_offset == fixture.private_stream_1.payload_offset &&
              private_2.payload_class == MpegProgramStreamPayloadClass::PrivateData &&
              private_2.kind == MpegProgramStreamPacketKind::OpaqueLengthDelimited &&
              private_2.payload_offset == fixture.private_stream_2.payload_offset,
        "MPEG-PS distinguishes private PES framing from opaque private-stream-2 framing");

    const auto repeated = omega::media::InspectMpegProgramStream(fixture.bytes);
    Check(repeated && *repeated == descriptor,
        "MPEG-PS inspection is deterministic for identical input");

    std::vector<std::byte> unaligned_storage(fixture.bytes.size() + 1U, std::byte{0xA5});
    std::copy(fixture.bytes.begin(), fixture.bytes.end(), unaligned_storage.begin() + 1);
    const auto unaligned = omega::media::InspectMpegProgramStream(
        std::span<const std::byte>(unaligned_storage.data() + 1, fixture.bytes.size()));
    Check(unaligned && *unaligned == descriptor,
        "MPEG-PS accepts an unaligned backing span without changing the descriptor");

    const auto owned = [&]() {
        auto transient = BuildCanonical().bytes;
        auto decoded = omega::media::InspectMpegProgramStream(transient);
        transient.assign(transient.size(), std::byte{0xFF});
        transient.clear();
        transient.shrink_to_fit();
        return decoded;
    }();
    Check(owned && *owned == descriptor,
        "MPEG-PS descriptor remains owned after source replacement and destruction");
}

void RunPayloadClassBoundaryChecks()
{
    std::vector<std::byte> bytes;
    AppendPackHeader(bytes);
    constexpr std::array<std::uint8_t, 1> payload{{0x5A}};
    AppendPes(bytes, 0xDF, payload);
    AppendPes(bytes, 0xEF, payload);
    AppendLengthDelimited(bytes, 0xF0, payload);
    AppendStartCode(bytes, 0xB9);

    const auto descriptor = omega::media::InspectMpegProgramStream(bytes);
    Check(descriptor && descriptor->packets.size() == 5U,
        "MPEG-PS accepts generated stream-ID upper-bound fixtures");
    if (!descriptor || descriptor->packets.size() != 5U)
        return;

    Check(descriptor->packets[1].stream_id == 0xDF &&
              descriptor->packets[1].payload_class == MpegProgramStreamPayloadClass::Audio,
        "MPEG-PS classifies 0xDF as the inclusive audio stream upper bound");
    Check(descriptor->packets[2].stream_id == 0xEF &&
              descriptor->packets[2].payload_class == MpegProgramStreamPayloadClass::Video,
        "MPEG-PS classifies 0xEF as the inclusive video stream upper bound");
    Check(descriptor->packets[3].stream_id == 0xF0 &&
              descriptor->packets[3].payload_class == MpegProgramStreamPayloadClass::Other &&
              descriptor->packets[3].kind ==
                  MpegProgramStreamPacketKind::OpaqueLengthDelimited,
        "MPEG-PS classifies adjacent stream 0xF0 outside the video range");
    Check(descriptor->audio_pes_packet_count == 1U &&
              descriptor->video_pes_packet_count == 1U,
        "MPEG-PS boundary classifications contribute to exact audio/video counts");
}

void RunZeroLengthVideoChecks()
{
    std::vector<std::byte> bytes;
    AppendPackHeader(bytes);
    constexpr std::array<std::uint8_t, 11> video_payload{{
        0x10, 0x00, 0x00, 0x01, 0xB3, 0x20, 0x00, 0x00, 0x01, 0xB8, 0x30,
    }};
    const PacketLocation video = AppendPes(bytes, 0xE2, video_payload, 45'000U, std::nullopt, true);
    const std::size_t second_pack_offset = bytes.size();
    AppendPackHeader(bytes, 1);
    AppendStartCode(bytes, 0xB9);

    const auto result = omega::media::InspectMpegProgramStream(bytes);
    Check(result && result->packets.size() == 4 && result->pack_header_count == 2 &&
              result->video_pes_packet_count == 1,
        "MPEG-PS accepts a zero-length video PES bounded by the next systems packet");
    if (result)
    {
        const auto& packet = result->packets[1];
        Check(packet.zero_length_pes && packet.packet_offset == video.packet_offset &&
                  packet.packet_bytes == second_pack_offset - video.packet_offset &&
                  packet.payload_offset == video.payload_offset &&
                  packet.payload_bytes == video_payload.size(),
            "MPEG-PS ignores elementary-layer start codes while finding a zero-length PES extent");
    }

    std::vector<std::byte> eof_bounded;
    AppendPackHeader(eof_bounded);
    AppendPes(eof_bounded, 0xE0, video_payload, std::nullopt, std::nullopt, true);
    const auto eof_result = omega::media::InspectMpegProgramStream(eof_bounded);
    Check(eof_result && eof_result->packets.size() == 2 && !eof_result->has_program_end &&
              eof_result->packets[1].payload_bytes == video_payload.size(),
        "MPEG-PS permits a packet-aligned EOF to bound the final zero-length video PES");

    std::vector<std::byte> packet_aligned = BuildPackOnly();
    constexpr std::array<std::uint8_t, 1> payload{{0x42}};
    AppendPes(packet_aligned, 0xC0, payload);
    const auto packet_aligned_result = omega::media::InspectMpegProgramStream(packet_aligned);
    Check(packet_aligned_result && packet_aligned_result->packets.size() == 2 &&
              !packet_aligned_result->has_program_end,
        "MPEG-PS reports packet-aligned input without inventing a program-end marker");
}

void RunTrailingZeroPaddingChecks()
{
    const CanonicalFixture canonical = BuildCanonical();
    constexpr std::array<std::size_t, 3> accepted_sizes{{1U, 2'044U, 2'047U}};
    for (const std::size_t padding_bytes : accepted_sizes)
    {
        auto padded = canonical.bytes;
        padded.insert(padded.end(), padding_bytes, std::byte{0});
        const auto result = omega::media::InspectMpegProgramStream(padded);
        Check(result && result->packets.size() == 10U && result->has_program_end,
            "MPEG-PS accepts a bounded zero optical-sector remainder after program end");
        if (result)
        {
            const auto& padding = result->packets.back();
            Check(padding.kind == MpegProgramStreamPacketKind::TrailingZeroPadding &&
                      padding.payload_class == MpegProgramStreamPayloadClass::None &&
                      padding.stream_id == 0U && padding.packet_offset == canonical.bytes.size() &&
                      padding.packet_bytes == padding_bytes &&
                      padding.payload_offset == canonical.bytes.size() &&
                      padding.payload_bytes == padding_bytes,
                "MPEG-PS describes terminal zero fill as a distinct exact extent");
        }
    }

    auto nonzero = canonical.bytes;
    nonzero.insert(nonzero.end(), 8U, std::byte{0});
    nonzero[canonical.bytes.size() + 3U] = std::byte{1};
    CheckError(omega::media::InspectMpegProgramStream(nonzero), DecodeErrorCode::Malformed,
        "MPEG-PS rejects a nonzero byte in terminal optical-sector fill");

    auto oversized = canonical.bytes;
    oversized.insert(oversized.end(), 2'048U, std::byte{0});
    CheckError(omega::media::InspectMpegProgramStream(oversized), DecodeErrorCode::LimitExceeded,
        "MPEG-PS rejects a full sector of bytes after program end");

    auto no_end = BuildPackOnly();
    no_end.insert(no_end.end(), 4U, std::byte{0});
    CheckError(omega::media::InspectMpegProgramStream(no_end), DecodeErrorCode::Malformed,
        "MPEG-PS rejects zero fill without a preceding program end");

    auto duplicate_end = canonical.bytes;
    AppendStartCode(duplicate_end, 0xB9);
    CheckError(omega::media::InspectMpegProgramStream(duplicate_end), DecodeErrorCode::Malformed,
        "MPEG-PS rejects a second program end as terminal padding");

    auto packet_after_end = canonical.bytes;
    AppendPackHeader(packet_after_end);
    CheckError(omega::media::InspectMpegProgramStream(packet_after_end),
        DecodeErrorCode::Malformed, "MPEG-PS rejects a systems packet after program end");
}

void RunTruncationChecks()
{
    const CanonicalFixture canonical = BuildCanonical();
    for (std::size_t size = 0; size < 4U; ++size)
    {
        CheckError(omega::media::InspectMpegProgramStream(
                       std::span<const std::byte>(canonical.bytes.data(), size)),
            DecodeErrorCode::Truncated, "MPEG-PS rejects a truncated initial start code");
    }
    for (std::size_t size = 4; size < 14U; ++size)
    {
        CheckError(omega::media::InspectMpegProgramStream(
                       std::span<const std::byte>(canonical.bytes.data(), size)),
            DecodeErrorCode::Truncated, "MPEG-PS rejects every truncated fixed pack header");
    }

    std::vector<std::byte> missing_stuffing;
    AppendPackHeader(missing_stuffing, 2);
    missing_stuffing.pop_back();
    CheckError(omega::media::InspectMpegProgramStream(missing_stuffing), DecodeErrorCode::Truncated,
        "MPEG-PS rejects truncated pack stuffing");

    std::vector<std::byte> missing_length = BuildPackOnly();
    AppendStartCode(missing_length, 0xC0);
    CheckError(omega::media::InspectMpegProgramStream(missing_length), DecodeErrorCode::Truncated,
        "MPEG-PS rejects a PES packet with no declared-length field");
    AppendByte(missing_length, 0);
    CheckError(omega::media::InspectMpegProgramStream(missing_length), DecodeErrorCode::Truncated,
        "MPEG-PS rejects a PES packet with a one-byte declared-length field");

    std::vector<std::byte> declared_past_eof = BuildPackOnly();
    AppendStartCode(declared_past_eof, 0xC0);
    AppendU16BigEndian(declared_past_eof, 10);
    CheckError(omega::media::InspectMpegProgramStream(declared_past_eof),
        DecodeErrorCode::Truncated, "MPEG-PS rejects a declared packet that reaches past EOF");

    std::vector<std::byte> short_optional_header = BuildPackOnly();
    AppendStartCode(short_optional_header, 0xC0);
    AppendU16BigEndian(short_optional_header, 2);
    AppendByte(short_optional_header, 0x80);
    AppendByte(short_optional_header, 0x00);
    CheckError(omega::media::InspectMpegProgramStream(short_optional_header),
        DecodeErrorCode::Truncated, "MPEG-PS rejects a truncated PES optional header");

    std::vector<std::byte> optional_data_past_packet = BuildPackOnly();
    AppendStartCode(optional_data_past_packet, 0xC0);
    AppendU16BigEndian(optional_data_past_packet, 4);
    AppendByte(optional_data_past_packet, 0x80);
    AppendByte(optional_data_past_packet, 0x80);
    AppendByte(optional_data_past_packet, 5);
    AppendByte(optional_data_past_packet, 0x21);
    CheckError(omega::media::InspectMpegProgramStream(optional_data_past_packet),
        DecodeErrorCode::Truncated,
        "MPEG-PS rejects optional PES data that reaches past the declared packet");
}

void RunMalformedChecks()
{
    const CanonicalFixture canonical = BuildCanonical();
    for (std::size_t index = 0; index < 3U; ++index)
    {
        auto malformed = canonical.bytes;
        malformed[index] ^= std::byte{0xFF};
        CheckError(omega::media::InspectMpegProgramStream(malformed), DecodeErrorCode::Malformed,
            "MPEG-PS rejects every malformed initial start-code-prefix byte");
    }

    std::vector<std::byte> pes_first;
    constexpr std::array<std::uint8_t, 1> one_byte{{0x42}};
    AppendPes(pes_first, 0xE0, one_byte);
    CheckError(omega::media::InspectMpegProgramStream(pes_first), DecodeErrorCode::Malformed,
        "MPEG-PS requires the first packet to be a pack header");

    auto mpeg1_pack = canonical.bytes;
    mpeg1_pack[4] = std::byte{0x21};
    CheckError(omega::media::InspectMpegProgramStream(mpeg1_pack),
        DecodeErrorCode::UnsupportedVariant,
        "MPEG-PS rejects MPEG-1 pack syntax without treating it as MPEG-2");

    constexpr std::array<std::size_t, 6> marker_offsets{{4, 6, 8, 9, 12, 13}};
    for (const std::size_t offset : marker_offsets)
    {
        auto malformed = canonical.bytes;
        if (offset == 4 || offset == 6 || offset == 8)
            malformed[offset] &= std::byte{0xFB};
        else if (offset == 9)
            malformed[offset] &= std::byte{0xFE};
        else if (offset == 12)
            malformed[offset] &= std::byte{0xFC};
        else
            malformed[offset] = std::byte{0x00};
        CheckError(omega::media::InspectMpegProgramStream(malformed), DecodeErrorCode::Malformed,
            "MPEG-PS rejects each malformed pack-header marker group");
    }

    auto dirty_pack_stuffing = canonical.bytes;
    dirty_pack_stuffing[14] = std::byte{0};
    CheckError(omega::media::InspectMpegProgramStream(dirty_pack_stuffing),
        DecodeErrorCode::Malformed, "MPEG-PS rejects non-FF pack stuffing");

    std::vector<std::byte> elementary_at_boundary = BuildPackOnly();
    AppendStartCode(elementary_at_boundary, 0xB3);
    CheckError(omega::media::InspectMpegProgramStream(elementary_at_boundary),
        DecodeErrorCode::Malformed,
        "MPEG-PS rejects an elementary-layer start code at a systems packet boundary");

    auto dirty_padding = canonical.bytes;
    const std::size_t padding_payload = dirty_padding.size() - 8U;
    dirty_padding[padding_payload] = std::byte{0};
    CheckError(omega::media::InspectMpegProgramStream(dirty_padding), DecodeErrorCode::Malformed,
        "MPEG-PS rejects a non-FF padding-stream byte");

    auto mpeg1_pes = canonical.bytes;
    mpeg1_pes[canonical.video.packet_offset + 6U] = std::byte{0};
    CheckError(omega::media::InspectMpegProgramStream(mpeg1_pes),
        DecodeErrorCode::UnsupportedVariant,
        "MPEG-PS rejects non-MPEG-2 PES optional-header syntax");

    auto reserved_timestamp_flags = canonical.bytes;
    reserved_timestamp_flags[canonical.video.packet_offset + 7U] = std::byte{0x40};
    CheckError(omega::media::InspectMpegProgramStream(reserved_timestamp_flags),
        DecodeErrorCode::Malformed, "MPEG-PS rejects the reserved PES timestamp flag pattern");

    auto short_timestamp_fields = canonical.bytes;
    short_timestamp_fields[canonical.video.packet_offset + 8U] = std::byte{4};
    CheckError(omega::media::InspectMpegProgramStream(short_timestamp_fields),
        DecodeErrorCode::Malformed,
        "MPEG-PS rejects PTS flags without five bytes of optional timestamp data");

    auto bad_timestamp_prefix = canonical.bytes;
    bad_timestamp_prefix[canonical.video.packet_offset + 9U] = std::byte{0x11};
    CheckError(omega::media::InspectMpegProgramStream(bad_timestamp_prefix),
        DecodeErrorCode::Malformed, "MPEG-PS rejects an invalid PTS prefix");

    auto bad_timestamp_marker = canonical.bytes;
    bad_timestamp_marker[canonical.video.packet_offset + 11U] &= std::byte{0xFE};
    CheckError(omega::media::InspectMpegProgramStream(bad_timestamp_marker),
        DecodeErrorCode::Malformed, "MPEG-PS rejects a missing PTS marker bit");

    std::vector<std::byte> zero_length_audio = BuildPackOnly();
    AppendStartCode(zero_length_audio, 0xC0);
    AppendU16BigEndian(zero_length_audio, 0);
    AppendByte(zero_length_audio, 0x80);
    AppendByte(zero_length_audio, 0);
    AppendByte(zero_length_audio, 0);
    CheckError(omega::media::InspectMpegProgramStream(zero_length_audio),
        DecodeErrorCode::Malformed, "MPEG-PS limits a zero PES length to video stream IDs");

}

void RunBudgetChecks()
{
    const CanonicalFixture fixture = BuildCanonical();
    const auto baseline = omega::media::InspectMpegProgramStream(fixture.bytes);
    Check(baseline.has_value(), "MPEG-PS budget fixture decodes at baseline");
    const std::uint64_t packet_count = baseline ? baseline->packets.size() : 0;

    auto limits = omega::media::DefaultMpegProgramStreamDecodeLimits();
    limits.maximum_input_bytes = fixture.bytes.size();
    Check(omega::media::InspectMpegProgramStream(fixture.bytes, limits).has_value(),
        "MPEG-PS succeeds at the exact caller input-byte budget");
    --limits.maximum_input_bytes;
    CheckError(omega::media::InspectMpegProgramStream(fixture.bytes, limits),
        DecodeErrorCode::LimitExceeded,
        "MPEG-PS rejects one byte below the required caller input budget");

    limits = omega::media::DefaultMpegProgramStreamDecodeLimits();
    limits.maximum_items = 1U + packet_count;
    Check(omega::media::InspectMpegProgramStream(fixture.bytes, limits).has_value(),
        "MPEG-PS succeeds at the exact root-plus-packets item budget");
    --limits.maximum_items;
    CheckError(omega::media::InspectMpegProgramStream(fixture.bytes, limits),
        DecodeErrorCode::LimitExceeded,
        "MPEG-PS rejects one item below the root-plus-packets budget");
    limits.maximum_items = 0;
    CheckError(omega::media::InspectMpegProgramStream(fixture.bytes, limits),
        DecodeErrorCode::LimitExceeded, "MPEG-PS rejects a zero root-item budget");

    const std::uint64_t output_bytes = sizeof(MpegProgramStreamDescriptor) +
        packet_count * sizeof(omega::media::MpegProgramStreamPacketDescriptor);
    limits = omega::media::DefaultMpegProgramStreamDecodeLimits();
    limits.maximum_output_bytes = output_bytes;
    Check(omega::media::InspectMpegProgramStream(fixture.bytes, limits).has_value(),
        "MPEG-PS succeeds at the exact owned-output budget");
    --limits.maximum_output_bytes;
    CheckError(omega::media::InspectMpegProgramStream(fixture.bytes, limits),
        DecodeErrorCode::LimitExceeded,
        "MPEG-PS rejects one byte below the required owned-output budget");
    limits.maximum_output_bytes = sizeof(MpegProgramStreamDescriptor) - 1U;
    CheckError(omega::media::InspectMpegProgramStream(fixture.bytes, limits),
        DecodeErrorCode::LimitExceeded,
        "MPEG-PS rejects an output budget below the fixed root descriptor size");

    limits = omega::media::DefaultMpegProgramStreamDecodeLimits();
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::media::InspectMpegProgramStream(fixture.bytes, limits).has_value(),
        "MPEG-PS uses zero dynamic scratch and nesting depth zero");
}
} // namespace

int main()
{
    static_assert(omega::media::kMpegProgramStreamMaximumInputBytes == 512ULL * 1024ULL * 1024ULL);
    static_assert(omega::media::kMpegProgramStreamMaximumPacketDescriptors == 262'144U);
    static_assert(omega::media::kMpegProgramStreamMaximumTrailingZeroPaddingBytes == 2'047U);
    static_assert(omega::media::DefaultMpegProgramStreamDecodeLimits().maximum_input_bytes ==
                  omega::media::kMpegProgramStreamMaximumInputBytes);

    RunCanonicalChecks();
    RunPayloadClassBoundaryChecks();
    RunZeroLengthVideoChecks();
    RunTrailingZeroPaddingChecks();
    RunTruncationChecks();
    RunMalformedChecks();
    RunBudgetChecks();

    if (failures != 0)
        std::cerr << failures << " MPEG-PS descriptor check(s) failed\n";
    return failures == 0 ? 0 : 1;
}
