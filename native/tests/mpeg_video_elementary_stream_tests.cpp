#include "omega/media/mpeg_video_elementary_stream.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
using omega::asset::DecodeErrorCode;
using omega::media::H262SequenceHeaderFacts;
using omega::media::MpegProgramStreamDescriptor;
using omega::media::MpegProgramStreamPacketKind;
using omega::media::MpegProgramStreamPayloadClass;
using omega::media::MpegVideoElementaryStreamPlan;
using omega::media::MpegVideoElementaryStreamView;

static_assert(std::forward_iterator<MpegVideoElementaryStreamView::const_iterator>);

struct PacketLocation
{
    std::size_t packet_offset = 0;
    std::size_t payload_offset = 0;
};

struct CanonicalProgram
{
    std::vector<std::byte> bytes;
    PacketLocation first_video;
    PacketLocation second_video;
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
void CheckError(const Result& result, const DecodeErrorCode code, const std::string_view prefix,
                const std::string_view message)
{
    if (result)
    {
        Check(false, message);
        return;
    }
    Check(result.error().code == code, message);
    Check(result.error().message.starts_with(prefix),
          "media error uses its stable subsystem prefix");
    Check(result.error().message.find('/') == std::string::npos &&
              result.error().message.find('\\') == std::string::npos,
          "media error contains no filesystem path");
}

template <typename Result>
void CheckExactError(const Result& result, const DecodeErrorCode code,
                     const std::string_view expected_message,
                     const std::uint64_t expected_byte_offset,
                     const std::string_view message)
{
    CheckError(result, code, "H.262", message);
    if (result)
        return;
    Check(result.error().message == expected_message, message);
    Check(result.error().byte_offset ==
              std::optional<std::uint64_t>{expected_byte_offset},
          message);
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

void AppendPackHeader(std::vector<std::byte>& bytes)
{
    AppendStartCode(bytes, 0xBA);
    constexpr std::array<std::uint8_t, 10> body{{
        0x44,
        0x00,
        0x04,
        0x00,
        0x04,
        0x01,
        0x00,
        0x00,
        0x03,
        0xF8,
    }};
    for (const std::uint8_t value : body)
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

PacketLocation AppendPes(std::vector<std::byte>& bytes, const std::uint8_t stream_id,
                         const std::span<const std::uint8_t> payload,
                         const std::optional<std::uint64_t> pts = std::nullopt)
{
    const std::size_t packet_offset = bytes.size();
    AppendStartCode(bytes, stream_id);
    const std::uint8_t timestamp_bytes = pts ? 5U : 0U;
    AppendU16BigEndian(bytes, static_cast<std::uint16_t>(3U + timestamp_bytes + payload.size()));
    AppendByte(bytes, 0x80);
    AppendByte(bytes, pts ? 0x80 : 0x00);
    AppendByte(bytes, timestamp_bytes);
    if (pts)
        AppendTimestamp(bytes, *pts);
    const std::size_t payload_offset = bytes.size();
    for (const std::uint8_t value : payload)
        AppendByte(bytes, value);
    return PacketLocation{.packet_offset = packet_offset, .payload_offset = payload_offset};
}

void AppendProgramEnd(std::vector<std::byte>& bytes)
{
    AppendStartCode(bytes, 0xB9);
}

[[nodiscard]] CanonicalProgram BuildCanonicalProgram()
{
    CanonicalProgram program;
    AppendPackHeader(program.bytes);
    constexpr std::array<std::uint8_t, 2> first{{0x11, 0x22}};
    program.first_video = AppendPes(program.bytes, 0xE0, first, 9'000U);
    constexpr std::array<std::uint8_t, 1> audio{{0xA5}};
    AppendPes(program.bytes, 0xC0, audio, 8'000U);
    constexpr std::array<std::uint8_t, 3> second{{0x33, 0x44, 0x55}};
    program.second_video = AppendPes(program.bytes, 0xE0, second);
    AppendProgramEnd(program.bytes);
    return program;
}

[[nodiscard]] std::optional<MpegProgramStreamDescriptor> Describe(
    const std::span<const std::byte> bytes)
{
    auto descriptor = omega::media::InspectMpegProgramStream(bytes);
    Check(descriptor.has_value(), "generated MPEG-PS fixture passes the framing descriptor");
    if (!descriptor)
        return std::nullopt;
    return std::move(*descriptor);
}

void RunPlanAndIteratorChecks()
{
    CanonicalProgram program = BuildCanonicalProgram();
    auto descriptor = Describe(program.bytes);
    if (!descriptor)
        return;

    auto plan = omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor);
    Check(plan && plan->stream_id == 0xE0 && plan->source_byte_count == program.bytes.size() &&
              plan->payloads.size() == 2 && plan->total_payload_bytes == 5,
          "video plan selects the unique stream and owns only ordered range "
          "metadata");
    if (!plan)
        return;
    Check(plan->payloads[0].source_offset == program.first_video.payload_offset &&
              plan->payloads[0].byte_count == 2 &&
              plan->payloads[0].presentation_timestamp_90khz == 9'000U &&
              plan->payloads[1].source_offset == program.second_video.payload_offset &&
              plan->payloads[1].byte_count == 3 && !plan->payloads[1].presentation_timestamp_90khz,
          "video plan preserves physical offsets, lengths, ordering, and "
          "optional PTS");

    auto view = omega::media::BorrowMpegVideoElementaryStream(*plan, program.bytes);
    Check(view && view->stream_id() == 0xE0 && view->size() == 2 &&
              view->total_payload_bytes() == 5 && !view->empty(),
          "video plan binds to a validated non-owning iterable view");
    if (!view)
        return;

    auto iterator = view->begin();
    const auto first = *iterator++;
    const auto second = *iterator;
    Check(first.payload.data() == program.bytes.data() + program.first_video.payload_offset &&
              first.payload.size() == 2 && first.presentation_timestamp_90khz == 9'000U &&
              first.source_offset == program.first_video.payload_offset &&
              second.payload.data() == program.bytes.data() + program.second_video.payload_offset &&
              second.payload.size() == 3 && !second.presentation_timestamp_90khz,
          "video iterator yields ordered zero-copy source subspans and PTS values");
    ++iterator;
    Check(iterator == view->end(), "video forward iterator reaches the stable sentinel");

    program.bytes[program.first_video.payload_offset] = std::byte{0x7A};
    Check(std::to_integer<std::uint8_t>(first.payload.front()) == 0x7A,
          "video iterator exposes borrowing rather than a hidden payload copy");

    MpegVideoElementaryStreamPlan detached;
    {
        CanonicalProgram transient = BuildCanonicalProgram();
        auto transient_descriptor = Describe(transient.bytes);
        if (transient_descriptor)
        {
            auto transient_plan = omega::media::BuildMpegVideoElementaryStreamPlan(
                transient.bytes, *transient_descriptor);
            if (transient_plan)
                detached = std::move(*transient_plan);
        }
    }
    Check(detached.payloads.size() == 2 && detached.total_payload_bytes == 5,
          "video plan remains valid metadata after its source storage is "
          "destroyed");
}

void RunSelectionChecks()
{
    std::vector<std::byte> bytes;
    AppendPackHeader(bytes);
    constexpr std::array<std::uint8_t, 1> payload0{{0x10}};
    constexpr std::array<std::uint8_t, 2> payload1{{0x20, 0x21}};
    AppendPes(bytes, 0xE0, payload0);
    AppendPes(bytes, 0xE1, payload1, 42U);
    AppendProgramEnd(bytes);
    auto descriptor = Describe(bytes);
    if (!descriptor)
        return;

    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(bytes, *descriptor),
               DecodeErrorCode::DuplicateReference, "MPEG video ES",
               "automatic video selection rejects multiple distinct stream IDs");
    const auto selected =
        omega::media::BuildMpegVideoElementaryStreamPlan(bytes, *descriptor, std::uint8_t{0xE1});
    Check(selected && selected->stream_id == 0xE1 && selected->payloads.size() == 1 &&
              selected->total_payload_bytes == 2 &&
              selected->payloads.front().presentation_timestamp_90khz == 42U,
          "explicit video selection resolves a multi-video program to exactly "
          "one stream ID");
    CheckError(
        omega::media::BuildMpegVideoElementaryStreamPlan(bytes, *descriptor, std::uint8_t{0xE2}),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "explicit video selection rejects a missing stream ID");
    CheckError(
        omega::media::BuildMpegVideoElementaryStreamPlan(bytes, *descriptor, std::uint8_t{0xC0}),
        DecodeErrorCode::UnsupportedVariant, "MPEG video ES",
        "explicit video selection rejects a non-video stream ID");

    std::vector<std::byte> audio_only;
    AppendPackHeader(audio_only);
    AppendPes(audio_only, 0xC0, payload0);
    AppendProgramEnd(audio_only);
    auto audio_descriptor = Describe(audio_only);
    if (audio_descriptor)
    {
        CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(audio_only, *audio_descriptor),
                   DecodeErrorCode::InvalidReference, "MPEG video ES",
                   "automatic video selection rejects a program with no video PES");
    }

    std::vector<std::byte> upper_bound;
    AppendPackHeader(upper_bound);
    constexpr std::array<std::uint8_t, 2> upper_payload{{0xEF, 0x5A}};
    AppendPes(upper_bound, 0xEF, upper_payload, 84U);
    AppendProgramEnd(upper_bound);
    auto upper_descriptor = Describe(upper_bound);
    if (upper_descriptor)
    {
        const auto selected_upper = omega::media::BuildMpegVideoElementaryStreamPlan(
            upper_bound, *upper_descriptor, std::uint8_t{0xEF});
        Check(selected_upper && selected_upper->stream_id == 0xEF &&
                  selected_upper->payloads.size() == 1U &&
                  selected_upper->total_payload_bytes == upper_payload.size() &&
                  selected_upper->payloads.front().presentation_timestamp_90khz == 84U,
            "explicit video selection accepts 0xEF at the stream-ID upper bound");
    }
}

void RunDescriptorAdversarialChecks()
{
    const CanonicalProgram program = BuildCanonicalProgram();
    auto descriptor = Describe(program.bytes);
    if (!descriptor)
        return;

    auto bad_physical_size = *descriptor;
    ++bad_physical_size.physical_byte_count;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, bad_physical_size),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video planning rejects a descriptor/source physical-size mismatch");

    auto packet_gap = *descriptor;
    ++packet_gap.packets[1].packet_offset;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, packet_gap),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video planning rejects packet gaps and overlaps");

