#include "omega/persistence/save_database.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace omega::persistence {
namespace {
constexpr std::array<std::byte, 8> kMagic{
    std::byte{'O'}, std::byte{'O'}, std::byte{'S'}, std::byte{'D'},
    std::byte{'B'}, std::byte{'0'}, std::byte{'0'}, std::byte{'1'},
};
constexpr std::uint32_t kEndianMarker = 0x01020304U;
constexpr std::size_t kHeaderBytes = 64U;
constexpr std::size_t kRecordHeaderBytes = 32U;
constexpr std::size_t kHeaderVersionOffset = 8U;
constexpr std::size_t kHeaderSizeOffset = 12U;
constexpr std::size_t kHeaderEndianOffset = 16U;
constexpr std::size_t kHeaderReserved0Offset = 20U;
constexpr std::size_t kHeaderGenerationOffset = 24U;
constexpr std::size_t kHeaderRecordCountOffset = 32U;
constexpr std::size_t kHeaderReserved1Offset = 36U;
constexpr std::size_t kHeaderPayloadSizeOffset = 40U;
constexpr std::size_t kHeaderPayloadCrcOffset = 48U;
constexpr std::size_t kHeaderCrcOffset = 52U;
constexpr std::size_t kHeaderReserved2Offset = 56U;

enum class Slot : std::uint8_t {
  A = 0,
  B = 1,
};

struct StoredRecord {
  std::uint32_t schema_version = 0U;
  std::uint64_t revision = 0U;
  std::vector<std::byte> value;

  bool operator==(const StoredRecord &) const = default;
};

using RecordMap = std::map<std::string, StoredRecord, std::less<>>;

struct DecodedSnapshot {
  std::uint64_t generation = 0U;
  RecordMap records;
  std::size_t logical_value_bytes = 0U;
};

enum class SlotReadStatus : std::uint8_t {
  Missing,
  Valid,
  Invalid,
};

struct SlotReadResult {
  SlotReadStatus status = SlotReadStatus::Missing;
  std::optional<DecodedSnapshot> snapshot;
  std::optional<SaveDatabaseError> error;
};

[[nodiscard]] SaveDatabaseError MakeError(const SaveDatabaseErrorCode code,
                                          std::string message) {
  return {.code = code, .message = std::move(message)};
}

[[nodiscard]] bool CheckedAdd(const std::size_t lhs, const std::size_t rhs,
                              std::size_t *const result) noexcept {
  if (lhs > std::numeric_limits<std::size_t>::max() - rhs)
    return false;
  *result = lhs + rhs;
  return true;
}

[[nodiscard]] std::uint8_t ByteValue(const std::byte value) noexcept {
  return std::to_integer<std::uint8_t>(value);
}

void StoreU16(std::span<std::byte> bytes, const std::size_t offset,
              const std::uint16_t value) noexcept {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void StoreU32(std::span<std::byte> bytes, const std::size_t offset,
              const std::uint32_t value) noexcept {
  for (std::size_t index = 0; index < 4U; ++index)
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
}

void StoreU64(std::span<std::byte> bytes, const std::size_t offset,
              const std::uint64_t value) noexcept {
  for (std::size_t index = 0; index < 8U; ++index)
    bytes[offset + index] =
        static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
}

[[nodiscard]] std::uint16_t LoadU16(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  return static_cast<std::uint16_t>(ByteValue(bytes[offset])) |
         static_cast<std::uint16_t>(ByteValue(bytes[offset + 1U]) << 8U);
}

[[nodiscard]] std::uint32_t LoadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  std::uint32_t value = 0U;
  for (std::size_t index = 0; index < 4U; ++index)
    value |= static_cast<std::uint32_t>(ByteValue(bytes[offset + index]))
             << (index * 8U);
  return value;
}

[[nodiscard]] std::uint64_t LoadU64(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept {
  std::uint64_t value = 0U;
  for (std::size_t index = 0; index < 8U; ++index)
    value |= static_cast<std::uint64_t>(ByteValue(bytes[offset + index]))
             << (index * 8U);
  return value;
}

[[nodiscard]] std::uint32_t
Crc32(const std::span<const std::byte> bytes) noexcept {
  std::uint32_t crc = 0xFFFFFFFFU;
  for (const std::byte value : bytes) {
    crc ^= ByteValue(value);
    for (std::uint32_t bit = 0U; bit < 8U; ++bit)
      crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
  }
  return crc ^ 0xFFFFFFFFU;
}

[[nodiscard]] bool IsKeyByte(const unsigned char value) noexcept {
  return (value >= static_cast<unsigned char>('a') &&
          value <= static_cast<unsigned char>('z')) ||
         (value >= static_cast<unsigned char>('0') &&
          value <= static_cast<unsigned char>('9')) ||
         value == static_cast<unsigned char>('_') ||
         value == static_cast<unsigned char>('-') ||
         value == static_cast<unsigned char>('.') ||
         value == static_cast<unsigned char>('/');
}

[[nodiscard]] bool HasInvalidSegment(const std::string_view key) noexcept {
  std::size_t segment_start = 0U;
  for (std::size_t index = 0U; index <= key.size(); ++index) {
    if (index != key.size() && key[index] != '/')
      continue;
    const std::string_view segment =
        key.substr(segment_start, index - segment_start);
    if (segment.empty() || segment == "." || segment == "..")
      return true;
    segment_start = index + 1U;
  }
  return false;
}

[[nodiscard]] bool IsValidKey(const std::string_view key,
                              const SaveDatabaseLimits &limits) noexcept {
  if (key.empty() || key.size() > limits.max_key_bytes || key.front() == '/' ||
      key.back() == '/') {
    return false;
  }
  if (std::ranges::any_of(key, [](const char value) {
        return !IsKeyByte(static_cast<unsigned char>(value));
      })) {
    return false;
  }
  return !HasInvalidSegment(key);
}

[[nodiscard]] bool IsValidPrefix(const std::string_view prefix,
                                 const SaveDatabaseLimits &limits) noexcept {
  if (prefix.empty())
    return true;
  if (prefix.size() > limits.max_key_bytes || prefix.front() == '/')
    return false;
  if (std::ranges::any_of(prefix, [](const char value) {
        return !IsKeyByte(static_cast<unsigned char>(value));
      })) {
    return false;
  }
  const std::string_view body =
      prefix.back() == '/' ? prefix.substr(0U, prefix.size() - 1U) : prefix;
  return !body.empty() && !HasInvalidSegment(body);
}

[[nodiscard]] std::expected<void, SaveDatabaseError>
ValidateLimits(const SaveDatabaseConfig &config) {
  const SaveDatabaseLimits &limits = config.limits;
  if (config.directory.empty() || !config.directory.is_absolute()) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::InvalidConfiguration,
                  "save database directory must be a nonempty absolute path"));
  }
  if (limits.max_records == 0U ||
      limits.max_records > SaveDatabase::kHardMaxRecords ||
      limits.max_mutations_per_commit == 0U ||
      limits.max_mutations_per_commit >
          SaveDatabase::kHardMaxMutationsPerCommit ||
      limits.max_key_bytes == 0U ||
      limits.max_key_bytes > SaveDatabase::kHardMaxKeyBytes ||
      limits.max_value_bytes == 0U ||
      limits.max_value_bytes > SaveDatabase::kHardMaxValueBytes ||
      limits.max_logical_value_bytes == 0U ||
      limits.max_logical_value_bytes >
          SaveDatabase::kHardMaxLogicalValueBytes ||
      limits.max_file_bytes < kHeaderBytes ||
      limits.max_file_bytes > SaveDatabase::kHardMaxFileBytes) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::InvalidConfiguration,
                  "save database limits are zero or exceed a hard bound"));
  }
  if (limits.max_value_bytes > limits.max_logical_value_bytes ||
      limits.max_logical_value_bytes > limits.max_file_bytes) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::InvalidConfiguration,
                  "save database byte limits are not monotonically bounded"));
  }
  return {};
}

