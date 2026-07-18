#pragma once

#include "omega/runtime/config_service.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/launch_options.h"
#include "omega/runtime/log_service.h"

#include <chrono>
#include <cstddef>
#include <expected>
#include <string>

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

// [game thread, startup] Loads the optional project-owned configuration file, or an empty store
// when no path was supplied, then applies validated --set overrides in source order.
[[nodiscard]] std::expected<ConfigStore, std::string> LoadRuntimeConfig(
    const LaunchOptions& options);

// [any thread; reentrant] Resolves the strict store into validated service settings. Unknown keys
// and values outside the service-owned bounds are rejected rather than ignored or clamped.
[[nodiscard]] std::expected<RuntimeSettings, std::string> ResolveRuntimeSettings(
    const ConfigStore& config);
} // namespace omega::runtime