    auto payload_overflow = *descriptor;
    payload_overflow.packets[1].payload_bytes = std::numeric_limits<std::uint64_t>::max();
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, payload_overflow),
               DecodeErrorCode::Overflow, "MPEG video ES",
               "video planning rejects overflowing descriptor payload ranges");

    auto inconsistent_class = *descriptor;
    inconsistent_class.packets[1].payload_class = MpegProgramStreamPayloadClass::Audio;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, inconsistent_class),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video planning rejects inconsistent PES classification");

    auto inconsistent_summary = *descriptor;
    ++inconsistent_summary.video_pes_packet_count;
    CheckError(
        omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, inconsistent_summary),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "video planning rejects inconsistent descriptor summary counters");

    auto invalid_timestamp = *descriptor;
    invalid_timestamp.packets[1].presentation_timestamp_90khz =
        omega::media::kMpegTimestampMaximum90Khz + 1U;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, invalid_timestamp),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video planning rejects a timestamp outside the MPEG 33-bit domain");

    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(
                   std::span<const std::byte>(program.bytes.data(), program.bytes.size() - 1U),
                   *descriptor),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video planning rejects a descriptor against a shorter source span");
}

void RunTrailingZeroPaddingDescriptorChecks()
{
    CanonicalProgram program = BuildCanonicalProgram();
    program.bytes.insert(program.bytes.end(), 2'047U, std::byte{0});
    auto descriptor = Describe(program.bytes);
    if (!descriptor)
        return;

    const auto plan =
        omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor);
    Check(plan && plan->payloads.size() == 2U && plan->total_payload_bytes == 5U,
        "video planning ignores a validated terminal zero-padding extent");
    Check(descriptor->packets.size() >= 2U &&
              descriptor->packets[descriptor->packets.size() - 2U].kind ==
                  MpegProgramStreamPacketKind::ProgramEnd &&
              descriptor->packets.back().kind ==
                  MpegProgramStreamPacketKind::TrailingZeroPadding,
        "video planning fixture contains program end followed by distinct zero padding");

    auto dirty_source = program.bytes;
    dirty_source.back() = std::byte{1};
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(dirty_source, *descriptor),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "video planning revalidates every terminal padding byte against source");

    auto mismatched_extent = *descriptor;
    --mismatched_extent.packets.back().payload_bytes;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, mismatched_extent),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "video planning rejects terminal padding whose payload does not cover its extent");

    auto oversized_source = program.bytes;
    oversized_source.push_back(std::byte{0});
    auto oversized_descriptor = *descriptor;
    ++oversized_descriptor.physical_byte_count;
    ++oversized_descriptor.packets.back().packet_bytes;
    ++oversized_descriptor.packets.back().payload_bytes;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(
                   oversized_source, oversized_descriptor),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "video planning rejects a tampered 2048-byte terminal padding extent");

    auto no_program_end = *descriptor;
    auto& former_end = no_program_end.packets[no_program_end.packets.size() - 2U];
    former_end.kind = MpegProgramStreamPacketKind::ProgramStreamMap;
    former_end.stream_id = 0xBCU;
    no_program_end.has_program_end = false;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, no_program_end),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "video planning rejects terminal padding without exactly one program end");
}