[[nodiscard]] std::filesystem::path SlotPath(const SaveDatabaseConfig &config,
                                             const Slot slot) {
  return config.directory / (slot == Slot::A ? kSaveDatabaseSlotAFileName
                                             : kSaveDatabaseSlotBFileName);
}

[[nodiscard]] std::filesystem::path LockPath(const SaveDatabaseConfig &config) {
  return config.directory / kSaveDatabaseLockFileName;
}

[[nodiscard]] bool
IsWindowsReparsePoint(const std::filesystem::path &path) noexcept {
#if defined(_WIN32)
  const DWORD attributes = GetFileAttributesW(path.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0U;
#else
  static_cast<void>(path);
  return false;
#endif
}

[[nodiscard]] std::expected<void, SaveDatabaseError>
EnsureDirectory(const SaveDatabaseConfig &config) {
  std::error_code error;
  const std::filesystem::file_status before =
      std::filesystem::symlink_status(config.directory, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to inspect save database directory"));
  }
  if (!error && before.type() != std::filesystem::file_type::not_found) {
    if (std::filesystem::is_symlink(before) ||
        IsWindowsReparsePoint(config.directory) ||
        !std::filesystem::is_directory(before)) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidConfiguration,
                    "save database path is not a regular directory"));
    }
    return {};
  }

  error.clear();
  std::filesystem::create_directories(config.directory, error);
  if (error) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to create save database directory"));
  }
  const std::filesystem::file_status after =
      std::filesystem::symlink_status(config.directory, error);
  if (error || !std::filesystem::is_directory(after) ||
      std::filesystem::is_symlink(after) ||
      IsWindowsReparsePoint(config.directory)) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "created save database directory failed validation"));
  }
  return {};
}

[[nodiscard]] std::expected<void, SaveDatabaseError>
ValidateWritableLeaf(const std::filesystem::path &path) {
  std::error_code error;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(path, error);
  if (error && error != std::errc::no_such_file_or_directory) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to inspect save database snapshot leaf"));
  }
  if (error || status.type() == std::filesystem::file_type::not_found)
    return {};
  if (std::filesystem::is_symlink(status) || IsWindowsReparsePoint(path) ||
      !std::filesystem::is_regular_file(status)) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "save database snapshot leaf is not a regular file"));
  }
  return {};
}

