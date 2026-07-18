#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omega::retail
{
enum class SklLineEnding : std::uint8_t
{
    CarriageReturn,
    CarriageReturnLineFeed,
};

struct SklRecordDescriptor
{
    ObservedByteRange line_region;
    ObservedByteRange token_region;
    ObservedByteRange terminator_region;

    bool operator==(const SklRecordDescriptor&) const = default;
};

struct SklContainerDescriptor
{
    SklLineEnding line_ending = SklLineEnding::CarriageReturn;
    std::vector<SklRecordDescriptor> records;
    ObservedExtent logical_extent;

    [[nodiscard]] bool operator==(const SklContainerDescriptor& other) const
    {
        return line_ending == other.line_ending && records == other.records &&
               logical_extent.observed_bytes == other.logical_extent.observed_bytes &&
               logical_extent.input_bytes == other.logical_extent.input_bytes &&
               logical_extent.relation == other.logical_extent.relation;
    }
};

// [any worker thread; reentrant] Passive structural inspection only. The descriptor retains no
// input or token bytes and assigns no meaning to records from the observed marker family.
[[nodiscard]] asset::DecodeResult<SklContainerDescriptor> InspectSklContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
