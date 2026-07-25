#pragma once

#include "omega/asset/decode.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace omega::retail {
inline constexpr std::uint16_t kFntV3Version = 3;
inline constexpr std::uint8_t kFntV3NameSpan = 13;
inline constexpr std::uint64_t kFntV3AtlasReferenceBytes = 12;
inline constexpr std::uint64_t kFntV3GlyphRecordBytes = 16;
inline constexpr std::uint64_t kFntV3MaximumGlyphCount = 0xB4;
inline constexpr std::uint64_t kFntV3MaximumInputBytes = 4096;
inline constexpr std::uint64_t kFntV3MaximumZeroTailBytes = 15;

struct FntV3GlyphIR {
  float u_left = 0.0F;
  float u_right = 0.0F;
  float v_top = 0.0F;
  float v_bottom = 0.0F;

  bool operator==(const FntV3GlyphIR &) const = default;
};

struct FntV3IR {
  std::string atlas_reference;
  // The typographic meanings of these source bytes are not proven. Byte 17's
  // only published semantic adapter is ApplyObservedByte17VerticalPlacement
  // below.
  std::uint8_t raw_byte_16 = 0;
  std::uint8_t raw_byte_17 = 0;
  std::vector<FntV3GlyphIR> glyphs;
  std::int8_t space_advance = 0;

  [[nodiscard]] const FntV3GlyphIR *
  GlyphForCodepoint(std::uint32_t codepoint) const noexcept;
  [[nodiscard]] std::optional<float>
  AdvanceForCodepoint(std::uint32_t codepoint,
                      float atlas_width) const noexcept;
  // Every glyph record of a v3 font stores the same vertical span: measured
  // over all 177 records of each shipped retail font, CALLOUT/DEFAULT/LARGEFNT
  // give exactly 17/19/27 texels of their own atlas with no outlier. This
  // returns that shared row height in atlas texels, or nullopt when the font
  // carries no glyph records or the records disagree.
  [[nodiscard]] std::optional<float>
  LineCellHeight(float atlas_height) const noexcept;
  [[nodiscard]] float
  ApplyObservedByte17VerticalPlacement(float source_y) const noexcept;

  bool operator==(const FntV3IR &) const = default;
};

inline constexpr std::uint64_t kFntV3MaximumDecodedItems =
    1U + kFntV3MaximumGlyphCount;
inline constexpr std::uint64_t kFntV3MaximumLogicalOutputBytes =
    sizeof(FntV3IR) + kFntV3AtlasReferenceBytes +
    kFntV3MaximumGlyphCount * sizeof(FntV3GlyphIR);

// [any worker thread; stateless/reentrant] Decodes only the independently
// documented version-3 FNT family into owned atlas binding and UV records.
// Optional pair-adjustment tables are not yet structurally proven: a nonzero
// presence byte fails closed as an unsupported variant.
[[nodiscard]] asset::DecodeResult<FntV3IR>
DecodeFntV3(std::span<const std::byte> bytes, asset::DecodeLimits limits = {});
} // namespace omega::retail
