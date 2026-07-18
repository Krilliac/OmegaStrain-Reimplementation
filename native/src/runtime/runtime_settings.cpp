#include "omega/runtime/runtime_settings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace omega::runtime
{
namespace
{
constexpr std::array<std::string_view, 8> kKnownSettings{
    "log.minimum_severity",
    "log.ring_capacity",
    "jobs.worker_count",
    "jobs.max_pending_jobs",
    "frame.simulation_step_ns",
    "frame.max_steps_per_frame",
    "frame.max_delta_ns",
    "input.max_events_per_frame",
};

[[nodiscard]] std::expected<std::int64_t, std::string> ReadBoundedInteger(
    const ConfigStore& config, const std::string_view key, const std::int64_t default_value,
    const std::int64_t minimum, const std::int64_t maximum)
{
    auto value = config.GetInt64(key);
    if (!value)
        return std::unexpected(std::string(key) + ": " + value.error());
    const std::int64_t resolved = value->value_or(default_value);
    if (resolved < minimum || resolved > maximum)
    {
        return std::unexpected(std::string(key) + " must be in [" +
                               std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
    }
    return resolved;
}

[[nodiscard]] std::expected<LogSeverity, std::string> ReadMinimumSeverity(
    const ConfigStore& config)
{
    const auto value = config.GetString("log.minimum_severity");
    if (!value)
        return LogSeverity::Info;
    if (*value == "trace")
        return LogSeverity::Trace;
    if (*value == "debug")
        return LogSeverity::Debug;
    if (*value == "info")
        return LogSeverity::Info;
    if (*value == "warning")
        return LogSeverity::Warning;
    if (*value == "error")
        return LogSeverity::Error;
    return std::unexpected(
        "log.minimum_severity must be one of trace, debug, info, warning, or error");
}
} // namespace

std::expected<ConfigStore, std::string> LoadRuntimeConfig(const LaunchOptions& options)
{
    auto loaded = options.config_path ? LoadConfigFile(*options.config_path) : ParseConfigText("");
    if (!loaded)
    {
        const std::string source = options.config_path ? options.config_path->string() : "defaults";
        return std::unexpected("runtime configuration " + source + ": " + loaded.error());
    }

    ConfigStore config = std::move(*loaded);
    for (const LaunchConfigOverride& override : options.config_overrides)
    {
        auto applied = config.ApplyOverride(override.key, override.value);
        if (!applied)
            return std::unexpected("--set=" + override.key + ": " + applied.error());
    }
    return config;
}

std::expected<RuntimeSettings, std::string> ResolveRuntimeSettings(const ConfigStore& config)
{
    for (const ConfigEntry& entry : config.entries())
    {
        if (std::ranges::find(kKnownSettings, entry.key) == kKnownSettings.end())
            return std::unexpected("unknown runtime setting: " + entry.key);
    }

    RuntimeSettings settings;
    auto severity = ReadMinimumSeverity(config);
    if (!severity)
        return std::unexpected(severity.error());
    settings.minimum_log_severity = *severity;

    auto ring_capacity = ReadBoundedInteger(config, "log.ring_capacity",
        static_cast<std::int64_t>(kDefaultLogRingCapacity), RingLogSink::kMinCapacity,
        RingLogSink::kMaxCapacity);
    if (!ring_capacity)
        return std::unexpected(ring_capacity.error());
    settings.log_ring_capacity = static_cast<std::size_t>(*ring_capacity);

    auto worker_count = ReadBoundedInteger(config, "jobs.worker_count",
        static_cast<std::int64_t>(DefaultJobWorkerCount()), JobService::kMinWorkerCount,
        JobService::kMaxWorkerCount);
    if (!worker_count)
        return std::unexpected(worker_count.error());
    settings.jobs.worker_count = static_cast<std::size_t>(*worker_count);

    auto pending_jobs = ReadBoundedInteger(config, "jobs.max_pending_jobs",
        static_cast<std::int64_t>(kDefaultMaxPendingJobs), 1,
        JobService::kMaxPendingJobsLimit);
    if (!pending_jobs)
        return std::unexpected(pending_jobs.error());
    settings.jobs.max_pending_jobs = static_cast<std::size_t>(*pending_jobs);

    auto simulation_step = ReadBoundedInteger(config, "frame.simulation_step_ns",
        kDefaultSimulationStep.count(), kMinimumSimulationStep.count(),
        kMaximumSimulationStep.count());
    if (!simulation_step)
        return std::unexpected(simulation_step.error());
    settings.frame.simulation_step = std::chrono::nanoseconds{*simulation_step};

    auto max_steps = ReadBoundedInteger(config, "frame.max_steps_per_frame",
        kDefaultMaxStepsPerFrame, 1, kMaximumStepsPerFrame);
    if (!max_steps)
        return std::unexpected(max_steps.error());
    settings.frame.max_steps_per_frame = static_cast<std::uint32_t>(*max_steps);

    auto max_delta = ReadBoundedInteger(config, "frame.max_delta_ns",
        kDefaultMaxFrameDelta.count(), settings.frame.simulation_step.count(),
        kMaximumFrameDelta.count());
    if (!max_delta)
        return std::unexpected(max_delta.error());
    settings.frame.max_frame_delta = std::chrono::nanoseconds{*max_delta};

    auto input_events = ReadBoundedInteger(config, "input.max_events_per_frame",
        static_cast<std::int64_t>(kDefaultMaxInputEventsPerFrame), 1,
        static_cast<std::int64_t>(InputTracker::kMaxEventsPerFrameLimit));
    if (!input_events)
        return std::unexpected(input_events.error());
    settings.max_input_events_per_frame = static_cast<std::size_t>(*input_events);
    return settings;
}
} // namespace omega::runtime
