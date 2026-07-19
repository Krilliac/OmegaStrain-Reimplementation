#pragma once

#include "omega/runtime/render_frame_packet.h"
#include "omega/runtime/render_texture.h"

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

namespace omega::app
{
class SdlPlatformService;
namespace detail
{
struct SdlGpuHostTestAccess;
}

// Aggregate-only main/render-thread diagnostics. Counters saturate instead of wrapping and expose
// no backend pointer, pool identity, texture identity, input identity, or source metadata.
struct GpuHostSnapshot
{
    runtime::RenderTexturePoolSnapshot textures;
    std::uint64_t successful_uploads = 0U;
    std::uint64_t successful_upload_logical_bytes = 0U;
    std::uint64_t successful_releases = 0U;
    std::uint64_t frame_submissions = 0U;
    std::uint64_t blit_submissions = 0U;
    std::uint64_t successful_blit_draws = 0U;
    std::uint64_t clear_submissions = 0U;
    std::uint64_t unavailable_swapchain_submissions = 0U;
    std::uint64_t rejected_nondefault_texture_handles = 0U;

    friend constexpr bool operator==(
        const GpuHostSnapshot&, const GpuHostSnapshot&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<GpuHostSnapshot>);
static_assert(std::is_standard_layout_v<GpuHostSnapshot>);

// Non-hot-reloadable main/render-thread owner for SDL window and GPU resources.
class SdlGpuHost final
{
public:
    // [main/render thread] Creates a fixed portable metadata pool and parallel backend slot table.
    [[nodiscard]] static std::expected<SdlGpuHost, std::string> Create(
        const SdlPlatformService& platform, bool debug_device,
        runtime::RenderTexturePoolConfig texture_config = {});

    // [main/render thread]
    ~SdlGpuHost();
    SdlGpuHost(SdlGpuHost&&) noexcept;
    SdlGpuHost& operator=(SdlGpuHost&&) noexcept = delete;
    SdlGpuHost(const SdlGpuHost&) = delete;
    SdlGpuHost& operator=(const SdlGpuHost&) = delete;

    // [main/render thread] Synchronously uploads one exact tightly packed RGBA8 image. The caller's
    // pixels are borrowed only through command submission and are never retained.
    [[nodiscard]] std::expected<runtime::RenderTextureHandle, std::string>
        UploadRgba8Texture(runtime::Rgba8TextureUploadView upload);

    // [main/render thread] Waits for GPU idle before releasing one exact resident generation.
    [[nodiscard]] std::expected<void, std::string> ReleaseTexture(
        const runtime::RenderTextureHandle& handle);

    // [main/render thread] Checked synchronization point; failure leaves residency unchanged.
    [[nodiscard]] std::expected<void, std::string> WaitForIdle();

    // [main/render thread] Synchronously consumes one owned renderer-neutral frame packet. Every
    // draw-list handle must resolve as a currently resident generation before any GPU work begins.
    // The packet's RGBA8 clear color applies to every available target clear; an empty list
    // preserves clear-only rendering.
    [[nodiscard]] std::expected<void, std::string> RenderFrame(
        const runtime::RenderFramePacket& packet);

    // [main/render thread] Aggregate-only snapshots; neither exposes a resource identity.
    [[nodiscard]] runtime::RenderTexturePoolSnapshot TextureSnapshot() const noexcept;
    [[nodiscard]] GpuHostSnapshot Snapshot() const noexcept;
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    friend struct detail::SdlGpuHostTestAccess;

    // [main/render thread, test access only] Returns four owned pixels from a temporary synthetic
    // 2x2 RGBA8 clear target. No backend resource, production counter, or residency escapes.
    [[nodiscard]] std::expected<
        std::array<runtime::RenderClearColorRgba8, 4U>, std::string>
        ReadbackClearForTesting(const runtime::RenderFramePacket& packet);

    // [main/render thread, test access only] Returns sixteen owned row-major pixels from a
    // temporary synthetic 4x4 RGBA8 target. Referenced generations remain caller-retained and
    // host-resident; no backend resource, production counter, or residency escapes.
    [[nodiscard]] std::expected<
        std::array<runtime::RenderClearColorRgba8, 16U>, std::string>
        ReadbackBlitsForTesting(const runtime::RenderFramePacket& packet);

    struct Impl;
    explicit SdlGpuHost(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
