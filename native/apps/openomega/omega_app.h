#pragma once

#include "sdl_audio_service.h"
#include "sdl_gpu_host.h"
#include "sdl_input_service.h"
#include "sdl_platform_service.h"
#include "run_capture.h"

#include "omega/runtime/asset_service.h"
#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/log_service.h"
#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_texture.h"
#include "omega/runtime/runtime_settings.h"
#include "omega/simulation/simulation_world.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace omega::app
{
namespace detail
{
struct OmegaAppTestAccess;
}

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
    ~OmegaApp() noexcept;
    OmegaApp(OmegaApp&&) noexcept;
    OmegaApp& operator=(OmegaApp&&) noexcept = delete;
    OmegaApp(const OmegaApp&) = delete;
    OmegaApp& operator=(const OmegaApp&) = delete;

    // [game/main/render thread]
    [[nodiscard]] std::expected<RunResult, std::string> Run(int frame_limit);

    // [game/main/render thread] Runs one finite diagnostic capture. Pre-loop validation and
    // backing-allocation failures return unexpected without mutating app services; operational
    // failures after loop entry are owned by the returned outcome and are not rolled back.
    [[nodiscard]] std::expected<RunCaptureOutcome, std::string> RunWithCapture(
        int frame_limit);
    [[nodiscard]] std::string_view driver_name() const noexcept;
    [[nodiscard]] std::string_view audio_driver_name() const noexcept;
    [[nodiscard]] int audio_sample_rate() const noexcept;
    [[nodiscard]] int audio_channel_count() const noexcept;

private:
    friend struct detail::OmegaAppTestAccess;

    static constexpr std::uint32_t kQuitAction = 1U;

    struct RunLoopResult
    {
        RunResult result;
        std::optional<std::string> operational_error;
        std::optional<runtime::RunCaptureSessionError> capture_error;
    };

    [[nodiscard]] RunLoopResult RunLoop(
        int frame_limit, runtime::RunCaptureSession* capture_session);

    OmegaApp(std::unique_ptr<runtime::ConfigStore> config,
        std::unique_ptr<runtime::ContentStartupState> content,
        std::unique_ptr<runtime::StderrLogSink> stderr_sink,
        std::unique_ptr<runtime::RingLogSink> ring_sink,
        std::unique_ptr<runtime::LogService> log,
        std::unique_ptr<runtime::JobService> jobs,
        std::unique_ptr<runtime::AssetService> assets,
        std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
        std::unique_ptr<runtime::InputTracker> input,
        std::unique_ptr<simulation::SimulationWorld> simulation,
        simulation::EntityId debug_locomotion_entity,
        std::unique_ptr<SdlPlatformService> platform,
        std::unique_ptr<SdlInputService> sdl_input,
        std::unique_ptr<SdlAudioService> audio,
        std::unique_ptr<SdlGpuHost> host,
        runtime::RenderTextureHandle diagnostic_texture,
        runtime::RenderDrawList diagnostic_draw_list) noexcept;

    // Declaration order is ownership order; destruction is the required reverse order.
    std::unique_ptr<runtime::ConfigStore> config_;
    std::unique_ptr<runtime::ContentStartupState> content_;
    std::unique_ptr<runtime::StderrLogSink> stderr_sink_;
    std::unique_ptr<runtime::RingLogSink> ring_sink_;
    std::unique_ptr<runtime::LogService> log_;
    std::unique_ptr<runtime::JobService> jobs_;
    std::unique_ptr<runtime::AssetService> assets_;
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler_;
    std::unique_ptr<runtime::InputTracker> input_;
    std::unique_ptr<simulation::SimulationWorld> simulation_;
    // Non-owning world-scoped identity. SimulationWorld owns the positioned component.
    simulation::EntityId debug_locomotion_entity_{};
    std::unique_ptr<SdlPlatformService> platform_;
    std::unique_ptr<SdlInputService> sdl_input_;
    std::unique_ptr<SdlAudioService> audio_;
    std::unique_ptr<SdlGpuHost> host_;
    // Non-owning generation-scoped identity. The host remains the backend-resource owner and a
    // default-moved-from app cannot release this copied value because its host_ is null.
    runtime::RenderTextureHandle diagnostic_texture_;
    // Immutable non-owning draw data, retained independently from the explicit release handle.
    runtime::RenderDrawList diagnostic_draw_list_;
};
} // namespace omega::app
