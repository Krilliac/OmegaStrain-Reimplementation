#pragma once

#include <cstdint>
#include <filesystem>

namespace omega::tool {
enum class FrontendEnvelopeCommandTestEvent : std::uint8_t {
  DirectoryIteratorOpened,
  BeforeHogFileOpen,
  HogFileOpened,
};

struct FrontendEnvelopeCommandTestHooks {
  using Callback = void (*)(FrontendEnvelopeCommandTestEvent event,
                            const std::filesystem::path &path, void *context);

  Callback callback = nullptr;
  void *context = nullptr;
};

// Measures only bounded native acceptance of the project-defined FNT, GUI, and
// IE envelope hypotheses found inside top-level or nested HOG archives. The
// fixed aggregate and report contain counters only; source identities and
// passive descriptor observations are not copied into that aggregate or
// emitted.
[[nodiscard]] int
FrontendEnvelopeCoverageVerifyTree(const std::filesystem::path &root);

// Test-only entry point for deterministic filesystem-race injection. Production
// callers should use FrontendEnvelopeCoverageVerifyTree().
[[nodiscard]] int FrontendEnvelopeCoverageVerifyTreeForTesting(
    const std::filesystem::path &root, FrontendEnvelopeCommandTestHooks hooks);
} // namespace omega::tool
