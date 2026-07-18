#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace omega::runtime
{
struct DebugImage;
}

namespace omega::app
{
class SdlPlatformService;

// Non-hot-reloadable main/render-thread owner for SDL window and GPU resources.
class SdlGpuHost final
{
public:
    // [main/render thread] The debug image is uploaded during creation and is not retained.
    [[nodiscard]] static std::expected<SdlGpuHost, std::string> Create(
        const SdlPlatformService& platform,
        const runtime::DebugImage* debug_image, bool debug_device);

    // [main/render thread, after GPU idle]
    ~SdlGpuHost();
    SdlGpuHost(SdlGpuHost&&) noexcept;
    SdlGpuHost& operator=(SdlGpuHost&&) noexcept = delete;
    SdlGpuHost(const SdlGpuHost&) = delete;
    SdlGpuHost& operator=(const SdlGpuHost&) = delete;

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
