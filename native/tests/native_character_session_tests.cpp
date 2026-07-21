#include "native_persistence.h"

#include "omega/persistence/save_database.h"
#include "omega/profiles/character_catalog.h"
#include "omega/profiles/profile_catalog.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {
using omega::app::ActiveCharacterConfirmationError;
using omega::app::ActiveCharacterConfirmationErrorCode;
using omega::app::ActiveProfileConfirmationError;
using omega::app::ActiveProfileConfirmationErrorCode;
using omega::app::GameSessionStartError;
using omega::app::GameSessionStartErrorCode;
using omega::app::NativePersistence;
using omega::app::NativePersistenceStartupError;
using omega::app::NativePersistenceStartupErrorCode;
using omega::persistence::SaveDatabase;
using omega::persistence::SaveMutation;
using omega::persistence::SaveWriteCondition;
using omega::profiles::CharacterId;
using omega::profiles::CharacterMetadata;
using omega::profiles::ProfileId;
using omega::profiles::ProfileMetadata;

constexpr std::string_view kActiveCharacterKey = "profiles/active-character";
constexpr std::uint32_t kActiveCharacterSchemaVersion = 1U;
constexpr std::string_view kGameSessionCheckpointSuffix =
    "/sessions/diagnostic/checkpoint";
constexpr std::uint32_t kGameSessionCheckpointSchemaVersion = 1U;

static_assert(sizeof(NativePersistenceStartupErrorCode) == 1U);
static_assert(sizeof(ActiveProfileConfirmationErrorCode) == 1U);
static_assert(sizeof(ActiveCharacterConfirmationErrorCode) == 1U);
static_assert(sizeof(GameSessionStartErrorCode) == 1U);

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

template <class T>
void CheckStartupError(
    const std::expected<T, NativePersistenceStartupError> &result,
    const NativePersistenceStartupErrorCode expected,
    const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::app::NativePersistenceStartupErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::app::NativePersistenceStartupErrorCodeName(
                             result.error().code))
            << '\n';
  ++failures;
}

void CheckProfileConfirmationError(
    const std::expected<void, ActiveProfileConfirmationError> &result,
    const ActiveProfileConfirmationErrorCode expected,
    const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::app::ActiveProfileConfirmationErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::app::ActiveProfileConfirmationErrorCodeName(
                             result.error().code))
            << '\n';
  ++failures;
}

void CheckCharacterConfirmationError(
    const std::expected<void, ActiveCharacterConfirmationError> &result,
    const ActiveCharacterConfirmationErrorCode expected,
    const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::app::ActiveCharacterConfirmationErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::app::ActiveCharacterConfirmationErrorCodeName(
                             result.error().code))
            << '\n';
  ++failures;
}

void CheckGameSessionError(
    const std::expected<void, GameSessionStartError> &result,
    const GameSessionStartErrorCode expected, const std::string_view message) {
  if (!result && result.error().code == expected)
    return;
  std::cerr << "FAILED: " << message << "\n  expected: "
            << omega::app::GameSessionStartErrorCodeName(expected)
            << "\n  actual:   "
            << (result ? "<success>"
                       : omega::app::GameSessionStartErrorCodeName(
                             result.error().code))
            << '\n';
  ++failures;
}

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text) {
  std::vector<std::byte> result;
  result.reserve(text.size());
  for (const char value : text) {
    result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
  }
  return result;
}

[[nodiscard]] ProfileId Profile(const std::string_view text) {
  const auto parsed = ProfileId::Parse(text);
  Check(parsed.has_value(), "the character-session test profile ID parses");
  if (parsed)
    return *parsed;
  return ProfileId::FromBytes({});
}

[[nodiscard]] CharacterId Character(const std::string_view text) {
  const auto parsed = CharacterId::Parse(text);
  Check(parsed.has_value(), "the character-session test character ID parses");
  if (parsed)
    return *parsed;
  return CharacterId::FromBytes({});
}

[[nodiscard]] std::string
GameSessionCheckpointKey(const ProfileId profile_id,
                         const CharacterId character_id) {
  return "profiles/" + profile_id.ToString() + "/characters/" +
         character_id.ToString() + std::string(kGameSessionCheckpointSuffix);
}

