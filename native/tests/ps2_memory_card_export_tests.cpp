#include "omega/compat/ps2_memory_card_export.h"

#include "omega/compat/ps2_memory_card_filesystem.h"
#include "omega/compat/ps2_memory_card_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using omega::compat::Ps2MemoryCardWriteError;
using omega::compat::Ps2MemoryCardWriteErrorCode;

constexpr std::uint16_t kFileMode = 0x8413U;
constexpr std::uint16_t kDirectoryMode = 0x8427U;
constexpr std::size_t kClusterBytes = 1024U;

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <class T>
void CheckError(const std::expected<T, Ps2MemoryCardWriteError> &result,
                const Ps2MemoryCardWriteErrorCode code,
                const std::string_view message) {
  Check(!result && result.error().code == code, message);
}

[[nodiscard]] std::uint32_t ReadU32(const std::vector<std::byte> &bytes,
                                    const std::size_t offset) {
  std::uint32_t value = 0U;
  for (std::size_t index = 0U; index < sizeof(value); ++index) {
    value |= static_cast<std::uint32_t>(
                 std::to_integer<std::uint8_t>(bytes[offset + index]))
             << static_cast<unsigned>(index * 8U);
  }
  return value;
}

[[nodiscard]] std::vector<std::byte> Payload(const std::size_t size,
                                             const std::uint8_t seed) {
  std::vector<std::byte> bytes(size);
  for (std::size_t index = 0U; index < bytes.size(); ++index) {
    bytes[index] = static_cast<std::byte>((index * 31U + seed) & 0xFFU);
  }
  return bytes;
}

[[nodiscard]] omega::compat::Ps2MemoryCardTimestamp
Timestamp(const std::uint8_t second, const std::uint16_t year) {
  return omega::compat::Ps2MemoryCardTimestamp{
      .bytes = {std::byte{0}, static_cast<std::byte>(second), std::byte{2},
                std::byte{3}, std::byte{4}, std::byte{5},
                static_cast<std::byte>(year & 0xFFU),
                static_cast<std::byte>((year >> 8U) & 0xFFU)}};
}

[[nodiscard]] omega::compat::Ps2MemoryCardSaveDirectory MakeDirectory() {
  return omega::compat::Ps2MemoryCardSaveDirectory{
      .name = "BASLUS-212830000",
      .mode = kDirectoryMode,
      .attributes = 0x12345678U,
      .created = Timestamp(1U, 2024U),
      .modified = Timestamp(2U, 2025U),
      .files =
          {
              omega::compat::Ps2MemoryCardOpaqueFile{
                  .name = "icon.sys",
                  .mode = kFileMode,
                  .attributes = 0xAABBCCDDU,
                  .created = Timestamp(3U, 2023U),
                  .modified = Timestamp(4U, 2024U),
                  .bytes = Payload(1300U, 7U),
              },
              omega::compat::Ps2MemoryCardOpaqueFile{
                  .name = "empty.bin",
                  .mode = kFileMode,
                  .attributes = 0x0BADF00DU,
                  .created = Timestamp(5U, 2022U),
                  .modified = Timestamp(6U, 2023U),
                  .bytes = {},
              },
          },
  };
}

