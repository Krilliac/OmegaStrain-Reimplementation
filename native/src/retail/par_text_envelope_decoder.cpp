#include "omega/retail/par_text_envelope_decoder.h"

#include <array>
#include <cstring>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kMinimumPhysicalBytes = 2048;
constexpr std::uint64_t kMaximumPhysicalBytes = 4096;
constexpr std::uint64_t kMaximumPaddingBytes = 2040;
constexpr std::uint64_t kMaximumLogicalTextBytes = kMaximumPhysicalBytes - 1;
// An 18-byte minimum first line, then empty CRLF lines and one final byte,
// derives this ceiling.
constexpr std::uint64_t kMaximumLineCount = 2040;
constexpr std::uint8_t kNul = 0x00;
constexpr std::uint8_t kCarriageReturn = 0x0D;
constexpr std::uint8_t kLineFeed = 0x0A;
constexpr std::string_view kVersionLiteral = ";version";

struct VersionToken
{
    std::string_view text;
    asset::ParDeclaredVersion version;
};

constexpr std::array<VersionToken, 8> kObservedVersions{{
    {"1.300000", asset::ParDeclaredVersion::Version1_3},
    {"1.400000", asset::ParDeclaredVersion::Version1_4},
    {"1.500000", asset::ParDeclaredVersion::Version1_5},
    {"1.700000", asset::ParDeclaredVersion::Version1_7},
    {"1.800000", asset::ParDeclaredVersion::Version1_8},
    {"1.900000", asset::ParDeclaredVersion::Version1_9},
    {"2.000000", asset::ParDeclaredVersion::Version2_0},
    {"2.100000", asset::ParDeclaredVersion::Version2_1},
}};

struct TextScan
{
    std::uint64_t line_count = 0;
    std::uint64_t first_line_bytes = 0;
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

[[nodiscard]] std::uint8_t ReadU8(const std::span<const std::byte> bytes,
                                  const std::uint64_t offset) noexcept
{
    return std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset)]);
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
}

[[nodiscard]] bool IsDigit(const std::uint8_t value) noexcept
{
    return value >= static_cast<std::uint8_t>('0') && value <= static_cast<std::uint8_t>('9');
}

[[nodiscard]] bool IsMarkerWhitespace(const std::uint8_t value) noexcept
{
    return value == 0x09 || value == 0x0B || value == 0x0C || value == 0x20;
}

[[nodiscard]] asset::DecodeResult<asset::ParDeclaredVersion> ParseDeclaredVersion(
    const std::span<const std::byte> bytes, const std::uint64_t first_line_bytes)
{
    std::uint64_t token_end = 0;
    std::uint64_t dot_count = 0;
    std::optional<std::uint64_t> dot_offset;
    while (token_end < first_line_bytes)
    {
        const std::uint8_t value = ReadU8(bytes, token_end);
        if (value == static_cast<std::uint8_t>('.'))
        {
            ++dot_count;
            dot_offset = token_end;
            ++token_end;
            continue;
        }
        if (!IsDigit(value))
            break;
        ++token_end;
    }

    if (token_end == 0 || dot_count != 1 || !dot_offset || *dot_offset == 0 ||
        *dot_offset + 1 == token_end)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "PAR declared-version token is malformed", token_end));
    }

    std::optional<asset::ParDeclaredVersion> declared_version;
    for (const VersionToken& observed : kObservedVersions)
    {
        if (token_end != observed.text.size())
            continue;

        bool equal = true;
        for (std::uint64_t index = 0; index < token_end; ++index)
        {
            if (ReadU8(bytes, index) !=
                static_cast<std::uint8_t>(observed.text[static_cast<std::size_t>(index)]))
            {
                equal = false;
                break;
            }
        }
        if (equal)
        {
            declared_version = observed.version;
            break;
        }
    }
    if (!declared_version)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PAR declared-version token is outside the observed set", 0));
    }

    std::uint64_t cursor = token_end;
    while (cursor < first_line_bytes && IsMarkerWhitespace(ReadU8(bytes, cursor)))
        ++cursor;

    const std::uint64_t remaining = first_line_bytes - cursor;
    if (remaining != kVersionLiteral.size())
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "PAR first line does not end with the required version marker",
                                     cursor));
    }
    for (std::uint64_t index = 0; index < kVersionLiteral.size(); ++index)
    {
        if (ReadU8(bytes, cursor + index) !=
            static_cast<std::uint8_t>(kVersionLiteral[static_cast<std::size_t>(index)]))
        {
            return std::unexpected(Error(
                asset::DecodeErrorCode::Malformed,
                "PAR first line does not contain the required version marker", cursor + index));
        }
    }
    return *declared_version;
}

