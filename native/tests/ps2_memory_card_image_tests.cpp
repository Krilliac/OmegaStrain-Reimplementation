#include "omega/compat/ps2_memory_card_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {
using omega::compat::Ps2MemoryCardImageError;
using omega::compat::Ps2MemoryCardImageErrorCode;
using omega::compat::Ps2MemoryCardImageLayout;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <class T>
void CheckError(const std::expected<T, Ps2MemoryCardImageError> &result,
                const Ps2MemoryCardImageErrorCode code,
                const std::string_view message) {
  Check(!result && result.error().code == code, message);
}

void WriteU16(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    bytes[offset + index] = static_cast<std::byte>(
        (value >> static_cast<unsigned>(index * 8U)) & 0xFFU);
  }
}

void WriteText(std::vector<std::byte> &bytes, const std::size_t offset,
               const std::string_view text) {
  for (std::size_t index = 0; index < text.size(); ++index)
    bytes[offset + index] = static_cast<std::byte>(text[index]);
}

[[nodiscard]] std::vector<std::byte> MakeLogicalCard() {
  std::vector<std::byte> card(omega::compat::kPs2MemoryCardLogicalImageBytes,
                              std::byte{0xFF});
  WriteText(card, 0U, "Sony PS2 Memory Card Format ");
  for (std::size_t offset = 0x1CU; offset < 0x28U; ++offset)
    card[offset] = std::byte{0};
  WriteText(card, 0x1CU, "1.2.0.0");
  WriteU16(card, 0x28U, 512U);
  WriteU16(card, 0x2AU, 2U);
  WriteU16(card, 0x2CU, 16U);
  WriteU16(card, 0x2EU, 0xFF00U);
  WriteU32(card, 0x30U, 8192U);
  WriteU32(card, 0x34U, 41U);
  WriteU32(card, 0x38U, 8135U);
  WriteU32(card, 0x3CU, 0U);
  WriteU32(card, 0x40U, 1023U);
  WriteU32(card, 0x44U, 1022U);
  for (std::size_t offset = 0x48U; offset < 0x50U; ++offset)
    card[offset] = std::byte{0};
  WriteU32(card, 0x50U, 8U);
  card[0x150U] = std::byte{2};
  card[0x151U] = std::byte{0x52};
  return card;
}

void WriteSyntheticEccPage(std::vector<std::byte> &logical) {
  constexpr std::size_t page_offset =
      omega::compat::kPs2MemoryCardLogicalPageBytes;
  for (std::size_t index = 0;
       index < omega::compat::kPs2MemoryCardLogicalPageBytes; ++index) {
    logical[page_offset + index] = std::byte{0};
  }
  logical[page_offset] = std::byte{1};
  logical[page_offset + 128U + 127U] = std::byte{0x80};
  for (std::size_t index = 0; index < 128U; ++index) {
    logical[page_offset + 256U + index] =
        static_cast<std::byte>((index * 37U + 11U) & 0xFFU);
    logical[page_offset + 384U + index] = static_cast<std::byte>(index);
  }
  logical[page_offset + 384U + 3U] ^= std::byte{1};
}

