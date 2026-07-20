#include "omega/compat/ps2_memory_card_filesystem.h"

#include "omega/compat/ps2_memory_card_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::compat {
namespace {
constexpr std::size_t kClusterBytes = 1024U;
constexpr std::size_t kDirectoryEntryBytes = 512U;
constexpr std::size_t kEntriesPerCluster = kClusterBytes / kDirectoryEntryBytes;
constexpr std::size_t kFatEntriesPerCluster = 256U;
constexpr std::size_t kIfcEntriesPerCluster = 256U;
constexpr std::size_t kIfcListOffset = 0x50U;
constexpr std::size_t kIfcListEntries = 32U;
constexpr std::uint32_t kClustersPerEraseBlock = 8U;
constexpr std::uint32_t kEndOfChain = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint32_t kAllocatedBit = 0x80000000U;
constexpr std::uint32_t kClusterIndexMask = 0x7FFFFFFFU;
constexpr std::uint16_t kModeFile = 0x0010U;
constexpr std::uint16_t kModeDirectory = 0x0020U;
constexpr std::uint16_t kModeExists = 0x8000U;

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

[[nodiscard]] Ps2MemoryCardReadError
Error(const Ps2MemoryCardReadErrorCode code, const std::size_t offset) {
  return Ps2MemoryCardReadError{code, offset};
}

[[nodiscard]] bool IsLegalNameByte(const std::uint8_t value) {
  return value >= 0x20U && value != 0x7FU && value != '?' && value != '*' &&
         value != '/';
}

[[nodiscard]] bool IsLegalSelectionName(const std::string_view name) {
  if (name.empty() || name.size() >= 32U || name == "." || name == "..")
    return false;
  return std::all_of(name.begin(), name.end(), [](const char value) {
    return IsLegalNameByte(static_cast<std::uint8_t>(value));
  });
}

struct DecodedEntry {
  bool exists{};
  bool is_file{};
  bool is_directory{};
  std::uint16_t mode{};
  std::uint32_t length{};
  std::uint32_t first_cluster{};
  std::uint32_t directory_entry{};
  std::uint32_t attributes{};
  Ps2MemoryCardTimestamp created;
  Ps2MemoryCardTimestamp modified;
  std::string name;
  std::size_t logical_offset{};
};

class CardReader final {
public:
  CardReader(std::vector<std::byte> logical,
             const Ps2MemoryCardImageDescriptor descriptor,
             const Ps2MemoryCardReadLimits &limits)
      : logical_(std::move(logical)), descriptor_(descriptor), limits_(limits),
        claimed_clusters_(descriptor.allocation_end, false) {}