[[nodiscard]] asset::DecodeResult<TextScan> ScanText(const std::span<const std::byte> bytes,
                                                     const std::uint64_t logical_bytes)
{
    TextScan scan;
    std::uint64_t line_start = 0;
    bool captured_first_line = false;
    std::uint64_t offset = 0;
    while (offset < logical_bytes)
    {
        const std::uint8_t value = ReadU8(bytes, offset);
        if (value > 0x7F)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                         "PAR logical text contains a non-ASCII byte", offset));
        }
        if (value == kLineFeed)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                         "PAR logical text contains a bare line feed", offset));
        }
        if (value != kCarriageReturn)
        {
            ++offset;
            continue;
        }
        if (offset + 1 >= logical_bytes || ReadU8(bytes, offset + 1) != kLineFeed)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                         "PAR logical text contains a bare carriage return",
                                         offset));
        }
        if (!captured_first_line)
        {
            scan.first_line_bytes = offset;
            captured_first_line = true;
        }
        ++scan.line_count;
        offset += 2;
        line_start = offset;
    }

    if (!captured_first_line)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "PAR first line is not terminated by CRLF", logical_bytes));
    }
    if (line_start < logical_bytes)
        ++scan.line_count;
    if (scan.line_count > kMaximumLineCount)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR line count exceeds the fixed decoder ceiling"));
    }
    return scan;
}

void PopulateLines(asset::ParTextEnvelopeIR& envelope) noexcept
{
    const std::uint64_t logical_bytes = envelope.logical_text.size();
    std::uint64_t line_start = 0;
    std::size_t line_index = 0;
    std::uint64_t offset = 0;
    while (offset < logical_bytes)
    {
        if (static_cast<std::uint8_t>(envelope.logical_text[static_cast<std::size_t>(offset)]) !=
            kCarriageReturn)
        {
            ++offset;
            continue;
        }

        envelope.lines[line_index++] = asset::ParOpaqueTextLineIR{
            .text_offset = static_cast<std::uint32_t>(line_start),
            .text_bytes = static_cast<std::uint32_t>(offset - line_start),
            .terminator_bytes = 2,
        };
        offset += 2;
        line_start = offset;
    }
    if (line_start < logical_bytes)
    {
        envelope.lines[line_index] = asset::ParOpaqueTextLineIR{
            .text_offset = static_cast<std::uint32_t>(line_start),
            .text_bytes = static_cast<std::uint32_t>(logical_bytes - line_start),
            .terminator_bytes = 0,
        };
    }
}
} // namespace

asset::DecodeResult<asset::ParTextEnvelopeIR> DecodeParTextEnvelope(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR input exceeds the caller byte limit"));
    }
    if (bytes.size() > kMaximumPhysicalBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR input exceeds the fixed physical-size ceiling"));
    }
    if (bytes.size() < kMinimumPhysicalBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "PAR input is below the observed physical-size range"));
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
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "PAR input has no trailing NUL padding", input_bytes));
    }
    for (std::uint64_t offset = logical_bytes; offset < input_bytes; ++offset)
    {
        if (ReadU8(bytes, offset) != kNul)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                         "PAR bytes after the logical end are not zero padding",
                                         offset));
        }
    }

    const std::uint64_t padding_bytes = input_bytes - logical_bytes;
    if (padding_bytes > kMaximumPaddingBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR zero padding exceeds the fixed decoder ceiling",
                                     logical_bytes));
    }
    if (logical_bytes > kMaximumLogicalTextBytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR logical text exceeds the fixed decoder ceiling"));
    }
    if (logical_bytes > limits.maximum_string_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR logical text exceeds the caller string limit"));
    }

    auto scan = ScanText(bytes, logical_bytes);
    if (!scan)
        return std::unexpected(scan.error());
    auto version = ParseDeclaredVersion(bytes, scan->first_line_bytes);
    if (!version)
        return std::unexpected(version.error());

    std::uint64_t item_count = 0;
    if (!Add(1, scan->line_count, item_count))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PAR output item count overflows"));
    }
    if (item_count > 1 + kMaximumLineCount || item_count > limits.maximum_items)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR output item count exceeds a decoder limit"));
    }

    std::uint64_t line_output_bytes = 0;
    std::uint64_t output_bytes = 0;
    if (!Multiply(scan->line_count, sizeof(asset::ParOpaqueTextLineIR), line_output_bytes) ||
        !Add(sizeof(asset::ParTextEnvelopeIR), logical_bytes, output_bytes) ||
        !Add(output_bytes, line_output_bytes, output_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "PAR output size overflows"));
    }
    constexpr std::uint64_t kMaximumOutputBytes =
        sizeof(asset::ParTextEnvelopeIR) + kMaximumLogicalTextBytes +
        kMaximumLineCount * sizeof(asset::ParOpaqueTextLineIR);
    if (output_bytes > kMaximumOutputBytes || output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "PAR output size exceeds a decoder limit"));
    }

    try
    {
        // Construct both owning buffers at their final sizes. In particular, do not
        // default-create a vector before the allocation boundary: the MSVC Debug
        // iterator proxy may allocate.
        std::string logical_text(static_cast<std::size_t>(logical_bytes), '\0');
        std::memcpy(logical_text.data(), bytes.data(), static_cast<std::size_t>(logical_bytes));
        std::vector<asset::ParOpaqueTextLineIR> lines(static_cast<std::size_t>(scan->line_count));
        asset::ParTextEnvelopeIR envelope{
            .declared_version = *version,
            .padding_bytes = static_cast<std::uint32_t>(padding_bytes),
            .logical_text = std::move(logical_text),
            .lines = std::move(lines),
        };
        PopulateLines(envelope);
        return envelope;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "PAR alloc fail"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "PAR alloc fail"));
    }
}
} // namespace omega::retail