void CheckRecognitionAndBounds() {
  auto logical = MakeLogicalCard();
  const auto inspected = omega::compat::InspectPs2MemoryCardImage(logical);
  Check(inspected.has_value(),
        "the synthetic standard 8 MiB logical card is recognized");
  if (inspected) {
    Check(inspected->layout == Ps2MemoryCardImageLayout::Logical512 &&
              inspected->stored_page_bytes == 512U &&
              inspected->page_count == 16384U &&
              inspected->clusters_per_card == 8192U,
          "logical recognition publishes the fixed standard geometry");
    Check(inspected->allocation_offset == 41U &&
              inspected->allocation_end == 8135U &&
              inspected->backup_block_1 == 1023U &&
              inspected->backup_block_2 == 1022U,
          "bounded superblock locations are retained in the descriptor");
  }

  for (const std::size_t invalid_size : std::array<std::size_t, 5U>{
           0U, omega::compat::kPs2MemoryCardLogicalImageBytes - 1U,
           omega::compat::kPs2MemoryCardLogicalImageBytes + 1U,
           omega::compat::kPs2MemoryCardRawImageBytes - 1U,
           omega::compat::kPs2MemoryCardRawImageBytes + 1U}) {
    std::vector<std::byte> invalid(invalid_size);
    CheckError(omega::compat::InspectPs2MemoryCardImage(invalid),
               Ps2MemoryCardImageErrorCode::UnsupportedImageSize,
               "all noncanonical image lengths fail closed");
  }

  auto malformed = logical;
  malformed[0] = std::byte{'X'};
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidSuperblockMagic,
             "an invalid superblock magic fails closed");
  malformed = logical;
  malformed[0x1EU] = std::byte{'3'};
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedSuperblockVersion,
             "an unimplemented superblock version remains unsupported");
  malformed = logical;
  malformed[0x24U] = std::byte{1};
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedSuperblockVersion,
             "nonzero version padding is rejected");

  malformed = logical;
  WriteU16(malformed, 0x28U, 1024U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedGeometry,
             "a nonstandard logical page size is rejected");
  malformed = logical;
  WriteU16(malformed, 0x2AU, 1U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedGeometry,
             "a nonstandard cluster geometry is rejected");
  malformed = logical;
  WriteU16(malformed, 0x2CU, 8U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedGeometry,
             "a nonstandard erase-block geometry is rejected");
  malformed = logical;
  WriteU32(malformed, 0x30U, 8191U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedGeometry,
             "a nonstandard card cluster count is rejected");

  malformed = logical;
  WriteU32(malformed, 0x34U, 8192U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
             "an allocation offset outside the card is rejected");
  malformed = logical;
  WriteU32(malformed, 0x38U, 8192U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
             "an allocation extent outside the card is rejected");
  malformed = logical;
  WriteU32(malformed, 0x3CU, 1U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
             "a nonzero root-directory cluster is rejected");
  malformed = logical;
  WriteU32(malformed, 0x40U, 1024U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
             "an out-of-card backup block is rejected");
  malformed = logical;
  WriteU32(malformed, 0x50U, 41U);
  CheckError(
      omega::compat::InspectPs2MemoryCardImage(malformed),
      Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
      "an indirect-FAT cluster inside the allocation region is rejected");
  malformed = logical;
  WriteU32(malformed, 0x54U, 9U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
             "an unexpected extra indirect-FAT pointer is rejected");
  malformed = logical;
  WriteU32(malformed, 0xD0U, 1024U);
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
             "an out-of-card bad-block entry is rejected");
  malformed = logical;
  malformed[0x150U] = std::byte{1};
  CheckError(omega::compat::InspectPs2MemoryCardImage(malformed),
             Ps2MemoryCardImageErrorCode::UnsupportedGeometry,
             "a non-PS2 card type is rejected");
}

void CheckCanonicalConversion() {
  auto logical = MakeLogicalCard();
  WriteSyntheticEccPage(logical);

  const auto raw = omega::compat::ConvertPs2MemoryCardImageToRaw(logical);
  Check(raw && raw->size() == omega::compat::kPs2MemoryCardRawImageBytes,
        "logical conversion emits exactly 16,384 raw 528-byte pages");
  if (!raw)
    return;

  const auto inspected = omega::compat::InspectPs2MemoryCardImage(*raw);
  Check(inspected && inspected->layout == Ps2MemoryCardImageLayout::Raw528 &&
            inspected->stored_page_bytes == 528U,
        "the canonical raw result is recognized as the raw layout");

  constexpr std::size_t spare_offset =
      omega::compat::kPs2MemoryCardRawPageBytes +
      omega::compat::kPs2MemoryCardLogicalPageBytes;
  constexpr std::array<std::byte, 16U> expected_spare{
      std::byte{0x70}, std::byte{0x80}, std::byte{0x7F}, std::byte{0x07},
      std::byte{0xFF}, std::byte{0x00}, std::byte{0x07}, std::byte{0xDE},
      std::byte{0x21}, std::byte{0x70}, std::byte{0x83}, std::byte{0x7C},
      std::byte{0x00}, std::byte{0x00}, std::byte{0x00}, std::byte{0x00}};
  Check(std::equal(expected_spare.begin(), expected_spare.end(),
                   raw->begin() + static_cast<std::ptrdiff_t>(spare_offset)),
        "raw conversion matches independent ECC vectors and zeroes spare tail");

  const auto decoded = omega::compat::ConvertPs2MemoryCardImageToLogical(*raw);
  Check(decoded && *decoded == logical,
        "canonical raw-to-logical conversion preserves every logical byte");

  auto noncanonical_raw = *raw;
  for (std::size_t index = 0; index < 16U; ++index)
    noncanonical_raw[spare_offset + index] = std::byte{0xA5};
  const auto decoded_noncanonical =
      omega::compat::ConvertPs2MemoryCardImageToLogical(noncanonical_raw);
  Check(decoded_noncanonical && *decoded_noncanonical == logical,
        "raw spare bytes are discarded rather than mistaken for save payload");
  const auto recanonicalized =
      omega::compat::ConvertPs2MemoryCardImageToRaw(noncanonical_raw);
  Check(recanonicalized && *recanonicalized == *raw,
        "raw input is re-encoded with canonical ECC and spare bytes");
}
} // namespace

int main() {
  static_assert(omega::compat::kPs2MemoryCardLogicalImageBytes == 8'388'608U);
  static_assert(omega::compat::kPs2MemoryCardRawImageBytes == 8'650'752U);
  CheckRecognitionAndBounds();
  CheckCanonicalConversion();
  return failures == 0 ? 0 : 1;
}
