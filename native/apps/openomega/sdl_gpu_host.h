#pragma once

#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace omega::runtime
{
struct ManifestDebugImage;
}

namespace omega::app
{
struct RunResult
{
    int rendered_frames = 0;
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

    // [main/render thread]
    [[nodiscard]] std::expected<RunResult, std::string> Run(int frame_limit);
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    struct Impl;
    explicit SdlGpuHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
