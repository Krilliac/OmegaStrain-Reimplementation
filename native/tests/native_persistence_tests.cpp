#include "native_persistence.h"
#include "front_end.h"

#include "omega/persistence/save_database.h"
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

namespace
{
using namespace std::string_view_literals;

using omega::app::ActiveProfileConfirmationError;
using omega::app::ActiveProfileConfirmationErrorCode;
using omega::app::DiagnosticCampaignStartError;
using omega::app::DiagnosticCampaignStartErrorCode;
using omega::app::NativePersistence;
using omega::app::NativePersistenceStartupError;
using omega::app::NativePersistenceStartupErrorCode;
using omega::persistence::SaveDatabase;
using omega::persistence::SaveDatabaseErrorCode;
using omega::persistence::SaveDatabaseLimits;
using omega::persistence::SaveMutation;
using omega::persistence::SaveWriteCondition;
using omega::profiles::ProfileCatalog;
using omega::profiles::ProfileId;
using omega::profiles::ProfileMetadata;

constexpr std::string_view kActiveProfileKey = "profiles/active";
constexpr std::uint32_t kActiveProfileSchemaVersion = 1U;
constexpr std::uint32_t kDiagnosticCheckpointSchemaVersion = 1U;
constexpr std::string_view kDiagnosticCheckpointSuffix =
    "/campaigns/diagnostic/checkpoint";

static_assert(sizeof(NativePersistenceStartupErrorCode) == 1U);
static_assert(sizeof(ActiveProfileConfirmationErrorCode) == 1U);
static_assert(sizeof(DiagnosticCampaignStartErrorCode) == 1U);

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <class T>
void CheckStartupError(const std::expected<T, NativePersistenceStartupError>& result,
                       const NativePersistenceStartupErrorCode expected,
                       const std::string_view message)
{
    if (!result && result.error().code == expected)
        return;
    std::cerr << "FAILED: " << message
              << "\n  expected: " << omega::app::NativePersistenceStartupErrorCodeName(expected)
              << "\n  actual:   "
              << (result ? "<success>"
                         : omega::app::NativePersistenceStartupErrorCodeName(result.error().code))
              << '\n';
    ++failures;
}

void CheckConfirmationError(
    const std::expected<void, ActiveProfileConfirmationError>& result,
    const ActiveProfileConfirmationErrorCode expected,
    const std::string_view message)
{
    if (!result && result.error().code == expected)
        return;
    std::cerr << "FAILED: " << message
              << "\n  expected: " << omega::app::ActiveProfileConfirmationErrorCodeName(expected)
              << "\n  actual:   "
              << (result ? "<success>"
                         : omega::app::ActiveProfileConfirmationErrorCodeName(result.error().code))
              << '\n';
    ++failures;
}

void CheckDiagnosticStartError(
    const std::expected<void, DiagnosticCampaignStartError>& result,
    const DiagnosticCampaignStartErrorCode expected,
    const std::string_view message)
{
    if (!result && result.error().code == expected)
        return;
    std::cerr << "FAILED: " << message
              << "\n  expected: "
              << omega::app::DiagnosticCampaignStartErrorCodeName(expected)
              << "\n  actual:   "
              << (result
                      ? "<success>"
                      : omega::app::DiagnosticCampaignStartErrorCodeName(
                            result.error().code))
              << '\n';
    ++failures;
}

[[nodiscard]] std::vector<std::byte> Bytes(const std::string_view text)
{
    std::vector<std::byte> result;
    result.reserve(text.size());
    for (const char value : text)
        result.push_back(static_cast<std::byte>(static_cast<unsigned char>(value)));
    return result;
}

[[nodiscard]] ProfileId Id(const std::string_view text)
{
    const auto parsed = ProfileId::Parse(text);
    Check(parsed.has_value(), "the native-persistence test profile ID parses");
    if (parsed)
        return *parsed;
    return ProfileId::FromBytes({});
}

[[nodiscard]] constexpr ProfileId SequentialId(const std::size_t index) noexcept
{
    std::array<std::uint8_t, 16U> bytes{};
    const std::uint32_t ordinal = static_cast<std::uint32_t>(index + 1U);
    bytes[12] = static_cast<std::uint8_t>((ordinal >> 24U) & 0xffU);
    bytes[13] = static_cast<std::uint8_t>((ordinal >> 16U) & 0xffU);
    bytes[14] = static_cast<std::uint8_t>((ordinal >> 8U) & 0xffU);
    bytes[15] = static_cast<std::uint8_t>(ordinal & 0xffU);
    return ProfileId::FromBytes(bytes);
}

[[nodiscard]] std::vector<std::byte> ActiveProfileValue(const ProfileId id)
{
    std::vector<std::byte> value = Bytes("OOACTPRF");
    value.resize(16U, std::byte{0});
    value[8U] = std::byte{1U};
    for (const std::uint8_t id_byte : id.bytes())
        value.push_back(static_cast<std::byte>(id_byte));
    return value;
}

[[nodiscard]] std::vector<std::byte> DiagnosticCheckpointValue(
    const ProfileId id)
{
    std::vector<std::byte> value = Bytes("OODIAGCP");
    value.resize(16U, std::byte{0});
    value[8U] = std::byte{1U};
    for (const std::uint8_t id_byte : id.bytes())
        value.push_back(static_cast<std::byte>(id_byte));
    return value;
}

[[nodiscard]] std::string DiagnosticCheckpointKey(const ProfileId id)
{
    return "profiles/" + id.ToString() +
           std::string(kDiagnosticCheckpointSuffix);
}

[[nodiscard]] std::string ProfileMetadataKey(const ProfileId id)
{
    return "profiles/" + id.ToString() + "/metadata";
}

class TempDirectory final
{
public:
    explicit TempDirectory(const std::string_view label)
    {
        static std::atomic<std::uint64_t> next{0U};
        const auto tick = std::chrono::steady_clock::now().time_since_epoch().count();
        root_ = std::filesystem::temp_directory_path() /
                ("openomega-native-persistence-tests-" + std::string(label) + "-" +
                 std::to_string(tick) + "-" + std::to_string(next.fetch_add(1U)));
        std::error_code error;
        std::filesystem::create_directories(root_, error);
        Check(!error, "the synthetic native-persistence directory is created");
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        Check(!error, "the synthetic native-persistence directory is removed");
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return root_;
    }

private:
    std::filesystem::path root_;
};

[[nodiscard]] bool CreateProfile(NativePersistence& persistence, const ProfileId id,
                                 std::string display_name)
{
    const auto created = persistence.profiles().Create(
        id, ProfileMetadata{
                .display_name = std::move(display_name),
                .created_unix_milliseconds = 1'000U,
                .modified_unix_milliseconds = 1'000U,
            });
    Check(created.has_value(), "the synthetic profile marker is created");
    return created.has_value();
}

[[nodiscard]] std::optional<std::uint64_t> PutActiveProfile(
    SaveDatabase& database, const std::uint32_t schema_version,
    std::vector<std::byte> value,
    const SaveWriteCondition condition = SaveWriteCondition::MustBeAbsent())
{
    std::array mutation{
        SaveMutation::Put(std::string(kActiveProfileKey), schema_version, std::move(value), condition),
    };
    const auto committed = database.Commit(mutation);
    Check(committed.has_value(), "the raw active-profile fixture commits");
    if (!committed)
        return std::nullopt;
    return *committed;
}

[[nodiscard]] std::optional<std::uint64_t> PutDiagnosticCheckpoint(
    SaveDatabase& database, const ProfileId key_id,
    const std::uint32_t schema_version, std::vector<std::byte> value,
    const SaveWriteCondition condition = SaveWriteCondition::MustBeAbsent())
{
    std::array mutation{
        SaveMutation::Put(DiagnosticCheckpointKey(key_id), schema_version,
                          std::move(value), condition),
    };
    const auto committed = database.Commit(mutation);
    Check(committed.has_value(), "the raw diagnostic-checkpoint fixture commits");
    if (!committed)
        return std::nullopt;
    return *committed;
}

void CheckPrivateErrorMessage(const std::string_view actual,
                              const std::string_view exact,
                              const std::filesystem::path& root,
                              const ProfileId id,
                              const std::string_view context)
{
    Check(actual == exact, context);
    Check(actual.find(root.string()) == std::string_view::npos,
          "a typed active-profile error does not expose its database path");
    Check(actual.find(id.ToString()) == std::string_view::npos,
          "a typed active-profile error does not expose its profile ID");
    Check(actual.find(kActiveProfileKey) == std::string_view::npos,
          "a typed active-profile error does not expose its database key");
    Check(actual.find("OOACTPRF") == std::string_view::npos,
          "a typed active-profile error does not expose persisted bytes");
}

void CheckDiagnosticPrivateErrorMessage(
    const std::string_view actual, const std::string_view exact,
    const std::filesystem::path& root, const ProfileId id,
    const std::string_view context)
{
    Check(actual == exact, context);
    Check(actual.find(root.string()) == std::string_view::npos,
          "a typed diagnostic-start error does not expose its database path");
    Check(actual.find(id.ToString()) == std::string_view::npos,
          "a typed diagnostic-start error does not expose its profile ID");
    Check(actual.find(kDiagnosticCheckpointSuffix) == std::string_view::npos,
          "a typed diagnostic-start error does not expose its database key");
    Check(actual.find("OODIAGCP") == std::string_view::npos,
          "a typed diagnostic-start error does not expose persisted bytes");
}

void CheckErrorNames()
{
    constexpr std::array startup_expected{
        "database-open"sv,
        "profile-catalog-bootstrap"sv,
        "persisted-active-profile"sv,
        "resource-exhausted"sv,
        "persisted-diagnostic-checkpoint"sv,
    };
    for (std::size_t index = 0U; index < startup_expected.size(); ++index)
    {
        Check(omega::app::NativePersistenceStartupErrorCodeName(
                  static_cast<NativePersistenceStartupErrorCode>(index)) == startup_expected[index],
              "every native-persistence startup error has a fixed name");
    }
    Check(omega::app::NativePersistenceStartupErrorCodeName(
              static_cast<NativePersistenceStartupErrorCode>(0xffU)) == "resource-exhausted",
          "an invalid native-persistence startup error uses the fixed fallback");

    constexpr std::array confirmation_expected{
        "profile-not-found"sv,
        "revision-conflict"sv,
        "storage-limit-exceeded"sv,
        "storage-failure"sv,
        "resource-exhausted"sv,
    };
    for (std::size_t index = 0U; index < confirmation_expected.size(); ++index)
    {
        Check(omega::app::ActiveProfileConfirmationErrorCodeName(
                  static_cast<ActiveProfileConfirmationErrorCode>(index)) ==
                  confirmation_expected[index],
              "every active-profile confirmation error has a fixed name");
    }
    Check(omega::app::ActiveProfileConfirmationErrorCodeName(
              static_cast<ActiveProfileConfirmationErrorCode>(0xffU)) == "storage-failure",
          "an invalid active-profile confirmation error uses the fixed fallback");

    constexpr std::array diagnostic_start_expected{
        "active-profile-required"sv,
        "profile-not-found"sv,
        "revision-conflict"sv,
        "storage-limit-exceeded"sv,
        "storage-failure"sv,
        "resource-exhausted"sv,
    };
    for (std::size_t index = 0U; index < diagnostic_start_expected.size(); ++index)
    {
        Check(omega::app::DiagnosticCampaignStartErrorCodeName(
                  static_cast<DiagnosticCampaignStartErrorCode>(index)) ==
                  diagnostic_start_expected[index],
              "every diagnostic-campaign start error has a fixed name");
    }
    Check(omega::app::DiagnosticCampaignStartErrorCodeName(
              static_cast<DiagnosticCampaignStartErrorCode>(0xffU)) ==
              "storage-failure",
          "an invalid diagnostic-campaign start error uses the fixed fallback");
}

void CheckAbsentBootstrapAndLimits()
{
    TempDirectory tree("absent");
    const auto database_root = tree.path() / "native-save";
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence && persistence->startup_profiles().empty() &&
              !persistence->persisted_confirmed_profile_id() &&
              persistence->database().generation() == 0U,
          "fresh bootstrap accepts an absent persisted active profile");
    if (!persistence)
        return;

