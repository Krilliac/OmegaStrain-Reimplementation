#include "omega/retail/retail_string_table_decoder.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
struct SyntheticEntry {
  std::string key;
  std::string value;
};

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <typename Result>
void CheckError(const Result &result, const omega::asset::DecodeErrorCode code,
                const std::string_view message) {
  if (result) {
    Check(false, message);
    return;
  }
  Check(result.error().code == code, message);
  Check(!result.error().message.empty(),
        "string-table errors own a nonempty diagnostic");
  Check(result.error().message.find('/') == std::string::npos &&
            result.error().message.find('\\') == std::string::npos,
        "string-table errors contain no filesystem path");
}

void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value) {
  bytes.push_back(static_cast<std::byte>(value & 0xFFU));
  bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
  bytes.push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
  bytes.push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
}

void WriteU32(std::vector<std::byte> &bytes, const std::size_t offset,
              const std::uint32_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
  bytes[offset + 2U] = static_cast<std::byte>((value >> 16U) & 0xFFU);
  bytes[offset + 3U] = static_cast<std::byte>((value >> 24U) & 0xFFU);
}

[[nodiscard]] std::vector<std::byte>
MakeTable(const std::span<const SyntheticEntry> entries) {
  std::vector<std::byte> bytes;
  AppendU32(bytes, static_cast<std::uint32_t>(entries.size()));
  for (const SyntheticEntry &entry : entries) {
    while (bytes.size() % 4U != 0)
      bytes.push_back(std::byte{0xC3});
    const std::size_t key_offset = bytes.size();
    bytes.resize(key_offset + omega::retail::kRetailStringTableKeyStorageBytes,
                 std::byte{0});
    for (std::size_t index = 0; index < entry.key.size(); ++index)
      bytes[key_offset + index] =
          static_cast<std::byte>(static_cast<unsigned char>(entry.key[index]));

    AppendU32(bytes, static_cast<std::uint32_t>(entry.value.size() + 1U));
    for (const char value : entry.value)
      bytes.push_back(
          static_cast<std::byte>(static_cast<unsigned char>(value)));
    bytes.push_back(std::byte{0});
    while (bytes.size() % 4U != 0)
      bytes.push_back(std::byte{0xA5});
  }
  return bytes;
}

[[nodiscard]] std::vector<std::byte>
MakeTable(const std::initializer_list<SyntheticEntry> entries) {
  return MakeTable(
      std::span<const SyntheticEntry>(entries.begin(), entries.size()));
}
} // namespace

