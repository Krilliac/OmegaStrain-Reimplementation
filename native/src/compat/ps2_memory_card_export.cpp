#include "omega/compat/ps2_memory_card_export.h"

#include "omega/compat/ps2_memory_card_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::compat {
namespace {
constexpr std::size_t kClusterBytes = 1024U;
constexpr std::size_t kDirectoryEntryBytes = 512U;
constexpr std::size_t kEntriesPerCluster = 2U;
constexpr std::uint32_t kClustersPerCard = 8192U;
constexpr std::uint32_t kAllocationOffset = 41U;
constexpr std::uint32_t kAllocationEnd = 8135U;
constexpr std::uint32_t kMaximumDataClusters = 8000U;
constexpr std::uint32_t kIfcCluster = 8U;
constexpr std::uint32_t kFirstFatCluster = 9U;
constexpr std::uint32_t kFatClusterCount = 32U;
constexpr std::uint32_t kFatEntriesPerCluster = 256U;
constexpr std::uint32_t kEndOfChain = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kAllocatedBit = 0x80000000U;
constexpr std::uint32_t kFreeCluster = 0x7FFFFFFFU;
constexpr std::uint16_t kModeFile = 0x0010U;
constexpr std::uint16_t kModeDirectory = 0x0020U;
constexpr std::uint16_t kModeExists = 0x8000U;
constexpr std::uint16_t kCanonicalDirectoryMode = 0x8427U;

[[nodiscard]] Ps2MemoryCardWriteError
Error(const Ps2MemoryCardWriteErrorCode code,
      const std::size_t file_index = 0U) {
  return Ps2MemoryCardWriteError{code, file_index};
}

[[nodiscard]] bool IsLegalNameByte(const std::uint8_t value) {
  return value >= 0x20U && value != 0x7FU && value != '?' && value != '*' &&
         value != '/';
}

[[nodiscard]] bool IsLegalName(const std::string_view name) {
  if (name.empty() || name.size() >= 32U || name == "." || name == "..")
    return false;
  return std::all_of(name.begin(), name.end(), [](const char value) {
    return IsLegalNameByte(static_cast<std::uint8_t>(value));
  });
}

[[nodiscard]] bool HasType(const std::uint16_t mode,
                           const std::uint16_t expected_type) {
  if ((mode & kModeExists) == 0U)
    return false;
  const auto type =
      static_cast<std::uint16_t>(mode & (kModeFile | kModeDirectory));
  return type == expected_type;
}

void WriteU16(std::span<std::byte> bytes, const std::size_t offset,
              const std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void WriteU32(std::span<std::byte> bytes, const std::size_t offset,
              const std::uint32_t value) {
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    bytes[offset + index] = static_cast<std::byte>(
        (value >> static_cast<unsigned>(index * 8U)) & 0xFFU);
  }
}

void WriteText(std::span<std::byte> bytes, const std::size_t offset,
               const std::string_view text) {
  for (std::size_t index = 0U; index < text.size(); ++index)
    bytes[offset + index] = static_cast<std::byte>(text[index]);
}

[[nodiscard]] std::size_t
AbsoluteClusterOffset(const std::uint32_t absolute_cluster) {
  return static_cast<std::size_t>(absolute_cluster) * kClusterBytes;
}

[[nodiscard]] std::size_t
RelativeClusterOffset(const std::uint32_t relative_cluster) {
  return AbsoluteClusterOffset(kAllocationOffset + relative_cluster);
}

[[nodiscard]] constexpr std::size_t CeilingDivide(const std::size_t value,
                                                  const std::size_t divisor) {
  return value / divisor + static_cast<std::size_t>(value % divisor != 0U);
}

void WriteDirectoryEntry(std::span<std::byte> image, const std::size_t offset,
                         const std::uint16_t mode, const std::uint32_t length,
                         const std::uint32_t first_cluster,
                         const std::uint32_t directory_entry,
                         const std::uint32_t attributes,
                         const Ps2MemoryCardTimestamp &created,
                         const Ps2MemoryCardTimestamp &modified,
                         const std::string_view name) {
  auto entry = image.subspan(offset, kDirectoryEntryBytes);
  std::fill(entry.begin(), entry.end(), std::byte{0});
  WriteU16(entry, 0U, mode);
  WriteU32(entry, 4U, length);
  std::copy(created.bytes.begin(), created.bytes.end(), entry.begin() + 0x08);
  WriteU32(entry, 0x10U, first_cluster);
  WriteU32(entry, 0x14U, directory_entry);
  std::copy(modified.bytes.begin(), modified.bytes.end(), entry.begin() + 0x18);
  WriteU32(entry, 0x20U, attributes);
  WriteText(entry, 0x40U, name);
}

void WriteSuperblock(std::span<std::byte> image) {
  auto page = image.first(kPs2MemoryCardLogicalPageBytes);
  std::fill(page.begin(), page.end(), std::byte{0});
  WriteText(page, 0U, "Sony PS2 Memory Card Format ");
  WriteText(page, 0x1CU, "1.2.0.0");
  WriteU16(page, 0x28U, 512U);
  WriteU16(page, 0x2AU, 2U);
  WriteU16(page, 0x2CU, 16U);
  WriteU16(page, 0x2EU, 0xFF00U);
  WriteU32(page, 0x30U, kClustersPerCard);
  WriteU32(page, 0x34U, kAllocationOffset);
  WriteU32(page, 0x38U, kAllocationEnd);
  WriteU32(page, 0x3CU, 0U);
  WriteU32(page, 0x40U, 1023U);
  WriteU32(page, 0x44U, 1022U);
  for (std::size_t index = 0U; index < 32U; ++index)
    WriteU32(page, 0x50U + index * 4U, kEndOfChain);
  WriteU32(page, 0x50U, kIfcCluster);
  for (std::size_t index = 0U; index < 32U; ++index)
    WriteU32(page, 0xD0U + index * 4U, kEndOfChain);
  page[0x150U] = std::byte{2};
  page[0x151U] = std::byte{0x52};
}

void WriteAllocationTables(std::span<std::byte> image) {
  auto ifc = image.subspan(AbsoluteClusterOffset(kIfcCluster), kClusterBytes);
  std::fill(ifc.begin(), ifc.end(), std::byte{0xFF});
  for (std::uint32_t index = 0U; index < kFatClusterCount; ++index)
    WriteU32(ifc, static_cast<std::size_t>(index) * 4U,
             kFirstFatCluster + index);

  for (std::uint32_t cluster = 0U; cluster < kFatClusterCount; ++cluster) {
    auto fat = image.subspan(AbsoluteClusterOffset(kFirstFatCluster + cluster),
                             kClusterBytes);
    std::fill(fat.begin(), fat.end(), std::byte{0xFF});
  }
  for (std::uint32_t index = 0U; index < kAllocationEnd; ++index) {
    const auto fat_cluster = index / kFatEntriesPerCluster;
    const auto fat_index = index % kFatEntriesPerCluster;
    auto fat = image.subspan(
        AbsoluteClusterOffset(kFirstFatCluster + fat_cluster), kClusterBytes);
    WriteU32(fat, static_cast<std::size_t>(fat_index) * 4U, kFreeCluster);
  }
}

void SetFat(std::span<std::byte> image, const std::uint32_t relative_cluster,
            const std::uint32_t value) {
  const auto fat_cluster = relative_cluster / kFatEntriesPerCluster;
  const auto fat_index = relative_cluster % kFatEntriesPerCluster;
  auto fat = image.subspan(
      AbsoluteClusterOffset(kFirstFatCluster + fat_cluster), kClusterBytes);
  WriteU32(fat, static_cast<std::size_t>(fat_index) * 4U, value);
}

void LinkChain(std::span<std::byte> image,
               const std::span<const std::uint32_t> chain) {
  for (std::size_t index = 0U; index < chain.size(); ++index) {
    const auto next = index + 1U == chain.size()
                          ? kEndOfChain
                          : kAllocatedBit | chain[index + 1U];
    SetFat(image, chain[index], next);
  }
}

[[nodiscard]] std::vector<std::uint32_t>
AllocateChain(std::uint32_t &next_cluster, const std::size_t cluster_count) {
  std::vector<std::uint32_t> chain;
  chain.reserve(cluster_count);
  for (std::size_t index = 0U; index < cluster_count; ++index) {
    chain.push_back(next_cluster);
    ++next_cluster;
  }
  return chain;
}

[[nodiscard]] std::expected<std::size_t, Ps2MemoryCardWriteError>
ValidateInput(const Ps2MemoryCardSaveDirectory &directory,
              const Ps2MemoryCardWriteLimits &limits) {
  if (!IsLegalName(directory.name)) {
    return std::unexpected(
        Error(Ps2MemoryCardWriteErrorCode::InvalidDirectoryName));
  }
  if (!HasType(directory.mode, kModeDirectory))
    return std::unexpected(Error(Ps2MemoryCardWriteErrorCode::InvalidMode));
  if (directory.files.size() > limits.maximum_files) {
    return std::unexpected(Error(Ps2MemoryCardWriteErrorCode::LimitExceeded,
                                 directory.files.size()));
  }
  constexpr auto kMaximumDirectoryFiles =
      static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) - 2U;
  if (directory.files.size() > kMaximumDirectoryFiles) {
    return std::unexpected(Error(Ps2MemoryCardWriteErrorCode::LimitExceeded,
                                 directory.files.size()));
  }

