#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace omega::tool {
enum class FrontendEnvelopeCommandTestEvent : std::uint8_t {
  DirectoryIteratorOpened,
  BeforeHogFileOpen,
  HogFileOpened,
};

struct FrontendEnvelopeCommandTestHooks {
  using Callback = void (*)(FrontendEnvelopeCommandTestEvent event,
                            const std::filesystem::path &path, void *context);
  using RawChunkReadCallback = void (*)(const std::filesystem::path &path,
                                        std::uint64_t chunk_offset,
                                        std::span<std::byte> chunk,
                                        void *context);

  Callback callback = nullptr;
  // Test-only seam invoked after a parser read owns one complete physical
  // chunk on a cache miss and before that chunk's discovery digest is checked.
  RawChunkReadCallback after_raw_hog_chunk_read = nullptr;
  void *context = nullptr;
  // Test-only override for the global retained discovery-digest count.
  std::uint64_t maximum_discovery_chunk_digests = ~std::uint64_t{0};
  // Test-only override for the per-HOG physical authentication budget. The
  // production budget grows only by bounded nested-HOG physical spans.
  std::uint64_t maximum_authenticated_hog_read_bytes = ~std::uint64_t{0};
};

using FrontendEnvelopeSha256Digest = std::array<std::byte, 32>;

// Test-only known-answer seam for the private chunk authenticator. Production
// callers should use FrontendEnvelopeCoverageVerifyTree().
[[nodiscard]] FrontendEnvelopeSha256Digest
FrontendEnvelopeSha256ForTesting(std::span<const std::byte> bytes) noexcept;

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
