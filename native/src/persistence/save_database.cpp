#include "omega/persistence/save_database.h"
#include "omega/debug/subsystem_entry_break.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
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
constexpr std::string_view kSnapshotTempMarker = ".tmp-";

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

struct SnapshotPublishFailure {
  SaveDatabaseError error;
  // True only when the atomic replacement completed but synchronizing the
  // containing directory failed. The caller must stop using its in-memory
  // view until the database is reopened and reconciled from disk.
  bool publication_may_have_succeeded = false;
};

[[nodiscard]] SaveDatabaseError MakeError(const SaveDatabaseErrorCode code,
                                          std::string message) {
  return {.code = code, .message = std::move(message)};
}

[[nodiscard]] SaveDatabaseError
MakeFallbackError(const SaveDatabaseErrorCode code) noexcept {
  SaveDatabaseError error;
  error.code = code;
  return error;
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

#if defined(_WIN32)
[[nodiscard]] std::filesystem::path SlotPath(const SaveDatabaseConfig &config,
                                             const Slot slot) {
  return config.directory / (slot == Slot::A ? kSaveDatabaseSlotAFileName
                                             : kSaveDatabaseSlotBFileName);
}

[[nodiscard]] std::filesystem::path LockPath(const SaveDatabaseConfig &config) {
  return config.directory / kSaveDatabaseLockFileName;
}
#endif

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
SynchronizeDirectory(const std::filesystem::path &path) {
#if defined(_WIN32)
  const HANDLE directory =
      CreateFileW(path.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
  if (directory == INVALID_HANDLE_VALUE) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to open a newly created directory for sync"));
  }
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  const bool valid =
      GetFileInformationByHandleEx(directory, FileAttributeTagInfo, &attributes,
                                   sizeof(attributes)) != FALSE &&
      (attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U;
  const bool flushed = valid && FlushFileBuffers(directory) != FALSE;
  static_cast<void>(CloseHandle(directory));
  if (!flushed) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to synchronize a newly created directory"));
  }
#else
  const int directory =
      ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_DIRECTORY);
  if (directory < 0) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to open a newly created directory for sync"));
  }
  bool flushed = true;
  while (::fsync(directory) != 0) {
    if (errno != EINTR) {
      flushed = false;
      break;
    }
  }
  static_cast<void>(::close(directory));
  if (!flushed) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to synchronize a newly created directory"));
  }
#endif
  return {};
}

[[nodiscard]] std::expected<void, SaveDatabaseError>
SynchronizeCreatedDirectoryChain(const std::filesystem::path &directory,
                                 const std::filesystem::path &stable_ancestor) {
  std::filesystem::path current = directory;
  for (;;) {
    if (auto synchronized = SynchronizeDirectory(current); !synchronized)
      return synchronized;
    if (current == stable_ancestor)
      return {};
    const std::filesystem::path parent = current.parent_path();
    if (parent.empty() || parent == current) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "new save database directory has no stable ancestor"));
    }
    current = parent;
  }
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

  std::filesystem::path stable_ancestor = config.directory.parent_path();
  for (;;) {
    error.clear();
    const std::filesystem::file_status ancestor_status =
        std::filesystem::symlink_status(stable_ancestor, error);
    if (!error &&
        ancestor_status.type() != std::filesystem::file_type::not_found) {
      if (!std::filesystem::is_directory(ancestor_status)) {
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::InvalidConfiguration,
                      "save database ancestor is not a directory"));
      }
      break;
    }
    if (error && error != std::errc::no_such_file_or_directory) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "unable to inspect save database ancestor"));
    }
    const std::filesystem::path parent = stable_ancestor.parent_path();
    if (parent.empty() || parent == stable_ancestor) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidConfiguration,
                    "save database has no existing directory ancestor"));
    }
    stable_ancestor = parent;
  }

  error.clear();
  const bool created =
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
#if !defined(_WIN32)
  if (created && ::chmod(config.directory.c_str(), S_IRWXU) != 0) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::IoFailure,
                  "unable to make the save database directory private"));
  }
