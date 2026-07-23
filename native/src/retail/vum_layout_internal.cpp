#include "vum_layout_internal.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail::detail
{
namespace
{
enum class MetadataRecordKind
{
    P,
    Q,
    T,
    Unknown,
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

[[nodiscard]] bool IsRecordStart(const std::uint32_t value, const std::uint32_t begin,
    const std::uint32_t end, const std::uint32_t stride) noexcept
{
    return value >= begin && value < end && (value - begin) % stride == 0;
}

[[nodiscard]] MetadataRecordKind ClassifyMetadataRecord(
    const std::span<const std::byte> record, const std::uint32_t metadata_begin,
    const std::uint32_t metadata_end, const std::uint32_t middle_payload_begin,
    const std::uint32_t final_payload_begin, const std::uint32_t primary_end) noexcept
{
    const std::array<std::uint32_t, 4> words{
        ReadVumU32(record, 0), ReadVumU32(record, 4),
        ReadVumU32(record, 8), ReadVumU32(record, 12)};
    if (IsRecordStart(words[2], metadata_begin, metadata_end, 16))
        return MetadataRecordKind::T;
    if (InRangeAligned(words[1], middle_payload_begin, final_payload_begin, 16) &&
        StrictlyInsideAligned(words[3], final_payload_begin, primary_end, 4))
        return MetadataRecordKind::Q;
    if (StrictlyInsideAligned(words[0], final_payload_begin, primary_end, 4) &&
        StrictlyInsideAligned(words[2], final_payload_begin, primary_end, 4) &&
        StrictlyInsideAligned(words[3], final_payload_begin, primary_end, 4))
        return MetadataRecordKind::P;
    return MetadataRecordKind::Unknown;
}

[[nodiscard]] bool IsObservedMiddleSpan(const std::uint32_t bytes) noexcept
{
    return bytes == 16 || bytes == 256 || bytes == 480 || bytes == 704;
}

[[nodiscard]] bool IsObservedFinalTail(const std::uint32_t bytes) noexcept
{
    return bytes == 4 || bytes == 8 || bytes == 12 || bytes == 16;
}

[[nodiscard]] asset::DecodeResult<bool> ValidateMiddlePayloadReferences(
    const std::span<const std::byte> bytes, const std::uint32_t span_begin,
    const std::uint32_t span_bytes, const std::uint32_t final_payload_begin,
    const std::uint32_t primary_end, const std::array<std::uint32_t, 4>& pair_references,
    const std::optional<std::uint32_t> previous_combined_reference)
{
    const bool grouped = span_bytes != 16U;
    // IsObservedMiddleSpan's fixed {16,256,480,704} set keeps every read below in range today
    // (0xF4+4 <= 256, the smallest grouped span). This is a release-mode backstop, not the
    // primary gate, against that set ever being widened without revisiting the fixed offsets
    // read here.
    const std::uint32_t first_reference_offset = grouped ? 0x74U : 4U;
    if (first_reference_offset + 4U > span_bytes || (grouped && 0xF4U + 4U > span_bytes))
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM middle payload span cannot hold its combined-reference offsets", span_begin));
    }
    const std::uint32_t first_reference = ReadVumU32(bytes, span_begin + first_reference_offset);
    if (!StrictlyInsideAligned(first_reference, final_payload_begin, primary_end, 16))
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
            "VUM middle payload has an invalid final-payload reference", span_begin));
    if ((!previous_combined_reference && first_reference != final_payload_begin + 16U) ||
        (previous_combined_reference && first_reference <= *previous_combined_reference))
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
            "VUM combined payload references are not strictly increasing", span_begin));

    if (!grouped)
    {
        if (!(first_reference < pair_references[0] &&
                pair_references[0] < pair_references[1] &&
                pair_references[1] < pair_references[2] &&
                pair_references[2] < pair_references[3]))
            return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                "VUM compact combined-reference order is invalid", span_begin + 4U));
        return false;
    }

    const std::uint32_t second_reference = ReadVumU32(bytes, span_begin + 0xF4U);
    if (!StrictlyInsideAligned(second_reference, final_payload_begin, primary_end, 16) ||
        !(first_reference < pair_references[0] &&
            pair_references[0] < pair_references[1] &&
            pair_references[1] < second_reference &&
            second_reference < pair_references[2] &&
            pair_references[2] < pair_references[3]))
        return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
            "VUM grouped combined-reference order is invalid", span_begin + 0xF4U));
    return true;
}
} // namespace

