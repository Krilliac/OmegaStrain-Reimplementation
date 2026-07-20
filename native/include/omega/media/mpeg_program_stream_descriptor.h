#pragma once

#include "omega/asset/decode.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace omega::media
{
// Project-owned parser safety ceilings. They are neither owner-corpus observations nor limits
// encoded by MPEG systems syntax. Explicit caller limits may tighten but cannot raise them.
inline constexpr std::uint64_t kMpegProgramStreamMaximumInputBytes = 512ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kMpegProgramStreamMaximumPacketDescriptors = 1ULL << 18U;

[[nodiscard]] constexpr asset::DecodeLimits DefaultMpegProgramStreamDecodeLimits() noexcept
{
    auto limits = asset::DecodeLimits{};
    limits.maximum_input_bytes = kMpegProgramStreamMaximumInputBytes;
    return limits;
}

enum class MpegProgramStreamPacketKind : std::uint8_t
{
    PackHeader,
    SystemHeader,
    ProgramStreamMap,
    ProgramEnd,
    Pes,
    Padding,
    OpaqueLengthDelimited,
};

enum class MpegProgramStreamPayloadClass : std::uint8_t
{
    None,
    Audio,
    Video,
    PrivateData,
    Other,
};

// One MPEG-2 Program Stream packet boundary. Every offset is relative to the inspected input. The
// descriptor owns no payload and retains no pointer into the borrowed source. Payload classification
// follows only the standardized stream-id ranges; it does not identify a codec.
struct MpegProgramStreamPacketDescriptor
{
    MpegProgramStreamPacketKind kind = MpegProgramStreamPacketKind::OpaqueLengthDelimited;
    MpegProgramStreamPayloadClass payload_class = MpegProgramStreamPayloadClass::None;
    std::uint8_t stream_id = 0;
    std::uint64_t packet_offset = 0;
    std::uint64_t packet_bytes = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t payload_bytes = 0;
    std::optional<std::uint64_t> presentation_timestamp_90khz;
    std::optional<std::uint64_t> decoding_timestamp_90khz;
    bool zero_length_pes = false;

    [[nodiscard]] bool operator==(const MpegProgramStreamPacketDescriptor&) const = default;
};

// Owned, codec-neutral framing summary. Packet descriptors preserve source order and byte ranges;
// encoded media bytes stay in the caller-owned input.
struct MpegProgramStreamDescriptor
{
    std::vector<MpegProgramStreamPacketDescriptor> packets;
    std::uint64_t physical_byte_count = 0;
    std::uint64_t pack_header_count = 0;
    std::uint64_t pes_packet_count = 0;
    std::uint64_t audio_pes_packet_count = 0;
    std::uint64_t video_pes_packet_count = 0;
    std::uint64_t private_data_packet_count = 0;
    bool has_program_end = false;

    [[nodiscard]] bool operator==(const MpegProgramStreamDescriptor&) const = default;
};

// [any worker thread; stateless/reentrant] Inspects a complete or packet-aligned MPEG-2 Program
// Stream span. The bounded parser validates pack and PES framing, MPEG-2 marker bits, optional
// PTS/DTS fields, declared lengths, and exact packet boundaries. A zero PES length is accepted only
// for a video stream and is bounded by the next systems-layer start code or EOF. No elementary
// stream, codec, subtitle, audio, or game-specific payload grammar is decoded. In particular, this
// generic fixture-backed API does not assert that any retail .pss member uses this syntax.
//
// The result owns its descriptor vector, uses zero dynamic scratch, and treats the root as nesting
// depth zero. maximum_items charges one root plus one item per packet descriptor.
[[nodiscard]] asset::DecodeResult<MpegProgramStreamDescriptor> InspectMpegProgramStream(
    std::span<const std::byte> bytes,
    asset::DecodeLimits limits = DefaultMpegProgramStreamDecodeLimits());
} // namespace omega::media
