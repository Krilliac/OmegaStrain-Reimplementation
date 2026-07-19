#include "sdl_gpu_host.h"
#include "sdl_platform_service.h"

#include "omega/runtime/render_frame_packet.h"
#include "omega/runtime/render_texture.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace
{
using Clock = std::chrono::steady_clock;

[[nodiscard]] int Fail(const std::string_view message)
{
    std::cerr << "omega_sdl_gpu_texture_smoke: " << message << '\n';
    return 1;
}

[[nodiscard]] int Fail(const std::string_view message, const std::string& detail)
{
    std::cerr << "omega_sdl_gpu_texture_smoke: " << message << ": " << detail << '\n';
    return 1;
}

template <std::size_t ByteCount>
void FillOpaquePattern(std::array<std::byte, ByteCount>& pixels,
    const std::uint32_t width, const std::uint32_t height, const std::uint8_t phase)
{
    for (std::uint32_t y = 0U; y < height; ++y)
    {
        for (std::uint32_t x = 0U; x < width; ++x)
        {
            const std::size_t offset =
                (static_cast<std::size_t>(y) * width + x) * 4U;
            const bool alternate = ((x / 2U) + (y / 2U) + phase) % 2U != 0U;
            pixels[offset + 0U] = static_cast<std::byte>(alternate ? 0xE0U : 0x20U);
            pixels[offset + 1U] = static_cast<std::byte>(alternate ? 0x50U : 0xC0U);
            pixels[offset + 2U] = static_cast<std::byte>(alternate ? 0x20U : 0xE0U);
            pixels[offset + 3U] = static_cast<std::byte>(0xFFU);
        }
    }
}

template <typename Predicate>
[[nodiscard]] bool RenderUntil(omega::app::SdlGpuHost& host,
    omega::runtime::RenderFramePacket& packet, Predicate&& reached,
    const std::string_view phase)
{
    constexpr std::uint32_t maximum_attempts = 240U;
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    for (std::uint32_t attempt = 0U;
         attempt < maximum_attempts && Clock::now() < deadline; ++attempt)
    {
        SDL_PumpEvents();
        auto rendered = host.RenderFrame(packet);
        if (!rendered)
        {
            std::cerr << "omega_sdl_gpu_texture_smoke: " << phase
                      << " render failed: " << rendered.error() << '\n';
            return false;
        }
        ++packet.rendered_frame_index;
        if (reached(host.Snapshot()))
            return true;
    }

    std::cerr << "omega_sdl_gpu_texture_smoke: " << phase
              << " did not reach a usable swapchain within its bound\n";
    return false;
}

[[nodiscard]] bool PoolIsEmpty(
    const omega::runtime::RenderTexturePoolSnapshot& pool) noexcept
{
    return pool.slot_capacity == 1U && pool.free_slots == 1U &&
           pool.reserved_slots == 0U && pool.resident_slots == 0U &&
           pool.retired_slots == 0U && pool.reserved_logical_bytes == 0U &&
           pool.resident_logical_bytes == 0U;
}
} // namespace

