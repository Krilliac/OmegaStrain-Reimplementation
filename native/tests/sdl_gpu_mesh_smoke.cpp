#include "sdl_gpu_host.h"
#include "sdl_platform_service.h"

#include "omega/asset/scene_ir.h"
#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_frame_packet.h"
#include "omega/runtime/render_mesh.h"
#include "omega/runtime/render_mesh_draw_list.h"
#include "omega/runtime/render_texture.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app::detail
{
struct SdlGpuHostTestAccess final
{
    [[nodiscard]] static std::expected<
        std::array<runtime::RenderClearColorRgba8, 64U>, std::string>
        ReadbackMeshesForTesting(
            SdlGpuHost& host, const runtime::RenderFramePacket& packet)
    {
        return host.ReadbackMeshesForTesting(packet);
    }
};
} // namespace omega::app::detail

namespace
{
using Clock = std::chrono::steady_clock;

[[nodiscard]] int Fail(const std::string_view message)
{
    std::cerr << "omega_sdl_gpu_mesh_smoke: " << message << '\n';
    return 1;
}

[[nodiscard]] int Fail(const std::string_view message, const std::string& detail)
{
    std::cerr << "omega_sdl_gpu_mesh_smoke: " << message << ": " << detail << '\n';
    return 1;
}

[[nodiscard]] bool MeshPoolIsEmpty(
    const omega::runtime::RenderMeshPoolSnapshot& pool) noexcept
{
    return pool.slot_capacity == 1U && pool.free_slots == 1U &&
           pool.reserved_slots == 0U && pool.resident_slots == 0U &&
           pool.retired_slots == 0U && pool.reserved_positions == 0U &&
           pool.resident_positions == 0U &&
           pool.reserved_triangle_indices == 0U &&
           pool.resident_triangle_indices == 0U &&
           pool.reserved_logical_bytes == 0U &&
           pool.resident_logical_bytes == 0U;
}

[[nodiscard]] bool TexturePoolIsEmpty(
    const omega::runtime::RenderTexturePoolSnapshot& pool) noexcept
{
    return pool.slot_capacity == 1U && pool.free_slots == 1U &&
           pool.reserved_slots == 0U && pool.resident_slots == 0U &&
           pool.retired_slots == 0U && pool.reserved_logical_bytes == 0U &&
           pool.resident_logical_bytes == 0U;
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
            std::cerr << "omega_sdl_gpu_mesh_smoke: " << phase
                      << " render failed: " << rendered.error() << '\n';
            return false;
        }
        ++packet.rendered_frame_index;
        if (reached(host.Snapshot()))
            return true;
    }

    std::cerr << "omega_sdl_gpu_mesh_smoke: " << phase
              << " did not reach a usable swapchain within its bound\n";
    return false;
}
} // namespace

