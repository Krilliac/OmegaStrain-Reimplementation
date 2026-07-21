#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
struct SkaContainerDescriptor
{
    std::uint32_t format_version = 0;
    std::uint32_t observed_word_0x04 = 0;
    std::uint32_t observed_word_0x08 = 0;
    std::uint32_t observed_word_0x10 = 0;
    ObservedExtent logical_extent;

    [[nodiscard]] bool operator==(const SkaContainerDescriptor& other) const
    {
        return format_version == other.format_version &&
               observed_word_0x04 == other.observed_word_0x04 &&
               observed_word_0x08 == other.observed_word_0x08 &&
               observed_word_0x10 == other.observed_word_0x10 &&
               logical_extent.observed_bytes == other.logical_extent.observed_bytes &&
               logical_extent.input_bytes == other.logical_extent.input_bytes &&
               logical_extent.relation == other.logical_extent.relation;
    }
};

// [any worker thread; reentrant] Retail-only passive observed-family inspection. The fixed-size
// descriptor retains no input bytes and classifies the correlated counted-word extent without
// treating a nonzero tail or an extent beyond the input as malformed structure. It assigns no
// animation, timing, channel, transform, bone, compression, or payload semantics to the observed
// words or counted region. SKAS is a separate text family and is intentionally outside this API.
[[nodiscard]] asset::DecodeResult<SkaContainerDescriptor> InspectSkaContainer(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
