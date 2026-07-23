#include "omega/retail/skl_container_descriptor.h"

#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kMinimumRecordCount = 10;
constexpr std::uint64_t kMaximumRecordCount = 60;
constexpr std::uint64_t kMaximumLineBytes = 29;
constexpr std::uint8_t kCarriageReturn = 0x0D;
constexpr std::uint8_t kLineFeed = 0x0A;
constexpr std::uint8_t kNul = 0x00;
constexpr std::uint8_t kSpace = 0x20;

struct ScanSummary
{
    SklLineEnding line_ending = SklLineEnding::CarriageReturn;
    std::uint64_t record_count = 0;
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

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] std::uint8_t ReadU8(
    const std::span<const std::byte> bytes, const std::uint64_t offset) noexcept
{
    return std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset)]);
}

[[nodiscard]] bool IsAsciiAlphanumeric(const std::uint8_t value) noexcept
{
    return (value >= static_cast<std::uint8_t>('0') &&
               value <= static_cast<std::uint8_t>('9')) ||
           (value >= static_cast<std::uint8_t>('A') &&
               value <= static_cast<std::uint8_t>('Z')) ||
           (value >= static_cast<std::uint8_t>('a') &&
               value <= static_cast<std::uint8_t>('z'));
}

[[nodiscard]] bool IsTokenByte(const std::uint8_t value) noexcept
{
    return IsAsciiAlphanumeric(value) || value == static_cast<std::uint8_t>('_') ||
           value == static_cast<std::uint8_t>('-') ||
           value == static_cast<std::uint8_t>('.');
}

[[nodiscard]] std::uint8_t AsciiUpper(const std::uint8_t value) noexcept
{
    if (value >= static_cast<std::uint8_t>('a') &&
        value <= static_cast<std::uint8_t>('z'))
        return static_cast<std::uint8_t>(value - static_cast<std::uint8_t>('a') +
                                         static_cast<std::uint8_t>('A'));
    return value;
}

[[nodiscard]] bool EndsWithSkel(const std::span<const std::byte> bytes,
    const std::uint64_t token_offset, const std::uint64_t token_size) noexcept
{
    constexpr char kSuffix[] = ".SKEL";
    constexpr std::uint64_t kSuffixBytes = sizeof(kSuffix) - 1;
    if (token_size < kSuffixBytes)
        return false;

    const std::uint64_t suffix_offset = token_offset + token_size - kSuffixBytes;
    for (std::uint64_t index = 0; index < kSuffixBytes; ++index)
    {
        if (AsciiUpper(ReadU8(bytes, suffix_offset + index)) !=
            static_cast<std::uint8_t>(kSuffix[index]))
            return false;
    }
    return true;
}

[[nodiscard]] asset::DecodeResult<void> ValidateMarkerFamily(
    const std::span<const std::byte> bytes, const std::uint64_t record_index,
    const std::uint64_t token_offset, const std::uint64_t token_size)
{
    bool contains_dot = false;
    for (std::uint64_t index = 0; index < token_size; ++index)
    {
        const std::uint8_t value = ReadU8(bytes, token_offset + index);
        if (!IsTokenByte(value))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "SKL token contains a byte outside the permitted grammar",
                token_offset + index));
        }
        contains_dot = contains_dot || value == static_cast<std::uint8_t>('.');
    }

    if (record_index == 0 && token_size == 0)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKL record markers are outside the observed family", token_offset));
    }
    if (record_index == 3)
    {
        if (token_size == 0 || !EndsWithSkel(bytes, token_offset, token_size))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "SKL record markers are outside the observed family", token_offset));
        }
    }
    else if (contains_dot)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKL record markers are outside the observed family", token_offset));
    }
    return {};
}

