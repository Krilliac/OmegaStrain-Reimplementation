#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace omega::runtime
{
class FrameScheduler;
class InputTracker;
class LogService;
struct ManifestDebugImage;
}

namespace omega::app
{
struct RunResult
{
    int rendered_frames = 0;
    std::uint64_t planned_simulation_steps = 0;
    std::uint64_t input_frames = 0;
    std::uint64_t clamped_frame_count = 0;
    std::uint64_t dropped_time_frame_count = 0;
    bool quit_requested = false;
};

// Non-hot-reloadable main/render-thread owner for SDL window and GPU resources.
class SdlGpuHost final
{
public:
    // [main/render thread] The debug image is uploaded during creation and is not retained.
    [[nodiscard]] static std::expected<SdlGpuHost, std::string> Create(
        const runtime::ManifestDebugImage* debug_image, bool debug_device);

    // [main/render thread, after GPU idle]
    ~SdlGpuHost();
    SdlGpuHost(SdlGpuHost&&) noexcept;
    SdlGpuHost& operator=(SdlGpuHost&&) noexcept;
    SdlGpuHost(const SdlGpuHost&) = delete;
    SdlGpuHost& operator=(const SdlGpuHost&) = delete;

    // [main/render thread] Runtime services are non-owning references whose lifetime is held by
    // OmegaApp. SDL events are translated into the platform-neutral tracker before frame planning.
    [[nodiscard]] std::expected<RunResult, std::string> Run(int frame_limit,
        runtime::FrameScheduler& frame_scheduler, runtime::InputTracker& input,
        runtime::LogService& log, std::uint32_t quit_action);
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    struct Impl;
    explicit SdlGpuHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
