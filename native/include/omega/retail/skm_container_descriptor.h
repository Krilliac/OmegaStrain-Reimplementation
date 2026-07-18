#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omega::retail
{
struct SkmChunkDescriptor
{
    std::uint8_t qword_count = 0;
    std::uint8_t observed_secondary_count = 0;
    ObservedByteRange payload_region;

    bool operator==(const SkmChunkDescriptor&) const = default;
};

struct SkmContainerDescriptor
{
    std::uint8_t format_version = 0;
    ObservedByteRange chunk_table_region;
    ObservedByteRange aligned_header_region;
    std::vector<SkmChunkDescriptor> chunks;
    ObservedExtent logical_extent;

    [[nodiscard]] bool operator==(const SkmContainerDescriptor& other) const
    {
        return format_version == other.format_version &&
               chunk_table_region == other.chunk_table_region &&
               aligned_header_region == other.aligned_header_region && chunks == other.chunks &&
               logical_extent.observed_bytes == other.logical_extent.observed_bytes &&
               logical_extent.input_bytes == other.logical_extent.input_bytes &&
               logical_extent.relation == other.logical_extent.relation;
    }
};

// [any worker thread; reentrant] Passive structural inspection only. The descriptor retains no
// input span and assigns no semantics to chunk payloads or the observed secondary count.
[[nodiscard]] asset::DecodeResult<SkmContainerDescriptor> InspectSkmContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
