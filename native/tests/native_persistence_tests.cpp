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
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
using namespace std::string_view_literals;

using omega::app::NativePersistence;
using omega::app::NativePersistenceStartupError;
using omega::app::NativePersistenceStartupErrorCode;
using omega::persistence::SaveDatabase;
using omega::persistence::SaveDatabaseErrorCode;
using omega::persistence::SaveMutation;
using omega::profiles::ProfileId;
using omega::profiles::ProfileMetadata;

static_assert(sizeof(NativePersistenceStartupErrorCode) == 1U);

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
void CheckErrorCode(const std::expected<T, NativePersistenceStartupError>& result,
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

void CheckErrorNames()
{
    constexpr std::array expected{
        "database-open"sv,
        "profile-catalog-bootstrap"sv,
        "resource-exhausted"sv,
    };
    for (std::size_t index = 0U; index < expected.size(); ++index)
    {
        Check(omega::app::NativePersistenceStartupErrorCodeName(
                  static_cast<NativePersistenceStartupErrorCode>(index)) == expected[index],
              "every native-persistence startup error has a fixed name");
    }
    Check(omega::app::NativePersistenceStartupErrorCodeName(
              static_cast<NativePersistenceStartupErrorCode>(0xffU)) == "resource-exhausted",
          "an invalid native-persistence error uses the fixed fallback");
}

void CheckBootstrapMoveAndReopen()
{
    TempDirectory tree("lifecycle");
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("00112233445566778899aabbccddeeff");
    {
        auto bootstrapped = NativePersistence::Bootstrap(database_root);
        Check(bootstrapped && bootstrapped->startup_profiles().empty() &&
                  bootstrapped->database().generation() == 0U,
              "fresh bootstrap validates an explicit empty profile snapshot");
        if (!bootstrapped)
            return;

        auto created = bootstrapped->profiles().Create(id, ProfileMetadata{
                                                               .display_name = "Synthetic",
                                                               .created_unix_milliseconds = 1'000U,
                                                               .modified_unix_milliseconds = 1'000U,
                                                           });
        Check(created && created->metadata_revision == 1U,
              "the owned catalog remains usable after bootstrap");
        const auto second_owner = SaveDatabase::Open({.directory = database_root});
        Check(!second_owner && second_owner.error().code == SaveDatabaseErrorCode::Busy,
              "the application persistence owner retains the database lock");

        NativePersistence moved = std::move(*bootstrapped);
        const auto read = moved.profiles().Read(id);
        Check(read && *read && (**read).metadata.display_name == "Synthetic",
              "moving the owner preserves the catalog-to-database reference");
    }

    auto reopened = NativePersistence::Bootstrap(database_root);
    Check(reopened && reopened->startup_profiles().size() == 1U &&
              reopened->startup_profiles()[0].id == id &&
              reopened->startup_profiles()[0].metadata.display_name == "Synthetic" &&
              reopened->startup_profiles()[0].metadata_revision == 1U,
          "reopen publishes deterministic typed startup summaries");
}

void CheckFailuresRemainTyped()
{
    CheckErrorCode(NativePersistence::Bootstrap("relative/native-save"),
                   NativePersistenceStartupErrorCode::DatabaseOpen,
                   "an invalid database path remains a typed database-open failure");

    TempDirectory tree("corrupt-profile");
    const auto database_root = tree.path() / "native-save";
    const ProfileId id = Id("ffffffffffffffffffffffffffffffff");
    {
        auto opened = SaveDatabase::Open({.directory = database_root});
        Check(opened.has_value(), "the corrupt profile fixture database opens");
        if (!opened)
            return;
        std::array mutation{
            SaveMutation::Put("profiles/" + id.ToString() + "/metadata", 2U,
                              Bytes("synthetic-future-profile")),
        };
        Check(opened->Commit(mutation).has_value(),
              "the future profile fixture commits through raw native storage");
    }

    const auto bootstrapped = NativePersistence::Bootstrap(database_root);
    CheckErrorCode(bootstrapped, NativePersistenceStartupErrorCode::ProfileCatalogBootstrap,
                   "typed profile validation failure aborts native persistence bootstrap");
    Check(!bootstrapped &&
              bootstrapped.error().message.starts_with("profile catalog [unsupported-metadata]:"),
          "profile bootstrap preserves a stable sanitized lower-layer category");
}
} // namespace

int main()
{
    CheckErrorNames();
    CheckBootstrapMoveAndReopen();
    CheckFailuresRemainTyped();
    return failures == 0 ? 0 : 1;
}
