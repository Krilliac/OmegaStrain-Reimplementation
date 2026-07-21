#include "native_persistence.h"
#include "front_end.h"

#include <array>
#include <cstddef>
#include <new>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::app
{
namespace
{
constexpr std::string_view kPersistedActiveProfileKey = "profiles/active";
constexpr std::uint32_t kPersistedActiveProfileSchemaVersion = 1U;
constexpr std::uint16_t kPersistedActiveProfilePayloadVersion = 1U;
constexpr std::string_view kPersistedActiveProfileMagic = "OOACTPRF";
constexpr std::size_t kPersistedActiveProfileValueBytes = 32U;
constexpr std::string_view kProfilesKeyPrefix = "profiles/";
constexpr std::string_view kDiagnosticCheckpointKeySuffix =
    "/campaigns/diagnostic/checkpoint";
constexpr std::uint32_t kDiagnosticCheckpointSchemaVersion = 1U;
constexpr std::uint16_t kDiagnosticCheckpointPayloadVersion = 1U;
constexpr std::string_view kDiagnosticCheckpointMagic = "OODIAGCP";
constexpr std::size_t kDiagnosticCheckpointValueBytes = 32U;
static_assert(kPersistedActiveProfileMagic.size() == 8U);
static_assert(16U + profiles::ProfileId::FromBytes({}).bytes().size() ==
              kPersistedActiveProfileValueBytes);
static_assert(kDiagnosticCheckpointMagic.size() == 8U);
static_assert(16U + profiles::ProfileId::FromBytes({}).bytes().size() ==
              kDiagnosticCheckpointValueBytes);

[[nodiscard]] NativePersistenceStartupError MakeError(const NativePersistenceStartupErrorCode code,
                                                      std::string message)
{
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] ActiveProfileConfirmationError MakeConfirmationError(
    const ActiveProfileConfirmationErrorCode code, std::string message)
{
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] DiagnosticCampaignStartError MakeDiagnosticStartError(
    const DiagnosticCampaignStartErrorCode code, std::string message)
{
    return {.code = code, .message = std::move(message)};
}

[[nodiscard]] NativePersistenceStartupError PersistedActiveProfileStartupError()
{
    return MakeError(NativePersistenceStartupErrorCode::PersistedActiveProfile,
                     "persisted active profile validation failed");
}

[[nodiscard]] NativePersistenceStartupError PersistedDiagnosticCheckpointStartupError()
{
    return MakeError(
        NativePersistenceStartupErrorCode::PersistedDiagnosticCheckpoint,
        "persisted diagnostic checkpoint validation failed");
}

[[nodiscard]] NativePersistenceStartupError PersistedDiagnosticCheckpointStartupError(
    const persistence::SaveDatabaseErrorCode code)
{
    if (code == persistence::SaveDatabaseErrorCode::LimitExceeded)
    {
        return MakeError(NativePersistenceStartupErrorCode::ResourceExhausted,
                         "native persistence startup allocation failed");
    }
    return PersistedDiagnosticCheckpointStartupError();
}

[[nodiscard]] persistence::SaveDatabaseLimits DefaultNativePersistenceLimits() noexcept
{
    persistence::SaveDatabaseLimits limits;
    // One bounded profile marker and one project diagnostic checkpoint per
    // front-end profile, plus the single active-profile pointer. This remains
    // a ceiling rather than a namespace reservation.
    limits.max_records = 2'049U;
    return limits;
}

void AppendU16(std::vector<std::byte>& bytes, const std::uint16_t value)
{
    bytes.push_back(static_cast<std::byte>(value & 0xffU));
    bytes.push_back(static_cast<std::byte>((value >> 8U) & 0xffU));
}

void AppendU32(std::vector<std::byte>& bytes, const std::uint32_t value)
{
    for (std::uint32_t shift = 0U; shift < 32U; shift += 8U)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffU));
}