int main()
{
    auto created_platform = omega::app::SdlPlatformService::Create();
    if (!created_platform)
        return Fail("platform creation failed", created_platform.error());
    auto platform = std::move(*created_platform);

    std::string driver;
    std::uint64_t unavailable_submissions = 0U;
    {
        auto created_host = omega::app::SdlGpuHost::Create(platform, false,
            omega::runtime::RenderTexturePoolConfig{
                .slot_capacity = 1U,
                .maximum_resident_logical_bytes = 256U,
            });
        if (!created_host)
            return Fail("GPU host creation failed", created_host.error());
        auto host = std::move(*created_host);
        driver = host.driver_name();
        if (driver.empty())
            return Fail("GPU driver name is empty");

        omega::runtime::RenderFramePacket packet;
        if (!RenderUntil(host, packet,
                [](const omega::app::GpuHostSnapshot& snapshot)
                { return snapshot.clear_submissions == 1U; },
                "clear"))
            return 1;

        const omega::app::GpuHostSnapshot after_clear = host.Snapshot();
        if (after_clear.clear_submissions != 1U ||
            after_clear.blit_submissions != 0U ||
            after_clear.rejected_nondefault_texture_handles != 0U ||
            !PoolIsEmpty(after_clear.textures))
            return Fail("clear phase counters or residency are inconsistent");

        std::array<std::byte, 8U * 8U * 4U> pixels_a{};
        FillOpaquePattern(pixels_a, 8U, 8U, 0U);
        auto uploaded_a = host.UploadRgba8Texture(omega::runtime::Rgba8TextureUploadView{
            .width = 8U,
            .height = 8U,
            .pixels = pixels_a,
        });
        if (!uploaded_a)
            return Fail("texture A upload failed", uploaded_a.error());

        const omega::app::GpuHostSnapshot after_upload_a = host.Snapshot();
        if (after_upload_a.successful_uploads != 1U ||
            after_upload_a.successful_upload_logical_bytes != pixels_a.size() ||
            after_upload_a.textures.resident_slots != 1U ||
            after_upload_a.textures.resident_logical_bytes != pixels_a.size())
            return Fail("texture A upload counters or residency are inconsistent");

        packet.diagnostic_texture = *uploaded_a;
        if (!RenderUntil(host, packet,
                [](const omega::app::GpuHostSnapshot& snapshot)
                { return snapshot.blit_submissions == 1U; },
                "texture A blit"))
            return 1;

        auto released_a = host.ReleaseTexture(*uploaded_a);
        if (!released_a)
            return Fail("texture A release failed", released_a.error());
        const omega::app::GpuHostSnapshot after_release_a = host.Snapshot();
        if (after_release_a.successful_releases != 1U ||
            !PoolIsEmpty(after_release_a.textures))
            return Fail("texture A release did not restore zero residency");

        const omega::app::GpuHostSnapshot before_stale = host.Snapshot();
        auto stale_render = host.RenderFrame(packet);
        if (stale_render)
            return Fail("released texture A handle was accepted");
        omega::app::GpuHostSnapshot expected_stale = before_stale;
        ++expected_stale.rejected_nondefault_texture_handles;
        if (host.Snapshot() != expected_stale)
            return Fail("stale-handle rejection mutated unrelated host state");

        std::array<std::byte, 4U * 8U * 4U> pixels_b{};
        FillOpaquePattern(pixels_b, 4U, 8U, 1U);
        auto uploaded_b = host.UploadRgba8Texture(omega::runtime::Rgba8TextureUploadView{
            .width = 4U,
            .height = 8U,
            .pixels = pixels_b,
        });
        if (!uploaded_b)
            return Fail("texture B upload failed", uploaded_b.error());
        if (uploaded_b->pool_identity != uploaded_a->pool_identity ||
            uploaded_b->slot_index != uploaded_a->slot_index ||
            uploaded_b->generation == uploaded_a->generation ||
            uploaded_b->generation == 0U)
            return Fail("capacity-one texture slot did not reuse with a new generation");

        const omega::app::GpuHostSnapshot after_upload_b = host.Snapshot();
        if (after_upload_b.successful_uploads != 2U ||
            after_upload_b.successful_upload_logical_bytes !=
                pixels_a.size() + pixels_b.size() ||
            after_upload_b.textures.resident_slots != 1U ||
            after_upload_b.textures.resident_logical_bytes != pixels_b.size())
            return Fail("texture B upload counters or residency are inconsistent");

        packet.diagnostic_texture = *uploaded_b;
        if (!RenderUntil(host, packet,
                [](const omega::app::GpuHostSnapshot& snapshot)
                { return snapshot.blit_submissions == 2U; },
                "texture B blit"))
            return 1;

        auto released_b = host.ReleaseTexture(*uploaded_b);
        if (!released_b)
            return Fail("texture B release failed", released_b.error());
        auto idle = host.WaitForIdle();
        if (!idle)
            return Fail("final GPU idle wait failed", idle.error());

        const omega::app::GpuHostSnapshot final = host.Snapshot();
        if (final.successful_uploads != 2U ||
            final.successful_upload_logical_bytes !=
                pixels_a.size() + pixels_b.size() ||
            final.successful_releases != 2U || final.blit_submissions != 2U ||
            final.clear_submissions != 1U ||
            final.rejected_nondefault_texture_handles != 1U ||
            final.frame_submissions !=
                3U + final.unavailable_swapchain_submissions ||
            !PoolIsEmpty(final.textures))
            return Fail("final counters or zero-residual residency are inconsistent");
        unavailable_submissions = final.unavailable_swapchain_submissions;
    }

    std::cout << "omega_sdl_gpu_texture_smoke: passed driver=" << driver
              << " uploads=2 releases=2 blits=2 unavailable="
              << unavailable_submissions << '\n';
    return 0;
}
