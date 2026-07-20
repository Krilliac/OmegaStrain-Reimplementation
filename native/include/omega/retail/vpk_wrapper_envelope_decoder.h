#pragma once

#include "omega/asset/decode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
inline constexpr std::uint32_t kVpkObservedWord0x08 = 2048;
inline constexpr std::uint64_t kVpkPhysicalAlignmentBytes = 2048;
inline constexpr std::uint64_t kVpkWrapperMinimumInputBytes = 1'320'960;
inline constexpr std::uint64_t kVpkWrapperMaximumInputBytes = 9'005'056;
inline constexpr std::uint64_t kVpkWrapperMaximumDecodedItems = 1;

// Retail-only passive structure. The opaque prefix bytes are retained exactly in source order.
// Physical size and aligned-block count are structural measurements, not storage, audio, or
// streaming units. The borrowed input and its opaque payload are never retained.
struct VpkWrapperEnvelopeDescriptor
{
    std::array<std::byte, 4> opaque_prefix_bytes_0x04{};
    std::array<std::byte, 4> opaque_prefix_bytes_0x0c{};
    std::uint64_t physical_byte_count = 0;
    std::uint64_t aligned_block_count = 0;

    bool operator==(const VpkWrapperEnvelopeDescriptor &) const = default;
};

inline constexpr std::uint64_t kVpkWrapperMaximumLogicalOutputBytes = sizeof(VpkWrapperEnvelopeDescriptor);

// [any worker thread; stateless/reentrant] Strictly validates only the aggregate-proven VPK
// wrapper envelope. Caller limits may tighten but cannot raise the fixed input, item, or output
// ceilings. The flat inspection uses zero dynamic scratch and nesting depth zero.
[[nodiscard]] asset::DecodeResult<VpkWrapperEnvelopeDescriptor> DecodeVpkWrapperEnvelope(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
