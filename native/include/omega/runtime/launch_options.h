#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace omega::runtime
{
inline constexpr std::size_t kMaxLaunchConfigOverrides = 256U;

struct LaunchConfigOverride
{
    std::string key;
    std::string value;
};

struct LaunchOptions
{
    int frame_limit = -1;
    std::optional<std::filesystem::path> data_root;
    std::optional<std::string> level_code;
    std::optional<std::filesystem::path> config_path;
    std::vector<LaunchConfigOverride> config_overrides;
    bool capture_run = false;
    bool replay_capture = false;
    bool probe_only = false;
    bool show_help = false;
};

// [any thread; reentrant] Parses arguments after argv[0]. No filesystem access occurs here.
[[nodiscard]] std::expected<LaunchOptions, std::string> ParseLaunchOptions(
    std::span<const std::string_view> arguments);

[[nodiscard]] std::string_view LaunchUsage() noexcept;
} // namespace omega::runtime
