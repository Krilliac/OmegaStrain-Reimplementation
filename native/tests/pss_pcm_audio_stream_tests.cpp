#include "omega/media/pss_pcm_audio_stream.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace
{
using omega::asset::DecodeErrorCode;
using omega::media::MpegProgramStreamDescriptor;

constexpr std::array<std::byte, 4> kPrefixA{std::byte{0xFF}, std::byte{0xA0}, std::byte{0x00},
                                            std::byte{0x00}};
constexpr std::array<std::byte, 4> kPrefixB{std::byte{0xFF}, std::byte{0xA1}, std::byte{0x00},
                                            std::byte{0x01}};

struct PacketLocation
{
    std::size_t payload_offset = 0;
    std::size_t content_bytes = 0;
};

struct Program
{
    std::vector<std::byte> bytes;
    std::vector<PacketLocation> audio_packets;
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
    Check(result.error().message.starts_with("PSS PCM"),
          "audio errors use a stable subsystem prefix");
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
          "audio errors contain no filesystem path");
}

void AppendByte(std::vector<std::byte>& bytes, const std::uint8_t value)
{
    bytes.push_back(static_cast<std::byte>(value));
}

void AppendU16BigEndian(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    AppendByte(bytes, static_cast<std::uint8_t>(value >> 8U));
    AppendByte(bytes, static_cast<std::uint8_t>(value));
}

void AppendU32LittleEndian(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    AppendByte(bytes, static_cast<std::uint8_t>(value));
    AppendByte(bytes, static_cast<std::uint8_t>(value >> 8U));
    AppendByte(bytes, static_cast<std::uint8_t>(value >> 16U));
    AppendByte(bytes, static_cast<std::uint8_t>(value >> 24U));
}

void AppendStartCode(std::vector<std::byte>& bytes, const std::uint8_t stream_id)
{
    AppendByte(bytes, 0);
    AppendByte(bytes, 0);
    AppendByte(bytes, 1);
    AppendByte(bytes, stream_id);
}

void AppendPackHeader(std::vector<std::byte>& bytes)
{
    AppendStartCode(bytes, 0xBA);
    constexpr std::array<std::uint8_t, 10> body{0x44, 0x00, 0x04, 0x00, 0x04,
                                                0x01, 0x00, 0x00, 0x03, 0xF8};
    for (const auto value : body)
        AppendByte(bytes, value);
}

void AppendTimestamp(std::vector<std::byte>& bytes, const std::uint64_t timestamp_90khz)
{
    AppendByte(bytes, static_cast<std::uint8_t>(0x20U | ((timestamp_90khz >> 29U) & 0x0EU) | 1U));
    AppendByte(bytes, static_cast<std::uint8_t>(timestamp_90khz >> 22U));
    AppendByte(bytes, static_cast<std::uint8_t>(((timestamp_90khz >> 14U) & 0xFEU) | 1U));
    AppendByte(bytes, static_cast<std::uint8_t>(timestamp_90khz >> 7U));
    AppendByte(bytes, static_cast<std::uint8_t>(((timestamp_90khz << 1U) & 0xFEU) | 1U));
}

PacketLocation AppendPrivatePes(std::vector<std::byte>& bytes,
                                const std::array<std::byte, 4> prefix,
                                const std::span<const std::byte> content,
                                const std::optional<std::uint64_t> pts)
{
    AppendStartCode(bytes, 0xBD);
    const std::uint16_t pes_bytes =
        static_cast<std::uint16_t>(3U + (pts ? 5U : 0U) + prefix.size() + content.size());
    AppendU16BigEndian(bytes, pes_bytes);
    AppendByte(bytes, 0x80);
    AppendByte(bytes, pts ? 0x80 : 0x00);
    AppendByte(bytes, pts ? 5U : 0U);
    if (pts)
        AppendTimestamp(bytes, *pts);
    const auto payload_offset = bytes.size();
    bytes.insert(bytes.end(), prefix.begin(), prefix.end());
    bytes.insert(bytes.end(), content.begin(), content.end());
    return PacketLocation{.payload_offset = payload_offset, .content_bytes = content.size()};
}

