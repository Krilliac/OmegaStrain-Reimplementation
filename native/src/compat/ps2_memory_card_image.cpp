#include "omega/compat/ps2_memory_card_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <string_view>

namespace omega::compat {
namespace {
using namespace std::string_view_literals;

constexpr auto kSuperblockMagic = "Sony PS2 Memory Card Format "sv;
constexpr auto kSupportedSuperblockVersion = "1.2.0.0"sv;

constexpr std::size_t kVersionOffset = 0x1CU;
constexpr std::size_t kVersionBytes = 12U;
constexpr std::size_t kPageLengthOffset = 0x28U;
constexpr std::size_t kPagesPerClusterOffset = 0x2AU;
constexpr std::size_t kPagesPerBlockOffset = 0x2CU;
constexpr std::size_t kClustersPerCardOffset = 0x30U;
constexpr std::size_t kAllocationOffsetOffset = 0x34U;
constexpr std::size_t kAllocationEndOffset = 0x38U;
constexpr std::size_t kRootDirectoryClusterOffset = 0x3CU;
constexpr std::size_t kBackupBlock1Offset = 0x40U;
constexpr std::size_t kBackupBlock2Offset = 0x44U;
constexpr std::size_t kIfcListOffset = 0x50U;
constexpr std::size_t kIfcListEntries = 32U;
constexpr std::size_t kBadBlockListOffset = 0xD0U;
constexpr std::size_t kBadBlockListEntries = 32U;
constexpr std::size_t kCardTypeOffset = 0x150U;

constexpr std::uint16_t kStandardPagesPerCluster = 2U;
constexpr std::uint16_t kStandardPagesPerEraseBlock = 16U;
constexpr std::uint32_t kStandardClustersPerCard = 8192U;
constexpr std::uint32_t kFatEntriesPerCluster = 256U;
constexpr std::uint8_t kPs2CardType = 2U;
constexpr std::uint32_t kUnusedCluster =
    std::numeric_limits<std::uint32_t>::max();

[[nodiscard]] constexpr std::uint8_t ByteValue(const std::byte value) {
  return std::to_integer<std::uint8_t>(value);
}

[[nodiscard]] std::uint16_t ReadU16(const std::span<const std::byte> bytes,
                                    const std::size_t offset) {
  return static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(ByteValue(bytes[offset])) |
      static_cast<std::uint16_t>(
          static_cast<std::uint16_t>(ByteValue(bytes[offset + 1U])) << 8U));
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t index = 0; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(ByteValue(bytes[offset + index]))
             << static_cast<unsigned>(index * 8U);
  }
  return value;
}

[[nodiscard]] bool EqualsText(const std::span<const std::byte> bytes,
                              const std::size_t offset,
                              const std::string_view expected) {
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (ByteValue(bytes[offset + index]) !=
        static_cast<std::uint8_t>(expected[index])) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Ps2MemoryCardImageError
Error(const Ps2MemoryCardImageErrorCode code, const std::size_t offset) {
  return Ps2MemoryCardImageError{code, offset};
}

[[nodiscard]] bool HasOddParity(std::uint8_t value) {
  bool odd = false;
  while (value != 0U) {
    odd = !odd;
    value = static_cast<std::uint8_t>(value & (value - 1U));
  }
  return odd;
}

// The public PS2 card ECC is a 20-bit Hamming code over 128 data bytes. This
// derives the column mask instead of carrying a borrowed lookup table: each set
// data bit contributes its bit index and complementary index to the two parity
// nibbles.
[[nodiscard]] std::uint8_t ColumnParityMask(const std::uint8_t value) {
  std::uint8_t mask = 0U;
  for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
    if ((value & static_cast<std::uint8_t>(1U << bit)) == 0U)
      continue;
    const auto contribution =
        static_cast<std::uint8_t>(static_cast<std::uint8_t>(bit << 4U) |
                                  static_cast<std::uint8_t>(7U - bit));
    mask = static_cast<std::uint8_t>(mask ^ contribution);
  }
  return mask;
}

[[nodiscard]] std::array<std::byte, 3U>
CalculatePageQuarterEcc(const std::span<const std::byte, 128U> bytes) {
  std::uint8_t column_parity = 0x77U;
  std::uint8_t even_line_parity = 0x7FU;
  std::uint8_t odd_line_parity = 0x7FU;

  for (std::size_t index = 0; index < bytes.size(); ++index) {
    const auto value = ByteValue(bytes[index]);
    column_parity =
        static_cast<std::uint8_t>(column_parity ^ ColumnParityMask(value));
    if (!HasOddParity(value))
      continue;
    const auto line = static_cast<std::uint8_t>(index);
    even_line_parity = static_cast<std::uint8_t>(
        even_line_parity ^ static_cast<std::uint8_t>(~line));
    odd_line_parity = static_cast<std::uint8_t>(odd_line_parity ^ line);
  }

  return {static_cast<std::byte>(column_parity),
          static_cast<std::byte>(even_line_parity),
          static_cast<std::byte>(odd_line_parity)};
}

