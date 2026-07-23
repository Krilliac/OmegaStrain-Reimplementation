#include "sdl_gpu_host.h"
#include "screenshot_capture.h"
#include "sdl_platform_service.h"

#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_frame_packet.h"
#include "omega/runtime/render_texture.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app::detail
{
struct SdlGpuHostTestAccess final
{
    [[nodiscard]] static std::expected<
        std::array<runtime::RenderClearColorRgba8, 4U>, std::string>
        ReadbackClearForTesting(
            SdlGpuHost& host, const runtime::RenderFramePacket& packet)
    {
        return host.ReadbackClearForTesting(packet);
    }

    [[nodiscard]] static std::expected<
        std::array<runtime::RenderClearColorRgba8, 16U>, std::string>
        ReadbackBlitsForTesting(
            SdlGpuHost& host, const runtime::RenderFramePacket& packet)
    {
        return host.ReadbackBlitsForTesting(packet);
    }
};
} // namespace omega::app::detail

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

        const omega::app::GpuHostSnapshot before_clear_readbacks = host.Snapshot();
        omega::runtime::RenderFramePacket packet;
        constexpr std::array endpoint_clear_colors{
            omega::runtime::RenderClearColorRgba8{
                .red = 0U,
                .green = 255U,
                .blue = 0U,
                .alpha = 255U,
            },
            omega::runtime::RenderClearColorRgba8{
                .red = 255U,
                .green = 0U,
                .blue = 255U,
                .alpha = 0U,
            },
        };
        for (const omega::runtime::RenderClearColorRgba8 clear_color :
            endpoint_clear_colors)
        {
            packet.clear_color = clear_color;
            auto readback = omega::app::detail::SdlGpuHostTestAccess::
                ReadbackClearForTesting(host, packet);
            if (!readback)
                return Fail("clear readback failed", readback.error());
            for (const omega::runtime::RenderClearColorRgba8 pixel : *readback)
            {
                if (pixel != clear_color)
                    return Fail("clear readback did not preserve exact RGBA8 endpoints");
            }
            if (host.Snapshot() != before_clear_readbacks)
                return Fail("clear readback mutated production host state");
        }
        packet.clear_color = endpoint_clear_colors.front();
        auto screenshot_readback = host.CaptureFrameRgba8(packet);
        if (!screenshot_readback ||
            screenshot_readback->size() != omega::app::kScreenshotPixelCount ||
            !std::ranges::all_of(*screenshot_readback,
                [expected = packet.clear_color](
                    const omega::runtime::RenderClearColorRgba8 pixel) {
                    return pixel == expected;
                }))
        {
            return Fail("fixed screenshot readback did not preserve the clear-only frame");
        }
        if (host.Snapshot() != before_clear_readbacks)
            return Fail("fixed screenshot readback mutated production host state");

        constexpr omega::runtime::RenderTextureBlitCommand readback_rejection_command{
            .texture = omega::runtime::RenderTextureHandle{
                .pool_identity = 1U,
                .generation = 1U,
                .slot_index = 0U,
            },
            .source = omega::runtime::RenderSourceRectQ16{
                .left = 0U,
                .top = 0U,
                .right = omega::runtime::kNormalizedRenderExtent,
                .bottom = omega::runtime::kNormalizedRenderExtent,
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = 0U,
                .top = 0U,
                .right = omega::runtime::kNormalizedRenderExtent,
                .bottom = omega::runtime::kNormalizedRenderExtent,
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Contain,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
        const std::array readback_rejection_commands{readback_rejection_command};
        auto readback_rejection_list =
            omega::runtime::RenderDrawList::Create(readback_rejection_commands);
        if (!readback_rejection_list)
            return Fail("clear readback rejection draw-list creation failed");
        packet.draw_list = *readback_rejection_list;
        auto rejected_readback = omega::app::detail::SdlGpuHostTestAccess::
            ReadbackClearForTesting(host, packet);
        if (rejected_readback ||
            rejected_readback.error() != "clear readback requires an empty draw list")
        {
            return Fail("clear readback accepted a nonempty draw list or returned the wrong error");
        }
        if (host.Snapshot() != before_clear_readbacks)
            return Fail("rejected clear readback mutated production host state");

        packet = omega::runtime::RenderFramePacket{};
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

        packet = omega::runtime::RenderFramePacket{};
        const omega::app::GpuHostSnapshot before_empty_blit_readback = host.Snapshot();
        auto empty_blit_readback = omega::app::detail::SdlGpuHostTestAccess::
            ReadbackBlitsForTesting(host, packet);
        if (empty_blit_readback ||
            empty_blit_readback.error() != "blit readback requires a nonempty draw list")
        {
            return Fail("blit readback accepted an empty draw list or returned the wrong error");
        }
        if (host.Snapshot() != before_empty_blit_readback)
            return Fail("rejected empty blit readback mutated production host state");

        constexpr std::array<std::byte, 2U * 2U * 4U> probe_pixels{
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0x00U),
            static_cast<std::byte>(0x00U), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0x00U), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0x00U), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0x00U), static_cast<std::byte>(0x00U),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
        };
        auto uploaded_probe = host.UploadRgba8Texture(omega::runtime::Rgba8TextureUploadView{
            .width = 2U,
            .height = 2U,
            .pixels = probe_pixels,
        });
        if (!uploaded_probe)
            return Fail("blit readback probe upload failed", uploaded_probe.error());

        const omega::app::GpuHostSnapshot before_blit_readback = host.Snapshot();
        constexpr std::uint32_t probe_normalized_extent =
            omega::runtime::kNormalizedRenderExtent;
        constexpr std::uint32_t probe_half_extent = probe_normalized_extent / 2U;
        constexpr std::uint32_t probe_quarter_extent = probe_normalized_extent / 4U;
        constexpr std::uint32_t probe_three_quarter_extent =
            probe_quarter_extent * 3U;
        const std::array probe_commands{
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_probe,
                .source = omega::runtime::RenderSourceRectQ16{
                    .left = 0U,
                    .top = 0U,
                    .right = probe_normalized_extent,
                    .bottom = probe_half_extent,
                },
                .destination = omega::runtime::RenderTargetRectQ16{
                    .left = 0U,
                    .top = 0U,
                    .right = probe_normalized_extent,
                    .bottom = probe_normalized_extent,
                },
                .fit_mode = omega::runtime::RenderTextureFitMode::Contain,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            },
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_probe,
                .source = omega::runtime::RenderSourceRectQ16{
                    .left = 0U,
                    .top = probe_half_extent,
                    .right = probe_half_extent,
                    .bottom = probe_normalized_extent,
                },
                .destination = omega::runtime::RenderTargetRectQ16{
                    .left = probe_half_extent,
                    .top = probe_quarter_extent,
                    .right = probe_three_quarter_extent,
                    .bottom = probe_three_quarter_extent,
                },
                .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            },
        };
        auto probe_draw_list = omega::runtime::RenderDrawList::Create(probe_commands);
        if (!probe_draw_list)
            return Fail("blit readback probe draw-list creation failed");

        constexpr omega::runtime::RenderClearColorRgba8 opaque_black{
            .red = 0U,
            .green = 0U,
            .blue = 0U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 opaque_red{
            .red = 255U,
            .green = 0U,
            .blue = 0U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 opaque_green{
            .red = 0U,
            .green = 255U,
            .blue = 0U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 opaque_blue{
            .red = 0U,
            .green = 0U,
            .blue = 255U,
            .alpha = 255U,
        };
        constexpr std::array expected_probe_readback{
            opaque_black, opaque_black, opaque_black, opaque_black,
            opaque_red, opaque_red, opaque_blue, opaque_green,
            opaque_red, opaque_red, opaque_blue, opaque_green,
            opaque_black, opaque_black, opaque_black, opaque_black,
        };
        packet.clear_color = opaque_black;
        packet.draw_list = *probe_draw_list;
        auto probe_readback = omega::app::detail::SdlGpuHostTestAccess::
            ReadbackBlitsForTesting(host, packet);
        if (!probe_readback)
            return Fail("blit readback probe failed", probe_readback.error());
        if (*probe_readback != expected_probe_readback)
            return Fail("blit readback probe did not match the exact 4x4 RGBA8 grid");
        if (host.Snapshot() != before_blit_readback)
            return Fail("blit readback probe mutated production host state");
        auto screenshot_blit_readback = host.CaptureFrameRgba8(packet);
        if (!screenshot_blit_readback ||
            screenshot_blit_readback->size() !=
                omega::app::kScreenshotPixelCount)
        {
            return Fail("fixed screenshot blit readback failed");
        }
        const auto screenshot_pixel = [&screenshot_blit_readback](
                                          const std::uint32_t x,
                                          const std::uint32_t y) {
            return (*screenshot_blit_readback)[
                static_cast<std::size_t>(y) * omega::app::kScreenshotWidth + x];
        };
        if (screenshot_pixel(0U, 0U) != opaque_black ||
            screenshot_pixel(80U, 112U) != opaque_red ||
            screenshot_pixel(560U, 112U) != opaque_green ||
            screenshot_pixel(400U, 200U) != opaque_blue ||
            screenshot_pixel(639U, 447U) != opaque_black)
        {
            return Fail("fixed screenshot blit readback sampled the wrong composed pixels");
        }
        if (host.Snapshot() != before_blit_readback)
            return Fail("fixed screenshot blit readback mutated production host state");

        constexpr std::array<std::byte, 2U * 2U * 4U> updated_probe_pixels{
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0x00U), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0x00U), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0x00U),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
            static_cast<std::byte>(0xFFU), static_cast<std::byte>(0xFFU),
        };
        const omega::app::GpuHostSnapshot before_probe_update = host.Snapshot();
        auto updated_probe = host.UpdateRgba8Texture(*uploaded_probe,
            omega::runtime::Rgba8TextureUploadView{
                .width = 2U,
                .height = 2U,
                .pixels = updated_probe_pixels,
            });
        if (!updated_probe)
            return Fail("resident probe update failed", updated_probe.error());
        const omega::app::GpuHostSnapshot after_probe_update = host.Snapshot();
        if (after_probe_update.textures != before_probe_update.textures ||
            after_probe_update.successful_uploads != before_probe_update.successful_uploads ||
            after_probe_update.successful_updates != 1U ||
            after_probe_update.successful_update_logical_bytes != updated_probe_pixels.size())
        {
            return Fail("resident probe update changed identity/residency or counters incorrectly");
        }

        auto mismatched_probe_update = host.UpdateRgba8Texture(*uploaded_probe,
            omega::runtime::Rgba8TextureUploadView{
                .width = 1U,
                .height = 4U,
                .pixels = updated_probe_pixels,
            });
        if (mismatched_probe_update || host.Snapshot() != after_probe_update)
            return Fail("mismatched resident probe update was accepted or mutated state");

        constexpr omega::runtime::RenderClearColorRgba8 opaque_yellow{
            .red = 255U,
            .green = 255U,
            .blue = 0U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 opaque_cyan{
            .red = 0U,
            .green = 255U,
            .blue = 255U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 opaque_magenta{
            .red = 255U,
            .green = 0U,
            .blue = 255U,
            .alpha = 255U,
        };
        constexpr std::array expected_updated_probe_readback{
            opaque_black, opaque_black, opaque_black, opaque_black,
            opaque_yellow, opaque_yellow, opaque_magenta, opaque_cyan,
            opaque_yellow, opaque_yellow, opaque_magenta, opaque_cyan,
            opaque_black, opaque_black, opaque_black, opaque_black,
        };
        auto updated_probe_readback = omega::app::detail::SdlGpuHostTestAccess::
            ReadbackBlitsForTesting(host, packet);
        if (!updated_probe_readback)
            return Fail("updated resident probe readback failed", updated_probe_readback.error());
        if (*updated_probe_readback != expected_updated_probe_readback)
            return Fail("resident probe update did not replace the exact RGBA8 pixels");
        if (host.Snapshot() != after_probe_update)
            return Fail("updated resident probe readback mutated production host state");

        auto released_probe = host.ReleaseTexture(*uploaded_probe);
        if (!released_probe)
            return Fail("blit readback probe release failed", released_probe.error());
        const omega::app::GpuHostSnapshot after_probe_release = host.Snapshot();
        if (after_probe_release.successful_uploads != 1U ||
            after_probe_release.successful_upload_logical_bytes != probe_pixels.size() ||
            after_probe_release.successful_updates != 1U ||
            after_probe_release.successful_update_logical_bytes != updated_probe_pixels.size() ||
            after_probe_release.successful_releases != 1U ||
            !PoolIsEmpty(after_probe_release.textures))
            return Fail("blit readback probe release did not restore empty residency");

        auto stale_probe_update = host.UpdateRgba8Texture(*uploaded_probe,
            omega::runtime::Rgba8TextureUploadView{
                .width = 2U,
                .height = 2U,
                .pixels = updated_probe_pixels,
            });
        omega::app::GpuHostSnapshot expected_stale_probe_update = after_probe_release;
        ++expected_stale_probe_update.rejected_nondefault_texture_handles;
        if (stale_probe_update || host.Snapshot() != expected_stale_probe_update)
            return Fail("stale resident probe update was accepted or mutated unrelated state");

        packet = omega::runtime::RenderFramePacket{};
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
        if (after_upload_b.successful_uploads != 3U ||
            after_upload_b.successful_upload_logical_bytes !=
                probe_pixels.size() + pixels_a.size() + pixels_b.size() ||
            after_upload_b.textures.slot_capacity != 2U ||
            after_upload_b.textures.free_slots != 0U ||
            after_upload_b.textures.resident_slots != 2U ||
            after_upload_b.textures.resident_logical_bytes !=
                pixels_a.size() + pixels_b.size() ||
            after_upload_b.textures.reserved_slots != 0U ||
            after_upload_b.textures.retired_slots != 0U)
            return Fail("texture A/B upload counters or residency are inconsistent");

        const std::array distinct_resident_commands{
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_a,
                .source = omega::runtime::RenderSourceRectQ16{
                    .left = 0U,
                    .top = 0U,
                    .right = 8'192U,
                    .bottom = 8'192U,
                },
                .destination = omega::runtime::RenderTargetRectQ16{
                    .left = 0U,
                    .top = 0U,
                    .right = omega::runtime::kNormalizedRenderExtent,
                    .bottom = omega::runtime::kNormalizedRenderExtent,
                },
                .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            },
            omega::runtime::RenderTextureBlitCommand{
                .texture = *uploaded_b,
                .source = omega::runtime::RenderSourceRectQ16{
                    .left = 0U,
                    .top = 0U,
                    .right = 16'384U,
                    .bottom = 8'192U,
                },
                .destination = omega::runtime::RenderTargetRectQ16{
                    .left = 16'384U,
                    .top = 16'384U,
                    .right = 49'152U,
                    .bottom = 49'152U,
                },
                .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
                .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
            },
        };
        auto distinct_resident_draw_list =
            omega::runtime::RenderDrawList::Create(distinct_resident_commands);
        if (!distinct_resident_draw_list)
            return Fail("distinct-resident readback draw-list creation failed");

        omega::runtime::RenderFramePacket distinct_resident_packet;
        distinct_resident_packet.clear_color = opaque_black;
        distinct_resident_packet.draw_list = *distinct_resident_draw_list;

        constexpr omega::runtime::RenderClearColorRgba8 distinct_a_color{
            .red = 32U,
            .green = 192U,
            .blue = 224U,
            .alpha = 255U,
        };
        constexpr omega::runtime::RenderClearColorRgba8 distinct_b_color{
            .red = 224U,
            .green = 80U,
            .blue = 32U,
            .alpha = 255U,
        };
        constexpr std::array expected_distinct_resident_readback{
            distinct_a_color, distinct_a_color, distinct_a_color, distinct_a_color,
            distinct_a_color, distinct_b_color, distinct_b_color, distinct_a_color,
            distinct_a_color, distinct_b_color, distinct_b_color, distinct_a_color,
            distinct_a_color, distinct_a_color, distinct_a_color, distinct_a_color,
        };
        const omega::app::GpuHostSnapshot before_distinct_resident_readback = host.Snapshot();
        auto distinct_resident_readback = omega::app::detail::SdlGpuHostTestAccess::
            ReadbackBlitsForTesting(host, distinct_resident_packet);
        if (!distinct_resident_readback)
        {
            return Fail(
                "distinct-resident blit readback failed", distinct_resident_readback.error());
        }
        if (*distinct_resident_readback != expected_distinct_resident_readback)
            return Fail("distinct-resident blit readback did not match the exact RGBA8 grid");
        if (host.Snapshot() != before_distinct_resident_readback)
            return Fail("distinct-resident blit readback mutated production host state");

        packet = omega::runtime::RenderFramePacket{};

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
        if (after_release_a.successful_releases != 2U ||
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
        if (after_upload_c.successful_uploads != 4U ||
            after_upload_c.successful_upload_logical_bytes !=
                probe_pixels.size() + pixels_a.size() + pixels_b.size() + pixels_c.size() ||
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
        if (final.successful_uploads != 4U ||
            final.successful_upload_logical_bytes !=
                probe_pixels.size() + pixels_a.size() + pixels_b.size() + pixels_c.size() ||
            final.successful_updates != 1U ||
            final.successful_update_logical_bytes != updated_probe_pixels.size() ||
            final.successful_releases != 4U || final.blit_submissions != 2U ||
            final.successful_blit_draws != 4U ||
            final.clear_submissions != 1U ||
            final.rejected_nondefault_texture_handles != 2U ||
            final.frame_submissions !=
                3U + final.unavailable_swapchain_submissions ||
            !PoolIsEmpty(final.textures))
            return Fail("final counters or zero-residual residency are inconsistent");
        unavailable_submissions = final.unavailable_swapchain_submissions;
    }

    std::cout << "omega_sdl_gpu_texture_smoke: passed driver=" << driver
              << " uploads=4 updates=1 releases=4 blit_frames=2 blit_draws=4 unavailable="
              << unavailable_submissions << '\n';
    return 0;
}
