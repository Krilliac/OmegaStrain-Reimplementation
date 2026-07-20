#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/skas_text_envelope_ir.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail
{
inline constexpr std::uint64_t kSkasMinimumPhysicalBytes = 5132;
inline constexpr std::uint64_t kSkasMaximumPhysicalBytes = 5156;
inline constexpr std::uint64_t kSkasMinimumLogicalTextBytes = 5129;
inline constexpr std::uint64_t kSkasMaximumLogicalTextBytes = 5155;
inline constexpr std::uint64_t kSkasMinimumZeroPaddingBytes = 1;
inline constexpr std::uint64_t kSkasMaximumZeroPaddingBytes = 3;
inline constexpr std::uint64_t kSkasLineCount = 72;
inline constexpr std::uint64_t kSkasBlankLineCount = 5;
inline constexpr std::uint64_t kSkasSingleColonLineCount = 67;
inline constexpr std::uint64_t kSkasMaximumDecodedItems = 1 + kSkasLineCount;
inline constexpr std::uint64_t kSkasMaximumLogicalOutputBytes = sizeof(asset::SkasTextEnvelopeIR) +
                                                                kSkasMaximumLogicalTextBytes +
                                                                kSkasLineCount * sizeof(asset::SkasOpaqueTextLineIR);

// The aggregate-proven logical text is larger than DecodeLimits' general
// per-field string default. Keep every other shared default and widen only this
// decoder's default string budget to its fixed hard ceiling.
[[nodiscard]] constexpr asset::DecodeLimits DefaultSkasDecodeLimits() noexcept
{
    auto limits = asset::DecodeLimits{};
    limits.maximum_string_bytes = static_cast<std::uint32_t>(kSkasMaximumLogicalTextBytes);
    return limits;
}

// [any worker thread; stateless/reentrant] Decodes only the fixed
// aggregate-proven SKAS text envelope into exact owned logical text and
// source-order opaque line ranges. Caller limits may tighten, but never widen,
// fixed input, item, and output ceilings. The flat decoder uses no dynamic
// scratch or nesting edges.
[[nodiscard]] asset::DecodeResult<asset::SkasTextEnvelopeIR> DecodeSkasTextEnvelope(
    std::span<const std::byte> bytes, asset::DecodeLimits limits = DefaultSkasDecodeLimits());
} // namespace omega::retail