    const SaveDatabaseLimits defaults;
    const auto* config = persistence->database().config();
    Check(config && config->limits.max_records ==
                        SaveDatabase::kHardMaxRecords,
          "production native persistence uses the storage layer's hard project-record ceiling");
    Check(config && config->limits.max_mutations_per_commit ==
                        defaults.max_mutations_per_commit &&
              config->limits.max_key_bytes ==
                  SaveDatabase::kHardMaxKeyBytes &&
              config->limits.max_value_bytes == defaults.max_value_bytes &&
              config->limits.max_logical_value_bytes == defaults.max_logical_value_bytes &&
              config->limits.max_file_bytes == defaults.max_file_bytes,
          "production native persistence uses the hard record and key ceilings needed by profile-scoped character sessions");

    const ProfileId missing = Id("ffffffffffffffffffffffffffffffff");
    const std::uint64_t before_generation = persistence->database().generation();
    const auto confirmation = persistence->ConfirmActiveProfile(missing);
    CheckConfirmationError(confirmation, ActiveProfileConfirmationErrorCode::ProfileNotFound,
                           "an absent profile cannot become durably confirmed");
    Check(!confirmation &&
              confirmation.error().message ==
                  "active profile confirmation requires an existing profile",
          "missing-profile confirmation uses a fixed sanitized message");
    Check(persistence->database().generation() == before_generation &&
              !persistence->persisted_confirmed_profile_id(),
          "missing-profile confirmation publishes no state");
    const auto pointer = persistence->database().Read(kActiveProfileKey);
    Check(pointer && !*pointer, "missing-profile confirmation writes no pointer record");
}

