#include "native_persistence.h"

#include <new>
#include <utility>

namespace omega::app
{
namespace
{
[[nodiscard]] NativePersistenceStartupError MakeError(const NativePersistenceStartupErrorCode code,
                                                      std::string message)
{
    return {.code = code, .message = std::move(message)};
}
} // namespace

std::string_view NativePersistenceStartupErrorCodeName(
    const NativePersistenceStartupErrorCode code) noexcept
{
    switch (code)
    {
    case NativePersistenceStartupErrorCode::DatabaseOpen:
        return "database-open";
    case NativePersistenceStartupErrorCode::ProfileCatalogBootstrap:
        return "profile-catalog-bootstrap";
    case NativePersistenceStartupErrorCode::ResourceExhausted:
        return "resource-exhausted";
    }
    return "resource-exhausted";
}

std::expected<NativePersistence, NativePersistenceStartupError> NativePersistence::Bootstrap(
    std::filesystem::path directory)
{
    try
    {
        auto opened = persistence::SaveDatabase::Open({.directory = std::move(directory)});
        if (!opened)
        {
            std::string message = "save database [";
            message += persistence::SaveDatabaseErrorCodeName(opened.error().code);
            message += "]: ";
            message += opened.error().message;
            return std::unexpected(
                MakeError(NativePersistenceStartupErrorCode::DatabaseOpen, std::move(message)));
        }

        auto database = std::make_unique<persistence::SaveDatabase>(std::move(*opened));
        auto profile_catalog = std::make_unique<profiles::ProfileCatalog>(*database);
        auto startup_profiles = profile_catalog->List();
        if (!startup_profiles)
        {
            std::string message = "profile catalog [";
            message += profiles::ProfileCatalogErrorCodeName(startup_profiles.error().code);
            message += "]: ";
            message += startup_profiles.error().message;
            return std::unexpected(MakeError(
                NativePersistenceStartupErrorCode::ProfileCatalogBootstrap, std::move(message)));
        }

        return NativePersistence(std::move(database), std::move(profile_catalog),
                                 std::move(*startup_profiles));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(MakeError(NativePersistenceStartupErrorCode::ResourceExhausted,
                                         "native persistence startup allocation failed"));
    }
}

NativePersistence::NativePersistence(
    std::unique_ptr<persistence::SaveDatabase> database,
    std::unique_ptr<profiles::ProfileCatalog> profiles,
    std::vector<profiles::ProfileSummary> startup_profiles) noexcept
    : database_(std::move(database)), profiles_(std::move(profiles)),
      startup_profiles_(std::move(startup_profiles))
{
}

NativePersistence::NativePersistence(NativePersistence&&) noexcept = default;

persistence::SaveDatabase& NativePersistence::database() noexcept
{
    return *database_;
}

profiles::ProfileCatalog& NativePersistence::profiles() noexcept
{
    return *profiles_;
}

std::span<const profiles::ProfileSummary> NativePersistence::startup_profiles() const noexcept
{
    return startup_profiles_;
}
} // namespace omega::app
