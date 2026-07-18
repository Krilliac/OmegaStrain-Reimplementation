#pragma once

#include "sdl_gpu_host.h"

#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/job_service.h"
#include "omega/runtime/log_service.h"
#include "omega/runtime/runtime_settings.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace omega::app
{
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

private:
    static constexpr std::uint32_t kQuitAction = 1U;

    OmegaApp(std::unique_ptr<runtime::ConfigStore> config,
        std::unique_ptr<runtime::ContentStartupState> content,
        std::unique_ptr<runtime::StderrLogSink> stderr_sink,
        std::unique_ptr<runtime::RingLogSink> ring_sink,
        std::unique_ptr<runtime::LogService> log,
        std::unique_ptr<runtime::JobService> jobs,
        std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
        std::unique_ptr<runtime::InputTracker> input, std::unique_ptr<SdlGpuHost> host) noexcept;

    // Declaration order is ownership order; destruction is the required reverse order.
    std::unique_ptr<runtime::ConfigStore> config_;
    std::unique_ptr<runtime::ContentStartupState> content_;
    std::unique_ptr<runtime::StderrLogSink> stderr_sink_;
    std::unique_ptr<runtime::RingLogSink> ring_sink_;
    std::unique_ptr<runtime::LogService> log_;
    std::unique_ptr<runtime::JobService> jobs_;
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler_;
    std::unique_ptr<runtime::InputTracker> input_;
    std::unique_ptr<SdlGpuHost> host_;
};
} // namespace omega::app
