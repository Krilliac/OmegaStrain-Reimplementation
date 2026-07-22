#include "omega/retail/fnt_v3_decoder.h"

#include <algorithm>
#include <array>
#include <bit>
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
constexpr std::string_view kSyntheticAtlasReference = "SYNTH001.TDX";
constexpr std::size_t kGlyphsOffset = 19U;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <typename Result>
void CheckError(const Result &result, const omega::asset::DecodeErrorCode code,
                const std::string_view message) {
  if (result) {
    Check(false, message);
    return;
  }
  Check(result.error().code == code, message);
  Check(!result.error().message.empty(),
        "FNT v3 errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "FNT v3 errors contain no filesystem path");
}

void WriteU16(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
  bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
  bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

void WriteF32(std::vector<std::byte> &bytes, const std::size_t offset,
              const float value) {
  WriteU32(bytes, offset, std::bit_cast<std::uint32_t>(value));
}

[[nodiscard]] constexpr std::size_t
StructuralEnd(const std::uint8_t glyph_count) {
  return kGlyphsOffset +
         static_cast<std::size_t>(glyph_count) *
             omega::retail::kFntV3GlyphRecordBytes +
         2U;
}

[[nodiscard]] std::vector<std::byte>
MakeFnt(const std::uint8_t glyph_count = 177U,
        const std::size_t tail_bytes = 11U) {
  std::vector<std::byte> bytes(StructuralEnd(glyph_count) + tail_bytes,
                               std::byte{0});
  WriteU16(bytes, 0, omega::retail::kFntV3Version);
  bytes[2] = static_cast<std::byte>(omega::retail::kFntV3NameSpan);
  for (std::size_t index = 0; index < kSyntheticAtlasReference.size(); ++index)
    bytes[3U + index] = static_cast<std::byte>(
        static_cast<unsigned char>(kSyntheticAtlasReference[index]));
  bytes[15] = std::byte{0};
  bytes[16] = std::byte{4};
  bytes[17] = std::byte{9};
  bytes[18] = static_cast<std::byte>(glyph_count);

  for (std::size_t glyph_index = 0; glyph_index < glyph_count; ++glyph_index) {
    const float u_left = static_cast<float>(glyph_index % 8U) / 16.0F;
    const float u_right = u_left + 0.0625F;
    const float v_top = static_cast<float>(glyph_index % 4U) / 8.0F;
    const float v_bottom = v_top + 0.125F;
    const std::size_t offset =
        kGlyphsOffset + glyph_index * omega::retail::kFntV3GlyphRecordBytes;
    WriteF32(bytes, offset, u_left);
    WriteF32(bytes, offset + 4U, u_right);
    WriteF32(bytes, offset + 8U, v_top);
    WriteF32(bytes, offset + 12U, v_bottom);
  }
  if (glyph_count != 0) {
    WriteF32(bytes, kGlyphsOffset, 0.125F);
    WriteF32(bytes, kGlyphsOffset + 4U, 0.375F);
    WriteF32(bytes, kGlyphsOffset + 8U, 0.25F);
    WriteF32(bytes, kGlyphsOffset + 12U, 0.5F);
  }

  const std::size_t space_advance_offset =
      kGlyphsOffset + static_cast<std::size_t>(glyph_count) *
                          omega::retail::kFntV3GlyphRecordBytes;
  bytes[space_advance_offset] = std::byte{0xFB};
  bytes[space_advance_offset + 1U] = std::byte{0};
  return bytes;
}
} // namespace

int main() {
  static_assert(kSyntheticAtlasReference.size() ==
                omega::retail::kFntV3AtlasReferenceBytes);
  static_assert(omega::retail::kFntV3MaximumGlyphCount + 1U == 0xB5U);
  static_assert(StructuralEnd(177U) + 11U == 2864U);

  const auto bytes = MakeFnt();
  const auto decoded = omega::retail::DecodeFntV3(bytes);
  Check(decoded.has_value(),
        "FNT v3 accepts a generated full-size observed-family fixture");
  if (decoded) {
    Check(decoded->atlas_reference == kSyntheticAtlasReference &&
              decoded->raw_byte_16 == 4U && decoded->raw_byte_17 == 9U &&
              decoded->glyphs.size() == 177U && decoded->space_advance == -5,
          "FNT v3 owns the atlas name, unnamed raw bytes, UV count, and signed "
          "space advance");
    const auto *first = decoded->GlyphForCodepoint(0x21U);
    const auto *last = decoded->GlyphForCodepoint(0xD1U);
    Check(first != nullptr && first->u_left == 0.125F &&
              first->u_right == 0.375F && first->v_top == 0.25F &&
              first->v_bottom == 0.5F && last != nullptr &&
              decoded->GlyphForCodepoint(0x20U) == nullptr &&
              decoded->GlyphForCodepoint(0xD2U) == nullptr,
          "FNT v3 maps printable codepoints to the proven source-order UV "
          "records");
    const auto glyph_width = decoded->AdvanceForCodepoint(0x21U, 256.0F);
    const auto space_width = decoded->AdvanceForCodepoint(0x20U, 256.0F);
    Check(
        glyph_width && *glyph_width == 64.0F && space_width &&
            *space_width == -5.0F,
        "FNT v3 applies proven atlas-width and signed-space advance behavior");
    Check(decoded->ApplyObservedByte17VerticalPlacement(100.0F) == 91.0F,
          "FNT v3 exposes only the proven byte-17 vertical-placement "
          "subtraction");
    Check(!decoded->AdvanceForCodepoint(0x21U, -1.0F) &&
              !decoded->AdvanceForCodepoint(
                  0x21U, std::numeric_limits<float>::infinity()),
          "FNT v3 rejects invalid atlas widths in its width adapter");
  }

  const auto repeated = omega::retail::DecodeFntV3(bytes);
  Check(decoded && repeated && *decoded == *repeated,
        "FNT v3 decoding is deterministic and stateless");

  std::vector<std::byte> unaligned_storage(bytes.size() + 1U, std::byte{0x6D});
  std::ranges::copy(bytes, unaligned_storage.begin() + 1);
  const auto unaligned = omega::retail::DecodeFntV3(
      std::span<const std::byte>(unaligned_storage.data() + 1, bytes.size()));
  Check(decoded && unaligned && *decoded == *unaligned,
        "FNT v3 reads unaligned little-endian float records safely");

  omega::retail::FntV3IR owned;
  {
    auto transient = MakeFnt();
    auto transient_result = omega::retail::DecodeFntV3(transient);
    Check(transient_result.has_value(), "FNT v3 ownership fixture decodes");
    if (transient_result)
      owned = std::move(*transient_result);
    std::ranges::fill(transient, std::byte{0xEE});
  }
  Check(owned.atlas_reference == kSyntheticAtlasReference &&
            owned.glyphs.size() == 177U && owned.glyphs[0].u_left == 0.125F,
        "FNT v3 output remains valid after source destruction");

  const std::size_t structural_end = StructuralEnd(177U);
  bool selected_truncations_rejected = true;
  const std::array<std::size_t, 10> truncation_sizes{0U,
                                                     1U,
                                                     2U,
                                                     3U,
                                                     15U,
                                                     16U,
                                                     18U,
                                                     19U,
                                                     structural_end - 2U,
                                                     structural_end - 1U};
  for (const std::size_t size : truncation_sizes) {
    const auto result = omega::retail::DecodeFntV3(
        std::span<const std::byte>(bytes.data(), size));
    selected_truncations_rejected =
        selected_truncations_rejected && !result &&
        result.error().code == omega::asset::DecodeErrorCode::Truncated;
  }
  Check(selected_truncations_rejected,
        "FNT v3 rejects representative prefix, record, and metric truncations");

  auto bad = bytes;
  WriteU16(bad, 0, 4U);
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "FNT v3 rejects a different version");

  bad = bytes;
  bad[2] = std::byte{12};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "FNT v3 rejects an unsupported name-span value");

  bad = bytes;
  bad[3] = std::byte{0x1F};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 rejects a non-printable atlas reference");

  bad = bytes;
  bad[3] = std::byte{'/'};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::InvalidReference,
             "FNT v3 rejects path syntax in its atlas member reference");

  bad = bytes;
  bad[14] = std::byte{'Q'};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::InvalidReference,
             "FNT v3 requires the proven .TDX atlas suffix");

  bad = bytes;
  bad[15] = std::byte{'X'};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 requires a NUL after the exact atlas reference");

  bad = bytes;
  bad[18] = std::byte{0xB5};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT v3 rejects the hostile first unsupported glyph count");

  bad = bytes;
  WriteF32(bad, kGlyphsOffset, std::numeric_limits<float>::infinity());
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 rejects infinite UV values");

  bad = bytes;
  WriteF32(bad, kGlyphsOffset, std::numeric_limits<float>::quiet_NaN());
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 rejects NaN UV values");

  bad = bytes;
  WriteF32(bad, kGlyphsOffset, -0.01F);
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 rejects UV values below the normalized range");

  bad = bytes;
  WriteF32(bad, kGlyphsOffset, 0.75F);
  WriteF32(bad, kGlyphsOffset + 4U, 0.5F);
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 rejects reversed horizontal UV bounds");

  bad = bytes;
  const std::size_t pair_presence_offset = structural_end - 1U;
  bad[pair_presence_offset] = std::byte{1};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::UnsupportedVariant,
             "FNT v3 fails closed on the unproven optional pair-table grammar");

  bad = bytes;
  bad[structural_end] = std::byte{1};
  CheckError(omega::retail::DecodeFntV3(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "FNT v3 rejects nonzero trailing bytes");

  const auto overlong_tail = MakeFnt(177U, 16U);
  CheckError(omega::retail::DecodeFntV3(overlong_tail),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT v3 rejects a zero tail above its fixed bound");

  const auto maximum_count = MakeFnt(
      static_cast<std::uint8_t>(omega::retail::kFntV3MaximumGlyphCount), 0U);
  Check(omega::retail::DecodeFntV3(maximum_count).has_value(),
        "FNT v3 accepts the exact proven glyph-count ceiling");
  const auto zero_count = MakeFnt(0U, 0U);
  const auto zero_count_result = omega::retail::DecodeFntV3(zero_count);
  Check(zero_count_result && zero_count_result->glyphs.empty(),
        "FNT v3 handles a bounded zero-count record table without synthesizing "
        "glyphs");

  auto limits = omega::asset::DecodeLimits{};
  limits.maximum_input_bytes = bytes.size();
  Check(omega::retail::DecodeFntV3(bytes, limits).has_value(),
        "FNT v3 accepts the exact caller input-byte budget");
  limits.maximum_input_bytes = bytes.size() - 1U;
  CheckError(omega::retail::DecodeFntV3(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT v3 rejects one below the caller input-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_items = 178U;
  Check(omega::retail::DecodeFntV3(bytes, limits).has_value(),
        "FNT v3 accepts the exact caller item budget");
  --limits.maximum_items;
  CheckError(omega::retail::DecodeFntV3(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT v3 rejects one below the caller item budget");

  const std::uint64_t logical_output_bytes =
      sizeof(omega::retail::FntV3IR) +
      omega::retail::kFntV3AtlasReferenceBytes +
      177U * sizeof(omega::retail::FntV3GlyphIR);
  limits = omega::asset::DecodeLimits{};
  limits.maximum_output_bytes = logical_output_bytes;
  Check(omega::retail::DecodeFntV3(bytes, limits).has_value(),
        "FNT v3 accepts the exact caller logical-output budget");
  --limits.maximum_output_bytes;
  CheckError(omega::retail::DecodeFntV3(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT v3 rejects one below the caller logical-output budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_string_bytes = omega::retail::kFntV3AtlasReferenceBytes;
  Check(omega::retail::DecodeFntV3(bytes, limits).has_value(),
        "FNT v3 accepts the exact caller atlas-string budget");
  --limits.maximum_string_bytes;
  CheckError(omega::retail::DecodeFntV3(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "FNT v3 rejects one below the caller atlas-string budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_scratch_bytes = 0;
  limits.maximum_nesting_depth = 0;
  Check(omega::retail::DecodeFntV3(bytes, limits).has_value(),
        "flat FNT v3 decoding needs no dynamic scratch or nesting edges");

  std::vector<std::byte> oversized(
      static_cast<std::size_t>(omega::retail::kFntV3MaximumInputBytes) + 1U,
      std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(omega::retail::DecodeFntV3(oversized, permissive_limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "caller limits cannot widen the fixed FNT v3 input ceiling");

  if (failures != 0)
    std::cerr << failures << " FNT v3 decoder test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
