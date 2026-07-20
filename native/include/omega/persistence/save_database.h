#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::persistence {
// OpenOmega-owned persistence. This is native application state, not a PS2
// savestate, emulated memory card, guest-memory image, or claim about the
// retail title's storage implementation.
inline constexpr std::uint32_t kSaveDatabaseFormatVersion = 1U;
inline constexpr std::string_view kSaveDatabaseSlotAFileName =
    "openomega-save-a.oodb";
inline constexpr std::string_view kSaveDatabaseSlotBFileName =
    "openomega-save-b.oodb";
inline constexpr std::string_view kSaveDatabaseLockFileName =
    "openomega-save.lock";

enum class SaveDatabaseErrorCode : std::uint8_t {
  InvalidConfiguration = 0,
  InvalidKey = 1,
  InvalidMutation = 2,
  PreconditionFailed = 3,
  LimitExceeded = 4,
  Busy = 5,
  IoFailure = 6,
  CorruptSnapshot = 7,
  UnsupportedFormat = 8,
  GenerationExhausted = 9,
  InvalidState = 10,
};

[[nodiscard]] std::string_view
SaveDatabaseErrorCodeName(SaveDatabaseErrorCode code) noexcept;

struct SaveDatabaseError {
  SaveDatabaseErrorCode code = SaveDatabaseErrorCode::InvalidState;
  std::string message;
};

// Synthetic native limits. They are deliberately explicit so hostile or
// corrupted files cannot turn persistence into an unbounded allocation surface.
// They are not retail or PS2 limits.
struct SaveDatabaseLimits {
  std::size_t max_records = 1'024U;
  std::size_t max_mutations_per_commit = 256U;
  std::size_t max_key_bytes = 96U;
  std::size_t max_value_bytes = 8U * 1024U * 1024U;
  std::size_t max_logical_value_bytes = 48U * 1024U * 1024U;
  std::size_t max_file_bytes = 64U * 1024U * 1024U;
};

struct SaveDatabaseConfig {
  // Must be a nonempty absolute directory. Open creates it when absent. The
  // final directory, lock leaf, and both database leaves must not be symlinks
  // or Windows reparse points. The directory is private application state;
  // hostile concurrent namespace mutation by the same OS account is outside
  // this service's containment boundary.
  std::filesystem::path directory;
  SaveDatabaseLimits limits;
};

enum class SaveWriteConditionKind : std::uint8_t {
  Unconditional = 0,
  MustBeAbsent = 1,
  MustExist = 2,
  ExactRevision = 3,
};

struct SaveWriteCondition {
  SaveWriteConditionKind kind = SaveWriteConditionKind::Unconditional;
  std::uint64_t revision = 0U;

  [[nodiscard]] static constexpr SaveWriteCondition Unconditional() noexcept {
    return {};
  }

  [[nodiscard]] static constexpr SaveWriteCondition MustBeAbsent() noexcept {
    return {.kind = SaveWriteConditionKind::MustBeAbsent};
  }

  [[nodiscard]] static constexpr SaveWriteCondition MustExist() noexcept {
    return {.kind = SaveWriteConditionKind::MustExist};
  }

  [[nodiscard]] static constexpr SaveWriteCondition
  ExactRevision(const std::uint64_t expected_revision) noexcept {
    return {
        .kind = SaveWriteConditionKind::ExactRevision,
        .revision = expected_revision,
    };
  }
};

enum class SaveMutationKind : std::uint8_t {
  Put = 0,
  Erase = 1,
};

struct SaveMutation {
  SaveMutationKind kind = SaveMutationKind::Put;
  std::string key;
  std::uint32_t schema_version = 0U;
  std::vector<std::byte> value;
  SaveWriteCondition condition;

  [[nodiscard]] static SaveMutation
  Put(std::string key, std::uint32_t schema_version,
      std::vector<std::byte> value,
      SaveWriteCondition condition = SaveWriteCondition::Unconditional());

  [[nodiscard]] static SaveMutation
  Erase(std::string key,
        SaveWriteCondition condition = SaveWriteCondition::MustExist());
};

struct SaveRecord {
  std::string key;
  std::uint32_t schema_version = 0U;
  std::uint64_t revision = 0U;
  std::vector<std::byte> value;
};

struct SaveRecordInfo {
  std::string key;
  std::uint32_t schema_version = 0U;
  std::uint64_t revision = 0U;
  std::size_t value_bytes = 0U;
};

// Non-hot-reloadable, dependency-free native persistence service. OmegaApp is
// the intended sole owner. It uses two complete checksummed snapshots; every
// commit writes and flushes a private same-directory file, atomically replaces
// the inactive slot, and only then publishes the new in-memory generation, so
// a torn write leaves the prior slot recoverable.
// Compatibility adapters translate PS2 save containers at this boundary and
// never become database backends.
//
// This first storage layer is externally serialized. Its methods may perform
// bounded filesystem I/O and must run on one persistence/game thread; callers
// must not invoke one instance concurrently. Read/List return owned copies so
// no internal views escape.
class SaveDatabase final {
public:
  static constexpr std::size_t kHardMaxRecords = 4'096U;
  static constexpr std::size_t kHardMaxMutationsPerCommit = 4'096U;
  static constexpr std::size_t kHardMaxKeyBytes = 255U;
  static constexpr std::size_t kHardMaxValueBytes = 16U * 1024U * 1024U;
  static constexpr std::size_t kHardMaxLogicalValueBytes = 128U * 1024U * 1024U;
  static constexpr std::size_t kHardMaxFileBytes = 256U * 1024U * 1024U;

  // [persistence/game thread, startup] Validates the directory and limits,
  // acquires exclusive process ownership, loads the newest valid snapshot, or
  // creates two empty generation-zero snapshots for a new database.
  [[nodiscard]] static std::expected<SaveDatabase, SaveDatabaseError>
  Open(SaveDatabaseConfig config);

  ~SaveDatabase();
  SaveDatabase(SaveDatabase &&) noexcept;
  SaveDatabase &operator=(SaveDatabase &&) noexcept;
  SaveDatabase(const SaveDatabase &) = delete;
  SaveDatabase &operator=(const SaveDatabase &) = delete;

  // [owning persistence/game thread] Empty when the validated key is absent.
  [[nodiscard]] std::expected<std::optional<SaveRecord>, SaveDatabaseError>
  Read(std::string_view key) const;

  // [owning persistence/game thread] Returns source-key-sorted metadata. An
  // empty prefix lists everything; a nonempty prefix follows the key grammar
  // and may end in '/'.
  [[nodiscard]] std::expected<std::vector<SaveRecordInfo>, SaveDatabaseError>
  List(std::string_view prefix = {}) const;

  // [owning persistence/game thread] Validates and applies the entire batch to
  // a private copy, durably writes a private temporary, then atomically
  // replaces the inactive snapshot. Empty batches and duplicate keys are
  // rejected. Success returns the new database generation. An indeterminate
  // OS-level publication failure poisons the instance; callers must destroy and
  // reopen it before any further read, list, or commit.
  [[nodiscard]] std::expected<std::uint64_t, SaveDatabaseError>
  Commit(std::span<const SaveMutation> mutations);

  [[nodiscard]] std::uint64_t generation() const noexcept;
  [[nodiscard]] std::size_t record_count() const noexcept;
  [[nodiscard]] std::size_t logical_value_bytes() const noexcept;
  [[nodiscard]] const SaveDatabaseConfig *config() const noexcept;

private:
  struct Impl;
  explicit SaveDatabase(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};
} // namespace omega::persistence