template <typename Visitor>
[[nodiscard]] asset::DecodeResult<ScanSummary> ScanRecords(
    const std::span<const std::byte> bytes, const std::uint64_t logical_end,
    Visitor&& visitor)
{
    ScanSummary summary;
    std::optional<SklLineEnding> observed_line_ending;
    std::uint64_t line_start = 0;

    while (line_start < logical_end)
    {
        std::uint64_t line_end = line_start;
        while (line_end < logical_end)
        {
            const std::uint8_t value = ReadU8(bytes, line_end);
            if (value == kCarriageReturn || value == kLineFeed)
                break;
            ++line_end;
        }

        const std::uint64_t line_size = line_end - line_start;
        if (line_size > kMaximumLineBytes)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "SKL line length is outside the observed range", line_start));
        }

        std::uint64_t terminator_size = 0;
        if (line_end < logical_end)
        {
            if (ReadU8(bytes, line_end) == kLineFeed)
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "SKL contains a bare line feed", line_end));
            }

            terminator_size = 1;
            std::uint64_t following_offset = 0;
            if (!Add(line_end, 1, following_offset))
            {
                return std::unexpected(Error(
                    asset::DecodeErrorCode::Overflow, "SKL line terminator offset overflows"));
            }
            if (following_offset < logical_end && ReadU8(bytes, following_offset) == kLineFeed)
                terminator_size = 2;

            const SklLineEnding line_ending = terminator_size == 1
                ? SklLineEnding::CarriageReturn
                : SklLineEnding::CarriageReturnLineFeed;
            if (observed_line_ending && *observed_line_ending != line_ending)
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "SKL line terminators are mixed", line_end));
            }
            observed_line_ending = line_ending;
        }

        std::uint64_t token_end = line_end;
        while (token_end > line_start && ReadU8(bytes, token_end - 1) == kSpace)
            --token_end;
        const std::uint64_t token_size = token_end - line_start;

        auto marker_result = ValidateMarkerFamily(
            bytes, summary.record_count, line_start, token_size);
        if (!marker_result)
            return std::unexpected(marker_result.error());

        std::uint64_t terminator_end = 0;
        if (!Add(line_end, terminator_size, terminator_end))
        {
            return std::unexpected(Error(
                asset::DecodeErrorCode::Overflow, "SKL line terminator range overflows"));
        }
        visitor(SklRecordDescriptor{
            .line_region = {.offset = line_start, .size = line_size},
            .token_region = {.offset = line_start, .size = token_size},
            .terminator_region = {.offset = line_end, .size = terminator_size},
        });

        std::uint64_t next_record_count = 0;
        if (!Add(summary.record_count, 1, next_record_count))
        {
            return std::unexpected(Error(
                asset::DecodeErrorCode::Overflow, "SKL record count overflows"));
        }
        summary.record_count = next_record_count;
        if (summary.record_count > kMaximumRecordCount)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "SKL record count is outside the observed range"));
        }
        line_start = terminator_end;
    }

    if (summary.record_count < kMinimumRecordCount)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "SKL record count is outside the observed range"));
    }
    if (!observed_line_ending)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "SKL line terminator family is unavailable"));
    }
    summary.line_ending = *observed_line_ending;
    return summary;
}
} // namespace

asset::DecodeResult<SklContainerDescriptor> InspectSklContainer(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKL input exceeds decoder byte limit"));
    }

    const std::uint64_t input_bytes = bytes.size();
    std::uint64_t logical_end = input_bytes;
    bool has_zero_tail = false;
    for (std::uint64_t offset = 0; offset < input_bytes; ++offset)
    {
        if (ReadU8(bytes, offset) == kNul)
        {
            logical_end = offset;
            has_zero_tail = true;
            break;
        }
    }
    if (has_zero_tail)
    {
        for (std::uint64_t offset = logical_end; offset < input_bytes; ++offset)
        {
            if (ReadU8(bytes, offset) != kNul)
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "SKL bytes after the logical end are not zero padding", offset));
            }
        }
    }

    auto summary = ScanRecords(bytes, logical_end, [](const SklRecordDescriptor&) {});
    if (!summary)
        return std::unexpected(summary.error());

    std::uint64_t item_count = 0;
    if (!Add(1, summary->record_count, item_count))
    {
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "SKL descriptor item count overflows"));
    }
    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKL descriptor exceeds decoder item limit"));
    }

    std::uint64_t record_output_bytes = 0;
    std::uint64_t output_bytes = 0;
    if (!Multiply(summary->record_count, sizeof(SklRecordDescriptor), record_output_bytes) ||
        !Add(sizeof(SklContainerDescriptor), record_output_bytes, output_bytes))
    {
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "SKL descriptor output size overflows"));
    }
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "SKL descriptor exceeds decoder output limit"));
    }

    SklContainerDescriptor descriptor{
        .line_ending = summary->line_ending,
        .logical_extent = ObservedExtent{
            .observed_bytes = logical_end,
            .input_bytes = input_bytes,
            .relation = has_zero_tail ? ObservedExtentRelation::ZeroPaddedTail
                                      : ObservedExtentRelation::Exact,
        },
    };
    // Reserve/push_back can throw under memory pressure even though `summary->record_count`
    // is already bounded by kMaximumRecordCount; contain that here rather than let it escape
    // this DecodeResult-typed boundary as a raw exception.
    try
    {
        descriptor.records.reserve(static_cast<std::size_t>(summary->record_count));
        auto construction = ScanRecords(bytes, logical_end,
            [&descriptor](const SklRecordDescriptor& record) { descriptor.records.push_back(record); });
        if (!construction)
            return std::unexpected(construction.error());
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "SKL allocation"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "SKL length"));
    }

    return descriptor;
}
} // namespace omega::retail
