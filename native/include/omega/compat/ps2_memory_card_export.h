#pragma once

#include "omega/compat/ps2_memory_card_filesystem.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace omega::compat {

enum class Ps2MemoryCardWriteErrorCode : std::uint8_t {
  InvalidDirectoryName,
  InvalidFileName,
  DuplicateFileName,
  InvalidMode,
  LimitExceeded,
  CardCapacityExceeded,
  EncodingFailed,
};

struct Ps2MemoryCardWriteError {
  Ps2MemoryCardWriteErrorCode code{Ps2MemoryCardWriteErrorCode::EncodingFailed};
  std::size_t file_index{};
};

struct Ps2MemoryCardWriteLimits {
  std::size_t maximum_files{256U};
  std::size_t maximum_file_bytes{8'388'608U};
  std::size_t maximum_total_file_bytes{8'388'608U};
};

// Creates a new deterministic standard 8 MiB card. File payloads are copied as
// opaque bytes and are never interpreted as a game-specific save format. There
// is deliberately no API for patching an existing card image.
[[nodiscard]] std::expected<std::vector<std::byte>, Ps2MemoryCardWriteError>
CreatePs2MemoryCardLogicalImage(const Ps2MemoryCardSaveDirectory &directory,
                                const Ps2MemoryCardWriteLimits &limits = {});

// Creates the same new card using the 528-byte raw-page representation and the
// shared canonical ECC/spare encoder.
[[nodiscard]] std::expected<std::vector<std::byte>, Ps2MemoryCardWriteError>
CreatePs2MemoryCardRawImage(const Ps2MemoryCardSaveDirectory &directory,
                            const Ps2MemoryCardWriteLimits &limits = {});

} // namespace omega::compat
