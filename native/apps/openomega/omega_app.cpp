#include "omega_app.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <limits>
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

    std::unique_ptr<runtime::AssetService> assets;
    if (content_owner->level_texture_store)
    {
        if (!content_owner->game_data)
        {
            constexpr std::string_view error =
                "asset service requires an available game-data service";
            log->Error("startup", error);
            return std::unexpected(std::string(error));
        }

        auto created_assets = runtime::AssetService::Create(
            *jobs, *content_owner->game_data, *content_owner->level_texture_store);
        if (!created_assets)
        {
            const std::string error = "asset service: " + std::string(
                runtime::AssetServiceErrorCodeName(created_assets.error().code));
            log->Error("startup", error);
            return std::unexpected(error);
        }
        assets = std::move(*created_assets);
    }

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

    auto created_simulation = simulation::SimulationWorld::Create(
        {.fixed_step = settings.frame.simulation_step});
    if (!created_simulation)
    {
        log->Error("startup", "simulation world: " + created_simulation.error());
        return std::unexpected("simulation world: " + created_simulation.error());
    }
    auto simulation =
        std::make_unique<simulation::SimulationWorld>(std::move(*created_simulation));

    auto created_platform = SdlPlatformService::Create();
    if (!created_platform)
    {
        log->Error("startup", "SDL platform service: " + created_platform.error());
        return std::unexpected("SDL platform service: " + created_platform.error());
    }
    auto platform = std::make_unique<SdlPlatformService>(std::move(*created_platform));

    auto created_sdl_input = SdlInputService::Create(*platform);
    if (!created_sdl_input)
    {
        log->Error("startup", "SDL input service: " + created_sdl_input.error());
        return std::unexpected("SDL input service: " + created_sdl_input.error());
    }
    auto sdl_input = std::make_unique<SdlInputService>(std::move(*created_sdl_input));

    auto created_audio = SdlAudioService::Create(*platform);
    if (!created_audio)
    {
        log->Error("startup", "SDL audio service: " + created_audio.error());
        return std::unexpected("SDL audio service: " + created_audio.error());
    }
    auto audio = std::make_unique<SdlAudioService>(std::move(*created_audio));

    auto created_host = SdlGpuHost::Create(*platform, debug_device);
    if (!created_host)
    {
        log->Error("startup", "SDL/GPU host: " + created_host.error());
        return std::unexpected(created_host.error());
    }
    auto host = std::make_unique<SdlGpuHost>(std::move(*created_host));

    runtime::RenderTextureHandle diagnostic_texture;
    runtime::RenderDrawList diagnostic_draw_list;
    if (content_owner->debug_image)
    {
        const runtime::DebugImage& image = *content_owner->debug_image;
        auto uploaded = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
            .width = image.width,
            .height = image.height,
            .pixels = image.pixels(),
        });
        if (!uploaded)
        {
            const std::string error =
                "SDL/GPU diagnostic texture upload: " + uploaded.error();
            log->Error("startup", error);
            return std::unexpected(error);
        }

        constexpr runtime::RenderTargetRectQ16 full_target{
            .left = 0U,
            .top = 0U,
            .right = runtime::kNormalizedRenderExtent,
            .bottom = runtime::kNormalizedRenderExtent,
        };
        const std::array commands{
            runtime::RenderTextureBlitCommand{
                .texture = *uploaded,
                .destination = full_target,
            },
        };
        auto created_draw_list = runtime::RenderDrawList::Create(commands);
        if (!created_draw_list)
        {
            constexpr std::string_view error =
                "SDL/GPU diagnostic draw-list creation failed";
            log->Error("startup", error);
            return std::unexpected(std::string(error));
        }

        diagnostic_texture = *uploaded;
        diagnostic_draw_list = std::move(*created_draw_list);
    }

    log->Info("startup", "runtime services ready with " +
                             std::to_string(jobs->worker_count()) + " workers and " +
                             std::string(audio->driver_name()) + " audio");

    return OmegaApp(std::move(config_owner), std::move(content_owner), std::move(stderr_sink),
        std::move(ring_sink), std::move(log), std::move(jobs), std::move(assets),
        std::move(frame_scheduler), std::move(input), std::move(simulation),
        std::move(platform), std::move(sdl_input), std::move(audio), std::move(host),
        diagnostic_texture, std::move(diagnostic_draw_list));
}

OmegaApp::OmegaApp(std::unique_ptr<runtime::ConfigStore> config,
    std::unique_ptr<runtime::ContentStartupState> content,
    std::unique_ptr<runtime::StderrLogSink> stderr_sink,
    std::unique_ptr<runtime::RingLogSink> ring_sink,
    std::unique_ptr<runtime::LogService> log, std::unique_ptr<runtime::JobService> jobs,
    std::unique_ptr<runtime::AssetService> assets,
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
    std::unique_ptr<runtime::InputTracker> input,
    std::unique_ptr<simulation::SimulationWorld> simulation,
    std::unique_ptr<SdlPlatformService> platform,
    std::unique_ptr<SdlInputService> sdl_input,
    std::unique_ptr<SdlAudioService> audio,
    std::unique_ptr<SdlGpuHost> host,
    const runtime::RenderTextureHandle diagnostic_texture,
    runtime::RenderDrawList diagnostic_draw_list) noexcept
    : config_(std::move(config)), content_(std::move(content)),
      stderr_sink_(std::move(stderr_sink)), ring_sink_(std::move(ring_sink)),
      log_(std::move(log)), jobs_(std::move(jobs)), assets_(std::move(assets)),
      frame_scheduler_(std::move(frame_scheduler)), input_(std::move(input)),
      simulation_(std::move(simulation)), platform_(std::move(platform)),
      sdl_input_(std::move(sdl_input)), audio_(std::move(audio)), host_(std::move(host)),
      diagnostic_texture_(diagnostic_texture),
      diagnostic_draw_list_(std::move(diagnostic_draw_list))
{
}