void CheckFrontEndStartupProfileBudget()
{
    TempDirectory tree("front-end-startup-profile-budget");
    const auto database_root = tree.path() / "native-save";
    constexpr std::size_t kStartupProfileMaximum =
        omega::app::kFrontEndMaximumProfiles;
    static_assert(kStartupProfileMaximum == 1'024U);

    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(),
              "the front-end startup-budget database bootstraps empty");
        if (!persistence)
            return;

        const ProfileId first = SequentialId(0U);
        const auto created = persistence->profiles().Create(
            first, ProfileMetadata{
                       .display_name = "bounded",
                       .created_unix_milliseconds = 1'000U,
                       .modified_unix_milliseconds = 1'000U,
                   });
        Check(created.has_value(),
              "the startup-budget seed profile commits through the typed catalog");
        if (!created)
            return;

        const auto encoded_seed =
            persistence->database().Read(ProfileMetadataKey(first));
        Check(encoded_seed && *encoded_seed,
              "the generated startup-budget seed metadata is readable");
        if (!encoded_seed || !*encoded_seed)
            return;

        const auto* config = persistence->database().config();
        Check(config && config->limits.max_mutations_per_commit >= 1U,
              "the startup-budget fixture exposes a nonzero mutation batch");
        if (!config || config->limits.max_mutations_per_commit == 0U)
            return;

        std::vector<SaveMutation> batch;
        batch.reserve(config->limits.max_mutations_per_commit);
        bool populated = true;
        for (std::size_t index = 1U; index < kStartupProfileMaximum; ++index)
        {
            batch.push_back(SaveMutation::Put(
                ProfileMetadataKey(SequentialId(index)),
                (**encoded_seed).schema_version, (**encoded_seed).value,
                SaveWriteCondition::MustBeAbsent()));
            if (batch.size() == config->limits.max_mutations_per_commit ||
                index + 1U == kStartupProfileMaximum)
            {
                const auto committed = persistence->database().Commit(batch);
                populated = populated && committed.has_value();
                batch.clear();
                if (!committed)
                    break;
            }
        }
        Check(populated && persistence->database().record_count() ==
                               kStartupProfileMaximum,
              "exactly 1,024 generated direct markers populate in bounded batches");
        if (!populated)
            return;

        std::array noise{
            SaveMutation::Put(
                "profiles/not-a-profile-id/metadata", 1U, Bytes("unparsed"),
                SaveWriteCondition::MustBeAbsent()),
            SaveMutation::Put(
                DiagnosticCheckpointKey(first),
                kDiagnosticCheckpointSchemaVersion,
                DiagnosticCheckpointValue(first),
                SaveWriteCondition::MustBeAbsent()),
            SaveMutation::Put(
                "profiles/00000000000000000000000000000001/notes/generated",
                1U, Bytes("noise"), SaveWriteCondition::MustBeAbsent()),
        };
        const auto committed_noise = persistence->database().Commit(noise);
        Check(committed_noise && persistence->database().record_count() ==
                                     kStartupProfileMaximum + noise.size(),
              "the 1,025th malformed direct marker, valid checkpoint, and non-marker noise commit together");
        if (!committed_noise)
            return;
    }

    const auto rejected = NativePersistence::Bootstrap(database_root);
    CheckStartupError(rejected,
        NativePersistenceStartupErrorCode::ProfileCatalogBootstrap,
        "startup rejects the 1,025th direct marker at the front-end budget");
    Check(!rejected &&
              rejected.error().message.starts_with(
                  "profile catalog [resource-exhausted]:"),
          "startup spends the direct-marker budget before parsing the malformed 1,025th marker while checkpoint and non-marker records spend no budget");
}

void CheckEncodeIdempotenceReplacementMoveAndReopen()
{
    TempDirectory tree("lifecycle");
    const auto database_root = tree.path() / "native-save";
    const ProfileId first = Id("00112233445566778899aabbccddeeff");
    const ProfileId second = Id("ffeeddccbbaa99887766554433221100");

    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(), "the lifecycle fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, first, "First") ||
            !CreateProfile(*persistence, second, "Second"))
        {
            return;
        }

        Check(!persistence->persisted_confirmed_profile_id(),
              "creating profile metadata does not imply durable confirmation");
        const std::uint64_t before_confirmation = persistence->database().generation();
        const auto confirmed = persistence->ConfirmActiveProfile(first);
        Check(confirmed.has_value() &&
                  persistence->database().generation() == before_confirmation + 1U &&
                  persistence->persisted_confirmed_profile_id() == first,
              "first confirmation publishes exactly one durable generation");

        const auto encoded = persistence->database().Read(kActiveProfileKey);
        Check(encoded && *encoded && (**encoded).schema_version == kActiveProfileSchemaVersion &&
                  (**encoded).value == ActiveProfileValue(first) &&
                  (**encoded).value.size() == 32U &&
                  (**encoded).revision == persistence->database().generation(),
              "confirmation emits the exact schema-1 32-byte active-profile value");

        const std::uint64_t idempotent_generation = persistence->database().generation();
        const std::uint64_t idempotent_revision = encoded && *encoded ? (**encoded).revision : 0U;
        const auto idempotent = persistence->ConfirmActiveProfile(first);
        const auto after_idempotent = persistence->database().Read(kActiveProfileKey);
        Check(idempotent.has_value() &&
                  persistence->database().generation() == idempotent_generation &&
                  after_idempotent && *after_idempotent &&
                  (**after_idempotent).revision == idempotent_revision,
              "confirming the exact persisted ID is a no-commit idempotent success");

        std::array unrelated_mutation{
            SaveMutation::Put("fixture/unrelated", 1U, Bytes("generated"),
                              SaveWriteCondition::MustBeAbsent()),
        };
        const auto unrelated_commit =
            persistence->database().Commit(unrelated_mutation);
        const auto pointer_after_unrelated =
            persistence->database().Read(kActiveProfileKey);
        Check(unrelated_commit &&
                  persistence->database().generation() ==
                      idempotent_generation + 1U &&
                  pointer_after_unrelated && *pointer_after_unrelated &&
                  (**pointer_after_unrelated).revision == idempotent_revision,
              "an unrelated commit advances the database without changing the active pointer revision");

        const auto replaced = persistence->ConfirmActiveProfile(second);
        const auto replacement_record = persistence->database().Read(kActiveProfileKey);
        Check(replaced.has_value() &&
                  persistence->database().generation() == idempotent_generation + 2U &&
                  persistence->persisted_confirmed_profile_id() == second &&
                  replacement_record && *replacement_record &&
                  (**replacement_record).value == ActiveProfileValue(second) &&
                  (**replacement_record).revision == persistence->database().generation(),
              "replacement atomically publishes the newly confirmed profile and revision");

        NativePersistence moved = std::move(*persistence);
        Check(moved.persisted_confirmed_profile_id() == second,
              "move construction preserves the owned persisted confirmation snapshot");
        const std::uint64_t moved_generation = moved.database().generation();
        Check(moved.ConfirmActiveProfile(second).has_value() &&
                  moved.database().generation() == moved_generation,
              "the moved owner preserves idempotent confirmation semantics");
    }

    auto reopened = NativePersistence::Bootstrap(database_root);
    Check(reopened && reopened->startup_profiles().size() == 2U &&
              reopened->persisted_confirmed_profile_id() == second,
          "reopen decodes the exact durable confirmation beside typed profile metadata");
    if (reopened)
    {
        const std::uint64_t generation = reopened->database().generation();
        Check(reopened->ConfirmActiveProfile(second).has_value() &&
                  reopened->database().generation() == generation,
              "decoded confirmation remains idempotent after reopen");
    }
}

