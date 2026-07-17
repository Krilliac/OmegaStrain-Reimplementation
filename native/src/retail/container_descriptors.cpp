#include "omega/retail/container_descriptors.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kColHeaderBytes = 48;
constexpr std::uint64_t kVumHeaderBytes = 92;
constexpr std::uint64_t kTdxHeaderBytes = 64;

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

template <typename Descriptor>
[[nodiscard]] asset::DecodeResult<void> CheckFixedDescriptorLimits(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "container input exceeds decoder byte limit"));
    if (limits.maximum_items < 1)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "container descriptor exceeds decoder item limit"));
    if (limits.maximum_output_bytes < sizeof(Descriptor))
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "container descriptor exceeds decoder output limit"));
    return {};
}

[[nodiscard]] std::uint16_t ReadU16(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint16_t>(bytes[offset]) |
           (std::to_integer<std::uint16_t>(bytes[offset + 1]) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
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

[[nodiscard]] ObservedExtent ClassifyExtent(
    const std::span<const std::byte> bytes, const std::uint64_t observed_bytes)
{
    ObservedExtentRelation relation = ObservedExtentRelation::Exact;
    if (observed_bytes > bytes.size())
    {
        relation = ObservedExtentRelation::ExceedsInput;
    }
    else if (observed_bytes < bytes.size())
    {
        const auto tail = bytes.subspan(static_cast<std::size_t>(observed_bytes));
        relation = std::ranges::all_of(
                       tail, [](const std::byte value) { return value == std::byte{0}; })
            ? ObservedExtentRelation::ZeroPaddedTail
            : ObservedExtentRelation::NonzeroTail;
    }
    return ObservedExtent{
        .observed_bytes = observed_bytes,
        .input_bytes = bytes.size(),
        .relation = relation,
    };
}

[[nodiscard]] bool IsPowerOfTwo(const std::uint16_t value) noexcept
{
    return value != 0 && (value & (value - 1U)) == 0;
}

[[nodiscard]] bool IsKnownTdxPair(
    const std::uint16_t bits_per_pixel, const std::uint16_t storage_format) noexcept
{
    return (bits_per_pixel == 4 && storage_format == 0x14) ||
           (bits_per_pixel == 8 && storage_format == 0x13) ||
           (bits_per_pixel == 24 && storage_format == 0x01) ||
           (bits_per_pixel == 32 && storage_format == 0x00);
}

[[nodiscard]] bool IsKnownTdxFlagFamily(
    const std::uint16_t bits_per_pixel, const std::uint16_t flags) noexcept
{
    if (bits_per_pixel == 4 || bits_per_pixel == 8)
        return flags == 1 || flags == 3 || flags == 5 || flags == 7;
    if (bits_per_pixel == 24)
        return flags == 0;
    return bits_per_pixel == 32 && (flags == 0 || flags == 2 || flags == 4 || flags == 6);
}

} // namespace

asset::DecodeResult<ColContainerDescriptor> InspectColContainer(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto limit_result = CheckFixedDescriptorLimits<ColContainerDescriptor>(bytes, limits);
    if (!limit_result)
        return std::unexpected(limit_result.error());
    if (bytes.size() < kColHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "COL input is shorter than its observed header", bytes.size()));
    if (bytes[0] != std::byte{'C'} || bytes[1] != std::byte{'O'} ||
        bytes[2] != std::byte{'L'})
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "COL input does not use the observed prefix", 0));

    const std::uint8_t version = std::to_integer<std::uint8_t>(bytes[3]);
    if (version != 3 && version != 5)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "COL format version is not supported", 3));
    if (ReadU32(bytes, 8) != kColHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "COL does not use the observed 48-byte header", 8));
    constexpr std::array<std::size_t, 4> count_offsets{4, 12, 20, 28};
    constexpr std::array<std::size_t, 4> end_offsets{16, 24, 32, 36};
    constexpr std::array<std::uint64_t, 4> record_bytes{64, 48, 16, 16};
    ColContainerDescriptor descriptor{
        .format_version = version,
        .header_bytes = static_cast<std::uint32_t>(kColHeaderBytes),
    };
    std::uint64_t cursor = kColHeaderBytes;
    for (std::size_t index = 0; index < descriptor.counted_tables.size(); ++index)
    {
        const std::uint32_t count = ReadU32(bytes, count_offsets[index]);
        std::uint64_t table_bytes = 0;
        std::uint64_t computed_end = 0;
        if (!Multiply(count, record_bytes[index], table_bytes) ||
            !Add(cursor, table_bytes, computed_end))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "COL counted table extent overflows", count_offsets[index]));
        if (computed_end != ReadU32(bytes, end_offsets[index]))
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "COL counted table endpoint contradicts its record count", end_offsets[index]));
        if (computed_end > bytes.size())
            return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                "COL counted table extends past the input", bytes.size()));
        descriptor.observed_record_counts[index] = count;
        descriptor.counted_tables[index] = ObservedByteRange{
            .offset = cursor,
            .size = table_bytes,
        };
        cursor = computed_end;
    }

    descriptor.observed_word_0x28 = ReadU32(bytes, 40);
    const std::uint64_t described_end = ReadU32(bytes, 44);
    if (described_end < cursor || described_end % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "COL trailing endpoint is non-monotonic or misaligned", 44));
    if (described_end > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "COL trailing section extends past the input", bytes.size()));
    if (bytes.size() % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "COL container span is not 16-byte aligned"));
    descriptor.uncounted_table_region = ObservedByteRange{
        .offset = cursor,
        .size = described_end - cursor,
    };
    descriptor.described_tables_extent = ClassifyExtent(bytes, described_end);
    return descriptor;
}