[[nodiscard]] std::string ProfileMetadataKey(const ProfileId profile_id) {
  return "profiles/" + profile_id.ToString() + "/metadata";
}

[[nodiscard]] std::string CharacterMetadataKey(const ProfileId profile_id,
                                               const CharacterId character_id) {
  return "profiles/" + profile_id.ToString() + "/characters/" +
         character_id.ToString() + "/metadata";
}

[[nodiscard]] bool HasMagic(const std::vector<std::byte> &bytes,
                            const std::string_view magic) noexcept {
  if (bytes.size() < magic.size())
    return false;
  for (std::size_t index = 0U; index < magic.size(); ++index) {
    if (std::to_integer<unsigned char>(bytes[index]) !=
        static_cast<unsigned char>(magic[index])) {
      return false;
    }
  }
  return true;
}

class TempDirectory final {
public:
  explicit TempDirectory(const std::string_view label) {
    static std::atomic<std::uint64_t> next{0U};
    const auto tick =
        std::chrono::steady_clock::now().time_since_epoch().count();
    root_ =
        std::filesystem::temp_directory_path() /
        ("openomega-native-character-session-tests-" + std::string(label) +
         "-" + std::to_string(tick) + "-" + std::to_string(next.fetch_add(1U)));
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    Check(!error,
          "the synthetic native character-session directory is created");
  }

  ~TempDirectory() {
    std::error_code error;
    std::filesystem::remove_all(root_, error);
    Check(!error,
          "the synthetic native character-session directory is removed");
  }

  TempDirectory(const TempDirectory &) = delete;
  TempDirectory &operator=(const TempDirectory &) = delete;

  [[nodiscard]] const std::filesystem::path &path() const noexcept {
    return root_;
  }

private:
  std::filesystem::path root_;
};

[[nodiscard]] bool CreateProfile(NativePersistence &persistence,
                                 const ProfileId profile_id,
                                 std::string display_name) {
  const auto created = persistence.profiles().Create(
      profile_id, ProfileMetadata{
                      .display_name = std::move(display_name),
                      .created_unix_milliseconds = 1'000U,
                      .modified_unix_milliseconds = 1'000U,
                  });
  Check(created.has_value(), "the synthetic session profile is created");
  return created.has_value();
}

[[nodiscard]] bool CreateCharacter(NativePersistence &persistence,
                                   const ProfileId profile_id,
                                   const CharacterId character_id,
                                   std::string display_name) {
  const auto created = persistence.characters().Create(
      profile_id, character_id,
      CharacterMetadata{
          .display_name = std::move(display_name),
          .created_unix_milliseconds = 2'000U,
          .modified_unix_milliseconds = 2'000U,
      });
  Check(created.has_value(), "the synthetic session character is created");
  return created.has_value();
}

[[nodiscard]] bool CorruptExistingRecord(SaveDatabase &database,
                                         const std::string_view key,
                                         const std::uint32_t schema_version,
                                         std::vector<std::byte> value) {
  const auto existing = database.Read(key);
  Check(existing && *existing,
        "the raw character-session record exists before corruption");
  if (!existing || !*existing)
    return false;

  std::array mutation{
      SaveMutation::Put(
          std::string(key), schema_version, std::move(value),
          SaveWriteCondition::ExactRevision((**existing).revision)),
  };
  const auto committed = database.Commit(mutation);
  Check(committed.has_value(),
        "the synthetic character-session corruption commits");
  return committed.has_value();
}

[[nodiscard]] bool EraseExistingRecord(SaveDatabase &database,
                                       const std::string_view key) {
  const auto existing = database.Read(key);
  Check(existing && *existing,
        "the raw character-session record exists before erasure");
  if (!existing || !*existing)
    return false;

  std::array mutation{
      SaveMutation::Erase(std::string(key), SaveWriteCondition::ExactRevision(
                                                (**existing).revision)),
  };
  const auto committed = database.Commit(mutation);
  Check(committed.has_value(), "the synthetic owned-record erasure commits");
  return committed.has_value();
}

