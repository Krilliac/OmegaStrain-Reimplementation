#include "omega/retail/ska_container_descriptor.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kHeaderBytes = 112;
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint32_t kFormatVersion = 3;
constexpr std::uint32_t kMinimumWord0x04 = 1;
constexpr std::uint32_t kMaximumWord0x04 = 357;
constexpr std::uint64_t kMinimumLogicalBytes = 464;
constexpr std::uint64_t kMaximumLogicalBytes = 125'776;
constexpr std::uint64_t kContainerAlignment = 16;

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

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] bool IsObservedWord0x08(const std::uint32_t value) noexcept
{
    return value == 56 || value == 88 || value == 92;
}

[[nodiscard]] bool IsObservedWordPair(
    const std::uint32_t word_0x08, const std::uint32_t word_0x10) noexcept
{
    return word_0x08 != 56 || word_0x10 == 0;
}

[[nodiscard]] ObservedExtent ClassifyExtent(
    const std::span<const std::byte> bytes, const std::uint64_t observed_bytes) noexcept
{
    ObservedExtentRelation relation = ObservedExtentRelation::Exact;
    if (observed_bytes > bytes.size())
    {
        relation = ObservedExtentRelation::ExceedsInput;
    }
    else if (observed_bytes < bytes.size())
    {
        relation = ObservedExtentRelation::ZeroPaddedTail;
        for (std::uint64_t offset = observed_bytes; offset < bytes.size(); ++offset)
        {
            if (bytes[static_cast<std::size_t>(offset)] != std::byte{0})
            {
                relation = ObservedExtentRelation::NonzeroTail;
                break;
            }
        }
    }

    return ObservedExtent{
        .observed_bytes = observed_bytes,
        .input_bytes = bytes.size(),
        .relation = relation,
    };
}
} // namespace

asset::DecodeResult<SkaContainerDescriptor> InspectSkaContainer(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKA input exceeds decoder byte limit"));
    }
    if (limits.maximum_items < 1)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKA descriptor exceeds decoder item limit"));
    }
    if (limits.maximum_output_bytes < sizeof(SkaContainerDescriptor))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKA descriptor exceeds decoder output limit"));
    }
    if (bytes.size() < kHeaderBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "SKA 112-byte prefix is truncated", bytes.size()));
    }

    const std::uint32_t format_version = ReadU32(bytes, 0);
    const std::uint32_t word_0x04 = ReadU32(bytes, 4);
    const std::uint32_t word_0x08 = ReadU32(bytes, 8);
    const std::uint32_t word_0x10 = ReadU32(bytes, 16);
    if (format_version != kFormatVersion)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKA format version is outside the observed version-3 family", 0));
    }
    if (word_0x04 < kMinimumWord0x04 || word_0x04 > kMaximumWord0x04)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKA word at 0x04 is outside the observed range", 4));
    }
    if (!IsObservedWord0x08(word_0x08))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKA word at 0x08 is outside the observed family", 8));
    }
    if (word_0x10 > 1)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKA word at 0x10 is outside the observed zero-or-one family", 16));
    }
    if (!IsObservedWordPair(word_0x08, word_0x10))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKA words at 0x08 and 0x10 form an unobserved pair", 8));
    }

    std::uint64_t counted_groups = 0;
    std::uint64_t counted_words = 0;
    std::uint64_t counted_word_bytes = 0;
    std::uint64_t logical_bytes = 0;
    if (!Add(word_0x04, word_0x10 == 0 ? 1U : 0U, counted_groups) ||
        !Multiply(word_0x08, counted_groups, counted_words) ||
        !Multiply(kWordBytes, counted_words, counted_word_bytes) ||
        !Add(kHeaderBytes, counted_word_bytes, logical_bytes))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "SKA counted-word extent overflows"));
    }
    if (logical_bytes < kMinimumLogicalBytes || logical_bytes > kMaximumLogicalBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKA computed logical extent is outside the observed range", 4));
    }
    if (bytes.size() % kContainerAlignment != 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "SKA physical span is not 16-byte aligned"));
    }

    return SkaContainerDescriptor{
        .format_version = format_version,
        .observed_word_0x04 = word_0x04,
        .observed_word_0x08 = word_0x08,
        .observed_word_0x10 = word_0x10,
        .logical_extent = ClassifyExtent(bytes, logical_bytes),
    };
}
} // namespace omega::retail
