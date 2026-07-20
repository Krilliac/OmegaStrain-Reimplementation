#include "omega/persistence/save_database.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using namespace std::string_view_literals;

using omega::persistence::SaveDatabase;
using omega::persistence::SaveDatabaseConfig;
using omega::persistence::SaveDatabaseError;
using omega::persistence::SaveDatabaseErrorCode;
using omega::persistence::SaveDatabaseLimits;
using omega::persistence::SaveMutation;
using omega::persistence::SaveWriteCondition;

static_assert(sizeof(SaveDatabaseErrorCode) == 1U);
static_assert(omega::persistence::kSaveDatabaseFormatVersion == 1U);

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <class T>
void CheckErrorCode(const std::expected<T, SaveDatabaseError> &result,
                    const SaveDatabaseErrorCode expected,
                    const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::persistence::SaveDatabaseErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::persistence::SaveDatabaseErrorCodeName(
                             result.error().code))
            << '\n';
  ++failures;
}

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char value : text)
    result.push_back(static_cast<std::byte>(value));
  return result;
}

[[nodiscard]] std::string Text(const std::span<const std::byte> bytes) {
  std::string result;
  result.reserve(bytes.size());
  for (const std::byte value : bytes)
    result.push_back(static_cast<char>(std::to_integer<unsigned char>(value)));
  return result;
}

class TempDirectory final {
public:
  explicit TempDirectory(const std::string_view label) {
    static std::atomic<std::uint64_t> next{0U};
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ = std::filesystem::temp_directory_path() /
            ("openomega-save-database-tests-" + std::string(label) + "-" +
             std::to_string(tick) + "-" + std::to_string(next.fetch_add(1U)));
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    Check(!error, "the synthetic save-database test directory is created");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    Check(!error, "the synthetic save-database test directory is removed");
  }

