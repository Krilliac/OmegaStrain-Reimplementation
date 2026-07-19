#include "omega/runtime/runtime_config_discovery.h"

namespace omega::runtime
{
namespace
{
[[nodiscard]] bool IsUsableAbsoluteRoot(const std::optional<std::filesystem::path>& root) noexcept
{
    return root && !root->empty() && root->is_absolute();
}
} // namespace

RuntimeConfigPlatform HostRuntimeConfigPlatform() noexcept
{
#if defined(_WIN32)
    return RuntimeConfigPlatform::Windows;
#elif defined(__APPLE__)
    return RuntimeConfigPlatform::MacOS;
#else
    return RuntimeConfigPlatform::Xdg;
#endif
}

std::optional<std::filesystem::path> ResolveDefaultRuntimeConfigPath(
    const RuntimeConfigPlatform platform, const RuntimeConfigSearchRoots& roots)
{
    switch (platform)
    {
    case RuntimeConfigPlatform::Windows:
        if (!IsUsableAbsoluteRoot(roots.local_app_data))
            return std::nullopt;
        return *roots.local_app_data / "OpenOmega" / "openomega.cfg";
    case RuntimeConfigPlatform::MacOS:
        if (!IsUsableAbsoluteRoot(roots.home))
            return std::nullopt;
        return *roots.home / "Library" / "Application Support" / "OpenOmega" / "openomega.cfg";
    case RuntimeConfigPlatform::Xdg:
        if (IsUsableAbsoluteRoot(roots.xdg_config_home))
            return *roots.xdg_config_home / "openomega" / "openomega.cfg";
        if (!IsUsableAbsoluteRoot(roots.home))
            return std::nullopt;
        return *roots.home / ".config" / "openomega" / "openomega.cfg";
    }
    return std::nullopt;
}
} // namespace omega::runtime
