#include "omega/runtime/runtime_config_discovery.h"
#include "omega/runtime/runtime_settings.h"

#include <chrono>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace
{
static_assert(sizeof(omega::runtime::RuntimeConfigPlatform) == 1U);
static_assert(static_cast<std::uint8_t>(omega::runtime::RuntimeConfigPlatform::Windows) == 0U);
static_assert(static_cast<std::uint8_t>(omega::runtime::RuntimeConfigPlatform::MacOS) == 1U);
static_assert(static_cast<std::uint8_t>(omega::runtime::RuntimeConfigPlatform::Xdg) == 2U);
static_assert(noexcept(omega::runtime::HostRuntimeConfigPlatform()));

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] std::filesystem::path SyntheticAbsoluteRoot(const std::string_view suffix)
{
#if defined(_WIN32)
    return std::filesystem::path("C:/openomega-runtime-config-tests") / suffix;
#else
    return std::filesystem::path("/openomega-runtime-config-tests") / suffix;
#endif
}

[[nodiscard]] bool WriteTextFile(const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

void CheckError(const std::expected<omega::runtime::ConfigStore, std::string>& result,
    const std::string& expected, const std::string_view context)
{
    if (!result && result.error() == expected)
        return;
    std::cerr << "FAILED: " << context << '\n'
              << "  expected: [" << expected << "]\n"
              << "  actual:   [" << (result ? "<success>" : result.error()) << "]\n";
    ++failures;
}

void CheckPathFreeError(
    const std::expected<omega::runtime::ConfigStore, std::string>& result,
    const std::string_view expected, const std::filesystem::path& private_path,
    const std::string_view context)
{
    Check(!result, context);
    if (result)
        return;

    Check(result.error() == expected, context);
    Check(result.error().find(private_path.string()) == std::string::npos,
        "runtime configuration errors omit the complete source path");
    Check(result.error().find("PrivateUser") == std::string::npos,
        "runtime configuration errors omit the synthetic user identity");
    Check(result.error().find("SecretVault") == std::string::npos,
        "runtime configuration errors omit the synthetic private directory");
}

#if defined(_WIN32)
[[nodiscard]] bool TryCreateUnprivilegedFileSymlink(const std::filesystem::path& target,
    const std::filesystem::path& link, std::error_code& error) noexcept
{
    DWORD flags = 0U;
#ifdef SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE
    flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
#endif
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), flags) != FALSE)
    {
        error.clear();
        return true;
    }
    error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
    return false;
}

[[nodiscard]] bool IsExplicitSymlinkCapabilitySkip(const std::error_code& error) noexcept
{
    switch (static_cast<DWORD>(error.value()))
    {
    case ERROR_ACCESS_DENIED:
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
    case ERROR_INVALID_PARAMETER:
    case ERROR_CALL_NOT_IMPLEMENTED:
    case ERROR_PRIVILEGE_NOT_HELD:
        return true;
    default:
        return false;
    }
}
#endif

[[nodiscard]] bool CreateFileSymlinkFixture(
    const std::filesystem::path& target, const std::filesystem::path& link, std::error_code& error)
{
    std::filesystem::create_symlink(target, link, error);
#if defined(_WIN32)
    if (error)
    {
        std::error_code cleanup_error;
        std::filesystem::remove(link, cleanup_error);
        if (!TryCreateUnprivilegedFileSymlink(target, link, error))
        {
            const bool explicit_skip = IsExplicitSymlinkCapabilitySkip(error);
            Check(explicit_skip,
                "a symlink fixture fails only for a recognized Windows capability limit");
            if (explicit_skip)
                std::cout << "SKIP: Windows file-symlink fixture is unavailable\n";
            return false;
        }
    }
#else
    if (error)
    {
        Check(false, "the file-symlink fixture is created on this platform");
        return false;
    }
#endif
    return true;
}
} // namespace

