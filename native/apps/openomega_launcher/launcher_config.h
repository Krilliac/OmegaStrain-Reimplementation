#pragma once

#include <expected>
#include <filesystem>
#include <optional>
#include <string>

namespace omega::launcher
{
// User choices owned by the prelaunch application. The game continues to
// consume these values through the project-owned runtime configuration grammar.
struct LauncherPreferences
{
    std::optional<std::filesystem::path> data_source;
    bool gamepad_enabled = false;
};

// [launcher UI thread] Missing files resolve to defaults. Existing files are
// parsed by the runtime configuration service, and selected paths are decoded
// as strict UTF-8. Diagnostics never include a configuration path, selected
// data path, or raw setting value.
[[nodiscard]] std::expected<LauncherPreferences, std::string> LoadLauncherPreferences(
    const std::filesystem::path& config_path);

// [launcher UI thread] Preserves parsed entries other than the two
// launcher-owned settings, validates the canonical result with the runtime
// parser, and atomically replaces the target using a same-directory temporary.
// Selected paths are encoded as strict UTF-8. Diagnostics never include a
// configuration path, selected data path, or raw setting value.
[[nodiscard]] std::expected<void, std::string> SaveLauncherPreferencesAtomically(
    const std::filesystem::path& config_path, const LauncherPreferences& preferences);
} // namespace omega::launcher
