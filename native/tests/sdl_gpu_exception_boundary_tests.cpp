#include "sdl_gpu_exception_boundary.h"

#include <expected>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using omega::app::detail::SdlGpuExceptionMessages;

void CheckBoundary(const SdlGpuExceptionMessages messages,
                   const std::string_view expected_allocation_failure,
                   const std::string_view expected_unexpected_failure,
                   int &failures) {
  const auto Check = [&failures](const bool condition,
                                 const std::string_view message) {
    if (!condition) {
      std::cerr << "FAILED: " << message << '\n';
      ++failures;
    }
  };

  int invocations = 0;
  const auto success = omega::app::detail::InvokeSdlGpuExceptionBoundary(
      messages, [&invocations]() -> std::expected<void, std::string> {
        ++invocations;
        return {};
      });
  Check(success.has_value() && invocations == 1,
        "success passes through and invokes the operation exactly once");

  const auto expected_failure =
      omega::app::detail::InvokeSdlGpuExceptionBoundary(
          messages, []() -> std::expected<void, std::string> {
            return std::unexpected("ordinary expected failure");
          });
  Check(!expected_failure &&
            expected_failure.error() == "ordinary expected failure",
        "ordinary expected errors pass through unchanged");

  const auto allocation_failure =
      omega::app::detail::InvokeSdlGpuExceptionBoundary(
          messages,
          []() -> std::expected<void, std::string> { throw std::bad_alloc{}; });
  Check(!allocation_failure &&
            allocation_failure.error() == expected_allocation_failure,
        "allocation exceptions map to the fixed allocation category");

  const auto unexpected_failure =
      omega::app::detail::InvokeSdlGpuExceptionBoundary(
          messages, []() -> std::expected<void, std::string> {
            throw std::runtime_error(
                "private path or backend exception detail");
          });
  Check(!unexpected_failure &&
            unexpected_failure.error() == expected_unexpected_failure,
        "unexpected exceptions map to the fixed sanitized category");
  Check(unexpected_failure.error().find("private path") == std::string::npos,
        "unexpected exception details are not exposed");
}
} // namespace

int main() {
  int failures = 0;
  CheckBoundary(omega::app::detail::kReleaseTextureExceptionMessages,
                "render texture release error allocation failed",
                "render texture release failed unexpectedly", failures);
  CheckBoundary(omega::app::detail::kReleaseMeshExceptionMessages,
                "render mesh release error allocation failed",
                "render mesh release failed unexpectedly", failures);
  CheckBoundary(omega::app::detail::kWaitForIdleExceptionMessages,
                "SDL GPU idle wait error allocation failed",
                "SDL GPU idle wait failed unexpectedly", failures);

  if (failures == 0)
    std::cout << "omega_sdl_gpu_exception_boundary_tests: all checks passed\n";
  return failures == 0 ? 0 : 1;
}