std::vector<std::byte> BuildLogicalAudio(
    const std::uint32_t encoding_tag = 1U, const std::uint32_t sample_rate = 48'000U,
    const std::uint32_t channel_count = 2U, const std::uint32_t interleave_bytes = 4U,
    const std::uint32_t loop_start = std::numeric_limits<std::uint32_t>::max(),
    const std::uint32_t loop_end = std::numeric_limits<std::uint32_t>::max())
{
    std::vector<std::byte> bytes;
    for (const char value : std::string_view{"SShd"})
        AppendByte(bytes, static_cast<std::uint8_t>(value));
    AppendU32LittleEndian(bytes, 24U);
    AppendU32LittleEndian(bytes, encoding_tag);
    AppendU32LittleEndian(bytes, sample_rate);
    AppendU32LittleEndian(bytes, channel_count);
    AppendU32LittleEndian(bytes, interleave_bytes);
    AppendU32LittleEndian(bytes, loop_start);
    AppendU32LittleEndian(bytes, loop_end);
    for (const char value : std::string_view{"SSbd"})
        AppendByte(bytes, static_cast<std::uint8_t>(value));

    // Two complete stereo interleave rounds. Each channel block has two signed LE
    // samples.
    constexpr std::array<std::int16_t, 8> block_ordered_samples{1, 2, -1, -2, 3, 4, -3, -4};
    AppendU32LittleEndian(
        bytes, static_cast<std::uint32_t>(block_ordered_samples.size() * sizeof(std::int16_t)));
    for (const auto sample : block_ordered_samples)
    {
        const auto bits = static_cast<std::uint16_t>(sample);
        AppendByte(bytes, static_cast<std::uint8_t>(bits));
        AppendByte(bytes, static_cast<std::uint8_t>(bits >> 8U));
    }
    return bytes;
}

Program BuildProgram(const std::span<const std::byte> logical,
                     const std::span<const std::size_t> chunk_sizes,
                     const std::array<std::byte, 4> prefix = kPrefixA)
{
    Program program;
    AppendPackHeader(program.bytes);
    std::size_t logical_offset = 0;
    std::uint64_t pts = 100U;
    for (const auto chunk_size : chunk_sizes)
    {
        const auto chunk = logical.subspan(logical_offset, chunk_size);
        program.audio_packets.push_back(AppendPrivatePes(program.bytes, prefix, chunk, pts));
        logical_offset += chunk_size;
        pts += 100U;
    }
    Check(logical_offset == logical.size(), "test chunk sizes cover the logical audio fixture");
    AppendStartCode(program.bytes, 0xB9);
    return program;
}

std::optional<MpegProgramStreamDescriptor> Describe(const std::span<const std::byte> bytes)
{
    auto descriptor = omega::media::InspectMpegProgramStream(bytes);
    Check(descriptor.has_value(), "generated audio fixture passes MPEG-PS framing inspection");
    if (!descriptor)
        return std::nullopt;
    return std::move(*descriptor);
}

void RunPlanAndDecodeChecks()
{
    const auto logical = BuildLogicalAudio();
    constexpr std::array<std::size_t, 3> chunks{5U, 38U, 13U};
    auto program = BuildProgram(logical, chunks);
    auto descriptor = Describe(program.bytes);
    if (!descriptor)
        return;

    auto plan = omega::media::BuildPssPcmAudioStreamPlan(program.bytes, *descriptor);
    Check(plan && plan->private_packet_prefix == kPrefixA &&
              plan->source_byte_count == program.bytes.size() && plan->sample_rate_hz == 48'000U &&
              plan->channel_count == 2U && plan->interleave_block_bytes == 4U &&
              plan->total_sample_bytes == 16U && plan->total_frame_count == 4U &&
              plan->first_packet_presentation_timestamp_90khz == 100U &&
              plan->last_packet_presentation_timestamp_90khz == 300U,
          "audio plan parses bounded format facts and packet-clock endpoints");
    if (!plan)
        return;
    Check(plan->payloads.size() == 2U && plan->payloads[0].sample_byte_offset == 0U &&
              plan->payloads[0].byte_count == 3U &&
              plan->payloads[0].presentation_timestamp_90khz == 200U &&
              plan->payloads[1].sample_byte_offset == 3U && plan->payloads[1].byte_count == 13U &&
              plan->payloads[1].presentation_timestamp_90khz == 300U,
          "audio plan strips framing while preserving cross-packet PCM ranges "
          "and PTS");

    std::array<std::int16_t, 8> decoded{};
    const auto decode = omega::media::DecodePssPcm16Interleaved(*plan, program.bytes, 0U, decoded);
    constexpr std::array<std::int16_t, 8> expected{1, -1, 2, -2, 3, -3, 4, -4};
    Check(decode && *decode == 4U && decoded == expected,
          "audio decode converts channel blocks to sample-major interleaved "
          "signed PCM");

    std::array<std::int16_t, 4> middle{};
    const auto middle_decode =
        omega::media::DecodePssPcm16Interleaved(*plan, program.bytes, 1U, middle);
    constexpr std::array<std::int16_t, 4> expected_middle{2, -2, 3, -3};
    Check(middle_decode && *middle_decode == 2U && middle == expected_middle,
          "audio decode supports an interval crossing channel-interleave rounds");
}

