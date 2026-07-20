#include "omega/compat/ps2_memory_card_filesystem.h"

#include "omega/compat/ps2_memory_card_image.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <new>
#include <span>
#include <string_view>
#include <vector>

namespace ps2_filesystem_test_allocation {
inline constexpr std::size_t kDisabled =
    std::numeric_limits<std::size_t>::max();
std::size_t allocations_before_failure = kDisabled;

void Arm(const std::size_t allocations_to_allow) noexcept {
  allocations_before_failure = allocations_to_allow;
}

void Disarm() noexcept { allocations_before_failure = kDisabled; }
} // namespace ps2_filesystem_test_allocation

void *operator new(const std::size_t size) {
  if (ps2_filesystem_test_allocation::allocations_before_failure !=
      ps2_filesystem_test_allocation::kDisabled) {
    if (ps2_filesystem_test_allocation::allocations_before_failure == 0U) {
      ps2_filesystem_test_allocation::Disarm();
      throw std::bad_alloc{};
    }
    --ps2_filesystem_test_allocation::allocations_before_failure;
  }
  if (void *const memory = std::malloc(size == 0U ? 1U : size))
    return memory;
  throw std::bad_alloc{};
}

void operator delete(void *const memory) noexcept { std::free(memory); }

void operator delete(void *const memory, const std::size_t) noexcept {
  std::free(memory);
}

