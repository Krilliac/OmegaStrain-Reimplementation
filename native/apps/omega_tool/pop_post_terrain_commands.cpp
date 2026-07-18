#include "pop_post_terrain_commands.h"

#include "omega/asset/pop_terrain_index.h"
#include "omega/retail/pop_post_terrain_hypothesis_descriptor.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <Windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace omega::tool {
namespace {
constexpr std::uint64_t kMaximumWalkEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumDirectories = 1ULL << 16U;
constexpr std::uint32_t kMaximumTreeDepth = 32U;
constexpr std::uint64_t kMaximumPopCandidates = 4096U;
constexpr std::uint64_t kMaximumPathMetadataBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumPopBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumTotalPopBytes =
    4ULL * 1024ULL * 1024ULL * 1024ULL;

enum class ErrorCategory : std::size_t {
  DiscoveryInvalidRoot,
  DiscoveryUnsafeEntry,
  DiscoveryLimitExceeded,
  DiscoveryIo,
  NoPopCandidates,
  PopRead,
  DescriptorTruncated,
  DescriptorMalformed,
  DescriptorOverflow,
  DescriptorLimitExceeded,
  DescriptorUnsupportedVariant,
  DescriptorInvalidReference,
  DescriptorDuplicateReference,
  UsageMeasurement,
  AggregateOverflow,
  Count,
};

constexpr std::array<std::string_view,
                     static_cast<std::size_t>(ErrorCategory::Count)>
    kErrorCategoryNames{
        "discovery_invalid_root",
        "discovery_unsafe_entry",
        "discovery_limit_exceeded",
        "discovery_io",
        "no_pop_candidates",
        "pop_read",
        "descriptor_truncated",
        "descriptor_malformed",
        "descriptor_overflow",
        "descriptor_limit_exceeded",
        "descriptor_unsupported_variant",
        "descriptor_invalid_reference",
        "descriptor_duplicate_reference",
        "usage_measurement",
        "aggregate_overflow",
    };

using ErrorCounts =
    std::array<std::uint64_t, static_cast<std::size_t>(ErrorCategory::Count)>;

struct LogicalUsage {
  std::uint64_t input_bytes = 0;
  std::uint64_t items = 0;
  std::uint64_t logical_output_bytes = 0;
  std::uint64_t string_bytes = 0;
  std::uint64_t peak_scratch_bytes = 0;
};

struct Totals {
  std::uint64_t pop_candidates_discovered = 0;
  std::uint64_t descriptors_accepted = 0;
  std::uint64_t descriptors_rejected = 0;
};

struct Aggregate {
  Totals totals;
  LogicalUsage maxima;
  ErrorCounts errors{};
};

[[nodiscard]] bool Add(std::uint64_t &target,
                       const std::uint64_t value) noexcept {
  if (value > std::numeric_limits<std::uint64_t>::max() - target)
    return false;
  target += value;
  return true;
}

[[nodiscard]] bool Multiply(const std::uint64_t left, const std::uint64_t right,
                            std::uint64_t &result) noexcept {
  if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
    return false;
  result = left * right;
  return true;
}

void RecordError(ErrorCounts &errors, const ErrorCategory category) noexcept {
  auto &count = errors[static_cast<std::size_t>(category)];
  if (count != std::numeric_limits<std::uint64_t>::max())
    ++count;
}

[[nodiscard]] std::uint64_t ErrorTotal(const ErrorCounts &errors) noexcept {
  std::uint64_t total = 0;
  for (const std::uint64_t count : errors) {
    if (!Add(total, count))
      return std::numeric_limits<std::uint64_t>::max();
  }
  return total;
}

[[nodiscard]] unsigned char FoldAscii(const unsigned char value) noexcept {
  return value >= static_cast<unsigned char>('a') &&
                 value <= static_cast<unsigned char>('z')
             ? static_cast<unsigned char>(value - ('a' - 'A'))
             : value;
}

[[nodiscard]] bool IsPopCandidate(const std::filesystem::path &path) {
  const std::string extension = path.extension().string();
  constexpr std::string_view expected = ".POP";
  if (extension.size() != expected.size())
    return false;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (FoldAscii(static_cast<unsigned char>(extension[index])) !=
        static_cast<unsigned char>(expected[index]))
      return false;
  }
  return true;
}