void RunSelectionChecks()
{
    const auto logical = BuildLogicalAudio();
    Program program;
    AppendPackHeader(program.bytes);
    AppendPrivatePes(program.bytes, kPrefixA, logical, 10U);
    AppendPrivatePes(program.bytes, kPrefixB, logical, 20U);
    AppendStartCode(program.bytes, 0xB9);
    auto descriptor = Describe(program.bytes);
    if (!descriptor)
        return;

    CheckError(omega::media::BuildPssPcmAudioStreamPlan(program.bytes, *descriptor),
               DecodeErrorCode::DuplicateReference,
               "automatic audio selection rejects multiple private packet prefixes");
    const auto selected =
        omega::media::BuildPssPcmAudioStreamPlan(program.bytes, *descriptor, kPrefixB);
    Check(selected && selected->private_packet_prefix == kPrefixB &&
              selected->first_packet_presentation_timestamp_90khz == 20U,
          "explicit audio selection resolves a multi-prefix private stream");
    CheckError(
        omega::media::BuildPssPcmAudioStreamPlan(
            program.bytes, *descriptor,
            std::array<std::byte, 4>{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}}),
        DecodeErrorCode::InvalidReference,
        "explicit audio selection rejects a missing private packet prefix");
}

void RunMalformedAndLimitChecks()
{
    const auto valid = BuildLogicalAudio();
    constexpr std::array<std::size_t, 1> one_chunk{56U};

    auto unsupported = BuildLogicalAudio(2U);
    auto unsupported_program = BuildProgram(unsupported, one_chunk);
    auto unsupported_descriptor = Describe(unsupported_program.bytes);
    if (unsupported_descriptor)
    {
        CheckError(omega::media::BuildPssPcmAudioStreamPlan(unsupported_program.bytes,
                                                            *unsupported_descriptor),
                   DecodeErrorCode::UnsupportedVariant,
                   "audio plan rejects an unimplemented SShd encoding tag");
    }

    auto looped = BuildLogicalAudio(1U, 48'000U, 2U, 4U, 0U, 4U);
    auto looped_program = BuildProgram(looped, one_chunk);
    auto looped_descriptor = Describe(looped_program.bytes);
    if (looped_descriptor)
    {
        CheckError(
            omega::media::BuildPssPcmAudioStreamPlan(looped_program.bytes, *looped_descriptor),
            DecodeErrorCode::UnsupportedVariant, "audio plan rejects unimplemented loop semantics");
    }

    auto wrong_size = valid;
    wrong_size[36] = std::byte{15};
    auto wrong_size_program = BuildProgram(wrong_size, one_chunk);
    auto wrong_size_descriptor = Describe(wrong_size_program.bytes);
    if (wrong_size_descriptor)
    {
        CheckError(omega::media::BuildPssPcmAudioStreamPlan(wrong_size_program.bytes,
                                                            *wrong_size_descriptor),
                   DecodeErrorCode::Malformed,
                   "audio plan requires SSbd size to reach exact selected-stream end");
    }

    auto valid_program = BuildProgram(valid, one_chunk);
    auto descriptor = Describe(valid_program.bytes);
    if (!descriptor)
        return;
    auto stale = *descriptor;
    ++stale.private_data_packet_count;
    CheckError(omega::media::BuildPssPcmAudioStreamPlan(valid_program.bytes, stale),
               DecodeErrorCode::InvalidReference,
               "audio plan rejects a descriptor changed after source inspection");

    auto no_scratch = omega::media::DefaultPssPcmAudioDecodeLimits();
    no_scratch.maximum_scratch_bytes = 0U;
    CheckError(
        omega::media::BuildPssPcmAudioStreamPlan(valid_program.bytes, *descriptor, {}, no_scratch),
        DecodeErrorCode::LimitExceeded, "audio descriptor reinspection obeys the scratch limit");

    auto plan = omega::media::BuildPssPcmAudioStreamPlan(valid_program.bytes, *descriptor);
    if (!plan)
        return;
    std::array<std::int16_t, 2> one_frame{};
    CheckError(omega::media::DecodePssPcm16Interleaved(*plan, valid_program.bytes,
                                                       plan->total_frame_count, one_frame),
               DecodeErrorCode::InvalidReference,
               "audio decode rejects a frame interval past stream end");
    auto bad_plan = *plan;
    bad_plan.payloads.front().source_offset = valid_program.bytes.size();
    CheckError(
        omega::media::DecodePssPcm16Interleaved(bad_plan, valid_program.bytes, 0U, one_frame),
        DecodeErrorCode::InvalidReference,
        "audio decode rejects a plan whose physical range reaches past source");
}
} // namespace

int main()
{
    RunPlanAndDecodeChecks();
    RunSelectionChecks();
    RunMalformedAndLimitChecks();
    if (failures == 0)
        std::cout << "omega_pss_pcm_audio_stream_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
