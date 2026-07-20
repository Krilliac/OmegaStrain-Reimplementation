#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <vector>

namespace omega::compat {

inline constexpr std::size_t kPs2MemoryCardLogicalPageBytes = 512U;
inline constexpr std::size_t kPs2MemoryCardSpareBytes = 16U;
inline constexpr std::size_t kPs2MemoryCardRawPageBytes =
    kPs2MemoryCardLogicalPageBytes + kPs2MemoryCardSpareBytes;
inline constexpr std::size_t kPs2MemoryCardPageCount = 16'384U;
inline constexpr std::size_t kPs2MemoryCardLogicalImageBytes =
    kPs2MemoryCardLogicalPageBytes * kPs2MemoryCardPageCount;
inline constexpr std::size_t kPs2MemoryCardRawImageBytes =
    kPs2MemoryCardRawPageBytes * kPs2MemoryCardPageCount;

enum class Ps2MemoryCardImageLayout : std::uint8_t {
  Logical512,
  Raw528,
};

enum class Ps2MemoryCardImageErrorCode : std::uint8_t {
  UnsupportedImageSize,
  InvalidSuperblockMagic,
  UnsupportedSuperblockVersion,
  UnsupportedGeometry,
  InvalidGeometryBounds,
  AllocationFailed,
};

struct Ps2MemoryCardImageError {
  Ps2MemoryCardImageErrorCode code{
      Ps2MemoryCardImageErrorCode::UnsupportedImageSize};
  std::size_t offset{};
};

struct Ps2MemoryCardImageDescriptor {
  Ps2MemoryCardImageLayout layout{Ps2MemoryCardImageLayout::Logical512};
  std::uint16_t stored_page_bytes{};
  std::uint16_t logical_page_bytes{};
  std::uint16_t pages_per_cluster{};
  std::uint16_t pages_per_erase_block{};
  std::uint32_t page_count{};
  std::uint32_t clusters_per_card{};
  std::uint32_t allocation_offset{};
  std::uint32_t allocation_end{};
  std::uint32_t root_directory_cluster{};
  std::uint32_t backup_block_1{};
  std::uint32_t backup_block_2{};
};

// These functions inspect only the standard PS2 card container/filesystem
// envelope. Game-specific save payloads remain opaque, and no emulator state is
// represented or executed.
[[nodiscard]] std::expected<Ps2MemoryCardImageDescriptor,
                            Ps2MemoryCardImageError>
InspectPs2MemoryCardImage(std::span<const std::byte> image);

// Raw input spare bytes are discarded, matching the documented PCSX2 raw to
// no-ECC conversion boundary. The superblock and fixed 8 MiB geometry are still
// validated before any output is published.
[[nodiscard]] std::expected<std::vector<std::byte>, Ps2MemoryCardImageError>
ConvertPs2MemoryCardImageToLogical(std::span<const std::byte> image);

// Every output page receives four freshly calculated three-byte ECC values and
// four zero spare bytes. Passing an already-raw image therefore canonicalizes
// its spare area instead of retaining emulator-selected physical bytes.
[[nodiscard]] std::expected<std::vector<std::byte>, Ps2MemoryCardImageError>
ConvertPs2MemoryCardImageToRaw(std::span<const std::byte> image);

} // namespace omega::compat
