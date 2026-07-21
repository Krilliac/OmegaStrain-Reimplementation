#pragma once

#include "omega/persistence/save_database.h"
#include "omega/profiles/character_catalog.h"
#include "omega/profiles/profile_catalog.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::app
{
enum class NativePersistenceStartupErrorCode : std::uint8_t
{
    DatabaseOpen = 0U,
    ProfileCatalogBootstrap = 1U,
    PersistedActiveProfile = 2U,
    ResourceExhausted = 3U,
    PersistedDiagnosticCheckpoint = 4U,
    CharacterCatalogBootstrap = 5U,
    PersistedActiveCharacter = 6U,
    PersistedGameSessionCheckpoint = 7U,
};

[[nodiscard]] std::string_view NativePersistenceStartupErrorCodeName(
    NativePersistenceStartupErrorCode code) noexcept;

struct NativePersistenceStartupError
{
    NativePersistenceStartupErrorCode code = NativePersistenceStartupErrorCode::ResourceExhausted;
    std::string message;
};

enum class ActiveProfileConfirmationErrorCode : std::uint8_t
{
    ProfileNotFound = 0U,
    RevisionConflict = 1U,
    StorageLimitExceeded = 2U,
    StorageFailure = 3U,
    ResourceExhausted = 4U,
};

[[nodiscard]] std::string_view ActiveProfileConfirmationErrorCodeName(
    ActiveProfileConfirmationErrorCode code) noexcept;

struct ActiveProfileConfirmationError
{
    ActiveProfileConfirmationErrorCode code =
        ActiveProfileConfirmationErrorCode::StorageFailure;
    std::string message;
};

enum class ActiveCharacterConfirmationErrorCode : std::uint8_t
{
    ActiveProfileRequired = 0U,
    ProfileNotFound = 1U,
    CharacterNotFound = 2U,
    RevisionConflict = 3U,
    StorageLimitExceeded = 4U,
    StorageFailure = 5U,
    ResourceExhausted = 6U,
};

[[nodiscard]] std::string_view ActiveCharacterConfirmationErrorCodeName(
    ActiveCharacterConfirmationErrorCode code) noexcept;

struct ActiveCharacterConfirmationError
{
    ActiveCharacterConfirmationErrorCode code =
        ActiveCharacterConfirmationErrorCode::StorageFailure;
    std::string message;
};

enum class DiagnosticCampaignStartErrorCode : std::uint8_t
{
    ActiveProfileRequired = 0U,
    ProfileNotFound = 1U,
    RevisionConflict = 2U,
    StorageLimitExceeded = 3U,
    StorageFailure = 4U,
    ResourceExhausted = 5U,
};

[[nodiscard]] std::string_view DiagnosticCampaignStartErrorCodeName(
    DiagnosticCampaignStartErrorCode code) noexcept;

struct DiagnosticCampaignStartError
{
    DiagnosticCampaignStartErrorCode code =
        DiagnosticCampaignStartErrorCode::StorageFailure;
    std::string message;
};

enum class GameSessionStartErrorCode : std::uint8_t
{
    ActiveProfileRequired = 0U,
    ActiveCharacterRequired = 1U,
    ProfileNotFound = 2U,
    CharacterNotFound = 3U,
    RevisionConflict = 4U,
    StorageLimitExceeded = 5U,
    StorageFailure = 6U,
    ResourceExhausted = 7U,
};

[[nodiscard]] std::string_view GameSessionStartErrorCodeName(
    GameSessionStartErrorCode code) noexcept;

struct GameSessionStartError
{
    GameSessionStartErrorCode code = GameSessionStartErrorCode::StorageFailure;
    std::string message;
};

// Non-hot-reloadable application persistence owner. SaveDatabase has one
// stable heap address, ProfileCatalog borrows it, and OmegaApp becomes the sole
// owner after startup. Declaration order guarantees the catalog is destroyed
// before its database.
class NativePersistence final
{
public:
    // [persistence/game thread, startup] Opens native storage, validates all
    // typed profile markers, any durable active-profile confirmation, and all
    // fixed project diagnostic checkpoints, then produces one deterministic
    // startup snapshot. This never creates or selects a default profile.
    [[nodiscard]] static std::expected<NativePersistence, NativePersistenceStartupError> Bootstrap(
        std::filesystem::path directory);

    // [persistence/game thread, startup, test support] Uses explicit synthetic
    // database limits. Production callers use the one-argument overload.
    [[nodiscard]] static std::expected<NativePersistence, NativePersistenceStartupError> Bootstrap(
        std::filesystem::path directory, persistence::SaveDatabaseLimits limits);

    ~NativePersistence() = default;
    NativePersistence(NativePersistence&&) noexcept;
    NativePersistence& operator=(NativePersistence&&) noexcept = delete;
    NativePersistence(const NativePersistence&) = delete;
    NativePersistence& operator=(const NativePersistence&) = delete;

    // [owning persistence/game thread] Borrowed typed services remain valid for
    // this owner's lifetime.
    [[nodiscard]] persistence::SaveDatabase& database() noexcept;
    [[nodiscard]] profiles::ProfileCatalog& profiles() noexcept;
    [[nodiscard]] profiles::CharacterCatalog& characters() noexcept;

    // [owning persistence/game thread] Immutable summaries captured during
    // Bootstrap, sorted by ProfileId.
    [[nodiscard]] std::span<const profiles::ProfileSummary> startup_profiles() const noexcept;

    // [owning persistence/game thread] The exact ProfileId decoded from and
    // confirmed by the durable profiles/active pointer. This is not the app's
    // current session selection and is empty when no confirmation is stored.
    [[nodiscard]] const std::optional<profiles::ProfileId>&
    persisted_confirmed_profile_id() const noexcept;

    // [owning persistence/game thread] Durable character confirmation decoded
    // at startup. The paired profile identity is kept internally and validated
    // against profiles/active; this accessor exposes only the character ID.
    [[nodiscard]] const std::optional<profiles::CharacterId>&
    persisted_confirmed_character_id() const noexcept;

    // [owning persistence/game thread] Confirms an existing profile through
    // at most one optimistic atomic commit. An already confirmed ID with the same
    // durable revision is a no-write success. Failure preserves the previously
    // confirmed owned snapshot.
    [[nodiscard]] std::expected<void, ActiveProfileConfirmationError>
    ConfirmActiveProfile(profiles::ProfileId id);

    // [owning persistence/game thread] Confirms a character that exists beneath
    // the same durable active profile. No app session state is implied until the
    // caller publishes the successful result.
    [[nodiscard]] std::expected<void, ActiveCharacterConfirmationError>
    ConfirmActiveCharacter(profiles::ProfileId profile_id,
                           profiles::CharacterId character_id);

    // [owning persistence/game thread] Prepares exactly one project-generated
    // diagnostic checkpoint for the same profile currently confirmed by the
    // durable active pointer. The schema-1 marker is not a retail campaign,
    // save, world serializer, or gameplay-semantic claim. An already valid
    // marker is a no-write success. Definite validation, precondition, and
    // limit failures commit nothing, and every returned error prevents app
    // state publication. A lower storage error may represent indeterminate
    // atomic publication and poisons the database until it is reopened.
    [[nodiscard]] std::expected<void, DiagnosticCampaignStartError>
    PrepareDiagnosticCampaignStart(profiles::ProfileId id);

    // [owning persistence/game thread] Prepares a character-owned diagnostic
    // level session. This project schema is not a retail campaign/save format.
    [[nodiscard]] std::expected<void, GameSessionStartError>
    PrepareGameSessionStart(profiles::ProfileId profile_id,
                            profiles::CharacterId character_id);

private:
    NativePersistence(std::unique_ptr<persistence::SaveDatabase> database,
                      std::unique_ptr<profiles::ProfileCatalog> profiles,
                      std::unique_ptr<profiles::CharacterCatalog> characters,
                      std::vector<profiles::ProfileSummary> startup_profiles,
                      std::optional<profiles::ProfileId> persisted_confirmed_profile_id,
                      std::uint64_t persisted_confirmed_profile_revision,
                      std::optional<profiles::ProfileId>
                          persisted_confirmed_character_profile_id,
                      std::optional<profiles::CharacterId>
                          persisted_confirmed_character_id,
                      std::uint64_t persisted_confirmed_character_revision) noexcept;

    std::unique_ptr<persistence::SaveDatabase> database_;
    std::unique_ptr<profiles::ProfileCatalog> profiles_;
    std::unique_ptr<profiles::CharacterCatalog> characters_;
    std::vector<profiles::ProfileSummary> startup_profiles_;
    std::optional<profiles::ProfileId> persisted_confirmed_profile_id_;
    std::uint64_t persisted_confirmed_profile_revision_ = 0U;
    std::optional<profiles::ProfileId>
        persisted_confirmed_character_profile_id_;
    std::optional<profiles::CharacterId> persisted_confirmed_character_id_;
    std::uint64_t persisted_confirmed_character_revision_ = 0U;
};
} // namespace omega::app