[[nodiscard]] std::expected<bool, ErrorCategory>
IsReparsePoint(const std::filesystem::path &path) noexcept {
#ifdef _WIN32
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return std::unexpected(ErrorCategory::DiscoveryIo);
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  static_cast<void>(path);
  return false;
#endif
}

[[nodiscard]] std::expected<std::filesystem::file_status, ErrorCategory>
SafeStatus(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error)
    return std::unexpected(ErrorCategory::DiscoveryIo);
  auto reparse = IsReparsePoint(path);
  if (!reparse)
    return std::unexpected(reparse.error());
  if (std::filesystem::is_symlink(status) || *reparse)
    return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
  return status;
}

enum class StablePathKind : std::uint8_t {
  Directory,
  RegularFile,
};

enum class StablePathAccess : std::uint8_t {
  Metadata,
  Read,
};

enum class StablePathError : std::uint8_t {
  Unsafe,
  Io,
  Changed,
};

struct PathSnapshot {
#ifdef _WIN32
  std::uint64_t volume = 0;
  std::array<std::byte, 16> file_id{};
  std::uint64_t creation_time = 0;
  std::uint64_t last_write_time = 0;
  std::uint64_t change_time = 0;
  std::uint32_t attributes = 0;
#else
  std::uint64_t device = 0;
  std::uint64_t inode = 0;
  std::int64_t modification_seconds = 0;
  std::int64_t modification_nanoseconds = 0;
  std::int64_t change_seconds = 0;
  std::int64_t change_nanoseconds = 0;
  std::uint64_t mode = 0;
#endif
  std::uint64_t size = 0;
  std::uint64_t links = 0;
};

[[nodiscard]] bool SameSnapshot(const PathSnapshot &left,
                                const PathSnapshot &right) noexcept {
#ifdef _WIN32
  return left.volume == right.volume && left.file_id == right.file_id &&
         left.creation_time == right.creation_time &&
         left.last_write_time == right.last_write_time &&
         left.change_time == right.change_time &&
         left.attributes == right.attributes && left.size == right.size &&
         left.links == right.links;
#else
  return left.device == right.device && left.inode == right.inode &&
         left.modification_seconds == right.modification_seconds &&
         left.modification_nanoseconds == right.modification_nanoseconds &&
         left.change_seconds == right.change_seconds &&
         left.change_nanoseconds == right.change_nanoseconds &&
         left.mode == right.mode && left.size == right.size &&
         left.links == right.links;
#endif
}

class StablePathGuard {
public:
#ifdef _WIN32
  using NativeHandle = HANDLE;
#else
  using NativeHandle = int;
#endif

  [[nodiscard]] static NativeHandle InvalidHandle() noexcept {
#ifdef _WIN32
    return INVALID_HANDLE_VALUE;
#else
    return -1;
#endif
  }

  StablePathGuard() = default;
  StablePathGuard(NativeHandle handle, std::filesystem::path path,
                  const StablePathKind kind, PathSnapshot snapshot) noexcept
      : handle_(handle), path_(std::move(path)), kind_(kind),
        snapshot_(std::move(snapshot)) {}

  StablePathGuard(const StablePathGuard &) = delete;
  StablePathGuard &operator=(const StablePathGuard &) = delete;

  StablePathGuard(StablePathGuard &&other) noexcept
      : handle_(std::exchange(other.handle_, InvalidHandle())),
        path_(std::move(other.path_)), kind_(other.kind_),
        snapshot_(std::move(other.snapshot_)) {}

  StablePathGuard &operator=(StablePathGuard &&other) noexcept {
    if (this != &other) {
      Close();
      handle_ = std::exchange(other.handle_, InvalidHandle());
      path_ = std::move(other.path_);
      kind_ = other.kind_;
      snapshot_ = std::move(other.snapshot_);
    }
    return *this;
  }

