#pragma once

#include <cstdint>
#include <filesystem>

namespace omega::tool {
enum class PopPostTerrainCommandTestEvent : std::uint8_t {
  DirectoryIteratorOpened,
  BeforePopFileOpen,
  PopFileOpened,
};

struct PopPostTerrainCommandTestHooks {
  using Callback = void (*)(PopPostTerrainCommandTestEvent event,
                            const std::filesystem::path &path, void *context);

  Callback callback = nullptr;
  void *context = nullptr;
};

// Verifies only native acceptance of the passive POP post-terrain hypothesis
// descriptor and emits one privacy-safe fixed-schema aggregate. Source
// identities and descriptor observations are never included in the report.
[[nodiscard]] int
PopPostTerrainHypothesesVerifyTree(const std::filesystem::path &root);

// Test-only entry point for deterministic filesystem-race injection. Production
// callers should use PopPostTerrainHypothesesVerifyTree().
[[nodiscard]] int PopPostTerrainHypothesesVerifyTreeForTesting(
    const std::filesystem::path &root, PopPostTerrainCommandTestHooks hooks);
} // namespace omega::tool