int main()
{
    auto created_platform = omega::app::SdlPlatformService::Create();
    if (!created_platform)
        return Fail("platform creation failed", created_platform.error());
    auto platform = std::move(*created_platform);

    auto created_host = omega::app::SdlGpuHost::Create(platform, false,
        omega::runtime::RenderTexturePoolConfig{
            .slot_capacity = 1U,
            .maximum_resident_logical_bytes = 4U,
        },
        omega::runtime::RenderMeshPoolConfig{
            .slot_capacity = 1U,
            .maximum_resident_positions = 5U,
            .maximum_resident_triangle_indices = 3U,
            .maximum_resident_logical_bytes = 72U,
        });
    if (!created_host)
        return Fail("GPU host creation failed", created_host.error());
    auto host = std::move(*created_host);
    const std::string driver(host.driver_name());
    if (driver.empty())
        return Fail("GPU driver name is empty");

    const omega::app::GpuHostSnapshot initial = host.Snapshot();
    if (!MeshPoolIsEmpty(initial.meshes) || host.MeshSnapshot() != initial.meshes)
        return Fail("initial aggregate mesh residency is inconsistent");

    omega::runtime::RenderFramePacket packet;
    auto empty_readback = omega::app::detail::SdlGpuHostTestAccess::
        ReadbackMeshesForTesting(host, packet);
    if (empty_readback || host.Snapshot() != initial)
        return Fail("empty mesh readback was accepted or mutated production state");

    omega::asset::SceneIR scene;
    scene.render_meshes.push_back(omega::asset::RenderMeshIR{
        .positions = {
            omega::asset::Float3IR{.x = -0.75F, .y = -0.75F, .z = 0.5F},
            omega::asset::Float3IR{.x = -0.75F, .y = -0.75F, .z = 0.5F},
            omega::asset::Float3IR{.x = -0.75F, .y = -0.75F, .z = 0.5F},
            omega::asset::Float3IR{.x = 0.75F, .y = -0.75F, .z = 0.5F},
            omega::asset::Float3IR{.x = 0.0F, .y = 0.75F, .z = 0.5F},
        },
        // The first three positions are degenerate. A visible readback therefore proves that the
        // indexed path selected positions 0, 3, and 4 instead of drawing the first three values.
        .triangle_indices = {0U, 3U, 4U},
    });
    scene.mesh_instances.push_back(omega::asset::SceneMeshInstanceIR{});

    constexpr std::array invalid_indices{0U, 3U, 5U};
    auto invalid_upload = host.UploadRenderMesh(omega::runtime::RenderMeshUploadView{
        .positions = std::span<const omega::asset::Float3IR>(
            scene.render_meshes.front().positions),
        .triangle_indices = invalid_indices,
    });
    if (invalid_upload || host.Snapshot() != initial)
        return Fail("out-of-range synthetic triangle index was accepted or mutated state");

    auto uploaded = host.UploadRenderMesh(scene.render_meshes.front());
    if (!uploaded)
        return Fail("SceneIR-owned mesh upload failed", uploaded.error());
    const omega::app::GpuHostSnapshot after_upload = host.Snapshot();
    if (after_upload.successful_mesh_uploads != 1U ||
        after_upload.successful_mesh_upload_logical_bytes != 72U ||
        after_upload.meshes.free_slots != 0U ||
        after_upload.meshes.resident_slots != 1U ||
        after_upload.meshes.resident_positions != 5U ||
        after_upload.meshes.resident_triangle_indices != 3U ||
        after_upload.meshes.resident_logical_bytes != 72U ||
        after_upload.meshes.reserved_slots != 0U)
    {
        return Fail("mesh upload counters or aggregate residency are inconsistent");
    }

    constexpr omega::runtime::RenderClearColorRgba8 opaque_black{
        .red = 0U,
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
    const omega::runtime::RenderMeshDrawCommand fill_command{
        .mesh = *uploaded,
        .object_to_clip = omega::asset::kIdentityMatrix4x4IR,
        .color = {
            .red = 0U,
            .green = 255U,
            .blue = 0U,
            .alpha = 17U,
        },
        .raster_mode = omega::runtime::RenderMeshRasterMode::Fill,
    };
    const std::array fill_commands{fill_command};
    auto fill_draw_list = omega::runtime::RenderMeshDrawList::Create(fill_commands);
    if (!fill_draw_list)
        return Fail("fill mesh draw-list creation failed");
    packet.clear_color = opaque_black;
    packet.mesh_draw_list = *fill_draw_list;

    const omega::app::GpuHostSnapshot before_readback = host.Snapshot();
    auto readback = omega::app::detail::SdlGpuHostTestAccess::
        ReadbackMeshesForTesting(host, packet);
    if (!readback)
        return Fail("indexed triangle readback failed", readback.error());
    std::size_t green_pixels = 0U;
    std::size_t black_pixels = 0U;
    std::size_t overlay_pixel_index = readback->size();
    for (std::size_t index = 0U; index < readback->size(); ++index)
    {
        const omega::runtime::RenderClearColorRgba8 pixel = (*readback)[index];
        if (pixel == opaque_green)
        {
            if (overlay_pixel_index == readback->size())
                overlay_pixel_index = index;
            ++green_pixels;
        }
        else if (pixel == opaque_black)
            ++black_pixels;
        else
            return Fail("indexed triangle readback contained an unexpected RGBA8 pixel");
    }
    if (green_pixels < 2U || black_pixels == 0U ||
        green_pixels + black_pixels != readback->size())
    {
        return Fail(
            "indexed triangle readback did not leave both overlay and survivor pixels");
    }
    if (host.Snapshot() != before_readback)
        return Fail("indexed triangle readback mutated production counters or residency");

    // Row-major, column-vector transform with unequal X/Y scales and translation into the lower
    // right clip quadrant. If the backend omits its row-to-column storage conversion, the final
    // column becomes a perspective W term and colored pixels escape this quadrant.
    constexpr omega::asset::Matrix4x4IR asymmetric_transform{
        .row_major = {
            0.4F, 0.0F, 0.0F, 0.5F,
            0.0F, 0.3F, 0.0F, -0.4F,
            0.0F, 0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    auto transformed_command = fill_command;
    transformed_command.object_to_clip = asymmetric_transform;
    const std::array transformed_commands{transformed_command};
    auto transformed_draw_list =
        omega::runtime::RenderMeshDrawList::Create(transformed_commands);
    if (!transformed_draw_list)
        return Fail("asymmetric transform draw-list creation failed");
    packet.mesh_draw_list = *transformed_draw_list;
    auto transformed_readback = omega::app::detail::SdlGpuHostTestAccess::
        ReadbackMeshesForTesting(host, packet);
    if (!transformed_readback)
        return Fail("asymmetric transform readback failed", transformed_readback.error());
    std::size_t transformed_green_pixels = 0U;
    for (std::size_t index = 0U; index < transformed_readback->size(); ++index)
    {
        const omega::runtime::RenderClearColorRgba8 pixel =
            (*transformed_readback)[index];
        if (pixel == opaque_green)
        {
            ++transformed_green_pixels;
            const std::size_t row = index / 8U;
            const std::size_t column = index % 8U;
            if (row < 4U || column < 4U)
            {
                return Fail(
                    "asymmetric transform escaped its expected lower-right quadrant");
            }
        }
        else if (pixel != opaque_black)
        {
            return Fail("asymmetric transform readback contained an unexpected RGBA8 pixel");
        }
    }
    if (transformed_green_pixels == 0U)
        return Fail("asymmetric transform readback produced no colored pixels");
    if (host.Snapshot() != before_readback)
        return Fail("asymmetric transform readback mutated production counters or residency");

    packet.mesh_draw_list = *fill_draw_list;

    constexpr omega::runtime::RenderClearColorRgba8 opaque_blue{
        .red = 0U,
        .green = 0U,
        .blue = 255U,
        .alpha = 255U,
    };
    constexpr std::array<std::byte, 4U> overlay_pixels{
        std::byte{0U}, std::byte{0U}, std::byte{255U}, std::byte{255U}};
    auto overlay_texture = host.UploadRgba8Texture(
        omega::runtime::Rgba8TextureUploadView{
            .width = 1U,
            .height = 1U,
            .pixels = overlay_pixels,
        });
    if (!overlay_texture)
        return Fail("mesh overlay texture upload failed", overlay_texture.error());

    constexpr std::uint32_t kReadbackExtent = 8U;
    static_assert(omega::runtime::kNormalizedRenderExtent % kReadbackExtent == 0U);
    constexpr std::uint32_t kPixelSpan =
        omega::runtime::kNormalizedRenderExtent / kReadbackExtent;
    const std::uint32_t overlay_row =
        static_cast<std::uint32_t>(overlay_pixel_index / kReadbackExtent);
    const std::uint32_t overlay_column =
        static_cast<std::uint32_t>(overlay_pixel_index % kReadbackExtent);
    constexpr omega::runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    const std::array overlay_commands{
        omega::runtime::RenderTextureBlitCommand{
            .texture = *overlay_texture,
            .source = full_source,
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = overlay_column * kPixelSpan,
                .top = overlay_row * kPixelSpan,
                .right = (overlay_column + 1U) * kPixelSpan,
                .bottom = (overlay_row + 1U) * kPixelSpan,
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        },
    };
    auto overlay_draw_list =
        omega::runtime::RenderDrawList::Create(overlay_commands);
    if (!overlay_draw_list)
        return Fail("mesh overlay draw-list creation failed");
    packet.draw_list = *overlay_draw_list;
    const omega::app::GpuHostSnapshot before_overlay_readback = host.Snapshot();
    auto overlay_readback = omega::app::detail::SdlGpuHostTestAccess::
        ReadbackMeshesForTesting(host, packet);
    if (!overlay_readback)
        return Fail("mesh plus texture overlay readback failed", overlay_readback.error());
    std::size_t surviving_green_pixels = 0U;
    for (std::size_t index = 0U; index < overlay_readback->size(); ++index)
    {
        if (index == overlay_pixel_index)
        {
            if ((*overlay_readback)[index] != opaque_blue)
                return Fail("texture overlay did not replace its selected mesh pixel");
            continue;
        }
        if ((*overlay_readback)[index] != (*readback)[index])
            return Fail("texture overlay changed a pixel outside its exact destination");
        if ((*overlay_readback)[index] == opaque_green)
            ++surviving_green_pixels;
    }
    if (surviving_green_pixels != green_pixels - 1U)
        return Fail("mesh pixels did not survive around the ordered texture overlay");
    if (host.Snapshot() != before_overlay_readback)
        return Fail("mesh overlay readback mutated production counters or residency");
    packet.draw_list = {};
    auto released_overlay = host.ReleaseTexture(*overlay_texture);
    if (!released_overlay)
        return Fail("mesh overlay texture release failed", released_overlay.error());

    if (!RenderUntil(host, packet,
            [](const omega::app::GpuHostSnapshot& snapshot)
            {
                return snapshot.mesh_submissions == 1U &&
                       snapshot.successful_mesh_draws == 1U;
            },
            "fill mesh"))
    {
        return 1;
    }

    const omega::runtime::RenderMeshDrawCommand wireframe_command{
        .mesh = *uploaded,
        .object_to_clip = omega::asset::kIdentityMatrix4x4IR,
        .color = {
            .red = 0U,
            .green = 255U,
            .blue = 255U,
            .alpha = 255U,
        },
        .raster_mode = omega::runtime::RenderMeshRasterMode::Wireframe,
    };
    const std::array wireframe_commands{wireframe_command};
    auto wireframe_draw_list =
        omega::runtime::RenderMeshDrawList::Create(wireframe_commands);
    if (!wireframe_draw_list)
        return Fail("wireframe mesh draw-list creation failed");
    packet.mesh_draw_list = *wireframe_draw_list;
    if (!RenderUntil(host, packet,
            [](const omega::app::GpuHostSnapshot& snapshot)
            {
                return snapshot.mesh_submissions == 2U &&
                       snapshot.successful_mesh_draws == 2U;
            },
            "wireframe mesh"))
    {
        return 1;
    }

    auto released = host.ReleaseRenderMesh(*uploaded);
    if (!released)
        return Fail("mesh release failed", released.error());
    const omega::app::GpuHostSnapshot after_release = host.Snapshot();
    if (after_release.successful_mesh_releases != 1U ||
        !MeshPoolIsEmpty(after_release.meshes))
    {
        return Fail("mesh release did not restore zero-residual residency");
    }

    auto stale_render = host.RenderFrame(packet);
    omega::app::GpuHostSnapshot expected_stale = after_release;
    ++expected_stale.rejected_nondefault_mesh_handles;
    if (stale_render || host.Snapshot() != expected_stale)
        return Fail("released mesh generation was accepted or mutated unrelated state");

    auto reused = host.UploadRenderMesh(scene.render_meshes.front());
    if (!reused)
        return Fail("mesh slot reuse upload failed", reused.error());
    if (reused->pool_identity != uploaded->pool_identity ||
        reused->slot_index != uploaded->slot_index ||
        reused->generation == uploaded->generation || reused->generation == 0U)
    {
        return Fail("mesh slot was not reused with a fresh generation");
    }
    auto released_reused = host.ReleaseRenderMesh(*reused);
    if (!released_reused)
        return Fail("reused mesh release failed", released_reused.error());
    auto idle = host.WaitForIdle();
    if (!idle)
        return Fail("final GPU idle wait failed", idle.error());

    const omega::app::GpuHostSnapshot final = host.Snapshot();
    if (final.successful_mesh_uploads != 2U ||
        final.successful_mesh_upload_logical_bytes != 144U ||
        final.successful_mesh_releases != 2U || final.mesh_submissions != 2U ||
        final.successful_mesh_draws != 2U ||
        final.rejected_nondefault_mesh_handles != 1U ||
        final.frame_submissions != 2U + final.unavailable_swapchain_submissions ||
        final.successful_uploads != 1U ||
        final.successful_upload_logical_bytes != 4U ||
        final.successful_releases != 1U || !TexturePoolIsEmpty(final.textures) ||
        final.blit_submissions != 0U || final.successful_blit_draws != 0U ||
        final.clear_submissions != 0U || !MeshPoolIsEmpty(final.meshes))
    {
        return Fail("final mesh counters or zero-residual residency are inconsistent");
    }

    std::cout << "omega_sdl_gpu_mesh_smoke: passed driver=" << driver
              << " uploads=2 releases=2 mesh_frames=2 mesh_draws=2 colored_pixels="
              << green_pixels << " transformed_pixels=" << transformed_green_pixels
              << " surviving_overlay_pixels=" << surviving_green_pixels
              << " unavailable="
              << final.unavailable_swapchain_submissions << '\n';
    return 0;
}