class DatabaseLock final {
public:
  DatabaseLock() noexcept = default;
  ~DatabaseLock() noexcept { Release(); }

  DatabaseLock(DatabaseLock &&other) noexcept { MoveFrom(other); }

  DatabaseLock &operator=(DatabaseLock &&other) noexcept {
    if (this != &other) {
      Release();
      MoveFrom(other);
    }
    return *this;
  }

  DatabaseLock(const DatabaseLock &) = delete;
  DatabaseLock &operator=(const DatabaseLock &) = delete;

  [[nodiscard]] static std::expected<DatabaseLock, SaveDatabaseError>
  Acquire(const std::filesystem::path &path) {
    if (auto leaf = ValidateWritableLeaf(path); !leaf)
      return std::unexpected(std::move(leaf.error()));

#if defined(_WIN32)
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const DWORD code = GetLastError();
      return std::unexpected(MakeError(
          code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION
              ? SaveDatabaseErrorCode::Busy
              : SaveDatabaseErrorCode::IoFailure,
          code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION
              ? "save database is already owned by another process"
              : "unable to acquire save database ownership"));
    }
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                     sizeof(attributes)) == FALSE ||
        (attributes.FileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U) {
      static_cast<void>(CloseHandle(handle));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "save database lock leaf is not a regular file"));
    }
    DatabaseLock lock;
    lock.handle_ = handle;
    return lock;
#else
    const int file =
        ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW,
               S_IRUSR | S_IWUSR);
    if (file < 0) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "unable to acquire save database ownership"));
    }
    struct stat status {};
    if (::fstat(file, &status) != 0 || !S_ISREG(status.st_mode)) {
      static_cast<void>(::close(file));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "save database lock leaf is not a regular file"));
    }
    if (::flock(file, LOCK_EX | LOCK_NB) != 0) {
      const int code = errno;
      static_cast<void>(::close(file));
      return std::unexpected(
          MakeError(code == EWOULDBLOCK || code == EAGAIN
                        ? SaveDatabaseErrorCode::Busy
                        : SaveDatabaseErrorCode::IoFailure,
                    code == EWOULDBLOCK || code == EAGAIN
                        ? "save database is already owned by another process"
                        : "unable to acquire save database ownership"));
    }
    DatabaseLock lock;
    lock.file_ = file;
    return lock;
#endif
  }

private:
  void Release() noexcept {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) {
      static_cast<void>(CloseHandle(handle_));
      handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (file_ >= 0) {
      static_cast<void>(::flock(file_, LOCK_UN));
      static_cast<void>(::close(file_));
      file_ = -1;
    }
#endif
  }

  void MoveFrom(DatabaseLock &other) noexcept {
#if defined(_WIN32)
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
#else
    file_ = other.file_;
    other.file_ = -1;
#endif
  }

#if defined(_WIN32)
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int file_ = -1;
#endif
};

[[nodiscard]] std::expected<void, SaveDatabaseError>
WriteDurableFile(const std::filesystem::path &path,
                 const std::span<const std::byte> bytes) {
  if (auto leaf = ValidateWritableLeaf(path); !leaf)
    return leaf;
#if defined(_WIN32)
  HANDLE file =
      CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::unexpected(MakeError(
        SaveDatabaseErrorCode::IoFailure,
        "unable to open inactive save database snapshot for writing"));
  }
  bool ok = true;
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const std::size_t remaining = bytes.size() - offset;
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        remaining,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD written = 0U;
    if (WriteFile(file, bytes.data() + offset, request, &written, nullptr) ==
            FALSE ||
        written != request) {
      ok = false;
      break;
    }
    offset += written;
  }
  if (ok && FlushFileBuffers(file) == FALSE)
    ok = false;
  if (CloseHandle(file) == FALSE)
    ok = false;
  if (!ok) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to durably write inactive save database snapshot"));
  }
#else
  const int file =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
             S_IRUSR | S_IWUSR);
  if (file < 0) {
    return std::unexpected(MakeError(
        SaveDatabaseErrorCode::IoFailure,
        "unable to open inactive save database snapshot for writing"));
  }
  bool ok = true;
  std::size_t offset = 0U;
  while (offset < bytes.size()) {
    const ssize_t written =
        ::write(file, bytes.data() + offset, bytes.size() - offset);
    if (written < 0 && errno == EINTR)
      continue;
    if (written <= 0) {
      ok = false;
      break;
    }
    offset += static_cast<std::size_t>(written);
  }
  if (ok) {
    while (::fsync(file) != 0) {
      if (errno != EINTR) {
        ok = false;
        break;
      }
    }
  }
  if (::close(file) != 0)
    ok = false;
  if (!ok) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to durably write inactive save database snapshot"));
  }
#endif
  return {};
}

