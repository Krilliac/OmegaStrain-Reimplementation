#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::compat {

enum class Ps2MemoryCardReadErrorCode : std::uint8_t {
  InvalidSelectionName,
  ImageRejected,
  InvalidAllocationTable,
  InvalidClusterChain,
  ClusterChainLoop,
  SharedCluster,
  InvalidDirectory,
  InvalidEntryName,
  UnsupportedEntryType,
  LimitExceeded,
  SaveDirectoryNotFound,
  AmbiguousSaveDirectory,
  AllocationFailed,
};

struct Ps2MemoryCardReadError {
  Ps2MemoryCardReadErrorCode code{Ps2MemoryCardReadErrorCode::ImageRejected};
  // Offsets are reported in the canonical 8 MiB logical image address space.
  std::size_t logical_offset{};
};

struct Ps2MemoryCardTimestamp {
  std::array<std::byte, 8U> bytes{};

  bool operator==(const Ps2MemoryCardTimestamp &) const = default;
};

struct Ps2MemoryCardOpaqueFile {
  std::string name;
  std::uint16_t mode{};
  std::uint32_t attributes{};
  Ps2MemoryCardTimestamp created;
  Ps2MemoryCardTimestamp modified;
  std::vector<std::byte> bytes;
};

struct Ps2MemoryCardSaveDirectory {
  std::string name;
  std::uint16_t mode{};
  std::uint32_t attributes{};
  Ps2MemoryCardTimestamp created;
  Ps2MemoryCardTimestamp modified;
  // Live regular files remain in their original directory-entry order.
  std::vector<Ps2MemoryCardOpaqueFile> files;
};

struct Ps2MemoryCardReadLimits {
  std::size_t maximum_root_entries{1024U};
  std::size_t maximum_save_entries{1024U};
  std::size_t maximum_files{256U};
  std::size_t maximum_file_bytes{8'388'608U};
  std::size_t maximum_total_file_bytes{8'388'608U};
};

// Reads one explicitly named top-level directory from a standard 8 MiB PS2
// card. This is a structural compatibility reader: file payloads are returned
// unchanged and are never interpreted as an Omega Strain save format.
// Immediate regular files are supported; nested live directories fail closed.
// Shared-cluster detection covers the traversed root chain, selected directory
// chain, and selected live-file chains; unselected directory subtrees are not
// walked or claimed by this bounded operation.
[[nodiscard]] std::expected<Ps2MemoryCardSaveDirectory, Ps2MemoryCardReadError>
ReadPs2MemoryCardSaveDirectory(std::span<const std::byte> image,
                               std::string_view top_level_name,
                               const Ps2MemoryCardReadLimits &limits = {});

} // namespace omega::compat
