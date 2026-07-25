#include "sdl_gpu_host.h"
#include "sdl_platform_service.h"

#include "omega/asset/scene_ir.h"
#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_frame_packet.h"
#include "omega/runtime/render_mesh.h"
#include "omega/runtime/render_mesh_draw_list.h"
#include "omega/runtime/render_texture.h"

#include <SDL3/SDL.h>

#include <algorithm>
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
           pool.resident_triangle_indices == 0U && pool.reserved_uvs == 0U &&
           pool.resident_uvs == 0U && pool.reserved_logical_bytes == 0U &&
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

// Exercises multiple in-flight color uploads without an intermediate idle
// barrier. Alternating lists makes stale/corrupt transfer reuse observable to
// backend validation while keeping the production counters deterministic.
[[nodiscard]] bool RenderAlternatingMeshBurst(omega::app::SdlGpuHost& host,
    omega::runtime::RenderFramePacket& packet,
    const omega::runtime::RenderMeshDrawList& first,
    const omega::runtime::RenderMeshDrawList& second)
{
    constexpr std::uint64_t required_mesh_frames = 64U;
    constexpr std::uint32_t maximum_attempts = 512U;
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    const omega::app::GpuHostSnapshot before = host.Snapshot();
    std::uint64_t observed_mesh_frames = 0U;

    for (std::uint32_t attempt = 0U;
         attempt < maximum_attempts && Clock::now() < deadline &&
         observed_mesh_frames < required_mesh_frames;
         ++attempt)
    {
        packet.mesh_draw_list =
            (observed_mesh_frames & 1U) == 0U ? first : second;
        SDL_PumpEvents();
        auto rendered = host.RenderFrame(packet);
        if (!rendered)
        {
            std::cerr << "omega_sdl_gpu_mesh_smoke: mesh transfer reuse burst failed: "
                      << rendered.error() << '\n';
            return false;
        }
        ++packet.rendered_frame_index;
        const omega::app::GpuHostSnapshot current = host.Snapshot();
        if (current.mesh_submissions < before.mesh_submissions ||
            current.mesh_submissions - before.mesh_submissions >
                required_mesh_frames)
        {
            std::cerr << "omega_sdl_gpu_mesh_smoke: mesh transfer reuse counters escaped their bound\n";
            return false;
        }
        observed_mesh_frames =
            current.mesh_submissions - before.mesh_submissions;
    }

    const omega::app::GpuHostSnapshot after = host.Snapshot();
    if (observed_mesh_frames != required_mesh_frames ||
        after.successful_mesh_draws !=
            before.successful_mesh_draws + required_mesh_frames)
    {
        std::cerr << "omega_sdl_gpu_mesh_smoke: mesh transfer reuse burst did not submit its exact draw count\n";
        return false;
    }
    return true;
}

// Which texel of the 2x2 checker (each quadrant a solid 4x4 block of an 8x8 texture) a readback
// pixel came from. The textured pixel shader multiplies the sampled albedo by a uniform key-light
// term, so absolute levels are not fixed; the CHANNEL STRUCTURE identifies the texel that was read.
enum class CheckerSwatch
{
    Red,
    Green,
    Blue,
    White,
    Other,
};

[[nodiscard]] std::string_view CheckerSwatchName(const CheckerSwatch swatch) noexcept
{
    switch (swatch)
    {
    case CheckerSwatch::Red:
        return "red";
    case CheckerSwatch::Green:
        return "green";
    case CheckerSwatch::Blue:
        return "blue";
    case CheckerSwatch::White:
        return "white";
    case CheckerSwatch::Other:
        return "other";
    }
    return "other";
}

[[nodiscard]] CheckerSwatch ClassifyCheckerPixel(
    const omega::runtime::RenderClearColorRgba8 pixel) noexcept
{
    if (pixel.alpha != 255U)
        return CheckerSwatch::Other;
    const int red = static_cast<int>(pixel.red);
    const int green = static_cast<int>(pixel.green);
    const int blue = static_cast<int>(pixel.blue);
    const int highest = std::max({red, green, blue});
    const int lowest = std::min({red, green, blue});
    // The clear color is opaque black, so anything this dark is an uncovered pixel, not a texel.
    if (highest < 48)
        return CheckerSwatch::Other;
    if (highest - lowest <= 24)
        return CheckerSwatch::White;
    const int dominance = 64;
    if (red == highest && red - std::max(green, blue) >= dominance)
        return CheckerSwatch::Red;
    if (green == highest && green - std::max(red, blue) >= dominance)
        return CheckerSwatch::Green;
    if (blue == highest && blue - std::max(red, green) >= dominance)
        return CheckerSwatch::Blue;
    return CheckerSwatch::Other;
}

