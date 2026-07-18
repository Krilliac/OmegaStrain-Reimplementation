#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace omega::runtime
{
class InputTracker;
class LogService;
struct ManifestDebugImage;
}

namespace omega::app
{
class SdlPlatformService;

struct HostEventResult
{
    bool quit_requested = false;
};

// Non-hot-reloadable main/render-thread owner for SDL window and GPU resources.
class SdlGpuHost final
{
public:
    // [main/render thread] The debug image is uploaded during creation and is not retained.
    [[nodiscard]] static std::expected<SdlGpuHost, std::string> Create(
        const SdlPlatformService& platform,
        const runtime::ManifestDebugImage* debug_image, bool debug_device);

    // [main/render thread, after GPU idle]
    ~SdlGpuHost();
    SdlGpuHost(SdlGpuHost&&) noexcept;
    SdlGpuHost& operator=(SdlGpuHost&&) noexcept;
    SdlGpuHost(const SdlGpuHost&) = delete;
    SdlGpuHost& operator=(const SdlGpuHost&) = delete;

    // [main thread] Pumps every queued SDL event, translating neutral controls into the borrowed
    // tracker. OmegaApp owns frame closure, quit-action policy, timing, and simulation execution.
    [[nodiscard]] HostEventResult PumpEvents(
        runtime::InputTracker& input, runtime::LogService& log);

    // [main/render thread] Renders and submits one frame. The index affects only the synthetic
    // content-free clear color and never feeds simulation state.
    [[nodiscard]] std::expected<void, std::string> RenderFrame(
        std::uint64_t rendered_frame_index);
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    struct Impl;
    explicit SdlGpuHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