#endif
  if (created) {
    if (auto synchronized =
            SynchronizeCreatedDirectoryChain(config.directory, stable_ancestor);
        !synchronized) {
      return synchronized;
    }
  }
  return {};
}

#if defined(_WIN32)
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
#endif

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
  Acquire(const SaveDatabaseConfig &config) {
#if defined(_WIN32)
    const HANDLE directory = CreateFileW(
        config.directory.c_str(), GENERIC_WRITE | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (directory == INVALID_HANDLE_VALUE) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "unable to anchor the save database directory"));
    }
    FILE_ATTRIBUTE_TAG_INFO directory_attributes{};
    if (GetFileInformationByHandleEx(directory, FileAttributeTagInfo,
                                     &directory_attributes,
                                     sizeof(directory_attributes)) == FALSE ||
        (directory_attributes.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) ==
            0U ||
        (directory_attributes.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) !=
            0U) {
      static_cast<void>(CloseHandle(directory));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "save database directory handle failed validation"));
    }

    const std::filesystem::path path = LockPath(config);
    const HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
      const DWORD code = GetLastError();
      static_cast<void>(CloseHandle(directory));
      return std::unexpected(MakeError(
          code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION
              ? SaveDatabaseErrorCode::Busy
              : SaveDatabaseErrorCode::IoFailure,
          code == ERROR_SHARING_VIOLATION || code == ERROR_LOCK_VIOLATION
              ? "save database is already owned by another process"
              : "unable to acquire save database ownership"));
    }
    FILE_ATTRIBUTE_TAG_INFO attributes{};
    BY_HANDLE_FILE_INFORMATION information{};
    if (GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &attributes,
                                     sizeof(attributes)) == FALSE ||
        GetFileInformationByHandle(handle, &information) == FALSE ||
        (attributes.FileAttributes &
         (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0U ||
        information.nNumberOfLinks != 1U) {
      static_cast<void>(CloseHandle(handle));
      static_cast<void>(CloseHandle(directory));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "save database lock leaf is not a regular file"));
    }
    DatabaseLock lock;
    lock.directory_handle_ = directory;
    lock.handle_ = handle;
    return lock;
#else
    const int directory =
        ::open(config.directory.c_str(),
               O_RDONLY | O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW);
    if (directory < 0) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "unable to anchor the save database directory"));
    }
    struct stat directory_status {};
    if (::fstat(directory, &directory_status) != 0 ||
        !S_ISDIR(directory_status.st_mode) ||
        (directory_status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
      static_cast<void>(::close(directory));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "save database directory handle failed validation"));
    }

    const int file =
        ::openat(directory, kSaveDatabaseLockFileName.data(),
                 O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (file < 0) {
      static_cast<void>(::close(directory));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "unable to acquire save database ownership"));
    }
    struct stat status {};
    if (::fstat(file, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_nlink != 1) {
      static_cast<void>(::close(file));
      static_cast<void>(::close(directory));
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::IoFailure,
                    "save database lock leaf is not a regular file"));
    }
    if (::flock(file, LOCK_EX | LOCK_NB) != 0) {
      const int code = errno;
      static_cast<void>(::close(file));
      static_cast<void>(::close(directory));
      return std::unexpected(
          MakeError(code == EWOULDBLOCK || code == EAGAIN
                        ? SaveDatabaseErrorCode::Busy
                        : SaveDatabaseErrorCode::IoFailure,
                    code == EWOULDBLOCK || code == EAGAIN
                        ? "save database is already owned by another process"
                        : "unable to acquire save database ownership"));
    }
    DatabaseLock lock;
    lock.directory_file_ = directory;
    lock.file_ = file;
    return lock;
#endif
  }

#if defined(_WIN32)
  [[nodiscard]] HANDLE directory_handle() const noexcept {
    return directory_handle_;
  }