int RuntimeConfigDiscoveryFailureCount()
{
    using omega::runtime::ResolveDefaultRuntimeConfigPath;
    using omega::runtime::RuntimeConfigPlatform;
    using omega::runtime::RuntimeConfigSearchRoots;

#if defined(_WIN32)
    Check(omega::runtime::HostRuntimeConfigPlatform() == RuntimeConfigPlatform::Windows,
        "the compile-time Windows host family is reported without I/O");
#elif defined(__APPLE__)
    Check(omega::runtime::HostRuntimeConfigPlatform() == RuntimeConfigPlatform::MacOS,
        "the compile-time macOS host family is reported without I/O");
#else
    Check(omega::runtime::HostRuntimeConfigPlatform() == RuntimeConfigPlatform::Xdg,
        "the compile-time XDG host family is reported without I/O");
#endif

    const auto local_root = SyntheticAbsoluteRoot("local");
    const auto xdg_root = SyntheticAbsoluteRoot("xdg");
    const auto home_root = SyntheticAbsoluteRoot("home");

    RuntimeConfigSearchRoots roots{
        .local_app_data = local_root,
        .xdg_config_home = xdg_root,
        .home = home_root,
    };
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Windows, roots) ==
              std::optional<std::filesystem::path>{local_root / "OpenOmega" / "openomega.cfg"},
        "Windows discovery appends only the fixed OpenOmega profile suffix");
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::MacOS, roots) ==
              std::optional<std::filesystem::path>{
                  home_root / "Library" / "Application Support" / "OpenOmega" / "openomega.cfg"},
        "macOS discovery appends only the fixed Application Support suffix");
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots) ==
              std::optional<std::filesystem::path>{xdg_root / "openomega" / "openomega.cfg"},
        "XDG_CONFIG_HOME takes precedence over HOME");

    roots.xdg_config_home.reset();
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots) ==
              std::optional<std::filesystem::path>{
                  home_root / ".config" / "openomega" / "openomega.cfg"},
        "XDG discovery falls back to the fixed HOME suffix when "
        "XDG_CONFIG_HOME is absent");
    roots.xdg_config_home = std::filesystem::path{};
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots) ==
              std::optional<std::filesystem::path>{
                  home_root / ".config" / "openomega" / "openomega.cfg"},
        "an empty XDG_CONFIG_HOME is ignored in favor of an absolute HOME");
    roots.xdg_config_home = std::filesystem::path("relative-xdg");
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots) ==
              std::optional<std::filesystem::path>{
                  home_root / ".config" / "openomega" / "openomega.cfg"},
        "a relative XDG_CONFIG_HOME is ignored in favor of an absolute HOME");

    roots.home.reset();
    Check(!ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::MacOS, roots) &&
              !ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots),
        "missing required HOME roots produce no macOS or XDG fallback candidate");
    roots.home = std::filesystem::path{};
    Check(!ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::MacOS, roots) &&
              !ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots),
        "empty required HOME roots produce no macOS or XDG fallback candidate");
    roots.home = std::filesystem::path("relative-home");
    Check(!ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::MacOS, roots) &&
              !ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Xdg, roots),
        "relative required HOME roots produce no macOS or XDG fallback "
        "candidate");

    roots.local_app_data.reset();
    Check(!ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Windows, roots),
        "missing LOCALAPPDATA produces no Windows candidate");
    roots.local_app_data = std::filesystem::path{};
    Check(!ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Windows, roots),
        "empty LOCALAPPDATA produces no Windows candidate");
    roots.local_app_data = std::filesystem::path("relative-local");
    Check(!ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Windows, roots),
        "relative LOCALAPPDATA produces no Windows candidate");

    const auto literal_root = SyntheticAbsoluteRoot("literal") / ".." / "$HOME" / "%LOCALAPPDATA%";
    roots.local_app_data = literal_root;
    Check(ResolveDefaultRuntimeConfigPath(RuntimeConfigPlatform::Windows, roots) ==
              std::optional<std::filesystem::path>{literal_root / "OpenOmega" / "openomega.cfg"},
        "discovery preserves dot-dot components and environment-looking tokens "
        "literally");

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto test_root = std::filesystem::temp_directory_path() /
                           ("omega-runtime-config-discovery-tests-PrivateUser-SecretVault-" +
                               std::to_string(suffix));
    std::error_code file_error;
    std::filesystem::create_directories(test_root, file_error);
    Check(!file_error, "the synthetic default-profile test root is created");

    const auto missing_default = test_root / "missing-default.cfg";
    omega::runtime::LaunchOptions options;
    options.config_overrides.push_back({.key = "jobs.worker_count", .value = "1"});
    auto loaded = omega::runtime::LoadRuntimeConfig(options, missing_default);
    Check(loaded && loaded->RequireInt64("jobs.worker_count") == 1,
        "an absent default profile silently starts from the empty store before "
        "--set");

    const auto valid_default = test_root / "valid-default.cfg";
    Check(WriteTextFile(valid_default, "jobs.worker_count = 2\n"
                                       "content.data_root = default/content\n"
                                       "content.level_code = default1\n"),
        "the valid default profile fixture is written");
    const auto ambient_free = omega::runtime::LoadRuntimeConfig({});
    Check(ambient_free && ambient_free->entry_count() == 0U,
        "the one-argument loader remains ambient-free beside a valid default fixture");
    loaded = omega::runtime::LoadRuntimeConfig(options, valid_default);
    Check(loaded && loaded->RequireInt64("jobs.worker_count") == 1 &&
              loaded->RequireString("content.data_root") == "default/content",
        "validated --set values override a regular default profile");

    const auto malformed_default = test_root / "malformed-default.cfg";
    Check(WriteTextFile(malformed_default, "not a setting\n"),
        "the malformed default profile fixture is written");
    CheckPathFreeError(omega::runtime::LoadRuntimeConfig({}, malformed_default),
        "runtime configuration default profile: config line 1 is missing '='",
        malformed_default,
        "regular default loader diagnostics reuse the existing parser bytes "
        "under one prefix");

    const omega::runtime::ConfigLimits default_limits;
    const auto oversized_default = test_root / "oversized-default.cfg";
    Check(WriteTextFile(oversized_default,
              std::string(default_limits.max_input_bytes + 1U, '#')),
        "the oversized default profile fixture is written");
    CheckPathFreeError(omega::runtime::LoadRuntimeConfig({}, oversized_default),
        "runtime configuration default profile: config file exceeds the " +
            std::to_string(default_limits.max_input_bytes) + "-byte budget",
        oversized_default,
        "the default-profile byte budget is fatal without disclosing its path");

    const auto directory_default = test_root / "directory-default.cfg";
    std::filesystem::create_directory(directory_default, file_error);
    Check(!file_error, "the non-regular default profile fixture is created");
    CheckPathFreeError(omega::runtime::LoadRuntimeConfig({}, directory_default),
        "runtime configuration default profile: config path is not a regular file",
        directory_default,
        "a directory at the final default entry is rejected without loading");