void CheckEmptyBootstrapAndTypedFailures() {
  TempDirectory tree("empty-and-errors");
  const auto database_root = tree.path() / "native-save";
  auto persistence = NativePersistence::Bootstrap(database_root);
  Check(persistence && persistence->startup_profiles().empty() &&
            !persistence->persisted_confirmed_profile_id() &&
            !persistence->persisted_confirmed_character_id() &&
            persistence->database().generation() == 0U &&
            persistence->database().record_count() == 0U,
        "fresh bootstrap has no profile, character, confirmation, or marker");
  if (!persistence)
    return;

  const ProfileId first_profile = Profile("00000000000000000000000000000001");
  const ProfileId other_profile = Profile("00000000000000000000000000000002");
  const CharacterId first_character =
      Character("00000000000000000000000000000001");
  const CharacterId missing_character =
      Character("00000000000000000000000000000002");

  const auto *config = persistence->database().config();
  const std::string checkpoint_key =
      GameSessionCheckpointKey(first_profile, first_character);
  Check(config && config->limits.max_key_bytes >= checkpoint_key.size(),
        "production persistence admits the bounded canonical session key");

  CheckProfileConfirmationError(
      persistence->ConfirmActiveProfile(first_profile),
      ActiveProfileConfirmationErrorCode::ProfileNotFound,
      "confirming a missing profile fails with profile-not-found");
  CheckCharacterConfirmationError(
      persistence->ConfirmActiveCharacter(first_profile, first_character),
      ActiveCharacterConfirmationErrorCode::ActiveProfileRequired,
      "character confirmation requires an active profile first");
  CheckGameSessionError(
      persistence->PrepareGameSessionStart(first_profile, first_character),
      GameSessionStartErrorCode::ActiveProfileRequired,
      "session start requires an active profile first");
  Check(persistence->database().generation() == 0U,
        "missing-identity failures do not mutate storage");

  if (!CreateProfile(*persistence, first_profile, "First Profile") ||
      !CreateCharacter(*persistence, first_profile, first_character,
                       "First Character")) {
    return;
  }
  Check(persistence->ConfirmActiveProfile(first_profile).has_value(),
        "the existing profile confirms");
  CheckGameSessionError(
      persistence->PrepareGameSessionStart(first_profile, first_character),
      GameSessionStartErrorCode::ActiveCharacterRequired,
      "session start requires an active character after profile confirmation");
  CheckCharacterConfirmationError(
      persistence->ConfirmActiveCharacter(other_profile, first_character),
      ActiveCharacterConfirmationErrorCode::ActiveProfileRequired,
      "a character cannot be confirmed beneath the wrong profile");
  CheckCharacterConfirmationError(
      persistence->ConfirmActiveCharacter(first_profile, missing_character),
      ActiveCharacterConfirmationErrorCode::CharacterNotFound,
      "confirming a missing character fails with character-not-found");

  Check(persistence->ConfirmActiveCharacter(first_profile, first_character)
            .has_value(),
        "the profile-owned character confirms");
  CheckGameSessionError(
      persistence->PrepareGameSessionStart(other_profile, first_character),
      GameSessionStartErrorCode::ActiveProfileRequired,
      "a session cannot start beneath the wrong profile");
  CheckGameSessionError(
      persistence->PrepareGameSessionStart(first_profile, missing_character),
      GameSessionStartErrorCode::ActiveCharacterRequired,
      "a session cannot start with an unconfirmed character");
  CheckProfileConfirmationError(
      persistence->ConfirmActiveProfile(other_profile),
      ActiveProfileConfirmationErrorCode::ProfileNotFound,
      "switching to a missing profile fails with profile-not-found");
  Check(persistence->persisted_confirmed_profile_id() == first_profile &&
            persistence->persisted_confirmed_character_id() == first_character,
        "typed identity failures preserve both durable confirmations");
}