  TempDirectory(const TempDirectory &) = delete;
  TempDirectory &operator=(const TempDirectory &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

[[nodiscard]] SaveDatabaseConfig Config(const std::filesystem::path &path,
                                        const SaveDatabaseLimits limits = {}) {
  return {.directory = path, .limits = limits};
}

[[nodiscard]] std::filesystem::path SlotA(const std::filesystem::path &root) {
  return root / std::string(omega::persistence::kSaveDatabaseSlotAFileName);
}

[[nodiscard]] std::filesystem::path SlotB(const std::filesystem::path &root) {
  return root / std::string(omega::persistence::kSaveDatabaseSlotBFileName);
}

[[nodiscard]] bool OverwriteByte(const std::filesystem::path &path,
                                 const std::uint64_t offset,
                                 const unsigned char value) {
  std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
  if (!file)
    return false;
  file.seekp(static_cast<std::streamoff>(offset));
  file.put(static_cast<char>(value));
  file.flush();
  return file.good();
}

void CheckCodeNames() {
  constexpr std::array expected{
      "invalid-configuration"sv,
      "invalid-key"sv,
      "invalid-mutation"sv,
      "precondition-failed"sv,
      "limit-exceeded"sv,
      "busy"sv,
      "io-failure"sv,
      "corrupt-snapshot"sv,
      "unsupported-format"sv,
      "generation-exhausted"sv,
      "invalid-state"sv,
  };
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    Check(omega::persistence::SaveDatabaseErrorCodeName(
              static_cast<SaveDatabaseErrorCode>(index)) == expected[index],
          "every save database error code has one fixed name");
  }
  Check(omega::persistence::SaveDatabaseErrorCodeName(
            static_cast<SaveDatabaseErrorCode>(255U)) == "invalid-state",
        "an invalid save database error code maps to the fixed fallback");
}

void CheckConfigurationValidation() {
  CheckErrorCode(SaveDatabase::Open({.directory = "relative/save-root"}),
                 SaveDatabaseErrorCode::InvalidConfiguration,
                 "a relative database directory is rejected");

  TempDirectory tree("configuration");
  SaveDatabaseLimits limits;
  limits.max_records = 0U;
  CheckErrorCode(
      SaveDatabase::Open(Config(tree.path() / "zero-records", limits)),
      SaveDatabaseErrorCode::InvalidConfiguration,
      "a zero record limit is rejected");

  limits = {};
  limits.max_value_bytes = limits.max_logical_value_bytes + 1U;
  CheckErrorCode(
      SaveDatabase::Open(Config(tree.path() / "nonmonotonic", limits)),
      SaveDatabaseErrorCode::InvalidConfiguration,
      "nonmonotonic byte limits are rejected");

  const auto file_root = tree.path() / "ordinary-file";
  {
    std::ofstream file(file_root, std::ios::binary);
    file << "synthetic";
  }
  CheckErrorCode(SaveDatabase::Open(Config(file_root)),
                 SaveDatabaseErrorCode::InvalidConfiguration,
                 "a regular file cannot stand in for the database directory");

  const auto hostile_leaf_root = tree.path() / "hostile-leaf";
  std::error_code error;
  std::filesystem::create_directories(SlotA(hostile_leaf_root), error);
  Check(!error, "the hostile snapshot-directory fixture is created");
  CheckErrorCode(
      SaveDatabase::Open(Config(hostile_leaf_root)),
      SaveDatabaseErrorCode::IoFailure,
      "a directory at a snapshot leaf is rejected without traversal");

  const auto hostile_lock_root = tree.path() / "hostile-lock-leaf";
  error.clear();
  std::filesystem::create_directories(
      hostile_lock_root /
          std::string(omega::persistence::kSaveDatabaseLockFileName),
      error);
  Check(!error, "the hostile lock-directory fixture is created");
  CheckErrorCode(SaveDatabase::Open(Config(hostile_lock_root)),
                 SaveDatabaseErrorCode::IoFailure,
                 "a directory at the lock leaf is rejected without traversal");
}

void CheckFreshDatabaseAndTransactions() {
  TempDirectory tree("transactions");
  const auto database_root = tree.path() / "native";
  auto opened = SaveDatabase::Open(Config(database_root));
  Check(opened.has_value(), "a fresh native save database opens");
  if (!opened)
    return;
  std::optional<SaveDatabase> database{std::in_place, std::move(*opened)};
  Check(database->generation() == 0U && database->record_count() == 0U &&
            database->logical_value_bytes() == 0U &&
            database->config() != nullptr,
        "a fresh database starts at an explicit empty generation zero");
  Check(std::filesystem::is_regular_file(SlotA(database_root)) &&
            std::filesystem::is_regular_file(SlotB(database_root)) &&
            std::filesystem::is_regular_file(
                database_root /
                std::string(omega::persistence::kSaveDatabaseLockFileName)) &&
            std::filesystem::file_size(SlotA(database_root)) == 64U &&
            std::filesystem::file_size(SlotB(database_root)) == 64U,
        "fresh startup materializes two fixed checksummed genesis slots");

  auto absent = database->Read("profiles/alpha/campaign/slot-0");
  Check(absent && !*absent, "a valid absent record returns an empty optional");
  CheckErrorCode(database->Read("Profiles/alpha"),
                 SaveDatabaseErrorCode::InvalidKey,
                 "uppercase bytes are rejected by the canonical key grammar");
  CheckErrorCode(database->Read("profiles//alpha"),
                 SaveDatabaseErrorCode::InvalidKey,
                 "empty key segments are rejected");
  CheckErrorCode(database->List("profiles/../"),
                 SaveDatabaseErrorCode::InvalidKey,
                 "dot-dot prefix segments are rejected");

  std::array first{
      SaveMutation::Put("profiles/alpha/campaign/slot-0", 1U, Bytes("one"),
                        SaveWriteCondition::MustBeAbsent()),
  };
  auto committed = database->Commit(first);
  Check(committed && *committed == 1U && database->generation() == 1U &&
            database->record_count() == 1U &&
            database->logical_value_bytes() == 3U,
        "the first native record commits as generation one");
  auto record = database->Read("profiles/alpha/campaign/slot-0");
  Check(record && *record && (*record)->schema_version == 1U &&
            (*record)->revision == 1U && Text((*record)->value) == "one",
        "the committed record retains its native schema, revision, and bytes");

  const std::uint64_t stable_generation = database->generation();
  CheckErrorCode(database->Commit(first),
                 SaveDatabaseErrorCode::PreconditionFailed,
                 "must-be-absent rejects an existing native record");
  Check(database->generation() == stable_generation,
        "a failed precondition does not publish a generation");

  std::array atomic_failure{
      SaveMutation::Put("profiles/alpha/campaign/slot-1", 1U,
                        Bytes("candidate")),
      SaveMutation::Put("profiles//invalid", 1U, Bytes("reject")),
  };
  CheckErrorCode(database->Commit(atomic_failure),
                 SaveDatabaseErrorCode::InvalidKey,
                 "an invalid later mutation rejects the entire batch");
  absent = database->Read("profiles/alpha/campaign/slot-1");
  Check(absent && !*absent && database->generation() == stable_generation,
        "a rejected batch leaves every earlier candidate mutation invisible");

  std::array update{
      SaveMutation::Put("profiles/alpha/campaign/slot-0", 2U, Bytes("two!"),
                        SaveWriteCondition::ExactRevision(1U)),
      SaveMutation::Put("profiles/alpha/campaign/slot-2", 1U, Bytes("third"),
                        SaveWriteCondition::MustBeAbsent()),
      SaveMutation::Put("profiles/alpha/campaign/slot-1", 1U, Bytes("second"),
                        SaveWriteCondition::MustBeAbsent()),
  };
  committed = database->Commit(update);
  Check(committed && *committed == 2U && database->record_count() == 3U &&
            database->logical_value_bytes() == 15U,
        "one batch atomically updates and inserts multiple native records");
  record = database->Read("profiles/alpha/campaign/slot-0");
  Check(record && *record && (*record)->schema_version == 2U &&
            (*record)->revision == 2U && Text((*record)->value) == "two!",
        "updating a record increments only its record revision");

  auto listed = database->List("profiles/alpha/campaign/");
  Check(listed && listed->size() == 3U &&
            (*listed)[0].key.ends_with("slot-0") &&
            (*listed)[1].key.ends_with("slot-1") &&
            (*listed)[2].key.ends_with("slot-2") &&
            (*listed)[0].revision == 2U && (*listed)[0].value_bytes == 4U,
        "prefix listing is deterministic and key-sorted with metadata only");
  const auto missing_list = database->List("missing/");
  Check(missing_list && missing_list->empty(),
        "a valid unmatched prefix returns an empty list");

  std::array duplicate{
      SaveMutation::Put("profiles/alpha/campaign/slot-3", 1U, Bytes("a")),
      SaveMutation::Erase("profiles/alpha/campaign/slot-3",
                          SaveWriteCondition::Unconditional()),
  };
  CheckErrorCode(database->Commit(duplicate),
                 SaveDatabaseErrorCode::InvalidMutation,
                 "one transaction cannot repeat a key");
  CheckErrorCode(database->Commit({}), SaveDatabaseErrorCode::InvalidMutation,
                 "an empty transaction is rejected rather than manufacturing a "
                 "generation");
  std::array missing_present{
      SaveMutation::Put("profiles/alpha/campaign/missing", 1U, Bytes("x"),
                        SaveWriteCondition::MustExist()),
  };
  CheckErrorCode(database->Commit(missing_present),
                 SaveDatabaseErrorCode::PreconditionFailed,
                 "must-exist rejects an absent record");

  std::array invalid_condition{
      SaveMutation::Put(
          "profiles/alpha/campaign/invalid-condition", 1U, Bytes("x"),
          SaveWriteCondition{
              .kind = static_cast<omega::persistence::SaveWriteConditionKind>(
                  255U)}),
  };
  CheckErrorCode(database->Commit(invalid_condition),
                 SaveDatabaseErrorCode::InvalidMutation,
                 "an unknown write-condition kind is rejected explicitly");
  invalid_condition[0].condition = SaveWriteCondition{
      .kind = omega::persistence::SaveWriteConditionKind::MustExist,
      .revision = 1U,
  };
  CheckErrorCode(database->Commit(invalid_condition),
                 SaveDatabaseErrorCode::InvalidMutation,
                 "a non-revision condition cannot carry a revision payload");

  std::array erase{
      SaveMutation::Erase("profiles/alpha/campaign/slot-0",
                          SaveWriteCondition::ExactRevision(2U)),
  };
  committed = database->Commit(erase);
  Check(committed && *committed == 3U && database->record_count() == 2U,
        "an exact-revision erase commits as one generation");
  absent = database->Read("profiles/alpha/campaign/slot-0");
  Check(absent && !*absent, "an erased native record is absent");

  database.reset();
  auto reopened = SaveDatabase::Open(Config(database_root));
  Check(reopened && reopened->generation() == 3U &&
            reopened->record_count() == 2U,
        "the newest complete snapshot survives a database reopen");
  if (reopened) {
    record = reopened->Read("profiles/alpha/campaign/slot-1");
    Check(record && *record && Text((*record)->value) == "second",
          "reopen reconstructs the owned record bytes");

    SaveDatabase moved = std::move(*reopened);
    Check(moved.config() != nullptr && reopened->config() == nullptr,
          "moving transfers sole database ownership");
    CheckErrorCode(reopened->Read("profiles/alpha/campaign/slot-1"),
                   SaveDatabaseErrorCode::InvalidState,
                   "a moved-from database rejects reads deterministically");
    CheckErrorCode(reopened->Commit(first), SaveDatabaseErrorCode::InvalidState,
                   "a moved-from database rejects commits deterministically");
  }
}

void CheckExclusiveOwnership() {
  TempDirectory tree("exclusive-ownership");
  const auto root = tree.path() / "database";
  {
    auto first = SaveDatabase::Open(Config(root));
    Check(first.has_value(), "the first database owner acquires its lock");
    if (!first)
      return;
    CheckErrorCode(SaveDatabase::Open(Config(root)),
                   SaveDatabaseErrorCode::Busy,
                   "a second live database owner is rejected as busy");

    SaveDatabase moved = std::move(*first);
    CheckErrorCode(SaveDatabase::Open(Config(root)),
                   SaveDatabaseErrorCode::Busy,
                   "move construction transfers the exclusive database lock");
  }
  Check(SaveDatabase::Open(Config(root)).has_value(),
        "destroying the owner releases the database lock");
}

void CheckLimits() {
  TempDirectory tree("limits");
  SaveDatabaseLimits limits;
  limits.max_records = 1U;
  limits.max_mutations_per_commit = 1U;
  limits.max_key_bytes = 32U;
  limits.max_value_bytes = 4U;
  limits.max_logical_value_bytes = 4U;
  limits.max_file_bytes = 128U;
  auto opened = SaveDatabase::Open(Config(tree.path() / "bounded", limits));
  Check(opened.has_value(), "a tight but internally consistent database opens");
  if (!opened)
    return;

  std::array too_many_mutations{
      SaveMutation::Put("records/a", 1U, Bytes("a")),
      SaveMutation::Put("records/b", 1U, Bytes("b")),
  };
  CheckErrorCode(opened->Commit(too_many_mutations),
                 SaveDatabaseErrorCode::InvalidMutation,
                 "the configured per-commit mutation limit is enforced first");

  std::array too_large{
      SaveMutation::Put("records/a", 1U, Bytes("12345")),
  };
  CheckErrorCode(opened->Commit(too_large),
                 SaveDatabaseErrorCode::InvalidMutation,
                 "an individual value above its configured limit is rejected");

  std::array first{
      SaveMutation::Put("records/a", 1U, Bytes("1234")),
  };
  Check(opened->Commit(first).has_value(),
        "the exact configured value and logical-byte limits remain usable");
  std::array second{
      SaveMutation::Put("records/b", 1U, Bytes("x")),
  };
  CheckErrorCode(
      opened->Commit(second), SaveDatabaseErrorCode::LimitExceeded,
      "the first record beyond the configured record limit is rejected");
  Check(opened->generation() == 1U && opened->record_count() == 1U,
        "a limit failure preserves the prior durable generation");

  limits.max_records = 2U;
  limits.max_mutations_per_commit = 2U;
  limits.max_value_bytes = 4U;
  limits.max_logical_value_bytes = 4U;
  limits.max_file_bytes = 256U;
  auto logical = SaveDatabase::Open(Config(tree.path() / "logical", limits));
  Check(logical.has_value(), "the logical-byte limit fixture opens");
  if (logical) {
    Check(logical->Commit(first).has_value(),
          "the first logical-byte fixture commits");
    CheckErrorCode(logical->Commit(second),
                   SaveDatabaseErrorCode::LimitExceeded,
                   "aggregate logical value bytes are bounded independently");
  }
}

void CheckCrashRecovery() {
  TempDirectory tree("recovery");
  const auto root = tree.path() / "database";
  auto opened = SaveDatabase::Open(Config(root));
  Check(opened.has_value(), "the recovery database opens");
  if (!opened)
    return;

  std::array first{
      SaveMutation::Put("profiles/recovery/slot-0", 1U,
                        Bytes("generation-one")),
  };
  std::array second{
      SaveMutation::Put("profiles/recovery/slot-0", 1U, Bytes("generation-two"),
                        SaveWriteCondition::ExactRevision(1U)),
  };
  Check(opened->Commit(first) && opened->Commit(second) &&
            opened->generation() == 2U,
        "two alternating generations are committed before the crash fixture");
  opened = std::unexpected(SaveDatabaseError{});

  std::error_code error;
  std::filesystem::resize_file(SlotA(root), 20U, error);
  Check(!error, "the newest slot is truncated to model a torn write");
  auto recovered = SaveDatabase::Open(Config(root));
  Check(recovered && recovered->generation() == 1U,
        "open falls back to the prior complete slot after a torn newest write");
  if (!recovered)
    return;
  auto record = recovered->Read("profiles/recovery/slot-0");
  Check(record && *record && Text((*record)->value) == "generation-one",
        "crash recovery never publishes bytes from the torn snapshot");

  std::array replacement{
      SaveMutation::Put("profiles/recovery/slot-0", 1U,
                        Bytes("recovered-write"),
                        SaveWriteCondition::ExactRevision(1U)),
  };
  Check(recovered->Commit(replacement) && recovered->generation() == 2U,
        "the recovered database overwrites and read-verifies the damaged "
        "inactive slot");
  recovered = std::unexpected(SaveDatabaseError{});
  auto verified = SaveDatabase::Open(Config(root));
  Check(verified.has_value(), "the post-recovery database reopens");
  if (verified) {
    record = verified->Read("profiles/recovery/slot-0");
    Check(record && *record && Text((*record)->value) == "recovered-write",
          "a post-recovery commit remains durable on the next reopen");
  }
  verified = std::unexpected(SaveDatabaseError{});

  std::filesystem::resize_file(SlotA(root), 4U, error);
  Check(!error, "the first slot is corrupted for the no-valid-slot fixture");
  std::filesystem::resize_file(SlotB(root), 4U, error);
  Check(!error, "the second slot is corrupted for the no-valid-slot fixture");
  CheckErrorCode(SaveDatabase::Open(Config(root)),
                 SaveDatabaseErrorCode::CorruptSnapshot,
                 "two invalid snapshots fail closed instead of silently "
                 "creating an empty database");
}

void CheckUnsupportedFormatFailsClosed() {
  TempDirectory tree("future-format");
  const auto root = tree.path() / "database";
  auto opened = SaveDatabase::Open(Config(root));
  Check(opened.has_value(), "the future-format fixture database opens");
  opened = std::unexpected(SaveDatabaseError{});
  Check(
      OverwriteByte(SlotB(root), 8U, 2U),
      "the inactive snapshot version is changed to a synthetic future version");
  CheckErrorCode(SaveDatabase::Open(Config(root)),
                 SaveDatabaseErrorCode::UnsupportedFormat,
                 "an unsupported slot fails closed instead of rolling back "
                 "through another slot");
}
} // namespace

int main() {
  CheckCodeNames();
  CheckConfigurationValidation();
  CheckFreshDatabaseAndTransactions();
  CheckExclusiveOwnership();
  CheckLimits();
  CheckCrashRecovery();
  CheckUnsupportedFormatFailsClosed();
  return failures == 0 ? 0 : 1;
}
