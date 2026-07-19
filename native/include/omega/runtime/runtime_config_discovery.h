#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace omega::runtime
{
enum class RuntimeConfigPlatform : std::uint8_t
{
    Windows,
    MacOS,
    Xdg,
};

struct RuntimeConfigSearchRoots
{
    std::optional<std::filesystem::path> local_app_data;
    std::optional<std::filesystem::path> xdg_config_home;
    std::optional<std::filesystem::path> home;
};

// [any thread; reentrant] Reports only the compile-time host family. No
// environment or filesystem access occurs here.
[[nodiscard]] RuntimeConfigPlatform HostRuntimeConfigPlatform() noexcept;

// [any thread; reentrant] Lexically selects the project-owned default profile
// path from an already captured set of search roots. The function performs no
// environment lookup, normalization, canonicalization, filesystem access,
// directory creation, or token expansion.
[[nodiscard]] std::optional<std::filesystem::path> ResolveDefaultRuntimeConfigPath(
    RuntimeConfigPlatform platform, const RuntimeConfigSearchRoots& roots);
} // namespace omega::runtime
