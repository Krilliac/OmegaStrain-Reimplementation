#pragma once

#include "omega/asset/decode.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::retail {
inline constexpr std::uint64_t kRetailStringTableMaximumInputBytes =
    4ULL * 1024ULL * 1024ULL;
inline constexpr std::uint64_t kRetailStringTableMaximumEntryCount = 100'000;
inline constexpr std::uint64_t kRetailStringTableKeyStorageBytes = 16;

// Owned source-order localization entry. Keys are normalized with the retail
// ASCII A-Z to a-z rule. Values retain their encoded bytes without transcoding;
// the on-disk terminal NUL and alignment bytes are not part of either owned
// string.
struct RetailStringEntryIR {
  std::string key;
  std::string value;

  bool operator==(const RetailStringEntryIR &) const = default;
};

struct RetailStringTableIR {
  std::vector<RetailStringEntryIR> entries;

  // Performs the same ASCII-only case normalization as the retail lookup. The
  // returned pointer remains valid until this table is mutated or destroyed.
  [[nodiscard]] const RetailStringEntryIR *
  Find(std::string_view key) const noexcept;

  bool operator==(const RetailStringTableIR &) const = default;
};

inline constexpr std::uint64_t kRetailStringTableMaximumDecodedItems =
    1U + kRetailStringTableMaximumEntryCount;
inline constexpr std::uint64_t kRetailStringTableMaximumLogicalOutputBytes =
    sizeof(RetailStringTableIR) +
    kRetailStringTableMaximumEntryCount * sizeof(RetailStringEntryIR) +
    kRetailStringTableMaximumInputBytes;

// [any worker thread; stateless/reentrant] Decodes the independently documented
// COMMON/STRINGS*.DAT table into owned source-order key/value entries. Caller
// limits may tighten but cannot widen the fixed input and entry ceilings.
// Duplicate normalized keys, incomplete NUL termination, malformed fixed-key
// padding, and bytes after the final counted record fail closed.
[[nodiscard]] asset::DecodeResult<RetailStringTableIR>
ParseRetailStringTable(std::span<const std::byte> bytes,
                        asset::DecodeLimits limits = {});
} // namespace omega::retail
