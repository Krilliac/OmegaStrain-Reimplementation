#include "omega/retail/skas_text_envelope_decoder.h"

#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace omega::retail
{
namespace
{
constexpr std::uint8_t kNul = 0x00;
constexpr std::uint8_t kCarriageReturn = 0x0D;
constexpr std::uint8_t kLineFeed = 0x0A;
constexpr std::uint8_t kColon = 0x3A;

struct TextScan
{
    std::uint64_t line_count = 0;
    std::uint64_t blank_line_count = 0;
    std::uint64_t single_colon_line_count = 0;
};

[[nodiscard]] asset::DecodeError Error(const asset::DecodeErrorCode code, std::string message,
                                       const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint8_t ReadU8(const std::span<const std::byte> bytes, const std::uint64_t offset) noexcept
{
    return std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset)]);
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right, std::uint64_t &result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] asset::DecodeResult<TextScan> ScanText(const std::span<const std::byte> bytes,
                                                     const std::uint64_t logical_bytes)
{
    TextScan scan;
    std::uint64_t line_start = 0;
    std::uint64_t colon_count = 0;
    std::uint64_t offset = 0;
    while (offset < logical_bytes)
    {
        const std::uint8_t value = ReadU8(bytes, offset);
        if (value >= 0x20 && value <= 0x7E)
        {
            if (value == kColon)
                ++colon_count;
            ++offset;
            continue;
        }
        if (value == kLineFeed)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::Malformed, "SKAS text contains a bare line feed", offset));
        }
        if (value != kCarriageReturn)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::Malformed, "SKAS text contains a non-printable byte", offset));
        }
        if (offset + 1 >= logical_bytes || ReadU8(bytes, offset + 1) != kLineFeed)
        {
            return std::unexpected(
                Error(asset::DecodeErrorCode::Malformed, "SKAS text contains a bare carriage return", offset));
        }

        ++scan.line_count;
        if (offset == line_start)
            ++scan.blank_line_count;
        if (colon_count == 1)
            ++scan.single_colon_line_count;
        offset += 2;
        line_start = offset;
        colon_count = 0;
    }

    if (line_start != logical_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "SKAS text does not end with CRLF", line_start));
    }
    if (scan.line_count != kSkasLineCount)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant, "SKAS line count is outside the observed envelope"));
    }
    if (scan.blank_line_count != kSkasBlankLineCount)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "SKAS blank-line count is outside the observed envelope"));
    }
    if (scan.single_colon_line_count != kSkasSingleColonLineCount)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "SKAS single-colon-line count is outside the observed envelope"));
    }
    return scan;
}

void PopulateLines(asset::SkasTextEnvelopeIR &envelope) noexcept
{
    std::uint64_t line_start = 0;
    std::size_t line_index = 0;
    for (std::uint64_t offset = 0; offset < envelope.logical_text.size(); ++offset)
    {
        if (static_cast<std::uint8_t>(envelope.logical_text[static_cast<std::size_t>(offset)]) != kCarriageReturn)
        {
            continue;
        }
        envelope.lines[line_index++] = asset::SkasOpaqueTextLineIR{
            .text_offset = static_cast<std::uint32_t>(line_start),
            .text_bytes = static_cast<std::uint32_t>(offset - line_start),
            .terminator_bytes = 2,
        };
        ++offset;
        line_start = offset + 1;
    }
}
} // namespace

asset::DecodeResult<asset::SkasTextEnvelopeIR> DecodeSkasTextEnvelope(const std::span<const std::byte> bytes,
                                                                      const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "SKAS input exceeds the caller byte limit"));
    }
    if (bytes.size() > kSkasMaximumPhysicalBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "SKAS input exceeds the fixed physical-size ceiling"));
    }
    if (bytes.size() < kSkasMinimumPhysicalBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant, "SKAS input is below the observed physical-size range"));
    }
    if (kSkasMaximumDecodedItems > limits.maximum_items)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "SKAS root and lines exceed the caller item limit"));
    }

    const std::uint64_t input_bytes = bytes.size();
    std::uint64_t logical_bytes = input_bytes;
    for (std::uint64_t offset = 0; offset < input_bytes; ++offset)
    {
        if (ReadU8(bytes, offset) == kNul)
        {
            logical_bytes = offset;
            break;
        }
    }
    if (logical_bytes == input_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "SKAS input has no trailing NUL padding", input_bytes));
    }
    for (std::uint64_t offset = logical_bytes; offset < input_bytes; ++offset)
    {
        if (ReadU8(bytes, offset) != kNul)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                         "SKAS bytes after the logical end are not zero padding", offset));
        }
    }

    const std::uint64_t padding_bytes = input_bytes - logical_bytes;
    if (padding_bytes < kSkasMinimumZeroPaddingBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed, "SKAS input has no trailing NUL padding", logical_bytes));
    }
    if (padding_bytes > kSkasMaximumZeroPaddingBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "SKAS zero padding exceeds the fixed decoder ceiling",
                                     logical_bytes + kSkasMaximumZeroPaddingBytes));
    }
    if (logical_bytes < kSkasMinimumLogicalTextBytes || logical_bytes > kSkasMaximumLogicalTextBytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::UnsupportedVariant, "SKAS logical text is outside the observed byte range"));
    }
    if (logical_bytes > limits.maximum_string_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "SKAS logical text exceeds the caller string limit"));
    }

    auto scan = ScanText(bytes, logical_bytes);
    if (!scan)
        return std::unexpected(scan.error());

    std::uint64_t line_output_bytes = 0;
    std::uint64_t output_bytes = 0;
    if (!Multiply(kSkasLineCount, sizeof(asset::SkasOpaqueTextLineIR), line_output_bytes) ||
        !Add(sizeof(asset::SkasTextEnvelopeIR), logical_bytes, output_bytes) ||
        !Add(output_bytes, line_output_bytes, output_bytes))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow, "SKAS decoded output size overflows"));
    }
    if (output_bytes > kSkasMaximumLogicalOutputBytes || output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "SKAS decoded output exceeds a decoder limit"));
    }

    try
    {
        std::string logical_text(static_cast<std::size_t>(logical_bytes), '\0');
        std::memcpy(logical_text.data(), bytes.data(), static_cast<std::size_t>(logical_bytes));
        std::vector<asset::SkasOpaqueTextLineIR> lines(static_cast<std::size_t>(kSkasLineCount));
        asset::SkasTextEnvelopeIR envelope{
            .padding_bytes = static_cast<std::uint32_t>(padding_bytes),
            .blank_line_count = static_cast<std::uint32_t>(scan->blank_line_count),
            .single_colon_line_count = static_cast<std::uint32_t>(scan->single_colon_line_count),
            .logical_text = std::move(logical_text),
            .lines = std::move(lines),
        };
        PopulateLines(envelope);
        return envelope;
    }
    catch (const std::bad_alloc &)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "SKAS allocation"));
    }
    catch (const std::length_error &)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "SKAS length"));
    }
}
} // namespace omega::retail