void CheckDurableSessionLifecycleAndReopen() {
  TempDirectory tree("durable-lifecycle");
  const auto database_root = tree.path() / "native-save";
  const ProfileId profile_id = Profile("11111111111111111111111111111111");
  const CharacterId character_id =
      Character("22222222222222222222222222222222");
  const std::string checkpoint_key =
      GameSessionCheckpointKey(profile_id, character_id);

  {
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence.has_value(), "the durable session database bootstraps");
    if (!persistence)
      return;
    if (!CreateProfile(*persistence, profile_id, "Durable Profile") ||
        !CreateCharacter(*persistence, profile_id, character_id,
                         "Durable Character")) {
      return;
    }

    Check(persistence->ConfirmActiveProfile(profile_id).has_value() &&
              persistence->persisted_confirmed_profile_id() == profile_id,
          "profile confirmation publishes the durable profile identity");
    Check(persistence->ConfirmActiveCharacter(profile_id, character_id)
                  .has_value() &&
              persistence->persisted_confirmed_character_id() == character_id,
          "character confirmation publishes the durable character identity");
    Check(persistence->PrepareGameSessionStart(profile_id, character_id)
              .has_value(),
          "the confirmed pair prepares a character-owned session checkpoint");

    const auto checkpoint = persistence->database().Read(checkpoint_key);
    Check(checkpoint && *checkpoint &&
              (**checkpoint).schema_version ==
                  kGameSessionCheckpointSchemaVersion &&
              (**checkpoint).value.size() == 48U &&
              HasMagic((**checkpoint).value, "OOGAMECP"),
          "session preparation writes the fixed project-owned marker");
    const auto active_character =
        persistence->database().Read(kActiveCharacterKey);
    Check(active_character && *active_character &&
              (**active_character).schema_version ==
                  kActiveCharacterSchemaVersion &&
              (**active_character).value.size() == 48U &&
              HasMagic((**active_character).value, "OOACTCHR"),
          "character confirmation writes the fixed project-owned pointer");
    Check(persistence->database().generation() == 5U &&
              persistence->database().record_count() == 5U,
          "profile, character, two confirmations, and session marker publish "
          "five records in five commits");

    const std::uint64_t generation = persistence->database().generation();
    Check(persistence->PrepareGameSessionStart(profile_id, character_id)
                  .has_value() &&
              persistence->database().generation() == generation,
          "repreparing the same live session marker is a no-write success");
  }

  auto reopened = NativePersistence::Bootstrap(database_root);
  Check(reopened && reopened->startup_profiles().size() == 1U &&
            reopened->startup_profiles()[0].id == profile_id &&
            reopened->persisted_confirmed_profile_id() == profile_id &&
            reopened->persisted_confirmed_character_id() == character_id,
        "reopen validates and preserves both durable confirmations");
  if (!reopened)
    return;

  const auto character = reopened->characters().Read(profile_id, character_id);
  const auto checkpoint = reopened->database().Read(checkpoint_key);
  Check(character && *character &&
            (**character).metadata.display_name == "Durable Character" &&
            checkpoint && *checkpoint &&
            HasMagic((**checkpoint).value, "OOGAMECP"),
        "reopen validates the character and its owned session marker");
  const std::uint64_t generation = reopened->database().generation();
  Check(
      reopened->PrepareGameSessionStart(profile_id, character_id).has_value() &&
          reopened->database().generation() == generation,
      "the reopened validated marker remains an idempotent session start");
}

void CheckDeletedOwnedRecordsFailTyped() {
  TempDirectory tree("deleted-owned-records");
  const auto database_root = tree.path() / "native-save";
  const ProfileId profile_id = Profile("abababababababababababababababab");
  const CharacterId character_id =
      Character("cdcdcdcdcdcdcdcdcdcdcdcdcdcdcdcd");

  auto persistence = NativePersistence::Bootstrap(database_root);
  Check(persistence.has_value(),
        "the owned-record deletion database bootstraps");
  if (!persistence)
    return;
  if (!CreateProfile(*persistence, profile_id, "Deleted Profile") ||
      !CreateCharacter(*persistence, profile_id, character_id,
                       "Deleted Character") ||
      !persistence->ConfirmActiveProfile(profile_id) ||
      !persistence->ConfirmActiveCharacter(profile_id, character_id)) {
    Check(false, "the owned-record deletion fixture is prepared");
    return;
  }

  if (!EraseExistingRecord(persistence->database(),
                           CharacterMetadataKey(profile_id, character_id))) {
    return;
  }
  CheckGameSessionError(
      persistence->PrepareGameSessionStart(profile_id, character_id),
      GameSessionStartErrorCode::CharacterNotFound,
      "a removed confirmed character fails session start with "
      "character-not-found");

  if (!CreateCharacter(*persistence, profile_id, character_id,
                       "Restored Character") ||
      !EraseExistingRecord(persistence->database(),
                           ProfileMetadataKey(profile_id))) {
    return;
  }
  CheckCharacterConfirmationError(
      persistence->ConfirmActiveCharacter(profile_id, character_id),
      ActiveCharacterConfirmationErrorCode::ProfileNotFound,
      "a removed confirmed profile fails character confirmation with "
      "profile-not-found");
  CheckGameSessionError(
      persistence->PrepareGameSessionStart(profile_id, character_id),
      GameSessionStartErrorCode::ProfileNotFound,
      "a removed confirmed profile fails session start with profile-not-found");
}

