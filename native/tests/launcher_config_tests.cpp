#include "launcher_config.h"

#include "omega/runtime/config_service.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output)
        return false;
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

[[nodiscard]] std::optional<std::string> ReadTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void CheckSanitized(const std::string_view diagnostic, const std::string_view context)
{
    constexpr std::array forbidden{
        std::string_view("PrivateUser"), std::string_view("SecretVault"),
        std::string_view("privateuser"), std::string_view("secretvault"),
        std::string_view("raw-secret"),  std::string_view("C:/" "Users/"),
    };
    for (const std::string_view fragment : forbidden)
        Check(diagnostic.find(fragment) == std::string_view::npos, context);
}

template <typename T>
void CheckSanitizedFailure(const std::expected<T, std::string>& result,
                           const std::string_view context)
{
    Check(!result, context);
    if (!result)
        CheckSanitized(result.error(), context);
}

[[nodiscard]] bool HasLauncherTemporary(const std::filesystem::path& root)
{
    std::error_code iteration_error;
    std::filesystem::directory_iterator cursor(root, iteration_error);
    const std::filesystem::directory_iterator end;
    while (!iteration_error && cursor != end)
    {
        const std::string leaf = cursor->path().filename().string();
        if (leaf.find(".openomega.tmp.") != std::string::npos)
            return true;
        cursor.increment(iteration_error);
    }
    return false;
}
} // namespace