[[nodiscard]] std::expected<std::vector<std::byte>, SaveDatabaseError>
EncodeSnapshot(const RecordMap &records, const std::uint64_t generation,
               const SaveDatabaseLimits &limits) {
  if (records.size() > limits.max_records) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::LimitExceeded,
                  "save database record count exceeds its configured limit"));
  }

  std::size_t payload_bytes = 0U;
  std::size_t logical_bytes = 0U;
  for (const auto &[key, record] : records) {
    if (!IsValidKey(key, limits) || record.schema_version == 0U ||
        record.revision == 0U || record.value.size() > limits.max_value_bytes) {
      return std::unexpected(MakeError(
          SaveDatabaseErrorCode::CorruptSnapshot,
          "in-memory save database state violates its format contract"));
    }
    std::size_t record_bytes = kRecordHeaderBytes;
    if (!CheckedAdd(record_bytes, key.size(), &record_bytes) ||
        !CheckedAdd(record_bytes, record.value.size(), &record_bytes) ||
        !CheckedAdd(payload_bytes, record_bytes, &payload_bytes) ||
        !CheckedAdd(logical_bytes, record.value.size(), &logical_bytes)) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::LimitExceeded,
                    "save database snapshot size arithmetic overflowed"));
    }
  }
  std::size_t file_bytes = 0U;
  if (logical_bytes > limits.max_logical_value_bytes ||
      !CheckedAdd(kHeaderBytes, payload_bytes, &file_bytes) ||
      file_bytes > limits.max_file_bytes) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::LimitExceeded,
                  "save database snapshot exceeds its configured byte limits"));
  }

  std::vector<std::byte> bytes(file_bytes, std::byte{0});
  std::size_t cursor = kHeaderBytes;
  for (const auto &[key, record] : records) {
    StoreU16(bytes, cursor, static_cast<std::uint16_t>(key.size()));
    StoreU16(bytes, cursor + 2U, 0U);
    StoreU32(bytes, cursor + 4U, record.schema_version);
    StoreU64(bytes, cursor + 8U, record.revision);
    StoreU64(bytes, cursor + 16U,
             static_cast<std::uint64_t>(record.value.size()));
    StoreU32(bytes, cursor + 24U, Crc32(record.value));
    StoreU32(bytes, cursor + 28U, 0U);
    cursor += kRecordHeaderBytes;
    std::ranges::transform(
        key, bytes.begin() + static_cast<std::ptrdiff_t>(cursor),
        [](const char value) { return static_cast<std::byte>(value); });
    cursor += key.size();
    std::ranges::copy(record.value,
                      bytes.begin() + static_cast<std::ptrdiff_t>(cursor));
    cursor += record.value.size();
  }

  std::ranges::copy(kMagic, bytes.begin());
  StoreU32(bytes, kHeaderVersionOffset, kSaveDatabaseFormatVersion);
  StoreU32(bytes, kHeaderSizeOffset, static_cast<std::uint32_t>(kHeaderBytes));
  StoreU32(bytes, kHeaderEndianOffset, kEndianMarker);
  StoreU32(bytes, kHeaderReserved0Offset, 0U);
  StoreU64(bytes, kHeaderGenerationOffset, generation);
  StoreU32(bytes, kHeaderRecordCountOffset,
           static_cast<std::uint32_t>(records.size()));
  StoreU32(bytes, kHeaderReserved1Offset, 0U);
  StoreU64(bytes, kHeaderPayloadSizeOffset,
           static_cast<std::uint64_t>(payload_bytes));
  StoreU32(bytes, kHeaderPayloadCrcOffset,
           Crc32(std::span<const std::byte>(bytes).subspan(kHeaderBytes)));
  StoreU32(bytes, kHeaderCrcOffset, 0U);
  StoreU64(bytes, kHeaderReserved2Offset, 0U);
  StoreU32(bytes, kHeaderCrcOffset,
           Crc32(std::span<const std::byte>(bytes).first(kHeaderBytes)));
  return bytes;
}