#else
  [[nodiscard]] int directory_file() const noexcept { return directory_file_; }
#endif

private:
  void Release() noexcept {
#if defined(_WIN32)
    if (handle_ != INVALID_HANDLE_VALUE) {
      static_cast<void>(CloseHandle(handle_));
      handle_ = INVALID_HANDLE_VALUE;
    }
    if (directory_handle_ != INVALID_HANDLE_VALUE) {
      static_cast<void>(CloseHandle(directory_handle_));
      directory_handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (file_ >= 0) {
      static_cast<void>(::flock(file_, LOCK_UN));
      static_cast<void>(::close(file_));
      file_ = -1;
    }
    if (directory_file_ >= 0) {
      static_cast<void>(::close(directory_file_));
      directory_file_ = -1;
    }
#endif
  }

  void MoveFrom(DatabaseLock &other) noexcept {
#if defined(_WIN32)
    directory_handle_ = other.directory_handle_;
    other.directory_handle_ = INVALID_HANDLE_VALUE;
    handle_ = other.handle_;
    other.handle_ = INVALID_HANDLE_VALUE;
#else
    directory_file_ = other.directory_file_;
    other.directory_file_ = -1;
    file_ = other.file_;
    other.file_ = -1;
#endif
  }

#if defined(_WIN32)
  HANDLE directory_handle_ = INVALID_HANDLE_VALUE;
  HANDLE handle_ = INVALID_HANDLE_VALUE;
#else
  int directory_file_ = -1;
  int file_ = -1;
#endif
};

[[nodiscard]] std::string_view SlotFileName(const Slot slot) noexcept {
  return slot == Slot::A ? kSaveDatabaseSlotAFileName
                         : kSaveDatabaseSlotBFileName;
}

[[nodiscard]] std::string TemporaryLeafName(const Slot slot) {
  static std::atomic_uint64_t counter{0U};
  const auto ticks = static_cast<std::uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
#if defined(_WIN32)
  const auto process = static_cast<std::uint64_t>(GetCurrentProcessId());
#else
  const auto process = static_cast<std::uint64_t>(::getpid());
#endif
  std::string name(SlotFileName(slot));
  name.append(kSnapshotTempMarker);
  name.append(std::to_string(process));
  name.push_back('-');
  name.append(std::to_string(ticks));
  name.push_back('-');
  name.append(std::to_string(counter.fetch_add(1U, std::memory_order_relaxed)));
  return name;
}

[[nodiscard]] std::expected<void, SnapshotPublishFailure>
PublishSnapshot(const DatabaseLock &database_lock,
                const SaveDatabaseConfig &config, const Slot slot,
                const std::span<const std::byte> bytes) {
  const std::string temporary_leaf = TemporaryLeafName(slot);
#if defined(_WIN32)
  const std::filesystem::path target = SlotPath(config, slot);
  const std::filesystem::path temporary = config.directory / temporary_leaf;
  if (auto target_leaf = ValidateWritableLeaf(target); !target_leaf) {
    return std::unexpected(SnapshotPublishFailure{
        .error = std::move(target_leaf.error()),
    });
  }
  if (database_lock.directory_handle() == INVALID_HANDLE_VALUE) {
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(SaveDatabaseErrorCode::InvalidState,
                           "save database directory anchor is invalid"),
    });
  }
  HANDLE file =
      CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
                      FILE_FLAG_WRITE_THROUGH,
                  nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(
            SaveDatabaseErrorCode::IoFailure,
            "unable to create private save database snapshot temporary"),
    });
  }
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  BY_HANDLE_FILE_INFORMATION information{};
  bool ok =
      GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes,
                                   sizeof(attributes)) != FALSE &&
      GetFileInformationByHandle(file, &information) != FALSE &&
      (attributes.FileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0U &&
      information.nNumberOfLinks == 1U;
  std::size_t offset = 0U;
  while (ok && offset < bytes.size()) {
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
    static_cast<void>(DeleteFileW(temporary.c_str()));
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(
            SaveDatabaseErrorCode::IoFailure,
            "unable to durably write private save database snapshot temporary"),
    });
  }
  SnapshotPublishFailure replacement_failure{
      .error = MakeError(
          SaveDatabaseErrorCode::IoFailure,
          "save database snapshot replacement returned an indeterminate error"),
      .publication_may_have_succeeded = true,
  };
  SnapshotPublishFailure directory_sync_failure{
      .error = MakeError(
          SaveDatabaseErrorCode::IoFailure,
          "save database snapshot was published but directory sync failed"),
      .publication_may_have_succeeded = true,
  };
  if (MoveFileExW(temporary.c_str(), target.c_str(),
                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) ==
      FALSE) {
    static_cast<void>(DeleteFileW(temporary.c_str()));
    return std::unexpected(std::move(replacement_failure));
  }
  if (FlushFileBuffers(database_lock.directory_handle()) == FALSE)
    return std::unexpected(std::move(directory_sync_failure));
