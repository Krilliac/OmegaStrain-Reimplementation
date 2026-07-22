#include "omega/retail/retail_string_table_decoder.h"
#include "omega/debug/subsystem_entry_break.h"

#include <algorithm>
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
constexpr std::uint64_t kCountBytes = 4;
constexpr std::uint64_t kValueSpanBytes = 4;

struct EntryLayout {
  std::uint64_t ordinal = 0;
  std::uint64_t key_offset = 0;
  std::uint64_t key_length = 0;
  std::uint64_t value_offset = 0;
  std::uint64_t value_length = 0;
};

struct TableLayout {
  std::vector<EntryLayout> entries;
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

[[nodiscard]] bool Align4(const std::uint64_t value,
                          std::uint64_t &result) noexcept {
  std::uint64_t with_padding = 0;
  if (!Add(value, 3, with_padding))
    return false;
  result = with_padding & ~std::uint64_t{3};
  return true;
}

[[nodiscard]] std::uint8_t ReadU8(const std::span<const std::byte> bytes,
                                  const std::uint64_t offset) noexcept {
  return std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(offset)]);
}

[[nodiscard]] std::uint32_t ReadU32(const std::span<const std::byte> bytes,
                                    const std::uint64_t offset) noexcept {
  return ReadU8(bytes, offset) |
         (static_cast<std::uint32_t>(ReadU8(bytes, offset + 1U)) << 8U) |
         (static_cast<std::uint32_t>(ReadU8(bytes, offset + 2U)) << 16U) |
         (static_cast<std::uint32_t>(ReadU8(bytes, offset + 3U)) << 24U);
}

[[nodiscard]] constexpr char LowerAscii(const char value) noexcept {
  return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A'))
                                      : value;
}

[[nodiscard]] bool IsPrintableAscii(const std::uint8_t value) noexcept {
  return value >= 0x20U && value <= 0x7EU;
}

[[nodiscard]] int CompareNormalizedKeys(const std::span<const std::byte> bytes,
                                        const EntryLayout &left,
                                        const EntryLayout &right) noexcept {
  const std::uint64_t shared = std::min(left.key_length, right.key_length);
  for (std::uint64_t index = 0; index < shared; ++index) {
    const char left_value =
        LowerAscii(static_cast<char>(ReadU8(bytes, left.key_offset + index)));
    const char right_value =
        LowerAscii(static_cast<char>(ReadU8(bytes, right.key_offset + index)));
    if (left_value < right_value)
      return -1;
    if (left_value > right_value)
      return 1;
  }
  if (left.key_length < right.key_length)
    return -1;
  if (left.key_length > right.key_length)
    return 1;
  return 0;
}