#if defined(_WIN32)
    std::cout << "SKIP: MSVC reports invalid and overlong final path components as not-found\n";
#else
    const auto uninspectable_default = test_root / std::string(300U, 'x');
    auto inspection = omega::runtime::LoadRuntimeConfig({}, uninspectable_default);
    CheckPathFreeError(inspection,
        "runtime configuration default profile: unable to inspect config file",
        uninspectable_default,
        "a non-missing final-entry inspection error is fatal and sanitized");
#endif

    const auto leaf_symlink = test_root / "leaf-symlink.cfg";
    if (CreateFileSymlinkFixture(valid_default, leaf_symlink, file_error))
    {
        CheckPathFreeError(omega::runtime::LoadRuntimeConfig({}, leaf_symlink),
            "runtime configuration default profile: config path is not a regular file",
            leaf_symlink,
            "a final-entry symlink is reported and rejected without following it");
    }

    const auto dangling_symlink = test_root / "dangling-symlink.cfg";
    file_error.clear();
    if (CreateFileSymlinkFixture(test_root / "absent-target.cfg", dangling_symlink, file_error))
    {
        CheckPathFreeError(omega::runtime::LoadRuntimeConfig({}, dangling_symlink),
            "runtime configuration default profile: config path is not a regular file",
            dangling_symlink,
            "a dangling final-entry symlink is rejected rather than treated "
            "as absent");
    }

    const auto explicit_config = test_root / "explicit.cfg";
    Check(WriteTextFile(explicit_config, "jobs.worker_count = 3\n"),
        "the explicit profile fixture is written");
    omega::runtime::LaunchOptions explicit_options;
    explicit_options.config_path = explicit_config;
    loaded = omega::runtime::LoadRuntimeConfig(explicit_options, directory_default);
    Check(loaded && loaded->RequireInt64("jobs.worker_count") == 3,
        "an explicit profile bypasses default-profile inspection and loading");

    const auto missing_explicit = test_root / "missing-explicit.cfg";
    explicit_options.config_path = missing_explicit;
    CheckPathFreeError(omega::runtime::LoadRuntimeConfig(explicit_options, directory_default),
        "runtime configuration explicit profile: unable to open config file",
        missing_explicit,
        "an explicit missing profile remains fatal without disclosing its path");

    explicit_options.config_path = oversized_default;
    CheckPathFreeError(omega::runtime::LoadRuntimeConfig(explicit_options, directory_default),
        "runtime configuration explicit profile: config file exceeds the " +
            std::to_string(default_limits.max_input_bytes) + "-byte budget",
        oversized_default,
        "an explicit oversized profile remains fatal without disclosing its path");

    omega::runtime::LaunchOptions invalid_override;
    invalid_override.config_overrides.push_back({.key = "Bad", .value = "value"});
    CheckError(omega::runtime::LoadRuntimeConfig(invalid_override, valid_default),
        "--set=Bad: config override: config key contains a byte outside "
        "[a-z0-9_.]",
        "--set diagnostics remain source-neutral after a default profile "
        "is loaded");

    std::filesystem::remove_all(test_root, file_error);
    Check(!file_error, "the synthetic default-profile test root is removed");
    return failures;
}
