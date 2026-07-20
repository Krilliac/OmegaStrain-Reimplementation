#pragma once

#include "omega/media/mpeg_program_stream_descriptor.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <vector>

namespace omega::media
{
inline constexpr std::uint64_t kMpegVideoElementaryStreamMaximumPayloadRanges =
    kMpegProgramStreamMaximumPacketDescriptors;
inline constexpr std::uint64_t kH262MaximumStartCodes = 1ULL << 20U;
inline constexpr std::uint64_t kMpegTimestampMaximum90Khz = (1ULL << 33U) - 1U;

[[nodiscard]] constexpr asset::DecodeLimits DefaultMpegVideoElementaryStreamDecodeLimits() noexcept
{
    return DefaultMpegProgramStreamDecodeLimits();
}

[[nodiscard]] constexpr asset::DecodeLimits DefaultH262SequenceHeaderDecodeLimits() noexcept
{
    return DefaultMpegProgramStreamDecodeLimits();
}

// Owned plan metadata for one video PES payload. The range refers to the
// original MPEG-PS source; it owns no encoded byte and retains no source
// pointer.
struct MpegVideoElementaryStreamPayloadRange
{
    std::uint64_t source_offset = 0;
    std::uint64_t byte_count = 0;
    std::optional<std::uint64_t> presentation_timestamp_90khz;

    [[nodiscard]] bool operator==(const MpegVideoElementaryStreamPayloadRange&) const = default;
};

// Owned, codec-neutral reassembly plan for exactly one MPEG video stream_id.
// The vector contains ranges and timestamps only; payload bytes remain in
// caller-owned storage and are never joined.
struct MpegVideoElementaryStreamPlan
{
    std::uint8_t stream_id = 0;
    std::uint64_t source_byte_count = 0;
    std::uint64_t total_payload_bytes = 0;
    std::vector<MpegVideoElementaryStreamPayloadRange> payloads;

    [[nodiscard]] bool operator==(const MpegVideoElementaryStreamPlan&) const = default;
};

// One iterator value. payload is a zero-copy subspan of the source supplied to
// BorrowMpegVideoElementaryStream and is valid only while that source remains
// alive and unmoved.
struct MpegVideoElementaryStreamChunk
{
    std::span<const std::byte> payload;
    std::optional<std::uint64_t> presentation_timestamp_90khz;
    std::uint64_t source_offset = 0;
};

// Non-owning range over a validated plan and source. The plan and source must
// outlive the view and all iterators, and the caller must not mutate the plan
// concurrently. Destroying the view does not invalidate iterators while those
// two borrowed objects remain alive.
class MpegVideoElementaryStreamView final
{
public:
    // [any thread; borrowed plan/source must not be mutated concurrently]
    class const_iterator final
    {
    public:
        using iterator_concept = std::forward_iterator_tag;
        using iterator_category = std::forward_iterator_tag;
        using value_type = MpegVideoElementaryStreamChunk;
        using difference_type = std::ptrdiff_t;

        const_iterator() noexcept = default;

        [[nodiscard]] value_type operator*() const noexcept;
        const_iterator& operator++() noexcept;
        const_iterator operator++(int) noexcept;

        [[nodiscard]] friend bool operator==(const const_iterator&,
                                             const const_iterator&) noexcept = default;

    private:
        friend class MpegVideoElementaryStreamView;

        const_iterator(const std::byte* source, const MpegVideoElementaryStreamPayloadRange* ranges,
                       std::size_t range_count, std::size_t index) noexcept;

        const std::byte* source_ = nullptr;
        const MpegVideoElementaryStreamPayloadRange* ranges_ = nullptr;
        std::size_t range_count_ = 0;
        std::size_t index_ = 0;
    };

    // [any thread; borrowed plan/source must not be mutated concurrently]
    [[nodiscard]] const_iterator begin() const noexcept;
    [[nodiscard]] const_iterator end() const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] std::uint8_t stream_id() const noexcept;
    [[nodiscard]] std::uint64_t total_payload_bytes() const noexcept;

private:
    friend asset::DecodeResult<MpegVideoElementaryStreamView> BorrowMpegVideoElementaryStream(
        const MpegVideoElementaryStreamPlan&, std::span<const std::byte>, asset::DecodeLimits);

    MpegVideoElementaryStreamView(std::span<const std::byte> source,
                                  std::span<const MpegVideoElementaryStreamPayloadRange> ranges,
                                  std::uint8_t stream_id,
                                  std::uint64_t total_payload_bytes) noexcept;

    std::span<const std::byte> source_;
    std::span<const MpegVideoElementaryStreamPayloadRange> ranges_;
    std::uint8_t stream_id_ = 0;
    std::uint64_t total_payload_bytes_ = 0;
};

// [any worker thread; stateless/reentrant] Validates the complete descriptor
// against source, then builds an owned offset-only plan. With no requested
// stream ID, exactly one distinct MPEG video stream must be present. A
// requested E0-EF ID resolves a multi-video program but must itself exist. The
// source and descriptor need only remain alive for this call; the returned plan
// retains neither. maximum_items charges one plan root plus one selected
// payload range. maximum_output_bytes charges
// sizeof(MpegVideoElementaryStreamPlan) plus the selected range array; scratch
// and nesting are zero.
[[nodiscard]] asset::DecodeResult<MpegVideoElementaryStreamPlan> BuildMpegVideoElementaryStreamPlan(
    std::span<const std::byte> source, const MpegProgramStreamDescriptor& descriptor,
    std::optional<std::uint8_t> requested_stream_id = std::nullopt,
    asset::DecodeLimits limits = DefaultMpegVideoElementaryStreamDecodeLimits());

// [any worker thread; stateless/reentrant] Revalidates every owned plan range
// against source and returns a borrowed zero-copy iterable view. The plan and
// source must outlive the view/iterators.
[[nodiscard]] asset::DecodeResult<MpegVideoElementaryStreamView> BorrowMpegVideoElementaryStream(
    const MpegVideoElementaryStreamPlan& plan, std::span<const std::byte> source,
    asset::DecodeLimits limits = DefaultMpegVideoElementaryStreamDecodeLimits());

struct H262SequenceHeaderFacts
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t aspect_ratio_code = 0;
    std::uint32_t frame_rate_numerator = 0;
    std::uint32_t frame_rate_denominator = 1;
    std::optional<std::uint8_t> profile_and_level_indication;
    std::uint64_t sequence_header_count = 0;

    [[nodiscard]] bool operator==(const H262SequenceHeaderFacts&) const = default;
};

// [any worker thread; stateless/reentrant] Scans selected payload spans as one
// logical H.262 byte stream without concatenating them. Start-code prefixes and
// fixed headers may cross PES payload boundaries. Consistent repeated sequence
// headers are folded into one fact set; inconsistent ones are rejected. A
// sequence extension, when present, contributes size extensions, profile/level,
// and the frame-rate extension ratio. DecodeError::byte_offset is a logical
// elementary-stream offset. maximum_items charges one result root plus every
// encountered start code; output is one fixed fact object and dynamic
// scratch/nesting are zero.
[[nodiscard]] asset::DecodeResult<H262SequenceHeaderFacts> InspectH262SequenceHeaderFacts(
    const MpegVideoElementaryStreamView& stream,
    asset::DecodeLimits limits = DefaultH262SequenceHeaderDecodeLimits());
} // namespace omega::media
