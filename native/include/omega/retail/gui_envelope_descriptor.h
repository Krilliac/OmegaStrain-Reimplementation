#pragma once

#include "omega/asset/decode.h"
#include "omega/retail/container_descriptors.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace omega::retail {
inline constexpr std::array<std::uint8_t, 3> kGuiObservedTag{
    static_cast<std::uint8_t>('G'), static_cast<std::uint8_t>('U'),
    static_cast<std::uint8_t>('I')};
inline constexpr std::uint64_t kGuiTagOffset = 0;
inline constexpr std::uint64_t kGuiTagBytes = kGuiObservedTag.size();
inline constexpr std::uint64_t kGuiOpaqueGapOffset = 3;
inline constexpr std::uint64_t kGuiOpaqueGapBytes = 1;
inline constexpr std::uint64_t kGuiObservedWordOffset = 4;
inline constexpr std::uint64_t kGuiObservedWordBytes = 2;
inline constexpr std::uint64_t kGuiRootBoundaryOffset = 6;

// This is a project safety ceiling, not an observed retail size maximum.
inline constexpr std::uint64_t kGuiMaximumInputBytes = 1ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kGuiMaximumDecodedItems = 1;

// Passive description of the bounded GUI prefix consumed before the opaque
// root bytes. The word at 0x04 remains observational: no version, count, flag,
// layout, widget, or recursive-node semantics are assigned. The byte at 0x03
// is skipped by the observed consumer and remains opaque. No source bytes are
// retained.
struct GuiEnvelopeDescriptor {
  std::uint16_t observed_word_0x04 = 0;
  ObservedByteRange tag_region;
  ObservedByteRange opaque_gap_region;
  ObservedByteRange observed_word_region;
  ObservedByteRange opaque_root_region;

  bool operator==(const GuiEnvelopeDescriptor &) const = default;
};

inline constexpr std::uint64_t kGuiMaximumLogicalOutputBytes =
    sizeof(GuiEnvelopeDescriptor);

// [any worker thread; stateless/reentrant] Validates only the exact three-byte
// GUI tag, reads the observational little-endian word at 0x04, and reports the
// fixed root boundary at 0x06. It deliberately does not traverse or validate
// the root payload. Caller limits may tighten but cannot raise fixed ceilings.
// The flat inspection uses zero dynamic scratch and nesting depth zero.
[[nodiscard]] asset::DecodeResult<GuiEnvelopeDescriptor>
InspectGuiEnvelope(std::span<const std::byte> bytes,
                   asset::DecodeLimits limits = {});
} // namespace omega::retail