[[nodiscard]] std::expected<DecodedSnapshot, SaveDatabaseError>
DecodeSnapshot(const std::span<const std::byte> bytes,
               const SaveDatabaseLimits &limits) {
  if (bytes.size() < kHeaderBytes) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot is shorter than its fixed header"));
  }
  if (!std::ranges::equal(kMagic, bytes.first(kMagic.size()))) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot magic is invalid"));
  }
  if (LoadU32(bytes, kHeaderVersionOffset) != kSaveDatabaseFormatVersion) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::UnsupportedFormat,
                  "save database snapshot format version is unsupported"));
  }
  if (LoadU32(bytes, kHeaderSizeOffset) != kHeaderBytes ||
      LoadU32(bytes, kHeaderEndianOffset) != kEndianMarker ||
      LoadU32(bytes, kHeaderReserved0Offset) != 0U ||
      LoadU32(bytes, kHeaderReserved1Offset) != 0U ||
      LoadU64(bytes, kHeaderReserved2Offset) != 0U) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot header fields are invalid"));
  }
  std::array<std::byte, kHeaderBytes> header{};
  std::ranges::copy(bytes.first(kHeaderBytes), header.begin());
  const std::uint32_t stored_header_crc = LoadU32(header, kHeaderCrcOffset);
  StoreU32(header, kHeaderCrcOffset, 0U);
  if (Crc32(header) != stored_header_crc) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot header checksum is invalid"));
  }
  const std::uint64_t payload_bytes64 =
      LoadU64(bytes, kHeaderPayloadSizeOffset);
  if (payload_bytes64 > std::numeric_limits<std::size_t>::max()) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot payload size is unrepresentable"));
  }
  const std::size_t payload_bytes = static_cast<std::size_t>(payload_bytes64);
  std::size_t expected_file_bytes = 0U;
  if (!CheckedAdd(kHeaderBytes, payload_bytes, &expected_file_bytes) ||
      expected_file_bytes != bytes.size() ||
      bytes.size() > limits.max_file_bytes) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot extent is invalid"));
  }
  const auto payload = bytes.subspan(kHeaderBytes);
  if (Crc32(payload) != LoadU32(bytes, kHeaderPayloadCrcOffset)) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot payload checksum is invalid"));
  }

  const std::uint32_t record_count = LoadU32(bytes, kHeaderRecordCountOffset);
  if (record_count > limits.max_records) {
    return std::unexpected(MakeError(
        SaveDatabaseErrorCode::LimitExceeded,
        "save database snapshot record count exceeds its configured limit"));
  }

  DecodedSnapshot decoded;
  decoded.generation = LoadU64(bytes, kHeaderGenerationOffset);
  std::size_t cursor = kHeaderBytes;
  std::string previous_key;
  for (std::uint32_t index = 0U; index < record_count; ++index) {
    std::size_t header_end = 0U;
    if (!CheckedAdd(cursor, kRecordHeaderBytes, &header_end) ||
        header_end > bytes.size()) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                    "save database record header overruns the snapshot"));
    }
    const std::uint16_t key_bytes = LoadU16(bytes, cursor);
    const std::uint16_t flags = LoadU16(bytes, cursor + 2U);
    const std::uint32_t schema_version = LoadU32(bytes, cursor + 4U);
    const std::uint64_t revision = LoadU64(bytes, cursor + 8U);
    const std::uint64_t value_bytes64 = LoadU64(bytes, cursor + 16U);
    const std::uint32_t value_crc = LoadU32(bytes, cursor + 24U);
    const std::uint32_t reserved = LoadU32(bytes, cursor + 28U);
    if (flags != 0U || reserved != 0U || schema_version == 0U ||
        revision == 0U || key_bytes == 0U || key_bytes > limits.max_key_bytes ||
        value_bytes64 > limits.max_value_bytes ||
        value_bytes64 > std::numeric_limits<std::size_t>::max()) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                    "save database record fields are invalid"));
    }
    const std::size_t value_bytes = static_cast<std::size_t>(value_bytes64);
    std::size_t key_end = 0U;
    std::size_t value_end = 0U;
    if (!CheckedAdd(header_end, key_bytes, &key_end) ||
        !CheckedAdd(key_end, value_bytes, &value_end) ||
        value_end > bytes.size()) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                    "save database record extent overruns the snapshot"));
    }
    std::string key;
    key.resize(key_bytes);
    for (std::size_t key_index = 0U; key_index < key_bytes; ++key_index)
      key[key_index] =
          static_cast<char>(ByteValue(bytes[header_end + key_index]));
    if (!IsValidKey(key, limits) ||
        (!previous_key.empty() && key <= previous_key)) {
      return std::unexpected(MakeError(
          SaveDatabaseErrorCode::CorruptSnapshot,
          "save database record keys are invalid, duplicated, or unsorted"));
    }
    std::vector<std::byte> value(value_bytes);
    std::ranges::copy(bytes.subspan(key_end, value_bytes), value.begin());
    if (Crc32(value) != value_crc ||
        !CheckedAdd(decoded.logical_value_bytes, value.size(),
                    &decoded.logical_value_bytes) ||
        decoded.logical_value_bytes > limits.max_logical_value_bytes) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                    "save database record value is corrupt or over budget"));
    }
    decoded.records.emplace(key, StoredRecord{.schema_version = schema_version,
                                              .revision = revision,
                                              .value = std::move(value)});
    previous_key = std::move(key);
    cursor = value_end;
  }
  if (cursor != bytes.size()) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot has unclaimed trailing bytes"));
  }
  return decoded;
}

