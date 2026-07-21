#include "frontend_envelope_commands.h"

#include "omega/archive/hog_archive.h"
#include "omega/asset/decode.h"
#include "omega/retail/fnt_envelope_descriptor.h"
#include "omega/retail/gui_envelope_descriptor.h"
#include "omega/retail/ie_envelope_descriptor.h"

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
constexpr std::uint64_t kMaximumTopLevelHogs = 1ULL << 12U;
constexpr std::uint64_t kMaximumTopLevelHogBytes =
    4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumAggregateTopLevelHogBytes =
    16ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumNestedHogs = 1ULL << 15U;
constexpr std::uint64_t kMaximumNestedHogBytes =
    archive::kDefaultMaximumArchiveLoadBytes;
constexpr std::uint64_t kMaximumAggregateNestedHogBytes =
    16ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumIndexedEntries = 1ULL << 20U;
constexpr std::uint64_t kMaximumFrontendCandidates = 1ULL << 20U;
constexpr std::uint64_t kMaximumAggregateCandidateBytes =
    4ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t kMaximumPathMetadataBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::size_t kMaximumTreeDepth = 32U;
constexpr std::size_t kMaximumNestedHogDepth = 32U;

enum class InputKind : std::uint8_t {
  Other,
  Hog,
  Fnt,
  Gui,
  Ie,
};

enum class RejectionKind : std::size_t {
  Truncated,
  Malformed,
  Overflow,
  LimitExceeded,
  UnsupportedVariant,
  InvalidReference,
  DuplicateReference,
  Count,
};

enum class ScanError : std::uint8_t {
  InvalidRoot,
  UnsafeEntry,
  LimitExceeded,
  Io,
  HogOpen,
  MemberRead,
};

using RejectionCounts =
    std::array<std::uint64_t, static_cast<std::size_t>(RejectionKind::Count)>;

struct FamilyStats {
  std::uint64_t candidates = 0;
  std::uint64_t accepted = 0;
  RejectionCounts rejected{};
};

struct Aggregate {
  FamilyStats fnt;
  FamilyStats gui;
  FamilyStats ie;
};

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

struct TopLevelHog {
  std::filesystem::path path;
  PathSnapshot discovery_snapshot;
};

struct Discovery {
  std::vector<TopLevelHog> hogs;
};

struct ScanState {
  Aggregate aggregate;
  std::uint64_t nested_hogs = 0;
  std::uint64_t nested_hog_bytes = 0;
  std::uint64_t indexed_entries = 0;
  std::uint64_t frontend_candidates = 0;
  std::uint64_t candidate_bytes = 0;
};

[[nodiscard]] unsigned char FoldAscii(const unsigned char value) noexcept {
  return value >= static_cast<unsigned char>('a') &&
                 value <= static_cast<unsigned char>('z')
             ? static_cast<unsigned char>(value - ('a' - 'A'))
             : value;
}

[[nodiscard]] bool ExtensionEquals(const std::filesystem::path &path,
                                   const std::string_view expected) {
  const std::string extension = path.extension().string();
  if (extension.size() != expected.size())
    return false;
  for (std::size_t index = 0; index < expected.size(); ++index) {
    if (FoldAscii(static_cast<unsigned char>(extension[index])) !=
        static_cast<unsigned char>(expected[index]))
      return false;
  }
  return true;
}

[[nodiscard]] InputKind Classify(const std::filesystem::path &path) {
  if (ExtensionEquals(path, ".HOG"))
    return InputKind::Hog;
  if (ExtensionEquals(path, ".FNT"))
    return InputKind::Fnt;
  if (ExtensionEquals(path, ".GUI"))
    return InputKind::Gui;
  if (ExtensionEquals(path, ".IE"))
    return InputKind::Ie;
  return InputKind::Other;
}

[[nodiscard]] bool AddBounded(std::uint64_t &target, const std::uint64_t value,
                              const std::uint64_t maximum) noexcept {
  if (target > maximum || value > maximum - target)
    return false;
  target += value;
  return true;
}

[[nodiscard]] bool Increment(std::uint64_t &target) noexcept {
  if (target == std::numeric_limits<std::uint64_t>::max())
    return false;
  ++target;
  return true;
}

