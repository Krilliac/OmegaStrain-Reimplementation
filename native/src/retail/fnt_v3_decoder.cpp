#include "omega/retail/fnt_v3_decoder.h"
#include "omega/debug/subsystem_entry_break.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace omega::retail {
namespace {
constexpr std::uint64_t kAtlasOffset = 3;
constexpr std::uint64_t kAtlasTerminatorOffset = 15;
constexpr std::uint64_t kRawByte16Offset = 16;
constexpr std::uint64_t kRawByte17Offset = 17;
constexpr std::uint64_t kGlyphCountOffset = 18;
constexpr std::uint64_t kGlyphsOffset = 19;

struct FntV3Layout {
  std::uint8_t glyph_count = 0;
  std::uint64_t space_advance_offset = 0;
};

[[nodiscard]] asset::DecodeError
Error(const asset::DecodeErrorCode code, std::string message,
      const std::optional<std::uint64_t> byte_offset = std::nullopt) {
  return asset::DecodeError{
      .code = code,
      .byte_offset = byte_offset,
      .message = std::move(message),
  };
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
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

[[nodiscard]] std::uint8_t ReadU8(const std::span<const std::byte> bytes,
                                  const std::uint64_t offset) noexcept {
  return std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset)]);
}

[[nodiscard]] std::uint16_t ReadU16(const std::span<const std::byte> bytes,
                                    const std::uint64_t offset) noexcept {
  return ReadU8(bytes, offset) |
         static_cast<std::uint16_t>(
             static_cast<std::uint16_t>(ReadU8(bytes, offset + 1U)) << 8U);
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                    const std::uint64_t offset) noexcept {
  return ReadU8(bytes, offset) |
         (static_cast<std::uint32_t>(ReadU8(bytes, offset + 1U)) << 8U) |
         (static_cast<std::uint32_t>(ReadU8(bytes, offset + 2U)) << 16U) |
         (static_cast<std::uint32_t>(ReadU8(bytes, offset + 3U)) << 24U);
}

[[nodiscard]] float ReadF32(const std::span<const std::byte> bytes,
                            const std::uint64_t offset) noexcept {
  return std::bit_cast<float>(ReadU32(bytes, offset));
}

[[nodiscard]] bool IsPrintableAscii(const std::uint8_t value) noexcept {
  return value >= 0x20U && value <= 0x7EU;
}

[[nodiscard]] bool IsSafeAtlasBaseByte(const std::uint8_t value) noexcept {
  return (value >= 'A' && value <= 'Z') || (value >= 'a' && value <= 'z') ||
         (value >= '0' && value <= '9') || value == '_' || value == '-';
}

[[nodiscard]] std::int8_t ReadS8(const std::span<const std::byte> bytes,
                                 const std::uint64_t offset) noexcept {
  const std::uint8_t value = ReadU8(bytes, offset);
  const std::int16_t widened = value <= 0x7FU
                                   ? static_cast<std::int16_t>(value)
                                   : static_cast<std::int16_t>(value) - 0x100;
  return static_cast<std::int8_t>(widened);
}