  [[nodiscard]] std::expected<Ps2MemoryCardSaveDirectory,
                              Ps2MemoryCardReadError>
  Read(const std::string_view selected_name) {
    auto allocation_table = ReadAllocationTable();
    if (!allocation_table)
      return std::unexpected(allocation_table.error());
    fat_ = std::move(*allocation_table);

    const auto root_first_cluster = descriptor_.root_directory_cluster;
    const auto root_absolute_cluster =
        static_cast<std::uint64_t>(descriptor_.allocation_offset) +
        root_first_cluster;
    const auto root_cluster = Cluster(root_absolute_cluster);
    if (!root_cluster) {
      return std::unexpected(Error(
          Ps2MemoryCardReadErrorCode::InvalidDirectory,
          static_cast<std::size_t>(root_absolute_cluster * kClusterBytes)));
    }
    const auto root_dot = DecodeEntry(root_cluster->first(kDirectoryEntryBytes),
                                      root_absolute_cluster * kClusterBytes);
    if (!root_dot)
      return std::unexpected(root_dot.error());
    if (!root_dot->exists || !root_dot->is_directory || root_dot->name != "." ||
        root_dot->length < 2U) {
      return std::unexpected(Error(
          Ps2MemoryCardReadErrorCode::InvalidDirectory,
          static_cast<std::size_t>(root_absolute_cluster * kClusterBytes)));
    }

    auto root_entries = ReadDirectory(root_first_cluster, root_dot->length,
                                      limits_.maximum_root_entries);
    if (!root_entries)
      return std::unexpected(root_entries.error());
    if (!HasDotEntries(*root_entries, true)) {
      return std::unexpected(Error(Ps2MemoryCardReadErrorCode::InvalidDirectory,
                                   root_entries->front().logical_offset));
    }

    const DecodedEntry *selected = nullptr;
    std::size_t selected_index = 0U;
    for (std::size_t index = 2U; index < root_entries->size(); ++index) {
      const auto &entry = (*root_entries)[index];
      if (!entry.exists)
        continue;
      if (entry.name == "." || entry.name == "..") {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidDirectory,
                  entry.logical_offset));
      }
      if (entry.name != selected_name)
        continue;
      if (!entry.is_directory) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::UnsupportedEntryType,
                  entry.logical_offset));
      }
      if (selected != nullptr) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::AmbiguousSaveDirectory,
                  entry.logical_offset));
      }
      selected = &entry;
      selected_index = index;
    }
    if (selected == nullptr) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::SaveDirectoryNotFound, 0U));
    }

    auto save_entries = ReadDirectory(selected->first_cluster, selected->length,
                                      limits_.maximum_save_entries);
    if (!save_entries)
      return std::unexpected(save_entries.error());
    if (!HasDotEntries(*save_entries, false, selected_index)) {
      return std::unexpected(Error(Ps2MemoryCardReadErrorCode::InvalidDirectory,
                                   save_entries->front().logical_offset));
    }

    Ps2MemoryCardSaveDirectory result{
        .name = selected->name,
        .mode = selected->mode,
        .attributes = selected->attributes,
        .created = selected->created,
        .modified = selected->modified,
    };
    std::size_t total_file_bytes = 0U;
    for (std::size_t index = 2U; index < save_entries->size(); ++index) {
      const auto &entry = (*save_entries)[index];
      if (!entry.exists)
        continue;
      if (entry.name == "." || entry.name == "..") {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidDirectory,
                  entry.logical_offset));
      }
      if (!entry.is_file) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::UnsupportedEntryType,
                  entry.logical_offset));
      }
      if (result.files.size() >= limits_.maximum_files ||
          entry.length > limits_.maximum_file_bytes ||
          entry.length > limits_.maximum_total_file_bytes -
                             std::min(total_file_bytes,
                                      limits_.maximum_total_file_bytes)) {
        return std::unexpected(Error(Ps2MemoryCardReadErrorCode::LimitExceeded,
                                     entry.logical_offset));
      }

      if (std::any_of(result.files.begin(), result.files.end(),
                      [&entry](const Ps2MemoryCardOpaqueFile &file) {
                        return file.name == entry.name;
                      })) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidDirectory,
                  entry.logical_offset));
      }
      auto payload = ReadFile(entry);
      if (!payload)
        return std::unexpected(payload.error());
      total_file_bytes += payload->size();
      result.files.push_back(Ps2MemoryCardOpaqueFile{
          .name = entry.name,
          .mode = entry.mode,
          .attributes = entry.attributes,
          .created = entry.created,
          .modified = entry.modified,
          .bytes = std::move(*payload),
      });
    }
    return result;
  }