[[nodiscard]] asset::DecodeResult<TableLayout>
Preflight(const std::span<const std::byte> bytes,
          const asset::DecodeLimits limits) {
  if (bytes.size() > kRetailStringTableMaximumInputBytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "retail string table exceeds the fixed input limit"));
  if (bytes.size() > limits.maximum_input_bytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "retail string table exceeds the caller input limit"));
  if (bytes.size() < kCountBytes)
    return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                 "retail string table count is truncated",
                                 bytes.size()));

  const std::uint64_t entry_count = ReadU32(bytes, 0);
  if (entry_count > kRetailStringTableMaximumEntryCount)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "retail string table entry count exceeds the fixed limit", 0));

  std::uint64_t decoded_items = 0;
  if (!Add(1, entry_count, decoded_items))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "retail string table item count overflows",
                                 0));
  if (decoded_items > limits.maximum_items)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "retail string table exceeds the caller item limit", 0));

  std::uint64_t entry_object_bytes = 0;
  std::uint64_t logical_output_bytes = sizeof(RetailStringTableIR);
  if (!Multiply(entry_count, sizeof(RetailStringEntryIR), entry_object_bytes) ||
      !Add(logical_output_bytes, entry_object_bytes, logical_output_bytes))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "retail string table output size overflows",
                                 0));
  if (logical_output_bytes > limits.maximum_output_bytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "retail string table exceeds the caller output limit", 0));

  std::uint64_t scratch_bytes = 0;
  if (!Multiply(entry_count, sizeof(EntryLayout), scratch_bytes))
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "retail string table scratch size overflows",
                                 0));
  if (scratch_bytes > limits.maximum_scratch_bytes)
    return std::unexpected(
        Error(asset::DecodeErrorCode::LimitExceeded,
              "retail string table exceeds the caller scratch limit", 0));

  try {
    TableLayout layout;
    layout.entries.reserve(static_cast<std::size_t>(entry_count));
    std::uint64_t cursor = kCountBytes;
    for (std::uint64_t ordinal = 0; ordinal < entry_count; ++ordinal) {
      if (!Align4(cursor, cursor))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow,
                  "retail string table cursor alignment overflows"));

      std::uint64_t entry_header_end = 0;
      if (!Add(cursor, kRetailStringTableKeyStorageBytes + kValueSpanBytes,
               entry_header_end))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow,
                  "retail string table entry header overflows", cursor));
      if (entry_header_end > bytes.size())
        return std::unexpected(Error(
            asset::DecodeErrorCode::Truncated,
            "retail string table entry header is truncated", bytes.size()));

      const std::uint64_t key_offset = cursor;
      std::optional<std::uint64_t> key_length;
      for (std::uint64_t index = 0; index < kRetailStringTableKeyStorageBytes;
           ++index) {
        const std::uint8_t value = ReadU8(bytes, key_offset + index);
        if (value == 0) {
          key_length = index;
          break;
        }
        if (!IsPrintableAscii(value))
          return std::unexpected(
              Error(asset::DecodeErrorCode::Malformed,
                    "retail string table key is not printable ASCII",
                    key_offset + index));
      }
      if (!key_length)
        return std::unexpected(
            Error(asset::DecodeErrorCode::Malformed,
                  "retail string table key is not NUL terminated", key_offset));
      if (*key_length == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "retail string table key is empty",
                                     key_offset));
      for (std::uint64_t index = *key_length + 1U;
           index < kRetailStringTableKeyStorageBytes; ++index) {
        if (ReadU8(bytes, key_offset + index) != 0)
          return std::unexpected(
              Error(asset::DecodeErrorCode::Malformed,
                    "retail string table fixed key padding is not zero",
                    key_offset + index));
      }
      if (*key_length > limits.maximum_string_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "retail string table key exceeds the caller string limit",
                  key_offset));

      const std::uint64_t value_span_offset =
          key_offset + kRetailStringTableKeyStorageBytes;
      const std::uint64_t value_span = ReadU32(bytes, value_span_offset);
      const std::uint64_t value_offset = value_span_offset + kValueSpanBytes;
      if (value_span == 0)
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                     "retail string table value span is empty",
                                     value_span_offset));

      std::uint64_t value_end = 0;
      if (!Add(value_offset, value_span, value_end))
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow,
            "retail string table value extent overflows", value_span_offset));
      if (value_end > bytes.size())
        return std::unexpected(Error(asset::DecodeErrorCode::Truncated,
                                     "retail string table value is truncated",
                                     bytes.size()));

      const std::uint64_t value_length = value_span - 1U;
      for (std::uint64_t index = 0; index < value_length; ++index) {
        if (ReadU8(bytes, value_offset + index) == 0)
          return std::unexpected(
              Error(asset::DecodeErrorCode::Malformed,
                    "retail string table value contains an early NUL",
                    value_offset + index));
      }
      if (ReadU8(bytes, value_end - 1U) != 0)
        return std::unexpected(Error(
            asset::DecodeErrorCode::Malformed,
            "retail string table value is not NUL terminated", value_end - 1U));
      if (value_length > limits.maximum_string_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "retail string table value exceeds the caller string limit",
                  value_span_offset));

      std::uint64_t next_output_bytes = 0;
      if (!Add(logical_output_bytes, *key_length, next_output_bytes) ||
          !Add(next_output_bytes, value_length, logical_output_bytes))
        return std::unexpected(Error(
            asset::DecodeErrorCode::Overflow,
            "retail string table output size overflows", value_span_offset));
      if (logical_output_bytes > kRetailStringTableMaximumLogicalOutputBytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "retail string table exceeds the fixed output limit",
                  value_span_offset));
      if (logical_output_bytes > limits.maximum_output_bytes)
        return std::unexpected(
            Error(asset::DecodeErrorCode::LimitExceeded,
                  "retail string table exceeds the caller output limit",
                  value_span_offset));

      std::uint64_t padded_value_span = 0;
      if (!Align4(value_span, padded_value_span) ||
          !Add(value_offset, padded_value_span, cursor))
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow,
                  "retail string table aligned value extent overflows",
                  value_span_offset));
      if (cursor > bytes.size())
        return std::unexpected(Error(
            asset::DecodeErrorCode::Truncated,
            "retail string table value alignment is truncated", bytes.size()));

      layout.entries.push_back(EntryLayout{
          .ordinal = ordinal,
          .key_offset = key_offset,
          .key_length = *key_length,
          .value_offset = value_offset,
          .value_length = value_length,
      });
    }

    if (cursor != bytes.size())
      return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                                   "retail string table has trailing bytes",
                                   cursor));

    std::ranges::sort(layout.entries, [&bytes](const EntryLayout &left,
                                               const EntryLayout &right) {
      const int comparison = CompareNormalizedKeys(bytes, left, right);
      return comparison != 0 ? comparison < 0 : left.ordinal < right.ordinal;
    });
    for (std::size_t index = 1; index < layout.entries.size(); ++index) {
      const EntryLayout &previous = layout.entries[index - 1U];
      const EntryLayout &current = layout.entries[index];
      if (CompareNormalizedKeys(bytes, previous, current) == 0) {
        const EntryLayout &duplicate =
            previous.ordinal > current.ordinal ? previous : current;
        return std::unexpected(
            Error(asset::DecodeErrorCode::DuplicateReference,
                  "retail string table has a duplicate normalized key",
                  duplicate.key_offset));
      }
    }
    return layout;
  } catch (const std::bad_alloc &) {
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "retail string table allocation"));
  } catch (const std::length_error &) {
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "retail string table allocation length"));
  }
}
} // namespace