  ~StablePathGuard() { Close(); }

  [[nodiscard]] NativeHandle native_handle() const noexcept { return handle_; }
  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return path_;
  }
  [[nodiscard]] StablePathKind kind() const noexcept { return kind_; }
  [[nodiscard]] const PathSnapshot &snapshot() const noexcept {
    return snapshot_;
  }

private:
  void Close() noexcept {
    if (handle_ == InvalidHandle())
      return;
#ifdef _WIN32
    static_cast<void>(CloseHandle(handle_));
#else
    static_cast<void>(::close(handle_));
#endif
    handle_ = InvalidHandle();
  }

  NativeHandle handle_ = InvalidHandle();
  std::filesystem::path path_;
  StablePathKind kind_ = StablePathKind::RegularFile;
  PathSnapshot snapshot_{};
};

#ifdef _WIN32
[[nodiscard]] std::expected<PathSnapshot, StablePathError>
QuerySnapshot(const StablePathGuard::NativeHandle handle,
              const StablePathKind expected_kind) noexcept {
  BY_HANDLE_FILE_INFORMATION information{};
  FILE_BASIC_INFO basic_information{};
  FILE_ID_INFO identity{};
  if (GetFileInformationByHandle(handle, &information) == FALSE ||
      GetFileInformationByHandleEx(handle, FileBasicInfo, &basic_information,
                                   sizeof(basic_information)) == FALSE)
    return std::unexpected(StablePathError::Io);

  const bool is_directory =
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  if ((information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
      is_directory != (expected_kind == StablePathKind::Directory))
    return std::unexpected(StablePathError::Unsafe);

  PathSnapshot snapshot{
      .volume = information.dwVolumeSerialNumber,
      .creation_time =
          static_cast<std::uint64_t>(basic_information.CreationTime.QuadPart),
      .last_write_time =
          static_cast<std::uint64_t>(basic_information.LastWriteTime.QuadPart),
      .change_time =
          static_cast<std::uint64_t>(basic_information.ChangeTime.QuadPart),
      .attributes = basic_information.FileAttributes,
      .size = (static_cast<std::uint64_t>(information.nFileSizeHigh) << 32U) |
              static_cast<std::uint64_t>(information.nFileSizeLow),
      .links = information.nNumberOfLinks,
  };
  if (GetFileInformationByHandleEx(handle, FileIdInfo, &identity,
                                   sizeof(identity)) != FALSE) {
    snapshot.volume = identity.VolumeSerialNumber;
    static_assert(sizeof(identity.FileId.Identifier) ==
                  sizeof(snapshot.file_id));
    for (std::size_t index = 0; index < snapshot.file_id.size(); ++index)
      snapshot.file_id[index] =
          static_cast<std::byte>(identity.FileId.Identifier[index]);
  } else {
    const std::uint64_t legacy_file_id =
        (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32U) |
        static_cast<std::uint64_t>(information.nFileIndexLow);
    for (std::size_t index = 0; index < sizeof(legacy_file_id); ++index)
      snapshot.file_id[index] =
          static_cast<std::byte>((legacy_file_id >> (index * 8U)) & 0xFFU);
  }
  return snapshot;
}
#else
[[nodiscard]] std::expected<PathSnapshot, StablePathError>
QuerySnapshot(const StablePathGuard::NativeHandle handle,
              const StablePathKind expected_kind) noexcept {
  struct stat information {};
  if (::fstat(handle, &information) != 0)
    return std::unexpected(StablePathError::Io);

  const bool expected_type = expected_kind == StablePathKind::Directory
                                 ? S_ISDIR(information.st_mode)
                                 : S_ISREG(information.st_mode);
  if (!expected_type || information.st_size < 0)
    return std::unexpected(StablePathError::Unsafe);

  PathSnapshot snapshot{
      .device = static_cast<std::uint64_t>(information.st_dev),
      .inode = static_cast<std::uint64_t>(information.st_ino),
#if defined(__APPLE__)
      .modification_seconds = information.st_mtimespec.tv_sec,
      .modification_nanoseconds = information.st_mtimespec.tv_nsec,
      .change_seconds = information.st_ctimespec.tv_sec,
      .change_nanoseconds = information.st_ctimespec.tv_nsec,
#else
      .modification_seconds = information.st_mtim.tv_sec,
      .modification_nanoseconds = information.st_mtim.tv_nsec,
      .change_seconds = information.st_ctim.tv_sec,
      .change_nanoseconds = information.st_ctim.tv_nsec,
#endif
      .mode = static_cast<std::uint64_t>(information.st_mode),
      .size = static_cast<std::uint64_t>(information.st_size),
      .links = static_cast<std::uint64_t>(information.st_nlink),
  };
  return snapshot;
}
#endif

[[nodiscard]] std::expected<StablePathGuard, StablePathError>
OpenStablePath(const std::filesystem::path &path, const StablePathKind kind,
               const StablePathAccess access) {
#ifdef _WIN32
  const DWORD desired_access = kind == StablePathKind::Directory
                                   ? FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES
                               : access == StablePathAccess::Read
                                   ? GENERIC_READ | FILE_READ_ATTRIBUTES
                                   : FILE_READ_ATTRIBUTES;
  const DWORD share_mode =
      kind == StablePathKind::RegularFile && access == StablePathAccess::Read
          ? FILE_SHARE_READ
          : FILE_SHARE_READ | FILE_SHARE_WRITE;
  DWORD flags = FILE_FLAG_OPEN_REPARSE_POINT;
  if (kind == StablePathKind::Directory)
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
  else if (access == StablePathAccess::Read)
    flags |= FILE_FLAG_SEQUENTIAL_SCAN;

  const HANDLE handle = CreateFileW(path.c_str(), desired_access, share_mode,
                                    nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE)
    return std::unexpected(StablePathError::Io);
  auto snapshot = QuerySnapshot(handle, kind);
  if (!snapshot) {
    static_cast<void>(CloseHandle(handle));
    return std::unexpected(snapshot.error());
  }
  return StablePathGuard(handle, path, kind, std::move(*snapshot));
#else
  int flags = O_RDONLY | O_CLOEXEC | O_NOFOLLOW;
  if (kind == StablePathKind::Directory)
    flags |= O_DIRECTORY;
  else if (access == StablePathAccess::Metadata)
    flags |= O_NONBLOCK;
  const int handle = ::open(path.c_str(), flags);
  if (handle < 0)
    return std::unexpected(errno == ELOOP ? StablePathError::Unsafe
                                          : StablePathError::Io);
  auto snapshot = QuerySnapshot(handle, kind);
  if (!snapshot) {
    static_cast<void>(::close(handle));
    return std::unexpected(snapshot.error());
  }
  return StablePathGuard(handle, path, kind, std::move(*snapshot));
#endif
}

[[nodiscard]] std::expected<void, StablePathError>
VerifyStablePath(const StablePathGuard &guard) {
  auto current = QuerySnapshot(guard.native_handle(), guard.kind());
  if (!current)
    return std::unexpected(current.error());
  if (!SameSnapshot(guard.snapshot(), *current))
    return std::unexpected(StablePathError::Changed);

  auto path_guard =
      OpenStablePath(guard.path(), guard.kind(), StablePathAccess::Metadata);
  if (!path_guard)
    return std::unexpected(path_guard.error());
  if (!SameSnapshot(guard.snapshot(), path_guard->snapshot()))
    return std::unexpected(StablePathError::Changed);
  return {};
}

[[nodiscard]] ErrorCategory
DiscoveryError(const StablePathError error) noexcept {
  return error == StablePathError::Unsafe ? ErrorCategory::DiscoveryUnsafeEntry
                                          : ErrorCategory::DiscoveryIo;
}

void InvokeHook(const PopPostTerrainCommandTestHooks &hooks,
                const PopPostTerrainCommandTestEvent event,
                const std::filesystem::path &path) {
  if (hooks.callback != nullptr)
    hooks.callback(event, path, hooks.context);
}

struct PopCandidate {
  std::filesystem::path path;
  PathSnapshot discovery_snapshot;
};

struct Discovery {
  std::vector<PopCandidate> candidates;
};

[[nodiscard]] bool RecordPathMetadata(const std::filesystem::path &path,
                                      std::uint64_t &total) noexcept {
  std::uint64_t bytes = 0;
  if (!Multiply(path.native().size(), sizeof(std::filesystem::path::value_type),
                bytes) ||
      bytes > kMaximumPathMetadataBytes - total)
    return false;
  total += bytes;
  return true;
}

[[nodiscard]] std::expected<std::vector<std::byte>, StablePathError>
ReadStableBytes(const StablePathGuard &guard) {
  const std::uint64_t expected_size = guard.snapshot().size;
  if (expected_size >
      static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    return std::unexpected(StablePathError::Io);
  std::vector<std::byte> bytes(static_cast<std::size_t>(expected_size));

#ifdef _WIN32
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const std::size_t remaining = bytes.size() - offset;
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        remaining,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD read = 0;
    if (ReadFile(guard.native_handle(), bytes.data() + offset, request, &read,
                 nullptr) == FALSE ||
        read == 0)
      return std::unexpected(StablePathError::Io);
    offset += read;
  }
  std::byte extra{};
  DWORD extra_read = 0;
  if (ReadFile(guard.native_handle(), &extra, 1U, &extra_read, nullptr) ==
          FALSE ||
      extra_read != 0)
    return std::unexpected(StablePathError::Changed);
#else
  if (::lseek(guard.native_handle(), 0, SEEK_SET) < 0)
    return std::unexpected(StablePathError::Io);
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t read = ::read(guard.native_handle(), bytes.data() + offset,
                                bytes.size() - offset);
    if (read < 0) {
      if (errno == EINTR)
        continue;
      return std::unexpected(StablePathError::Io);
    }
    if (read == 0)
      return std::unexpected(StablePathError::Changed);
    offset += static_cast<std::size_t>(read);
  }
  std::byte extra{};
  ssize_t extra_read = 0;
  do {
    extra_read = ::read(guard.native_handle(), &extra, 1U);
  } while (extra_read < 0 && errno == EINTR);
  if (extra_read < 0)
    return std::unexpected(StablePathError::Io);
  if (extra_read != 0)
    return std::unexpected(StablePathError::Changed);
#endif
  return bytes;
}