int main() {
  static_assert(omega::retail::kRetailStringTableKeyStorageBytes == 16U);
  static_assert(omega::retail::kRetailStringTableMaximumDecodedItems ==
                1U + omega::retail::kRetailStringTableMaximumEntryCount);

  std::string encoded_value;
  encoded_value.push_back(static_cast<char>(0xE9));
  encoded_value.push_back('\n');
  const std::vector<SyntheticEntry> specs{
      {.key = "AlphaKey", .value = "First"},
      {.key = "BETA", .value = encoded_value},
  };
  const auto bytes = MakeTable(specs);
  const auto decoded = omega::retail::DecodeRetailStringTable(bytes);
  Check(decoded.has_value(),
        "string table accepts a generated counted fixture");
  if (decoded) {
    Check(decoded->entries.size() == 2U,
          "string table preserves the counted entry total");
    Check(decoded->entries[0].key == "alphakey" &&
              decoded->entries[0].value == "First" &&
              decoded->entries[1].key == "beta" &&
              decoded->entries[1].value == encoded_value,
          "string table owns source-order lowercase keys and untranslated "
          "value bytes");
    const auto *alpha = decoded->Find("ALPHAKEY");
    const auto *beta = decoded->Find("beta");
    Check(alpha != nullptr && alpha->value == "First" && beta != nullptr &&
              beta->value == encoded_value &&
              decoded->Find("missing") == nullptr,
          "string table performs ASCII-case-insensitive key lookup");
  }

  const auto repeated = omega::retail::DecodeRetailStringTable(bytes);
  Check(decoded && repeated && *decoded == *repeated,
        "string-table decoding is deterministic and stateless");

  std::vector<std::byte> unaligned_storage(bytes.size() + 1U, std::byte{0x7C});
  std::ranges::copy(bytes, unaligned_storage.begin() + 1);
  const auto unaligned = omega::retail::DecodeRetailStringTable(
      std::span<const std::byte>(unaligned_storage.data() + 1, bytes.size()));
  Check(decoded && unaligned && *decoded == *unaligned,
        "string table supports an unaligned borrowed input span");

  omega::retail::RetailStringTableIR owned;
  {
    auto transient = MakeTable({{.key = "OwnedKey", .value = "OwnedValue"}});
    auto transient_result = omega::retail::DecodeRetailStringTable(transient);
    Check(transient_result.has_value(),
          "string-table ownership fixture decodes");
    if (transient_result)
      owned = std::move(*transient_result);
    std::ranges::fill(transient, std::byte{0xDD});
  }
  Check(owned.entries.size() == 1U && owned.entries[0].key == "ownedkey" &&
            owned.entries[0].value == "OwnedValue",
        "string-table output remains valid after source destruction");

  bool truncations_rejected = true;
  for (std::size_t size = 0; size < bytes.size(); ++size) {
    const auto result = omega::retail::DecodeRetailStringTable(
        std::span<const std::byte>(bytes.data(), size));
    truncations_rejected =
        truncations_rejected && !result &&
        result.error().code == omega::asset::DecodeErrorCode::Truncated;
  }
  Check(truncations_rejected, "every generated string-table truncation fails "
                              "before reading beyond the span");

  auto bad = bytes;
  std::fill_n(bad.begin() + 4, 16, std::byte{'Q'});
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects a key without a fixed-field NUL");

  bad = bytes;
  bad[4] = std::byte{0};
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects an empty key");

  bad = bytes;
  bad[4] = std::byte{0x1F};
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects a non-printable key byte");

  bad = bytes;
  bad[4U + specs[0].key.size() + 1U] = std::byte{'X'};
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects nonzero bytes after the fixed-key NUL");

  constexpr std::size_t first_value_offset = 24U;
  bad = bytes;
  bad[first_value_offset] = std::byte{0};
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects an early value NUL");

  bad = bytes;
  bad[first_value_offset + specs[0].value.size()] = std::byte{'X'};
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects a value span without a terminal NUL");

  const auto duplicate = MakeTable(
      {{.key = "CaseKey", .value = "one"}, {.key = "casekey", .value = "two"}});
  CheckError(omega::retail::DecodeRetailStringTable(duplicate),
             omega::asset::DecodeErrorCode::DuplicateReference,
             "string table rejects duplicate ASCII-normalized keys");

  bad = bytes;
  bad.push_back(std::byte{0});
  CheckError(omega::retail::DecodeRetailStringTable(bad),
             omega::asset::DecodeErrorCode::Malformed,
             "string table rejects bytes after the final aligned entry");

  std::vector<std::byte> hostile_count(4U, std::byte{0});
  WriteU32(hostile_count, 0, std::numeric_limits<std::uint32_t>::max());
  CheckError(omega::retail::DecodeRetailStringTable(hostile_count),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "string table rejects a hostile entry count before allocation");

  auto empty_table = MakeTable(std::span<const SyntheticEntry>{});
  const auto empty = omega::retail::DecodeRetailStringTable(empty_table);
  Check(empty && empty->entries.empty(),
        "string table accepts an exact zero-entry table");
  empty_table.push_back(std::byte{0});
  CheckError(omega::retail::DecodeRetailStringTable(empty_table),
             omega::asset::DecodeErrorCode::Malformed,
             "zero-entry string table still rejects trailing bytes");

  auto limits = omega::asset::DecodeLimits{};
  limits.maximum_input_bytes = bytes.size();
  Check(omega::retail::DecodeRetailStringTable(bytes, limits).has_value(),
        "string table accepts the exact caller input-byte budget");
  limits.maximum_input_bytes = bytes.size() - 1U;
  CheckError(omega::retail::DecodeRetailStringTable(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "string table rejects one below the caller input-byte budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_items = 1U + specs.size();
  Check(omega::retail::DecodeRetailStringTable(bytes, limits).has_value(),
        "string table accepts the exact caller item budget");
  --limits.maximum_items;
  CheckError(omega::retail::DecodeRetailStringTable(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "string table rejects one below the caller item budget");

  const std::uint64_t logical_output_bytes =
      sizeof(omega::retail::RetailStringTableIR) +
      specs.size() * sizeof(omega::retail::RetailStringEntryIR) +
      specs[0].key.size() + specs[0].value.size() + specs[1].key.size() +
      specs[1].value.size();
  limits = omega::asset::DecodeLimits{};
  limits.maximum_output_bytes = logical_output_bytes;
  Check(omega::retail::DecodeRetailStringTable(bytes, limits).has_value(),
        "string table accepts the exact caller logical-output budget");
  --limits.maximum_output_bytes;
  CheckError(omega::retail::DecodeRetailStringTable(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "string table rejects one below the caller output budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_string_bytes = 8U;
  Check(omega::retail::DecodeRetailStringTable(bytes, limits).has_value(),
        "string table accepts the exact largest owned-string budget");
  limits.maximum_string_bytes = 7U;
  CheckError(omega::retail::DecodeRetailStringTable(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "string table rejects a key above the caller string budget");

  const auto value_budget = MakeTable({{.key = "A", .value = "12345"}});
  limits = omega::asset::DecodeLimits{};
  limits.maximum_string_bytes = 5U;
  Check(
      omega::retail::DecodeRetailStringTable(value_budget, limits).has_value(),
      "string table accepts the exact value-string budget");
  limits.maximum_string_bytes = 4U;
  CheckError(omega::retail::DecodeRetailStringTable(value_budget, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "string table rejects a value above the caller string budget");

  limits = omega::asset::DecodeLimits{};
  limits.maximum_scratch_bytes = 0;
  CheckError(omega::retail::DecodeRetailStringTable(bytes, limits),
             omega::asset::DecodeErrorCode::LimitExceeded,
             "nonempty string table accounts duplicate-check scratch storage");
  limits.maximum_nesting_depth = 0;
  Check(omega::retail::DecodeRetailStringTable(
            MakeTable(std::span<const SyntheticEntry>{}), limits)
            .has_value(),
        "empty flat string table needs no dynamic scratch or nesting edges");

  std::vector<std::byte> oversized(
      static_cast<std::size_t>(
          omega::retail::kRetailStringTableMaximumInputBytes) +
          1U,
      std::byte{0});
  auto permissive_limits = omega::asset::DecodeLimits{};
  permissive_limits.maximum_input_bytes =
      std::numeric_limits<std::uint64_t>::max();
  CheckError(
      omega::retail::DecodeRetailStringTable(oversized, permissive_limits),
      omega::asset::DecodeErrorCode::LimitExceeded,
      "caller limits cannot widen the fixed string-table input ceiling");

  if (failures != 0)
    std::cerr << failures << " retail string table decoder test(s) failed\n";
  return failures == 0 ? 0 : 1;
}
