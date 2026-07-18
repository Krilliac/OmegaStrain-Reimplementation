#include "omega/retail/vum_render_payload_descriptor.h"

#include "vum_layout_internal.h"

#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::retail
{
namespace
{
[[nodiscard]] asset::DecodeError Error(const asset::DecodeErrorCode code,
    std::string message, const std::optional<std::uint64_t> byte_offset = std::nullopt)
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
} // namespace

asset::DecodeResult<VumRenderPayloadDescriptor> InspectVumRenderPayload(
    const std::span<const std::byte> bytes, const asset::DecodeLimits limits)
{
    auto layout = detail::ValidateVumPayloadLayout(bytes, limits);
    if (!layout)
        return std::unexpected(layout.error());

    std::uint64_t final_reference_count = 0;
    if (!Multiply(layout->pair_count, 4U, final_reference_count))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "VUM render-payload reference count overflows"));
    std::uint64_t middle_reference_count = 0;
    if (!Add(layout->pair_count, layout->grouped_pair_count, middle_reference_count))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "VUM middle-payload reference count overflows"));
    std::uint64_t items = 1;
    for (const std::uint64_t amount : {layout->metadata_record_count,
             static_cast<std::uint64_t>(layout->pair_count),
             static_cast<std::uint64_t>(layout->target_count), middle_reference_count,
             final_reference_count})
    {
        auto result = Accumulate(items, amount, limits.maximum_items,
            "VUM render-payload item count overflows",
            "VUM render-payload items exceed decoder limit");
        if (!result)
            return std::unexpected(result.error());
    }

    std::uint64_t pair_bytes = 0;
    std::uint64_t target_bytes = 0;
    if (!Multiply(layout->pair_count, sizeof(VumRenderPayloadPairDescriptor), pair_bytes) ||
        !Multiply(layout->target_count, sizeof(std::uint32_t), target_bytes))
        return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
            "VUM render-payload decoded output size overflows"));
    std::uint64_t output_bytes = sizeof(VumRenderPayloadDescriptor);
    for (const std::uint64_t amount : {pair_bytes, target_bytes})
    {
        auto result = Accumulate(output_bytes, amount, limits.maximum_output_bytes,
            "VUM render-payload decoded output size overflows",
            "VUM render-payload exceeds decoder output limit");
        if (!result)
            return std::unexpected(result.error());
    }

    VumRenderPayloadDescriptor result{
        .metadata_records_region = ObservedByteRange{
            .offset = layout->materials_end,
            .size = static_cast<std::uint64_t>(layout->metadata_end) - layout->materials_end,
        },
        .middle_payload_region = ObservedByteRange{
            .offset = layout->middle_payload_begin,
            .size = static_cast<std::uint64_t>(layout->final_payload_begin) -
                    layout->middle_payload_begin,
        },
        .final_payload_region = ObservedByteRange{
            .offset = layout->final_payload_begin,
            .size = static_cast<std::uint64_t>(layout->primary_end) -
                    layout->final_payload_begin,
        },
    };
    result.pairs.resize(layout->pair_count);
    result.targeted_pair_indices.reserve(layout->target_count);

    const std::uint64_t target_begin = layout->target_block_start_index;
    const std::uint64_t target_end = target_begin + layout->target_count;
    for (std::uint64_t source_index = 0;
         source_index < layout->metadata_record_count; ++source_index)
    {
        const std::uint32_t record_offset = static_cast<std::uint32_t>(
            layout->materials_end + source_index * detail::kVumMetadataRecordBytes);
        const auto record = bytes.subspan(record_offset, detail::kVumMetadataRecordBytes);
        if (source_index >= target_begin && source_index < target_end)
        {
            const std::uint32_t target = detail::ReadVumU32(record, 8);
            const std::uint64_t target_source_index =
                (target - layout->materials_end) / detail::kVumMetadataRecordBytes;
            if (target_source_index < target_end)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM target does not follow its contiguous target block", record_offset + 8U));
            const std::uint64_t normalized_index =
                target_source_index - layout->target_count;
            if (normalized_index % 2U != 0 || normalized_index / 2U >= layout->pair_count)
                return std::unexpected(Error(asset::DecodeErrorCode::InvalidReference,
                    "VUM target cannot be normalized to a Q/P pair", record_offset + 8U));
            result.targeted_pair_indices.push_back(
                static_cast<std::uint32_t>(normalized_index / 2U));
            continue;
        }

        const std::uint64_t normalized_index = source_index < target_begin
            ? source_index
            : source_index - layout->target_count;
        const std::size_t pair_index = static_cast<std::size_t>(normalized_index / 2U);
        auto& pair = result.pairs[pair_index];
        if (normalized_index % 2U == 0)
        {
            // Temporarily retain the absolute Q start; the source-order pass below turns it into
            // the bounded span size without allocating scratch storage.
            pair.middle_payload_bytes = detail::ReadVumU32(record, 4);
            pair.final_payload_reference_offsets[0] =
                detail::ReadVumU32(record, 12) - layout->final_payload_begin;
            continue;
        }
        pair.final_payload_reference_offsets[1] =
            detail::ReadVumU32(record, 0) - layout->final_payload_begin;
        pair.final_payload_reference_offsets[2] =
            detail::ReadVumU32(record, 8) - layout->final_payload_begin;
        pair.final_payload_reference_offsets[3] =
            detail::ReadVumU32(record, 12) - layout->final_payload_begin;
    }

    for (std::size_t index = 0; index < result.pairs.size(); ++index)
    {
        const std::uint32_t begin = result.pairs[index].middle_payload_bytes;
        const std::uint32_t end = index + 1U < result.pairs.size()
            ? result.pairs[index + 1U].middle_payload_bytes
            : layout->final_payload_begin;
        const std::uint32_t span_bytes = end - begin;
        auto& pair = result.pairs[index];
        pair.middle_payload_bytes = span_bytes;
        if (span_bytes == 16U)
        {
            pair.middle_payload_final_reference_offsets[0] =
                detail::ReadVumU32(bytes, begin + 4U) - layout->final_payload_begin;
            continue;
        }
        pair.middle_payload_structural_group_count =
            static_cast<std::uint8_t>((span_bytes - 32U) / 224U);
        pair.middle_payload_final_reference_offsets[0] =
            detail::ReadVumU32(bytes, begin + 0x74U) - layout->final_payload_begin;
        pair.middle_payload_final_reference_offsets[1] =
            detail::ReadVumU32(bytes, begin + 0xF4U) - layout->final_payload_begin;
    }
    return result;
}
} // namespace omega::retail
