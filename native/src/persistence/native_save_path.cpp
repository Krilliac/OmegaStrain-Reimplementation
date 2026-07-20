#include "omega/persistence/native_save_path.h"

namespace omega::persistence {
namespace {
[[nodiscard]] bool IsUsableAbsoluteRoot(
    const std::optional<std::filesystem::path> &root) noexcept {
  return root && !root->empty() && root->is_absolute();
}
} // namespace

NativeSavePlatform HostNativeSavePlatform() noexcept {
#if defined(_WIN32)
  return NativeSavePlatform::Windows;
#elif defined(__APPLE__)
  return NativeSavePlatform::MacOS;
#else
  return NativeSavePlatform::Xdg;
#endif
}

std::optional<std::filesystem::path>
ResolveDefaultNativeSavePath(const NativeSavePlatform platform,
                             const NativeSaveSearchRoots &roots) {
  switch (platform) {
  case NativeSavePlatform::Windows:
    if (!IsUsableAbsoluteRoot(roots.local_app_data))
      return std::nullopt;
    return *roots.local_app_data / "OpenOmega" / "native-save";
  case NativeSavePlatform::MacOS:
    if (!IsUsableAbsoluteRoot(roots.home))
      return std::nullopt;
    return *roots.home / "Library" / "Application Support" / "OpenOmega" /
           "native-save";
  case NativeSavePlatform::Xdg:
    if (IsUsableAbsoluteRoot(roots.xdg_data_home))
      return *roots.xdg_data_home / "openomega" / "native-save";
    if (!IsUsableAbsoluteRoot(roots.home))
      return std::nullopt;
    return *roots.home / ".local" / "share" / "openomega" / "native-save";
  }
  return std::nullopt;
}
} // namespace omega::persistence
