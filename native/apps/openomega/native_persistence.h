#pragma once

#include "omega/persistence/save_database.h"
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

// Non-hot-reloadable application persistence owner. SaveDatabase has one
// stable heap address, ProfileCatalog borrows it, and OmegaApp becomes the sole
// owner after startup. Declaration order guarantees the catalog is destroyed
// before its database.
class NativePersistence final
{
public:
    // [persistence/game thread, startup] Opens native storage, validates all
    // typed profile markers and any durable active-profile confirmation, then
    // produces one deterministic startup snapshot. This never creates or
    // selects a default profile.
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

    // [owning persistence/game thread] Immutable summaries captured during
    // Bootstrap, sorted by ProfileId.
    [[nodiscard]] std::span<const profiles::ProfileSummary> startup_profiles() const noexcept;

    // [owning persistence/game thread] The exact ProfileId decoded from and
    // confirmed by the durable profiles/active pointer. This is not the app's
    // current session selection and is empty when no confirmation is stored.
    [[nodiscard]] const std::optional<profiles::ProfileId>&
    persisted_confirmed_profile_id() const noexcept;

    // [owning persistence/game thread] Confirms an existing profile through
    // at most one optimistic atomic commit. An already confirmed ID with the same
    // durable revision is a no-write success. Failure preserves the previously
    // confirmed owned snapshot.
    [[nodiscard]] std::expected<void, ActiveProfileConfirmationError>
    ConfirmActiveProfile(profiles::ProfileId id);

private:
    NativePersistence(std::unique_ptr<persistence::SaveDatabase> database,
                      std::unique_ptr<profiles::ProfileCatalog> profiles,
                      std::vector<profiles::ProfileSummary> startup_profiles,
                      std::optional<profiles::ProfileId> persisted_confirmed_profile_id,
                      std::uint64_t persisted_confirmed_profile_revision) noexcept;

    std::unique_ptr<persistence::SaveDatabase> database_;
    std::unique_ptr<profiles::ProfileCatalog> profiles_;
    std::vector<profiles::ProfileSummary> startup_profiles_;
    std::optional<profiles::ProfileId> persisted_confirmed_profile_id_;
    std::uint64_t persisted_confirmed_profile_revision_ = 0U;
};
} // namespace omega::app