void CheckDiagnosticCampaignCheckpointLifecycle()
{
    TempDirectory tree("diagnostic-checkpoint-lifecycle");
    const auto database_root = tree.path() / "native-save";
    const ProfileId first = Id("0a0b0c0d0e0f10111213141516171819");
    const ProfileId second = Id("191817161514131211100f0e0d0c0b0a");

    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(),
              "the diagnostic-checkpoint lifecycle fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, first, "First") ||
            !CreateProfile(*persistence, second, "Second"))
        {
            return;
        }

        const std::uint64_t before_unconfirmed =
            persistence->database().generation();
        const auto unconfirmed =
            persistence->PrepareDiagnosticCampaignStart(first);
        CheckDiagnosticStartError(
            unconfirmed,
            DiagnosticCampaignStartErrorCode::ActiveProfileRequired,
            "diagnostic start requires an explicit active-profile confirmation");
        Check(!unconfirmed &&
                  unconfirmed.error().message ==
                      "diagnostic campaign start requires the same confirmed active profile" &&
                  persistence->database().generation() == before_unconfirmed,
              "unconfirmed diagnostic start uses a fixed private error and writes nothing");
        const auto absent_checkpoint =
            persistence->database().Read(DiagnosticCheckpointKey(first));
        Check(absent_checkpoint && !*absent_checkpoint,
              "unconfirmed diagnostic start creates no checkpoint record");

        if (!persistence->ConfirmActiveProfile(first))
        {
            Check(false,
                  "the lifecycle fixture confirms its first active profile");
            return;
        }
        const std::uint64_t before_wrong_profile =
            persistence->database().generation();
        const auto wrong_profile =
            persistence->PrepareDiagnosticCampaignStart(second);
        CheckDiagnosticStartError(
            wrong_profile,
            DiagnosticCampaignStartErrorCode::ActiveProfileRequired,
            "diagnostic start rejects a profile other than the confirmed active profile");
        Check(persistence->database().generation() == before_wrong_profile,
              "same-profile prerequisite failure publishes no generation");

        const auto prepared =
            persistence->PrepareDiagnosticCampaignStart(first);
        const auto checkpoint =
            persistence->database().Read(DiagnosticCheckpointKey(first));
        Check(prepared &&
                  persistence->database().generation() ==
                      before_wrong_profile + 1U &&
                  checkpoint && *checkpoint &&
                  (**checkpoint).key == DiagnosticCheckpointKey(first) &&
                  (**checkpoint).schema_version ==
                      kDiagnosticCheckpointSchemaVersion &&
                  (**checkpoint).value == DiagnosticCheckpointValue(first) &&
                  (**checkpoint).value.size() == 32U &&
                  (**checkpoint).revision ==
                      persistence->database().generation(),
              "diagnostic preparation publishes the exact schema-1 32-byte profile-bound marker");

        const std::uint64_t idempotent_generation =
            persistence->database().generation();
        const std::uint64_t checkpoint_revision =
            checkpoint && *checkpoint ? (**checkpoint).revision : 0U;
        const auto idempotent =
            persistence->PrepareDiagnosticCampaignStart(first);
        const auto after_idempotent =
            persistence->database().Read(DiagnosticCheckpointKey(first));
        Check(idempotent &&
                  persistence->database().generation() ==
                      idempotent_generation &&
                  after_idempotent && *after_idempotent &&
                  (**after_idempotent).revision == checkpoint_revision,
              "an exact diagnostic checkpoint is a no-write idempotent success");

        std::array unrelated_mutation{
            SaveMutation::Put("fixture/diagnostic-unrelated", 1U,
                              Bytes("generated"),
                              SaveWriteCondition::MustBeAbsent()),
        };
        const auto unrelated =
            persistence->database().Commit(unrelated_mutation);
        const auto after_unrelated =
            persistence->PrepareDiagnosticCampaignStart(first);
        const auto stable_checkpoint =
            persistence->database().Read(DiagnosticCheckpointKey(first));
        Check(unrelated && after_unrelated &&
                  persistence->database().generation() ==
                      idempotent_generation + 1U &&
                  stable_checkpoint && *stable_checkpoint &&
                  (**stable_checkpoint).revision == checkpoint_revision,
              "an unrelated generation does not turn idempotent diagnostic preparation into a write");

        NativePersistence moved = std::move(*persistence);
        const std::uint64_t moved_generation = moved.database().generation();
        Check(moved.PrepareDiagnosticCampaignStart(first).has_value() &&
                  moved.database().generation() == moved_generation,
              "move construction preserves diagnostic checkpoint idempotence");
    }

    {
        auto reopened = NativePersistence::Bootstrap(database_root);
        Check(reopened && reopened->startup_profiles().size() == 2U &&
                  reopened->persisted_confirmed_profile_id() == first,
              "reopen validates the profile-bound diagnostic checkpoint beside the active pointer");
        if (!reopened)
            return;

        const std::uint64_t before_idempotent =
            reopened->database().generation();
        Check(reopened->PrepareDiagnosticCampaignStart(first).has_value() &&
                  reopened->database().generation() == before_idempotent,
              "a validated reopened diagnostic checkpoint remains a no-write success");
        if (!reopened->ConfirmActiveProfile(second))
        {
            Check(false,
                  "the reopened lifecycle fixture confirms its second profile");
            return;
        }
        const std::uint64_t before_second = reopened->database().generation();
        const auto second_prepared =
            reopened->PrepareDiagnosticCampaignStart(second);
        const auto second_checkpoint =
            reopened->database().Read(DiagnosticCheckpointKey(second));
        Check(second_prepared &&
                  reopened->database().generation() == before_second + 1U &&
                  second_checkpoint && *second_checkpoint &&
                  (**second_checkpoint).value ==
                      DiagnosticCheckpointValue(second),
              "each confirmed profile receives its own fixed diagnostic checkpoint key");
    }

    auto verified = NativePersistence::Bootstrap(database_root);
    bool first_checkpoint_exists = false;
    bool second_checkpoint_exists = false;
    if (verified)
    {
        const auto first_checkpoint =
            verified->database().Read(DiagnosticCheckpointKey(first));
        const auto second_checkpoint =
            verified->database().Read(DiagnosticCheckpointKey(second));
        first_checkpoint_exists = first_checkpoint && *first_checkpoint;
        second_checkpoint_exists = second_checkpoint && *second_checkpoint;
    }
    Check(verified && verified->startup_profiles().size() == 2U &&
              verified->persisted_confirmed_profile_id() == second &&
              first_checkpoint_exists && second_checkpoint_exists,
          "a second reopen validates both independent project diagnostic checkpoints");
}

struct InvalidPointerFixture
{
    std::string label;
    std::uint32_t schema_version = kActiveProfileSchemaVersion;
    std::vector<std::byte> value;
    bool create_target = true;
};

