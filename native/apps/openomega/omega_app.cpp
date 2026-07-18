#include "omega_app.h"

#include <SDL3/SDL.h>

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace omega::app
{
std::expected<OmegaApp, std::string> OmegaApp::Create(runtime::ConfigStore config,
    const runtime::RuntimeSettings& settings, runtime::ContentStartupState content,
    const bool debug_device)
{
    auto config_owner = std::make_unique<runtime::ConfigStore>(std::move(config));
    auto content_owner = std::make_unique<runtime::ContentStartupState>(std::move(content));
    auto stderr_sink = std::make_unique<runtime::StderrLogSink>();

    auto created_ring = runtime::RingLogSink::Create(settings.log_ring_capacity);
    if (!created_ring)
        return std::unexpected("log ring: " + created_ring.error());
    auto ring_sink = std::move(*created_ring);

    auto created_log = runtime::LogService::Create(runtime::LogServiceConfig{
        .minimum_severity = settings.minimum_log_severity,
        .max_category_bytes = 32U,
        .max_message_bytes = 512U,
        .sinks = {stderr_sink.get(), ring_sink.get()},
    });
    if (!created_log)
        return std::unexpected("logging service: " + created_log.error());
    auto log = std::make_unique<runtime::LogService>(std::move(*created_log));

    auto created_jobs = runtime::JobService::Create(settings.jobs);
    if (!created_jobs)
    {
        log->Error("startup", "job service: " + created_jobs.error());
        return std::unexpected("job service: " + created_jobs.error());
    }
    auto jobs = std::make_unique<runtime::JobService>(std::move(*created_jobs));

    auto created_scheduler = runtime::FrameScheduler::Create(settings.frame);
    if (!created_scheduler)
    {
        log->Error("startup", "frame scheduler: " + created_scheduler.error());
        return std::unexpected("frame scheduler: " + created_scheduler.error());
    }
    auto frame_scheduler =
        std::make_unique<runtime::FrameScheduler>(std::move(*created_scheduler));

    constexpr std::array bindings{
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_ESCAPE),
            .action = kQuitAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_BACK),
            .action = kQuitAction,
        },
    };
    auto binding_table = runtime::InputBindingTable::FromBindings(bindings);
    if (!binding_table)
    {
        log->Error("startup", "input bindings: " + binding_table.error());
        return std::unexpected("input bindings: " + binding_table.error());
    }
    auto created_input = runtime::InputTracker::Create(
        std::move(*binding_table), settings.max_input_events_per_frame);
    if (!created_input)
    {
        log->Error("startup", "input tracker: " + created_input.error());
        return std::unexpected("input tracker: " + created_input.error());
    }
    auto input = std::make_unique<runtime::InputTracker>(std::move(*created_input));

    auto created_host = SdlGpuHost::Create(
        content_owner->debug_image ? &*content_owner->debug_image : nullptr, debug_device);
    if (!created_host)
    {
        log->Error("startup", "SDL/GPU host: " + created_host.error());
        return std::unexpected(created_host.error());
    }
    auto host = std::make_unique<SdlGpuHost>(std::move(*created_host));
    log->Info("startup", "runtime services ready with " +
                             std::to_string(jobs->worker_count()) + " workers");

    return OmegaApp(std::move(config_owner), std::move(content_owner), std::move(stderr_sink),
        std::move(ring_sink), std::move(log), std::move(jobs), std::move(frame_scheduler),
        std::move(input), std::move(host));
}

OmegaApp::OmegaApp(std::unique_ptr<runtime::ConfigStore> config,
    std::unique_ptr<runtime::ContentStartupState> content,
    std::unique_ptr<runtime::StderrLogSink> stderr_sink,
    std::unique_ptr<runtime::RingLogSink> ring_sink,
    std::unique_ptr<runtime::LogService> log, std::unique_ptr<runtime::JobService> jobs,
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
    std::unique_ptr<runtime::InputTracker> input, std::unique_ptr<SdlGpuHost> host) noexcept
    : config_(std::move(config)), content_(std::move(content)),
      stderr_sink_(std::move(stderr_sink)), ring_sink_(std::move(ring_sink)),
      log_(std::move(log)), jobs_(std::move(jobs)),
      frame_scheduler_(std::move(frame_scheduler)), input_(std::move(input)),
      host_(std::move(host))
{
}

OmegaApp::~OmegaApp() = default;
OmegaApp::OmegaApp(OmegaApp&&) noexcept = default;

std::expected<RunResult, std::string> OmegaApp::Run(const int frame_limit)
{
    log_->Info("runtime", "entering native host loop");
    auto result = host_->Run(frame_limit, *frame_scheduler_, *input_, *log_, kQuitAction);
    jobs_->WaitForIdle();
    if (!result)
    {
        log_->Error("runtime", result.error());
        return std::unexpected(result.error());
    }

    log_->Info("runtime", "host loop ended after " +
                              std::to_string(result->rendered_frames) + " rendered frames and " +
                              std::to_string(result->planned_simulation_steps) +
                              " planned simulation steps");
    return result;
}

std::string_view OmegaApp::driver_name() const noexcept
{
    return host_->driver_name();
}
} // namespace omega::app
