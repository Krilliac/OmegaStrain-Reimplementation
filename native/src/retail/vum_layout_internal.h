#pragma once

#include "omega/asset/decode.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail::detail
{
constexpr std::uint64_t kVumHeaderBytes = 92;
constexpr std::uint64_t kVumPreambleFixedBytes = 20;
constexpr std::uint64_t kVumNameRegionOffset = kVumHeaderBytes + kVumPreambleFixedBytes;
constexpr std::uint64_t kVumMaterialRecordBytes = 92;
constexpr std::uint64_t kVumMetadataRecordBytes = 16;

struct VumPayloadLayout
{
    std::uint32_t name_count = 0;
    std::uint32_t material_count = 0;
    std::uint32_t names_end = 0;
    std::uint32_t materials_end = 0;
    std::uint32_t metadata_end = 0;
    std::uint32_t middle_payload_begin = 0;
    std::uint32_t final_payload_begin = 0;
    std::uint32_t primary_end = 0;
    std::uint32_t pair_count = 0;
    std::uint32_t grouped_pair_count = 0;
    std::uint32_t target_count = 0;
    std::uint32_t target_block_start_index = 0;
    std::uint64_t metadata_record_count = 0;
};

[[nodiscard]] std::uint32_t ReadVumU32(
    std::span<const std::byte> bytes, std::size_t offset) noexcept;

// Validates only the shared VUM prefix, counted material extent, and passive P/Q/T plus payload
// boundary grammar. Catalog contents and decoded-output budgets remain the caller's concern.
[[nodiscard]] asset::DecodeResult<VumPayloadLayout> ValidateVumPayloadLayout(
    std::span<const std::byte> bytes, asset::DecodeLimits limits);
} // namespace omega::retail::detail