[[nodiscard]] std::expected<Discovery, ErrorCategory>
DiscoverPopCandidates(const std::filesystem::path &root,
                      const PopPostTerrainCommandTestHooks &hooks) {
  auto root_status = SafeStatus(root);
  if (!root_status)
    return std::unexpected(root_status.error());
  if (!std::filesystem::is_directory(*root_status))
    return std::unexpected(ErrorCategory::DiscoveryInvalidRoot);

  struct PendingDirectory {
    std::filesystem::path path;
    std::uint32_t depth = 0;
    StablePathGuard guard;
  };

  auto root_guard = OpenStablePath(root, StablePathKind::Directory,
                                   StablePathAccess::Metadata);
  if (!root_guard)
    return std::unexpected(DiscoveryError(root_guard.error()));

  std::vector<PendingDirectory> pending;
  pending.push_back(PendingDirectory{
      .path = root,
      .depth = 0,
      .guard = std::move(*root_guard),
  });
  std::uint64_t directories = 1;
  std::uint64_t visited_entries = 0;
  std::uint64_t path_metadata_bytes = 0;
  std::uint64_t total_pop_bytes = 0;
  if (!RecordPathMetadata(root, path_metadata_bytes))
    return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);

  Discovery discovery;
  while (!pending.empty()) {
    PendingDirectory directory = std::move(pending.back());
    pending.pop_back();
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory.path, error), end;
    if (error)
      return std::unexpected(ErrorCategory::DiscoveryIo);
    InvokeHook(hooks, PopPostTerrainCommandTestEvent::DirectoryIteratorOpened,
               directory.path);
    auto opened_verification = VerifyStablePath(directory.guard);
    if (!opened_verification)
      return std::unexpected(DiscoveryError(opened_verification.error()));

    while (iterator != end && !error) {
      if (visited_entries >= kMaximumWalkEntries)
        return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
      ++visited_entries;
      const std::filesystem::path path = iterator->path();
      if (!RecordPathMetadata(path, path_metadata_bytes))
        return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);

      auto status = SafeStatus(path);
      if (!status)
        return std::unexpected(status.error());
      if (std::filesystem::is_directory(*status)) {
        if (directory.depth >= kMaximumTreeDepth ||
            directories >= kMaximumDirectories)
          return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
        auto child_guard = OpenStablePath(path, StablePathKind::Directory,
                                          StablePathAccess::Metadata);
        if (!child_guard)
          return std::unexpected(DiscoveryError(child_guard.error()));
        ++directories;
        pending.push_back(PendingDirectory{
            .path = path,
            .depth = directory.depth + 1U,
            .guard = std::move(*child_guard),
        });
      } else if (std::filesystem::is_regular_file(*status)) {
        if (IsPopCandidate(path)) {
          if (discovery.candidates.size() >= kMaximumPopCandidates)
            return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
          auto file_guard = OpenStablePath(path, StablePathKind::RegularFile,
                                           StablePathAccess::Metadata);
          if (!file_guard)
            return std::unexpected(DiscoveryError(file_guard.error()));
          const std::uint64_t size = file_guard->snapshot().size;
          if (size > kMaximumPopBytes ||
              size > kMaximumTotalPopBytes - total_pop_bytes)
            return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);
          total_pop_bytes += size;
          discovery.candidates.push_back(PopCandidate{
              .path = path,
              .discovery_snapshot = file_guard->snapshot(),
          });
        }
      } else {
        return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
      }
      iterator.increment(error);
    }
    auto traversed_verification = VerifyStablePath(directory.guard);
    if (!traversed_verification)
      return std::unexpected(DiscoveryError(traversed_verification.error()));
    if (error)
      return std::unexpected(ErrorCategory::DiscoveryIo);
  }

  std::ranges::sort(discovery.candidates,
                    [](const PopCandidate &left, const PopCandidate &right) {
                      return left.path.native() < right.path.native();
                    });
  return discovery;
}