asset::DecodeResult<VumContainerDescriptor> InspectVumContainer(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto limit_result = CheckFixedDescriptorLimits<VumContainerDescriptor>(bytes, limits);
    if (!limit_result)
        return std::unexpected(limit_result.error());
    if (bytes.size() < kVumHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "VUM input is shorter than its observed prefix", bytes.size()));
    if (bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'U'} ||
        bytes[2] != std::byte{'M'} || bytes[3] != std::byte{'S'})
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM input does not use the observed VUMS prefix", 0));
    const std::array<std::uint32_t, 3> boundaries{
        ReadU32(bytes, 80), ReadU32(bytes, 84), ReadU32(bytes, 88)};
    if (boundaries[0] < kVumHeaderBytes || boundaries[0] % 4U != 0 ||
        boundaries[1] % 4U != 0 || boundaries[2] % 16U != 0 ||
        boundaries[0] > boundaries[1] || boundaries[1] > boundaries[2])
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM observed boundaries are out of order or misaligned", 80));
    if (boundaries[2] > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "VUM primary boundary extends past the input", bytes.size()));
    if (bytes.size() % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM container span is not 16-byte aligned"));

    return VumContainerDescriptor{
        .observed_variant = ReadU32(bytes, 4),
        .observed_word_0x1c = ReadU32(bytes, 28),
        .observed_boundaries = boundaries,
        .primary_extent = ClassifyExtent(bytes, boundaries[2]),
    };
}

