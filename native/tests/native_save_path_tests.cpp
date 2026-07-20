#include "omega/persistence/native_save_path.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string_view>

namespace {
static_assert(sizeof(omega::persistence::NativeSavePlatform) == 1U);
static_assert(static_cast<std::uint8_t>(
                  omega::persistence::NativeSavePlatform::Windows) == 0U);
static_assert(static_cast<std::uint8_t>(
                  omega::persistence::NativeSavePlatform::MacOS) == 1U);
static_assert(static_cast<std::uint8_t>(
                  omega::persistence::NativeSavePlatform::Xdg) == 2U);
static_assert(noexcept(omega::persistence::HostNativeSavePlatform()));

int failures = 0;

void Check(const bool condition, const std::string_view message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    ++failures;
  }
}

[[nodiscard]] std::filesystem::path
SyntheticAbsoluteRoot(const std::string_view suffix) {
#if defined(_WIN32)
  return std::filesystem::path("C:/openomega-native-save-path-tests") / suffix;
#else
  return std::filesystem::path("/openomega-native-save-path-tests") / suffix;
#endif
}
} // namespace

int main() {
  using omega::persistence::NativeSavePlatform;
  using omega::persistence::NativeSaveSearchRoots;
  using omega::persistence::ResolveDefaultNativeSavePath;

#if defined(_WIN32)
  Check(omega::persistence::HostNativeSavePlatform() ==
            NativeSavePlatform::Windows,
        "the compile-time Windows host family is reported without I/O");
#elif defined(__APPLE__)
  Check(omega::persistence::HostNativeSavePlatform() ==
            NativeSavePlatform::MacOS,
        "the compile-time macOS host family is reported without I/O");
#else
  Check(omega::persistence::HostNativeSavePlatform() == NativeSavePlatform::Xdg,
        "the compile-time XDG host family is reported without I/O");
#endif

  const auto local_root = SyntheticAbsoluteRoot("local");
  const auto xdg_root = SyntheticAbsoluteRoot("xdg");
  const auto home_root = SyntheticAbsoluteRoot("home");
  NativeSaveSearchRoots roots{
      .local_app_data = local_root,
      .xdg_data_home = xdg_root,
      .home = home_root,
  };

  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::Windows, roots) ==
            std::optional<std::filesystem::path>{local_root / "OpenOmega" /
                                                 "native-save"},
        "Windows appends only the fixed OpenOmega native-save suffix");
  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::MacOS, roots) ==
            std::optional<std::filesystem::path>{home_root / "Library" /
                                                 "Application Support" /
                                                 "OpenOmega" / "native-save"},
        "macOS appends only the fixed Application Support native-save suffix");
  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots) ==
            std::optional<std::filesystem::path>{xdg_root / "openomega" /
                                                 "native-save"},
        "XDG_DATA_HOME takes precedence over HOME");

  roots.xdg_data_home.reset();
  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots) ==
            std::optional<std::filesystem::path>{
                home_root / ".local" / "share" / "openomega" / "native-save"},
        "XDG falls back to HOME/.local/share when XDG_DATA_HOME is absent");
  roots.xdg_data_home = std::filesystem::path{};
  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots) ==
            std::optional<std::filesystem::path>{
                home_root / ".local" / "share" / "openomega" / "native-save"},
        "an empty XDG_DATA_HOME falls back to absolute HOME");
  roots.xdg_data_home = std::filesystem::path("relative-xdg");
  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots) ==
            std::optional<std::filesystem::path>{
                home_root / ".local" / "share" / "openomega" / "native-save"},
        "a relative XDG_DATA_HOME falls back to absolute HOME");

  roots.home.reset();
  Check(!ResolveDefaultNativeSavePath(NativeSavePlatform::MacOS, roots) &&
            !ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots),
        "missing HOME produces no macOS or XDG fallback path");
  roots.home = std::filesystem::path{};
  Check(!ResolveDefaultNativeSavePath(NativeSavePlatform::MacOS, roots) &&
            !ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots),
        "empty HOME produces no macOS or XDG fallback path");
  roots.home = std::filesystem::path("relative-home");
  Check(!ResolveDefaultNativeSavePath(NativeSavePlatform::MacOS, roots) &&
            !ResolveDefaultNativeSavePath(NativeSavePlatform::Xdg, roots),
        "relative HOME produces no macOS or XDG fallback path");

  roots.local_app_data.reset();
  Check(!ResolveDefaultNativeSavePath(NativeSavePlatform::Windows, roots),
        "missing LOCALAPPDATA produces no Windows path");
  roots.local_app_data = std::filesystem::path{};
  Check(!ResolveDefaultNativeSavePath(NativeSavePlatform::Windows, roots),
        "empty LOCALAPPDATA produces no Windows path");
  roots.local_app_data = std::filesystem::path("relative-local");
  Check(!ResolveDefaultNativeSavePath(NativeSavePlatform::Windows, roots),
        "relative LOCALAPPDATA produces no Windows path");

  const auto literal_root =
      SyntheticAbsoluteRoot("literal") / ".." / "$HOME" / "%LOCALAPPDATA%";
  roots.local_app_data = literal_root;
  Check(ResolveDefaultNativeSavePath(NativeSavePlatform::Windows, roots) ==
            std::optional<std::filesystem::path>{literal_root / "OpenOmega" /
                                                 "native-save"},
        "the resolver preserves dot-dot and environment-looking tokens "
        "literally");

  return failures == 0 ? 0 : 1;
}