std::uint32_t ReadVumU32(
    const std::span<const std::byte> bytes, const std::size_t offset) noexcept
{
    return std::to_integer<std::uint32_t>(bytes[offset]) |
           (std::to_integer<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (std::to_integer<std::uint32_t>(bytes[offset + 3]) << 24U);
}

asset::DecodeResult<VumPayloadLayout> ValidateVumPayloadLayout(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    if (bytes.size() > limits.maximum_input_bytes)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "VUM input exceeds decoder byte limit"));
    if (bytes.size() < kVumNameRegionOffset)
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "VUM input is shorter than its observed preamble", bytes.size()));
    if (bytes.size() % 16U != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM container span is not 16-byte aligned"));
    if (bytes[0] != std::byte{'V'} || bytes[1] != std::byte{'U'} ||
        bytes[2] != std::byte{'M'} || bytes[3] != std::byte{'S'})
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM input does not use the observed VUMS prefix", 0));

    const std::uint32_t name_count = ReadVumU32(bytes, 20);
    const std::uint32_t material_count = ReadVumU32(bytes, 24);
    const std::uint32_t names_end = ReadVumU32(bytes, 80);
    const std::uint32_t materials_end = ReadVumU32(bytes, 84);
    const std::uint32_t primary_end = ReadVumU32(bytes, 88);
    if (names_end < kVumNameRegionOffset || names_end % 4U != 0 ||
        materials_end % 4U != 0 || primary_end % 16U != 0 ||
        names_end > materials_end || materials_end > primary_end)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM boundaries are out of order or misaligned", 80));
    if (primary_end > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
            "VUM primary boundary extends past the input", bytes.size()));
    if (name_count == 0 || material_count == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM is outside the observed nonempty name/material family", 20));

    std::uint64_t material_bytes = 0;
    if (!Multiply(material_count, kVumMaterialRecordBytes, material_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "VUM material table size overflows", 24));
    if (material_bytes != static_cast<std::uint64_t>(materials_end) - names_end)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM material count contradicts its fixed table extent", 24));

    const std::uint32_t middle_payload_begin = ReadVumU32(bytes, 92);
    const std::uint32_t final_payload_begin = ReadVumU32(bytes, 96);
    if (middle_payload_begin % 16U != 0 || final_payload_begin % 16U != 0 ||
        middle_payload_begin < materials_end || middle_payload_begin > final_payload_begin ||
        final_payload_begin > primary_end)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM payload boundaries are out of order or misaligned", 92));
    if (ReadVumU32(bytes, 100) != 0 || ReadVumU32(bytes, 104) != 0 ||
        ReadVumU32(bytes, 108) != 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM preamble uses an unsupported reserved-word layout", 100));

    const std::uint32_t paired_count_word = ReadVumU32(bytes, 12);
    const std::uint32_t target_count = ReadVumU32(bytes, 16);
    if (paired_count_word == 0 || paired_count_word % 2U == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM metadata paired-record count is outside the observed odd family", 12));
    const std::uint32_t pair_count = (paired_count_word - 1U) / 2U;
    const std::uint64_t metadata_record_count =
        static_cast<std::uint64_t>(pair_count) * 2U + target_count;
    if (metadata_record_count > limits.maximum_items)
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "VUM metadata records exceed decoder item limit", 12));
    std::uint64_t metadata_bytes = 0;
    std::uint64_t metadata_end_u64 = 0;
    if (!Multiply(metadata_record_count, kVumMetadataRecordBytes, metadata_bytes) ||
        !Add(materials_end, metadata_bytes, metadata_end_u64) ||
        metadata_end_u64 > middle_payload_begin)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM metadata count contradicts its bounded record region", 12));
    const std::uint32_t metadata_end = static_cast<std::uint32_t>(metadata_end_u64);
    const std::uint32_t alignment_bytes = middle_payload_begin - metadata_end;
    if (alignment_bytes > 12U ||
        !std::ranges::all_of(bytes.subspan(metadata_end, alignment_bytes),
            [](const std::byte value) { return value == std::byte{0}; }))
        return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
            "VUM metadata alignment suffix is outside the observed zero layout", metadata_end));

    std::uint64_t p_count = 0;
    std::uint64_t q_count = 0;
    std::uint64_t t_count = 0;
    std::uint64_t non_t_ordinal = 0;
    bool inside_t_block = false;
    bool after_t_block = false;
    std::uint32_t target_block_start_index =
        static_cast<std::uint32_t>(metadata_record_count);
    std::optional<std::uint32_t> previous_t_target;
    std::optional<std::uint32_t> previous_q_payload;
    std::optional<std::uint32_t> previous_final_boundary;
    std::optional<std::uint32_t> previous_combined_reference;
    std::array<std::uint32_t, 4> current_pair_references{};
    std::uint32_t grouped_pair_count = 0;
    for (std::uint64_t index = 0; index < metadata_record_count; ++index)
    {
        const std::uint32_t record_offset = static_cast<std::uint32_t>(
            materials_end + index * kVumMetadataRecordBytes);
        const auto record = bytes.subspan(record_offset, kVumMetadataRecordBytes);
        const MetadataRecordKind kind = ClassifyMetadataRecord(record, materials_end,
            metadata_end, middle_payload_begin, final_payload_begin, primary_end);
        if (kind == MetadataRecordKind::Unknown)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM metadata record is outside the observed P/Q/T families", record_offset));

        if (kind == MetadataRecordKind::T)
        {
            if (after_t_block)
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "VUM metadata T records are not contiguous", record_offset));
            if (!inside_t_block)
                target_block_start_index = static_cast<std::uint32_t>(index);
            inside_t_block = true;
            ++t_count;
            const std::uint32_t target = ReadVumU32(record, 8);
            if (target <= record_offset ||
                !IsRecordStart(target, materials_end, metadata_end,
                    static_cast<std::uint32_t>(kVumMetadataRecordBytes)))
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM metadata T record does not target a forward metadata record",
                    record_offset + 8U));
            const auto target_record = bytes.subspan(target, kVumMetadataRecordBytes);
            if (ClassifyMetadataRecord(target_record, materials_end, metadata_end,
                    middle_payload_begin, final_payload_begin, primary_end) !=
                MetadataRecordKind::Q)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM metadata T record does not target a Q record", record_offset + 8U));
            if (previous_t_target && target <= *previous_t_target)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM metadata T targets are not strictly increasing", record_offset + 8U));
            previous_t_target = target;
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

        if (kind == MetadataRecordKind::Q)
        {
            ++q_count;
            const std::uint32_t q_payload = ReadVumU32(record, 4);
            if (!previous_q_payload)
            {
                if (q_payload != middle_payload_begin)
                    return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                        "VUM first Q record does not start the middle payload partition",
                        record_offset + 4U));
            }
            else
            {
                if (q_payload <= *previous_q_payload)
                    return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                        "VUM Q record uses an unsupported middle-payload span",
                        record_offset + 4U));
                const std::uint32_t span_bytes = q_payload - *previous_q_payload;
                if (!IsObservedMiddleSpan(span_bytes))
                    return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                        "VUM Q record uses an unsupported middle-payload span",
                        record_offset + 4U));
                auto references = ValidateMiddlePayloadReferences(bytes, *previous_q_payload,
                    span_bytes, final_payload_begin, primary_end, current_pair_references,
                    previous_combined_reference);
                if (!references)
                    return std::unexpected(references.error());
                grouped_pair_count += *references;
                previous_combined_reference = current_pair_references[3];
            }
            previous_q_payload = q_payload;

            const std::uint32_t reference = ReadVumU32(record, 12);
            if (previous_final_boundary && reference <= *previous_final_boundary)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM final-payload references are not strictly increasing",
                    record_offset + 12U));
            previous_final_boundary = reference;
            current_pair_references[0] = reference;
            continue;
        }

        ++p_count;
        constexpr std::array<std::size_t, 3> p_reference_offsets{0, 8, 12};
        for (std::size_t reference_index = 0;
             reference_index < p_reference_offsets.size(); ++reference_index)
        {
            const std::size_t reference_offset = p_reference_offsets[reference_index];
            const std::uint32_t reference = ReadVumU32(record, reference_offset);
            if (!previous_final_boundary || reference <= *previous_final_boundary)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM final-payload references are not strictly increasing",
                    record_offset + reference_offset));
            previous_final_boundary = reference;
            current_pair_references[reference_index + 1U] = reference;
        }
    }

    if (p_count != pair_count || q_count != pair_count || t_count != target_count)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "VUM metadata P/Q/T counts contradict their header counts", 12));
    if (previous_q_payload)
    {
        if (final_payload_begin <= *previous_q_payload)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM final Q record uses an unsupported middle-payload span",
                final_payload_begin));
        const std::uint32_t final_span_bytes = final_payload_begin - *previous_q_payload;
        if (!IsObservedMiddleSpan(final_span_bytes))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM final Q record uses an unsupported middle-payload span",
                final_payload_begin));
        auto references = ValidateMiddlePayloadReferences(bytes, *previous_q_payload,
            final_span_bytes, final_payload_begin, primary_end, current_pair_references,
            previous_combined_reference);
        if (!references)
            return std::unexpected(references.error());
        grouped_pair_count += *references;
        if (!previous_final_boundary ||
            !IsObservedFinalTail(primary_end - *previous_final_boundary))
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM final-payload reference uses an unsupported trailing word count",
                primary_end));
    }
    else
    {
        if (middle_payload_begin != final_payload_begin)
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "VUM empty Q family does not have an empty middle payload",
                middle_payload_begin));
        if (primary_end - final_payload_begin != 16U)
            return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                "VUM empty Q family does not use the observed final payload sentinel",
                final_payload_begin));
    }

    return VumPayloadLayout{
        .name_count = name_count,
        .material_count = material_count,
        .names_end = names_end,
        .materials_end = materials_end,
        .metadata_end = metadata_end,
        .middle_payload_begin = middle_payload_begin,
        .final_payload_begin = final_payload_begin,
        .primary_end = primary_end,
        .pair_count = pair_count,
        .grouped_pair_count = grouped_pair_count,
        .target_count = target_count,
        .target_block_start_index = target_block_start_index,
        .metadata_record_count = metadata_record_count,
    };
}
} // namespace omega::retail::detail