asset::DecodeResult<TdxContainerDescriptor> InspectTdxContainer(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto limit_result = CheckFixedDescriptorLimits<TdxContainerDescriptor>(bytes, limits);
    if (!limit_result)
        return std::unexpected(limit_result.error());
    if (bytes.size() < kTdxHeaderBytes)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "TDX input is shorter than its observed header", bytes.size()));
    if (bytes.size() % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "TDX container span is not 16-byte aligned"));

    const std::uint16_t version = ReadU16(bytes, 0);
    if (version != 5)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX format version is not supported", 0));
    const std::uint16_t flags = ReadU16(bytes, 2);
    const std::uint16_t width = ReadU16(bytes, 4);
    const std::uint16_t height = ReadU16(bytes, 6);
    if (width == 0 || height == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "TDX dimensions must be nonzero", 4));
    if (!IsPowerOfTwo(width) || !IsPowerOfTwo(height))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX dimensions are outside the observed power-of-two family", 4));
    const std::uint16_t bits_per_pixel = ReadU16(bytes, 8);
    const std::uint16_t storage_format = ReadU16(bytes, 10);
    if (!IsKnownTdxPair(bits_per_pixel, storage_format))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX bit-depth and storage-format pair is not supported", 8));
    if (!IsKnownTdxFlagFamily(bits_per_pixel, flags))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX flags are outside the observed bit-depth family", 2));
    constexpr std::array<std::size_t, 7> layout_word_offsets{16, 18, 20, 24, 26, 38, 54};
    const bool indexed_family = bits_per_pixel == 4 || bits_per_pixel == 8;
    const std::array<std::uint32_t, 7> expected_layout_words{
        bits_per_pixel == 4 ? 8U : bits_per_pixel == 8 ? 16U : 0U,
        bits_per_pixel == 4 ? 2U : bits_per_pixel == 8 ? 16U : 0U,
        indexed_family ? 32U : 0U,
        indexed_family ? 1U : 0U,
        indexed_family ? 4U : 0U,
        indexed_family ? 1U : 0U,
        indexed_family ? 128U : 0U,
    };
    for (std::size_t index = 0; index < layout_word_offsets.size(); ++index)
    {
        if (ReadU16(bytes, layout_word_offsets[index]) != expected_layout_words[index])
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX opaque layout signature is not supported", layout_word_offsets[index]));
    }
    constexpr std::array<std::uint16_t, 9> observed_block_counts{
        1, 2, 3, 4, 6, 8, 10, 11, 19};
    const std::uint16_t block_count = ReadU16(bytes, 34);
    if (std::ranges::find(observed_block_counts, block_count) ==
        observed_block_counts.end())
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX block count is outside the observed family", 34));
    const std::uint16_t primary_plane_count = ReadU16(bytes, 36);
    if (primary_plane_count < 1 || primary_plane_count > 4 ||
        ReadU16(bytes, 52) != 128U * primary_plane_count)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX primary-plane count and descriptor extent are not supported", 36));
    const std::uint16_t palette_plane_count = ReadU16(bytes, 38);
    const std::uint16_t width_unit_word = ReadU16(bytes, 12);
    const std::uint16_t minimum_width_units = bits_per_pixel <= 8 ? 2 : 1;
    const std::uint16_t expected_width_units =
        std::max<std::uint16_t>(minimum_width_units, width / 64U);
    if (width_unit_word != expected_width_units)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX width-unit word is outside the observed family", 12));
    if (ReadU32(bytes, 60) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX observed reserved word is not supported", 60));
    constexpr std::array<std::size_t, 5> zero_word_offsets{22, 30, 40, 48, 50};
    for (const std::size_t offset : zero_word_offsets)
    {
        if (ReadU16(bytes, offset) != 0)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "TDX observed zero layout word is not supported", offset));
    }

    const std::uint16_t storage_unit_word = ReadU16(bytes, 14);
    const std::uint32_t block_stride = ReadU32(bytes, 56);
    if (block_stride < 32U)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "TDX block stride is shorter than its pointer header", 56));
    std::uint64_t counted_block_bytes = 0;
    std::uint64_t observed_total_bytes = 0;
    if (!Multiply(block_count, block_stride, counted_block_bytes) ||
        !Add(kTdxHeaderBytes, counted_block_bytes, observed_total_bytes))
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "TDX counted block extent overflows", 56));
    const ObservedExtent extent = ClassifyExtent(bytes, observed_total_bytes);
    std::optional<ObservedByteRange> bounded_region;
    if (extent.relation == ObservedExtentRelation::Exact ||
        extent.relation == ObservedExtentRelation::ZeroPaddedTail)
        bounded_region = ObservedByteRange{
            .offset = kTdxHeaderBytes,
            .size = counted_block_bytes,
        };

    std::uint64_t area_units = 0;
    std::uint64_t area_bits = 0;
    const bool area_bit_formula_valid = Multiply(width, height, area_units) &&
        Multiply(area_units, bits_per_pixel, area_bits) && area_bits % 8U == 0;
    const std::uint64_t observed_storage_bytes =
        static_cast<std::uint64_t>(storage_unit_word) * 256U;
    return TdxContainerDescriptor{
        .format_version = version,
        .observed_flags = flags,
        .width = width,
        .height = height,
        .bits_per_pixel = bits_per_pixel,
        .observed_storage_format_code = storage_format,
        .observed_width_unit_word = width_unit_word,
        .observed_storage_unit_word = storage_unit_word,
        .block_count = block_count,
        .primary_plane_count = primary_plane_count,
        .palette_plane_count = palette_plane_count,
        .block_stride = block_stride,
        .storage_word_matches_area_bit_formula =
            area_bit_formula_valid && area_bits / 8U == observed_storage_bytes,
        .counted_blocks_extent = extent,
        .bounded_blocks_region = bounded_region,
    };
}
} // namespace omega::retail
