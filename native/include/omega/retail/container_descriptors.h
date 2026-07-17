#pragma once

#include "omega/asset/decode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace omega::retail
{
struct ObservedByteRange
{
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
};

enum class ObservedExtentRelation
{
    Exact,
    ZeroPaddedTail,
    NonzeroTail,
    ExceedsInput,
};

struct ObservedExtent
{
    std::uint64_t observed_bytes = 0;
    std::uint64_t input_bytes = 0;
    ObservedExtentRelation relation = ObservedExtentRelation::Exact;
};

struct ColContainerDescriptor
{
    std::uint8_t format_version = 0;
    std::uint32_t header_bytes = 0;
    std::array<std::uint32_t, 4> observed_record_counts{};
    std::array<ObservedByteRange, 4> counted_tables{};
    std::uint32_t observed_word_0x28 = 0;
    // The fifth endpoint closes the header-described table region, not the container. Every
    // corpus sample has additional nonzero payload after it.
    ObservedByteRange uncounted_table_region;
    ObservedExtent described_tables_extent;
};

struct VumContainerDescriptor
{
    std::uint32_t observed_variant = 0;
    // Correlates with the optional trailing region in this corpus but has no assigned semantics.
    std::uint32_t observed_word_0x1c = 0;
    std::array<std::uint32_t, 3> observed_boundaries{};
    ObservedExtent primary_extent;
};

struct TdxContainerDescriptor
{
    std::uint16_t format_version = 0;
    std::uint16_t observed_flags = 0;
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint16_t bits_per_pixel = 0;
    std::uint16_t observed_storage_format_code = 0;
    std::uint16_t observed_width_unit_word = 0;
    std::uint16_t observed_storage_unit_word = 0;
    std::uint32_t observed_primary_size_word = 0;
    bool storage_word_matches_area_bit_formula = false;
    ObservedExtent primary_extent;
    // Present only when the size field reaches the input end or an all-zero tail. Pixel, palette,
    // swizzle, mip, animation, and alpha semantics remain deliberately uninterpreted.
    std::optional<ObservedByteRange> bounded_primary_region;
};

// [any worker thread; reentrant] Passive, allocation-free structural inspectors. They retain no
// input span and publish no executable or console-specific instruction representation.
[[nodiscard]] asset::DecodeResult<ColContainerDescriptor> InspectColContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
[[nodiscard]] asset::DecodeResult<VumContainerDescriptor> InspectVumContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
[[nodiscard]] asset::DecodeResult<TdxContainerDescriptor> InspectTdxContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
