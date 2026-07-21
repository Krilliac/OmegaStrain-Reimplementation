#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail {
inline constexpr std::uint64_t kIeOpaquePrefixOffset = 0;
inline constexpr std::uint64_t kIeOpaquePrefixBytes = 2;
inline constexpr std::uint64_t kIeObservedWordOffset = 2;
inline constexpr std::uint64_t kIeObservedWordBytes = 2;
inline constexpr std::uint64_t kIeRootBoundaryOffset = 4;

// This is a project safety ceiling, not an observed retail size maximum.
inline constexpr std::uint64_t kIeMaximumInputBytes = 1ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kIeMaximumDecodedItems = 1;

// Passive description of the bounded IE prefix consumed before the opaque
// root bytes. The leading two bytes are skipped by the observed consumer and
// remain opaque. The word at 0x02 remains observational: no tag, version,
// count, flag, string, node, or recursive-layout semantics are assigned. No
// source bytes are retained.
struct IeEnvelopeDescriptor {
  std::uint16_t observed_word_0x02 = 0;
  ObservedByteRange opaque_prefix_region;
  ObservedByteRange observed_word_region;
  ObservedByteRange opaque_root_region;

  bool operator==(const IeEnvelopeDescriptor &) const = default;
};

inline constexpr std::uint64_t kIeMaximumLogicalOutputBytes =
    sizeof(IeEnvelopeDescriptor);

// [any worker thread; stateless/reentrant] Skips the opaque two-byte prefix,
// reads the arbitrary observational little-endian word at 0x02, and reports
// the fixed root boundary at 0x04. It deliberately does not traverse or
// validate the root payload. Caller limits may tighten but cannot raise fixed
// ceilings. The flat inspection uses zero dynamic scratch and nesting depth
// zero.
[[nodiscard]] asset::DecodeResult<IeEnvelopeDescriptor>
InspectIeEnvelope(std::span<const std::byte> bytes,
                  asset::DecodeLimits limits = {});
} // namespace omega::retail