[[nodiscard]] asset::DecodeResult<FntV3Layout>
Preflight(const std::span<const std::byte> bytes,
          const asset::DecodeLimits limits) {
  if (bytes.size() > kFntV3MaximumInputBytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "FNT v3 input exceeds the fixed decoder limit"));
  if (bytes.size() > limits.maximum_input_bytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "FNT v3 input exceeds the caller decoder limit"));
  if (bytes.size() < 2U)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT v3 version is truncated", bytes.size()));
  if (ReadU16(bytes, 0) != kFntV3Version)
    return std::unexpected(
        Error(asset::DecodeErrorCode::UnsupportedVariant,
              "FNT version is not the supported version-3 family", 0));
  if (bytes.size() < kAtlasOffset)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT v3 name span is truncated",
                                 bytes.size()));
  if (ReadU8(bytes, 2) != kFntV3NameSpan)
    return std::unexpected(Error(asset::DecodeErrorCode::UnsupportedVariant,
                                 "FNT v3 name span is not supported", 2));
  if (bytes.size() <= kAtlasTerminatorOffset)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT v3 atlas reference is truncated",
                                 bytes.size()));
  if (kFntV3AtlasReferenceBytes > limits.maximum_string_bytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "FNT v3 atlas reference exceeds the caller string limit",
              kAtlasOffset));

  for (std::uint64_t index = 0; index < kFntV3AtlasReferenceBytes; ++index) {
    const std::uint8_t value = ReadU8(bytes, kAtlasOffset + index);
    if (!IsPrintableAscii(value))
      return std::unexpected(
          Error(asset::DecodeErrorCode::Malformed,
                "FNT v3 atlas reference is not printable ASCII",
                kAtlasOffset + index));
    if (index < kFntV3AtlasReferenceBytes - 4U && !IsSafeAtlasBaseByte(value))
      return std::unexpected(
          Error(asset::DecodeErrorCode::InvalidReference,
                "FNT v3 atlas reference is not a safe member name",
                kAtlasOffset + index));
  }
  constexpr char suffix[] = ".TDX";
  for (std::uint64_t index = 0; index < 4U; ++index) {
    if (ReadU8(bytes, kAtlasOffset + kFntV3AtlasReferenceBytes - 4U + index) !=
        static_cast<std::uint8_t>(suffix[index]))
      return std::unexpected(
          Error(asset::DecodeErrorCode::InvalidReference,
                "FNT v3 atlas reference does not end in .TDX",
                kAtlasOffset + kFntV3AtlasReferenceBytes - 4U + index));
  }
  if (ReadU8(bytes, kAtlasTerminatorOffset) != 0)
    return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                 "FNT v3 atlas reference is not NUL terminated",
                                 kAtlasTerminatorOffset));
  if (bytes.size() <= kGlyphCountOffset)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT v3 glyph count is truncated",
                                 bytes.size()));

  const std::uint8_t glyph_count = ReadU8(bytes, kGlyphCountOffset);
  if (glyph_count > kFntV3MaximumGlyphCount)
    return std::unexpected(Error(
        asset::DecodeErrorCode::LimitExceeded,
        "FNT v3 glyph count exceeds the supported limit", kGlyphCountOffset));

  std::uint64_t decoded_items = 0;
  if (!Add(1, glyph_count, decoded_items))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "FNT v3 decoded item count overflows",
                                 kGlyphCountOffset));
  if (decoded_items > limits.maximum_items)
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "FNT v3 exceeds the caller item limit",
                                 kGlyphCountOffset));

  std::uint64_t glyph_bytes = 0;
  std::uint64_t space_advance_offset = 0;
  std::uint64_t structural_end = 0;
  if (!Multiply(glyph_count, kFntV3GlyphRecordBytes, glyph_bytes) ||
      !Add(kGlyphsOffset, glyph_bytes, space_advance_offset) ||
      !Add(space_advance_offset, 2, structural_end))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "FNT v3 structural extent overflows",
                                 kGlyphCountOffset));
  if (structural_end > bytes.size())
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "FNT v3 glyph or metric data is truncated",
                                 bytes.size()));

  std::uint64_t glyph_output_bytes = 0;
  std::uint64_t logical_output_bytes =
      sizeof(FntV3IR) + kFntV3AtlasReferenceBytes;
  if (!Multiply(glyph_count, sizeof(FntV3GlyphIR), glyph_output_bytes) ||
      !Add(logical_output_bytes, glyph_output_bytes, logical_output_bytes))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "FNT v3 decoded output size overflows",
                                 kGlyphCountOffset));
  if (logical_output_bytes > kFntV3MaximumLogicalOutputBytes)
    return std::unexpected(Error(
        asset::DecodeErrorCode::LimitExceeded,
        "FNT v3 decoded output exceeds the fixed limit", kGlyphCountOffset));
  if (logical_output_bytes > limits.maximum_output_bytes)
    return std::unexpected(Error(
        asset::DecodeErrorCode::LimitExceeded,
        "FNT v3 decoded output exceeds the caller limit", kGlyphCountOffset));

  for (std::uint64_t glyph_index = 0; glyph_index < glyph_count;
       ++glyph_index) {
    const std::uint64_t offset =
        kGlyphsOffset + glyph_index * kFntV3GlyphRecordBytes;
    const float u_left = ReadF32(bytes, offset);
    const float u_right = ReadF32(bytes, offset + 4U);
    const float v_top = ReadF32(bytes, offset + 8U);
    const float v_bottom = ReadF32(bytes, offset + 12U);
    if (!std::isfinite(u_left) || !std::isfinite(u_right) ||
        !std::isfinite(v_top) || !std::isfinite(v_bottom))
      return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                   "FNT v3 glyph UV contains a nonfinite value",
                                   offset));
    if (u_left < 0.0F || u_left > 1.0F || u_right < 0.0F || u_right > 1.0F ||
        v_top < 0.0F || v_top > 1.0F || v_bottom < 0.0F || v_bottom > 1.0F)
      return std::unexpected(
          Error(asset::DecodeErrorCode::Malformed,
                "FNT v3 glyph UV is outside the normalized range", offset));
    if (u_left > u_right || v_top > v_bottom)
      return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                   "FNT v3 glyph UV bounds are reversed",
                                   offset));
  }

  const std::uint64_t pair_table_presence_offset = space_advance_offset + 1U;
  if (ReadU8(bytes, pair_table_presence_offset) != 0)
    return std::unexpected(
        Error(asset::DecodeErrorCode::UnsupportedVariant,
              "FNT v3 optional pair-adjustment table grammar is not supported",
              pair_table_presence_offset));

  const std::uint64_t tail_bytes = bytes.size() - structural_end;
  if (tail_bytes > kFntV3MaximumZeroTailBytes)
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "FNT v3 zero tail exceeds the fixed limit",
                                 structural_end + kFntV3MaximumZeroTailBytes));
  for (std::uint64_t index = 0; index < tail_bytes; ++index) {
    if (ReadU8(bytes, structural_end + index) != 0)
      return std::unexpected(
          Error(asset::DecodeErrorCode::Malformed,
                "FNT v3 trailing region contains a nonzero byte",
                structural_end + index));
  }

  return FntV3Layout{
      .glyph_count = glyph_count,
      .space_advance_offset = space_advance_offset,
  };
}
} // namespace

