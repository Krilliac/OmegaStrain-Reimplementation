#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/lpd_envelope_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
inline constexpr std::uint32_t kLpdHeaderWordCount = 22;
inline constexpr std::uint64_t kLpdHeaderBytes =
    static_cast<std::uint64_t>(kLpdHeaderWordCount) * 4U;
inline constexpr std::uint64_t kLpdMaximumInputBytes = 4096;
inline constexpr std::uint64_t kLpdMaximumZeroTailBytes = 1932;
inline constexpr std::uint64_t kLpdMaximumEntryCount =
    (kLpdMaximumInputBytes - kLpdHeaderBytes) / 4U;
inline constexpr std::uint64_t kLpdMaximumDecodedItems =
    1U + asset::kLpdSourceTrackCount + kLpdMaximumEntryCount;
inline constexpr std::uint64_t kLpdMaximumLogicalOutputBytes =
    sizeof(asset::LpdEnvelopeIR) + 4U * kLpdMaximumEntryCount;

// [any worker thread; stateless/reentrant] Decodes the bounded, counted LPD envelope into owned
// source-order tracks. Caller limits may only tighten the fixed input, item, output, and tail
// ceilings. No meaning is assigned to tracks or four-byte entries.
[[nodiscard]] asset::DecodeResult<asset::LpdEnvelopeIR> DecodeLpdEnvelope(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