const RetailStringEntryIR *
RetailStringTableIR::Find(const std::string_view key) const noexcept {
  for (const RetailStringEntryIR &entry : entries) {
    if (entry.key.size() != key.size())
      continue;
    bool equal = true;
    for (std::size_t index = 0; index < key.size(); ++index) {
      if (entry.key[index] != LowerAscii(key[index])) {
        equal = false;
        break;
      }
    }
    if (equal)
      return &entry;
  }
  return nullptr;
}

asset::DecodeResult<RetailStringTableIR>
ParseRetailStringTable(const std::span<const std::byte> bytes,
                        const asset::DecodeLimits limits) {
  OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_retail_formats");
  auto layout = Preflight(bytes, limits);
  if (!layout)
    return std::unexpected(layout.error());

  try {
    RetailStringTableIR result{
        .entries = std::vector<RetailStringEntryIR>(layout->entries.size()),
    };
    for (const EntryLayout &entry_layout : layout->entries) {
      RetailStringEntryIR &entry =
          result.entries[static_cast<std::size_t>(entry_layout.ordinal)];
      entry.key.resize(static_cast<std::size_t>(entry_layout.key_length));
      for (std::size_t index = 0; index < entry.key.size(); ++index) {
        entry.key[index] = LowerAscii(static_cast<char>(
            ReadU8(bytes, entry_layout.key_offset +
                              static_cast<std::uint64_t>(index))));
      }
      const char *value_begin = reinterpret_cast<const char *>(
          bytes.data() + static_cast<std::size_t>(entry_layout.value_offset));
      entry.value.assign(value_begin,
                         static_cast<std::size_t>(entry_layout.value_length));
    }
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
                                 "retail string table allocation"));
  } catch (const std::length_error &) {
    return std::unexpected(Error(asset::DecodeErrorCode::Overflow,
                                 "retail string table allocation length"));
  }
}
} // namespace omega::retail