[[nodiscard]] std::expected<std::uint64_t, ScanError>
CheckedAdd(const std::uint64_t left, const std::uint64_t right) noexcept {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    return std::unexpected(ScanError::LimitExceeded);
  return left + right;
}

[[nodiscard]] std::expected<bool, ScanError>
IsReparsePoint(const std::filesystem::path &path) noexcept {
#ifdef _WIN32
  const DWORD attributes = GetFileAttributesW(path.c_str());
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return std::unexpected(ScanError::Io);
  return (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
#else
  static_cast<void>(path);
  return false;
#endif
}

[[nodiscard]] std::expected<std::filesystem::file_status, ScanError>
SafeStatus(const std::filesystem::path &path) {
  std::error_code error;
  const auto status = std::filesystem::symlink_status(path, error);
  if (error)
    return std::unexpected(ScanError::Io);
  auto reparse = IsReparsePoint(path);
  if (!reparse)
    return std::unexpected(reparse.error());
  if (std::filesystem::is_symlink(status) || *reparse)
    return std::unexpected(ScanError::UnsafeEntry);
  return status;
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
    flags |= FILE_FLAG_RANDOM_ACCESS;

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
  else
    flags |= O_NONBLOCK;
  static_cast<void>(access);
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

[[nodiscard]] ScanError DiscoveryError(const StablePathError error) noexcept {
  return error == StablePathError::Unsafe ? ScanError::UnsafeEntry
                                          : ScanError::Io;
}

[[nodiscard]] ScanError FileReadError(const StablePathError error) noexcept {
  return error == StablePathError::Unsafe ? ScanError::UnsafeEntry
                                          : ScanError::MemberRead;
}

void InvokeHook(const FrontendEnvelopeCommandTestHooks &hooks,
                const FrontendEnvelopeCommandTestEvent event,
                const std::filesystem::path &path) {
  if (hooks.callback != nullptr)
    hooks.callback(event, path, hooks.context);
}

[[nodiscard]] std::expected<void, std::string>
ReadStableRange(void *opaque, const std::uint64_t offset,
                const std::span<std::byte> output) {
  if (opaque == nullptr)
    return std::unexpected("HOG read source has no stable handle");
  auto &guard = *static_cast<StablePathGuard *>(opaque);
  if (guard.kind() != StablePathKind::RegularFile ||
      offset > guard.snapshot().size ||
      output.size() > guard.snapshot().size - offset)
    return std::unexpected("HOG read range exceeds the stable file");

#ifdef _WIN32
  if (offset > static_cast<std::uint64_t>(std::numeric_limits<LONGLONG>::max()))
    return std::unexpected("stable HOG offset exceeds the host file API");
  LARGE_INTEGER position{};
  position.QuadPart = static_cast<LONGLONG>(offset);
  if (SetFilePointerEx(guard.native_handle(), position, nullptr, FILE_BEGIN) ==
      FALSE)
    return std::unexpected("unable to seek the stable HOG file");
  std::size_t complete = 0;
  while (complete < output.size()) {
    const DWORD request = static_cast<DWORD>(std::min<std::size_t>(
        output.size() - complete,
        static_cast<std::size_t>(std::numeric_limits<DWORD>::max())));
    DWORD read = 0;
    if (ReadFile(guard.native_handle(), output.data() + complete, request,
                 &read, nullptr) == FALSE ||
        read == 0)
      return std::unexpected("unable to read the stable HOG file");
    complete += read;
  }
#else
  std::size_t complete = 0;
  while (complete < output.size()) {
    auto current_offset =
        CheckedAdd(offset, static_cast<std::uint64_t>(complete));
    if (!current_offset ||
        *current_offset >
            static_cast<std::uint64_t>(std::numeric_limits<off_t>::max()))
      return std::unexpected("stable HOG offset exceeds the host file API");
    const std::size_t request = std::min<std::size_t>(
        output.size() - complete,
        static_cast<std::size_t>(std::numeric_limits<ssize_t>::max()));
    const ssize_t read =
        ::pread(guard.native_handle(), output.data() + complete, request,
                static_cast<off_t>(*current_offset));
    if (read < 0) {
      if (errno == EINTR)
        continue;
      return std::unexpected("unable to read the stable HOG file");
    }
    if (read == 0)
      return std::unexpected(
          "stable HOG file ended before the requested range");
    complete += static_cast<std::size_t>(read);
  }
#endif
  return {};
}

[[nodiscard]] archive::HogReadSource
MakeReadSource(StablePathGuard &guard) noexcept {
  return archive::HogReadSource{
      .size = guard.snapshot().size,
      .context = &guard,
      .read_exact = ReadStableRange,
  };
}

[[nodiscard]] bool RecordPathMetadata(const std::filesystem::path &path,
                                      std::uint64_t &total) noexcept {
  const std::size_t characters = path.native().size();
  if (characters > std::numeric_limits<std::uint64_t>::max() /
                       sizeof(std::filesystem::path::value_type))
    return false;
  const auto bytes = static_cast<std::uint64_t>(characters) *
                     sizeof(std::filesystem::path::value_type);
  return AddBounded(total, bytes, kMaximumPathMetadataBytes);
}

[[nodiscard]] std::expected<Discovery, ScanError>
DiscoverTopLevelHogs(const std::filesystem::path &root,
                     const FrontendEnvelopeCommandTestHooks &hooks) {
  auto root_status = SafeStatus(root);
  if (!root_status) {
    if (root_status.error() == ScanError::UnsafeEntry)
      return std::unexpected(root_status.error());
    return std::unexpected(ScanError::InvalidRoot);
  }
  if (!std::filesystem::is_directory(*root_status))
    return std::unexpected(ScanError::InvalidRoot);

  struct PendingDirectory {
    std::filesystem::path path;
    std::size_t depth = 0;
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
  std::uint64_t top_level_hog_bytes = 0;
  if (!RecordPathMetadata(root, path_metadata_bytes))
    return std::unexpected(ScanError::LimitExceeded);

  Discovery discovery;
  while (!pending.empty()) {
    PendingDirectory directory = std::move(pending.back());
    pending.pop_back();
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory.path, error), end;
    if (error)
      return std::unexpected(ScanError::Io);
    InvokeHook(hooks, FrontendEnvelopeCommandTestEvent::DirectoryIteratorOpened,
               directory.path);
    auto opened_verification = VerifyStablePath(directory.guard);
    if (!opened_verification)
      return std::unexpected(DiscoveryError(opened_verification.error()));

    while (iterator != end && !error) {
      if (!AddBounded(visited_entries, 1U, kMaximumWalkEntries))
        return std::unexpected(ScanError::LimitExceeded);
      const std::filesystem::path path = iterator->path();
      if (!RecordPathMetadata(path, path_metadata_bytes))
        return std::unexpected(ScanError::LimitExceeded);

      auto status = SafeStatus(path);
      if (!status)
        return std::unexpected(status.error());
      if (std::filesystem::is_directory(*status)) {
        if (directory.depth >= kMaximumTreeDepth ||
            !AddBounded(directories, 1U, kMaximumDirectories))
          return std::unexpected(ScanError::LimitExceeded);
        auto child_guard = OpenStablePath(path, StablePathKind::Directory,
                                          StablePathAccess::Metadata);
        if (!child_guard)
          return std::unexpected(DiscoveryError(child_guard.error()));
        pending.push_back(PendingDirectory{
            .path = path,
            .depth = directory.depth + 1U,
            .guard = std::move(*child_guard),
        });
      } else if (std::filesystem::is_regular_file(*status)) {
        if (Classify(path) == InputKind::Hog) {
          if (discovery.hogs.size() >= kMaximumTopLevelHogs)
            return std::unexpected(ScanError::LimitExceeded);
          auto file_guard = OpenStablePath(path, StablePathKind::RegularFile,
                                           StablePathAccess::Metadata);
          if (!file_guard)
            return std::unexpected(DiscoveryError(file_guard.error()));
          const std::uint64_t size = file_guard->snapshot().size;
          if (size > kMaximumTopLevelHogBytes ||
              !AddBounded(top_level_hog_bytes, size,
                          kMaximumAggregateTopLevelHogBytes))
            return std::unexpected(ScanError::LimitExceeded);
          discovery.hogs.push_back(TopLevelHog{
              .path = path,
              .discovery_snapshot = file_guard->snapshot(),
          });
        }
      } else {
        return std::unexpected(ScanError::UnsafeEntry);
      }
      iterator.increment(error);
    }
    auto traversed_verification = VerifyStablePath(directory.guard);
    if (!traversed_verification)
      return std::unexpected(DiscoveryError(traversed_verification.error()));
    if (error)
      return std::unexpected(ScanError::Io);
  }

  std::ranges::sort(discovery.hogs,
                    [](const TopLevelHog &left, const TopLevelHog &right) {
                      return left.path.native() < right.path.native();
                    });
  return discovery;
}

[[nodiscard]] std::expected<std::vector<std::byte>, ScanError>
ReadRange(const archive::HogReadSource &source, const std::uint64_t offset,
          const std::uint64_t size) {
  if (source.read_exact == nullptr ||
      size > std::numeric_limits<std::size_t>::max() || offset > source.size ||
      size > source.size - offset)
    return std::unexpected(ScanError::MemberRead);

  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  auto read = source.read_exact(source.context, offset, bytes);
  if (!read)
    return std::unexpected(ScanError::MemberRead);
  return bytes;
}

[[nodiscard]] RejectionKind
ToRejectionKind(const asset::DecodeErrorCode code) noexcept {
  switch (code) {
  case asset::DecodeErrorCode::Truncated:
    return RejectionKind::Truncated;
  case asset::DecodeErrorCode::Malformed:
    return RejectionKind::Malformed;
  case asset::DecodeErrorCode::Overflow:
    return RejectionKind::Overflow;
  case asset::DecodeErrorCode::LimitExceeded:
    return RejectionKind::LimitExceeded;
  case asset::DecodeErrorCode::UnsupportedVariant:
    return RejectionKind::UnsupportedVariant;
  case asset::DecodeErrorCode::InvalidReference:
    return RejectionKind::InvalidReference;
  case asset::DecodeErrorCode::DuplicateReference:
    return RejectionKind::DuplicateReference;
  }
  return RejectionKind::Malformed;
}

template <typename Descriptor>
[[nodiscard]] bool
RecordDescriptor(FamilyStats &stats,
                 const asset::DecodeResult<Descriptor> &descriptor) noexcept {
  if (descriptor)
    return Increment(stats.accepted);
  return Increment(stats.rejected[static_cast<std::size_t>(
      ToRejectionKind(descriptor.error().code))]);
}

[[nodiscard]] FamilyStats &StatsFor(Aggregate &aggregate,
                                    const InputKind kind) noexcept {
  switch (kind) {
  case InputKind::Fnt:
    return aggregate.fnt;
  case InputKind::Gui:
    return aggregate.gui;
  case InputKind::Ie:
    return aggregate.ie;
  case InputKind::Other:
  case InputKind::Hog:
    break;
  }
  return aggregate.fnt;
}

[[nodiscard]] std::uint64_t MaximumInputBytes(const InputKind kind) noexcept {
  switch (kind) {
  case InputKind::Fnt:
    return retail::kFntMaximumInputBytes;
  case InputKind::Gui:
    return retail::kGuiMaximumInputBytes;
  case InputKind::Ie:
    return retail::kIeMaximumInputBytes;
  case InputKind::Other:
  case InputKind::Hog:
    break;
  }
  return 0;
}

[[nodiscard]] bool InspectCandidate(const InputKind kind,
                                    const std::span<const std::byte> bytes,
                                    FamilyStats &stats) {
  switch (kind) {
  case InputKind::Fnt:
    return RecordDescriptor(stats, retail::InspectFntEnvelope(bytes));
  case InputKind::Gui:
    return RecordDescriptor(stats, retail::InspectGuiEnvelope(bytes));
  case InputKind::Ie:
    return RecordDescriptor(stats, retail::InspectIeEnvelope(bytes));
  case InputKind::Other:
  case InputKind::Hog:
    break;
  }
  return false;
}

[[nodiscard]] std::expected<void, ScanError>
ScanHogEntries(const archive::HogReadSource &source,
               const archive::HogIndex &parent,
               const std::uint64_t parent_file_offset, const std::size_t depth,
               ScanState &state) {
  const auto entry_count = static_cast<std::uint64_t>(parent.entries().size());
  if (!AddBounded(state.indexed_entries, entry_count, kMaximumIndexedEntries))
    return std::unexpected(ScanError::LimitExceeded);

  for (const auto &entry : parent.entries()) {
    const InputKind kind = Classify(std::filesystem::path(entry.name));
    if (kind == InputKind::Other)
      continue;

    auto absolute_offset = CheckedAdd(parent_file_offset, entry.offset);
    if (!absolute_offset)
      return std::unexpected(absolute_offset.error());

    if (kind == InputKind::Hog) {
      if (depth >= kMaximumNestedHogDepth ||
          !AddBounded(state.nested_hogs, 1U, kMaximumNestedHogs) ||
          entry.size > kMaximumNestedHogBytes ||
          !AddBounded(state.nested_hog_bytes, entry.size,
                      kMaximumAggregateNestedHogBytes))
        return std::unexpected(ScanError::LimitExceeded);

      auto nested = archive::HogIndex::OpenRange(
          source,
          archive::HogFileRange{.offset = *absolute_offset, .size = entry.size},
          kMaximumNestedHogBytes);
      if (!nested)
        return std::unexpected(ScanError::HogOpen);
      auto scanned =
          ScanHogEntries(source, *nested, *absolute_offset, depth + 1U, state);
      if (!scanned)
        return std::unexpected(scanned.error());
      continue;
    }

    if (!AddBounded(state.frontend_candidates, 1U,
                    kMaximumFrontendCandidates) ||
        !AddBounded(state.candidate_bytes, entry.size,
                    kMaximumAggregateCandidateBytes))
      return std::unexpected(ScanError::LimitExceeded);

    FamilyStats &stats = StatsFor(state.aggregate, kind);
    if (!Increment(stats.candidates))
      return std::unexpected(ScanError::LimitExceeded);
    if (entry.size > MaximumInputBytes(kind)) {
      if (!Increment(stats.rejected[static_cast<std::size_t>(
              RejectionKind::LimitExceeded)]))
        return std::unexpected(ScanError::LimitExceeded);
      continue;
    }

    auto bytes = ReadRange(source, *absolute_offset, entry.size);
    if (!bytes)
      return std::unexpected(bytes.error());
    if (!InspectCandidate(
            kind, std::span<const std::byte>(bytes->data(), bytes->size()),
            stats))
      return std::unexpected(ScanError::LimitExceeded);
  }
  return {};
}

[[nodiscard]] std::expected<Aggregate, ScanError>
ScanTree(const std::filesystem::path &root,
         const FrontendEnvelopeCommandTestHooks &hooks) {
  auto discovery = DiscoverTopLevelHogs(root, hooks);
  if (!discovery)
    return std::unexpected(discovery.error());

  ScanState state;
  for (const auto &candidate : discovery->hogs) {
    InvokeHook(hooks, FrontendEnvelopeCommandTestEvent::BeforeHogFileOpen,
               candidate.path);
    auto guard = OpenStablePath(candidate.path, StablePathKind::RegularFile,
                                StablePathAccess::Read);
    if (!guard)
      return std::unexpected(FileReadError(guard.error()));
    if (!SameSnapshot(candidate.discovery_snapshot, guard->snapshot()))
      return std::unexpected(ScanError::MemberRead);

    InvokeHook(hooks, FrontendEnvelopeCommandTestEvent::HogFileOpened,
               candidate.path);
    auto opened_verification = VerifyStablePath(*guard);
    if (!opened_verification)
      return std::unexpected(FileReadError(opened_verification.error()));

    auto source = MakeReadSource(*guard);
    auto hog = archive::HogIndex::Open(source);
    if (!hog)
      return std::unexpected(ScanError::HogOpen);
    auto scanned = ScanHogEntries(source, *hog, 0, 0, state);
    if (!scanned)
      return std::unexpected(scanned.error());

    auto final_verification = VerifyStablePath(*guard);
    if (!final_verification)
      return std::unexpected(FileReadError(final_verification.error()));
  }
  return state.aggregate;
}

void PrintFamily(const std::string_view name, const FamilyStats &stats) {
  std::cout
      << '"' << name << "\":{\"candidates\":" << stats.candidates
      << ",\"accepted\":" << stats.accepted << ",\"rejected_truncated\":"
      << stats.rejected[static_cast<std::size_t>(RejectionKind::Truncated)]
      << ",\"rejected_malformed\":"
      << stats.rejected[static_cast<std::size_t>(RejectionKind::Malformed)]
      << ",\"rejected_overflow\":"
      << stats.rejected[static_cast<std::size_t>(RejectionKind::Overflow)]
      << ",\"rejected_limit_exceeded\":"
      << stats.rejected[static_cast<std::size_t>(RejectionKind::LimitExceeded)]
      << ",\"rejected_unsupported_variant\":"
      << stats.rejected[static_cast<std::size_t>(
             RejectionKind::UnsupportedVariant)]
      << ",\"rejected_invalid_reference\":"
      << stats.rejected[static_cast<std::size_t>(
             RejectionKind::InvalidReference)]
      << ",\"rejected_duplicate_reference\":"
      << stats.rejected[static_cast<std::size_t>(
             RejectionKind::DuplicateReference)]
      << '}';
}

void PrintReport(const Aggregate &aggregate) {
  std::cout << "{\"schema_version\":1,";
  PrintFamily("fnt", aggregate.fnt);
  std::cout << ',';
  PrintFamily("gui", aggregate.gui);
  std::cout << ',';
  PrintFamily("ie", aggregate.ie);
  std::cout << "}\n";
}

[[nodiscard]] std::string_view ErrorName(const ScanError error) noexcept {
  switch (error) {
  case ScanError::InvalidRoot:
    return "discovery_invalid_root";
  case ScanError::UnsafeEntry:
    return "discovery_unsafe_entry";
  case ScanError::LimitExceeded:
    return "scan_limit_exceeded";
  case ScanError::Io:
    return "scan_io";
  case ScanError::HogOpen:
    return "hog_open";
  case ScanError::MemberRead:
    return "member_read";
  }
  return "scan_io";
}

void PrintError(const std::string_view name) {
  std::cerr << "frontend-envelope-coverage: " << name << '\n';
}

[[nodiscard]] bool HasEveryFamily(const Aggregate &aggregate) noexcept {
  return aggregate.fnt.candidates != 0 && aggregate.gui.candidates != 0 &&
         aggregate.ie.candidates != 0;
}

[[nodiscard]] bool AllAccepted(const Aggregate &aggregate) noexcept {
  return aggregate.fnt.accepted == aggregate.fnt.candidates &&
         aggregate.gui.accepted == aggregate.gui.candidates &&
         aggregate.ie.accepted == aggregate.ie.candidates;
}
} // namespace

int FrontendEnvelopeCoverageVerifyTreeForTesting(
    const std::filesystem::path &root,
    const FrontendEnvelopeCommandTestHooks hooks) {
  try {
    auto aggregate = ScanTree(root, hooks);
    if (!aggregate) {
      PrintReport({});
      PrintError(ErrorName(aggregate.error()));
      return 1;
    }

    PrintReport(*aggregate);
    if (!HasEveryFamily(*aggregate)) {
      PrintError("missing_family_candidates");
      return 2;
    }
    if (!AllAccepted(*aggregate)) {
      PrintError("descriptor_rejections");
      return 2;
    }
    return 0;
  } catch (...) {
    PrintReport({});
    PrintError("scan_io");
    return 1;
  }
}

int FrontendEnvelopeCoverageVerifyTree(const std::filesystem::path &root) {
  return FrontendEnvelopeCoverageVerifyTreeForTesting(root, {});
}
} // namespace omega::tool
