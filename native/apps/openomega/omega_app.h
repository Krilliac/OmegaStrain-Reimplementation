#pragma once

#include "sdl_audio_service.h"
#include "sdl_gpu_host.h"
#include "sdl_input_service.h"
#include "sdl_platform_service.h"

#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/log_service.h"
#include "omega/runtime/runtime_settings.h"
#include "omega/simulation/simulation_world.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace omega::app
{
struct RunResult
{
    int rendered_frames = 0;
    std::uint64_t planned_simulation_steps = 0;
    std::uint64_t executed_simulation_steps = 0;
    std::uint64_t input_frames = 0;
    std::uint64_t clamped_frame_count = 0;
    std::uint64_t dropped_time_frame_count = 0;
    std::uint64_t audio_callback_count = 0;
    std::uint64_t audio_frames_provided = 0;
    bool quit_requested = false;
};

// Non-hot-reloadable native composition root. It is the sole owner of service lifetimes; SDL and
// service dependencies receive only non-owning references whose lifetime is guaranteed here.
class OmegaApp final
{
public:
    // [game/main thread, startup] Creates services in dependency order and the SDL/GPU host last.
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        runtime::ContentStartupState content, bool debug_device);

    // [game/main thread, after all worker clients have stopped]
    ~OmegaApp();
    OmegaApp(OmegaApp&&) noexcept;
    OmegaApp& operator=(OmegaApp&&) noexcept = delete;
    OmegaApp(const OmegaApp&) = delete;
    OmegaApp& operator=(const OmegaApp&) = delete;

    // [game/main/render thread]
    [[nodiscard]] std::expected<RunResult, std::string> Run(int frame_limit);
    [[nodiscard]] std::string_view driver_name() const noexcept;
    [[nodiscard]] std::string_view audio_driver_name() const noexcept;
    [[nodiscard]] int audio_sample_rate() const noexcept;
    [[nodiscard]] int audio_channel_count() const noexcept;

private:
    static constexpr std::uint32_t kQuitAction = 1U;

    OmegaApp(std::unique_ptr<runtime::ConfigStore> config,
        std::unique_ptr<runtime::ContentStartupState> content,
        std::unique_ptr<runtime::StderrLogSink> stderr_sink,
        std::unique_ptr<runtime::RingLogSink> ring_sink,
        std::unique_ptr<runtime::LogService> log,
        std::unique_ptr<runtime::JobService> jobs,
        std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
        std::unique_ptr<runtime::InputTracker> input,
        std::unique_ptr<simulation::SimulationWorld> simulation,
        std::unique_ptr<SdlPlatformService> platform,
        std::unique_ptr<SdlInputService> sdl_input,
        std::unique_ptr<SdlAudioService> audio,
        std::unique_ptr<SdlGpuHost> host) noexcept;

    // Declaration order is ownership order; destruction is the required reverse order.
    std::unique_ptr<runtime::ConfigStore> config_;
    std::unique_ptr<runtime::ContentStartupState> content_;
    std::unique_ptr<runtime::StderrLogSink> stderr_sink_;
    std::unique_ptr<runtime::RingLogSink> ring_sink_;
    std::unique_ptr<runtime::LogService> log_;
    std::unique_ptr<runtime::JobService> jobs_;
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler_;
    std::unique_ptr<runtime::InputTracker> input_;
    std::unique_ptr<simulation::SimulationWorld> simulation_;
    std::unique_ptr<SdlPlatformService> platform_;
    std::unique_ptr<SdlInputService> sdl_input_;
    std::unique_ptr<SdlAudioService> audio_;
    std::unique_ptr<SdlGpuHost> host_;
};
} // namespace omega::app