namespace {
using namespace std::string_view_literals;

using omega::compat::Ps2MemoryCardReadError;
using omega::compat::Ps2MemoryCardReadErrorCode;

constexpr std::size_t kClusterBytes = 1024U;
constexpr std::uint32_t kAllocationOffset = 41U;
constexpr std::uint32_t kEndOfChain = 0xFFFFFFFFU;
constexpr std::uint32_t kAllocatedBit = 0x80000000U;
constexpr std::uint16_t kFileMode = 0x8413U;
constexpr std::uint16_t kDirectoryMode = 0x8427U;
constexpr auto kSaveName = "BASLUS-212830000"sv;
constexpr std::array<std::byte, 8U> kCreated{
    std::byte{0}, std::byte{1}, std::byte{2},    std::byte{3},
    std::byte{4}, std::byte{5}, std::byte{0xE8}, std::byte{0x07}};
constexpr std::array<std::byte, 8U> kModified{
    std::byte{0}, std::byte{6},  std::byte{7},    std::byte{8},
    std::byte{9}, std::byte{10}, std::byte{0xE9}, std::byte{0x07}};

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <class T>
void CheckError(const std::expected<T, Ps2MemoryCardReadError> &result,
                const Ps2MemoryCardReadErrorCode code,
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

[[nodiscard]] std::size_t
AbsoluteClusterOffset(const std::uint32_t absolute_cluster) {
  return static_cast<std::size_t>(absolute_cluster) * kClusterBytes;
}

[[nodiscard]] std::size_t
RelativeClusterOffset(const std::uint32_t relative_cluster) {
  return AbsoluteClusterOffset(kAllocationOffset + relative_cluster);
}

void SetFat(std::vector<std::byte> &card, const std::uint32_t relative_cluster,
            const std::uint32_t value) {
  const auto fat_cluster = 9U + relative_cluster / 256U;
  const auto fat_index = relative_cluster % 256U;
  WriteU32(card, AbsoluteClusterOffset(fat_cluster) + fat_index * 4U, value);
}

void WriteEntry(std::vector<std::byte> &card, const std::size_t offset,
                const std::uint16_t mode, const std::uint32_t length,
                const std::uint32_t first_cluster,
                const std::uint32_t attributes, const std::string_view name,
                const std::uint32_t directory_entry = 0U) {
  std::fill_n(card.begin() + static_cast<std::ptrdiff_t>(offset), 512U,
              std::byte{0});
  WriteU16(card, offset, mode);
  WriteU32(card, offset + 4U, length);
  std::copy(kCreated.begin(), kCreated.end(),
            card.begin() + static_cast<std::ptrdiff_t>(offset + 8U));
  WriteU32(card, offset + 0x10U, first_cluster);
  WriteU32(card, offset + 0x14U, directory_entry);
  std::copy(kModified.begin(), kModified.end(),
            card.begin() + static_cast<std::ptrdiff_t>(offset + 0x18U));
  WriteU32(card, offset + 0x20U, attributes);
  WriteText(card, offset + 0x40U, name);
}

[[nodiscard]] std::vector<std::byte> MakeSyntheticCard() {
  std::vector<std::byte> card(omega::compat::kPs2MemoryCardLogicalImageBytes,
                              std::byte{0xFF});
  WriteText(card, 0U, "Sony PS2 Memory Card Format ");
  std::fill_n(card.begin() + 0x1C, 12U, std::byte{0});
  WriteText(card, 0x1CU, "1.2.0.0");
  WriteU16(card, 0x28U, 512U);
  WriteU16(card, 0x2AU, 2U);
  WriteU16(card, 0x2CU, 16U);
  WriteU16(card, 0x2EU, 0xFF00U);
  WriteU32(card, 0x30U, 8192U);
  WriteU32(card, 0x34U, kAllocationOffset);
  WriteU32(card, 0x38U, 8135U);
  WriteU32(card, 0x3CU, 0U);
  WriteU32(card, 0x40U, 1023U);
  WriteU32(card, 0x44U, 1022U);
  std::fill_n(card.begin() + 0x48, 8U, std::byte{0});
  WriteU32(card, 0x50U, 8U);
  card[0x150U] = std::byte{2};
  card[0x151U] = std::byte{0x52};

  const auto ifc_offset = AbsoluteClusterOffset(8U);
  for (std::uint32_t index = 0U; index < 32U; ++index)
    WriteU32(card, ifc_offset + index * 4U, 9U + index);
  for (std::uint32_t fat_cluster = 9U; fat_cluster <= 40U; ++fat_cluster) {
    const auto offset = AbsoluteClusterOffset(fat_cluster);
    for (std::size_t index = 0U; index < 256U; ++index)
      WriteU32(card, offset + index * 4U, 0x7FFFFFFFU);
  }

  SetFat(card, 0U, kAllocatedBit | 1U);
  SetFat(card, 1U, kEndOfChain);
  SetFat(card, 2U, kAllocatedBit | 3U);
  SetFat(card, 3U, kEndOfChain);
  SetFat(card, 4U, kAllocatedBit | 5U);
  SetFat(card, 5U, kEndOfChain);

  const auto root_0 = RelativeClusterOffset(0U);
  const auto root_1 = RelativeClusterOffset(1U);
  WriteEntry(card, root_0, kDirectoryMode, 3U, 0U, 0x01020304U, ".");
  WriteEntry(card, root_0 + 512U, kDirectoryMode, 0U, 0U, 0U, "..");
  WriteEntry(card, root_1, kDirectoryMode, 4U, 2U, 0x12345678U, kSaveName);

  const auto save_0 = RelativeClusterOffset(2U);
  const auto save_1 = RelativeClusterOffset(3U);
  WriteEntry(card, save_0, kDirectoryMode, 0U, 0U, 0U, ".", 2U);
  WriteEntry(card, save_0 + 512U, kDirectoryMode, 0U, 0U, 0U, "..");
  WriteEntry(card, save_1, kFileMode, 1300U, 4U, 0xAABBCCDDU, "icon.sys");
  WriteEntry(card, save_1 + 512U, kFileMode, 0U, kEndOfChain, 0x0BADF00DU,
             "empty.bin");

  for (std::size_t index = 0U; index < 1300U; ++index) {
    const auto cluster = static_cast<std::uint32_t>(4U + index / kClusterBytes);
    const auto in_cluster = index % kClusterBytes;
    card[RelativeClusterOffset(cluster) + in_cluster] =
        static_cast<std::byte>((index * 29U + 7U) & 0xFFU);
  }
  return card;
}

[[nodiscard]] std::vector<std::byte> ExpectedPayload() {
  std::vector<std::byte> result(1300U);
  for (std::size_t index = 0U; index < result.size(); ++index)
    result[index] = static_cast<std::byte>((index * 29U + 7U) & 0xFFU);
  return result;
}

void CheckHappyPath() {
  const auto card = MakeSyntheticCard();
  const auto save =
      omega::compat::ReadPs2MemoryCardSaveDirectory(card, kSaveName);
  Check(save.has_value(),
        "the synthetic standard IFC/FAT save directory is decoded");
  if (!save)
    return;
  Check(save->name == kSaveName && save->mode == kDirectoryMode &&
            save->attributes == 0x12345678U &&
            save->created.bytes == kCreated &&
            save->modified.bytes == kModified,
        "top-level directory metadata is preserved exactly");
  Check(save->files.size() == 2U && save->files[0].name == "icon.sys" &&
            save->files[1].name == "empty.bin",
        "live files retain their directory-entry order");
  if (save->files.size() != 2U)
    return;
  Check(save->files[0].mode == kFileMode &&
            save->files[0].attributes == 0xAABBCCDDU &&
            save->files[0].created.bytes == kCreated &&
            save->files[0].modified.bytes == kModified &&
            save->files[0].bytes == ExpectedPayload(),
        "multi-cluster file metadata and opaque bytes are exact");
  Check(save->files[1].attributes == 0x0BADF00DU &&
            save->files[1].bytes.empty(),
        "an empty file preserves metadata without claiming a cluster");

  const auto raw = omega::compat::ConvertPs2MemoryCardImageToRaw(card);
  Check(raw.has_value(), "the synthetic reader fixture converts to raw pages");
  if (raw) {
    const auto raw_save =
        omega::compat::ReadPs2MemoryCardSaveDirectory(*raw, kSaveName);
    Check(raw_save && raw_save->files.size() == 2U &&
              raw_save->files[0].bytes == ExpectedPayload(),
          "the same bounded reader accepts canonical 528-byte raw pages");
  }

  auto zero_padded = card;
  std::fill(zero_padded.begin() + 0x54, zero_padded.begin() + 0xD0,
            std::byte{0});
  std::fill(zero_padded.begin() + static_cast<std::ptrdiff_t>(
                                      AbsoluteClusterOffset(8U) + 32U * 4U),
            zero_padded.begin() +
                static_cast<std::ptrdiff_t>(AbsoluteClusterOffset(9U)),
            std::byte{0});
  const auto zero_padded_save =
      omega::compat::ReadPs2MemoryCardSaveDirectory(zero_padded, kSaveName);
  Check(zero_padded_save && zero_padded_save->files.size() == 2U,
        "zero-filled unused IFC slots and indirect FAT tails are accepted");
}

void CheckSelectionFailures() {
  const auto card = MakeSyntheticCard();
  CheckError(omega::compat::ReadPs2MemoryCardSaveDirectory(card, ""),
             Ps2MemoryCardReadErrorCode::InvalidSelectionName,
             "an empty explicit selection is rejected");
  CheckError(omega::compat::ReadPs2MemoryCardSaveDirectory(card, "bad/name"),
             Ps2MemoryCardReadErrorCode::InvalidSelectionName,
             "an illegal explicit selection is rejected");
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(card, "MISSING-SAVE"),
      Ps2MemoryCardReadErrorCode::SaveDirectoryNotFound,
      "a missing top-level directory is reported without selecting another");

  auto duplicate = card;
  WriteU32(duplicate, RelativeClusterOffset(0U) + 4U, 4U);
  WriteEntry(duplicate, RelativeClusterOffset(1U) + 512U, kDirectoryMode, 2U,
             6U, 0U, kSaveName);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(duplicate, kSaveName),
      Ps2MemoryCardReadErrorCode::AmbiguousSaveDirectory,
      "duplicate exact top-level names fail closed");
}

