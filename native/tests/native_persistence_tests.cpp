#include "native_persistence.h"

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

static_assert(sizeof(NativePersistenceStartupErrorCode) == 1U);
static_assert(sizeof(ActiveProfileConfirmationErrorCode) == 1U);

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

[[nodiscard]] std::vector<std::byte> ActiveProfileValue(const ProfileId id)
{
    std::vector<std::byte> value = Bytes("OOACTPRF");
    value.resize(16U, std::byte{0});
    value[8U] = std::byte{1U};
    for (const std::uint8_t id_byte : id.bytes())
        value.push_back(static_cast<std::byte>(id_byte));
    return value;
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

void CheckErrorNames()
{
    constexpr std::array startup_expected{
        "database-open"sv,
        "profile-catalog-bootstrap"sv,
        "persisted-active-profile"sv,
        "resource-exhausted"sv,
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
    Check(config && config->limits.max_records == 1'025U,
          "production native persistence has capacity for 1,024 profile markers plus the active pointer when no other record consumes it");
    Check(config && config->limits.max_mutations_per_commit ==
                        defaults.max_mutations_per_commit &&
              config->limits.max_key_bytes == defaults.max_key_bytes &&
              config->limits.max_value_bytes == defaults.max_value_bytes &&
              config->limits.max_logical_value_bytes == defaults.max_logical_value_bytes &&
              config->limits.max_file_bytes == defaults.max_file_bytes,
          "production native persistence otherwise preserves default database limits");

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

    const auto reopened = NativePersistence::Bootstrap(database_root, limits);
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
    CheckEncodeIdempotenceReplacementMoveAndReopen();
    CheckGeneratedDecodeFailures();
    CheckReplacementConflictRollback();
    CheckAbsentPointerConflictRollback();
    CheckConfirmationRevalidatesIdempotentTarget();
    CheckConstrainedLimits();
    CheckStorageFailureRollbackAndPrivacy();
    CheckExistingBootstrapFailuresRemainTyped();
    return failures == 0 ? 0 : 1;
}
