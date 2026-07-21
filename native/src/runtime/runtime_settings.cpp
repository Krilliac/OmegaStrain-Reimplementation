#include "omega/runtime/runtime_settings.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace omega::runtime
{
namespace
{
constexpr std::array<std::string_view, 10> kKnownSettings{
    "log.minimum_severity",
    "log.ring_capacity",
    "jobs.worker_count",
    "jobs.max_pending_jobs",
    "frame.simulation_step_ns",
    "frame.max_steps_per_frame",
    "frame.max_delta_ns",
    "input.max_events_per_frame",
    "content.data_root",
    "content.level_code",
};

constexpr std::string_view kContentDataRootKey = "content.data_root";
constexpr std::string_view kContentLevelCodeKey = "content.level_code";
constexpr std::string_view kMissingDataRootMessage =
    "content.data_root is required when content.level_code is set";
constexpr std::string_view kInvalidDataRootMessage =
    "content.data_root must be a non-empty valid path";
constexpr std::string_view kInvalidLevelCodeMessage =
    "content.level_code must contain 1 to 32 ASCII alphanumeric characters";
constexpr std::string_view kInvalidOptionsMessage =
    "direct content launch options are inconsistent";
constexpr std::string_view kExplicitProfileErrorPrefix =
    "runtime configuration explicit profile: ";
constexpr std::string_view kDefaultProfileErrorPrefix =
    "runtime configuration default profile: ";
constexpr std::string_view kDefaultProfileResolutionError =
    "unable to resolve default config path";

[[nodiscard]] ContentLaunchProfileError MakeContentLaunchProfileError(
    const ContentLaunchProfileErrorCode code)
{
    std::string_view message;
    switch (code)
    {
    case ContentLaunchProfileErrorCode::MissingDataRoot:
        message = kMissingDataRootMessage;
        break;
    case ContentLaunchProfileErrorCode::InvalidDataRoot:
        message = kInvalidDataRootMessage;
        break;
    case ContentLaunchProfileErrorCode::InvalidLevelCode:
        message = kInvalidLevelCodeMessage;
        break;
    case ContentLaunchProfileErrorCode::InvalidOptions:
        message = kInvalidOptionsMessage;
        break;
    }
    return ContentLaunchProfileError{.code = code, .message = std::string(message)};
}

[[nodiscard]] bool IsValidContentLevelCode(const std::string_view value) noexcept
{
    if (value.empty() || value.size() > 32U)
        return false;
    for (const unsigned char character : value)
    {
        const bool upper = character >= static_cast<unsigned char>('A') &&
                           character <= static_cast<unsigned char>('Z');
        const bool lower = character >= static_cast<unsigned char>('a') &&
                           character <= static_cast<unsigned char>('z');
        const bool digit = character >= static_cast<unsigned char>('0') &&
                           character <= static_cast<unsigned char>('9');
        if (!upper && !lower && !digit)
            return false;
    }
    return true;
}

[[nodiscard]] std::string NormalizeContentLevelCode(const std::string_view value)
{
    std::string normalized;
    normalized.reserve(value.size());
    for (const unsigned char character : value)
    {
        const bool lower = character >= static_cast<unsigned char>('a') &&
                           character <= static_cast<unsigned char>('z');
        normalized.push_back(
            static_cast<char>(lower ? character - ('a' - 'A') : character));
    }
    return normalized;
}

[[nodiscard]] std::expected<std::optional<ContentLaunchProfile>, ContentLaunchProfileError>
ResolveConfiguredContentLaunchProfile(const ConfigStore& config)
{
    const auto configured_root = config.GetString(kContentDataRootKey);
    const auto configured_level = config.GetString(kContentLevelCodeKey);
    if (!configured_root && !configured_level)
        return std::optional<ContentLaunchProfile>{};
    if (!configured_root)
    {
        return std::unexpected(
            MakeContentLaunchProfileError(ContentLaunchProfileErrorCode::MissingDataRoot));
    }

    std::filesystem::path data_root;
    try
    {
        data_root = std::filesystem::path(*configured_root);
    }
    catch (...)
    {
        return std::unexpected(
            MakeContentLaunchProfileError(ContentLaunchProfileErrorCode::InvalidDataRoot));
    }
    if (data_root.empty())
    {
        return std::unexpected(
            MakeContentLaunchProfileError(ContentLaunchProfileErrorCode::InvalidDataRoot));
    }

    std::optional<std::string> level_code;
    if (configured_level)
    {
        if (!IsValidContentLevelCode(*configured_level))
        {
            return std::unexpected(
                MakeContentLaunchProfileError(ContentLaunchProfileErrorCode::InvalidLevelCode));
        }
        level_code = NormalizeContentLevelCode(*configured_level);
    }
    return std::optional<ContentLaunchProfile>{ContentLaunchProfile{
        .data_root = std::move(data_root),
        .level_code = std::move(level_code),
    }};
}

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

[[nodiscard]] std::expected<ConfigStore, std::string> ApplyRuntimeConfigOverrides(
    ConfigStore config, const LaunchOptions& options)
{
    for (const LaunchConfigOverride& override : options.config_overrides)
    {
        auto applied = config.ApplyOverride(override.key, override.value);
        if (!applied)
            return std::unexpected("--set=" + override.key + ": " + applied.error());
    }
    return config;
}
} // namespace

std::expected<ConfigStore, std::string> LoadRuntimeConfig(const LaunchOptions& options)
{
    auto loaded = options.config_path ? LoadConfigFile(*options.config_path) : ParseConfigText("");
    if (!loaded)
    {
        const std::string_view prefix =
            options.config_path ? kExplicitProfileErrorPrefix : kDefaultProfileErrorPrefix;
        return std::unexpected(std::string(prefix) + loaded.error());
    }

    return ApplyRuntimeConfigOverrides(std::move(*loaded), options);
}

std::expected<ConfigStore, std::string> LoadRuntimeConfig(
    const LaunchOptions& options,
    const std::optional<std::filesystem::path>& default_profile_path)
{
    // Explicit selection is both higher priority and a hard ambient-inspection bypass.
    if (options.config_path || !default_profile_path)
        return LoadRuntimeConfig(options);

    try
    {
        std::error_code inspection_error;
        const std::filesystem::file_status profile_status =
            std::filesystem::symlink_status(*default_profile_path, inspection_error);
        if (inspection_error)
        {
            if (inspection_error == std::errc::no_such_file_or_directory)
                return LoadRuntimeConfig(options);
            return std::unexpected(std::string(kDefaultProfileErrorPrefix) +
                                   "unable to inspect config file");
        }
        if (profile_status.type() == std::filesystem::file_type::not_found)
            return LoadRuntimeConfig(options);
        if (profile_status.type() != std::filesystem::file_type::regular)
        {
            return std::unexpected(std::string(kDefaultProfileErrorPrefix) +
                                   "config path is not a regular file");
        }

        auto loaded = LoadConfigFile(*default_profile_path);
        if (!loaded)
        {
            return std::unexpected(
                std::string(kDefaultProfileErrorPrefix) + loaded.error());
        }
        return ApplyRuntimeConfigOverrides(std::move(*loaded), options);
    }
    catch (...)
    {
        return std::unexpected(std::string(kDefaultProfileErrorPrefix) +
                               std::string(kDefaultProfileResolutionError));
    }
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

std::expected<std::optional<ContentLaunchProfile>, ContentLaunchProfileError>
ResolveContentLaunchProfile(const LaunchOptions& options, const ConfigStore& config)
{
    // Validate the effective file-plus-override tuple even when a direct CLI pair will win.
    auto configured = ResolveConfiguredContentLaunchProfile(config);
    if (!configured)
        return std::unexpected(std::move(configured).error());

    if (options.level_code && !options.data_root)
    {
        return std::unexpected(
            MakeContentLaunchProfileError(ContentLaunchProfileErrorCode::InvalidOptions));
    }
    if (!options.data_root)
        return configured;
    if (options.data_root->empty() ||
        (options.level_code && !IsValidContentLevelCode(*options.level_code)))
    {
        return std::unexpected(
            MakeContentLaunchProfileError(ContentLaunchProfileErrorCode::InvalidOptions));
    }

    std::optional<std::string> level_code;
    if (options.level_code)
        level_code = NormalizeContentLevelCode(*options.level_code);
    return std::optional<ContentLaunchProfile>{ContentLaunchProfile{
        .data_root = *options.data_root,
        .level_code = std::move(level_code),
    }};
}
} // namespace omega::runtime
