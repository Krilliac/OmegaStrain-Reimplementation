#pragma once

#include "omega/retail/fnt_v3_decoder.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace omega::frontend {

inline constexpr std::uint64_t kMaximumTextLayoutCodepoints = 65'536U;
inline constexpr std::uint64_t kMaximumTextLayoutLines = 65'536U;
inline constexpr std::uint64_t kMaximumTextLayoutGlyphs = 65'536U;
inline constexpr std::uint64_t kMaximumTextLayoutPairAdjustments = 65'536U;
inline constexpr std::uint64_t kMaximumTextLayoutOutputBytes =
    32U * 1024U * 1024U;

struct GuiTextRectangle {
  float left;
  float top;
  float width;
  float height;

  bool operator==(const GuiTextRectangle &) const = default;
};

struct FontAtlasExtent {
  float width;
  float height;

  bool operator==(const FontAtlasExtent &) const = default;
};

enum class HorizontalTextAlignment : std::uint8_t {
  Left,
  Right,
  Center,
};

enum class VerticalTextAlignment : std::uint8_t {
  Top,
  Center,
  Bottom,
};

enum class TextWrapMode : std::uint8_t {
  SpacesAndExplicitNewlines,
  ExplicitNewlinesOnly,
};

enum class TextEllipsisMode : std::uint8_t {
  Disabled,
  LiteralThreeDots,
};

// Optional pair adjustments are supplied independently of FntV3IR because the
// complete retail pair-table serialization is not yet proven. The value is an
// advance delta in canonical GUI units and may be positive or negative.
struct SignedPairAdjustment {
  char32_t left;
  char32_t right;
  float advance_delta;

  bool operator==(const SignedPairAdjustment &) const = default;
};

struct TextLayoutLimits {
  std::uint64_t maximum_codepoints;
  std::uint64_t maximum_lines;
  std::uint64_t maximum_glyphs;
  std::uint64_t maximum_pair_adjustments;
  std::uint64_t maximum_output_bytes;

  bool operator==(const TextLayoutLimits &) const = default;
};

struct TextLayoutOptions {
  GuiTextRectangle rectangle;
  FontAtlasExtent atlas_extent;

  // Explicit source-origin step between lines. No typographic meaning is
  // assigned to either unnamed FNT header byte to synthesize this value.
  float line_origin_step;

  HorizontalTextAlignment horizontal_alignment;
  VerticalTextAlignment vertical_alignment;
  TextWrapMode wrap_mode;
  TextEllipsisMode ellipsis_mode;
  std::span<const SignedPairAdjustment> pair_adjustments;
  TextLayoutLimits limits;
};

struct TextGlyphQuad {
  char32_t codepoint = U'\0';
  std::optional<std::size_t> source_index;
  std::size_t line_index = 0;
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
  retail::FntV3GlyphIR uv;

  bool operator==(const TextGlyphQuad &) const = default;
};

struct TextLineLayout {
  std::size_t first_glyph = 0;
  std::size_t glyph_count = 0;
  float left = 0.0F;
  float source_y = 0.0F;
  float advance = 0.0F;
  bool ellipsized = false;

  bool operator==(const TextLineLayout &) const = default;
};

struct TextLayout {
  std::vector<TextGlyphQuad> glyphs;
  std::vector<TextLineLayout> lines;
  bool ellipsized = false;

  bool operator==(const TextLayout &) const = default;
};

enum class TextLayoutErrorCode : std::uint8_t {
  InvalidGeometry,
  InvalidOption,
  InvalidFont,
  UnsupportedCodepoint,
  InvalidPairAdjustment,
  DuplicatePairAdjustment,
  EllipsisUnavailable,
  LimitExceeded,
  Overflow,
  AllocationFailure,
};

struct TextLayoutError {
  TextLayoutErrorCode code;
  std::optional<std::size_t> input_index;

  bool operator==(const TextLayoutError &) const = default;
};

using TextLayoutResult = std::expected<TextLayout, TextLayoutError>;

// [any thread; stateless/reentrant] Produces owned canonical-GUI-space glyph
// quads and line records. `font` must already have been selected by the caller;
// this layer deliberately cannot guess an empty widget font name or consult a
// manager default. Input is explicit codepoints, so it also makes no host-
// encoding assumption about retail string bytes. The font, text, and optional
// pair span are borrowed only for this call; no view or pointer survives in the
// returned value.
[[nodiscard]] TextLayoutResult
LayoutRetailText(const retail::FntV3IR &font, std::u32string_view text,
                 const TextLayoutOptions &options);

} // namespace omega::frontend