void CheckAllocationAndChainFailures() {
  const auto card = MakeSyntheticCard();

  auto malformed = card;
  WriteU32(malformed, AbsoluteClusterOffset(8U), 1U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
      "a FAT metadata pointer in reserved erase block zero is rejected");

  malformed = card;
  WriteU32(malformed, AbsoluteClusterOffset(8U), kAllocationOffset);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
      "a FAT pointer inside the allocation region is rejected");

  malformed = card;
  WriteU32(malformed, AbsoluteClusterOffset(8U) + 4U, 9U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidAllocationTable,
      "a shared FAT metadata cluster is rejected");

  malformed = card;
  SetFat(malformed, 2U, kAllocatedBit | 2U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::ClusterChainLoop,
      "a selected directory cluster-chain loop is rejected");

  malformed = card;
  SetFat(malformed, 4U, kAllocatedBit | 4U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::ClusterChainLoop,
      "a file cluster-chain loop is rejected");

  malformed = card;
  SetFat(malformed, 4U, kEndOfChain);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidClusterChain,
      "a chain shorter than the file extent is rejected");

  malformed = card;
  WriteU32(malformed, RelativeClusterOffset(3U) + 4U, 1U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidClusterChain,
      "a chain longer than the file extent is rejected");

  malformed = card;
  WriteU32(malformed, RelativeClusterOffset(3U) + 512U + 0x10U, 6U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidClusterChain,
      "an empty file cannot retain a data-cluster pointer");

  malformed = card;
  WriteU32(malformed, RelativeClusterOffset(3U) + 512U + 4U, 1U);
  WriteU32(malformed, RelativeClusterOffset(3U) + 512U + 0x10U, 4U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::SharedCluster,
      "two live files cannot share a data cluster");

  malformed = card;
  WriteU32(malformed, RelativeClusterOffset(1U) + 0x10U, 0U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::SharedCluster,
      "the selected directory cannot reuse a root-directory cluster");

  malformed = card;
  WriteU32(malformed, RelativeClusterOffset(3U) + 4U, 0xFFFFFFFFU);
  auto wide_limits = omega::compat::Ps2MemoryCardReadLimits{};
  wide_limits.maximum_file_bytes = std::numeric_limits<std::size_t>::max();
  wide_limits.maximum_total_file_bytes =
      std::numeric_limits<std::size_t>::max();
  CheckError(omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName,
                                                           wide_limits),
             Ps2MemoryCardReadErrorCode::InvalidClusterChain,
             "an impossible file chain count is rejected before allocation");
}

