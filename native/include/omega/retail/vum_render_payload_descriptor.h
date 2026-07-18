#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace omega::retail
{
struct VumRenderPayloadPairDescriptor
{
    std::uint32_t middle_payload_bytes = 0;
    // Zero denotes the compact 16-byte family. Values one through three denote the observed
    // 32 + 224 * group-count family; this is deliberately not named as a material layer count.
    std::uint8_t middle_payload_structural_group_count = 0;
    // Opaque references from the middle payload into the final payload, normalized relative to
    // final_payload_region. Compact spans use element zero; grouped spans use both elements.
    std::array<std::uint32_t, 2> middle_payload_final_reference_offsets{};
    // Opaque, strictly ordered references relative to final_payload_region. Their target extents,
    // rendering, topology, and instruction meanings remain deliberately unassigned.
    std::array<std::uint32_t, 4> final_payload_reference_offsets{};

    bool operator==(const VumRenderPayloadPairDescriptor&) const = default;
};

// Retail-only passive structure. This is not canonical asset IR and must not cross into renderer
// or simulation targets. It owns no payload bytes and exposes no console instruction model.
struct VumRenderPayloadDescriptor
{
    ObservedByteRange metadata_records_region;
    ObservedByteRange middle_payload_region;
    ObservedByteRange final_payload_region;
    std::vector<VumRenderPayloadPairDescriptor> pairs;
    // Source-order metadata targets normalized to pair ordinals.
    std::vector<std::uint32_t> targeted_pair_indices;

    bool operator==(const VumRenderPayloadDescriptor&) const = default;
};

// [any worker thread; reentrant] Validates the bounded render-payload relationship grammar and
// returns only owned numeric structure. Payload data, packet words, and executable semantics are
// never retained or interpreted.
[[nodiscard]] asset::DecodeResult<VumRenderPayloadDescriptor> InspectVumRenderPayload(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