[[nodiscard]] SlotReadResult ReadSlot(const SaveDatabaseConfig &config,
                                      const Slot slot) {
  const std::filesystem::path path = SlotPath(config, slot);
  std::error_code error;
  const std::filesystem::file_status status =
      std::filesystem::symlink_status(path, error);
  if (error == std::errc::no_such_file_or_directory ||
      (!error && status.type() == std::filesystem::file_type::not_found)) {
    return {};
  }
  if (error) {
    return {.status = SlotReadStatus::Invalid,
            .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                               "unable to inspect save database snapshot")};
  }
  if (std::filesystem::is_symlink(status) || IsWindowsReparsePoint(path) ||
      !std::filesystem::is_regular_file(status)) {
    return {.status = SlotReadStatus::Invalid,
            .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                               "save database snapshot is not a regular file")};
  }
  const std::uintmax_t file_size = std::filesystem::file_size(path, error);
  if (error) {
    return {.status = SlotReadStatus::Invalid,
            .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                               "unable to measure save database snapshot")};
  }
  if (file_size > config.limits.max_file_bytes ||
      file_size > std::numeric_limits<std::size_t>::max()) {
    return {.status = SlotReadStatus::Invalid,
            .error = MakeError(
                SaveDatabaseErrorCode::LimitExceeded,
                "save database snapshot exceeds its configured file limit")};
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {.status = SlotReadStatus::Invalid,
            .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                               "unable to open save database snapshot")};
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(file_size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  }
  if ((!bytes.empty() &&
       input.gcount() != static_cast<std::streamsize>(bytes.size())) ||
      input.bad()) {
    return {.status = SlotReadStatus::Invalid,
            .error =
                MakeError(SaveDatabaseErrorCode::IoFailure,
                          "unable to read complete save database snapshot")};
  }
  auto decoded = DecodeSnapshot(bytes, config.limits);
  if (!decoded) {
    return {.status = SlotReadStatus::Invalid,
            .error = std::move(decoded.error())};
  }
  return {.status = SlotReadStatus::Valid, .snapshot = std::move(*decoded)};
}

[[nodiscard]] bool
SatisfiesCondition(const SaveWriteCondition condition,
                   const RecordMap::const_iterator found,
                   const RecordMap::const_iterator end) noexcept {
  const bool exists = found != end;
  switch (condition.kind) {
  case SaveWriteConditionKind::Unconditional:
    return true;
  case SaveWriteConditionKind::MustBeAbsent:
    return !exists;
  case SaveWriteConditionKind::MustExist:
    return exists;
  case SaveWriteConditionKind::ExactRevision:
    return exists && condition.revision != 0U &&
           found->second.revision == condition.revision;
  }
  return false;
}

[[nodiscard]] bool
IsValidCondition(const SaveWriteCondition condition) noexcept {
  switch (condition.kind) {
  case SaveWriteConditionKind::Unconditional:
  case SaveWriteConditionKind::MustBeAbsent:
  case SaveWriteConditionKind::MustExist:
    return condition.revision == 0U;
  case SaveWriteConditionKind::ExactRevision:
    return condition.revision != 0U;
  }
  return false;
}

[[nodiscard]] Slot OtherSlot(const Slot slot) noexcept {
  return slot == Slot::A ? Slot::B : Slot::A;
}
} // namespace

struct SaveDatabase::Impl {
  explicit Impl(DatabaseLock acquired_lock) noexcept
      : database_lock(std::move(acquired_lock)) {}

  DatabaseLock database_lock;
  SaveDatabaseConfig config;
  Slot active_slot = Slot::A;
  std::uint64_t generation = 0U;
  RecordMap records;
  std::size_t logical_value_bytes = 0U;
};

std::string_view
SaveDatabaseErrorCodeName(const SaveDatabaseErrorCode code) noexcept {
  switch (code) {
  case SaveDatabaseErrorCode::InvalidConfiguration:
    return "invalid-configuration";
  case SaveDatabaseErrorCode::InvalidKey:
    return "invalid-key";
  case SaveDatabaseErrorCode::InvalidMutation:
    return "invalid-mutation";
  case SaveDatabaseErrorCode::PreconditionFailed:
    return "precondition-failed";
  case SaveDatabaseErrorCode::LimitExceeded:
    return "limit-exceeded";
  case SaveDatabaseErrorCode::Busy:
    return "busy";
  case SaveDatabaseErrorCode::IoFailure:
    return "io-failure";
  case SaveDatabaseErrorCode::CorruptSnapshot:
    return "corrupt-snapshot";
  case SaveDatabaseErrorCode::UnsupportedFormat:
    return "unsupported-format";
  case SaveDatabaseErrorCode::GenerationExhausted:
    return "generation-exhausted";
  case SaveDatabaseErrorCode::InvalidState:
    return "invalid-state";
  }
  return "invalid-state";
}

SaveMutation SaveMutation::Put(std::string key,
                               const std::uint32_t schema_version,
                               std::vector<std::byte> value,
                               const SaveWriteCondition condition) {
  return {
      .kind = SaveMutationKind::Put,
      .key = std::move(key),
      .schema_version = schema_version,
      .value = std::move(value),
      .condition = condition,
  };
}

SaveMutation SaveMutation::Erase(std::string key,
                                 const SaveWriteCondition condition) {
  return {
      .kind = SaveMutationKind::Erase,
      .key = std::move(key),
      .condition = condition,
  };
}

SaveDatabase::SaveDatabase(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl)) {}

SaveDatabase::~SaveDatabase() = default;
SaveDatabase::SaveDatabase(SaveDatabase &&) noexcept = default;
SaveDatabase &SaveDatabase::operator=(SaveDatabase &&) noexcept = default;