  std::size_t total_bytes = 0U;
  const auto save_entry_count = directory.files.size() + 2U;
  const auto save_cluster_count =
      CeilingDivide(save_entry_count, kEntriesPerCluster);
  std::size_t total_clusters = 2U + save_cluster_count;
  if (total_clusters > kMaximumDataClusters) {
    return std::unexpected(
        Error(Ps2MemoryCardWriteErrorCode::CardCapacityExceeded));
  }
  for (std::size_t index = 0U; index < directory.files.size(); ++index) {
    const auto &file = directory.files[index];
    if (!IsLegalName(file.name)) {
      return std::unexpected(
          Error(Ps2MemoryCardWriteErrorCode::InvalidFileName, index));
    }
    if (!HasType(file.mode, kModeFile)) {
      return std::unexpected(
          Error(Ps2MemoryCardWriteErrorCode::InvalidMode, index));
    }
    if (std::any_of(directory.files.begin(),
                    directory.files.begin() +
                        static_cast<std::ptrdiff_t>(index),
                    [&file](const Ps2MemoryCardOpaqueFile &earlier) {
                      return earlier.name == file.name;
                    })) {
      return std::unexpected(
          Error(Ps2MemoryCardWriteErrorCode::DuplicateFileName, index));
    }
    if (file.bytes.size() > limits.maximum_file_bytes ||
        file.bytes.size() > std::numeric_limits<std::uint32_t>::max() ||
        file.bytes.size() > limits.maximum_total_file_bytes - total_bytes) {
      return std::unexpected(
          Error(Ps2MemoryCardWriteErrorCode::LimitExceeded, index));
    }
    total_bytes += file.bytes.size();
    const auto file_clusters = CeilingDivide(file.bytes.size(), kClusterBytes);
    if (file_clusters > kMaximumDataClusters - total_clusters) {
      return std::unexpected(
          Error(Ps2MemoryCardWriteErrorCode::CardCapacityExceeded, index));
    }
    total_clusters += file_clusters;
  }
  return total_clusters;
}
} // namespace