#else
  static_cast<void>(config);
  const int directory = database_lock.directory_file();
  if (directory < 0) {
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(SaveDatabaseErrorCode::InvalidState,
                           "save database directory anchor is invalid"),
    });
  }
  const std::string_view target_leaf = SlotFileName(slot);
  struct stat target_status {};
  if (::fstatat(directory, target_leaf.data(), &target_status,
                AT_SYMLINK_NOFOLLOW) == 0) {
    if (!S_ISREG(target_status.st_mode)) {
      return std::unexpected(SnapshotPublishFailure{
          .error =
              MakeError(SaveDatabaseErrorCode::IoFailure,
                        "save database snapshot leaf is not a regular file"),
      });
    }
  } else if (errno != ENOENT) {
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                           "unable to inspect save database snapshot leaf"),
    });
  }

  while (::fsync(directory) != 0) {
    if (errno == EINTR)
      continue;
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                           "unable to synchronize save database directory"),
    });
  }

  const int file = ::openat(
      directory, temporary_leaf.c_str(),
      O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
  if (file < 0) {
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(
            SaveDatabaseErrorCode::IoFailure,
            "unable to create private save database snapshot temporary"),
    });
  }
  struct stat temporary_status {};
  bool ok = ::fstat(file, &temporary_status) == 0 &&
            S_ISREG(temporary_status.st_mode) && temporary_status.st_nlink == 1;
  std::size_t offset = 0U;
  while (ok && offset < bytes.size()) {
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
    static_cast<void>(::unlinkat(directory, temporary_leaf.c_str(), 0));
    return std::unexpected(SnapshotPublishFailure{
        .error = MakeError(
            SaveDatabaseErrorCode::IoFailure,
            "unable to durably write private save database snapshot temporary"),
    });
  }

  // Construct this before rename so an allocation failure cannot obscure an
  // already-published snapshot.
  SnapshotPublishFailure directory_sync_failure{
      .error = MakeError(
          SaveDatabaseErrorCode::IoFailure,
          "save database snapshot was published but directory sync failed"),
      .publication_may_have_succeeded = true,
  };
  SnapshotPublishFailure replacement_failure{
      .error = MakeError(
          SaveDatabaseErrorCode::IoFailure,
          "save database snapshot replacement returned an indeterminate error"),
      .publication_may_have_succeeded = true,
  };
  if (::renameat(directory, temporary_leaf.c_str(), directory,
                 target_leaf.data()) != 0) {
    static_cast<void>(::unlinkat(directory, temporary_leaf.c_str(), 0));
    return std::unexpected(std::move(replacement_failure));
  }
  while (::fsync(directory) != 0) {
    if (errno != EINTR)
      return std::unexpected(std::move(directory_sync_failure));
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
  std::array<std::byte, kHeaderBytes> header{};
  std::ranges::copy(bytes.first(kHeaderBytes), header.begin());
  const std::uint32_t stored_header_crc = LoadU32(header, kHeaderCrcOffset);
  StoreU32(header, kHeaderCrcOffset, 0U);
  if (Crc32(header) != stored_header_crc) {
    return std::unexpected(
        MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                  "save database snapshot header checksum is invalid"));
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
  if (decoded.generation == 0U && record_count != 0U) {
    return std::unexpected(MakeError(
        SaveDatabaseErrorCode::CorruptSnapshot,
        "save database generation zero contains unreachable records"));
  }
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
        revision == 0U || revision > decoded.generation || key_bytes == 0U ||
        key_bytes > limits.max_key_bytes ||
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

[[nodiscard]] SlotReadResult ReadSlot(const DatabaseLock &database_lock,
                                      const SaveDatabaseConfig &config,
                                      const Slot slot) {
  std::uint64_t file_size = 0U;
  std::vector<std::byte> bytes;
#if defined(_WIN32)
  const std::filesystem::path path = SlotPath(config, slot);
  if (database_lock.directory_handle() == INVALID_HANDLE_VALUE) {
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = MakeError(SaveDatabaseErrorCode::InvalidState,
                               "save database directory anchor is invalid")};
  }
  const HANDLE file = CreateFileW(
      path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT |
          FILE_FLAG_SEQUENTIAL_SCAN,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    const DWORD code = GetLastError();
    if (code == ERROR_FILE_NOT_FOUND || code == ERROR_PATH_NOT_FOUND)
      return {};
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                               "unable to open save database snapshot")};
  }
  FILE_ATTRIBUTE_TAG_INFO attributes{};
  BY_HANDLE_FILE_INFORMATION information{};
  bool ok =
      GetFileInformationByHandleEx(file, FileAttributeTagInfo, &attributes,
                                   sizeof(attributes)) != FALSE &&
      GetFileInformationByHandle(file, &information) != FALSE &&
      (attributes.FileAttributes &
       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) == 0U &&
      information.nNumberOfLinks == 1U;
  if (ok) {
    file_size = (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) |
                information.nFileSizeLow;
    ok = file_size <= config.limits.max_file_bytes &&
         file_size <= std::numeric_limits<std::size_t>::max();
  }
  if (ok)
    bytes.resize(static_cast<std::size_t>(file_size));
  std::size_t offset = 0U;
  while (ok && offset < bytes.size()) {
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        bytes.size() - offset,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD read = 0U;
    if (ReadFile(file, bytes.data() + offset, request, &read, nullptr) ==
            FALSE ||
        read != request) {
      ok = false;
      break;
    }
    offset += read;
  }
  if (CloseHandle(file) == FALSE)
    ok = false;
  if (!ok) {
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = MakeError(
                file_size > config.limits.max_file_bytes
                    ? SaveDatabaseErrorCode::LimitExceeded
                    : SaveDatabaseErrorCode::IoFailure,
                file_size > config.limits.max_file_bytes
                    ? "save database snapshot exceeds its configured file limit"
                    : "save database snapshot handle or read is invalid")};
  }