private:
  [[nodiscard]] std::expected<std::span<const std::byte>,
                              Ps2MemoryCardReadError>
  Cluster(const std::uint64_t absolute_cluster) const {
    if (absolute_cluster >= descriptor_.clusters_per_card) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
                static_cast<std::size_t>(std::min<std::uint64_t>(
                    absolute_cluster * kClusterBytes,
                    std::numeric_limits<std::size_t>::max()))));
    }
    const auto offset =
        static_cast<std::size_t>(absolute_cluster) * kClusterBytes;
    return std::span<const std::byte>(logical_).subspan(offset, kClusterBytes);
  }

  [[nodiscard]] std::expected<std::vector<std::uint32_t>,
                              Ps2MemoryCardReadError>
  ReadAllocationTable() const {
    const auto fat_cluster_count =
        (static_cast<std::size_t>(descriptor_.allocation_end) +
         kFatEntriesPerCluster - 1U) /
        kFatEntriesPerCluster;
    const auto ifc_cluster_count =
        (fat_cluster_count + kIfcEntriesPerCluster - 1U) /
        kIfcEntriesPerCluster;
    if (ifc_cluster_count == 0U || ifc_cluster_count > kIfcListEntries) {
      return std::unexpected(Error(
          Ps2MemoryCardReadErrorCode::InvalidAllocationTable, kIfcListOffset));
    }

    std::vector<bool> metadata_clusters(descriptor_.clusters_per_card, false);
    std::vector<std::uint32_t> fat;
    fat.reserve(descriptor_.allocation_end);
    std::size_t fat_clusters_read = 0U;
    for (std::size_t ifc_index = 0U; ifc_index < ifc_cluster_count;
         ++ifc_index) {
      const auto ifc_pointer_offset = kIfcListOffset + ifc_index * 4U;
      const auto ifc_cluster_number =
          ReadU32(std::span<const std::byte>(logical_), ifc_pointer_offset);
      if (ifc_cluster_number < kClustersPerEraseBlock ||
          ifc_cluster_number >= descriptor_.allocation_offset ||
          metadata_clusters[ifc_cluster_number]) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
                  ifc_pointer_offset));
      }
      metadata_clusters[ifc_cluster_number] = true;
      const auto ifc_cluster = Cluster(ifc_cluster_number);
      if (!ifc_cluster)
        return std::unexpected(ifc_cluster.error());

      for (std::size_t entry_index = 0U; entry_index < kIfcEntriesPerCluster;
           ++entry_index) {
        const auto pointer_offset =
            static_cast<std::size_t>(ifc_cluster_number) * kClusterBytes +
            entry_index * 4U;
        const auto fat_cluster_number = ReadU32(*ifc_cluster, entry_index * 4U);
        if (fat_clusters_read >= fat_cluster_count) {
          if (fat_cluster_number != 0U && fat_cluster_number != kEndOfChain) {
            return std::unexpected(
                Error(Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
                      pointer_offset));
          }
          continue;
        }
        if (fat_cluster_number < kClustersPerEraseBlock ||
            fat_cluster_number >= descriptor_.allocation_offset ||
            metadata_clusters[fat_cluster_number]) {
          return std::unexpected(
              Error(Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
                    pointer_offset));
        }
        metadata_clusters[fat_cluster_number] = true;
        const auto fat_cluster = Cluster(fat_cluster_number);
        if (!fat_cluster)
          return std::unexpected(fat_cluster.error());

        for (std::size_t fat_index = 0U;
             fat_index < kFatEntriesPerCluster &&
             fat.size() < descriptor_.allocation_end;
             ++fat_index) {
          fat.push_back(ReadU32(*fat_cluster, fat_index * 4U));
        }
        ++fat_clusters_read;
      }
    }
    if (fat_clusters_read != fat_cluster_count ||
        fat.size() != descriptor_.allocation_end) {
      return std::unexpected(Error(
          Ps2MemoryCardReadErrorCode::InvalidAllocationTable, kIfcListOffset));
    }
    return fat;
  }

  [[nodiscard]] std::expected<std::vector<std::uint32_t>,
                              Ps2MemoryCardReadError>
  ReadExactChain(const std::uint32_t first_cluster,
                 const std::size_t cluster_count,
                 const std::size_t entry_offset) {
    if (cluster_count > descriptor_.allocation_end) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::InvalidClusterChain, entry_offset));
    }
    if (cluster_count == 0U) {
      if (first_cluster == kEndOfChain)
        return std::vector<std::uint32_t>{};
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::InvalidClusterChain, entry_offset));
    }
    if (first_cluster == kEndOfChain)
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::InvalidClusterChain, entry_offset));

    std::vector<bool> local_clusters(descriptor_.allocation_end, false);
    std::vector<std::uint32_t> chain;
    chain.reserve(cluster_count);
    auto current = first_cluster;
    for (std::size_t index = 0U; index < cluster_count; ++index) {
      if (current >= descriptor_.allocation_end) {
        return std::unexpected(Error(
            Ps2MemoryCardReadErrorCode::InvalidClusterChain, entry_offset));
      }
      if (local_clusters[current]) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::ClusterChainLoop, entry_offset));
      }
      if (claimed_clusters_[current]) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::SharedCluster, entry_offset));
      }
      local_clusters[current] = true;
      claimed_clusters_[current] = true;
      chain.push_back(current);

      const auto next = fat_[current];
      if (index + 1U == cluster_count) {
        if (next != kEndOfChain) {
          return std::unexpected(Error(
              Ps2MemoryCardReadErrorCode::InvalidClusterChain, entry_offset));
        }
        continue;
      }
      if (next == kEndOfChain || (next & kAllocatedBit) == 0U) {
        return std::unexpected(Error(
            Ps2MemoryCardReadErrorCode::InvalidClusterChain, entry_offset));
      }
      current = next & kClusterIndexMask;
    }
    return chain;
  }

  [[nodiscard]] std::expected<DecodedEntry, Ps2MemoryCardReadError>
  DecodeEntry(const std::span<const std::byte> bytes,
              const std::uint64_t logical_offset) const {
    DecodedEntry entry{
        .mode = ReadU16(bytes, 0U),
        .length = ReadU32(bytes, 4U),
        .first_cluster = ReadU32(bytes, 0x10U),
        .directory_entry = ReadU32(bytes, 0x14U),
        .attributes = ReadU32(bytes, 0x20U),
        .logical_offset = static_cast<std::size_t>(logical_offset),
    };
    entry.exists = (entry.mode & kModeExists) != 0U;
    if (!entry.exists)
      return entry;

    entry.is_file = (entry.mode & kModeFile) != 0U;
    entry.is_directory = (entry.mode & kModeDirectory) != 0U;
    if (entry.is_file == entry.is_directory) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::UnsupportedEntryType,
                entry.logical_offset));
    }

    std::copy_n(bytes.begin() + 0x08, entry.created.bytes.size(),
                entry.created.bytes.begin());
    std::copy_n(bytes.begin() + 0x18, entry.modified.bytes.size(),
                entry.modified.bytes.begin());

    constexpr std::size_t name_offset = 0x40U;
    constexpr std::size_t name_bytes = 32U;
    std::size_t name_length = name_bytes;
    for (std::size_t index = 0U; index < name_bytes; ++index) {
      const auto value = ByteValue(bytes[name_offset + index]);
      if (value == 0U) {
        name_length = index;
        break;
      }
      if (!IsLegalNameByte(value)) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidEntryName,
                  entry.logical_offset + name_offset + index));
      }
    }
    if (name_length == 0U || name_length == name_bytes) {
      return std::unexpected(Error(Ps2MemoryCardReadErrorCode::InvalidEntryName,
                                   entry.logical_offset + name_offset));
    }
    for (std::size_t index = name_length + 1U; index < name_bytes; ++index) {
      if (bytes[name_offset + index] != std::byte{0}) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidEntryName,
                  entry.logical_offset + name_offset + index));
      }
    }
    entry.name.reserve(name_length);
    for (std::size_t index = 0U; index < name_length; ++index) {
      entry.name.push_back(
          static_cast<char>(ByteValue(bytes[name_offset + index])));
    }
    return entry;
  }

  [[nodiscard]] std::expected<std::vector<DecodedEntry>, Ps2MemoryCardReadError>
  ReadDirectory(const std::uint32_t first_cluster,
                const std::uint32_t entry_count,
                const std::size_t maximum_entries) {
    if (entry_count < 2U || entry_count > maximum_entries ||
        entry_count > static_cast<std::uint64_t>(descriptor_.allocation_end) *
                          kEntriesPerCluster) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::LimitExceeded, 0U));
    }
    const auto cluster_count =
        (static_cast<std::size_t>(entry_count) + kEntriesPerCluster - 1U) /
        kEntriesPerCluster;
    auto chain = ReadExactChain(first_cluster, cluster_count, 0U);
    if (!chain)
      return std::unexpected(chain.error());

    std::vector<DecodedEntry> entries;
    entries.reserve(entry_count);
    for (std::size_t index = 0U; index < entry_count; ++index) {
      const auto relative_cluster = (*chain)[index / kEntriesPerCluster];
      const auto absolute_cluster =
          static_cast<std::uint64_t>(descriptor_.allocation_offset) +
          relative_cluster;
      const auto cluster = Cluster(absolute_cluster);
      if (!cluster)
        return std::unexpected(cluster.error());
      const auto in_cluster_offset =
          (index % kEntriesPerCluster) * kDirectoryEntryBytes;
      const auto logical_offset =
          absolute_cluster * kClusterBytes + in_cluster_offset;
      auto entry =
          DecodeEntry(cluster->subspan(in_cluster_offset, kDirectoryEntryBytes),
                      logical_offset);
      if (!entry)
        return std::unexpected(entry.error());
      entries.push_back(std::move(*entry));
    }
    return entries;
  }

  [[nodiscard]] bool
  HasDotEntries(const std::vector<DecodedEntry> &entries, const bool root,
                const std::size_t expected_parent_entry = 0U) const {
    if (entries.size() < 2U)
      return false;
    const auto &dot = entries[0];
    const auto &dot_dot = entries[1];
    if (!dot.exists || !dot.is_directory || dot.name != "." ||
        !dot_dot.exists || !dot_dot.is_directory || dot_dot.name != "..") {
      return false;
    }
    if (dot_dot.length != 0U || dot_dot.first_cluster != 0U ||
        dot_dot.directory_entry != 0U)
      return false;
    if (root) {
      return dot.length == entries.size() && dot.first_cluster == 0U &&
             dot.directory_entry == 0U;
    }
    return dot.length == 0U && dot.first_cluster == 0U &&
           dot.directory_entry == expected_parent_entry;
  }

  [[nodiscard]] std::expected<std::vector<std::byte>, Ps2MemoryCardReadError>
  ReadFile(const DecodedEntry &entry) {
    if (entry.length == 0U) {
      if (entry.first_cluster != kEndOfChain) {
        return std::unexpected(
            Error(Ps2MemoryCardReadErrorCode::InvalidClusterChain,
                  entry.logical_offset));
      }
      return std::vector<std::byte>{};
    }

    const auto cluster_count =
        (static_cast<std::size_t>(entry.length) + kClusterBytes - 1U) /
        kClusterBytes;
    auto chain = ReadExactChain(entry.first_cluster, cluster_count,
                                entry.logical_offset);
    if (!chain)
      return std::unexpected(chain.error());

    std::vector<std::byte> bytes;
    bytes.reserve(entry.length);
    std::size_t remaining = entry.length;
    for (const auto relative_cluster : *chain) {
      const auto absolute_cluster =
          static_cast<std::uint64_t>(descriptor_.allocation_offset) +
          relative_cluster;
      const auto cluster = Cluster(absolute_cluster);
      if (!cluster)
        return std::unexpected(cluster.error());
      const auto to_copy = std::min(remaining, kClusterBytes);
      bytes.insert(bytes.end(), cluster->begin(),
                   cluster->begin() + static_cast<std::ptrdiff_t>(to_copy));
      remaining -= to_copy;
    }
    if (remaining != 0U) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::InvalidClusterChain,
                entry.logical_offset));
    }
    return bytes;
  }

  std::vector<std::byte> logical_;
  Ps2MemoryCardImageDescriptor descriptor_;
  const Ps2MemoryCardReadLimits &limits_;
  std::vector<std::uint32_t> fat_;
  std::vector<bool> claimed_clusters_;
};
} // namespace