void CheckProfileSwitchInvalidatesActiveCharacter() {
  TempDirectory tree("profile-switch");
  const auto database_root = tree.path() / "native-save";
  const ProfileId first_profile = Profile("33333333333333333333333333333333");
  const ProfileId second_profile = Profile("44444444444444444444444444444444");
  const CharacterId first_character =
      Character("55555555555555555555555555555555");
  const CharacterId second_character =
      Character("66666666666666666666666666666666");

  {
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence.has_value(), "the profile-switch database bootstraps");
    if (!persistence)
      return;
    if (!CreateProfile(*persistence, first_profile, "First") ||
        !CreateProfile(*persistence, second_profile, "Second") ||
        !CreateCharacter(*persistence, first_profile, first_character,
                         "First Character") ||
        !CreateCharacter(*persistence, second_profile, second_character,
                         "Second Character")) {
      return;
    }
    if (!persistence->ConfirmActiveProfile(first_profile) ||
        !persistence->ConfirmActiveCharacter(first_profile, first_character)) {
      Check(false, "the first profile and character confirmations succeed");
      return;
    }

    Check(persistence->ConfirmActiveProfile(second_profile).has_value(),
          "switching to the second existing profile succeeds");
    Check(persistence->persisted_confirmed_profile_id() == second_profile &&
              !persistence->persisted_confirmed_character_id(),
          "switching profiles clears the in-memory durable character snapshot");
    const auto active_character =
        persistence->database().Read(kActiveCharacterKey);
    Check(active_character && !*active_character,
          "switching profiles atomically erases the active-character pointer");
    CheckCharacterConfirmationError(
        persistence->ConfirmActiveCharacter(first_profile, first_character),
        ActiveCharacterConfirmationErrorCode::ActiveProfileRequired,
        "the prior profile's character cannot remain active after a switch");
    CheckGameSessionError(
        persistence->PrepareGameSessionStart(second_profile, second_character),
        GameSessionStartErrorCode::ActiveCharacterRequired,
        "the new profile requires an explicit character confirmation");
  }

  auto reopened = NativePersistence::Bootstrap(database_root);
  Check(reopened &&
            reopened->persisted_confirmed_profile_id() == second_profile &&
            !reopened->persisted_confirmed_character_id(),
        "reopen preserves the profile switch and absent character pointer");
  if (!reopened)
    return;
  Check(reopened->ConfirmActiveCharacter(second_profile, second_character)
            .has_value(),
        "the new profile's character can be explicitly confirmed after reopen");
}

