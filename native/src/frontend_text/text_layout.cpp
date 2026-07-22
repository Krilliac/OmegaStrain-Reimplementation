#include "omega/frontend_text/text_layout.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace omega::frontend {
namespace {

struct TextItem {
  char32_t codepoint = U'\0';
  std::optional<std::size_t> source_index;
};

struct PairLookupEntry {
  std::uint64_t key = 0;
  float advance_delta = 0.0F;
  std::size_t source_index = 0;
};

struct WorkingLine {
  std::vector<TextItem> items;
  float advance = 0.0F;
  bool ellipsized = false;
};

[[nodiscard]] TextLayoutError
Error(const TextLayoutErrorCode code,
      const std::optional<std::size_t> input_index = std::nullopt) noexcept {
  return TextLayoutError{.code = code, .input_index = input_index};
}

[[nodiscard]] bool Add(const std::uint64_t left, const std::uint64_t right,
                       std::uint64_t &result) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    return false;
  result = left + right;
  return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t &result) noexcept {
  if (left != 0U && right > std::numeric_limits<std::uint64_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

[[nodiscard]] bool AddFinite(const float left, const float right,
                             float &result) noexcept {
  result = left + right;
  return std::isfinite(result);
}

[[nodiscard]] bool MultiplyFinite(const float left, const float right,
                                  float &result) noexcept {
  result = left * right;
  return std::isfinite(result);
}

[[nodiscard]] constexpr std::uint64_t PairKey(const char32_t left,
                                              const char32_t right) noexcept {
  return (static_cast<std::uint64_t>(left) << 32U) |
         static_cast<std::uint64_t>(right);
}

[[nodiscard]] bool IsSupportedCodepoint(const retail::FntV3IR &font,
                                        const char32_t codepoint) noexcept {
  return codepoint == U' ' || font.GlyphForCodepoint(codepoint) != nullptr;
}

[[nodiscard]] bool
ValidHorizontalAlignment(const HorizontalTextAlignment alignment) noexcept {
  switch (alignment) {
  case HorizontalTextAlignment::Left:
  case HorizontalTextAlignment::Right:
  case HorizontalTextAlignment::Center:
    return true;
  }
  return false;
}

[[nodiscard]] bool
ValidVerticalAlignment(const VerticalTextAlignment alignment) noexcept {
  switch (alignment) {
  case VerticalTextAlignment::Top:
  case VerticalTextAlignment::Center:
  case VerticalTextAlignment::Bottom:
    return true;
  }
  return false;
}

[[nodiscard]] bool ValidWrapMode(const TextWrapMode mode) noexcept {
  switch (mode) {
  case TextWrapMode::SpacesAndExplicitNewlines:
  case TextWrapMode::ExplicitNewlinesOnly:
    return true;
  }
  return false;
}

[[nodiscard]] bool ValidEllipsisMode(const TextEllipsisMode mode) noexcept {
  switch (mode) {
  case TextEllipsisMode::Disabled:
  case TextEllipsisMode::LiteralThreeDots:
    return true;
  }
  return false;
}

[[nodiscard]] std::expected<float, TextLayoutError>
AdvanceFor(const retail::FntV3IR &font, const char32_t codepoint,
           const float atlas_width,
           const std::optional<std::size_t> input_index) noexcept {
  const auto advance = font.AdvanceForCodepoint(
      static_cast<std::uint32_t>(codepoint), atlas_width);
  if (!advance)
    return std::unexpected(
        Error(TextLayoutErrorCode::UnsupportedCodepoint, input_index));
  if (!std::isfinite(*advance))
    return std::unexpected(Error(TextLayoutErrorCode::Overflow, input_index));
  return *advance;
}

[[nodiscard]] float
FindPairAdjustment(const std::vector<PairLookupEntry> &pairs,
                   const char32_t left, const char32_t right) noexcept {
  const std::uint64_t key = PairKey(left, right);
  const auto found = std::lower_bound(
      pairs.begin(), pairs.end(), key,
      [](const PairLookupEntry &entry, const std::uint64_t value) {
        return entry.key < value;
      });
  return found != pairs.end() && found->key == key ? found->advance_delta
                                                   : 0.0F;
}

[[nodiscard]] std::expected<float, TextLayoutError>
MeasureItems(const retail::FntV3IR &font, const std::vector<TextItem> &items,
             const float atlas_width,
             const std::vector<PairLookupEntry> &pairs) noexcept {
  float advance = 0.0F;
  for (std::size_t index = 0; index < items.size(); ++index) {
    if (index != 0U) {
      if (!AddFinite(advance,
                     FindPairAdjustment(pairs, items[index - 1U].codepoint,
                                        items[index].codepoint),
                     advance))
        return std::unexpected(
            Error(TextLayoutErrorCode::Overflow, items[index].source_index));
    }
    const auto item_advance = AdvanceFor(
        font, items[index].codepoint, atlas_width, items[index].source_index);
    if (!item_advance)
      return std::unexpected(item_advance.error());
    if (!AddFinite(advance, *item_advance, advance))
      return std::unexpected(
          Error(TextLayoutErrorCode::Overflow, items[index].source_index));
  }
  return advance;
}

[[nodiscard]] std::optional<std::size_t>
LastSpace(const std::vector<TextItem> &items) noexcept {
  for (std::size_t index = items.size(); index != 0U; --index) {
    if (items[index - 1U].codepoint == U' ')
      return index - 1U;
  }
  return std::nullopt;
}

[[nodiscard]] bool HasNonSpaceBefore(const std::vector<TextItem> &items,
                                     const std::size_t end) noexcept {
  return std::any_of(
      items.begin(), items.begin() + end,
      [](const TextItem &item) { return item.codepoint != U' '; });
}

void TrimTrailingSpaces(std::vector<TextItem> &items) {
  while (!items.empty() && items.back().codepoint == U' ')
    items.pop_back();
}

void TrimLeadingSpaces(std::vector<TextItem> &items) {
  const auto first_nonspace =
      std::find_if(items.begin(), items.end(),
                   [](const TextItem &item) { return item.codepoint != U' '; });
  items.erase(items.begin(), first_nonspace);
}

[[nodiscard]] std::expected<bool, TextLayoutError>
ApplyEllipsis(const retail::FntV3IR &font, std::vector<TextItem> &items,
              float &advance, const TextLayoutOptions &options,
              const std::vector<PairLookupEntry> &pairs) {
  if (advance <= options.rectangle.width ||
      options.ellipsis_mode == TextEllipsisMode::Disabled)
    return false;

  if (font.GlyphForCodepoint(U'.') == nullptr)
    return std::unexpected(Error(TextLayoutErrorCode::EllipsisUnavailable));

  const std::vector<TextItem> dots{
      TextItem{.codepoint = U'.', .source_index = std::nullopt},
      TextItem{.codepoint = U'.', .source_index = std::nullopt},
      TextItem{.codepoint = U'.', .source_index = std::nullopt},
  };
  const auto dots_advance =
      MeasureItems(font, dots, options.atlas_extent.width, pairs);
  if (!dots_advance)
    return std::unexpected(dots_advance.error());
  if (*dots_advance > options.rectangle.width)
    return std::unexpected(Error(TextLayoutErrorCode::EllipsisUnavailable));

  TrimTrailingSpaces(items);
  std::vector<float> prefix_advances(items.size() + 1U, 0.0F);
  for (std::size_t index = 0; index < items.size(); ++index) {
    float prefix = prefix_advances[index];
    if (index != 0U &&
        !AddFinite(prefix,
                   FindPairAdjustment(pairs, items[index - 1U].codepoint,
                                      items[index].codepoint),
                   prefix))
      return std::unexpected(
          Error(TextLayoutErrorCode::Overflow, items[index].source_index));
    const auto item_advance =
        AdvanceFor(font, items[index].codepoint, options.atlas_extent.width,
                   items[index].source_index);
    if (!item_advance)
      return std::unexpected(item_advance.error());
    if (!AddFinite(prefix, *item_advance, prefix))
      return std::unexpected(
          Error(TextLayoutErrorCode::Overflow, items[index].source_index));
    prefix_advances[index + 1U] = prefix;
  }

  for (std::size_t prefix_size = items.size();; --prefix_size) {
    if (prefix_size == 0U || items[prefix_size - 1U].codepoint != U' ') {
      float candidate_advance = prefix_advances[prefix_size];
      if (prefix_size != 0U &&
          !AddFinite(candidate_advance,
                     FindPairAdjustment(
                         pairs, items[prefix_size - 1U].codepoint, U'.'),
                     candidate_advance))
        return std::unexpected(Error(TextLayoutErrorCode::Overflow));
      if (!AddFinite(candidate_advance, *dots_advance, candidate_advance))
        return std::unexpected(Error(TextLayoutErrorCode::Overflow));
      if (candidate_advance <= options.rectangle.width) {
        items.resize(prefix_size);
        items.insert(items.end(), dots.begin(), dots.end());
        advance = candidate_advance;
        return true;
      }
    }
    if (prefix_size == 0U)
      break;
  }
  return std::unexpected(Error(TextLayoutErrorCode::EllipsisUnavailable));
}

[[nodiscard]] std::expected<std::vector<PairLookupEntry>, TextLayoutError>
BuildPairLookup(const retail::FntV3IR &font, const TextLayoutOptions &options) {
  const std::uint64_t pair_count = options.pair_adjustments.size();
  if (pair_count > kMaximumTextLayoutPairAdjustments ||
      pair_count > options.limits.maximum_pair_adjustments)
    return std::unexpected(Error(TextLayoutErrorCode::LimitExceeded));

  std::vector<PairLookupEntry> result;
  result.reserve(options.pair_adjustments.size());
  for (std::size_t index = 0; index < options.pair_adjustments.size();
       ++index) {
    const SignedPairAdjustment &pair = options.pair_adjustments[index];
    if (!IsSupportedCodepoint(font, pair.left) ||
        !IsSupportedCodepoint(font, pair.right) ||
        !std::isfinite(pair.advance_delta))
      return std::unexpected(
          Error(TextLayoutErrorCode::InvalidPairAdjustment, index));
    result.push_back(PairLookupEntry{
        .key = PairKey(pair.left, pair.right),
        .advance_delta = pair.advance_delta,
        .source_index = index,
    });
  }
  std::sort(result.begin(), result.end(),
            [](const PairLookupEntry &left, const PairLookupEntry &right) {
              return left.key < right.key;
            });
  for (std::size_t index = 1; index < result.size(); ++index) {
    if (result[index - 1U].key == result[index].key)
      return std::unexpected(Error(TextLayoutErrorCode::DuplicatePairAdjustment,
                                   result[index].source_index));
  }
  return result;
}

[[nodiscard]] std::optional<TextLayoutError>
ValidateInputs(const retail::FntV3IR &font, const std::u32string_view text,
               const TextLayoutOptions &options) noexcept {
  const GuiTextRectangle &rectangle = options.rectangle;
  const float rectangle_right = rectangle.left + rectangle.width;
  const float rectangle_bottom = rectangle.top - rectangle.height;
  if (!std::isfinite(rectangle.left) || !std::isfinite(rectangle.top) ||
      !std::isfinite(rectangle.width) || !std::isfinite(rectangle.height) ||
      !std::isfinite(rectangle_right) || !std::isfinite(rectangle_bottom) ||
      rectangle.width < 0.0F || rectangle.height < 0.0F ||
      !std::isfinite(options.atlas_extent.width) ||
      !std::isfinite(options.atlas_extent.height) ||
      options.atlas_extent.width <= 0.0F ||
      options.atlas_extent.height <= 0.0F ||
      !std::isfinite(options.line_origin_step) ||
      options.line_origin_step <= 0.0F)
    return Error(TextLayoutErrorCode::InvalidGeometry);

  if (!ValidHorizontalAlignment(options.horizontal_alignment) ||
      !ValidVerticalAlignment(options.vertical_alignment) ||
      !ValidWrapMode(options.wrap_mode) ||
      !ValidEllipsisMode(options.ellipsis_mode))
    return Error(TextLayoutErrorCode::InvalidOption);

  if (text.size() > kMaximumTextLayoutCodepoints ||
      text.size() > options.limits.maximum_codepoints)
    return Error(TextLayoutErrorCode::LimitExceeded);
  if (font.glyphs.size() > retail::kFntV3MaximumGlyphCount)
    return Error(TextLayoutErrorCode::InvalidFont);

  for (const retail::FntV3GlyphIR &glyph : font.glyphs) {
    if (!std::isfinite(glyph.u_left) || !std::isfinite(glyph.u_right) ||
        !std::isfinite(glyph.v_top) || !std::isfinite(glyph.v_bottom) ||
        glyph.u_left < 0.0F || glyph.u_left > 1.0F || glyph.u_right < 0.0F ||
        glyph.u_right > 1.0F || glyph.v_top < 0.0F || glyph.v_top > 1.0F ||
        glyph.v_bottom < 0.0F || glyph.v_bottom > 1.0F ||
        glyph.u_left > glyph.u_right || glyph.v_top > glyph.v_bottom)
      return Error(TextLayoutErrorCode::InvalidFont);
  }

  for (std::size_t index = 0; index < text.size(); ++index) {
    const char32_t codepoint = text[index];
    if (codepoint != U'\n' && !IsSupportedCodepoint(font, codepoint))
      return Error(TextLayoutErrorCode::UnsupportedCodepoint, index);
  }
  return std::nullopt;
}

[[nodiscard]] std::expected<void, TextLayoutError>
CheckLogicalOutputSize(const std::uint64_t line_count,
                       const std::uint64_t glyph_count,
                       const TextLayoutOptions &options) noexcept {
  std::uint64_t line_bytes = 0;
  std::uint64_t glyph_bytes = 0;
  std::uint64_t output_bytes = sizeof(TextLayout);
  if (!Multiply(line_count, sizeof(TextLineLayout), line_bytes) ||
      !Multiply(glyph_count, sizeof(TextGlyphQuad), glyph_bytes) ||
      !Add(output_bytes, line_bytes, output_bytes) ||
      !Add(output_bytes, glyph_bytes, output_bytes))
    return std::unexpected(Error(TextLayoutErrorCode::Overflow));
  if (output_bytes > kMaximumTextLayoutOutputBytes ||
      output_bytes > options.limits.maximum_output_bytes)
    return std::unexpected(Error(TextLayoutErrorCode::LimitExceeded));
  return {};
}

} // namespace

TextLayoutResult LayoutRetailText(const retail::FntV3IR &font,
                                  const std::u32string_view text,
                                  const TextLayoutOptions &options) {
  if (const auto error = ValidateInputs(font, text, options))
    return std::unexpected(*error);

  try {
    auto pairs = BuildPairLookup(font, options);
    if (!pairs)
      return std::unexpected(pairs.error());

    std::vector<WorkingLine> working_lines;
    std::vector<TextItem> current;
    current.reserve(std::min<std::size_t>(text.size(), 256U));
    float current_advance = 0.0F;
    std::optional<std::size_t> last_space;
    bool any_ellipsized = false;

    const auto finalize_line =
        [&](std::vector<TextItem> items,
            float advance) -> std::expected<void, TextLayoutError> {
      if (working_lines.size() >= kMaximumTextLayoutLines ||
          working_lines.size() >= options.limits.maximum_lines)
        return std::unexpected(Error(TextLayoutErrorCode::LimitExceeded));
      auto ellipsized = ApplyEllipsis(font, items, advance, options, *pairs);
      if (!ellipsized)
        return std::unexpected(ellipsized.error());
      any_ellipsized = any_ellipsized || *ellipsized;
      working_lines.push_back(WorkingLine{
          .items = std::move(items),
          .advance = advance,
          .ellipsized = *ellipsized,
      });
      return {};
    };

    const auto recompute_current =
        [&]() -> std::expected<void, TextLayoutError> {
      const auto measured =
          MeasureItems(font, current, options.atlas_extent.width, *pairs);
      if (!measured)
        return std::unexpected(measured.error());
      current_advance = *measured;
      last_space = LastSpace(current);
      return {};
    };

    for (std::size_t input_index = 0; input_index < text.size();
         ++input_index) {
      const char32_t codepoint = text[input_index];
      if (codepoint == U'\n') {
        auto finalized = finalize_line(std::move(current), current_advance);
        if (!finalized)
          return std::unexpected(finalized.error());
        current.clear();
        current_advance = 0.0F;
        last_space.reset();
        continue;
      }

      if (!current.empty() &&
          !AddFinite(
              current_advance,
              FindPairAdjustment(*pairs, current.back().codepoint, codepoint),
              current_advance))
        return std::unexpected(
            Error(TextLayoutErrorCode::Overflow, input_index));
      const auto item_advance =
          AdvanceFor(font, codepoint, options.atlas_extent.width, input_index);
      if (!item_advance)
        return std::unexpected(item_advance.error());
      if (!AddFinite(current_advance, *item_advance, current_advance))
        return std::unexpected(
            Error(TextLayoutErrorCode::Overflow, input_index));
      current.push_back(
          TextItem{.codepoint = codepoint, .source_index = input_index});
      if (codepoint == U' ')
        last_space = current.size() - 1U;

      if (options.wrap_mode == TextWrapMode::SpacesAndExplicitNewlines &&
          current_advance > options.rectangle.width && last_space &&
          HasNonSpaceBefore(current, *last_space)) {
        std::vector<TextItem> line_items(current.begin(),
                                         current.begin() + *last_space);
        TrimTrailingSpaces(line_items);
        std::vector<TextItem> remainder(current.begin() + *last_space + 1U,
                                        current.end());
        TrimLeadingSpaces(remainder);
        const auto line_advance =
            MeasureItems(font, line_items, options.atlas_extent.width, *pairs);
        if (!line_advance)
          return std::unexpected(line_advance.error());
        auto finalized = finalize_line(std::move(line_items), *line_advance);
        if (!finalized)
          return std::unexpected(finalized.error());
        current = std::move(remainder);
        auto recomputed = recompute_current();
        if (!recomputed)
          return std::unexpected(recomputed.error());
      }
    }

    auto finalized = finalize_line(std::move(current), current_advance);
    if (!finalized)
      return std::unexpected(finalized.error());

    std::uint64_t glyph_count = 0U;
    for (const WorkingLine &line : working_lines) {
      for (const TextItem &item : line.items) {
        if (item.codepoint != U' ' && !Add(glyph_count, 1U, glyph_count))
          return std::unexpected(Error(TextLayoutErrorCode::Overflow));
      }
    }
    if (glyph_count > kMaximumTextLayoutGlyphs ||
        glyph_count > options.limits.maximum_glyphs)
      return std::unexpected(Error(TextLayoutErrorCode::LimitExceeded));
    auto output_size =
        CheckLogicalOutputSize(working_lines.size(), glyph_count, options);
    if (!output_size)
      return std::unexpected(output_size.error());

    float block_height = 0.0F;
    if (!MultiplyFinite(options.line_origin_step,
                        static_cast<float>(working_lines.size()), block_height))
      return std::unexpected(Error(TextLayoutErrorCode::Overflow));
    float first_source_y = options.rectangle.top;
    switch (options.vertical_alignment) {
    case VerticalTextAlignment::Top:
      break;
    case VerticalTextAlignment::Center: {
      float unused_height = 0.0F;
      if (!AddFinite(options.rectangle.height, -block_height, unused_height) ||
          !AddFinite(options.rectangle.top, -unused_height * 0.5F,
                     first_source_y))
        return std::unexpected(Error(TextLayoutErrorCode::Overflow));
      break;
    }
    case VerticalTextAlignment::Bottom:
      if (!AddFinite(options.rectangle.top, -options.rectangle.height,
                     first_source_y) ||
          !AddFinite(first_source_y, block_height, first_source_y))
        return std::unexpected(Error(TextLayoutErrorCode::Overflow));
      break;
    }

    TextLayout result;
    result.ellipsized = any_ellipsized;
    result.lines.reserve(working_lines.size());
    result.glyphs.reserve(static_cast<std::size_t>(glyph_count));

    for (std::size_t line_index = 0; line_index < working_lines.size();
         ++line_index) {
      const WorkingLine &working_line = working_lines[line_index];
      float line_left = options.rectangle.left;
      switch (options.horizontal_alignment) {
      case HorizontalTextAlignment::Left:
        break;
      case HorizontalTextAlignment::Right:
        if (!AddFinite(options.rectangle.left, options.rectangle.width,
                       line_left) ||
            !AddFinite(line_left, -working_line.advance, line_left))
          return std::unexpected(Error(TextLayoutErrorCode::Overflow));
        break;
      case HorizontalTextAlignment::Center: {
        float remaining_width = 0.0F;
        if (!AddFinite(options.rectangle.width, -working_line.advance,
                       remaining_width) ||
            !AddFinite(options.rectangle.left, remaining_width * 0.5F,
                       line_left))
          return std::unexpected(Error(TextLayoutErrorCode::Overflow));
        break;
      }
      }

      float source_y = 0.0F;
      float line_offset = 0.0F;
      if (!MultiplyFinite(options.line_origin_step,
                          static_cast<float>(line_index), line_offset) ||
          !AddFinite(first_source_y, -line_offset, source_y))
        return std::unexpected(Error(TextLayoutErrorCode::Overflow));

      const std::size_t first_glyph = result.glyphs.size();
      float cursor = line_left;
      for (std::size_t item_index = 0; item_index < working_line.items.size();
           ++item_index) {
        const TextItem &item = working_line.items[item_index];
        if (item_index != 0U &&
            !AddFinite(cursor,
                       FindPairAdjustment(
                           *pairs,
                           working_line.items[item_index - 1U].codepoint,
                           item.codepoint),
                       cursor))
          return std::unexpected(
              Error(TextLayoutErrorCode::Overflow, item.source_index));

        const auto advance =
            AdvanceFor(font, item.codepoint, options.atlas_extent.width,
                       item.source_index);
        if (!advance)
          return std::unexpected(advance.error());
        if (item.codepoint != U' ') {
          const retail::FntV3GlyphIR *glyph =
              font.GlyphForCodepoint(item.codepoint);
          if (glyph == nullptr)
            return std::unexpected(Error(
                TextLayoutErrorCode::UnsupportedCodepoint, item.source_index));
          float glyph_height = 0.0F;
          if (!MultiplyFinite(options.atlas_extent.height,
                              glyph->v_bottom - glyph->v_top, glyph_height))
            return std::unexpected(
                Error(TextLayoutErrorCode::Overflow, item.source_index));
          const float glyph_top =
              font.ApplyObservedByte17VerticalPlacement(source_y);
          float glyph_right = 0.0F;
          float glyph_bottom = 0.0F;
          if (!std::isfinite(glyph_top) ||
              !AddFinite(cursor, *advance, glyph_right) ||
              !AddFinite(glyph_top, -glyph_height, glyph_bottom))
            return std::unexpected(
                Error(TextLayoutErrorCode::Overflow, item.source_index));
          result.glyphs.push_back(TextGlyphQuad{
              .codepoint = item.codepoint,
              .source_index = item.source_index,
              .line_index = line_index,
              .left = cursor,
              .top = glyph_top,
              .right = glyph_right,
              .bottom = glyph_bottom,
              .uv = *glyph,
          });
        }
        if (!AddFinite(cursor, *advance, cursor))
          return std::unexpected(
              Error(TextLayoutErrorCode::Overflow, item.source_index));
      }

      result.lines.push_back(TextLineLayout{
          .first_glyph = first_glyph,
          .glyph_count = result.glyphs.size() - first_glyph,
          .left = line_left,
          .source_y = source_y,
          .advance = working_line.advance,
          .ellipsized = working_line.ellipsized,
      });
    }
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(Error(TextLayoutErrorCode::AllocationFailure));
  } catch (const std::length_error &) {
    return std::unexpected(Error(TextLayoutErrorCode::Overflow));
  }
}

} // namespace omega::frontend