// The UV proof.
//
// A screen-filling quad carrying KNOWN per-vertex UVs (0,0)..(1,1) is drawn against an 8x8 texture
// whose four 4x4 quadrants are four distinguishable colors, and the result is read back at 8x8.
// That makes each readback pixel center land exactly on one texel center, so the readback must come
// back as four uniform 4x4 blocks holding all four colors -- which can only happen if the UVs
// actually addressed the texture. It is deliberately NOT satisfied by "any UVs": with degenerate
// (all-zero) UVs every fragment samples the same point, every block classifies the same, and the
// permutation assertion below fails. This is the case that separates real UVs from any UVs.
//
// The host lives inside this function so only one GPU device exists at a time; it is called before
// the baseline case creates its own.
[[nodiscard]] int RunUvCheckerCase(const omega::app::SdlPlatformService& platform)
{
    constexpr std::uint32_t kCheckerExtent = 8U;
    constexpr std::size_t kCheckerBytes =
        static_cast<std::size_t>(kCheckerExtent) * kCheckerExtent * 4U;
    constexpr std::uint64_t kQuadPositions = 4U;
    constexpr std::uint64_t kQuadIndices = 6U;
    // positions*12 + indices*4 + uvs*8.
    constexpr std::uint64_t kQuadLogicalBytes = 4U * 12U + 6U * 4U + 4U * 8U;
    static_assert(kQuadLogicalBytes == 104U);

    auto created_host = omega::app::SdlGpuHost::Create(platform, false,
        omega::runtime::RenderTexturePoolConfig{
            .slot_capacity = 1U,
            .maximum_resident_logical_bytes = kCheckerBytes,
        },
        omega::runtime::RenderMeshPoolConfig{
            .slot_capacity = 1U,
            .maximum_resident_positions = kQuadPositions,
            .maximum_resident_triangle_indices = kQuadIndices,
            .maximum_resident_logical_bytes = kQuadLogicalBytes,
        });
    if (!created_host)
        return Fail("uv checker host creation failed", created_host.error());
    auto host = std::move(*created_host);
    const std::string driver(host.driver_name());
    if (driver != "direct3d12")
    {
        // The textured pipeline is DXIL-only (CreateTexturedMeshShader) and the host fail-softs to
        // the flat pipeline elsewhere, which cannot sample anything. Skipping loudly beats
        // asserting a property this backend structurally cannot provide.
        std::cout << "omega_sdl_gpu_mesh_smoke: UV CHECKER CASE SKIPPED (driver=" << driver
                  << " has no DXIL textured pipeline; the UV attribute is unverified here)\n";
        return 0;
    }

    // Four solid 4x4 quadrants: top-left red, top-right green, bottom-left blue, bottom-right
    // white. Solid blocks (rather than single texels) keep every readback pixel well inside one
    // color, so linear filtering cannot smear one quadrant's answer into another's.
    std::array<std::byte, kCheckerBytes> checker_pixels{};
    for (std::uint32_t y = 0U; y < kCheckerExtent; ++y)
    {
        for (std::uint32_t x = 0U; x < kCheckerExtent; ++x)
        {
            const bool right = x >= kCheckerExtent / 2U;
            const bool lower = y >= kCheckerExtent / 2U;
            const std::uint8_t red = (!right && !lower) || (right && lower) ? 255U : 0U;
            const std::uint8_t green = (right && !lower) || (right && lower) ? 255U : 0U;
            const std::uint8_t blue = (!right && lower) || (right && lower) ? 255U : 0U;
            const std::size_t offset =
                (static_cast<std::size_t>(y) * kCheckerExtent + x) * 4U;
            checker_pixels[offset + 0U] = std::byte{red};
            checker_pixels[offset + 1U] = std::byte{green};
            checker_pixels[offset + 2U] = std::byte{blue};
            checker_pixels[offset + 3U] = std::byte{255U};
        }
    }
    auto checker_texture =
        host.UploadRgba8Texture(omega::runtime::Rgba8TextureUploadView{
            .width = kCheckerExtent,
            .height = kCheckerExtent,
            .pixels = checker_pixels,
        });
    if (!checker_texture)
        return Fail("uv checker texture upload failed", checker_texture.error());

    // Screen-filling quad. u = (x + 1) / 2 and v = (y + 1) / 2, so an 8x8 readback samples each of
    // the eight texel centers along each axis exactly once.
    constexpr std::array<omega::asset::Float3IR, 4U> quad_positions{
        omega::asset::Float3IR{.x = -1.0F, .y = -1.0F, .z = 0.5F},
        omega::asset::Float3IR{.x = 1.0F, .y = -1.0F, .z = 0.5F},
        omega::asset::Float3IR{.x = 1.0F, .y = 1.0F, .z = 0.5F},
        omega::asset::Float3IR{.x = -1.0F, .y = 1.0F, .z = 0.5F},
    };
    constexpr std::array<std::array<float, 2U>, 4U> quad_uvs{
        std::array<float, 2U>{{0.0F, 0.0F}},
        std::array<float, 2U>{{1.0F, 0.0F}},
        std::array<float, 2U>{{1.0F, 1.0F}},
        std::array<float, 2U>{{0.0F, 1.0F}},
    };
    constexpr std::array<std::uint32_t, 6U> quad_indices{0U, 1U, 2U, 0U, 2U, 3U};

    const std::array<std::array<float, 2U>, 2U> short_uvs{
        std::array<float, 2U>{{0.0F, 0.0F}},
        std::array<float, 2U>{{1.0F, 0.0F}},
    };
    auto mismatched = host.UploadRenderMesh(omega::runtime::RenderMeshUploadView{
        .positions = quad_positions,
        .triangle_indices = quad_indices,
        .uvs = short_uvs,
    });
    if (mismatched)
        return Fail("a uv count that does not match the position count was accepted");

    auto quad = host.UploadRenderMesh(omega::runtime::RenderMeshUploadView{
        .positions = quad_positions,
        .triangle_indices = quad_indices,
        .uvs = quad_uvs,
    });
    if (!quad)
        return Fail("uv quad upload failed", quad.error());
    const omega::app::GpuHostSnapshot after_quad = host.Snapshot();
    if (after_quad.meshes.resident_uvs != kQuadPositions ||
        after_quad.meshes.resident_logical_bytes != kQuadLogicalBytes ||
        after_quad.successful_mesh_upload_logical_bytes != kQuadLogicalBytes)
    {
        return Fail("uv quad residency did not charge the uv payload exactly");
    }

    const std::array quad_commands{
        omega::runtime::RenderMeshDrawCommand{
            .mesh = *quad,
            .object_to_clip = omega::asset::kIdentityMatrix4x4IR,
            // A magenta flat color that is NOT one of the four checker swatches: if the textured
            // pipeline were bypassed, every pixel would classify as Other and the case fails.
            .color = {
                .red = 255U,
                .green = 0U,
                .blue = 255U,
                .alpha = 255U,
            },
            .raster_mode = omega::runtime::RenderMeshRasterMode::Fill,
            .texture = *checker_texture,
        },
    };
    auto quad_draw_list = omega::runtime::RenderMeshDrawList::Create(quad_commands);
    if (!quad_draw_list)
        return Fail("uv quad draw-list creation failed");
    omega::runtime::RenderFramePacket packet;
    packet.clear_color = omega::runtime::RenderClearColorRgba8{
        .red = 0U,
        .green = 0U,
        .blue = 0U,
        .alpha = 255U,
    };
    packet.mesh_draw_list = *quad_draw_list;
    auto readback =
        omega::app::detail::SdlGpuHostTestAccess::ReadbackMeshesForTesting(host, packet);
    if (!readback)
        return Fail("uv checker readback failed", readback.error());

    // Each 4x4 readback quadrant must be a single swatch, and the four quadrants together must be
    // all four swatches. Degenerate UVs collapse every quadrant onto one sample point and fail here.
    std::array<CheckerSwatch, 4U> quadrant{CheckerSwatch::Other, CheckerSwatch::Other,
        CheckerSwatch::Other, CheckerSwatch::Other};
    for (std::size_t index = 0U; index < readback->size(); ++index)
    {
        const std::size_t row = index / kCheckerExtent;
        const std::size_t column = index % kCheckerExtent;
        const std::size_t block =
            (row >= kCheckerExtent / 2U ? 2U : 0U) + (column >= kCheckerExtent / 2U ? 1U : 0U);
        const CheckerSwatch swatch = ClassifyCheckerPixel((*readback)[index]);
        if (swatch == CheckerSwatch::Other)
        {
            std::cerr << "omega_sdl_gpu_mesh_smoke: uv checker pixel (" << column << ',' << row
                      << ") is neither a checker color nor covered\n";
            return 1;
        }
        if (quadrant[block] == CheckerSwatch::Other)
            quadrant[block] = swatch;
        else if (quadrant[block] != swatch)
        {
            std::cerr << "omega_sdl_gpu_mesh_smoke: uv checker quadrant " << block
                      << " is not uniform (" << CheckerSwatchName(quadrant[block]) << " vs "
                      << CheckerSwatchName(swatch) << ")\n";
            return 1;
        }
    }
    for (std::size_t first = 0U; first < quadrant.size(); ++first)
    {
        for (std::size_t second = first + 1U; second < quadrant.size(); ++second)
        {
            if (quadrant[first] == quadrant[second])
            {
                std::cerr << "omega_sdl_gpu_mesh_smoke: uv checker quadrants " << first << " and "
                          << second << " both sampled " << CheckerSwatchName(quadrant[first])
                          << ": the per-vertex UVs did not address the texture\n";
                return 1;
            }
        }
    }

    auto released_quad = host.ReleaseRenderMesh(*quad);
    if (!released_quad)
        return Fail("uv quad release failed", released_quad.error());
    auto released_checker = host.ReleaseTexture(*checker_texture);
    if (!released_checker)
        return Fail("uv checker texture release failed", released_checker.error());
    const omega::app::GpuHostSnapshot after_release = host.Snapshot();
    if (after_release.meshes.resident_uvs != 0U ||
        after_release.meshes.resident_logical_bytes != 0U ||
        after_release.meshes.resident_slots != 0U)
    {
        return Fail("uv quad release left residual mesh residency");
    }
    auto idle = host.WaitForIdle();
    if (!idle)
        return Fail("uv checker idle wait failed", idle.error());

    std::cout << "omega_sdl_gpu_mesh_smoke: uv checker passed driver=" << driver
              << " quadrants=" << CheckerSwatchName(quadrant[0]) << ','
              << CheckerSwatchName(quadrant[1]) << ',' << CheckerSwatchName(quadrant[2]) << ','
              << CheckerSwatchName(quadrant[3]) << '\n';
    return 0;
}
} // namespace