std::expected<Ps2MemoryCardSaveDirectory, Ps2MemoryCardReadError>
ReadPs2MemoryCardSaveDirectory(const std::span<const std::byte> image,
                               const std::string_view top_level_name,
                               const Ps2MemoryCardReadLimits &limits) {
  try {
    if (!IsLegalSelectionName(top_level_name)) {
      return std::unexpected(
          Error(Ps2MemoryCardReadErrorCode::InvalidSelectionName, 0U));
    }

    const auto descriptor = InspectPs2MemoryCardImage(image);
    if (!descriptor) {
      return std::unexpected(Error(Ps2MemoryCardReadErrorCode::ImageRejected,
                                   descriptor.error().offset));
    }
    auto logical = ConvertPs2MemoryCardImageToLogical(image);
    if (!logical) {
      const auto code =
          logical.error().code == Ps2MemoryCardImageErrorCode::AllocationFailed
              ? Ps2MemoryCardReadErrorCode::AllocationFailed
              : Ps2MemoryCardReadErrorCode::ImageRejected;
      return std::unexpected(Error(code, logical.error().offset));
    }

    CardReader reader(std::move(*logical), *descriptor, limits);
    return reader.Read(top_level_name);
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        Error(Ps2MemoryCardReadErrorCode::AllocationFailed, 0U));
  }
}

} // namespace omega::compat
