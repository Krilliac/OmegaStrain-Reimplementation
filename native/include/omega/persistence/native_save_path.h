#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace omega::persistence {
enum class NativeSavePlatform : std::uint8_t {
  Windows = 0U,
  MacOS = 1U,
  Xdg = 2U,
};

struct NativeSaveSearchRoots {
  std::optional<std::filesystem::path> local_app_data;
  std::optional<std::filesystem::path> xdg_data_home;
  std::optional<std::filesystem::path> home;
};

// [any thread; reentrant] Reports only the compile-time host family. No
// environment or filesystem access occurs here.
[[nodiscard]] NativeSavePlatform HostNativeSavePlatform() noexcept;

// [any thread; reentrant] Selects the OpenOmega-owned native-save directory
// from already captured platform roots. This pure resolver performs no
// environment lookup, path normalization, canonicalization, filesystem access,
// directory creation, or token expansion. Relative and empty roots are ignored.
[[nodiscard]] std::optional<std::filesystem::path>
ResolveDefaultNativeSavePath(NativeSavePlatform platform,
                             const NativeSaveSearchRoots &roots);
} // namespace omega::persistence
