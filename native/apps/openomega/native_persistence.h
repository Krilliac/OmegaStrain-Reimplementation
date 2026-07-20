#pragma once

#include "omega/persistence/save_database.h"
#include "omega/profiles/profile_catalog.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
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
    ResourceExhausted = 2U,
};

[[nodiscard]] std::string_view NativePersistenceStartupErrorCodeName(
    NativePersistenceStartupErrorCode code) noexcept;

struct NativePersistenceStartupError
{
    NativePersistenceStartupErrorCode code = NativePersistenceStartupErrorCode::ResourceExhausted;
    std::string message;
};

// Non-hot-reloadable application persistence owner. SaveDatabase has one
// stable heap address, ProfileCatalog borrows it, and OmegaApp becomes the sole
// owner after startup. Declaration order guarantees the catalog is destroyed
// before its database.
class NativePersistence final
{
public:
    // [persistence/game thread, startup] Opens native storage and validates all
    // typed profile markers by producing one deterministic startup snapshot.
    // This never creates or selects a default profile.
    [[nodiscard]] static std::expected<NativePersistence, NativePersistenceStartupError> Bootstrap(
        std::filesystem::path directory);

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

private:
    NativePersistence(std::unique_ptr<persistence::SaveDatabase> database,
                      std::unique_ptr<profiles::ProfileCatalog> profiles,
                      std::vector<profiles::ProfileSummary> startup_profiles) noexcept;

    std::unique_ptr<persistence::SaveDatabase> database_;
    std::unique_ptr<profiles::ProfileCatalog> profiles_;
    std::vector<profiles::ProfileSummary> startup_profiles_;
};
} // namespace omega::app