OmegaApp::~OmegaApp() noexcept
{
    diagnostic_draw_list_ = {};
    if (host_ != nullptr && diagnostic_texture_.valid())
    {
        bool release_failed = false;
        try
        {
            release_failed = !host_->ReleaseTexture(diagnostic_texture_);
        }
        catch (...)
        {
            // The host remains the authoritative owner and releases all surviving resources.
            release_failed = true;
        }

        if (release_failed && log_ != nullptr)
        {
            try
            {
                log_->Warning("shutdown",
                    "diagnostic texture release failed; SDL/GPU host cleanup will retry");
            }
            catch (...)
            {
                // Destruction remains noexcept even if bounded shutdown logging cannot allocate.
            }
        }
    }
    diagnostic_texture_ = {};
}
OmegaApp::OmegaApp(OmegaApp&&) noexcept = default;

std::expected<RunResult, std::string> OmegaApp::Run(const int frame_limit)
{
    using Clock = std::chrono::steady_clock;

    log_->Info("runtime", "entering native host loop");
    RunResult result;
    bool running = true;
    auto previous_frame = Clock::now();
    while (running && (frame_limit < 0 || result.rendered_frames < frame_limit))
    {
        const InputPumpResult events = sdl_input_->PumpEvents(*input_, *log_);
        const runtime::InputSnapshot input_snapshot = input_->EndFrame();
        ++result.input_frames;
        if (input_snapshot.rejected_event_count() != 0U)
        {
            log_->Warning("input", "rejected " +
                                       std::to_string(input_snapshot.rejected_event_count()) +
                                       " host events in one frame");
        }
        if (events.quit_requested || input_snapshot.WasPressed(kQuitAction))
        {
            running = false;
            result.quit_requested = true;
            break;
        }

        const auto current_frame = Clock::now();
        const runtime::FramePlan plan = frame_scheduler_->BeginFrame(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                current_frame - previous_frame));
        previous_frame = current_frame;
        if (plan.simulation_steps >
            std::numeric_limits<std::uint64_t>::max() - result.planned_simulation_steps)
        {
            jobs_->WaitForIdle();
            constexpr std::string_view error = "run-local simulation step counter exhausted";
            log_->Error("simulation", error);
            return std::unexpected(std::string(error));
        }
        result.planned_simulation_steps += plan.simulation_steps;
        if (plan.clamped_delta)
        {
            ++result.clamped_frame_count;
            if (result.clamped_frame_count == 1U)
                log_->Warning("frame", "wall-time delta reached the configured clamp");
        }
        if (plan.dropped_time)
        {
            ++result.dropped_time_frame_count;
            if (result.dropped_time_frame_count == 1U)
                log_->Warning(
                    "frame", "fixed-step backlog exceeded the configured frame budget");
        }

        for (std::uint32_t step = 0; step < plan.simulation_steps; ++step)
        {
            if (simulation_->AdvanceOneStep() != simulation::SimulationStepResult::Advanced)
            {
                jobs_->WaitForIdle();
                constexpr std::string_view error =
                    "simulation world representation exhausted";
                log_->Error("simulation", error);
                return std::unexpected(std::string(error));
            }
            ++result.executed_simulation_steps;
        }

        const simulation::SimulationState simulation_snapshot = simulation_->Snapshot();
        const runtime::RenderFramePacket render_packet{
            .rendered_frame_index = static_cast<std::uint64_t>(result.rendered_frames),
            .completed_simulation_steps = simulation_snapshot.completed_steps,
            .simulated_time = simulation_snapshot.simulated_time,
            .alive_entities = simulation_snapshot.alive_entities,
            .draw_list = diagnostic_draw_list_,
        };
        auto rendered = host_->RenderFrame(render_packet);
        if (!rendered)
        {
            jobs_->WaitForIdle();
            log_->Error("render", rendered.error());
            return std::unexpected(rendered.error());
        }
        ++result.rendered_frames;

        if (audio_->Snapshot().callback_failures != 0U)
        {
            jobs_->WaitForIdle();
            constexpr std::string_view error =
                "audio callback failed to provide playback data";
            log_->Error("audio", error);
            return std::unexpected(std::string(error));
        }
    }

    jobs_->WaitForIdle();
    const AudioServiceSnapshot audio = audio_->Snapshot();
    if (audio.callback_failures != 0U)
    {
        constexpr std::string_view error = "audio callback failed to provide playback data";
        log_->Error("audio", error);
        return std::unexpected(std::string(error));
    }
    result.audio_callback_count = audio.callback_count;
    result.audio_frames_provided = audio.provided_frames;
    log_->Info("runtime", "host loop ended after " +
                              std::to_string(result.rendered_frames) + " rendered frames and " +
                              std::to_string(result.executed_simulation_steps) +
                              " executed simulation steps");
    return result;
}

std::string_view OmegaApp::driver_name() const noexcept
{
    return host_->driver_name();
}

std::string_view OmegaApp::audio_driver_name() const noexcept
{
    return audio_->driver_name();
}

int OmegaApp::audio_sample_rate() const noexcept
{
    return SdlAudioService::kSampleRate;
}

int OmegaApp::audio_channel_count() const noexcept
{
    return SdlAudioService::kChannelCount;
}
} // namespace omega::app
