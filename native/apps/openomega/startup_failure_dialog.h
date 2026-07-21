#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace omega::app
{
enum class StartupFailureStage : std::uint8_t
{
    RuntimeConfiguration = 0U,
    RuntimeSettings,
    ContentLaunchProfile,
    ContentStartup,
    NativePersistence,
    ApplicationStartup,
};

enum class StartupFailureDialogPolicy : std::uint8_t
{
    Allow = 0U,
    Suppress,
};

enum class StartupFailureDialogOutcome : std::uint8_t
{
    Suppressed = 0U,
    Presented,
    Unavailable,
};

struct StartupFailureDialogRequest
{
    StartupFailureStage stage;
    std::string_view category;
    std::string_view detail;
};

inline constexpr char kApplicationStartupFailureDialogCategory[] = "application-startup";
inline constexpr char kApplicationStartupFailureDialogDetail[] =
    "Application components could not be initialized.";

// Raw component and SDL diagnostics remain stderr-only. Packaged presentation uses this fixed,
// identity-free request so a backend message cannot disclose a host path or device identifier.
[[nodiscard]] inline constexpr StartupFailureDialogRequest
ApplicationStartupFailureDialogRequest() noexcept
{
    return {
        .stage = StartupFailureStage::ApplicationStartup,
        .category = kApplicationStartupFailureDialogCategory,
        .detail = kApplicationStartupFailureDialogDetail,
    };
}

inline constexpr char kStartupFailureDialogTitle[] = "OpenOmega startup error";
inline constexpr char kStartupFailureDialogDisableEnvironmentVariable[] =
    "OPENOMEGA_DISABLE_STARTUP_DIALOG";
inline constexpr std::size_t kStartupFailureDialogCategoryLimit = 48U;
inline constexpr std::size_t kStartupFailureDialogDetailLimit = 384U;
inline constexpr std::size_t kStartupFailureDialogMaximumTextSize = 616U;
inline constexpr std::size_t kStartupFailureDialogTextCapacity = 640U;

struct StartupFailureDialogText
{
    std::array<char, kStartupFailureDialogTextCapacity> bytes{};
    std::size_t size = 0U; // Excludes the terminating NUL.
};

// [any thread; reentrant; nonallocating; noexcept] Projects borrowed failure text into one owned,
// bounded ASCII message. No source view is retained.
[[nodiscard]] StartupFailureDialogText BuildStartupFailureDialogText(
    const StartupFailureDialogRequest& request) noexcept;

// [any thread; reentrant; noexcept] Only the exact value "1" suppresses presentation.
[[nodiscard]] StartupFailureDialogPolicy ResolveStartupFailureDialogPolicy(
    const char* disable_value) noexcept;

// [any thread; SDL cached-environment read only; noexcept]
[[nodiscard]] StartupFailureDialogPolicy
ReadStartupFailureDialogPolicyFromEnvironment() noexcept;

// [main thread; blocking; process startup; noexcept] Does not initialize SDL or own its lifetime.
[[nodiscard]] StartupFailureDialogOutcome TryShowStartupFailureDialog(
    const StartupFailureDialogRequest& request,
    StartupFailureDialogPolicy policy) noexcept;
} // namespace omega::app
