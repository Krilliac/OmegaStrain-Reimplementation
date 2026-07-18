#include "omega/retail/vum_material_catalog_decoder.h"

#include "vum_layout_internal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
constexpr std::uint64_t kNameRegionOffset = detail::kVumNameRegionOffset;
constexpr std::uint64_t kMaterialRecordBytes = detail::kVumMaterialRecordBytes;
constexpr std::uint32_t kInactiveWord = 0xFFFFFFFFU;

struct CatalogLayout
{
    std::uint32_t name_count = 0;
    std::uint32_t material_count = 0;
    std::uint32_t names_end = 0;
};

[[nodiscard]] asset::DecodeError Error(const asset::DecodeErrorCode code,
    std::string message, const std::optional<std::uint64_t> byte_offset = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = byte_offset,
        .message = std::move(message),
    };
}

[[nodiscard]] std::uint32_t ReadU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return detail::ReadVumU32(bytes, offset);
}

[[nodiscard]] float ReadF32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::bit_cast<float>(ReadU32(bytes, offset));
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

[[nodiscard]] asset::DecodeResult<void> Accumulate(std::uint64_t& total,
    const std::uint64_t amount, const std::uint64_t limit, const char* overflow_message,
    const char* limit_message)
{
    std::uint64_t next = 0;
    if (!Add(total, amount, next))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow, overflow_message));
    if (next > limit)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded, limit_message));
    total = next;
    return {};
}

[[nodiscard]] bool IsObservedUsageFamily(const std::span<const std::byte> record,
    const std::uint32_t active_count) noexcept
{
    const std::array<std::uint32_t, 3> usage{
        ReadU32(record, 72), ReadU32(record, 76), ReadU32(record, 80)};
    if (active_count == 1)
        return usage[0] == 2 || usage[0] == 13;
    if (active_count == 2)
        return usage[0] == 2 &&
            (usage[1] == 11 || usage[1] == 10 || usage[1] == 2);
    return usage == std::array<std::uint32_t, 3>{2, 12, 14};
}