[[nodiscard]] std::expected<Ps2MemoryCardImageLayout, Ps2MemoryCardImageError>
ClassifyLayout(const std::size_t image_bytes) {
  if (image_bytes == kPs2MemoryCardLogicalImageBytes)
    return Ps2MemoryCardImageLayout::Logical512;
  if (image_bytes == kPs2MemoryCardRawImageBytes)
    return Ps2MemoryCardImageLayout::Raw528;
  return std::unexpected(
      Error(Ps2MemoryCardImageErrorCode::UnsupportedImageSize, image_bytes));
}
} // namespace

std::expected<Ps2MemoryCardImageDescriptor, Ps2MemoryCardImageError>
InspectPs2MemoryCardImage(const std::span<const std::byte> image) {
  const auto layout = ClassifyLayout(image.size());
  if (!layout)
    return std::unexpected(layout.error());

  // In either supported representation the first 512 bytes are the logical
  // superblock page. Raw spare bytes follow it rather than being interleaved.
  const auto superblock = image.first(kPs2MemoryCardLogicalPageBytes);
  if (!EqualsText(superblock, 0U, kSuperblockMagic)) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::InvalidSuperblockMagic, 0U));
  }
  if (!EqualsText(superblock, kVersionOffset, kSupportedSuperblockVersion)) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::UnsupportedSuperblockVersion,
              kVersionOffset));
  }
  for (std::size_t index = kSupportedSuperblockVersion.size();
       index < kVersionBytes; ++index) {
    if (superblock[kVersionOffset + index] != std::byte{0}) {
      return std::unexpected(
          Error(Ps2MemoryCardImageErrorCode::UnsupportedSuperblockVersion,
                kVersionOffset + index));
    }
  }

  const auto logical_page_bytes = ReadU16(superblock, kPageLengthOffset);
  const auto pages_per_cluster = ReadU16(superblock, kPagesPerClusterOffset);
  const auto pages_per_block = ReadU16(superblock, kPagesPerBlockOffset);
  const auto clusters_per_card = ReadU32(superblock, kClustersPerCardOffset);
  if (logical_page_bytes != kPs2MemoryCardLogicalPageBytes ||
      pages_per_cluster != kStandardPagesPerCluster ||
      pages_per_block != kStandardPagesPerEraseBlock ||
      clusters_per_card != kStandardClustersPerCard) {
    return std::unexpected(Error(
        Ps2MemoryCardImageErrorCode::UnsupportedGeometry, kPageLengthOffset));
  }

  const auto geometry_bytes = static_cast<std::uint64_t>(logical_page_bytes) *
                              pages_per_cluster * clusters_per_card;
  if (geometry_bytes != kPs2MemoryCardLogicalImageBytes ||
      pages_per_block % pages_per_cluster != 0U) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
              kClustersPerCardOffset));
  }

  const auto allocation_offset = ReadU32(superblock, kAllocationOffsetOffset);
  const auto allocation_end = ReadU32(superblock, kAllocationEndOffset);
  const auto root_directory_cluster =
      ReadU32(superblock, kRootDirectoryClusterOffset);
  const auto backup_block_1 = ReadU32(superblock, kBackupBlock1Offset);
  const auto backup_block_2 = ReadU32(superblock, kBackupBlock2Offset);
  const auto clusters_per_block =
      static_cast<std::uint32_t>(pages_per_block / pages_per_cluster);
  const auto block_count = clusters_per_card / clusters_per_block;
  const auto absolute_allocation_end =
      static_cast<std::uint64_t>(allocation_offset) + allocation_end;

  if (allocation_offset == 0U || allocation_offset >= clusters_per_card ||
      allocation_end == 0U || absolute_allocation_end > clusters_per_card ||
      root_directory_cluster != 0U ||
      root_directory_cluster >= allocation_end) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
              kAllocationOffsetOffset));
  }
  if (backup_block_1 >= block_count || backup_block_2 >= block_count ||
      backup_block_1 == backup_block_2) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
              kBackupBlock1Offset));
  }

  const auto backup_1_begin =
      static_cast<std::uint64_t>(backup_block_1) * clusters_per_block;
  const auto backup_2_begin =
      static_cast<std::uint64_t>(backup_block_2) * clusters_per_block;
  if (backup_1_begin < absolute_allocation_end ||
      backup_2_begin < absolute_allocation_end) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
              kBackupBlock1Offset));
  }

  const auto fat_cluster_count =
      (allocation_end + kFatEntriesPerCluster - 1U) / kFatEntriesPerCluster;
  const auto ifc_cluster_count = static_cast<std::size_t>(
      (fat_cluster_count + kFatEntriesPerCluster - 1U) / kFatEntriesPerCluster);
  if (ifc_cluster_count == 0U || ifc_cluster_count > kIfcListEntries) {
    return std::unexpected(Error(
        Ps2MemoryCardImageErrorCode::InvalidGeometryBounds, kIfcListOffset));
  }
  for (std::size_t index = 0; index < kIfcListEntries; ++index) {
    const auto pointer = ReadU32(superblock, kIfcListOffset + index * 4U);
    if (index < ifc_cluster_count) {
      if (pointer < clusters_per_block || pointer == kUnusedCluster ||
          pointer >= allocation_offset) {
        return std::unexpected(
            Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
                  kIfcListOffset + index * 4U));
      }
    } else if (pointer != 0U && pointer != kUnusedCluster) {
      return std::unexpected(
          Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
                kIfcListOffset + index * 4U));
    }
  }

  bool reached_bad_block_sentinel = false;
  std::array<std::uint32_t, kBadBlockListEntries> bad_blocks{};
  std::size_t bad_block_count = 0U;
  for (std::size_t index = 0; index < kBadBlockListEntries; ++index) {
    const auto block = ReadU32(superblock, kBadBlockListOffset + index * 4U);
    if (block == kUnusedCluster) {
      reached_bad_block_sentinel = true;
      continue;
    }
    if (reached_bad_block_sentinel || block >= block_count ||
        block == backup_block_1 || block == backup_block_2 ||
        std::find(bad_blocks.begin(),
                  bad_blocks.begin() +
                      static_cast<std::ptrdiff_t>(bad_block_count),
                  block) !=
            bad_blocks.begin() + static_cast<std::ptrdiff_t>(bad_block_count)) {
      return std::unexpected(
          Error(Ps2MemoryCardImageErrorCode::InvalidGeometryBounds,
                kBadBlockListOffset + index * 4U));
    }
    bad_blocks[bad_block_count] = block;
    ++bad_block_count;
  }

  if (ByteValue(superblock[kCardTypeOffset]) != kPs2CardType) {
    return std::unexpected(Error(
        Ps2MemoryCardImageErrorCode::UnsupportedGeometry, kCardTypeOffset));
  }

  return Ps2MemoryCardImageDescriptor{
      .layout = *layout,
      .stored_page_bytes = static_cast<std::uint16_t>(
          *layout == Ps2MemoryCardImageLayout::Logical512
              ? kPs2MemoryCardLogicalPageBytes
              : kPs2MemoryCardRawPageBytes),
      .logical_page_bytes = logical_page_bytes,
      .pages_per_cluster = pages_per_cluster,
      .pages_per_erase_block = pages_per_block,
      .page_count = static_cast<std::uint32_t>(kPs2MemoryCardPageCount),
      .clusters_per_card = clusters_per_card,
      .allocation_offset = allocation_offset,
      .allocation_end = allocation_end,
      .root_directory_cluster = root_directory_cluster,
      .backup_block_1 = backup_block_1,
      .backup_block_2 = backup_block_2,
  };
}