#else
  const int directory = database_lock.directory_file();
  if (directory < 0) {
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = MakeError(SaveDatabaseErrorCode::InvalidState,
                               "save database directory anchor is invalid")};
  }
  const std::string_view leaf = SlotFileName(slot);
  const int file =
      ::openat(directory, leaf.data(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (file < 0) {
    if (errno == ENOENT)
      return {};
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = MakeError(SaveDatabaseErrorCode::IoFailure,
                               "unable to open save database snapshot")};
  }
  struct stat status {};
  bool ok = ::fstat(file, &status) == 0 && S_ISREG(status.st_mode) &&
            status.st_nlink == 1 && status.st_size >= 0;
  if (ok) {
    file_size = static_cast<std::uint64_t>(status.st_size);
    ok = file_size <= config.limits.max_file_bytes &&
         file_size <= std::numeric_limits<std::size_t>::max();
  }
  if (ok)
    bytes.resize(static_cast<std::size_t>(file_size));
  std::size_t offset = 0U;
  while (ok && offset < bytes.size()) {
    const ssize_t read =
        ::read(file, bytes.data() + offset, bytes.size() - offset);
    if (read < 0 && errno == EINTR)
      continue;
    if (read <= 0) {
      ok = false;
      break;
    }
    offset += static_cast<std::size_t>(read);
  }
  if (::close(file) != 0)
    ok = false;
  if (!ok) {
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = MakeError(
                file_size > config.limits.max_file_bytes
                    ? SaveDatabaseErrorCode::LimitExceeded
                    : SaveDatabaseErrorCode::IoFailure,
                file_size > config.limits.max_file_bytes
                    ? "save database snapshot exceeds its configured file limit"
                    : "save database snapshot handle or read is invalid")};
  }