void CheckGeneratedDecodeFailures()
{
    const ProfileId id = Id("102132435465768798a9bacbdcedfe0f");
    const std::vector<std::byte> valid = ActiveProfileValue(id);
    std::vector<InvalidPointerFixture> fixtures;

    constexpr std::array invalid_sizes{0U, 7U, 8U, 15U, 16U, 31U, 33U};
    for (const std::size_t size : invalid_sizes)
    {
        auto wrong_size = valid;
        wrong_size.resize(size, std::byte{0U});
        fixtures.push_back({
            .label = "size-" + std::to_string(size),
            .value = std::move(wrong_size),
        });
    }

    for (std::size_t index = 0U; index < 8U; ++index)
    {
        auto bad_magic = valid;
        bad_magic[index] ^= std::byte{1U};
        fixtures.push_back({
            .label = "magic-" + std::to_string(index),
            .value = std::move(bad_magic),
        });
    }

    for (const std::size_t index : {8U, 9U})
    {
        auto future_payload = valid;
        future_payload[index] = index == 8U ? std::byte{2U} : std::byte{1U};
        fixtures.push_back({
            .label = "future-payload-" + std::to_string(index),
            .value = std::move(future_payload),
        });
    }

    for (const std::size_t index : {10U, 11U})
    {
        auto flags = valid;
        flags[index] = std::byte{1U};
        fixtures.push_back({
            .label = "flags-" + std::to_string(index),
            .value = std::move(flags),
        });
    }

    for (const std::size_t index : {12U, 13U, 14U, 15U})
    {
        auto reserved = valid;
        reserved[index] = std::byte{1U};
        fixtures.push_back({
            .label = "reserved-" + std::to_string(index),
            .value = std::move(reserved),
        });
    }

    fixtures.push_back({
        .label = "future-schema",
        .schema_version = 2U,
        .value = valid,
    });
    fixtures.push_back({
        .label = "stale-target",
        .value = valid,
        .create_target = false,
    });

    for (const InvalidPointerFixture& fixture : fixtures)
    {
        TempDirectory tree(fixture.label);
        const auto database_root = tree.path() / "native-save";
        {
            auto database = SaveDatabase::Open({.directory = database_root});
            Check(database.has_value(), "the invalid-pointer fixture database opens");
            if (!database)
                continue;
            ProfileCatalog catalog(*database);
            if (fixture.create_target)
            {
                const auto created = catalog.Create(
                    id, ProfileMetadata{
                            .display_name = "Fixture",
                            .created_unix_milliseconds = 1'000U,
                            .modified_unix_milliseconds = 1'000U,
                        });
                Check(created.has_value(), "the invalid-pointer target profile is created");
            }
            static_cast<void>(PutActiveProfile(*database, fixture.schema_version, fixture.value));
        }

        const auto bootstrap = NativePersistence::Bootstrap(database_root);
        CheckStartupError(bootstrap, NativePersistenceStartupErrorCode::PersistedActiveProfile,
                          "every invalid persisted active-profile fixture fails in one category");
        if (!bootstrap)
        {
            CheckPrivateErrorMessage(bootstrap.error().message,
                                     "persisted active profile validation failed", tree.path(), id,
                                     "invalid active-profile startup uses one fixed message");
        }
    }
}

struct InvalidDiagnosticCheckpointFixture
{
    std::string label;
    std::uint32_t schema_version = kDiagnosticCheckpointSchemaVersion;
    std::vector<std::byte> value;
    bool create_target = true;
};

void CheckGeneratedDiagnosticCheckpointDecodeFailures()
{
    const ProfileId id = Id("abcdef0123456789abcdef0123456789");
    const ProfileId other = Id("9876543210fedcba9876543210fedcba");
    const std::vector<std::byte> valid = DiagnosticCheckpointValue(id);
    std::vector<InvalidDiagnosticCheckpointFixture> fixtures;

    constexpr std::array invalid_sizes{0U, 7U, 8U, 15U, 16U, 31U, 33U};
    for (const std::size_t size : invalid_sizes)
    {
        auto wrong_size = valid;
        wrong_size.resize(size, std::byte{0U});
        fixtures.push_back({
            .label = "diagnostic-size-" + std::to_string(size),
            .value = std::move(wrong_size),
        });
    }

    for (std::size_t index = 0U; index < 8U; ++index)
    {
        auto bad_magic = valid;
        bad_magic[index] ^= std::byte{1U};
        fixtures.push_back({
            .label = "diagnostic-magic-" + std::to_string(index),
            .value = std::move(bad_magic),
        });
    }

    for (const std::size_t index : {8U, 9U})
    {
        auto future_payload = valid;
        future_payload[index] = index == 8U ? std::byte{2U} : std::byte{1U};
        fixtures.push_back({
            .label = "diagnostic-future-payload-" + std::to_string(index),
            .value = std::move(future_payload),
        });
    }

    for (const std::size_t index : {10U, 11U})
    {
        auto flags = valid;
        flags[index] = std::byte{1U};
        fixtures.push_back({
            .label = "diagnostic-flags-" + std::to_string(index),
            .value = std::move(flags),
        });
    }

    for (const std::size_t index : {12U, 13U, 14U, 15U})
    {
        auto reserved = valid;
        reserved[index] = std::byte{1U};
        fixtures.push_back({
            .label = "diagnostic-reserved-" + std::to_string(index),
            .value = std::move(reserved),
        });
    }

    fixtures.push_back({
        .label = "diagnostic-future-schema",
        .schema_version = 2U,
        .value = valid,
    });
    fixtures.push_back({
        .label = "diagnostic-key-value-id-mismatch",
        .value = DiagnosticCheckpointValue(other),
    });
    fixtures.push_back({
        .label = "diagnostic-stale-target",
        .value = valid,
        .create_target = false,
    });

    for (const InvalidDiagnosticCheckpointFixture& fixture : fixtures)
    {
        TempDirectory tree(fixture.label);
        const auto database_root = tree.path() / "native-save";
        {
            auto database = SaveDatabase::Open({.directory = database_root});
            Check(database.has_value(),
                  "the invalid diagnostic-checkpoint fixture database opens");
            if (!database)
                continue;
            ProfileCatalog catalog(*database);
            if (fixture.create_target)
            {
                const auto created = catalog.Create(
                    id, ProfileMetadata{
                            .display_name = "Fixture",
                            .created_unix_milliseconds = 1'000U,
                            .modified_unix_milliseconds = 1'000U,
                        });
                Check(created.has_value(),
                      "the invalid diagnostic-checkpoint target profile is created");
            }
            static_cast<void>(PutDiagnosticCheckpoint(
                *database, id, fixture.schema_version, fixture.value));
        }

        const auto bootstrap = NativePersistence::Bootstrap(database_root);
        CheckStartupError(
            bootstrap,
            NativePersistenceStartupErrorCode::PersistedDiagnosticCheckpoint,
            "every invalid persisted diagnostic-checkpoint fixture fails in one category");
        if (!bootstrap)
        {
            CheckDiagnosticPrivateErrorMessage(
                bootstrap.error().message,
                "persisted diagnostic checkpoint validation failed", tree.path(),
                id,
                "invalid diagnostic-checkpoint startup uses one fixed message");
        }
    }

    TempDirectory malformed_tree("diagnostic-malformed-key-id");
    const auto malformed_root = malformed_tree.path() / "native-save";
    {
        auto database = SaveDatabase::Open({.directory = malformed_root});
        Check(database.has_value(),
              "the malformed diagnostic-checkpoint key fixture opens");
        if (database)
        {
            std::array mutation{
                SaveMutation::Put(
                    "profiles/not-a-profile-id/campaigns/diagnostic/checkpoint",
                    kDiagnosticCheckpointSchemaVersion, valid,
                    SaveWriteCondition::MustBeAbsent()),
            };
            Check(database->Commit(mutation).has_value(),
                  "the malformed diagnostic-checkpoint key fixture commits through raw storage");
        }
    }
    const auto malformed = NativePersistence::Bootstrap(malformed_root);
    CheckStartupError(
        malformed,
        NativePersistenceStartupErrorCode::PersistedDiagnosticCheckpoint,
        "a diagnostic-checkpoint-shaped key with a malformed profile ID fails bootstrap");
    if (!malformed)
    {
        CheckDiagnosticPrivateErrorMessage(
            malformed.error().message,
            "persisted diagnostic checkpoint validation failed",
            malformed_tree.path(), id,
            "malformed diagnostic-checkpoint key failure stays private");
    }

    TempDirectory overlap_tree("diagnostic-overlapping-key");
    const auto overlap_root = overlap_tree.path() / "native-save";
    {
        auto database = SaveDatabase::Open({.directory = overlap_root});
        Check(database.has_value(),
              "the overlapping diagnostic-checkpoint key fixture opens");
        if (database)
        {
            std::array mutation{
                SaveMutation::Put(
                    "profiles/campaigns/diagnostic/checkpoint",
                    kDiagnosticCheckpointSchemaVersion, valid,
                    SaveWriteCondition::MustBeAbsent()),
            };
            Check(database->Commit(mutation).has_value(),
                  "the identifier-free overlapping diagnostic-checkpoint key "
                  "fixture commits through raw storage");
        }
    }
    const auto overlap = NativePersistence::Bootstrap(overlap_root);
    CheckStartupError(
        overlap,
        NativePersistenceStartupErrorCode::PersistedDiagnosticCheckpoint,
        "an identifier-free overlapping diagnostic-checkpoint key fails bootstrap");
    if (!overlap)
    {
        CheckDiagnosticPrivateErrorMessage(
            overlap.error().message,
            "persisted diagnostic checkpoint validation failed",
            overlap_tree.path(), id,
            "overlapping diagnostic-checkpoint key failure stays private");
    }
}

