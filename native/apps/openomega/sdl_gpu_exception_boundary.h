#pragma once

#include <expected>
#include <new>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app::detail {
struct SdlGpuExceptionMessages {
  std::string_view allocation_failure;
  std::string_view unexpected_failure;
};

inline constexpr SdlGpuExceptionMessages kReleaseTextureExceptionMessages{
    .allocation_failure = "render texture release error allocation failed",
    .unexpected_failure = "render texture release failed unexpectedly",
};

inline constexpr SdlGpuExceptionMessages kReleaseMeshExceptionMessages{
    .allocation_failure = "render mesh release error allocation failed",
    .unexpected_failure = "render mesh release failed unexpectedly",
};

inline constexpr SdlGpuExceptionMessages kWaitForIdleExceptionMessages{
    .allocation_failure = "SDL GPU idle wait error allocation failed",
    .unexpected_failure = "SDL GPU idle wait failed unexpectedly",
};

// Maps thrown implementation details to the same categorical expected-error
// policy as the other SDL GPU entry points. The callable is invoked exactly
// once and ordinary expected errors pass through unchanged. This boundary is
// intentionally not noexcept: materializing the string error can itself
// allocate, as required by the existing public error type.
template <typename Operation>
[[nodiscard]] std::expected<void, std::string>
InvokeSdlGpuExceptionBoundary(const SdlGpuExceptionMessages messages,
                              Operation &&operation) {
  try {
    return std::forward<Operation>(operation)();
  } catch (const std::bad_alloc &) {
    return std::unexpected(std::string(messages.allocation_failure));
  } catch (...) {
    return std::unexpected(std::string(messages.unexpected_failure));
  }
}
} // namespace omega::app::detail
