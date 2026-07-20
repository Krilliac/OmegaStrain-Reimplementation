#include "omega/retail/lpd_envelope_decoder.h"

#include <algorithm>
#include <array>
#include <iterator>
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
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint64_t kFixedDecodedItems = 1 + asset::kLpdSourceTrackCount;

struct LpdLayout
{
    std::array<std::uint32_t, asset::kLpdSourceTrackCount> counts{};
    std::uint64_t logical_bytes = kLpdHeaderBytes;
};

template <std::size_t... TrackIndices>
[[nodiscard]] asset::LpdEnvelopeIR AllocateEnvelope(
    const std::array<std::uint32_t, asset::kLpdSourceTrackCount>& counts,
    std::index_sequence<TrackIndices...>)
{
    return asset::LpdEnvelopeIR{
        .tracks = {asset::LpdTrackIR{
            .entries = std::vector<std::array<std::byte, 4>>(counts[TrackIndices])}...},
    };
}

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

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3U]) << 24U);
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

[[nodiscard]] asset::DecodeResult<LpdLayout> Preflight(const std::span<const std::byte> bytes,
                                                       const asset::DecodeLimits limits)
{
    if (bytes.size() > kLpdMaximumInputBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "LPD input exceeds the fixed decoder byte limit"));
    if (bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "LPD input exceeds decoder byte limit"));
    if (kFixedDecodedItems > limits.maximum_items)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "LPD root and source tracks exceed decoder item limit"));
    if (sizeof(asset::LpdEnvelopeIR) > limits.maximum_output_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded, "LPD root exceeds decoder output limit"));

    if (bytes.size() < kWordBytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "LPD header word is truncated", bytes.size()));
    if (ReadU32(bytes, 0) != kLpdHeaderWordCount)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                     "LPD header word count is not supported", 0));
    if (bytes.size() < kLpdHeaderBytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Truncated, "LPD count table is truncated", bytes.size()));

    LpdLayout layout;
    std::uint64_t decoded_entries = 0;
    std::uint64_t decoded_items = kFixedDecodedItems;
    std::uint64_t output_bytes = sizeof(asset::LpdEnvelopeIR);
    for (std::size_t track_index = 0; track_index < layout.counts.size(); ++track_index)
    {
        const std::uint64_t count_offset = kWordBytes * (track_index + 1U);
        const std::uint32_t count = ReadU32(bytes, static_cast<std::size_t>(count_offset));
        layout.counts[track_index] = count;

        std::uint64_t next_entries = 0;
        if (!Add(decoded_entries, count, next_entries))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "LPD decoded entry count overflows", count_offset));
        if (next_entries > kLpdMaximumEntryCount)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "LPD entries exceed the fixed decoder limit",
                                         count_offset));
        decoded_entries = next_entries;

        std::uint64_t next_items = 0;
        if (!Add(decoded_items, count, next_items))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "LPD decoded item count overflows", count_offset));
        if (next_items > kLpdMaximumDecodedItems)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "LPD items exceed the fixed decoder limit", count_offset));
        if (next_items > limits.maximum_items)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "LPD entries exceed decoder item limit", count_offset));
        decoded_items = next_items;

        std::uint64_t entry_bytes = 0;
        if (!Multiply(count, kWordBytes, entry_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "LPD track byte count overflows", count_offset));
        std::uint64_t next_output_bytes = 0;
        if (!Add(output_bytes, entry_bytes, next_output_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "LPD decoded output size overflows", count_offset));
        if (next_output_bytes > kLpdMaximumLogicalOutputBytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "LPD output exceeds the fixed decoder limit",
                                         count_offset));
        if (next_output_bytes > limits.maximum_output_bytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                         "LPD entries exceed decoder output limit", count_offset));
        output_bytes = next_output_bytes;

        std::uint64_t next_logical_bytes = 0;
        if (!Add(layout.logical_bytes, entry_bytes, next_logical_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                         "LPD logical extent overflows", count_offset));
        layout.logical_bytes = next_logical_bytes;
    }

    if (layout.logical_bytes > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                     "LPD counted payload is truncated", bytes.size()));

    const std::uint64_t tail_bytes = bytes.size() - layout.logical_bytes;
    const std::uint64_t inspected_tail_bytes = std::min(tail_bytes, kLpdMaximumZeroTailBytes);
    const auto inspected_tail = bytes.subspan(static_cast<std::size_t>(layout.logical_bytes),
                                              static_cast<std::size_t>(inspected_tail_bytes));
    const auto nonzero = std::find_if(inspected_tail.begin(), inspected_tail.end(),
                                      [](const std::byte value) { return value != std::byte{0}; });
    if (nonzero != inspected_tail.end())
    {
        const std::uint64_t offset =
            layout.logical_bytes +
            static_cast<std::uint64_t>(std::distance(inspected_tail.begin(), nonzero));
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "LPD trailing region contains a nonzero byte", offset));
    }
    if (tail_bytes > kLpdMaximumZeroTailBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                     "LPD zero tail exceeds the fixed decoder limit",
                                     layout.logical_bytes + kLpdMaximumZeroTailBytes));
    return layout;
}
} // namespace

asset::DecodeResult<asset::LpdEnvelopeIR> DecodeLpdEnvelope(const std::span<const std::byte> bytes,
                                                            const asset::DecodeLimits limits)
{
    auto layout = Preflight(bytes, limits);
    if (!layout)
        return std::unexpected(layout.error());

    try
    {
        auto envelope = AllocateEnvelope(layout->counts,
                                         std::make_index_sequence<asset::kLpdSourceTrackCount>{});
        std::size_t source_offset = static_cast<std::size_t>(kLpdHeaderBytes);
        for (std::size_t track_index = 0; track_index < envelope.tracks.size(); ++track_index)
        {
            auto& entries = envelope.tracks[track_index].entries;
            for (auto& entry : entries)
            {
                std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(source_offset),
                            entry.size(), entry.begin());
                source_offset += entry.size();
            }
        }
        return envelope;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "LPD allocation"));
    }
    catch (const std::length_error&)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, "LPD length"));
    }
}
} // namespace omega::retail