const FntV3GlyphIR *
FntV3IR::GlyphForCodepoint(const std::uint32_t codepoint) const noexcept {
  if (codepoint < 0x21U)
    return nullptr;
  const std::uint64_t index = static_cast<std::uint64_t>(codepoint) - 0x21U;
  return index < glyphs.size() ? &glyphs[static_cast<std::size_t>(index)]
                               : nullptr;
}

std::optional<float>
FntV3IR::AdvanceForCodepoint(const std::uint32_t codepoint,
                             const float atlas_width) const noexcept {
  if (codepoint == 0x20U)
    return static_cast<float>(space_advance);
  if (!std::isfinite(atlas_width) || atlas_width < 0.0F)
    return std::nullopt;
  const FntV3GlyphIR *glyph = GlyphForCodepoint(codepoint);
  if (glyph == nullptr)
    return std::nullopt;
  const float width = atlas_width * (glyph->u_right - glyph->u_left);
  return std::isfinite(width) ? std::optional<float>{width} : std::nullopt;
}

float FntV3IR::ApplyObservedByte17VerticalPlacement(
    const float source_y) const noexcept {
  return source_y - static_cast<float>(raw_byte_17);
}

asset::DecodeResult<FntV3IR> DecodeFntV3(const std::span<const std::byte> bytes,
                                         const asset::DecodeLimits limits) {
  OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_retail_formats");
  auto layout = Preflight(bytes, limits);
  if (!layout)
    return std::unexpected(layout.error());

  try {
    FntV3IR result{
        .atlas_reference = std::string(
            reinterpret_cast<const char *>(
                bytes.data() + static_cast<std::size_t>(kAtlasOffset)),
            static_cast<std::size_t>(kFntV3AtlasReferenceBytes)),
        .raw_byte_16 = ReadU8(bytes, kRawByte16Offset),
        .raw_byte_17 = ReadU8(bytes, kRawByte17Offset),
        .glyphs = std::vector<FntV3GlyphIR>(layout->glyph_count),
        .space_advance = ReadS8(bytes, layout->space_advance_offset),
    };
    for (std::uint64_t glyph_index = 0; glyph_index < layout->glyph_count;
         ++glyph_index) {
      const std::uint64_t offset =
          kGlyphsOffset + glyph_index * kFntV3GlyphRecordBytes;
      result.glyphs[static_cast<std::size_t>(glyph_index)] = FntV3GlyphIR{
          .u_left = ReadF32(bytes, offset),
          .u_right = ReadF32(bytes, offset + 4U),
          .v_top = ReadF32(bytes, offset + 8U),
          .v_bottom = ReadF32(bytes, offset + 12U),
      };
    }
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded, "FNT v3 allocation"));
  } catch (const std::length_error &) {
    return std::unexpected(
        Error(asset::DecodeErrorCode::Overflow, "FNT v3 allocation length"));
  }
}
} // namespace omega::retail