[[nodiscard]] std::expected<std::vector<std::byte>, ErrorCategory>
ReadPopCandidate(const PopCandidate &candidate,
                 std::uint64_t &remaining_total_bytes,
                 const PopPostTerrainCommandTestHooks &hooks) {
  InvokeHook(hooks, PopPostTerrainCommandTestEvent::BeforePopFileOpen,
             candidate.path);
  auto guard = OpenStablePath(candidate.path, StablePathKind::RegularFile,
                              StablePathAccess::Read);
  if (!guard) {
    if (guard.error() == StablePathError::Unsafe)
      return std::unexpected(ErrorCategory::DiscoveryUnsafeEntry);
    return std::unexpected(ErrorCategory::PopRead);
  }
  if (!SameSnapshot(candidate.discovery_snapshot, guard->snapshot()))
    return std::unexpected(ErrorCategory::PopRead);

  const std::uint64_t expected_size = guard->snapshot().size;
  if (expected_size > kMaximumPopBytes ||
      expected_size > remaining_total_bytes ||
      expected_size >
          static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()))
    return std::unexpected(ErrorCategory::DiscoveryLimitExceeded);

  InvokeHook(hooks, PopPostTerrainCommandTestEvent::PopFileOpened,
             candidate.path);
  auto opened_verification = VerifyStablePath(*guard);
  if (!opened_verification)
    return std::unexpected(ErrorCategory::PopRead);

  auto bytes = ReadStableBytes(*guard);
  if (!bytes)
    return std::unexpected(ErrorCategory::PopRead);

  auto final_verification = VerifyStablePath(*guard);
  if (!final_verification)
    return std::unexpected(ErrorCategory::PopRead);

  remaining_total_bytes -= expected_size;
  return std::move(*bytes);
}

