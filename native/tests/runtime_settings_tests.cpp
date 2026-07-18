#include "omega/runtime/runtime_settings.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool WriteTextFile(
    const std::filesystem::path& path, const std::string_view text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}
} // namespace

int RuntimeSettingsFailureCount()
{
    auto empty = omega::runtime::ParseConfigText("");
    Check(empty.has_value(), "an empty runtime configuration parses");
    if (empty)
    {
        auto defaults = omega::runtime::ResolveRuntimeSettings(*empty);
        Check(defaults &&
                  defaults->minimum_log_severity == omega::runtime::LogSeverity::Info &&
                  defaults->log_ring_capacity == omega::runtime::kDefaultLogRingCapacity &&
                  defaults->jobs.worker_count == omega::runtime::DefaultJobWorkerCount() &&
                  defaults->jobs.max_pending_jobs ==
                      omega::runtime::kDefaultMaxPendingJobs &&
                  defaults->frame.simulation_step ==
                      omega::runtime::kDefaultSimulationStep &&
                  defaults->frame.max_steps_per_frame ==
                      omega::runtime::kDefaultMaxStepsPerFrame &&
                  defaults->frame.max_frame_delta ==
                      omega::runtime::kDefaultMaxFrameDelta &&
                  defaults->max_input_events_per_frame ==
                      omega::runtime::kDefaultMaxInputEventsPerFrame,
            "the empty store resolves to explicit synthetic-shell defaults");
    }

    auto configured = omega::runtime::ParseConfigText(
        "log.minimum_severity = debug\n"
        "log.ring_capacity = 17\n"
        "jobs.worker_count = 2\n"
        "jobs.max_pending_jobs = 33\n"
        "frame.simulation_step_ns = 1000000\n"
        "frame.max_steps_per_frame = 3\n"
        "frame.max_delta_ns = 5000000\n"
        "input.max_events_per_frame = 41\n");
    Check(configured.has_value(), "a complete runtime configuration parses");
    if (configured)
    {
        auto resolved = omega::runtime::ResolveRuntimeSettings(*configured);
        Check(resolved && resolved->minimum_log_severity == omega::runtime::LogSeverity::Debug &&
                  resolved->log_ring_capacity == 17U && resolved->jobs.worker_count == 2U &&
                  resolved->jobs.max_pending_jobs == 33U &&
                  resolved->frame.simulation_step == std::chrono::milliseconds{1} &&
                  resolved->frame.max_steps_per_frame == 3U &&
                  resolved->frame.max_frame_delta == std::chrono::milliseconds{5} &&
                  resolved->max_input_events_per_frame == 41U,
            "known keys resolve without clamping or hidden defaults");
    }

    auto unknown = omega::runtime::ParseConfigText("renderer.guess = false\n");
    Check(unknown && !omega::runtime::ResolveRuntimeSettings(*unknown),
        "unknown settings are rejected rather than silently ignored");
    auto bad_severity = omega::runtime::ParseConfigText("log.minimum_severity = verbose\n");
    Check(bad_severity && !omega::runtime::ResolveRuntimeSettings(*bad_severity),
        "unknown log severities are rejected");
    auto bad_workers = omega::runtime::ParseConfigText("jobs.worker_count = 0\n");
    Check(bad_workers && !omega::runtime::ResolveRuntimeSettings(*bad_workers),
        "service-owned worker bounds are enforced");
    auto bad_delta = omega::runtime::ParseConfigText(
        "frame.simulation_step_ns = 2000000\nframe.max_delta_ns = 1000000\n");
    Check(bad_delta && !omega::runtime::ResolveRuntimeSettings(*bad_delta),
        "the frame delta clamp cannot be shorter than one simulation step");
    auto bad_integer = omega::runtime::ParseConfigText("input.max_events_per_frame = many\n");
    Check(bad_integer && !omega::runtime::ResolveRuntimeSettings(*bad_integer),
        "typed setting errors retain strict integer parsing");

    omega::runtime::LaunchOptions override_only;
    override_only.config_overrides.push_back(
        {.key = "jobs.worker_count", .value = "1"});
    auto overridden = omega::runtime::LoadRuntimeConfig(override_only);
    Check(overridden && overridden->RequireInt64("jobs.worker_count") == 1,
        "command-line overrides apply to the empty default store");

    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto root = std::filesystem::temp_directory_path() /
                      ("omega-runtime-settings-tests-" + std::to_string(suffix));
    std::error_code file_error;
    std::filesystem::create_directories(root, file_error);
    Check(!file_error, "temporary runtime-settings directory is created");
    const auto config_path = root / "openomega.cfg";
    Check(WriteTextFile(config_path,
              "jobs.worker_count = 2\nlog.minimum_severity = warning\n"),
        "runtime-settings fixture is written");

    omega::runtime::LaunchOptions file_options;
    file_options.config_path = config_path;
    file_options.config_overrides.push_back(
        {.key = "jobs.worker_count", .value = "3"});
    auto file_config = omega::runtime::LoadRuntimeConfig(file_options);
    Check(file_config && file_config->RequireInt64("jobs.worker_count") == 3 &&
              file_config->RequireString("log.minimum_severity") == "warning",
        "command-line values override a bounded configuration file");

    file_options.config_path = root / "missing.cfg";
    Check(!omega::runtime::LoadRuntimeConfig(file_options),
        "a requested missing runtime configuration is fatal");
    std::filesystem::remove_all(root, file_error);
    Check(!file_error, "temporary runtime-settings directory is removed");
    return failures;
}