void CheckDirectoryAndLimitFailures() {
  const auto card = MakeSyntheticCard();
  auto malformed = card;
  WriteU32(malformed, RelativeClusterOffset(2U) + 0x14U, 1U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidDirectory,
      "the selected dot entry must backlink to its root directory entry");

  malformed = card;
  WriteU32(malformed, RelativeClusterOffset(0U) + 0x14U, 1U);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidDirectory,
      "the root dot entry must retain its canonical backlink");

  malformed = card;
  std::fill_n(malformed.begin() + static_cast<std::ptrdiff_t>(
                                      RelativeClusterOffset(3U) + 0x40U),
              32U, std::byte{'A'});
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidEntryName,
      "a live nonterminated file name is rejected");

  malformed = card;
  WriteU16(malformed, RelativeClusterOffset(3U), kDirectoryMode);
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::UnsupportedEntryType,
      "nested directories remain outside the bounded reader");

  malformed = card;
  WriteEntry(malformed, RelativeClusterOffset(3U) + 512U, kFileMode, 0U,
             kEndOfChain, 0U, "icon.sys");
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(malformed, kSaveName),
      Ps2MemoryCardReadErrorCode::InvalidDirectory,
      "duplicate live child names are rejected");

  auto limits = omega::compat::Ps2MemoryCardReadLimits{};
  limits.maximum_file_bytes = 1299U;
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(card, kSaveName, limits),
      Ps2MemoryCardReadErrorCode::LimitExceeded,
      "the per-file output limit is enforced before allocation");
  limits = omega::compat::Ps2MemoryCardReadLimits{};
  limits.maximum_files = 1U;
  CheckError(
      omega::compat::ReadPs2MemoryCardSaveDirectory(card, kSaveName, limits),
      Ps2MemoryCardReadErrorCode::LimitExceeded,
      "the live-file count limit is enforced");
}

void CheckAllocationFailures() {
  const auto card = MakeSyntheticCard();

  ps2_filesystem_test_allocation::Arm(0U);
  const auto conversion_failure =
      omega::compat::ReadPs2MemoryCardSaveDirectory(card, kSaveName);
  ps2_filesystem_test_allocation::Disarm();
  CheckError(conversion_failure, Ps2MemoryCardReadErrorCode::AllocationFailed,
             "reader maps image conversion allocation failure");

  ps2_filesystem_test_allocation::Arm(1U);
  const auto reader_failure =
      omega::compat::ReadPs2MemoryCardSaveDirectory(card, kSaveName);
  ps2_filesystem_test_allocation::Disarm();
  CheckError(reader_failure, Ps2MemoryCardReadErrorCode::AllocationFailed,
             "reader catches its internal allocation failure");
}
} // namespace

int main() {
  CheckHappyPath();
  CheckSelectionFailures();
  CheckAllocationAndChainFailures();
  CheckDirectoryAndLimitFailures();
  CheckAllocationFailures();
  return failures == 0 ? 0 : 1;
}
