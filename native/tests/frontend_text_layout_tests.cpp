#include "omega/frontend_text/text_layout.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

void CheckNear(const float actual, const float expected,
               const std::string_view message) {
  Check(std::fabs(actual - expected) <= 0.0001F, message);
}

void CheckError(const omega::frontend::TextLayoutResult &result,
                const omega::frontend::TextLayoutErrorCode expected,
                const std::string_view message) {
  Check(!result && result.error().code == expected, message);
}

[[nodiscard]] omega::retail::FntV3IR MakeFont() {
  omega::retail::FntV3IR font;
  font.atlas_reference = "SYNTH001.TDX";
  font.raw_byte_16 = 7U;
  font.raw_byte_17 = 2U;
  font.space_advance = 3;
  font.glyphs.resize(0x7EU - 0x21U + 1U);
  for (auto &glyph : font.glyphs) {
    glyph = omega::retail::FntV3GlyphIR{
        .u_left = 0.0F,
        .u_right = 0.05F,
        .v_top = 0.10F,
        .v_bottom = 0.20F,
    };
  }
  const std::size_t dot_index = static_cast<std::size_t>(U'.' - U'!');
  font.glyphs[dot_index].u_right = 0.02F;
  return font;
}

[[nodiscard]] omega::frontend::TextLayoutLimits GenerousLimits() {
  return omega::frontend::TextLayoutLimits{
      .maximum_codepoints = omega::frontend::kMaximumTextLayoutCodepoints,
      .maximum_lines = omega::frontend::kMaximumTextLayoutLines,
      .maximum_glyphs = omega::frontend::kMaximumTextLayoutGlyphs,
      .maximum_pair_adjustments =
          omega::frontend::kMaximumTextLayoutPairAdjustments,
      .maximum_output_bytes = omega::frontend::kMaximumTextLayoutOutputBytes,
  };
}

[[nodiscard]] omega::frontend::TextLayoutOptions MakeOptions(
    const std::span<const omega::frontend::SignedPairAdjustment> pairs = {}) {
  return omega::frontend::TextLayoutOptions{
      .rectangle = {.left = 10.0F,
                    .top = 50.0F,
                    .width = 100.0F,
                    .height = 48.0F},
      .atlas_extent = {.width = 100.0F, .height = 100.0F},
      .line_origin_step = 12.0F,
      .horizontal_alignment = omega::frontend::HorizontalTextAlignment::Left,
      .vertical_alignment = omega::frontend::VerticalTextAlignment::Top,
      .wrap_mode = omega::frontend::TextWrapMode::SpacesAndExplicitNewlines,
      .ellipsis_mode = omega::frontend::TextEllipsisMode::Disabled,
      .pair_adjustments = pairs,
      .limits = GenerousLimits(),
  };
}

} // namespace

