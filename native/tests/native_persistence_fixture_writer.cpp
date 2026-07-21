#include "native_persistence.h"

#include "omega/profiles/profile_catalog.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
constexpr omega::profiles::ProfileId kFixtureProfileId =
    omega::profiles::ProfileId::FromBytes(
        std::array<std::uint8_t, 16U>{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                      0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U});
constexpr std::string_view kFixtureDisplayName = "CONTRACT PROFILE";
constexpr std::uint64_t kFixtureTimestamp = 1'725'000'000'123ULL;
constexpr std::string_view kSuccessOutput =
    "OpenOmega native persistence fixture: profiles=1 active=confirmed\n";
constexpr std::string_view kFailureOutput =
    "OpenOmega native persistence fixture failed\n";

[[nodiscard]] bool IsFixtureProfile(
    const omega::profiles::ProfileSummary& profile) noexcept
{
    return profile.id == kFixtureProfileId &&
           profile.metadata.display_name == kFixtureDisplayName &&
           profile.metadata.created_unix_milliseconds == kFixtureTimestamp &&
           profile.metadata.modified_unix_milliseconds == kFixtureTimestamp;
}

[[nodiscard]] int Fail()
{
    std::cerr << kFailureOutput;
    return EXIT_FAILURE;
}
} // namespace

int main(const int argc, char* argv[])
{
    if (argc != 2 || argv == nullptr || argv[1] == nullptr)
        return Fail();

    try
    {
        const std::filesystem::path native_save_directory(argv[1]);
        if (native_save_directory.empty() || !native_save_directory.is_absolute())
            return Fail();

        auto persistence =
            omega::app::NativePersistence::Bootstrap(native_save_directory);
        if (!persistence)
            return Fail();

        const auto startup_profiles = persistence->startup_profiles();
        if (startup_profiles.empty())
        {
            auto created = persistence->profiles().Create(
                kFixtureProfileId,
                omega::profiles::ProfileMetadata{
                    .display_name = std::string(kFixtureDisplayName),
                    .created_unix_milliseconds = kFixtureTimestamp,
                    .modified_unix_milliseconds = kFixtureTimestamp,
                });
            if (!created || !IsFixtureProfile(*created))
                return Fail();
        }
        else if (startup_profiles.size() != 1U ||
                 !IsFixtureProfile(startup_profiles.front()))
        {
            return Fail();
        }

        const auto confirmed =
            persistence->ConfirmActiveProfile(kFixtureProfileId);
        if (!confirmed ||
            persistence->persisted_confirmed_profile_id() != kFixtureProfileId)
        {
            return Fail();
        }

        const auto profiles = persistence->profiles().List();
        if (!profiles || profiles->size() != 1U ||
            !IsFixtureProfile(profiles->front()))
        {
            return Fail();
        }

        std::cout << kSuccessOutput;
        return EXIT_SUCCESS;
    }
    catch (...)
    {
        return Fail();
    }
}