#endif
  auto decoded = DecodeSnapshot(bytes, config.limits);
  if (!decoded) {
    return {.status = SlotReadStatus::Invalid,
            .snapshot = std::nullopt,
            .error = std::move(decoded.error())};
  }
  return {.status = SlotReadStatus::Valid,
          .snapshot = std::move(*decoded),
          .error = std::nullopt};
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
  bool usable = true;
};

std::string_view
SaveDatabaseErrorCodeName(const SaveDatabaseErrorCode code) noexcept {
  OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_persistence");
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
      .schema_version = 0U,
      .value = {},
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
    auto database_lock = DatabaseLock::Acquire(config);
    if (!database_lock)
      return std::unexpected(std::move(database_lock.error()));

    SlotReadResult a = ReadSlot(*database_lock, config, Slot::A);
    SlotReadResult b = ReadSlot(*database_lock, config, Slot::B);
    const auto fatal_read_error = [](const SlotReadResult &slot) {
      return slot.status == SlotReadStatus::Invalid && slot.error &&
             slot.error->code != SaveDatabaseErrorCode::CorruptSnapshot;
    };
    if (fatal_read_error(a))
      return std::unexpected(std::move(*a.error));
    if (fatal_read_error(b))
      return std::unexpected(std::move(*b.error));

    if (a.status == SlotReadStatus::Missing &&
        b.status == SlotReadStatus::Missing) {
      RecordMap empty;
      auto genesis = EncodeSnapshot(empty, 0U, config.limits);
      if (!genesis)
        return std::unexpected(std::move(genesis.error()));
      if (auto publish =
              PublishSnapshot(*database_lock, config, Slot::A, *genesis);
          !publish) {
        return std::unexpected(std::move(publish.error().error));
      }
      if (auto publish =
              PublishSnapshot(*database_lock, config, Slot::B, *genesis);
          !publish) {
        return std::unexpected(std::move(publish.error().error));
      }
      a = ReadSlot(*database_lock, config, Slot::A);
      b = ReadSlot(*database_lock, config, Slot::B);
      if (fatal_read_error(a))
        return std::unexpected(std::move(*a.error));
      if (fatal_read_error(b))
        return std::unexpected(std::move(*b.error));
    }

    const bool a_missing_b_valid = a.status == SlotReadStatus::Missing &&
                                   b.status == SlotReadStatus::Valid;
    const bool b_missing_a_valid = b.status == SlotReadStatus::Missing &&
                                   a.status == SlotReadStatus::Valid;
    if (a_missing_b_valid || b_missing_a_valid) {
      const DecodedSnapshot &survivor =
          a_missing_b_valid ? *b.snapshot : *a.snapshot;
      if (survivor.generation != 0U) {
        return std::unexpected(MakeError(
            SaveDatabaseErrorCode::CorruptSnapshot,
            "an established save database snapshot is unexpectedly missing"));
      }
      RecordMap empty;
      auto genesis = EncodeSnapshot(empty, 0U, config.limits);
      if (!genesis)
        return std::unexpected(std::move(genesis.error()));
      const Slot missing_slot = a_missing_b_valid ? Slot::A : Slot::B;
      if (auto publish =
              PublishSnapshot(*database_lock, config, missing_slot, *genesis);
          !publish) {
        return std::unexpected(std::move(publish.error().error));
      }
      SlotReadResult restored = ReadSlot(*database_lock, config, missing_slot);
      if (fatal_read_error(restored))
        return std::unexpected(std::move(*restored.error));
      if (restored.status != SlotReadStatus::Valid || !restored.snapshot) {
        return std::unexpected(
            MakeError(SaveDatabaseErrorCode::CorruptSnapshot,
                      "restored genesis snapshot failed validation"));
      }
      if (missing_slot == Slot::A)
        a = std::move(restored);
      else
        b = std::move(restored);
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
        MakeFallbackError(SaveDatabaseErrorCode::LimitExceeded));
  } catch (...) {
    return std::unexpected(MakeFallbackError(SaveDatabaseErrorCode::IoFailure));
  }
}

