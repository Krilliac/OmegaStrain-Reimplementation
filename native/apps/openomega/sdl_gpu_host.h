#pragma once

#include "omega/runtime/render_frame_packet.h"

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

    // [main/render thread] Synchronously consumes one owned renderer-neutral frame packet. The
    // frame index affects only the synthetic content-free clear color; simulation values are
    // transported across the boundary but do not yet drive a scene.
    [[nodiscard]] std::expected<void, std::string> RenderFrame(
        const runtime::RenderFramePacket& packet);
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    struct Impl;
    explicit SdlGpuHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