void CheckLogicalAndRawRoundTrip() {
  const auto directory = MakeDirectory();
  const auto logical =
      omega::compat::CreatePs2MemoryCardLogicalImage(directory);
  Check(logical &&
            logical->size() == omega::compat::kPs2MemoryCardLogicalImageBytes,
        "the exporter creates exactly one standard logical 8 MiB card");
  if (!logical)
    return;
  const auto repeated =
      omega::compat::CreatePs2MemoryCardLogicalImage(directory);
  Check(repeated && *repeated == *logical,
        "the same archive produces byte-identical logical cards");

  const auto descriptor = omega::compat::InspectPs2MemoryCardImage(*logical);
  Check(descriptor && descriptor->allocation_offset == 41U &&
            descriptor->allocation_end == 8135U,
        "the generated superblock has the fixed bounded standard geometry");
  Check(
      ReadU32(*logical, 8U * kClusterBytes) == 9U &&
          ReadU32(*logical, 8U * kClusterBytes + 31U * 4U) == 40U &&
          ReadU32(*logical, 8U * kClusterBytes + 32U * 4U) == 0xFFFFFFFFU,
      "the generated IFC deterministically names the 32 standard FAT clusters");
  Check(
      ReadU32(*logical, 9U * kClusterBytes) == 0x80000001U &&
          ReadU32(*logical, 9U * kClusterBytes + 4U) == 0xFFFFFFFFU &&
          ReadU32(*logical, 9U * kClusterBytes + 8U) == 0x80000003U &&
          ReadU32(*logical, 9U * kClusterBytes + 12U) == 0xFFFFFFFFU &&
          ReadU32(*logical, 9U * kClusterBytes + 16U) == 0x80000005U &&
          ReadU32(*logical, 9U * kClusterBytes + 20U) == 0xFFFFFFFFU,
      "root, save-directory, and first file chains use deterministic clusters");
  const auto save_directory_offset = (41U + 2U) * kClusterBytes;
  Check(ReadU32(*logical, save_directory_offset + 0x10U) == 0U &&
            ReadU32(*logical, save_directory_offset + 0x14U) == 2U,
        "the child dot entry points to its parent cluster and root entry");
  bool reserved_superblock_bytes_are_zero = true;
  for (std::size_t offset = 0x152U;
       offset < omega::compat::kPs2MemoryCardLogicalPageBytes; ++offset) {
    reserved_superblock_bytes_are_zero = reserved_superblock_bytes_are_zero &&
                                         (*logical)[offset] == std::byte{0};
  }
  Check(reserved_superblock_bytes_are_zero,
        "reserved superblock bytes are deterministically zero-filled");

  const auto imported =
      omega::compat::ReadPs2MemoryCardSaveDirectory(*logical, directory.name);
  Check(imported && imported->name == directory.name &&
            imported->mode == directory.mode &&
            imported->attributes == directory.attributes &&
            imported->created == directory.created &&
            imported->modified == directory.modified &&
            imported->files.size() == directory.files.size(),
        "the strict reader recovers generated directory metadata and order");
  if (imported && imported->files.size() == directory.files.size()) {
    for (std::size_t index = 0U; index < directory.files.size(); ++index) {
      const auto &actual = imported->files[index];
      const auto &expected = directory.files[index];
      Check(actual.name == expected.name && actual.mode == expected.mode &&
                actual.attributes == expected.attributes &&
                actual.created == expected.created &&
                actual.modified == expected.modified &&
                actual.bytes == expected.bytes,
            "every opaque file round-trips without payload interpretation");
    }
  }

  const auto raw = omega::compat::CreatePs2MemoryCardRawImage(directory);
  Check(raw && raw->size() == omega::compat::kPs2MemoryCardRawImageBytes,
        "the raw exporter emits exactly 16,384 canonical 528-byte pages");
  if (raw) {
    const auto raw_imported =
        omega::compat::ReadPs2MemoryCardSaveDirectory(*raw, directory.name);
    Check(raw_imported && raw_imported->files.size() == 2U &&
              raw_imported->files[0].bytes == directory.files[0].bytes,
          "canonical raw output round-trips through the strict reader");
  }

  auto empty_directory = directory;
  empty_directory.files.clear();
  const auto empty_card =
      omega::compat::CreatePs2MemoryCardLogicalImage(empty_directory);
  Check(empty_card.has_value(), "an empty save directory can be exported");
  if (empty_card) {
    const auto empty_import = omega::compat::ReadPs2MemoryCardSaveDirectory(
        *empty_card, empty_directory.name);
    Check(empty_import && empty_import->files.empty(),
          "a directory containing only dot entries round-trips exactly");
  }
}

void CheckInputValidation() {
  auto directory = MakeDirectory();
  directory.name = "bad/name";
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory),
             Ps2MemoryCardWriteErrorCode::InvalidDirectoryName,
             "an illegal top-level directory name is rejected");

  directory = MakeDirectory();
  directory.mode = kFileMode;
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory),
             Ps2MemoryCardWriteErrorCode::InvalidMode,
             "the top-level archive must describe a live directory");

  directory = MakeDirectory();
  directory.files[0].name = "bad*name";
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory),
             Ps2MemoryCardWriteErrorCode::InvalidFileName,
             "an illegal file name is rejected");

  directory = MakeDirectory();
  directory.files[1].name = directory.files[0].name;
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory),
             Ps2MemoryCardWriteErrorCode::DuplicateFileName,
             "duplicate file names are rejected");

  directory = MakeDirectory();
  directory.files[0].mode = kDirectoryMode;
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory),
             Ps2MemoryCardWriteErrorCode::InvalidMode,
             "nested directory entries cannot be exported as files");
}

void CheckLimitsAndCapacity() {
  const auto directory = MakeDirectory();
  auto limits = omega::compat::Ps2MemoryCardWriteLimits{};
  limits.maximum_files = 1U;
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory, limits),
             Ps2MemoryCardWriteErrorCode::LimitExceeded,
             "the exporter enforces its file-count limit before allocation");

  limits = omega::compat::Ps2MemoryCardWriteLimits{};
  limits.maximum_file_bytes = 1299U;
  CheckError(omega::compat::CreatePs2MemoryCardLogicalImage(directory, limits),
             Ps2MemoryCardWriteErrorCode::LimitExceeded,
             "the exporter enforces its per-file byte limit before allocation");

  limits = omega::compat::Ps2MemoryCardWriteLimits{};
  limits.maximum_total_file_bytes = 1299U;
  CheckError(
      omega::compat::CreatePs2MemoryCardLogicalImage(directory, limits),
      Ps2MemoryCardWriteErrorCode::LimitExceeded,
      "the exporter enforces its aggregate byte limit before allocation");

  auto oversized = MakeDirectory();
  oversized.files.clear();
  oversized.files.push_back(omega::compat::Ps2MemoryCardOpaqueFile{
      .name = "capacity.bin",
      .mode = kFileMode,
      .bytes = std::vector<std::byte>(8U * 1024U * 1024U),
  });
  CheckError(
      omega::compat::CreatePs2MemoryCardLogicalImage(oversized),
      Ps2MemoryCardWriteErrorCode::CardCapacityExceeded,
      "filesystem metadata and payload clusters must fit the standard card");
}
} // namespace

int main() {
  CheckLogicalAndRawRoundTrip();
  CheckInputValidation();
  CheckLimitsAndCapacity();
  return failures == 0 ? 0 : 1;
}