[[nodiscard]] std::uint16_t LoadU16(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    return static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U));
}

[[nodiscard]] std::uint32_t LoadU32(const std::span<const std::byte> bytes,
                                    const std::size_t offset) noexcept
{
    std::uint32_t value = 0U;
    for (std::uint32_t index = 0U; index < 4U; ++index)
    {
        value |= static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::vector<std::byte> EncodePersistedActiveProfile(
    const profiles::ProfileId id)
{
    std::vector<std::byte> bytes;
    bytes.reserve(kPersistedActiveProfileValueBytes);
    for (const char value : kPersistedActiveProfileMagic)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    AppendU16(bytes, kPersistedActiveProfilePayloadVersion);
    AppendU16(bytes, 0U);
    AppendU32(bytes, 0U);
    for (const std::uint8_t value : id.bytes())
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

[[nodiscard]] std::optional<profiles::ProfileId> DecodePersistedActiveProfile(
    const persistence::SaveRecord& record) noexcept
{
    if (record.schema_version != kPersistedActiveProfileSchemaVersion ||
        record.value.size() != kPersistedActiveProfileValueBytes)
    {
        return std::nullopt;
    }

    const std::span<const std::byte> bytes(record.value);
    for (std::size_t index = 0U; index < kPersistedActiveProfileMagic.size(); ++index)
    {
        if (std::to_integer<unsigned char>(bytes[index]) !=
            static_cast<unsigned char>(kPersistedActiveProfileMagic[index]))
        {
            return std::nullopt;
        }
    }
    if (LoadU16(bytes, 8U) != kPersistedActiveProfilePayloadVersion ||
        LoadU16(bytes, 10U) != 0U || LoadU32(bytes, 12U) != 0U)
    {
        return std::nullopt;
    }

    std::array<std::uint8_t, 16U> id_bytes{};
    for (std::size_t index = 0U; index < id_bytes.size(); ++index)
        id_bytes[index] = std::to_integer<std::uint8_t>(bytes[16U + index]);
    return profiles::ProfileId::FromBytes(id_bytes);
}

[[nodiscard]] std::string DiagnosticCheckpointKey(const profiles::ProfileId id)
{
    return std::string(kProfilesKeyPrefix) + id.ToString() +
           std::string(kDiagnosticCheckpointKeySuffix);
}

[[nodiscard]] bool IsDiagnosticCheckpointKeyCandidate(
    const std::string_view key) noexcept
{
    return key.starts_with(kProfilesKeyPrefix) &&
           key.ends_with(kDiagnosticCheckpointKeySuffix);
}

[[nodiscard]] std::optional<profiles::ProfileId>
DiagnosticCheckpointProfileIdFromKey(std::string_view key) noexcept
{
    if (!IsDiagnosticCheckpointKeyCandidate(key))
        return std::nullopt;
    key.remove_prefix(kProfilesKeyPrefix.size());
    key.remove_suffix(kDiagnosticCheckpointKeySuffix.size());
    return profiles::ProfileId::Parse(key);
}

[[nodiscard]] std::vector<std::byte> EncodeDiagnosticCheckpoint(
    const profiles::ProfileId id)
{
    std::vector<std::byte> bytes;
    bytes.reserve(kDiagnosticCheckpointValueBytes);
    for (const char value : kDiagnosticCheckpointMagic)
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    AppendU16(bytes, kDiagnosticCheckpointPayloadVersion);
    AppendU16(bytes, 0U);
    AppendU32(bytes, 0U);
    for (const std::uint8_t value : id.bytes())
        bytes.push_back(static_cast<std::byte>(value));
    return bytes;
}

[[nodiscard]] std::optional<profiles::ProfileId> DecodeDiagnosticCheckpoint(
    const persistence::SaveRecord& record) noexcept
{
    if (record.schema_version != kDiagnosticCheckpointSchemaVersion ||
        record.value.size() != kDiagnosticCheckpointValueBytes)
    {
        return std::nullopt;
    }

    const std::span<const std::byte> bytes(record.value);
    for (std::size_t index = 0U; index < kDiagnosticCheckpointMagic.size(); ++index)
    {
        if (std::to_integer<unsigned char>(bytes[index]) !=
            static_cast<unsigned char>(kDiagnosticCheckpointMagic[index]))
        {
            return std::nullopt;
        }
    }
    if (LoadU16(bytes, 8U) != kDiagnosticCheckpointPayloadVersion ||
        LoadU16(bytes, 10U) != 0U || LoadU32(bytes, 12U) != 0U)
    {
        return std::nullopt;
    }

    std::array<std::uint8_t, 16U> id_bytes{};
    for (std::size_t index = 0U; index < id_bytes.size(); ++index)
        id_bytes[index] = std::to_integer<std::uint8_t>(bytes[16U + index]);
    return profiles::ProfileId::FromBytes(id_bytes);
}

[[nodiscard]] bool ContainsProfile(
    const std::span<const profiles::ProfileSummary> profile_summaries,
    const profiles::ProfileId id) noexcept
{
    for (const profiles::ProfileSummary& profile : profile_summaries)
    {
        if (profile.id == id)
            return true;
    }
    return false;
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
    case NativePersistenceStartupErrorCode::PersistedActiveProfile:
        return "persisted-active-profile";
    case NativePersistenceStartupErrorCode::PersistedDiagnosticCheckpoint:
        return "persisted-diagnostic-checkpoint";
    case NativePersistenceStartupErrorCode::ResourceExhausted:
        return "resource-exhausted";
    }
    return "resource-exhausted";
}

std::string_view DiagnosticCampaignStartErrorCodeName(
    const DiagnosticCampaignStartErrorCode code) noexcept
{
    switch (code)
    {
    case DiagnosticCampaignStartErrorCode::ActiveProfileRequired:
        return "active-profile-required";
    case DiagnosticCampaignStartErrorCode::ProfileNotFound:
        return "profile-not-found";
    case DiagnosticCampaignStartErrorCode::RevisionConflict:
        return "revision-conflict";
    case DiagnosticCampaignStartErrorCode::StorageLimitExceeded:
        return "storage-limit-exceeded";
    case DiagnosticCampaignStartErrorCode::StorageFailure:
        return "storage-failure";
    case DiagnosticCampaignStartErrorCode::ResourceExhausted:
        return "resource-exhausted";
    }
    return "storage-failure";
}

std::string_view ActiveProfileConfirmationErrorCodeName(
    const ActiveProfileConfirmationErrorCode code) noexcept
{
    switch (code)
    {
    case ActiveProfileConfirmationErrorCode::ProfileNotFound:
        return "profile-not-found";
    case ActiveProfileConfirmationErrorCode::RevisionConflict:
        return "revision-conflict";
    case ActiveProfileConfirmationErrorCode::StorageLimitExceeded:
        return "storage-limit-exceeded";
    case ActiveProfileConfirmationErrorCode::StorageFailure:
        return "storage-failure";
    case ActiveProfileConfirmationErrorCode::ResourceExhausted:
        return "resource-exhausted";
    }
    return "storage-failure";
}

std::expected<NativePersistence, NativePersistenceStartupError> NativePersistence::Bootstrap(
    std::filesystem::path directory)
{
    return Bootstrap(std::move(directory), DefaultNativePersistenceLimits());
}

std::expected<NativePersistence, NativePersistenceStartupError> NativePersistence::Bootstrap(
    std::filesystem::path directory, persistence::SaveDatabaseLimits limits)
{
    try
    {
        auto opened = persistence::SaveDatabase::Open(
            {.directory = std::move(directory), .limits = std::move(limits)});
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
        auto startup_profiles =
            profile_catalog->ListBounded(kFrontEndMaximumProfiles);
        if (!startup_profiles)
        {
            std::string message = "profile catalog [";
            message += profiles::ProfileCatalogErrorCodeName(startup_profiles.error().code);
            message += "]: ";
            message += startup_profiles.error().message;
            return std::unexpected(MakeError(
                NativePersistenceStartupErrorCode::ProfileCatalogBootstrap, std::move(message)));
        }

        std::optional<profiles::ProfileId> persisted_confirmed_profile_id;
        std::uint64_t persisted_confirmed_profile_revision = 0U;
        auto persisted_active_profile = database->Read(kPersistedActiveProfileKey);
        if (!persisted_active_profile)
            return std::unexpected(PersistedActiveProfileStartupError());
        if (*persisted_active_profile)
        {
            const auto decoded = DecodePersistedActiveProfile(**persisted_active_profile);
            if (!decoded || !ContainsProfile(*startup_profiles, *decoded))
                return std::unexpected(PersistedActiveProfileStartupError());
            persisted_confirmed_profile_id = *decoded;
            persisted_confirmed_profile_revision = (**persisted_active_profile).revision;
        }

        auto profile_records = database->List(kProfilesKeyPrefix);
        if (!profile_records)
        {
            return std::unexpected(PersistedDiagnosticCheckpointStartupError(
                profile_records.error().code));
        }
        for (const persistence::SaveRecordInfo& info : *profile_records)
        {
            if (!IsDiagnosticCheckpointKeyCandidate(info.key))
                continue;

            const auto key_profile_id =
                DiagnosticCheckpointProfileIdFromKey(info.key);
            if (!key_profile_id)
                return std::unexpected(PersistedDiagnosticCheckpointStartupError());

            auto checkpoint = database->Read(info.key);
            if (!checkpoint)
            {
                return std::unexpected(PersistedDiagnosticCheckpointStartupError(
                    checkpoint.error().code));
            }
            if (!*checkpoint)
                return std::unexpected(PersistedDiagnosticCheckpointStartupError());
            const auto checkpoint_profile_id =
                DecodeDiagnosticCheckpoint(**checkpoint);
            if (!checkpoint_profile_id ||
                *checkpoint_profile_id != *key_profile_id ||
                !ContainsProfile(*startup_profiles, *key_profile_id))
            {
                return std::unexpected(PersistedDiagnosticCheckpointStartupError());
            }
        }

        return NativePersistence(std::move(database), std::move(profile_catalog),
                                 std::move(*startup_profiles),
                                 persisted_confirmed_profile_id,
                                 persisted_confirmed_profile_revision);
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
    std::vector<profiles::ProfileSummary> startup_profiles,
    std::optional<profiles::ProfileId> persisted_confirmed_profile_id,
    const std::uint64_t persisted_confirmed_profile_revision) noexcept
    : database_(std::move(database)), profiles_(std::move(profiles)),
      startup_profiles_(std::move(startup_profiles)),
      persisted_confirmed_profile_id_(persisted_confirmed_profile_id),
      persisted_confirmed_profile_revision_(persisted_confirmed_profile_revision)
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

const std::optional<profiles::ProfileId>&
NativePersistence::persisted_confirmed_profile_id() const noexcept
{
    return persisted_confirmed_profile_id_;
}

std::expected<void, ActiveProfileConfirmationError> NativePersistence::ConfirmActiveProfile(
    const profiles::ProfileId id)
{
    try
    {
        auto profile = profiles_->Read(id);
        if (!profile)
        {
            const auto code = profile.error().code == profiles::ProfileCatalogErrorCode::ResourceExhausted
                                  ? ActiveProfileConfirmationErrorCode::ResourceExhausted
                                  : ActiveProfileConfirmationErrorCode::StorageFailure;
            return std::unexpected(MakeConfirmationError(
                code, code == ActiveProfileConfirmationErrorCode::ResourceExhausted
                          ? "active profile confirmation allocation failed"
                          : "active profile confirmation storage failed"));
        }
        if (!*profile)
        {
            return std::unexpected(MakeConfirmationError(
                ActiveProfileConfirmationErrorCode::ProfileNotFound,
                "active profile confirmation requires an existing profile"));
        }

        if (persisted_confirmed_profile_id_ && *persisted_confirmed_profile_id_ == id)
        {
            const auto current_pointer = database_->Read(kPersistedActiveProfileKey);
            if (!current_pointer)
            {
                const auto code = current_pointer.error().code ==
                                          persistence::SaveDatabaseErrorCode::LimitExceeded
                                      ? ActiveProfileConfirmationErrorCode::ResourceExhausted
                                      : ActiveProfileConfirmationErrorCode::StorageFailure;
                return std::unexpected(MakeConfirmationError(
                    code, code == ActiveProfileConfirmationErrorCode::ResourceExhausted
                              ? "active profile confirmation allocation failed"
                              : "active profile confirmation storage failed"));
            }
            const auto decoded = *current_pointer
                                     ? DecodePersistedActiveProfile(**current_pointer)
                                     : std::nullopt;
            if (!*current_pointer || !decoded || *decoded != id ||
                (**current_pointer).revision != persisted_confirmed_profile_revision_)
            {
                return std::unexpected(MakeConfirmationError(
                    ActiveProfileConfirmationErrorCode::RevisionConflict,
                    "persisted active profile changed before confirmation committed"));
            }
            return {};
        }

        const persistence::SaveWriteCondition condition = persisted_confirmed_profile_id_
                                                              ? persistence::SaveWriteCondition::ExactRevision(
                                                                    persisted_confirmed_profile_revision_)
                                                              : persistence::SaveWriteCondition::MustBeAbsent();
        std::array mutation{
            persistence::SaveMutation::Put(std::string(kPersistedActiveProfileKey),
                                           kPersistedActiveProfileSchemaVersion,
                                           EncodePersistedActiveProfile(id), condition),
        };
        const auto committed = database_->Commit(mutation);
        if (!committed)
        {
            switch (committed.error().code)
            {
            case persistence::SaveDatabaseErrorCode::PreconditionFailed:
                return std::unexpected(MakeConfirmationError(
                    ActiveProfileConfirmationErrorCode::RevisionConflict,
                    "persisted active profile changed before confirmation committed"));
            case persistence::SaveDatabaseErrorCode::LimitExceeded:
                return std::unexpected(MakeConfirmationError(
                    ActiveProfileConfirmationErrorCode::StorageLimitExceeded,
                    "active profile confirmation exceeds native storage limits"));
            default:
                return std::unexpected(MakeConfirmationError(
                    ActiveProfileConfirmationErrorCode::StorageFailure,
                    "active profile confirmation storage failed"));
            }
        }

        persisted_confirmed_profile_id_ = id;
        persisted_confirmed_profile_revision_ = *committed;
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(MakeConfirmationError(
            ActiveProfileConfirmationErrorCode::ResourceExhausted,
            "active profile confirmation allocation failed"));
    }
}

std::expected<void, DiagnosticCampaignStartError>
NativePersistence::PrepareDiagnosticCampaignStart(const profiles::ProfileId id)
{
    try
    {
        if (!persisted_confirmed_profile_id_ ||
            *persisted_confirmed_profile_id_ != id)
        {
            return std::unexpected(MakeDiagnosticStartError(
                DiagnosticCampaignStartErrorCode::ActiveProfileRequired,
                "diagnostic campaign start requires the same confirmed active profile"));
        }

        auto current_pointer = database_->Read(kPersistedActiveProfileKey);
        if (!current_pointer)
        {
            const auto code = current_pointer.error().code ==
                                      persistence::SaveDatabaseErrorCode::LimitExceeded
                                  ? DiagnosticCampaignStartErrorCode::ResourceExhausted
                                  : DiagnosticCampaignStartErrorCode::StorageFailure;
            return std::unexpected(MakeDiagnosticStartError(
                code, code == DiagnosticCampaignStartErrorCode::ResourceExhausted
                          ? "diagnostic campaign start allocation failed"
                          : "diagnostic campaign checkpoint storage failed"));
        }
        const auto current_profile = *current_pointer
                                         ? DecodePersistedActiveProfile(**current_pointer)
                                         : std::nullopt;
        if (!*current_pointer || !current_profile || *current_profile != id ||
            (**current_pointer).revision != persisted_confirmed_profile_revision_)
        {
            return std::unexpected(MakeDiagnosticStartError(
                DiagnosticCampaignStartErrorCode::RevisionConflict,
                "diagnostic campaign start observed changed persisted state"));
        }

        auto profile = profiles_->Read(id);
        if (!profile)
        {
            const auto code = profile.error().code ==
                                      profiles::ProfileCatalogErrorCode::ResourceExhausted
                                  ? DiagnosticCampaignStartErrorCode::ResourceExhausted
                                  : DiagnosticCampaignStartErrorCode::StorageFailure;
            return std::unexpected(MakeDiagnosticStartError(
                code, code == DiagnosticCampaignStartErrorCode::ResourceExhausted
                          ? "diagnostic campaign start allocation failed"
                          : "diagnostic campaign checkpoint storage failed"));
        }
        if (!*profile)
        {
            return std::unexpected(MakeDiagnosticStartError(
                DiagnosticCampaignStartErrorCode::ProfileNotFound,
                "diagnostic campaign start requires an existing profile"));
        }

        const std::string checkpoint_key = DiagnosticCheckpointKey(id);
        auto existing = database_->Read(checkpoint_key);
        if (!existing)
        {
            const auto code = existing.error().code ==
                                      persistence::SaveDatabaseErrorCode::LimitExceeded
                                  ? DiagnosticCampaignStartErrorCode::ResourceExhausted
                                  : DiagnosticCampaignStartErrorCode::StorageFailure;
            return std::unexpected(MakeDiagnosticStartError(
                code, code == DiagnosticCampaignStartErrorCode::ResourceExhausted
                          ? "diagnostic campaign start allocation failed"
                          : "diagnostic campaign checkpoint storage failed"));
        }
        if (*existing)
        {
            const auto decoded = DecodeDiagnosticCheckpoint(**existing);
            if (!decoded || *decoded != id)
            {
                return std::unexpected(MakeDiagnosticStartError(
                    DiagnosticCampaignStartErrorCode::RevisionConflict,
                    "diagnostic campaign start observed changed persisted state"));
            }
            return {};
        }

        std::array mutation{
            persistence::SaveMutation::Put(
                checkpoint_key, kDiagnosticCheckpointSchemaVersion,
                EncodeDiagnosticCheckpoint(id),
                persistence::SaveWriteCondition::MustBeAbsent()),
        };
        const auto committed = database_->Commit(mutation);
        if (!committed)
        {
            switch (committed.error().code)
            {
            case persistence::SaveDatabaseErrorCode::PreconditionFailed:
                return std::unexpected(MakeDiagnosticStartError(
                    DiagnosticCampaignStartErrorCode::RevisionConflict,
                    "diagnostic campaign start observed changed persisted state"));
            case persistence::SaveDatabaseErrorCode::LimitExceeded:
                return std::unexpected(MakeDiagnosticStartError(
                    DiagnosticCampaignStartErrorCode::StorageLimitExceeded,
                    "diagnostic campaign checkpoint exceeds native storage limits"));
            default:
                return std::unexpected(MakeDiagnosticStartError(
                    DiagnosticCampaignStartErrorCode::StorageFailure,
                    "diagnostic campaign checkpoint storage failed"));
            }
        }
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(MakeDiagnosticStartError(
            DiagnosticCampaignStartErrorCode::ResourceExhausted,
            "diagnostic campaign start allocation failed"));
    }
}
} // namespace omega::app