[[nodiscard]] ErrorCategory
DescriptorErrorCategory(const asset::DecodeErrorCode code) noexcept {
  switch (code) {
  case asset::DecodeErrorCode::Truncated:
    return ErrorCategory::DescriptorTruncated;
  case asset::DecodeErrorCode::Malformed:
    return ErrorCategory::DescriptorMalformed;
  case asset::DecodeErrorCode::Overflow:
    return ErrorCategory::DescriptorOverflow;
  case asset::DecodeErrorCode::LimitExceeded:
    return ErrorCategory::DescriptorLimitExceeded;
  case asset::DecodeErrorCode::UnsupportedVariant:
    return ErrorCategory::DescriptorUnsupportedVariant;
  case asset::DecodeErrorCode::InvalidReference:
    return ErrorCategory::DescriptorInvalidReference;
  case asset::DecodeErrorCode::DuplicateReference:
    return ErrorCategory::DescriptorDuplicateReference;
  }
  return ErrorCategory::DescriptorMalformed;
}

[[nodiscard]] std::expected<LogicalUsage, ErrorCategory>
MeasureLogicalUsage(const std::span<const std::byte> bytes) {
  // Usage measurement intentionally revisits only the proven TER prefix. No
  // post-terrain literal, descriptor field, candidate value, or opaque range
  // contributes to the report.
  auto terrain = asset::PopTerrainIndex::Parse(bytes);
  if (!terrain)
    return std::unexpected(ErrorCategory::UsageMeasurement);

  LogicalUsage usage{
      .input_bytes = bytes.size(),
      .items = 1,
      .logical_output_bytes =
          sizeof(retail::PopPostTerrainHypothesisDescriptor),
  };
  std::uint64_t terrain_name_bytes = 0;
  for (const auto &record : terrain->records()) {
    usage.string_bytes =
        std::max<std::uint64_t>(usage.string_bytes, record.name.size());
    if (!Add(terrain_name_bytes, record.name.size()))
      return std::unexpected(ErrorCategory::UsageMeasurement);
  }

  constexpr std::uint64_t parser_record_overhead =
      sizeof(asset::PopTerrainRecord) + 2U * sizeof(void *);
  std::uint64_t record_scratch_bytes = 0;
  if (!Multiply(terrain->records().size(), parser_record_overhead,
                record_scratch_bytes) ||
      !Add(record_scratch_bytes, terrain_name_bytes))
    return std::unexpected(ErrorCategory::UsageMeasurement);
  usage.peak_scratch_bytes = record_scratch_bytes;
  return usage;
}