std::expected<std::optional<SaveRecord>, SaveDatabaseError>
SaveDatabase::Read(const std::string_view key) const {
  try {
    if (!impl_ || !impl_->usable) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidState,
                    "save database is moved-from or requires "
                    "reopen after an indeterminate write"));
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
    return std::unexpected(
        MakeFallbackError(SaveDatabaseErrorCode::LimitExceeded));
  }
}

std::expected<std::vector<SaveRecordInfo>, SaveDatabaseError>
SaveDatabase::List(const std::string_view prefix) const {
  try {
    if (!impl_ || !impl_->usable) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidState,
                    "save database is moved-from or requires "
                    "reopen after an indeterminate write"));
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
    return std::unexpected(
        MakeFallbackError(SaveDatabaseErrorCode::LimitExceeded));
  }
}

std::expected<std::uint64_t, SaveDatabaseError>
SaveDatabase::Commit(const std::span<const SaveMutation> mutations) {
  try {
    if (!impl_ || !impl_->usable) {
      return std::unexpected(
          MakeError(SaveDatabaseErrorCode::InvalidState,
                    "save database is moved-from or requires "
                    "reopen after an indeterminate write"));
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

    const std::uint64_t next_generation = impl_->generation + 1U;
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
        candidate[mutation.key] = StoredRecord{
            .schema_version = mutation.schema_version,
            .revision = next_generation,
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

    std::size_t candidate_logical_value_bytes = 0U;
    for (const auto &[key, record] : candidate) {
      static_cast<void>(key);
      if (!CheckedAdd(candidate_logical_value_bytes, record.value.size(),
                      &candidate_logical_value_bytes) ||
          candidate_logical_value_bytes >
              impl_->config.limits.max_logical_value_bytes) {
        return std::unexpected(MakeError(
            SaveDatabaseErrorCode::LimitExceeded,
            "save database candidate exceeds its configured logical limit"));
      }
    }

    auto encoded =
        EncodeSnapshot(candidate, next_generation, impl_->config.limits);
    if (!encoded)
      return std::unexpected(std::move(encoded.error()));
    const Slot target = OtherSlot(impl_->active_slot);
    if (auto publish = PublishSnapshot(impl_->database_lock, impl_->config,
                                       target, *encoded);
        !publish) {
      if (publish.error().publication_may_have_succeeded)
        impl_->usable = false;
      return std::unexpected(std::move(publish.error().error));
    }

    impl_->active_slot = target;
    impl_->generation = next_generation;
    impl_->logical_value_bytes = candidate_logical_value_bytes;
    impl_->records.swap(candidate);
    return impl_->generation;
  } catch (const std::bad_alloc &) {
    return std::unexpected(
        MakeFallbackError(SaveDatabaseErrorCode::LimitExceeded));
  } catch (...) {
    return std::unexpected(MakeFallbackError(SaveDatabaseErrorCode::IoFailure));
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