std::expected<std::vector<std::byte>, Ps2MemoryCardWriteError>
CreatePs2MemoryCardLogicalImage(const Ps2MemoryCardSaveDirectory &directory,
                                const Ps2MemoryCardWriteLimits &limits) {
  const auto required_clusters = ValidateInput(directory, limits);
  if (!required_clusters)
    return std::unexpected(required_clusters.error());

  std::vector<std::byte> image(kPs2MemoryCardLogicalImageBytes,
                               std::byte{0xFF});
  auto image_span = std::span<std::byte>(image);
  WriteSuperblock(image_span);
  WriteAllocationTables(image_span);

  std::uint32_t next_cluster = 0U;
  auto root_chain = AllocateChain(next_cluster, 2U);
  const auto save_entry_count = directory.files.size() + 2U;
  const auto save_cluster_count =
      CeilingDivide(save_entry_count, kEntriesPerCluster);
  auto save_chain = AllocateChain(next_cluster, save_cluster_count);
  std::vector<std::vector<std::uint32_t>> file_chains;
  file_chains.reserve(directory.files.size());
  for (const auto &file : directory.files) {
    const auto cluster_count = CeilingDivide(file.bytes.size(), kClusterBytes);
    file_chains.push_back(AllocateChain(next_cluster, cluster_count));
  }
  if (next_cluster != *required_clusters) {
    return std::unexpected(Error(Ps2MemoryCardWriteErrorCode::EncodingFailed));
  }

  LinkChain(image_span, root_chain);
  LinkChain(image_span, save_chain);
  for (const auto &chain : file_chains)
    LinkChain(image_span, chain);

  for (const auto cluster : root_chain) {
    auto bytes =
        image_span.subspan(RelativeClusterOffset(cluster), kClusterBytes);
    std::fill(bytes.begin(), bytes.end(), std::byte{0});
  }
  for (const auto cluster : save_chain) {
    auto bytes =
        image_span.subspan(RelativeClusterOffset(cluster), kClusterBytes);
    std::fill(bytes.begin(), bytes.end(), std::byte{0});
  }

  const auto empty_timestamp = Ps2MemoryCardTimestamp{};
  const auto root_offset = RelativeClusterOffset(root_chain.front());
  WriteDirectoryEntry(image_span, root_offset, kCanonicalDirectoryMode, 3U, 0U,
                      0U, 0U, empty_timestamp, empty_timestamp, ".");
  WriteDirectoryEntry(image_span, root_offset + kDirectoryEntryBytes,
                      kCanonicalDirectoryMode, 0U, 0U, 0U, 0U, empty_timestamp,
                      empty_timestamp, "..");
  const auto root_save_offset = RelativeClusterOffset(root_chain[1]);
  WriteDirectoryEntry(image_span, root_save_offset, directory.mode,
                      static_cast<std::uint32_t>(save_entry_count),
                      save_chain.front(), 0U, directory.attributes,
                      directory.created, directory.modified, directory.name);

  const auto save_offset = RelativeClusterOffset(save_chain.front());
  WriteDirectoryEntry(image_span, save_offset, kCanonicalDirectoryMode, 0U, 0U,
                      2U, 0U, empty_timestamp, empty_timestamp, ".");
  WriteDirectoryEntry(image_span, save_offset + kDirectoryEntryBytes,
                      kCanonicalDirectoryMode, 0U, 0U, 0U, 0U, empty_timestamp,
                      empty_timestamp, "..");

  for (std::size_t index = 0U; index < directory.files.size(); ++index) {
    const auto entry_index = index + 2U;
    const auto directory_cluster = save_chain[entry_index / kEntriesPerCluster];
    const auto entry_offset =
        RelativeClusterOffset(directory_cluster) +
        (entry_index % kEntriesPerCluster) * kDirectoryEntryBytes;
    const auto first_cluster =
        file_chains[index].empty() ? kEndOfChain : file_chains[index].front();
    const auto &file = directory.files[index];
    WriteDirectoryEntry(image_span, entry_offset, file.mode,
                        static_cast<std::uint32_t>(file.bytes.size()),
                        first_cluster, 0U, file.attributes, file.created,
                        file.modified, file.name);

    std::size_t source_offset = 0U;
    for (const auto cluster : file_chains[index]) {
      const auto to_copy =
          std::min(kClusterBytes, file.bytes.size() - source_offset);
      std::copy_n(
          file.bytes.begin() + static_cast<std::ptrdiff_t>(source_offset),
          to_copy,
          image.begin() +
              static_cast<std::ptrdiff_t>(RelativeClusterOffset(cluster)));
      source_offset += to_copy;
    }
    if (source_offset != file.bytes.size()) {
      return std::unexpected(
          Error(Ps2MemoryCardWriteErrorCode::EncodingFailed, index));
    }
  }
  return image;
}

std::expected<std::vector<std::byte>, Ps2MemoryCardWriteError>
CreatePs2MemoryCardRawImage(const Ps2MemoryCardSaveDirectory &directory,
                            const Ps2MemoryCardWriteLimits &limits) {
  auto logical = CreatePs2MemoryCardLogicalImage(directory, limits);
  if (!logical)
    return std::unexpected(logical.error());
  auto raw = ConvertPs2MemoryCardImageToRaw(*logical);
  if (!raw) {
    return std::unexpected(Error(Ps2MemoryCardWriteErrorCode::EncodingFailed));
  }
  return std::move(*raw);
}

} // namespace omega::compat
