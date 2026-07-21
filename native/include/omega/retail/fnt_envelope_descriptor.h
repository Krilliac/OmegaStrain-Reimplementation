#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail {
inline constexpr std::uint16_t kFntObservedWord0x00 = 3;
inline constexpr std::uint8_t kFntObservedByte0x02 = 13;
inline constexpr std::uint64_t kFntObservedAsciiOffset = 3;
inline constexpr std::uint64_t kFntObservedAsciiBytes = 12;
inline constexpr std::uint64_t kFntObservedTerminatorOffset = 15;
inline constexpr std::uint64_t kFntObservedPayloadOffset = 16;

// This is a project safety ceiling, not an observed retail size maximum.
inline constexpr std::uint64_t kFntMaximumInputBytes = 1ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kFntMaximumDecodedItems = 1;

// Passive description of the small, project-defined FNT wrapper-prefix
// hypothesis. The exact prefix constants are a falsifiable project boundary; no
// tracked source records their retail provenance (see
// analysis/formats/FRONTEND-EVIDENCE-AUDIT.md, tier C6). The
// numeric prefix values and regions intentionally remain observational: no
// version, resource-binding, glyph, metric, or texture semantics are assigned.
// The borrowed source and its bytes are never retained.
struct FntEnvelopeDescriptor {
  std::uint16_t observed_word_0x00 = 0;
  std::uint8_t observed_byte_0x02 = 0;
  ObservedByteRange ascii_region;
  ObservedByteRange terminator_region;
  ObservedByteRange opaque_payload_region;

  bool operator==(const FntEnvelopeDescriptor &) const = default;
};

inline constexpr std::uint64_t kFntMaximumLogicalOutputBytes =
    sizeof(FntEnvelopeDescriptor);

// [any worker thread; stateless/reentrant] Validates only the fixed,
// project-defined prefix hypothesis and reports byte ranges without
// retaining their contents. Caller limits may tighten but cannot raise the
// fixed input, item, or output ceilings. The flat inspection uses zero dynamic
// scratch and nesting depth zero.
[[nodiscard]] asset::DecodeResult<FntEnvelopeDescriptor>
InspectFntEnvelope(std::span<const std::byte> bytes,
                   asset::DecodeLimits limits = {});
} // namespace omega::retail