void CheckReplacementConflictRollback()
{
    TempDirectory tree("replacement-conflict");
    const auto database_root = tree.path() / "native-save";
    const ProfileId first = Id("11111111111111111111111111111111");
    const ProfileId second = Id("22222222222222222222222222222222");

    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(), "the replacement-conflict fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, first, "First") ||
            !CreateProfile(*persistence, second, "Second") ||
            !persistence->ConfirmActiveProfile(first))
        {
            return;
        }

        const auto observed = persistence->database().Read(kActiveProfileKey);
        Check(observed && *observed, "the first confirmation is observable before interference");
        if (!observed || !*observed)
            return;
        static_cast<void>(PutActiveProfile(
            persistence->database(), kActiveProfileSchemaVersion, ActiveProfileValue(first),
            SaveWriteCondition::ExactRevision((**observed).revision)));

        const std::uint64_t before_failed_replacement = persistence->database().generation();
        const auto stale_idempotent = persistence->ConfirmActiveProfile(first);
        CheckConfirmationError(stale_idempotent,
                               ActiveProfileConfirmationErrorCode::RevisionConflict,
                               "same-ID confirmation rejects a concurrently replaced revision");
        Check(persistence->database().generation() == before_failed_replacement &&
                  persistence->persisted_confirmed_profile_id() == first,
              "same-ID revision conflict performs no commit and preserves the owned snapshot");

        const auto replacement = persistence->ConfirmActiveProfile(second);
        CheckConfirmationError(replacement, ActiveProfileConfirmationErrorCode::RevisionConflict,
                               "replacement rejects a stale owned pointer revision");
        Check(!replacement &&
                  replacement.error().message ==
                      "persisted active profile changed before confirmation committed",
              "replacement conflict uses a fixed sanitized message");
        const auto after = persistence->database().Read(kActiveProfileKey);
        Check(persistence->database().generation() == before_failed_replacement &&
                  persistence->persisted_confirmed_profile_id() == first &&
                  after && *after && (**after).value == ActiveProfileValue(first),
              "replacement conflict rolls back both storage and the owned confirmation snapshot");
    }

    const auto reopened = NativePersistence::Bootstrap(database_root);
    Check(reopened && reopened->persisted_confirmed_profile_id() == first,
          "reopen confirms that a conflicted replacement did not publish");
}

void CheckAbsentPointerConflictRollback()
{
    TempDirectory tree("absent-conflict");
    const auto database_root = tree.path() / "native-save";
    const ProfileId first = Id("33333333333333333333333333333333");
    const ProfileId second = Id("44444444444444444444444444444444");

    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(), "the absent-conflict fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, first, "First") ||
            !CreateProfile(*persistence, second, "Second"))
        {
            return;
        }
        static_cast<void>(PutActiveProfile(persistence->database(), kActiveProfileSchemaVersion,
                                           ActiveProfileValue(first)));
        const std::uint64_t before = persistence->database().generation();
        const auto confirmation = persistence->ConfirmActiveProfile(second);
        CheckConfirmationError(confirmation, ActiveProfileConfirmationErrorCode::RevisionConflict,
                               "first confirmation uses a must-be-absent precondition");
        Check(persistence->database().generation() == before &&
                  !persistence->persisted_confirmed_profile_id(),
              "must-be-absent conflict preserves the owner's prior absent snapshot");
    }

    const auto reopened = NativePersistence::Bootstrap(database_root);
    Check(reopened && reopened->persisted_confirmed_profile_id() == first,
          "reopen reconciles the independently committed pointer after an absent conflict");
}