[[nodiscard]] asset::DecodeResult<CatalogLayout> ParseLayout(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto payload_layout = detail::ValidateVumPayloadLayout(bytes, limits);
    if (!payload_layout)
        return std::unexpected(payload_layout.error());
    const std::uint32_t name_count = payload_layout->name_count;
    const std::uint32_t material_count = payload_layout->material_count;
    const std::uint32_t names_end = payload_layout->names_end;

    if (names_end == kNameRegionOffset || bytes[names_end - 1U] != std::byte{0})
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog name region is empty or lacks a final terminator", names_end));
    std::uint64_t observed_names = 0;
    std::uint64_t total_name_bytes = 0;
    std::uint64_t current_name_bytes = 0;
    for (std::uint64_t offset = kNameRegionOffset; offset < names_end; ++offset)
    {
        const std::uint8_t value = std::to_integer<std::uint8_t>(bytes[offset]);
        if (value == 0)
        {
            if (current_name_bytes == 0)
                continue;
            ++observed_names;
            current_name_bytes = 0;
            if (observed_names > name_count)
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "VUM catalog name count exceeds its declared count", offset));
            continue;
        }
        if (value < 32 || value > 126)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM catalog name uses an unsupported character", offset));
        ++current_name_bytes;
        ++total_name_bytes;
        if (current_name_bytes > limits.maximum_string_bytes)
            return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                "VUM catalog name exceeds decoder string limit", offset));
    }
    if (observed_names != name_count)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog name count contradicts its name region", 20));

    std::uint64_t active_name_references = 0;
    constexpr std::array<std::size_t, 3> reference_offsets{56, 60, 64};
    constexpr std::array<std::size_t, 3> usage_offsets{72, 76, 80};
    for (std::uint64_t index = 0; index < material_count; ++index)
    {
        const std::uint64_t record_offset = names_end + index * kMaterialRecordBytes;
        const auto record = bytes.subspan(
            static_cast<std::size_t>(record_offset), kMaterialRecordBytes);
        if (record[0] != std::byte{'M'} || record[1] != std::byte{'T'} ||
            record[2] != std::byte{'R'} || record[3] != std::byte{'L'})
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "VUM catalog record does not use the observed MTRL prefix", record_offset));
        if (!std::ranges::all_of(record.subspan(16, 40),
                [](const std::byte value) { return value == std::byte{0}; }))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM catalog record uses an unsupported reserved-byte layout",
                record_offset + 16U));
        if (!std::isfinite(ReadF32(record, 8)) || !std::isfinite(ReadF32(record, 12)))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM catalog record uses an unsupported non-finite scalar", record_offset + 8U));
        if (ReadU32(record, 68) != kInactiveWord || ReadU32(record, 84) != kInactiveWord)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM catalog record uses an unsupported sentinel layout", record_offset + 68U));
        const std::uint32_t active_count = ReadU32(record, 88);
        if (active_count < 1 || active_count > 3 ||
            !IsObservedUsageFamily(record, active_count))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM catalog record uses an unsupported dense-reference family",
                record_offset + 88U));
        for (std::size_t slot = 0; slot < reference_offsets.size(); ++slot)
        {
            const std::uint32_t reference = ReadU32(record, reference_offsets[slot]);
            const std::uint32_t usage = ReadU32(record, usage_offsets[slot]);
            if (slot < active_count)
            {
                if (reference >= name_count)
                    return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                        "VUM catalog record name index is out of range",
                        record_offset + reference_offsets[slot]));
            }
            else if (reference != kInactiveWord || usage != kInactiveWord)
                return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                    "VUM catalog record inactive slot does not use the observed sentinel",
                    record_offset + reference_offsets[slot]));
        }
        if (!Add(active_name_references, active_count, active_name_references))
            return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                "VUM catalog active-reference count overflows", record_offset + 88U));
    }

    std::uint64_t items = 1;
    auto item_result = Accumulate(items, name_count, limits.maximum_items,
        "VUM catalog item count overflows", "VUM catalog items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());
    item_result = Accumulate(items, material_count, limits.maximum_items,
        "VUM catalog item count overflows", "VUM catalog items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());
    item_result = Accumulate(items, active_name_references, limits.maximum_items,
        "VUM catalog item count overflows", "VUM catalog items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());
    item_result = Accumulate(items, payload_layout->metadata_record_count, limits.maximum_items,
        "VUM catalog item count overflows", "VUM catalog items exceed decoder limit");
    if (!item_result)
        return std::unexpected(item_result.error());

    std::uint64_t output_bytes = sizeof(asset::MaterialCatalogIR);
    std::uint64_t name_objects_bytes = 0;
    std::uint64_t material_objects_bytes = 0;
    if (!Multiply(name_count, sizeof(std::string), name_objects_bytes) ||
        !Multiply(material_count, sizeof(asset::MaterialCatalogEntryIR),
            material_objects_bytes))
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow, "VUM catalog decoded output size overflows"));
    auto output_result = Accumulate(output_bytes, name_objects_bytes,
        limits.maximum_output_bytes, "VUM catalog decoded output size overflows",
        "VUM catalog exceeds decoder output limit");
    if (!output_result)
        return std::unexpected(output_result.error());
    output_result = Accumulate(output_bytes, total_name_bytes,
        limits.maximum_output_bytes, "VUM catalog decoded output size overflows",
        "VUM catalog exceeds decoder output limit");
    if (!output_result)
        return std::unexpected(output_result.error());
    output_result = Accumulate(output_bytes, material_objects_bytes,
        limits.maximum_output_bytes, "VUM catalog decoded output size overflows",
        "VUM catalog exceeds decoder output limit");
    if (!output_result)
        return std::unexpected(output_result.error());

    return CatalogLayout{
        .name_count = name_count,
        .material_count = material_count,
        .names_end = names_end,
    };
}
} // namespace

asset::DecodeResult<asset::MaterialCatalogIR> DecodeVumMaterialCatalog(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto layout = ParseLayout(bytes, limits);
    if (!layout)
        return std::unexpected(layout.error());

    asset::MaterialCatalogIR result;
    result.names.reserve(layout->name_count);
    std::uint64_t name_start = kNameRegionOffset;
    for (std::uint64_t offset = kNameRegionOffset; offset < layout->names_end; ++offset)
    {
        if (bytes[offset] != std::byte{0})
            continue;
        if (offset != name_start)
        {
            result.names.emplace_back(
                reinterpret_cast<const char*>(bytes.data() + name_start),
                static_cast<std::size_t>(offset - name_start));
        }
        name_start = offset + 1U;
    }

    result.materials.reserve(layout->material_count);
    for (std::uint64_t index = 0; index < layout->material_count; ++index)
    {
        const std::uint64_t record_offset =
            layout->names_end + index * kMaterialRecordBytes;
        const std::uint32_t active_count =
            ReadU32(bytes, static_cast<std::size_t>(record_offset + 88U));
        asset::MaterialCatalogEntryIR material{
            .name_count = static_cast<std::uint8_t>(active_count),
        };
        for (std::size_t slot = 0; slot < active_count; ++slot)
            material.name_indices[slot] =
                ReadU32(bytes, static_cast<std::size_t>(record_offset + 56U + slot * 4U));
        result.materials.push_back(material);
    }
    return result;
}
} // namespace omega::retail