std::expected<SaveDatabase, SaveDatabaseError>
SaveDatabase::Open(SaveDatabaseConfig config) {
  try {
    if (auto valid = ValidateLimits(config); !valid)
      return std::unexpected(std::move(valid.error()));
    if (auto directory = EnsureDirectory(config); !directory)
      return std::unexpected(std::move(directory.error()));
    auto database_lock = DatabaseLock::Acquire(LockPath(config));
    if (!database_lock)
      return std::unexpected(std::move(database_lock.error()));

    SlotReadResult a = ReadSlot(config, Slot::A);
    SlotReadResult b = ReadSlot(config, Slot::B);
    const auto unsupported = [](const SlotReadResult &slot) {
      return slot.error &&
             slot.error->code == SaveDatabaseErrorCode::UnsupportedFormat;
    };
    if (unsupported(a))
      return std::unexpected(std::move(*a.error));
    if (unsupported(b))
      return std::unexpected(std::move(*b.error));

    if (a.status == SlotReadStatus::Missing &&
        b.status == SlotReadStatus::Missing) {
      RecordMap empty;
      auto genesis = EncodeSnapshot(empty, 0U, config.limits);
      if (!genesis)
        return std::unexpected(std::move(genesis.error()));
      if (auto write = WriteDurableFile(SlotPath(config, Slot::A), *genesis);
          !write)
        return std::unexpected(std::move(write.error()));
      if (auto write = WriteDurableFile(SlotPath(config, Slot::B), *genesis);
          !write)
        return std::unexpected(std::move(write.error()));
      a = ReadSlot(config, Slot::A);
      b = ReadSlot(config, Slot::B);
    }

    if (a.status != SlotReadStatus::Valid &&
        b.status != SlotReadStatus::Valid) {
      if (a.error)
        return std::unexpected(std::move(*a.error));
      if (b.error)
        return std::unexpected(std::move(*b.error));
      return std::unexpected(MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                                       "save database has no valid snapshot"));
    }

    Slot selected_slot = Slot::A;
    DecodedSnapshot selected;
    if (a.status == SlotReadStatus::Valid &&
        b.status == SlotReadStatus::Valid) {
      if (a.snapshot->generation == b.snapshot->generation &&
          a.snapshot->records != b.snapshot->records) {
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                      "save database snapshots disagree at one generation"));
      }
      if (b.snapshot->generation > a.snapshot->generation) {
        selected_slot = Slot::B;
        selected = std::move(*b.snapshot);
      } else {
        selected = std::move(*a.snapshot);
      }
    } else if (a.status == SlotReadStatus::Valid) {
      selected = std::move(*a.snapshot);
    } else {
      selected_slot = Slot::B;
      selected = std::move(*b.snapshot);
    }

    auto impl = std::make_unique<Impl>(std::move(*database_lock));
    impl->config = std::move(config);
    impl->active_slot = selected_slot;
    impl->generation = selected.generation;
    impl->logical_value_bytes = selected.logical_value_bytes;
    impl->records = std::move(selected.records);
    return SaveDatabase(std::move(impl));
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::LimitExceeded,
                  "save database allocation failed within validated limits"));
  } catch (...) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "save database startup failed unexpectedly"));
  }
}

std::expected<std::optional<SaveRecord>, SaveDatabaseError>
SaveDatabase::Read(const std::string_view key) const {
  try {
    if (!impl_) {
      return std::unexpected(MakeError(SaveDatabaseErrorCode::InvalidState,
                                       "save database is moved-from"));
    }
    if (!IsValidKey(key, impl_->config.limits)) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidKey,
                    "save database key violates the canonical key grammar"));
    }
    const auto found = impl_->records.find(key);
    if (found == impl_->records.end())
      return std::optional<SaveRecord>{};
    return std::optional<SaveRecord>{SaveRecord{
        .key = found->first,
        .schema_version = found->second.schema_version,
        .revision = found->second.revision,
        .value = found->second.value,
    }};
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeError(SaveDatabaseErrorCode::LimitExceeded,
                                     "save database read allocation failed"));
  }
}

std::expected<std::vector<SaveRecordInfo>, SaveDatabaseError>
SaveDatabase::List(const std::string_view prefix) const {
  try {
    if (!impl_) {
      return std::unexpected(MakeError(SaveDatabaseErrorCode::InvalidState,
                                       "save database is moved-from"));
    }
    if (!IsValidPrefix(prefix, impl_->config.limits)) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidKey,
                    "save database prefix violates the canonical key grammar"));
    }
    std::vector<SaveRecordInfo> result;
    result.reserve(impl_->records.size());
    auto iterator = prefix.empty() ? impl_->records.begin()
                                   : impl_->records.lower_bound(prefix);
    for (; iterator != impl_->records.end(); ++iterator) {
      if (!prefix.empty() && !iterator->first.starts_with(prefix))
        break;
      result.push_back({
          .key = iterator->first,
          .schema_version = iterator->second.schema_version,
          .revision = iterator->second.revision,
          .value_bytes = iterator->second.value.size(),
      });
    }
    return result;
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeError(SaveDatabaseErrorCode::LimitExceeded,
                                     "save database list allocation failed"));
  }
}