int main() {
  using omega::frontend::HorizontalTextAlignment;
  using omega::frontend::LayoutRetailText;
  using omega::frontend::SignedPairAdjustment;
  using omega::frontend::TextEllipsisMode;
  using omega::frontend::TextLayoutErrorCode;
  using omega::frontend::TextWrapMode;
  using omega::frontend::VerticalTextAlignment;

  const omega::retail::FntV3IR font = MakeFont();
  const std::array pair_adjustments{
      SignedPairAdjustment{.left = U'A', .right = U'V', .advance_delta = -1.0F},
  };
  const auto basic =
      LayoutRetailText(font, U"AV", MakeOptions(pair_adjustments));
  Check(basic.has_value(), "generated font lays out a two-glyph run");
  if (basic) {
    Check(basic->lines.size() == 1U && basic->glyphs.size() == 2U &&
              !basic->ellipsized,
          "layout owns one line and two glyph quads");
    CheckNear(basic->lines[0].advance, 9.0F,
              "signed pair adjustment changes the measured advance");
    CheckNear(basic->glyphs[0].left, 10.0F,
              "left alignment begins at the GUI rectangle left edge");
    CheckNear(basic->glyphs[0].right, 15.0F,
              "atlas width and UV span determine glyph width");
    CheckNear(basic->glyphs[1].left, 14.0F,
              "signed pair adjustment is applied before the right glyph");
    CheckNear(basic->glyphs[0].top, 48.0F,
              "the unnamed byte-17 behavior subtracts from source Y");
    CheckNear(basic->glyphs[0].bottom, 38.0F,
              "atlas height and the UV tuple determine quad height");
    Check(basic->glyphs[0].source_index == 0U &&
              basic->glyphs[1].source_index == 1U &&
              basic->glyphs[0].uv == *font.GlyphForCodepoint(U'A'),
          "glyph quads own source identity and the selected UV tuple");
  }

  const auto first_printable = LayoutRetailText(font, U"!", MakeOptions());
  Check(first_printable && first_printable->glyphs.size() == 1U &&
            first_printable->glyphs[0].uv == font.glyphs[0],
        "printable codepoint 0x21 selects FNT record zero");

  auto signed_space_font = font;
  signed_space_font.space_advance = -2;
  const auto signed_space =
      LayoutRetailText(signed_space_font, U"A A", MakeOptions());
  Check(signed_space && signed_space->glyphs.size() == 2U,
        "spaces contribute advance without producing glyph quads");
  if (signed_space) {
    CheckNear(signed_space->lines[0].advance, 8.0F,
              "the FNT space scalar remains signed");
    CheckNear(signed_space->glyphs[1].left, 13.0F,
              "a signed negative space advance moves the next glyph left");
  }

  auto right_options = MakeOptions();
  right_options.rectangle.width = 30.0F;
  right_options.horizontal_alignment = HorizontalTextAlignment::Right;
  const auto right = LayoutRetailText(font, U"AV", right_options);
  Check(right && right->lines.size() == 1U,
        "right-aligned generated run lays out");
  if (right)
    CheckNear(right->lines[0].left, 30.0F,
              "right alignment uses rectangle right minus line advance");

  auto center_options = right_options;
  center_options.horizontal_alignment = HorizontalTextAlignment::Center;
  const auto center = LayoutRetailText(font, U"AV", center_options);
  Check(center && center->lines.size() == 1U,
        "center-aligned generated run lays out");
  if (center)
    CheckNear(center->lines[0].left, 20.0F,
              "center alignment divides the unused horizontal extent");

  auto vertical_options = MakeOptions();
  vertical_options.rectangle.height = 36.0F;
  const auto top = LayoutRetailText(font, U"A\nV", vertical_options);
  Check(top && top->lines.size() == 2U, "explicit newline produces two lines");
  if (top) {
    CheckNear(top->lines[0].source_y, 50.0F,
              "top alignment keeps the first source origin at rectangle top");
    CheckNear(top->lines[1].source_y, 38.0F,
              "GUI Y-up line origins descend by the explicit step");
  }

  vertical_options.vertical_alignment = VerticalTextAlignment::Center;
  const auto vertically_centered =
      LayoutRetailText(font, U"A\nV", vertical_options);
  Check(vertically_centered && vertically_centered->lines.size() == 2U,
        "explicit center vertical mode lays out");
  if (vertically_centered) {
    CheckNear(vertically_centered->lines[0].source_y, 44.0F,
              "center mode splits unused line-box height");
    CheckNear(vertically_centered->lines[1].source_y, 32.0F,
              "centered second line retains the explicit step");
  }

  vertical_options.vertical_alignment = VerticalTextAlignment::Bottom;
  const auto bottom = LayoutRetailText(font, U"A\nV", vertical_options);
  Check(bottom && bottom->lines.size() == 2U,
        "explicit bottom vertical mode lays out");
  if (bottom) {
    CheckNear(bottom->lines[0].source_y, 38.0F,
              "bottom mode aligns the explicit line boxes to rectangle bottom");
    CheckNear(bottom->lines[1].source_y, 26.0F,
              "bottom-aligned lines remain ordered in GUI Y-up space");
  }

  auto wrap_options = MakeOptions();
  wrap_options.rectangle.width = 12.0F;
  const auto wrapped = LayoutRetailText(font, U"AA AA", wrap_options);
  Check(wrapped && wrapped->lines.size() == 2U && wrapped->glyphs.size() == 4U,
        "space overflow wraps a generated string into two owned lines");
  if (wrapped) {
    CheckNear(wrapped->lines[0].advance, 13.0F,
              "wrap retains the consumed boundary-space advance");
    CheckNear(wrapped->lines[1].advance, 10.0F,
              "the wrapped word is measured independently");
    Check(wrapped->glyphs[2].source_index == 3U,
          "wrapped glyph retains its original source index");
  }

  wrap_options.horizontal_alignment = HorizontalTextAlignment::Center;
  const auto centered_wrap =
      LayoutRetailText(font, U"AA AA", wrap_options);
  Check(centered_wrap && centered_wrap->lines.size() == 2U,
        "centered wrap fixture lays out two lines");
  if (centered_wrap) {
    CheckNear(centered_wrap->lines[0].left, 9.5F,
              "consumed boundary advance participates in centered placement");
    CheckNear(centered_wrap->lines[1].left, 11.0F,
              "the independent wrapped word uses only its own advance");
  }

  wrap_options.wrap_mode = TextWrapMode::ExplicitNewlinesOnly;
  wrap_options.rectangle.width = 8.0F;
  const auto no_wrap = LayoutRetailText(font, U"AAA", wrap_options);
  Check(no_wrap && no_wrap->lines.size() == 1U && no_wrap->glyphs.size() == 3U,
        "explicit no-wrap mode preserves one over-wide line");
  if (no_wrap)
    CheckNear(no_wrap->lines[0].advance, 15.0F,
              "no-wrap does not synthesize a character break");

  auto ellipsis_options = wrap_options;
  ellipsis_options.rectangle.width = 11.0F;
  ellipsis_options.ellipsis_mode = TextEllipsisMode::LiteralThreeDots;
  const auto ellipsized = LayoutRetailText(font, U"AAAA", ellipsis_options);
  Check(ellipsized && ellipsized->ellipsized &&
            ellipsized->lines.size() == 1U && ellipsized->lines[0].ellipsized &&
            ellipsized->glyphs.size() == 4U,
        "ellipsis mode replaces an over-wide suffix with three dots");
  if (ellipsized) {
    Check(ellipsized->glyphs[0].codepoint == U'A' &&
              ellipsized->glyphs[0].source_index == 0U &&
              ellipsized->glyphs[1].codepoint == U'.' &&
              !ellipsized->glyphs[1].source_index &&
              !ellipsized->glyphs[2].source_index &&
              !ellipsized->glyphs[3].source_index,
          "literal ellipsis dots are identified as generated glyphs");
    CheckNear(ellipsized->lines[0].advance, 11.0F,
              "ellipsis reserves the exact asset-derived dot advances");
  }

  const auto empty = LayoutRetailText(font, U"", MakeOptions());
  Check(empty && empty->lines.size() == 1U && empty->glyphs.empty() &&
            empty->lines[0].advance == 0.0F,
        "empty input deterministically owns one empty layout line");

  std::u32string transient = U"AV";
  const auto owned = LayoutRetailText(font, transient, MakeOptions());
  transient.assign(U"XX");
  Check(owned && owned->glyphs.size() == 2U &&
            owned->glyphs[0].codepoint == U'A' &&
            owned->glyphs[1].codepoint == U'V',
        "layout output remains owned after caller text mutation");
  const auto repeated = LayoutRetailText(font, U"AV", MakeOptions());
  const auto repeated_again = LayoutRetailText(font, U"AV", MakeOptions());
  Check(repeated && repeated_again && *repeated == *repeated_again,
        "layout is deterministic and holds no cross-call state");
  auto different_unknown_metric = font;
  different_unknown_metric.raw_byte_16 = 0xF0U;
  const auto unknown_metric_layout =
      LayoutRetailText(different_unknown_metric, U"AV", MakeOptions());
  Check(repeated && unknown_metric_layout &&
            *repeated == *unknown_metric_layout,
        "the unproven byte-16 metric does not acquire invented layout meaning");
  auto different_observed_vertical_metric = font;
  different_observed_vertical_metric.raw_byte_17 = 5U;
  const auto shifted =
      LayoutRetailText(different_observed_vertical_metric, U"A", MakeOptions());
  Check(shifted && shifted->glyphs.size() == 1U,
        "changed observed vertical-placement metric lays out");
  if (shifted)
    CheckNear(shifted->glyphs[0].top, 45.0F,
              "byte-17 subtraction remains visible in canonical GUI space");

  auto bad_options = MakeOptions();
  bad_options.rectangle.left = std::numeric_limits<float>::quiet_NaN();
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::InvalidGeometry,
             "nonfinite GUI rectangle input fails closed");
  bad_options = MakeOptions();
  bad_options.atlas_extent.width = std::numeric_limits<float>::infinity();
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::InvalidGeometry,
             "nonfinite atlas extent fails closed");
  bad_options = MakeOptions();
  bad_options.line_origin_step = 0.0F;
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::InvalidGeometry,
             "nonpositive explicit line step fails closed");
  bad_options = MakeOptions();
  bad_options.vertical_alignment = static_cast<VerticalTextAlignment>(0xFFU);
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::InvalidOption,
             "unknown vertical alignment fails closed instead of defaulting");
  bad_options = MakeOptions();
  bad_options.horizontal_alignment =
      static_cast<HorizontalTextAlignment>(0xFFU);
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::InvalidOption,
             "unknown horizontal alignment fails closed");
  bad_options = MakeOptions();
  bad_options.wrap_mode = static_cast<TextWrapMode>(0xFFU);
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::InvalidOption,
             "unknown wrap mode fails closed");

  auto bad_font = font;
  bad_font.glyphs[0].u_left = std::numeric_limits<float>::quiet_NaN();
  CheckError(LayoutRetailText(bad_font, U"A", MakeOptions()),
             TextLayoutErrorCode::InvalidFont,
             "nonfinite FNT UV input fails closed");
  bad_font = font;
  bad_font.glyphs[0].v_top = 0.9F;
  bad_font.glyphs[0].v_bottom = 0.2F;
  CheckError(LayoutRetailText(bad_font, U"A", MakeOptions()),
             TextLayoutErrorCode::InvalidFont,
             "reversed FNT UV bounds fail closed");
  bad_font = font;
  bad_font.glyphs.resize(
      static_cast<std::size_t>(omega::retail::kFntV3MaximumGlyphCount) + 1U);
  CheckError(LayoutRetailText(bad_font, U"A", MakeOptions()),
             TextLayoutErrorCode::InvalidFont,
             "oversized manually-constructed FNT IR fails closed");

  const auto unsupported = LayoutRetailText(font, U"\r", MakeOptions());
  CheckError(unsupported, TextLayoutErrorCode::UnsupportedCodepoint,
             "unsupported control codepoint fails closed");
  Check(!unsupported && unsupported.error().input_index == 0U,
        "unsupported codepoint reports only its bounded input index");

  const std::array nonfinite_pair{
      SignedPairAdjustment{
          .left = U'A',
          .right = U'V',
          .advance_delta = std::numeric_limits<float>::quiet_NaN(),
      },
  };
  CheckError(LayoutRetailText(font, U"AV", MakeOptions(nonfinite_pair)),
             TextLayoutErrorCode::InvalidPairAdjustment,
             "nonfinite pair adjustment fails closed");
  const std::array unsupported_pair{
      SignedPairAdjustment{.left = U'\n', .right = U'V', .advance_delta = 1.0F},
  };
  CheckError(LayoutRetailText(font, U"AV", MakeOptions(unsupported_pair)),
             TextLayoutErrorCode::InvalidPairAdjustment,
             "pair adjustment cannot name an unsupported codepoint");
  const std::array duplicate_pairs{
      SignedPairAdjustment{.left = U'A', .right = U'V', .advance_delta = -1.0F},
      SignedPairAdjustment{.left = U'A', .right = U'V', .advance_delta = 2.0F},
  };
  CheckError(LayoutRetailText(font, U"AV", MakeOptions(duplicate_pairs)),
             TextLayoutErrorCode::DuplicatePairAdjustment,
             "duplicate pair keys fail closed instead of depending on order");

  bad_options = MakeOptions();
  bad_options.rectangle.width = 5.0F;
  bad_options.wrap_mode = TextWrapMode::ExplicitNewlinesOnly;
  bad_options.ellipsis_mode = TextEllipsisMode::LiteralThreeDots;
  CheckError(LayoutRetailText(font, U"AA", bad_options),
             TextLayoutErrorCode::EllipsisUnavailable,
             "ellipsis fails closed when three asset-derived dots cannot fit");

  bad_options = MakeOptions();
  bad_options.limits.maximum_codepoints = 1U;
  CheckError(LayoutRetailText(font, U"AA", bad_options),
             TextLayoutErrorCode::LimitExceeded,
             "caller codepoint limit is enforced");
  const std::u32string above_fixed_codepoint_limit(
      static_cast<std::size_t>(omega::frontend::kMaximumTextLayoutCodepoints) +
          1U,
      U'A');
  bad_options = MakeOptions();
  bad_options.limits.maximum_codepoints =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(LayoutRetailText(font, above_fixed_codepoint_limit, bad_options),
             TextLayoutErrorCode::LimitExceeded,
             "caller limits cannot widen the fixed codepoint ceiling");
  bad_options = MakeOptions();
  bad_options.limits.maximum_lines = 1U;
  CheckError(LayoutRetailText(font, U"A\nA", bad_options),
             TextLayoutErrorCode::LimitExceeded,
             "caller line limit is enforced");
  bad_options = MakeOptions();
  bad_options.limits.maximum_glyphs = 1U;
  CheckError(LayoutRetailText(font, U"AA", bad_options),
             TextLayoutErrorCode::LimitExceeded,
             "caller glyph limit is enforced");
  bad_options = MakeOptions(pair_adjustments);
  bad_options.limits.maximum_pair_adjustments = 0U;
  CheckError(LayoutRetailText(font, U"AV", bad_options),
             TextLayoutErrorCode::LimitExceeded,
             "caller pair-table limit is enforced before allocation");
  bad_options = MakeOptions();
  bad_options.limits.maximum_output_bytes = sizeof(omega::frontend::TextLayout);
  CheckError(LayoutRetailText(font, U"A", bad_options),
             TextLayoutErrorCode::LimitExceeded,
             "caller logical-output byte limit is enforced");

  auto overflow_font = font;
  for (auto &glyph : overflow_font.glyphs) {
    glyph.u_left = 0.0F;
    glyph.u_right = 1.0F;
  }
  bad_options = MakeOptions();
  bad_options.rectangle.width = std::numeric_limits<float>::max();
  bad_options.atlas_extent.width = std::numeric_limits<float>::max();
  CheckError(LayoutRetailText(overflow_font, U"AA", bad_options),
             TextLayoutErrorCode::Overflow,
             "finite metric accumulation overflow fails closed");

  if (failures != 0)
    std::cerr << failures << " frontend text layout test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