void RunPlanBudgetAndBorrowChecks()
{
    const CanonicalProgram program = BuildCanonicalProgram();
    auto descriptor = Describe(program.bytes);
    if (!descriptor)
        return;
    const auto baseline =
        omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor);
    Check(baseline.has_value(), "video plan budget fixture builds at baseline");
    if (!baseline)
        return;

    auto limits = omega::media::DefaultMpegVideoElementaryStreamDecodeLimits();
    limits.maximum_input_bytes = program.bytes.size();
    Check(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor, std::nullopt,
                                                           limits)
              .has_value(),
          "video plan succeeds at the exact input-byte budget");
    --limits.maximum_input_bytes;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor,
                                                                std::nullopt, limits),
               DecodeErrorCode::LimitExceeded, "MPEG video ES",
               "video plan rejects one byte below its input budget");

    limits = omega::media::DefaultMpegVideoElementaryStreamDecodeLimits();
    limits.maximum_items = 1U + baseline->payloads.size();
    Check(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor, std::nullopt,
                                                           limits)
              .has_value(),
          "video plan succeeds at the exact root-plus-ranges item budget");
    --limits.maximum_items;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor,
                                                                std::nullopt, limits),
               DecodeErrorCode::LimitExceeded, "MPEG video ES",
               "video plan rejects one item below its plan budget");

    const std::uint64_t output_bytes =
        sizeof(MpegVideoElementaryStreamPlan) +
        baseline->payloads.size() * sizeof(omega::media::MpegVideoElementaryStreamPayloadRange);
    limits = omega::media::DefaultMpegVideoElementaryStreamDecodeLimits();
    limits.maximum_output_bytes = output_bytes;
    Check(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor, std::nullopt,
                                                           limits)
              .has_value(),
          "video plan succeeds at the exact owned-output budget");
    --limits.maximum_output_bytes;
    CheckError(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor,
                                                                std::nullopt, limits),
               DecodeErrorCode::LimitExceeded, "MPEG video ES",
               "video plan rejects one byte below its output budget");

    limits = omega::media::DefaultMpegVideoElementaryStreamDecodeLimits();
    limits.maximum_scratch_bytes = 0;
    limits.maximum_nesting_depth = 0;
    Check(omega::media::BuildMpegVideoElementaryStreamPlan(program.bytes, *descriptor, std::nullopt,
                                                           limits)
              .has_value(),
          "video planning uses zero dynamic scratch and nesting depth zero");

    auto bad_stream = *baseline;
    bad_stream.stream_id = 0xC0;
    CheckError(omega::media::BorrowMpegVideoElementaryStream(bad_stream, program.bytes),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video borrowing rejects a non-video planned stream ID");

    auto empty = *baseline;
    empty.payloads.clear();
    empty.total_payload_bytes = 0;
    CheckError(omega::media::BorrowMpegVideoElementaryStream(empty, program.bytes),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video borrowing rejects an empty mutated plan");

    auto wrong_total = *baseline;
    ++wrong_total.total_payload_bytes;
    CheckError(omega::media::BorrowMpegVideoElementaryStream(wrong_total, program.bytes),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video borrowing rejects an inconsistent payload total");

    auto past_source = *baseline;
    past_source.payloads.front().source_offset = program.bytes.size();
    past_source.payloads.front().byte_count = 1;
    CheckError(omega::media::BorrowMpegVideoElementaryStream(past_source, program.bytes),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video borrowing rejects a payload range past source");

    auto overlap = *baseline;
    overlap.payloads[1].source_offset = overlap.payloads[0].source_offset;
    CheckError(omega::media::BorrowMpegVideoElementaryStream(overlap, program.bytes),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video borrowing rejects overlapping or out-of-order ranges");

    auto bad_pts = *baseline;
    bad_pts.payloads.front().presentation_timestamp_90khz =
        omega::media::kMpegTimestampMaximum90Khz + 1U;
    CheckError(omega::media::BorrowMpegVideoElementaryStream(bad_pts, program.bytes),
               DecodeErrorCode::InvalidReference, "MPEG video ES",
               "video borrowing revalidates planned timestamps");

    CheckError(
        omega::media::BorrowMpegVideoElementaryStream(
            *baseline, std::span<const std::byte>(program.bytes.data(), program.bytes.size() - 1U)),
        DecodeErrorCode::InvalidReference, "MPEG video ES",
        "video borrowing rejects a source-size mismatch");
}

void AppendEsStartCode(std::vector<std::uint8_t>& bytes, const std::uint8_t code)
{
    bytes.push_back(0);
    bytes.push_back(0);
    bytes.push_back(1);
    bytes.push_back(code);
}

void AppendSequenceHeader(std::vector<std::uint8_t>& bytes, const std::uint16_t width,
                          const std::uint16_t height, const std::uint8_t aspect_ratio_code,
                          const std::uint8_t frame_rate_code, const bool load_intra_matrix = false)
{
    AppendEsStartCode(bytes, 0xB3);
    bytes.push_back(static_cast<std::uint8_t>(width >> 4U));
    bytes.push_back(static_cast<std::uint8_t>(((width & 0x0FU) << 4U) | ((height >> 8U) & 0x0FU)));
    bytes.push_back(static_cast<std::uint8_t>(height & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((aspect_ratio_code << 4U) | frame_rate_code));
    bytes.push_back(0);
    bytes.push_back(0);
    bytes.push_back(0x20);
    bytes.push_back(load_intra_matrix ? 0x02 : 0x00);
}

void AppendSequenceExtension(std::vector<std::uint8_t>& bytes, const std::uint8_t profile_and_level,
                             const std::uint8_t horizontal_size_extension = 0,
                             const std::uint8_t vertical_size_extension = 0,
                             const std::uint8_t frame_rate_extension_n = 0,
                             const std::uint8_t frame_rate_extension_d = 0,
                             const std::uint8_t chroma_format = 1,
                             const bool bit_rate_marker = true)
{
    AppendEsStartCode(bytes, 0xB5);
    std::uint64_t packed = 0;
    const auto append_bits = [&](const std::uint64_t value, const unsigned bit_count) {
        const std::uint64_t mask = (1ULL << bit_count) - 1U;
        packed = (packed << bit_count) | (value & mask);
    };
    append_bits(1, 4); // sequence_extension_id
    append_bits(profile_and_level, 8);
    append_bits(1, 1); // progressive_sequence
    append_bits(chroma_format, 2); // chroma_format
    append_bits(horizontal_size_extension, 2);
    append_bits(vertical_size_extension, 2);
    append_bits(0, 12); // bit_rate_extension
    append_bits(bit_rate_marker ? 1U : 0U, 1); // marker_bit
    append_bits(0, 8);  // vbv_buffer_size_extension
    append_bits(0, 1);  // low_delay
    append_bits(frame_rate_extension_n, 2);
    append_bits(frame_rate_extension_d, 5);
    for (int shift = 40; shift >= 0; shift -= 8)
        bytes.push_back(static_cast<std::uint8_t>(packed >> shift));
}

[[nodiscard]] std::vector<std::byte> BuildVideoProgram(
    const std::vector<std::vector<std::uint8_t>>& payloads)
{
    std::vector<std::byte> bytes;
    AppendPackHeader(bytes);
    for (std::size_t index = 0; index < payloads.size(); ++index)
    {
        const std::optional<std::uint64_t> pts =
            index == 0 ? std::optional<std::uint64_t>{90'000U} : std::nullopt;
        AppendPes(bytes, 0xE0, payloads[index], pts);
    }
    AppendProgramEnd(bytes);
    return bytes;
}

[[nodiscard]] std::vector<std::vector<std::uint8_t>> SplitEveryByte(
    const std::span<const std::uint8_t> bytes)
{
    std::vector<std::vector<std::uint8_t>> result;
    result.reserve(bytes.size());
    for (const std::uint8_t value : bytes)
        result.push_back({value});
    return result;
}

template <typename Callback>
void WithH262View(const std::vector<std::vector<std::uint8_t>>& payloads, Callback&& callback)
{
    std::vector<std::byte> source = BuildVideoProgram(payloads);
    auto descriptor = Describe(source);
    if (!descriptor)
        return;
    auto plan = omega::media::BuildMpegVideoElementaryStreamPlan(source, *descriptor);
    Check(plan.has_value(), "H.262 fixture creates a video elementary-stream plan");
    if (!plan)
        return;
    auto view = omega::media::BorrowMpegVideoElementaryStream(*plan, source);
    Check(view.has_value(), "H.262 fixture creates a borrowed video view");
    if (!view)
        return;
    std::forward<Callback>(callback)(source, *plan, *view);
}

void RunH262CanonicalAndRepeatChecks()
{
    std::vector<std::uint8_t> elementary{0x55};
    AppendSequenceHeader(elementary, 720, 480, 3, 4);
    AppendSequenceExtension(elementary, 0x48, 0, 0, 1, 0);
    WithH262View(SplitEveryByte(elementary), [](const std::vector<std::byte>&,
                                                const MpegVideoElementaryStreamPlan&,
                                                const MpegVideoElementaryStreamView& view) {
        const auto facts = omega::media::InspectH262SequenceHeaderFacts(view);
        Check(facts && facts->width == 720 && facts->height == 480 &&
                  facts->aspect_ratio_code == 3 && facts->frame_rate_numerator == 60'000U &&
                  facts->frame_rate_denominator == 1'001U &&
                  facts->profile_and_level_indication == 0x48 && facts->sequence_header_count == 1,
              "H.262 facts cross one-byte PES spans and apply "
              "sequence-extension metadata");
    });

    std::vector<std::uint8_t> repeated;
    AppendSequenceHeader(repeated, 704, 480, 2, 3);
    AppendSequenceExtension(repeated, 0x46);
    AppendEsStartCode(repeated, 0xB8);
    repeated.push_back(0x80);
    AppendSequenceHeader(repeated, 704, 480, 2, 3);
    AppendSequenceExtension(repeated, 0x46);
    WithH262View({repeated}, [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                                const MpegVideoElementaryStreamView& view) {
        const auto facts = omega::media::InspectH262SequenceHeaderFacts(view);
        Check(facts && facts->width == 704 && facts->height == 480 &&
                  facts->frame_rate_numerator == 25 && facts->frame_rate_denominator == 1 &&
                  facts->profile_and_level_indication == 0x46 && facts->sequence_header_count == 2,
              "H.262 folds consistent repeated sequence headers into one fact set");
    });

    std::vector<std::uint8_t> base_only;
    AppendSequenceHeader(base_only, 352, 240, 1, 1);
    WithH262View({base_only}, [](const std::vector<std::byte>&,
                                 const MpegVideoElementaryStreamPlan&,
                                 const MpegVideoElementaryStreamView& view) {
        const auto facts = omega::media::InspectH262SequenceHeaderFacts(view);
        Check(facts && facts->width == 352 && facts->height == 240 &&
                  facts->frame_rate_numerator == 24'000U &&
                  facts->frame_rate_denominator == 1'001U && !facts->profile_and_level_indication,
              "H.262 publishes base sequence facts when no MPEG-2 sequence "
              "extension exists");
    });
}

void RunH262AdversarialChecks()
{
    std::vector<std::uint8_t> missing;
    AppendEsStartCode(missing, 0xB8);
    WithH262View({missing}, [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                               const MpegVideoElementaryStreamView& view) {
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view),
                   DecodeErrorCode::InvalidReference, "H.262",
                   "H.262 rejects a selected stream with no sequence header");
    });

    std::vector<std::uint8_t> truncated;
    AppendEsStartCode(truncated, 0xB3);
    truncated.insert(truncated.end(), {0x2D, 0x01, 0xE0});
    WithH262View(SplitEveryByte(truncated), [](const std::vector<std::byte>&,
                                               const MpegVideoElementaryStreamPlan&,
                                               const MpegVideoElementaryStreamView& view) {
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view), DecodeErrorCode::Truncated,
                   "H.262", "H.262 rejects a fixed sequence header truncated across spans");
    });

    std::vector<std::uint8_t> dirty_marker;
    AppendSequenceHeader(dirty_marker, 720, 480, 3, 4);
    dirty_marker[10] = 0;
    WithH262View({dirty_marker}, [](const std::vector<std::byte>&,
                                    const MpegVideoElementaryStreamPlan&,
                                    const MpegVideoElementaryStreamView& view) {
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view), DecodeErrorCode::Malformed,
                   "H.262", "H.262 rejects a missing sequence-header marker bit");
    });

    std::vector<std::uint8_t> reserved_rate;
    AppendSequenceHeader(reserved_rate, 720, 480, 3, 0);
    WithH262View({reserved_rate},
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckError(omega::media::InspectH262SequenceHeaderFacts(view),
                                DecodeErrorCode::UnsupportedVariant, "H.262",
                                "H.262 rejects a reserved frame-rate code");
                 });

    std::vector<std::uint8_t> reserved_aspect;
    AppendSequenceHeader(reserved_aspect, 720, 480, 0, 4);
    WithH262View({reserved_aspect},
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckExactError(
                         omega::media::InspectH262SequenceHeaderFacts(view),
                         DecodeErrorCode::UnsupportedVariant,
                         "H.262 sequence header has a reserved aspect-ratio code", 7U,
                         "H.262 rejects a reserved aspect-ratio code with its exact diagnostic");
                 });

    std::vector<std::uint8_t> reserved_extension_chroma;
    AppendSequenceHeader(reserved_extension_chroma, 720, 480, 3, 4);
    AppendSequenceExtension(reserved_extension_chroma, 0x48, 0, 0, 0, 0, 0);
    WithH262View({reserved_extension_chroma},
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckExactError(
                         omega::media::InspectH262SequenceHeaderFacts(view),
                         DecodeErrorCode::Malformed,
                         "H.262 sequence extension has a reserved chroma format", 17U,
                         "H.262 rejects reserved extension chroma with its exact diagnostic");
                 });

    std::vector<std::uint8_t> missing_extension_marker;
    AppendSequenceHeader(missing_extension_marker, 720, 480, 3, 4);
    AppendSequenceExtension(missing_extension_marker, 0x48, 0, 0, 0, 0, 1, false);
    WithH262View({missing_extension_marker},
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckExactError(
                         omega::media::InspectH262SequenceHeaderFacts(view),
                         DecodeErrorCode::Malformed,
                         "H.262 sequence-extension bit-rate marker is missing", 19U,
                         "H.262 rejects a missing extension marker with its exact diagnostic");
                 });

    std::vector<std::uint8_t> matrix_truncated;
    AppendSequenceHeader(matrix_truncated, 720, 480, 3, 4, true);
    WithH262View({matrix_truncated}, [](const std::vector<std::byte>&,
                                        const MpegVideoElementaryStreamPlan&,
                                        const MpegVideoElementaryStreamView& view) {
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view), DecodeErrorCode::Truncated,
                   "H.262", "H.262 rejects a truncated declared quantizer matrix");
    });

    std::vector<std::uint8_t> intra_first_bit_one;
    AppendSequenceHeader(intra_first_bit_one, 720, 480, 3, 4, true);
    intra_first_bit_one.back() =
        static_cast<std::uint8_t>(intra_first_bit_one.back() | 0x01U);
    intra_first_bit_one.insert(intra_first_bit_one.end(), 64U, 0U);
    AppendSequenceExtension(intra_first_bit_one, 0x48);
    WithH262View({intra_first_bit_one}, [](const std::vector<std::byte>&,
                                           const MpegVideoElementaryStreamPlan&,
                                           const MpegVideoElementaryStreamView& view) {
        const auto facts = omega::media::InspectH262SequenceHeaderFacts(view);
        Check(facts && facts->width == 720 && facts->height == 480 &&
                  facts->profile_and_level_indication == 0x48,
              "H.262 treats fixed-prefix bit zero as the first intra-matrix bit when loaded");
    });

    std::vector<std::uint8_t> truncated_non_intra;
    AppendSequenceHeader(truncated_non_intra, 720, 480, 3, 4, true);
    truncated_non_intra.insert(truncated_non_intra.end(), 64U, 0U);
    truncated_non_intra.back() =
        static_cast<std::uint8_t>(truncated_non_intra.back() | 0x01U);
    truncated_non_intra.insert(truncated_non_intra.end(), 63U, 0U);
    WithH262View({truncated_non_intra}, [](const std::vector<std::byte>&,
                                           const MpegVideoElementaryStreamPlan&,
                                           const MpegVideoElementaryStreamView& view) {
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view), DecodeErrorCode::Truncated,
                   "H.262", "H.262 reads load_non_intra after the optional intra matrix");
    });

    std::vector<std::uint8_t> embedded_matrix_start_code;
    AppendSequenceHeader(embedded_matrix_start_code, 720, 480, 3, 4, true);
    const std::size_t matrix_tail_offset = embedded_matrix_start_code.size();
    embedded_matrix_start_code.insert(embedded_matrix_start_code.end(), 64U, 0U);
    embedded_matrix_start_code[matrix_tail_offset + 10U] = 0U;
    embedded_matrix_start_code[matrix_tail_offset + 11U] = 0U;
    embedded_matrix_start_code[matrix_tail_offset + 12U] = 1U;
    embedded_matrix_start_code[matrix_tail_offset + 13U] = 0xB3U;
    AppendSequenceExtension(embedded_matrix_start_code, 0x48);
    WithH262View({embedded_matrix_start_code}, [](const std::vector<std::byte>&,
                                                  const MpegVideoElementaryStreamPlan&,
                                                  const MpegVideoElementaryStreamView& view) {
        const auto facts = omega::media::InspectH262SequenceHeaderFacts(view);
        Check(facts && facts->sequence_header_count == 1U,
              "H.262 scanner skips parsed matrix bodies instead of finding nested start codes");
    });

    std::vector<std::uint8_t> extension_first;
    AppendSequenceExtension(extension_first, 0x48);
    WithH262View({extension_first},
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckError(omega::media::InspectH262SequenceHeaderFacts(view),
                                DecodeErrorCode::InvalidReference, "H.262",
                                "H.262 rejects a sequence extension before any sequence header");
                 });

    std::vector<std::uint8_t> duplicate_extension;
    AppendSequenceHeader(duplicate_extension, 720, 480, 3, 4);
    AppendSequenceExtension(duplicate_extension, 0x48);
    AppendSequenceExtension(duplicate_extension, 0x48);
    WithH262View({duplicate_extension},
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckError(omega::media::InspectH262SequenceHeaderFacts(view),
                                DecodeErrorCode::DuplicateReference, "H.262",
                                "H.262 rejects multiple sequence extensions for one header");
                 });

    std::vector<std::uint8_t> inconsistent;
    AppendSequenceHeader(inconsistent, 720, 480, 3, 4);
    AppendSequenceExtension(inconsistent, 0x48);
    AppendSequenceHeader(inconsistent, 704, 480, 3, 4);
    AppendSequenceExtension(inconsistent, 0x48);
    WithH262View(SplitEveryByte(inconsistent),
                 [](const std::vector<std::byte>&, const MpegVideoElementaryStreamPlan&,
                    const MpegVideoElementaryStreamView& view) {
                     CheckError(omega::media::InspectH262SequenceHeaderFacts(view),
                                DecodeErrorCode::DuplicateReference, "H.262",
                                "H.262 rejects inconsistent repeated sequence-header facts");
                 });
}