int main()
{
    auto created_platform = omega::app::SdlPlatformService::Create();
    if (!created_platform)
        return Fail("platform creation failed", created_platform.error());
    auto platform = std::move(*created_platform);

    // Runs first, and owns its own host, so only one GPU device is live at a time.
    if (const int uv_result = RunUvCheckerCase(platform); uv_result != 0)
        return uv_result;

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

    // Depth-occlusion proof.
    //
    // The shared triangle mesh is drawn twice at two different clip-space depths that cover
    // exactly the same pixels, and the NEAR layer is submitted FIRST with the FAR layer SECOND.
    // That ordering is deliberate and must not be "tidied" into far-first/near-second: with the
    // far layer first, submission order alone leaves the near color on screen, so such a test
    // passes identically with no depth-stencil target bound at all and proves nothing. With the
    // near layer first, submission order and depth order disagree, so the near color can only
    // survive if SDL_GPU_COMPAREOP_LESS is genuinely testing against a bound, depth-written
    // attachment. This case therefore fails on the depth-less behavior and passes on the fixed
    // behavior, which is the only property that makes it worth having.
    constexpr omega::runtime::RenderClearColorRgba8 opaque_near_red{
        .red = 255U,
        .green = 0U,
        .blue = 0U,
        .alpha = 255U,
    };
    constexpr omega::runtime::RenderClearColorRgba8 opaque_far_blue{
        .red = 0U,
        .green = 0U,
        .blue = 255U,
        .alpha = 255U,
    };
    // Row-major, column-vector transforms whose only effect is a clip-space Z translation: the
    // mesh's own Z of 0.5 becomes 0.25 (near) and 0.75 (far). X and Y stay identity in both, so
    // the two layers rasterize exactly the same pixels and the assertions below are about depth
    // rather than about coverage. The near/far colors share no nonzero channel, so a partial
    // overwrite cannot masquerade as either color.
    constexpr omega::asset::Matrix4x4IR near_depth_transform{
        .row_major = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, -0.25F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    constexpr omega::asset::Matrix4x4IR far_depth_transform{
        .row_major = {
            1.0F, 0.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F, 0.0F,
            0.0F, 0.0F, 1.0F, 0.25F,
            0.0F, 0.0F, 0.0F, 1.0F,
        },
    };
    auto near_depth_command = fill_command;
    near_depth_command.object_to_clip = near_depth_transform;
    near_depth_command.color = omega::runtime::RenderMeshColorRgba8{
        .red = 255U,
        .green = 0U,
        .blue = 0U,
        .alpha = 255U,
    };
    auto far_depth_command = fill_command;
    far_depth_command.object_to_clip = far_depth_transform;
    far_depth_command.color = omega::runtime::RenderMeshColorRgba8{
        .red = 0U,
        .green = 0U,
        .blue = 255U,
        .alpha = 255U,
    };

    // Control pass: the far layer alone. It establishes the exact pixel set the far layer is
    // able to paint, so the ordered pass below cannot pass merely because the far draw was
    // dropped, mistransformed, or clipped away rather than depth-rejected.
    const std::array far_only_commands{far_depth_command};
    auto far_only_draw_list =
        omega::runtime::RenderMeshDrawList::Create(far_only_commands);
    if (!far_only_draw_list)
        return Fail("far-only depth control draw-list creation failed");
    packet.mesh_draw_list = *far_only_draw_list;
    auto far_only_readback = omega::app::detail::SdlGpuHostTestAccess::
        ReadbackMeshesForTesting(host, packet);
    if (!far_only_readback)
        return Fail("far-only depth control readback failed", far_only_readback.error());
    std::array<bool, 64U> covered_by_far_layer{};
    std::size_t far_control_pixels = 0U;
    for (std::size_t index = 0U; index < far_only_readback->size(); ++index)
    {
        const omega::runtime::RenderClearColorRgba8 pixel = (*far_only_readback)[index];
        if (pixel == opaque_far_blue)
        {
            covered_by_far_layer[index] = true;
            ++far_control_pixels;
        }
        else if (pixel != opaque_black)
            return Fail("far-only depth control readback contained an unexpected RGBA8 pixel");
    }
    if (far_control_pixels < 8U)
        return Fail("far-only depth control covered too few pixels to prove depth ordering");
    if (host.Snapshot() != before_readback)
        return Fail("far-only depth control readback mutated production counters or residency");

    // The discriminating pass: NEAR first, FAR second. See the note above before reordering.
    const std::array depth_ordered_commands{near_depth_command, far_depth_command};
    auto depth_ordered_draw_list =
        omega::runtime::RenderMeshDrawList::Create(depth_ordered_commands);
    if (!depth_ordered_draw_list)
        return Fail("near-then-far depth draw-list creation failed");
    packet.mesh_draw_list = *depth_ordered_draw_list;
    auto depth_readback = omega::app::detail::SdlGpuHostTestAccess::
        ReadbackMeshesForTesting(host, packet);
    if (!depth_readback)
        return Fail("near-then-far depth readback failed", depth_readback.error());
    std::size_t depth_near_pixels = 0U;
    for (std::size_t index = 0U; index < depth_readback->size(); ++index)
    {
        const omega::runtime::RenderClearColorRgba8 pixel = (*depth_readback)[index];
        if (pixel == opaque_far_blue)
        {
            return Fail(
                "the far mesh painted over the near mesh: the depth test or the depth-stencil "
                "target is not active for the mesh pass");
        }
        if (covered_by_far_layer[index])
        {
            if (pixel != opaque_near_red)
            {
                return Fail(
                    "a pixel covered by both depth layers did not keep the near mesh color");
            }
            ++depth_near_pixels;
        }
        else if (pixel != opaque_black && pixel != opaque_near_red)
            return Fail("near-then-far depth readback contained an unexpected RGBA8 pixel");
    }
    if (depth_near_pixels != far_control_pixels)
        return Fail("the near mesh did not cover the whole region the far mesh covers");
    if (host.Snapshot() != before_readback)
        return Fail("near-then-far depth readback mutated production counters or residency");

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

    if (!RenderAlternatingMeshBurst(
            host, packet, *fill_draw_list, *wireframe_draw_list))
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
        final.successful_mesh_releases != 2U || final.mesh_submissions != 66U ||
        final.successful_mesh_draws != 66U ||
        final.rejected_nondefault_mesh_handles != 1U ||
        final.frame_submissions != 66U + final.unavailable_swapchain_submissions ||
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
              << " depth_occluded_pixels=" << depth_near_pixels
              << " surviving_overlay_pixels=" << surviving_green_pixels
              << " unavailable="
              << final.unavailable_swapchain_submissions << '\n';
    return 0;
}
