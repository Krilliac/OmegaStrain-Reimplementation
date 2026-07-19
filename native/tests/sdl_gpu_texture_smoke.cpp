#include "sdl_gpu_host.h"
#include "sdl_platform_service.h"

#include "omega/runtime/render_draw_list.h"
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
    return pool.slot_capacity == 2U && pool.free_slots == 2U &&
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
                .slot_capacity = 2U,
                .maximum_resident_logical_bytes = 384U,
            });
        if (!created_host)
            return Fail("GPU host creation failed", created_host.error());
        auto host = std::move(*created_host);
        driver = host.driver_name();
        if (driver.empty())
            return Fail("GPU driver name is empty");

        constexpr omega::runtime::RenderClearColorRgba8 clear_only_color{
            .red = 0U,
            .green = 128U,
            .blue = 255U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 first_blit_color{
            .red = 85U,
            .green = 102U,
            .blue = 119U,
            .alpha = 136U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 second_blit_color{
            .red = 153U,
            .green = 170U,
            .blue = 187U,
            .alpha = 204U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 stale_rejection_color{
            .red = 221U,
            .green = 51U,
            .blue = 170U,
            .alpha = 238U,
        };
        static_assert(clear_only_color != omega::runtime::kDefaultRenderClearColor &&
                      first_blit_color != omega::runtime::kDefaultRenderClearColor &&
                      second_blit_color != omega::runtime::kDefaultRenderClearColor &&
                      stale_rejection_color != omega::runtime::kDefaultRenderClearColor &&
                      first_blit_color != second_blit_color &&
                      first_blit_color != stale_rejection_color &&
                      second_blit_color != stale_rejection_color);

        omega::runtime::RenderFramePacket packet;
        packet.clear_color = clear_only_color;
        if (!RenderUntil(host, packet,
                [](const omega::app::GpuHostSnapshot& snapshot)
                { return snapshot.clear_submissions == 1U; },
                "clear"))
            return 1;

        const omega::app::GpuHostSnapshot after_clear = host.Snapshot();
        if (after_clear.clear_submissions != 1U ||
            after_clear.blit_submissions != 0U ||
            after_clear.successful_blit_draws != 0U ||
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

        std::array<std::byte, 4U * 8U * 4U> pixels_b{};
        FillOpaquePattern(pixels_b, 4U, 8U, 1U);
        auto uploaded_b = host.UploadRgba8Texture(omega::runtime::Rgba8TextureUploadView{
            .width = 4U,
            .height = 8U,
            .pixels = pixels_b,
        });
        if (!uploaded_b)
            return Fail("texture B upload failed", uploaded_b.error());

        const omega::app::GpuHostSnapshot after_upload_b = host.Snapshot();
        if (after_upload_b.successful_uploads != 2U ||
            after_upload_b.successful_upload_logical_bytes !=
                pixels_a.size() + pixels_b.size() ||
            after_upload_b.textures.slot_capacity != 2U ||
            after_upload_b.textures.free_slots != 0U ||
            after_upload_b.textures.resident_slots != 2U ||
            after_upload_b.textures.resident_logical_bytes !=
                pixels_a.size() + pixels_b.size() ||
            after_upload_b.textures.reserved_slots != 0U ||
            after_upload_b.textures.retired_slots != 0U)
            return Fail("texture A/B upload counters or residency are inconsistent");

        constexpr std::uint32_t half_extent =
            omega::runtime::kNormalizedRenderExtent / 2U;
        constexpr omega::runtime::RenderTargetRectQ16 left_half{
            .left = 0U,
            .top = 0U,
            .right = half_extent,
            .bottom = omega::runtime::kNormalizedRenderExtent,
        };
        constexpr omega::runtime::RenderTargetRectQ16 right_half{
            .left = half_extent,
            .top = 0U,
            .right = omega::runtime::kNormalizedRenderExtent,
            .bottom = omega::runtime::kNormalizedRenderExtent,
        };
        constexpr omega::runtime::RenderSourceRectQ16 full_source{
            .left = 0U,
            .top = 0U,
            .right = omega::runtime::kNormalizedRenderExtent,
            .bottom = omega::runtime::kNormalizedRenderExtent,
        };
        constexpr omega::runtime::RenderSourceRectQ16 first_b_source{
            .left = 0U,
            .top = 0U,
            .right = 32'768U,
            .bottom = omega::runtime::kNormalizedRenderExtent,
        };
        constexpr omega::runtime::RenderSourceRectQ16 first_a_source{
            .left = 16'384U,
            .top = 0U,
            .right = 49'152U,
            .bottom = omega::runtime::kNormalizedRenderExtent,
        };
        const std::array first_commands{
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_b,
                .source = first_b_source,
                .destination = left_half,
                .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Linear,
            },
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_a,
                .source = first_a_source,
                .destination = right_half,
                .fit_mode = omega::runtime::RenderTextureFitMode::Contain,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            },
        };
        auto first_draw_list = omega::runtime::RenderDrawList::Create(first_commands);
        if (!first_draw_list)
            return Fail("texture A/B draw-list creation failed");
        packet.draw_list = *first_draw_list;
        packet.clear_color = first_blit_color;
        if (!RenderUntil(host, packet,
                [](const omega::app::GpuHostSnapshot& snapshot)
                {
                    return snapshot.blit_submissions == 1U &&
                           snapshot.successful_blit_draws == 2U;
                },
                "texture A/B blit"))
            return 1;

        auto released_a = host.ReleaseTexture(*uploaded_a);
        if (!released_a)
            return Fail("texture A release failed", released_a.error());
        const omega::app::GpuHostSnapshot after_release_a = host.Snapshot();
        if (after_release_a.successful_releases != 1U ||
            after_release_a.textures.free_slots != 1U ||
            after_release_a.textures.resident_slots != 1U ||
            after_release_a.textures.resident_logical_bytes != pixels_b.size())
            return Fail("texture A release did not preserve only texture B");

        const omega::app::GpuHostSnapshot before_stale = host.Snapshot();
        packet.clear_color = stale_rejection_color;
        auto stale_render = host.RenderFrame(packet);
        if (stale_render)
            return Fail("draw list containing released texture A was accepted");
        omega::app::GpuHostSnapshot expected_stale = before_stale;
        ++expected_stale.rejected_nondefault_texture_handles;
        if (host.Snapshot() != expected_stale)
            return Fail("stale-list rejection mutated unrelated host state");

        std::array<std::byte, 8U * 8U * 4U> pixels_c{};
        FillOpaquePattern(pixels_c, 8U, 8U, 2U);
        auto uploaded_c = host.UploadRgba8Texture(omega::runtime::Rgba8TextureUploadView{
            .width = 8U,
            .height = 8U,
            .pixels = pixels_c,
        });
        if (!uploaded_c)
            return Fail("texture C upload failed", uploaded_c.error());
        if (uploaded_c->pool_identity != uploaded_a->pool_identity ||
            uploaded_c->slot_index != uploaded_a->slot_index ||
            uploaded_c->generation == uploaded_a->generation ||
            uploaded_c->generation == 0U)
            return Fail("texture A slot did not reuse for C with a new generation");

        const omega::app::GpuHostSnapshot after_upload_c = host.Snapshot();
        if (after_upload_c.successful_uploads != 3U ||
            after_upload_c.successful_upload_logical_bytes !=
                pixels_a.size() + pixels_b.size() + pixels_c.size() ||
            after_upload_c.textures.free_slots != 0U ||
            after_upload_c.textures.resident_slots != 2U ||
            after_upload_c.textures.resident_logical_bytes !=
                pixels_b.size() + pixels_c.size())
            return Fail("texture B/C upload counters or residency are inconsistent");

        const std::array second_commands{
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_b,
                .source = full_source,
                .destination = left_half,
                .fit_mode = omega::runtime::RenderTextureFitMode::Contain,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Linear,
            },
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_c,
                .source = omega::runtime::RenderSourceRectQ16{
                    .left = 32'768U,
                    .top = 0U,
                    .right = omega::runtime::kNormalizedRenderExtent,
                    .bottom = 32'768U,
                },
                .destination = right_half,
                .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            },
        };
        auto second_draw_list = omega::runtime::RenderDrawList::Create(second_commands);
        if (!second_draw_list)
            return Fail("texture B/C draw-list creation failed");
        packet.draw_list = *second_draw_list;
        packet.clear_color = second_blit_color;
        if (!RenderUntil(host, packet,
                [](const omega::app::GpuHostSnapshot& snapshot)
                {
                    return snapshot.blit_submissions == 2U &&
                           snapshot.successful_blit_draws == 4U;
                },
                "texture B/C blit"))
            return 1;

        auto released_b = host.ReleaseTexture(*uploaded_b);
        if (!released_b)
            return Fail("texture B release failed", released_b.error());
        auto released_c = host.ReleaseTexture(*uploaded_c);
        if (!released_c)
            return Fail("texture C release failed", released_c.error());
        auto idle = host.WaitForIdle();
        if (!idle)
            return Fail("final GPU idle wait failed", idle.error());

        const omega::app::GpuHostSnapshot final = host.Snapshot();
        if (final.successful_uploads != 3U ||
            final.successful_upload_logical_bytes !=
                pixels_a.size() + pixels_b.size() + pixels_c.size() ||
            final.successful_releases != 3U || final.blit_submissions != 2U ||
            final.successful_blit_draws != 4U ||
            final.clear_submissions != 1U ||
            final.rejected_nondefault_texture_handles != 1U ||
            final.frame_submissions !=
                3U + final.unavailable_swapchain_submissions ||
            !PoolIsEmpty(final.textures))
            return Fail("final counters or zero-residual residency are inconsistent");
        unavailable_submissions = final.unavailable_swapchain_submissions;
    }

    std::cout << "omega_sdl_gpu_texture_smoke: passed driver=" << driver
              << " uploads=3 releases=3 blit_frames=2 blit_draws=4 unavailable="
              << unavailable_submissions << '\n';
    return 0;
}
