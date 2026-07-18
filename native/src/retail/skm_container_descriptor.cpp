#include "omega/retail/skm_container_descriptor.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kFixedHeaderBytes = 2;
constexpr std::uint64_t kChunkRecordBytes = 2;
constexpr std::uint64_t kContainerAlignment = 16;
constexpr std::uint8_t kFormatVersion = 3;
constexpr std::uint8_t kMinimumChunkCount = 1;
constexpr std::uint8_t kMaximumChunkCount = 61;
constexpr std::uint8_t kMinimumQwordCount = 4;
constexpr std::uint8_t kMaximumQwordCount = 55;
constexpr std::uint8_t kMinimumSecondaryCount = 1;
constexpr std::uint8_t kMaximumSecondaryCount = 30;

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

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool AlignUp(
    const std::uint64_t value, const std::uint64_t alignment, std::uint64_t& result) noexcept
{
    if (alignment == 0)
        return false;

    const std::uint64_t remainder = value % alignment;
    if (remainder == 0)
    {
        result = value;
        return true;
    }
    return Add(value, alignment - remainder, result);
}

[[nodiscard]] std::uint8_t ReadU8(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint8_t>(bytes[offset]);
}
} // namespace

asset::DecodeResult<SkmContainerDescriptor> InspectSkmContainer(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKM input exceeds decoder byte limit"));
    }
    if (bytes.size() < kFixedHeaderBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "SKM fixed header is truncated", bytes.size()));
    }
    const std::uint8_t chunk_count = ReadU8(bytes, 0);
    const std::uint8_t format_version = ReadU8(bytes, 1);
    if (format_version != kFormatVersion)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKM format version is outside the observed version-3 family", 1));
    }
    if (chunk_count < kMinimumChunkCount || chunk_count > kMaximumChunkCount)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKM chunk count is outside the observed range", 0));
    }

    std::uint64_t item_count = 0;
    if (!Add(1, chunk_count, item_count))
    {
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "SKM descriptor item count overflows"));
    }
    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKM descriptor exceeds decoder item limit"));
    }

    std::uint64_t chunk_output_bytes = 0;
    std::uint64_t output_bytes = 0;
    if (!Multiply(chunk_count, sizeof(SkmChunkDescriptor), chunk_output_bytes) ||
        !Add(sizeof(SkmContainerDescriptor), chunk_output_bytes, output_bytes))
    {
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "SKM descriptor output size overflows"));
    }
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKM descriptor exceeds decoder output limit"));
    }

    std::uint64_t chunk_table_bytes = 0;
    std::uint64_t chunk_table_end = 0;
    std::uint64_t aligned_header_bytes = 0;
    if (!Multiply(chunk_count, kChunkRecordBytes, chunk_table_bytes) ||
        !Add(kFixedHeaderBytes, chunk_table_bytes, chunk_table_end) ||
        !AlignUp(chunk_table_end, kContainerAlignment, aligned_header_bytes))
    {
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "SKM aligned header size overflows"));
    }
    if (chunk_table_end > bytes.size())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "SKM chunk table is truncated", bytes.size()));
    }
    if (aligned_header_bytes > bytes.size())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "SKM aligned header is truncated", bytes.size()));
    }

    SkmContainerDescriptor descriptor{
        .format_version = format_version,
        .chunk_table_region = {.offset = kFixedHeaderBytes, .size = chunk_table_bytes},
        .aligned_header_region = {.offset = 0, .size = aligned_header_bytes},
    };
    descriptor.chunks.reserve(chunk_count);

    std::uint64_t payload_offset = aligned_header_bytes;
    for (std::uint64_t chunk_index = 0;
         chunk_index < static_cast<std::uint64_t>(chunk_count); ++chunk_index)
    {
        std::uint64_t chunk_record_offset = 0;
        std::uint64_t relative_record_offset = 0;
        if (!Multiply(chunk_index, kChunkRecordBytes, relative_record_offset) ||
            !Add(kFixedHeaderBytes, relative_record_offset, chunk_record_offset))
        {
            return std::unexpected(Error(
                asset::DecodeErrorCode::Overflow, "SKM chunk record offset overflows"));
        }

        const auto record_offset = static_cast<std::size_t>(chunk_record_offset);
        const std::uint8_t qword_count = ReadU8(bytes, record_offset);
        const std::uint8_t secondary_count = ReadU8(bytes, record_offset + 1);
        if (qword_count < kMinimumQwordCount || qword_count > kMaximumQwordCount)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "SKM qword count is outside the observed range", chunk_record_offset));
        }
        if (secondary_count < kMinimumSecondaryCount ||
            secondary_count > kMaximumSecondaryCount)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "SKM secondary count is outside the observed range", chunk_record_offset + 1));
        }

        std::uint64_t payload_bytes = 0;
        std::uint64_t payload_end = 0;
        if (!Multiply(qword_count, kContainerAlignment, payload_bytes) ||
            !Add(payload_offset, payload_bytes, payload_end))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "SKM payload range overflows", chunk_record_offset));
        }
        if (payload_end > bytes.size())
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                "SKM chunk payload is truncated", bytes.size()));
        }

        descriptor.chunks.push_back(SkmChunkDescriptor{
            .qword_count = qword_count,
            .observed_secondary_count = secondary_count,
            .payload_region = {.offset = payload_offset, .size = payload_bytes},
        });
        payload_offset = payload_end;
    }

    if (bytes.size() % kContainerAlignment != 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "SKM physical span is not 16-byte aligned"));
    }

    ObservedExtentRelation extent_relation = ObservedExtentRelation::Exact;
    if (payload_offset < bytes.size())
    {
        extent_relation = ObservedExtentRelation::ZeroPaddedTail;
        for (std::uint64_t tail_offset = payload_offset; tail_offset < bytes.size(); ++tail_offset)
        {
            if (bytes[static_cast<std::size_t>(tail_offset)] != std::byte{0})
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "SKM trailing bytes are not zero padding", tail_offset));
            }
        }
    }

    descriptor.logical_extent = ObservedExtent{
        .observed_bytes = payload_offset,
        .input_bytes = bytes.size(),
        .relation = extent_relation,
    };
    return descriptor;
}
} // namespace omega::retail