int main()
{
    using omega::launcher::LauncherPreferences;
    using omega::launcher::LoadLauncherPreferences;
    using omega::launcher::SaveLauncherPreferencesAtomically;

    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("omega-launcher-config-PrivateUser-SecretVault-" + std::to_string(unique_suffix));
    const std::filesystem::path config_path = root / "openomega.cfg";
    std::error_code file_error;
    std::filesystem::remove_all(root, file_error);

    const auto missing = LoadLauncherPreferences(config_path);
    Check(missing && !missing->data_source && !missing->opening_movie_member &&
              !missing->gamepad_enabled,
          "a missing configuration resolves to launcher defaults");

    std::filesystem::create_directories(root, file_error);
    Check(!file_error, "the launcher config test directory is created");
    Check(WriteTextFile(config_path, "# a comment is intentionally canonicalized away\n"
                                     "log.minimum_severity = debug\n"
                                     "content.data_root = stale/source\n"
                                     "content.opening_movie_member = OLD/INTRO.BIN\n"
                                     "input.gamepad_enabled = true\n"),
          "the existing launcher configuration fixture is written");

    const std::filesystem::path unicode_source =
        root / std::filesystem::path(u8"Owned data \u03a9");
    LauncherPreferences preferences{
        .data_source = unicode_source,
        .opening_movie_member = "MOVIES/OPENING.BIN",
        .gamepad_enabled = false,
    };
    const auto first_save = SaveLauncherPreferencesAtomically(config_path, preferences);
    Check(first_save.has_value(), "folder and Unicode data-source paths save "
                                  "through the canonical config channel");
    Check(!HasLauncherTemporary(root),
          "a successful atomic replacement leaves no sibling temporary behind");

    const auto round_trip = LoadLauncherPreferences(config_path);
    Check(round_trip && round_trip->data_source == unicode_source &&
              round_trip->opening_movie_member ==
                  std::optional<std::string>{"MOVIES/OPENING.BIN"} &&
              !round_trip->gamepad_enabled,
          "a Unicode data source, private movie selection, and explicit false gamepad preference round-trip");

    auto preserved_store = omega::runtime::LoadConfigFile(config_path);
    Check(preserved_store && preserved_store->GetString("log.minimum_severity") ==
                                 std::optional<std::string_view>{"debug"},
          "saving launcher preferences preserves an unrelated known runtime "
          "setting");
    Check(preserved_store && preserved_store->entry_count() == 4U,
          "canonical replacement retains exactly the unrelated and "
          "launcher-owned entries");

    preferences.gamepad_enabled = true;
    const auto true_save = SaveLauncherPreferencesAtomically(config_path, preferences);
    const auto true_round_trip = LoadLauncherPreferences(config_path);
    Check(true_save && true_round_trip && true_round_trip->gamepad_enabled &&
              true_round_trip->data_source == unicode_source &&
              true_round_trip->opening_movie_member == preferences.opening_movie_member,
          "the opt-in true gamepad preference replaces false without disturbing "
          "the source");

    preferences.data_source.reset();
    const auto source_removal = SaveLauncherPreferencesAtomically(config_path, preferences);
    const auto source_removed = LoadLauncherPreferences(config_path);
    Check(source_removal && source_removed && !source_removed->data_source &&
              source_removed->opening_movie_member == preferences.opening_movie_member &&
              source_removed->gamepad_enabled,
          "an absent data-source preference omits the content.data_root entry");
    preserved_store = omega::runtime::LoadConfigFile(config_path);
    Check(preserved_store && !preserved_store->Contains("content.data_root") &&
              preserved_store->Contains("log.minimum_severity"),
          "removing the launcher source does not remove unrelated parsed settings");

    preferences.opening_movie_member.reset();
    const auto member_removal =
        SaveLauncherPreferencesAtomically(config_path, preferences);
    const auto member_removed = LoadLauncherPreferences(config_path);
    Check(member_removal && member_removed &&
              !member_removed->opening_movie_member,
          "an absent private movie selection omits its configuration entry");

    const std::filesystem::path malformed_bool_path = root / "malformed-bool.cfg";
    Check(WriteTextFile(malformed_bool_path, "input.gamepad_enabled = "
                                             "C:/" "Users/PrivateUser/SecretVault/raw-secret\n"),
          "the malformed private boolean fixture is written");
    const auto malformed_bool = LoadLauncherPreferences(malformed_bool_path);
    CheckSanitizedFailure(malformed_bool,
                          "a malformed gamepad value fails without exposing the raw private value");

    const std::filesystem::path malformed_path = root / "malformed-path.cfg";
    std::string invalid_utf8 = "content.data_root = PrivateUser-SecretVault-";
    invalid_utf8.push_back(static_cast<char>(0xC3U));
    invalid_utf8.push_back('(');
    invalid_utf8.append("-raw-secret\ninput.gamepad_enabled = false\n");
    Check(WriteTextFile(malformed_path, invalid_utf8),
          "the malformed private UTF-8 path fixture is written");
    const auto malformed_source = LoadLauncherPreferences(malformed_path);
    CheckSanitizedFailure(malformed_source,
                          "a malformed UTF-8 source fails without exposing path bytes");

    const std::filesystem::path malformed_member_path = root / "malformed-member.cfg";
    std::string invalid_member =
        "content.opening_movie_member = PrivateUser-SecretVault-";
    invalid_member.push_back(static_cast<char>(0xC3U));
    invalid_member.push_back('(');
    invalid_member.append("-raw-secret\n");
    Check(WriteTextFile(malformed_member_path, invalid_member),
          "the malformed private movie member fixture is written");
    const auto malformed_member =
        LoadLauncherPreferences(malformed_member_path);
    CheckSanitizedFailure(malformed_member,
                          "a malformed movie selection fails without exposing its bytes");

    const std::filesystem::path rejected_path = root / "rejected.cfg";
    LauncherPreferences control_source{
        .data_source =
            std::filesystem::path(std::u8string(u8"PrivateUser\nSecretVault-raw-secret")),
        .gamepad_enabled = false,
    };
    const auto rejected_control = SaveLauncherPreferencesAtomically(rejected_path, control_source);
    CheckSanitizedFailure(rejected_control,
                          "a path containing a control byte is rejected without exposing it");
    Check(!std::filesystem::exists(rejected_path) && !HasLauncherTemporary(root),
          "a rejected control-byte value writes neither a target nor a temporary");

    LauncherPreferences oversized_source{
        .data_source =
            std::filesystem::path(std::u8string(omega::runtime::kMaxConfigValueBytes + 1U, u8'x')),
        .gamepad_enabled = false,
    };
    const auto rejected_oversize =
        SaveLauncherPreferencesAtomically(rejected_path, oversized_source);
    CheckSanitizedFailure(rejected_oversize,
                          "a path over the config value budget is rejected without exposing it");
    Check(!std::filesystem::exists(rejected_path) && !HasLauncherTemporary(root),
          "an over-budget value is rejected before any filesystem publication");

    const auto canonical_text = ReadTextFile(config_path);
    Check(canonical_text && canonical_text->find("#") == std::string::npos &&
              canonical_text->find("log.minimum_severity = debug\n") != std::string::npos &&
              canonical_text->find("input.gamepad_enabled = true\n") != std::string::npos,
          "atomic replacement emits canonical key, separator, value, and LF "
          "records");

    std::filesystem::remove_all(root, file_error);
    Check(!file_error, "the launcher config test directory is removed");

    if (failures == 0)
        std::cout << "launcher config tests passed\n";
    return failures == 0 ? 0 : 1;
}
