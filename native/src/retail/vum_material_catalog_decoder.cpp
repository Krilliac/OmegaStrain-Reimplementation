#include "omega/retail/vum_material_catalog_decoder.h"

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
constexpr std::uint64_t kHeaderBytes = 92;
constexpr std::uint64_t kPreambleFixedBytes = 20;
constexpr std::uint64_t kNameRegionOffset = kHeaderBytes + kPreambleFixedBytes;
constexpr std::uint64_t kMaterialRecordBytes = 92;
constexpr std::uint64_t kMetadataRecordBytes = 16;
constexpr std::uint32_t kInactiveWord = 0xFFFFFFFFU;

enum class MetadataRecordKind
{
    P,
    Q,
    T,
    Unknown,
};

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
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
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

[[nodiscard]] bool InRangeAligned(const std::uint32_t value, const std::uint32_t begin,
    const std::uint32_t end, const std::uint32_t alignment) noexcept
{
    return value >= begin && value < end && value % alignment == 0;
}

[[nodiscard]] bool StrictlyInsideAligned(const std::uint32_t value,
    const std::uint32_t begin, const std::uint32_t end,
    const std::uint32_t alignment) noexcept
{
    return value > begin && value < end && value % alignment == 0;
}

[[nodiscard]] MetadataRecordKind ClassifyMetadataRecord(
    const std::span<const std::byte> record, const std::uint32_t metadata_begin,
    const std::uint32_t metadata_end, const std::uint32_t payload_a,
    const std::uint32_t payload_b, const std::uint32_t primary_end) noexcept
{
    const std::array<std::uint32_t, 4> words{
        ReadU32(record, 0), ReadU32(record, 4), ReadU32(record, 8), ReadU32(record, 12)};
    if (InRangeAligned(words[2], metadata_begin, metadata_end, 16))
        return MetadataRecordKind::T;
    if (InRangeAligned(words[1], payload_a, payload_b, 16) &&
        StrictlyInsideAligned(words[3], payload_b, primary_end, 4))
        return MetadataRecordKind::Q;
    if (StrictlyInsideAligned(words[0], payload_b, primary_end, 4) &&
        StrictlyInsideAligned(words[2], payload_b, primary_end, 4) &&
        StrictlyInsideAligned(words[3], payload_b, primary_end, 4))
        return MetadataRecordKind::P;
    return MetadataRecordKind::Unknown;
}

[[nodiscard]] bool IsObservedQSpan(const std::uint32_t bytes) noexcept
{
    return bytes == 16 || bytes == 256 || bytes == 480 || bytes == 704;
}