void CheckConfirmationRevalidatesIdempotentTarget()
{
    TempDirectory tree("idempotent-stale");
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("55555555555555555555555555555555");

    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(), "the idempotent-stale fixture bootstraps");
        if (!persistence)
            return;
        const auto created = persistence->profiles().Create(
            id, ProfileMetadata{
                    .display_name = "Stale",
                    .created_unix_milliseconds = 1'000U,
                    .modified_unix_milliseconds = 1'000U,
                });
        Check(created.has_value(), "the idempotent-stale profile is created");
        if (!created || !persistence->ConfirmActiveProfile(id))
            return;

        std::array erase{
            SaveMutation::Erase(ProfileMetadataKey(id),
                                SaveWriteCondition::ExactRevision(created->metadata_revision)),
        };
        const auto erased = persistence->database().Commit(erase);
        Check(erased.has_value(), "the raw stale-profile fixture erases its metadata marker");
        if (!erased)
            return;

        const std::uint64_t before = persistence->database().generation();
        const auto missing_profile_start =
            persistence->PrepareDiagnosticCampaignStart(id);
        CheckDiagnosticStartError(
            missing_profile_start,
            DiagnosticCampaignStartErrorCode::ProfileNotFound,
            "diagnostic preparation revalidates current profile existence");
        Check(!missing_profile_start &&
                  missing_profile_start.error().message ==
                      "diagnostic campaign start requires an existing profile" &&
                  persistence->database().generation() == before,
              "missing-profile diagnostic preparation publishes no checkpoint generation");
        if (!missing_profile_start)
        {
            CheckDiagnosticPrivateErrorMessage(
                missing_profile_start.error().message,
                "diagnostic campaign start requires an existing profile",
                tree.path(), id,
                "missing-profile diagnostic preparation uses one fixed private message");
        }

        const auto idempotent = persistence->ConfirmActiveProfile(id);
        CheckConfirmationError(idempotent, ActiveProfileConfirmationErrorCode::ProfileNotFound,
                               "same-ID confirmation revalidates current profile existence");
        Check(persistence->database().generation() == before &&
                  persistence->persisted_confirmed_profile_id() == id,
              "failed same-ID revalidation preserves the prior owned pointer without a commit");
    }

    const auto reopened = NativePersistence::Bootstrap(database_root);
    CheckStartupError(reopened, NativePersistenceStartupErrorCode::PersistedActiveProfile,
                      "reopen rejects a pointer whose profile was removed");
}

void CheckCapacityRollback(const std::string_view label, SaveDatabaseLimits limits)
{
    TempDirectory tree(label);
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("66666666666666666666666666666666");
    {
        auto persistence = NativePersistence::Bootstrap(database_root, limits);
        Check(persistence.has_value(), "the constrained native-persistence fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, id, "A"))
            return;

        const std::uint64_t before = persistence->database().generation();
        const auto confirmation = persistence->ConfirmActiveProfile(id);
        CheckConfirmationError(confirmation,
                               ActiveProfileConfirmationErrorCode::StorageLimitExceeded,
                               "constrained storage rejects the active-profile pointer");
        Check(!confirmation &&
                  confirmation.error().message ==
                      "active profile confirmation exceeds native storage limits",
              "storage capacity failure uses a fixed sanitized message");
        const auto pointer = persistence->database().Read(kActiveProfileKey);
        Check(persistence->database().generation() == before &&
                  !persistence->persisted_confirmed_profile_id() && pointer && !*pointer,
              "capacity failure rolls back storage and the owned confirmation snapshot");
    }

    auto reopened = NativePersistence::Bootstrap(database_root, limits);
    Check(reopened && !reopened->persisted_confirmed_profile_id(),
          "constrained reopen observes no partial pointer after capacity rollback");
}

void CheckConstrainedLimits()
{
    SaveDatabaseLimits record_limit;
    record_limit.max_records = 1U;
    CheckCapacityRollback("record-capacity", record_limit);

    SaveDatabaseLimits logical_limit;
    logical_limit.max_value_bytes = 64U;
    logical_limit.max_logical_value_bytes = 64U;
    CheckCapacityRollback("logical-capacity", logical_limit);
}

void CheckDiagnosticCheckpointCapacityRollback(
    const std::string_view label, SaveDatabaseLimits limits)
{
    TempDirectory tree(label);
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("67676767676767676767676767676767");
    {
        auto persistence = NativePersistence::Bootstrap(database_root, limits);
        Check(persistence.has_value(),
              "the constrained diagnostic-checkpoint fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, id, "A") ||
            !persistence->ConfirmActiveProfile(id))
        {
            return;
        }

        const std::uint64_t before = persistence->database().generation();
        const auto prepared =
            persistence->PrepareDiagnosticCampaignStart(id);
        CheckDiagnosticStartError(
            prepared,
            DiagnosticCampaignStartErrorCode::StorageLimitExceeded,
            "constrained storage rejects the project diagnostic checkpoint");
        Check(!prepared &&
                  prepared.error().message ==
                      "diagnostic campaign checkpoint exceeds native storage limits",
              "diagnostic checkpoint capacity failure uses a fixed sanitized message");
        const auto checkpoint =
            persistence->database().Read(DiagnosticCheckpointKey(id));
        Check(persistence->database().generation() == before && checkpoint &&
                  !*checkpoint,
              "diagnostic checkpoint capacity failure rolls back storage completely");
    }

    auto reopened = NativePersistence::Bootstrap(database_root, limits);
    bool checkpoint_absent = false;
    if (reopened)
    {
        const auto checkpoint =
            reopened->database().Read(DiagnosticCheckpointKey(id));
        checkpoint_absent = checkpoint && !*checkpoint;
    }
    Check(reopened && reopened->persisted_confirmed_profile_id() == id &&
              checkpoint_absent,
          "constrained reopen observes the active profile and no partial diagnostic checkpoint");
}

void CheckDiagnosticCheckpointConstrainedLimits()
{
    SaveDatabaseLimits record_limit;
    record_limit.max_records = 2U;
    CheckDiagnosticCheckpointCapacityRollback(
        "diagnostic-record-capacity", record_limit);

    SaveDatabaseLimits logical_limit;
    logical_limit.max_records = 3U;
    logical_limit.max_value_bytes = 64U;
    logical_limit.max_logical_value_bytes = 90U;
    CheckDiagnosticCheckpointCapacityRollback(
        "diagnostic-logical-capacity", logical_limit);
}

void CheckDiagnosticCheckpointConflictRollbackAndPrivacy()
{
    TempDirectory tree("diagnostic-conflict");
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("78787878787878787878787878787878");
    {
        auto persistence = NativePersistence::Bootstrap(database_root);
        Check(persistence.has_value(),
              "the diagnostic-conflict fixture bootstraps");
        if (!persistence || !CreateProfile(*persistence, id, "Conflict") ||
            !persistence->ConfirmActiveProfile(id))
        {
            return;
        }

        const auto active = persistence->database().Read(kActiveProfileKey);
        Check(active && *active,
              "the diagnostic-conflict fixture observes its active pointer");
        if (!active || !*active)
            return;
        static_cast<void>(PutActiveProfile(
            persistence->database(), kActiveProfileSchemaVersion,
            ActiveProfileValue(id),
            SaveWriteCondition::ExactRevision((**active).revision)));

        const std::uint64_t before = persistence->database().generation();
        const auto conflicted =
            persistence->PrepareDiagnosticCampaignStart(id);
        CheckDiagnosticStartError(
            conflicted, DiagnosticCampaignStartErrorCode::RevisionConflict,
            "diagnostic start rejects a replaced active-pointer revision");
        const auto checkpoint =
            persistence->database().Read(DiagnosticCheckpointKey(id));
        Check(!conflicted &&
                  conflicted.error().message ==
                      "diagnostic campaign start observed changed persisted state" &&
                  persistence->database().generation() == before && checkpoint &&
                  !*checkpoint,
              "active-pointer conflict leaves the diagnostic checkpoint absent and generation unchanged");
        if (!conflicted)
        {
            CheckDiagnosticPrivateErrorMessage(
                conflicted.error().message,
                "diagnostic campaign start observed changed persisted state",
                tree.path(), id,
                "diagnostic active-pointer conflict uses one fixed private message");
        }
    }

    {
        auto reconciled = NativePersistence::Bootstrap(database_root);
        Check(reconciled && reconciled->persisted_confirmed_profile_id() == id,
              "reopen reconciles the independently replaced active pointer");
        if (!reconciled ||
            !reconciled->PrepareDiagnosticCampaignStart(id))
        {
            Check(false,
                  "the reconciled fixture creates its valid diagnostic checkpoint");
            return;
        }

        const auto valid_checkpoint =
            reconciled->database().Read(DiagnosticCheckpointKey(id));
        Check(valid_checkpoint && *valid_checkpoint,
              "the reconciled fixture observes its diagnostic checkpoint");
        if (!valid_checkpoint || !*valid_checkpoint)
            return;
        auto invalid_value = DiagnosticCheckpointValue(id);
        invalid_value[0U] ^= std::byte{1U};
        static_cast<void>(PutDiagnosticCheckpoint(
            reconciled->database(), id, kDiagnosticCheckpointSchemaVersion,
            std::move(invalid_value),
            SaveWriteCondition::ExactRevision((**valid_checkpoint).revision)));

        const std::uint64_t before_invalid = reconciled->database().generation();
        const auto invalid = reconciled->PrepareDiagnosticCampaignStart(id);
        CheckDiagnosticStartError(
            invalid, DiagnosticCampaignStartErrorCode::RevisionConflict,
            "runtime preparation rejects a replaced invalid checkpoint marker");
        Check(reconciled->database().generation() == before_invalid,
              "invalid existing checkpoint rejection publishes no additional generation");
    }

    const auto rejected_reopen = NativePersistence::Bootstrap(database_root);
    CheckStartupError(
        rejected_reopen,
        NativePersistenceStartupErrorCode::PersistedDiagnosticCheckpoint,
        "reopen rejects the same invalid diagnostic checkpoint through startup validation");
}

void CheckDiagnosticCheckpointStorageFailureRollbackAndPrivacy()
{
    TempDirectory tree("diagnostic-storage-failure");
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("89898989898989898989898989898989");
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence.has_value(),
          "the diagnostic storage-failure fixture bootstraps");
    if (!persistence || !CreateProfile(*persistence, id, "Failure") ||
        !persistence->ConfirmActiveProfile(id))
    {
        return;
    }

    SaveDatabase displaced = std::move(persistence->database());
    const auto prepared = persistence->PrepareDiagnosticCampaignStart(id);
    CheckDiagnosticStartError(
        prepared, DiagnosticCampaignStartErrorCode::StorageFailure,
        "a moved-from database becomes a typed diagnostic-start storage failure");
    if (!prepared)
    {
        CheckDiagnosticPrivateErrorMessage(
            prepared.error().message,
            "diagnostic campaign checkpoint storage failed", tree.path(), id,
            "diagnostic storage failure uses one fixed private message");
    }
    const auto durable = displaced.Read(DiagnosticCheckpointKey(id));
    Check(durable && !*durable,
          "the displaced live database proves failed diagnostic preparation changed no durable bytes");
}