std::expected<std::uint64_t, SaveDatabaseError>
SaveDatabase::Commit(const std::span<const SaveMutation> mutations) {
  try {
    if (!impl_) {
      return std::unexpected(MakeError(SaveDatabaseErrorCode::InvalidState,
                                       "save database is moved-from"));
    }
    if (mutations.empty() ||
        mutations.size() > impl_->config.limits.max_mutations_per_commit) {
      return std::unexpected(MakeError(SaveDatabaseErrorCode::InvalidMutation,
                                       "save database mutation batch is empty "
                                       "or exceeds its configured limit"));
    }
    if (impl_->generation == std::numeric_limits<std::uint64_t>::max()) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::GenerationExhausted,
                    "save database generation is exhausted"));
    }

    RecordMap candidate = impl_->records;
    std::set<std::string, std::less<>> touched;
    for (const SaveMutation &mutation : mutations) {
      if (!IsValidKey(mutation.key, impl_->config.limits)) {
        return std::unexpected(MakeError(
            SaveDatabaseErrorCode::InvalidKey,
            "save database mutation key violates the canonical key grammar"));
      }
      if (!touched.emplace(mutation.key).second) {
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::InvalidMutation,
                      "save database mutation batch repeats a key"));
      }
      if (!IsValidCondition(mutation.condition)) {
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::InvalidMutation,
                      "save database mutation condition is invalid"));
      }
      auto found = candidate.find(mutation.key);
      if (!SatisfiesCondition(mutation.condition, found, candidate.end())) {
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::PreconditionFailed,
                      "save database mutation precondition failed"));
      }

      switch (mutation.kind) {
      case SaveMutationKind::Put: {
        if (mutation.schema_version == 0U ||
            mutation.value.size() > impl_->config.limits.max_value_bytes) {
          return std::unexpected(MakeError(
              SaveDatabaseErrorCode::InvalidMutation,
              "save database put has an invalid schema or over-budget value"));
        }
        if (found != candidate.end() &&
            found->second.revision ==
                std::numeric_limits<std::uint64_t>::max()) {
          return std::unexpected(
              MakeError(SaveDatabaseErrorCode::GenerationExhausted,
                        "save database record revision is exhausted"));
        }
        const std::uint64_t revision =
            found == candidate.end() ? 1U : found->second.revision + 1U;
        candidate[mutation.key] = StoredRecord{
            .schema_version = mutation.schema_version,
            .revision = revision,
            .value = mutation.value,
        };
        break;
      }
      case SaveMutationKind::Erase:
        if (mutation.schema_version != 0U || !mutation.value.empty()) {
          return std::unexpected(
              MakeError(SaveDatabaseErrorCode::InvalidMutation,
                        "save database erase carries put-only fields"));
        }
        if (found != candidate.end())
          candidate.erase(found);
        break;
      default:
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::InvalidMutation,
                      "save database mutation kind is invalid"));
      }
    }

    const std::uint64_t next_generation = impl_->generation + 1U;
    auto encoded =
        EncodeSnapshot(candidate, next_generation, impl_->config.limits);
    if (!encoded)
      return std::unexpected(std::move(encoded.error()));
    const Slot target = OtherSlot(impl_->active_slot);
    if (auto write =
            WriteDurableFile(SlotPath(impl_->config, target), *encoded);
        !write)
      return std::unexpected(std::move(write.error()));
    SlotReadResult verified = ReadSlot(impl_->config, target);
    if (verified.status != SlotReadStatus::Valid || !verified.snapshot ||
        verified.snapshot->generation != next_generation ||
        verified.snapshot->records != candidate) {
      return std::unexpected(verified.error
                                 ? std::move(*verified.error)
                                 : MakeError(SaveDatabaseErrorCode::IoFailure,
                                             "inactive save database snapshot "
                                             "failed readback verification"));
    }

    impl_->active_slot = target;
    impl_->generation = next_generation;
    impl_->logical_value_bytes = verified.snapshot->logical_value_bytes;
    impl_->records = std::move(verified.snapshot->records);
    return impl_->generation;
  } catch (const std::bad_alloc &) {
    return std::unexpected(MakeError(
        SaveDatabaseErrorCode::LimitExceeded,
        "save database commit allocation failed within validated limits"));
  } catch (...) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "save database commit failed unexpectedly"));
  }
}

std::uint64_t SaveDatabase::generation() const noexcept {
  return impl_ ? impl_->generation : 0U;
}

std::size_t SaveDatabase::record_count() const noexcept {
  return impl_ ? impl_->records.size() : 0U;
}

std::size_t SaveDatabase::logical_value_bytes() const noexcept {
  return impl_ ? impl_->logical_value_bytes : 0U;
}

const SaveDatabaseConfig *SaveDatabase::config() const noexcept {
  return impl_ ? &impl_->config : nullptr;
}
} // namespace omega::persistence