[[nodiscard]] asset::DecodeResult<std::uint64_t> ValidatePayloadMetadata(
    const std::span<const std::byte> bytes, const std::uint32_t materials_end,
    const std::uint32_t payload_a, const std::uint32_t payload_b,
    const std::uint32_t primary_end)
{
    const std::uint32_t paired_count_word = ReadU32(bytes, 12);
    const std::uint32_t target_count = ReadU32(bytes, 16);
    if (paired_count_word == 0 || paired_count_word % 2U == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM metadata paired-record count is outside the observed odd family", 12));
    const std::uint64_t pair_count = (paired_count_word - 1U) / 2U;
    std::uint64_t metadata_record_count = 0;
    if (!Add(pair_count * 2U, target_count, metadata_record_count))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "VUM metadata record count overflows", 12));
    std::uint64_t metadata_bytes = 0;
    std::uint64_t metadata_end_u64 = 0;
    if (!Multiply(metadata_record_count, kMetadataRecordBytes, metadata_bytes) ||
        !Add(materials_end, metadata_bytes, metadata_end_u64) ||
        metadata_end_u64 > payload_a)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM metadata count contradicts its bounded record region", 12));
    const std::uint32_t metadata_end = static_cast<std::uint32_t>(metadata_end_u64);
    const std::uint32_t alignment_bytes = payload_a - metadata_end;
    if (alignment_bytes > 12U ||
        !std::ranges::all_of(bytes.subspan(metadata_end, alignment_bytes),
            [](const std::byte value) { return value == std::byte{0}; }))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM metadata alignment suffix is outside the observed zero layout",
            metadata_end));

    std::uint64_t p_count = 0;
    std::uint64_t q_count = 0;
    std::uint64_t t_count = 0;
    std::uint64_t non_t_ordinal = 0;
    bool inside_t_block = false;
    bool after_t_block = false;
    std::optional<std::uint32_t> previous_q_payload;
    for (std::uint64_t index = 0; index < metadata_record_count; ++index)
    {
        const std::uint32_t record_offset = static_cast<std::uint32_t>(
            materials_end + index * kMetadataRecordBytes);
        const auto record = bytes.subspan(record_offset, kMetadataRecordBytes);
        const MetadataRecordKind kind = ClassifyMetadataRecord(
            record, materials_end, metadata_end, payload_a, payload_b, primary_end);
        if (kind == MetadataRecordKind::Unknown)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM metadata record is outside the observed P/Q/T families", record_offset));

        if (kind == MetadataRecordKind::T)
        {
            if (after_t_block)
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "VUM metadata T records are not contiguous", record_offset));
            inside_t_block = true;
            ++t_count;
            const std::uint32_t target = ReadU32(record, 8);
            if (target <= record_offset || target % 16U != 0 ||
                (target - materials_end) % kMetadataRecordBytes != 0)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM metadata T record does not target a forward record", record_offset + 8U));
            const auto target_record = bytes.subspan(target, kMetadataRecordBytes);
            if (ClassifyMetadataRecord(target_record, materials_end, metadata_end,
                    payload_a, payload_b, primary_end) != MetadataRecordKind::Q)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM metadata T record does not target a Q record", record_offset + 8U));
            continue;
        }

        if (inside_t_block)
            after_t_block = true;
        const MetadataRecordKind expected = non_t_ordinal % 2U == 0
            ? MetadataRecordKind::Q
            : MetadataRecordKind::P;
        if (kind != expected)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "VUM metadata records do not preserve Q/P alternation", record_offset));
        ++non_t_ordinal;

        if (kind == MetadataRecordKind::P)
        {
            ++p_count;
            continue;
        }
        ++q_count;
        const std::uint32_t q_payload = ReadU32(record, 4);
        if (!previous_q_payload)
        {
            if (q_payload != payload_a)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM first Q record does not start the middle payload partition",
                    record_offset + 4U));
        }
        else if (q_payload <= *previous_q_payload ||
                 !IsObservedQSpan(q_payload - *previous_q_payload))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM Q record uses an unsupported middle-payload span", record_offset + 4U));
        previous_q_payload = q_payload;
    }

    if (p_count != pair_count || q_count != pair_count || t_count != target_count)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM metadata P/Q/T counts contradict their header counts", 12));
    if (previous_q_payload)
    {
        if (payload_b <= *previous_q_payload ||
            !IsObservedQSpan(payload_b - *previous_q_payload))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM final Q record uses an unsupported middle-payload span", payload_b));
    }
    else if (payload_a != payload_b)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM empty Q family does not have an empty middle payload", payload_a));
    return metadata_record_count;
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
    if (bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "VUM catalog input exceeds decoder byte limit"));
    if (bytes.size() < kNameRegionOffset)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "VUM catalog input is shorter than its observed preamble", bytes.size()));
    if (bytes.size() % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog container span is not 16-byte aligned"));
    if (bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'U'} ||
        bytes[2] != std::byte{'M'} || bytes[3] != std::byte{'S'})
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog input does not use the observed VUMS prefix", 0));

    const std::uint32_t names_end = ReadU32(bytes, 80);
    const std::uint32_t materials_end = ReadU32(bytes, 84);
    const std::uint32_t primary_end = ReadU32(bytes, 88);
    if (names_end < kNameRegionOffset || names_end % 4U != 0 ||
        materials_end % 4U != 0 || primary_end % 16U != 0 ||
        names_end > materials_end || materials_end > primary_end)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog boundaries are out of order or misaligned", 80));
    if (primary_end > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "VUM catalog primary boundary extends past the input", bytes.size()));

    const std::uint32_t payload_a = ReadU32(bytes, 92);
    const std::uint32_t payload_b = ReadU32(bytes, 96);
    if (payload_a % 16U != 0 || payload_b % 16U != 0 ||
        payload_a < materials_end || payload_a > payload_b || payload_b > primary_end)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog payload boundaries are out of order or misaligned", 92));
    if (ReadU32(bytes, 100) != 0 || ReadU32(bytes, 104) != 0 ||
        ReadU32(bytes, 108) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM catalog preamble uses an unsupported reserved-word layout", 100));

    const std::uint32_t name_count = ReadU32(bytes, 20);
    const std::uint32_t material_count = ReadU32(bytes, 24);
    if (name_count == 0 || material_count == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM catalog is outside the observed nonempty name/material family", 20));
    std::uint64_t material_bytes = 0;
    if (!Multiply(material_count, kMaterialRecordBytes, material_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "VUM catalog material table size overflows", 24));
    if (material_bytes != static_cast<std::uint64_t>(materials_end) - names_end)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM catalog material count contradicts its fixed table extent", 24));

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

    auto metadata_record_count = ValidatePayloadMetadata(
        bytes, materials_end, payload_a, payload_b, primary_end);
    if (!metadata_record_count)
        return std::unexpected(metadata_record_count.error());

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
    item_result = Accumulate(items, *metadata_record_count, limits.maximum_items,
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