std::expected<std::vector<std::byte>, Ps2MemoryCardImageError>
ConvertPs2MemoryCardImageToLogical(const std::span<const std::byte> image) {
  try {
    const auto descriptor = InspectPs2MemoryCardImage(image);
    if (!descriptor)
      return std::unexpected(descriptor.error());

    if (descriptor->layout == Ps2MemoryCardImageLayout::Logical512)
      return std::vector<std::byte>(image.begin(), image.end());

    std::vector<std::byte> logical(kPs2MemoryCardLogicalImageBytes);
    for (std::size_t page = 0; page < kPs2MemoryCardPageCount; ++page) {
      const auto source_offset = page * kPs2MemoryCardRawPageBytes;
      const auto target_offset = page * kPs2MemoryCardLogicalPageBytes;
      std::copy_n(image.begin() + static_cast<std::ptrdiff_t>(source_offset),
                  kPs2MemoryCardLogicalPageBytes,
                  logical.begin() + static_cast<std::ptrdiff_t>(target_offset));
    }
    return logical;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::AllocationFailed, 0U));
  }
}

std::expected<std::vector<std::byte>, Ps2MemoryCardImageError>
ConvertPs2MemoryCardImageToRaw(const std::span<const std::byte> image) {
  try {
    auto logical = ConvertPs2MemoryCardImageToLogical(image);
    if (!logical)
      return std::unexpected(logical.error());

    std::vector<std::byte> raw(kPs2MemoryCardRawImageBytes, std::byte{0});
    for (std::size_t page = 0; page < kPs2MemoryCardPageCount; ++page) {
      const auto source_offset = page * kPs2MemoryCardLogicalPageBytes;
      const auto target_offset = page * kPs2MemoryCardRawPageBytes;
      std::copy_n(logical->begin() + static_cast<std::ptrdiff_t>(source_offset),
                  kPs2MemoryCardLogicalPageBytes,
                  raw.begin() + static_cast<std::ptrdiff_t>(target_offset));

      for (std::size_t quarter = 0; quarter < 4U; ++quarter) {
        const auto quarter_offset = source_offset + quarter * 128U;
        const auto quarter_bytes = std::span<const std::byte, 128U>(
            logical->data() + quarter_offset, 128U);
        const auto ecc = CalculatePageQuarterEcc(quarter_bytes);
        const auto ecc_offset =
            target_offset + kPs2MemoryCardLogicalPageBytes + quarter * 3U;
        std::copy(ecc.begin(), ecc.end(),
                  raw.begin() + static_cast<std::ptrdiff_t>(ecc_offset));
      }
    }
    return raw;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        Error(Ps2MemoryCardImageErrorCode::AllocationFailed, 0U));
  }
}

} // namespace omega::compat