[[nodiscard]] bool MergeAccepted(Aggregate &aggregate,
                                 const LogicalUsage &usage) noexcept {
  Aggregate next = aggregate;
  if (!Add(next.totals.descriptors_accepted, 1U))
    return false;
  next.maxima.input_bytes =
      std::max(next.maxima.input_bytes, usage.input_bytes);
  next.maxima.items = std::max(next.maxima.items, usage.items);
  next.maxima.logical_output_bytes =
      std::max(next.maxima.logical_output_bytes, usage.logical_output_bytes);
  next.maxima.string_bytes =
      std::max(next.maxima.string_bytes, usage.string_bytes);
  next.maxima.peak_scratch_bytes =
      std::max(next.maxima.peak_scratch_bytes, usage.peak_scratch_bytes);
  aggregate = std::move(next);
  return true;
}

void PrintReport(const Aggregate &aggregate) {
  std::cout << "{\"schema_version\":1,"
               "\"scope\":\"native aggregate POP post-terrain hypothesis "
               "verification; "
               "independent logical field maxima over accepted descriptors "
               "only; literal order and "
               "arithmetic extents remain hypotheses, not decoded sections, "
               "counts, records, "
               "payloads, placement, visibility, rendering, or gameplay; no "
               "paths, names, hashes, "
               "literal spellings, candidate offsets, observed words, strides, "
               "opaque-region sizes, "
               "per-file rows, identities, or bindings\",\"totals\":{"
               "\"pop_candidates_discovered\":"
            << aggregate.totals.pop_candidates_discovered
            << ",\"descriptors_accepted\":"
            << aggregate.totals.descriptors_accepted
            << ",\"descriptors_rejected\":"
            << aggregate.totals.descriptors_rejected
            << ",\"errors\":" << ErrorTotal(aggregate.errors)
            << "},\"maxima\":{\"input_bytes\":" << aggregate.maxima.input_bytes
            << ",\"items\":" << aggregate.maxima.items
            << ",\"logical_output_bytes\":"
            << aggregate.maxima.logical_output_bytes
            << ",\"string_bytes\":" << aggregate.maxima.string_bytes
            << ",\"peak_scratch_bytes\":" << aggregate.maxima.peak_scratch_bytes
            << "},\"error_categories\":{";
  for (std::size_t index = 0; index < kErrorCategoryNames.size(); ++index) {
    if (index != 0)
      std::cout << ',';
    std::cout << '\"' << kErrorCategoryNames[index]
              << "\":" << aggregate.errors[index];
  }
  std::cout << "}}\n";
}