void CheckStorageFailureRollbackAndPrivacy()
{
    TempDirectory tree("storage-failure");
    const auto database_root = tree.path() / "native-save";
    const ProfileId first = Id("77777777777777777777777777777777");
    const ProfileId second = Id("88888888888888888888888888888888");
    auto persistence = NativePersistence::Bootstrap(database_root);
    Check(persistence.has_value(), "the storage-failure fixture bootstraps");
    if (!persistence || !CreateProfile(*persistence, first, "First") ||
        !CreateProfile(*persistence, second, "Second") ||
        !persistence->ConfirmActiveProfile(first))
    {
        return;
    }

    SaveDatabase displaced = std::move(persistence->database());
    const auto confirmation = persistence->ConfirmActiveProfile(second);
    CheckConfirmationError(confirmation, ActiveProfileConfirmationErrorCode::StorageFailure,
                           "a moved-from database becomes a typed confirmation storage failure");
    Check(persistence->persisted_confirmed_profile_id() == first,
          "storage failure preserves the previously confirmed profile snapshot");
    if (!confirmation)
    {
        CheckPrivateErrorMessage(confirmation.error().message,
                                 "active profile confirmation storage failed", tree.path(), second,
                                 "confirmation storage failure uses one fixed private message");
    }
    const auto durable = displaced.Read(kActiveProfileKey);
    Check(durable && *durable && (**durable).value == ActiveProfileValue(first),
          "the displaced live database proves failed confirmation changed no durable bytes");
}

void CheckExistingBootstrapFailuresRemainTyped()
{
    CheckStartupError(NativePersistence::Bootstrap("relative/native-save"),
                      NativePersistenceStartupErrorCode::DatabaseOpen,
                      "an invalid database path remains a typed database-open failure");

    TempDirectory tree("corrupt-profile");
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("99999999999999999999999999999999");
    {
        auto opened = SaveDatabase::Open({.directory = database_root});
        Check(opened.has_value(), "the corrupt profile fixture database opens");
        if (!opened)
            return;
        std::array mutation{
            SaveMutation::Put(ProfileMetadataKey(id), 2U, Bytes("synthetic-future-profile")),
        };
        Check(opened->Commit(mutation).has_value(),
              "the future profile fixture commits through raw native storage");
    }

    const auto bootstrap = NativePersistence::Bootstrap(database_root);
    CheckStartupError(bootstrap, NativePersistenceStartupErrorCode::ProfileCatalogBootstrap,
                      "typed profile validation failure aborts native persistence bootstrap");
    Check(!bootstrap &&
              bootstrap.error().message.starts_with("profile catalog [unsupported-metadata]:"),
          "profile bootstrap preserves its existing stable lower-layer category");
}
} // namespace

int main()
{
    CheckErrorNames();
    CheckAbsentBootstrapAndLimits();
    CheckFrontEndStartupProfileBudget();
    CheckEncodeIdempotenceReplacementMoveAndReopen();
    CheckDiagnosticCampaignCheckpointLifecycle();
    CheckGeneratedDecodeFailures();
    CheckGeneratedDiagnosticCheckpointDecodeFailures();
    CheckReplacementConflictRollback();
    CheckAbsentPointerConflictRollback();
    CheckConfirmationRevalidatesIdempotentTarget();
    CheckConstrainedLimits();
    CheckDiagnosticCheckpointConstrainedLimits();
    CheckDiagnosticCheckpointConflictRollbackAndPrivacy();
    CheckStorageFailureRollbackAndPrivacy();
    CheckDiagnosticCheckpointStorageFailureRollbackAndPrivacy();
    CheckExistingBootstrapFailuresRemainTyped();
    return failures == 0 ? 0 : 1;
}