void RunH262BudgetChecks()
{
    std::vector<std::uint8_t> elementary;
    AppendSequenceHeader(elementary, 720, 480, 3, 4);
    AppendSequenceExtension(elementary, 0x48);
    WithH262View(SplitEveryByte(elementary), [](const std::vector<std::byte>&,
                                                const MpegVideoElementaryStreamPlan& plan,
                                                const MpegVideoElementaryStreamView& view) {
        auto limits = omega::media::DefaultH262SequenceHeaderDecodeLimits();
        limits.maximum_input_bytes = plan.total_payload_bytes;
        Check(omega::media::InspectH262SequenceHeaderFacts(view, limits).has_value(),
              "H.262 succeeds at the exact logical input-byte budget");
        --limits.maximum_input_bytes;
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view, limits),
                   DecodeErrorCode::LimitExceeded, "H.262",
                   "H.262 rejects one byte below its logical input budget");

        limits = omega::media::DefaultH262SequenceHeaderDecodeLimits();
        limits.maximum_items = 3; // one root plus sequence-header and extension start codes
        Check(omega::media::InspectH262SequenceHeaderFacts(view, limits).has_value(),
              "H.262 succeeds at the exact root-plus-start-codes item budget");
        --limits.maximum_items;
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view, limits),
                   DecodeErrorCode::LimitExceeded, "H.262",
                   "H.262 rejects one item below its start-code budget");

        limits = omega::media::DefaultH262SequenceHeaderDecodeLimits();
        limits.maximum_output_bytes = sizeof(H262SequenceHeaderFacts);
        limits.maximum_scratch_bytes = 0;
        limits.maximum_nesting_depth = 0;
        Check(omega::media::InspectH262SequenceHeaderFacts(view, limits).has_value(),
              "H.262 uses exactly one fixed output object and zero "
              "scratch/nesting");
        --limits.maximum_output_bytes;
        CheckError(omega::media::InspectH262SequenceHeaderFacts(view, limits),
                   DecodeErrorCode::LimitExceeded, "H.262",
                   "H.262 rejects one byte below its fixed output budget");
    });
}
} // namespace

int main()
{
    static_assert(omega::media::kMpegVideoElementaryStreamMaximumPayloadRanges == 262'144U);
    static_assert(omega::media::kMpegTimestampMaximum90Khz == 8'589'934'591ULL);
    static_assert(omega::media::kH262MaximumStartCodes == 1'048'576U);

    RunPlanAndIteratorChecks();
    RunSelectionChecks();
    RunDescriptorAdversarialChecks();
    RunTrailingZeroPaddingDescriptorChecks();
    RunPlanBudgetAndBorrowChecks();
    RunH262CanonicalAndRepeatChecks();
    RunH262AdversarialChecks();
    RunH262BudgetChecks();

    if (failures != 0)
        std::cerr << failures << " MPEG video elementary-stream check(s) failed\n";
    return failures == 0 ? 0 : 1;
}
