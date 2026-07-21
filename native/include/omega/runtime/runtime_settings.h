#pragma once

#include "omega/runtime/config_service.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/launch_options.h"
#include "omega/runtime/log_service.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace omega::runtime
{
// Synthetic-shell defaults. They are native runtime engineering choices, not measurements or
// claims about the retail engine's timing, queue depths, or worker topology.
inline constexpr std::size_t kDefaultLogRingCapacity = 256U;
inline constexpr std::size_t kDefaultMaxPendingJobs = 1024U;
inline constexpr std::chrono::nanoseconds kDefaultSimulationStep{16'666'667};
inline constexpr std::uint32_t kDefaultMaxStepsPerFrame = 8U;
inline constexpr std::chrono::nanoseconds kDefaultMaxFrameDelta{
    std::chrono::milliseconds{250}};
inline constexpr std::size_t kDefaultMaxInputEventsPerFrame = 1024U;

struct RuntimeSettings
{
    LogSeverity minimum_log_severity = LogSeverity::Info;
    std::size_t log_ring_capacity = kDefaultLogRingCapacity;
    JobServiceConfig jobs{
        .worker_count = DefaultJobWorkerCount(),
        .max_pending_jobs = kDefaultMaxPendingJobs,
    };
    FrameSchedulerConfig frame{
        .simulation_step = kDefaultSimulationStep,
        .max_steps_per_frame = kDefaultMaxStepsPerFrame,
        .max_frame_delta = kDefaultMaxFrameDelta,
    };
    std::size_t max_input_events_per_frame = kDefaultMaxInputEventsPerFrame;
};

struct ContentLaunchProfile
{
    std::filesystem::path data_root;
    std::optional<std::string> level_code;
};

enum class ContentLaunchProfileErrorCode : std::uint8_t
{
    MissingDataRoot = 0U,
    InvalidDataRoot,
    InvalidLevelCode,
    InvalidOptions,
};

[[nodiscard]] constexpr std::string_view ContentLaunchProfileErrorCodeName(
    const ContentLaunchProfileErrorCode code) noexcept
{
    switch (code)
    {
    case ContentLaunchProfileErrorCode::MissingDataRoot:
        return "missing-data-root";
    case ContentLaunchProfileErrorCode::InvalidDataRoot:
        return "invalid-data-root";
    case ContentLaunchProfileErrorCode::InvalidLevelCode:
        return "invalid-level-code";
    case ContentLaunchProfileErrorCode::InvalidOptions:
        return "invalid-options";
    }
    return "unknown";
}

struct ContentLaunchProfileError
{
    ContentLaunchProfileErrorCode code = ContentLaunchProfileErrorCode::InvalidOptions;
    std::string message;
};

// [game thread, startup] Loads only an explicitly selected project-owned configuration file, or
// an empty store when no path was supplied, then applies validated --set overrides in source
// order. This overload is ambient-free and performs no default-profile discovery. Failure
// diagnostics use fixed source categories and never interpolate source paths, keys, or values.
[[nodiscard]] std::expected<ConfigStore, std::string> LoadRuntimeConfig(
    const LaunchOptions& options);

// [game thread, startup] Selects an explicit --config path ahead of the supplied default-profile
// candidate. A missing default is an empty store; a reported non-regular final entry is rejected
// without following it. The caller remains responsible for environment capture and lexical path
// discovery. Failure diagnostics never interpolate source paths, keys, or values.
[[nodiscard]] std::expected<ConfigStore, std::string> LoadRuntimeConfig(
    const LaunchOptions& options,
    const std::optional<std::filesystem::path>& default_profile_path);

// [any thread; reentrant] Resolves the strict store into validated service settings. Unknown keys
// and values outside the service-owned bounds are rejected rather than ignored or clamped.
// Diagnostics may name only compile-time-known public settings; raw keys and values are omitted.
[[nodiscard]] std::expected<RuntimeSettings, std::string> ResolveRuntimeSettings(
    const ConfigStore& config);

// [game thread, startup] Validates the effective configuration content tuple before considering
// direct launch options. A direct data-root/optional-level pair then wins atomically and never
// inherits a configured level. No filesystem access or ambient profile discovery occurs here.
[[nodiscard]] std::expected<std::optional<ContentLaunchProfile>, ContentLaunchProfileError>
ResolveContentLaunchProfile(const LaunchOptions& options, const ConfigStore& config);
} // namespace omega::runtime