void CheckProfileSwitchRejectsInterferingCharacterPointer() {
  TempDirectory tree("profile-switch-interference");
  const auto database_root = tree.path() / "native-save";
  const ProfileId first_profile = Profile("12121212121212121212121212121212");
  const ProfileId second_profile = Profile("34343434343434343434343434343434");

  auto persistence = NativePersistence::Bootstrap(database_root);
  Check(persistence.has_value(),
        "the profile-switch interference database bootstraps");
  if (!persistence)
    return;
  if (!CreateProfile(*persistence, first_profile, "First") ||
      !CreateProfile(*persistence, second_profile, "Second") ||
      !persistence->ConfirmActiveProfile(first_profile)) {
    Check(false, "the profile-switch interference fixture is prepared");
    return;
  }

  std::array insert_interference{
      SaveMutation::Put(std::string(kActiveCharacterKey),
                        kActiveCharacterSchemaVersion, Bytes("interference"),
                        SaveWriteCondition::MustBeAbsent()),
  };
  Check(persistence->database().Commit(insert_interference).has_value(),
        "a raw writer inserts an active-character pointer absent from the owned snapshot");
  const std::uint64_t generation_before_switch =
      persistence->database().generation();

  CheckProfileConfirmationError(
      persistence->ConfirmActiveProfile(second_profile),
      ActiveProfileConfirmationErrorCode::RevisionConflict,
      "switching profiles rejects an active-character pointer inserted outside the owned snapshot");
  const auto active_character =
      persistence->database().Read(kActiveCharacterKey);
  Check(persistence->persisted_confirmed_profile_id() == first_profile &&
            !persistence->persisted_confirmed_character_id() &&
            persistence->database().generation() == generation_before_switch &&
            active_character && *active_character &&
            (**active_character).value == Bytes("interference"),
        "the rejected profile switch preserves the prior confirmation and the conflicting raw record");
}

void CheckCorruptActiveCharacterPointerFailsBootstrap() {
  TempDirectory tree("corrupt-active-character");
  const auto database_root = tree.path() / "native-save";
  const ProfileId profile_id = Profile("77777777777777777777777777777777");
  const CharacterId character_id =
      Character("88888888888888888888888888888888");

  {
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence.has_value(),
          "the active-character corruption database bootstraps");
    if (!persistence)
      return;
    if (!CreateProfile(*persistence, profile_id, "Pointer Profile") ||
        !CreateCharacter(*persistence, profile_id, character_id,
                         "Pointer Character") ||
        !persistence->ConfirmActiveProfile(profile_id) ||
        !persistence->ConfirmActiveCharacter(profile_id, character_id)) {
      Check(false, "the active-character corruption fixture is prepared");
      return;
    }
    if (!CorruptExistingRecord(persistence->database(), kActiveCharacterKey,
                               kActiveCharacterSchemaVersion,
                               Bytes("truncated"))) {
      return;
    }
  }

  CheckStartupError(
      NativePersistence::Bootstrap(database_root),
      NativePersistenceStartupErrorCode::PersistedActiveCharacter,
      "a corrupt active-character pointer fails the exact startup category");
}

void CheckCorruptGameSessionCheckpointFailsBootstrap() {
  TempDirectory tree("corrupt-session-checkpoint");
  const auto database_root = tree.path() / "native-save";
  const ProfileId profile_id = Profile("99999999999999999999999999999999");
  const CharacterId character_id =
      Character("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
  const std::string checkpoint_key =
      GameSessionCheckpointKey(profile_id, character_id);

  {
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence.has_value(),
          "the session-checkpoint corruption database bootstraps");
    if (!persistence)
      return;
    if (!CreateProfile(*persistence, profile_id, "Checkpoint Profile") ||
        !CreateCharacter(*persistence, profile_id, character_id,
                         "Checkpoint Character") ||
        !persistence->ConfirmActiveProfile(profile_id) ||
        !persistence->ConfirmActiveCharacter(profile_id, character_id) ||
        !persistence->PrepareGameSessionStart(profile_id, character_id)) {
      Check(false, "the session-checkpoint corruption fixture is prepared");
      return;
    }
    if (!CorruptExistingRecord(persistence->database(), checkpoint_key,
                               kGameSessionCheckpointSchemaVersion,
                               Bytes("truncated"))) {
      return;
    }
  }

  CheckStartupError(
      NativePersistence::Bootstrap(database_root),
      NativePersistenceStartupErrorCode::PersistedGameSessionCheckpoint,
      "a corrupt character-owned checkpoint fails the exact startup category");
}
} // namespace

int main() {
  CheckEmptyBootstrapAndTypedFailures();
  CheckDurableSessionLifecycleAndReopen();
  CheckDeletedOwnedRecordsFailTyped();
  CheckProfileSwitchInvalidatesActiveCharacter();
  CheckProfileSwitchRejectsInterferingCharacterPointer();
  CheckCorruptActiveCharacterPointerFailsBootstrap();
  CheckCorruptGameSessionCheckpointFailsBootstrap();
  return failures == 0 ? 0 : 1;
}