void PrintErrors(const ErrorCounts &errors) {
  for (std::size_t index = 0; index < errors.size(); ++index) {
    if (errors[index] != 0)
      std::cerr << "pop-post-terrain-hypotheses: " << kErrorCategoryNames[index]
                << '\n';
  }
}

[[nodiscard]] int VerifyTree(const std::filesystem::path &root,
                             const PopPostTerrainCommandTestHooks &hooks) {
  Aggregate aggregate;
  auto discovery = DiscoverPopCandidates(root, hooks);
  if (!discovery) {
    RecordError(aggregate.errors, discovery.error());
    PrintReport(aggregate);
    PrintErrors(aggregate.errors);
    return 1;
  }

  aggregate.totals.pop_candidates_discovered = discovery->candidates.size();
  if (discovery->candidates.empty()) {
    RecordError(aggregate.errors, ErrorCategory::NoPopCandidates);
    PrintReport(aggregate);
    PrintErrors(aggregate.errors);
    return 2;
  }

  std::uint64_t remaining_total_bytes = kMaximumTotalPopBytes;
  for (const auto &candidate : discovery->candidates) {
    auto bytes = ReadPopCandidate(candidate, remaining_total_bytes, hooks);
    if (!bytes) {
      RecordError(aggregate.errors, bytes.error());
      continue;
    }

    auto descriptor = retail::InspectPopPostTerrainHypotheses(*bytes);
    if (!descriptor) {
      if (!Add(aggregate.totals.descriptors_rejected, 1U))
        RecordError(aggregate.errors, ErrorCategory::AggregateOverflow);
      RecordError(aggregate.errors,
                  DescriptorErrorCategory(descriptor.error().code));
      continue;
    }

    auto usage = MeasureLogicalUsage(*bytes);
    if (!usage) {
      RecordError(aggregate.errors, usage.error());
      continue;
    }
    if (!MergeAccepted(aggregate, *usage))
      RecordError(aggregate.errors, ErrorCategory::AggregateOverflow);
  }

  PrintReport(aggregate);
  PrintErrors(aggregate.errors);
  const bool complete = ErrorTotal(aggregate.errors) == 0 &&
                        aggregate.totals.descriptors_accepted ==
                            aggregate.totals.pop_candidates_discovered;
  return complete ? 0 : 2;
}
} // namespace

int PopPostTerrainHypothesesVerifyTree(const std::filesystem::path &root) {
  return PopPostTerrainHypothesesVerifyTreeForTesting(root, {});
}

int PopPostTerrainHypothesesVerifyTreeForTesting(
    const std::filesystem::path &root,
    const PopPostTerrainCommandTestHooks hooks) {
  try {
    return VerifyTree(root, hooks);
  } catch (...) {
    Aggregate aggregate;
    RecordError(aggregate.errors, ErrorCategory::DiscoveryIo);
    PrintReport(aggregate);
    PrintErrors(aggregate.errors);
    return 1;
  }
}
} // namespace omega::tool
