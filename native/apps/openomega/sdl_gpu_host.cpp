#include "sdl_gpu_host.h"

#include "sdl_platform_service.h"

#include "omega/runtime/render_draw_list.h"
#include "omega/runtime/render_texture_pool.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace omega::app
{
namespace
{
[[nodiscard]] std::string SdlError(const std::string_view operation)
{
    const char* detail = SDL_GetError();
    return std::string(operation) + ": " +
           (detail != nullptr && detail[0] != '\0' ? detail : "unknown SDL error");
}

[[nodiscard]] std::string PoolError(
    const std::string_view operation, const runtime::RenderTextureError& error)
{
    return std::string(operation) + ": " +
           std::string(runtime::RenderTextureErrorCodeName(error.code));
}

constexpr std::size_t kPostAcquireErrorCapacity = 512U;

void AppendBounded(std::string& destination, const std::string_view text) noexcept
{
    const std::size_t remaining = destination.capacity() - destination.size();
    destination.append(text.data(), std::min(remaining, text.size()));
}

void SetSdlErrorBounded(
    std::string& destination, const std::string_view operation) noexcept
{
    destination.clear();
    AppendBounded(destination, operation);
    AppendBounded(destination, ": ");
    const char* detail = SDL_GetError();
    AppendBounded(destination,
        detail != nullptr && detail[0] != '\0' ? std::string_view(detail)
                                                : std::string_view("unknown SDL error"));
}

void AppendSdlErrorBounded(
    std::string& destination, const std::string_view operation) noexcept
{
    AppendBounded(destination, "; ");
    AppendBounded(destination, operation);
    AppendBounded(destination, ": ");
    const char* detail = SDL_GetError();
    AppendBounded(destination,
        detail != nullptr && detail[0] != '\0' ? std::string_view(detail)
                                                : std::string_view("unknown SDL error"));
}

class ReservationRollbackGuard final
{
public:
    ReservationRollbackGuard(runtime::RenderTexturePool& pool,
        const runtime::RenderTextureReservation& reservation) noexcept
        : pool_(&pool), reservation_(&reservation)
    {
    }

    ~ReservationRollbackGuard()
    {
        if (pool_ != nullptr)
            static_cast<void>(pool_->Rollback(*reservation_));
    }

    ReservationRollbackGuard(const ReservationRollbackGuard&) = delete;
    ReservationRollbackGuard& operator=(const ReservationRollbackGuard&) = delete;

    void Dismiss() noexcept
    {
        pool_ = nullptr;
        reservation_ = nullptr;
    }

private:
    runtime::RenderTexturePool* pool_ = nullptr;
    const runtime::RenderTextureReservation* reservation_ = nullptr;
};

class TextureGuard final
{
public:
    TextureGuard(SDL_GPUDevice* device, SDL_GPUTexture* texture) noexcept
        : device_(device), texture_(texture)
    {
    }

    ~TextureGuard()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(device_, texture_);
    }

    TextureGuard(const TextureGuard&) = delete;
    TextureGuard& operator=(const TextureGuard&) = delete;

    void Dismiss() noexcept { texture_ = nullptr; }

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* texture_ = nullptr;
};

class TransferBufferGuard final
{
public:
    TransferBufferGuard(SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer) noexcept
        : device_(device), transfer_(transfer)
    {
    }

    ~TransferBufferGuard()
    {
        if (transfer_ != nullptr)
            SDL_ReleaseGPUTransferBuffer(device_, transfer_);
    }

    TransferBufferGuard(const TransferBufferGuard&) = delete;
    TransferBufferGuard& operator=(const TransferBufferGuard&) = delete;

private:
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTransferBuffer* transfer_ = nullptr;
};

enum class CommandBufferUnwindAction
{
    Cancel,
    Submit,
};

class CommandBufferGuard final
{
public:
    explicit CommandBufferGuard(SDL_GPUCommandBuffer* commands) noexcept
        : commands_(commands)
    {
    }

    ~CommandBufferGuard()
    {
        if (commands_ == nullptr)
            return;
        if (unwind_action_ == CommandBufferUnwindAction::Submit)
            static_cast<void>(SDL_SubmitGPUCommandBuffer(commands_));
        else
            static_cast<void>(SDL_CancelGPUCommandBuffer(commands_));
    }

    CommandBufferGuard(const CommandBufferGuard&) = delete;
    CommandBufferGuard& operator=(const CommandBufferGuard&) = delete;

    void SubmitOnUnwind() noexcept
    {
        unwind_action_ = CommandBufferUnwindAction::Submit;
    }

    [[nodiscard]] bool Cancel() noexcept
    {
        SDL_GPUCommandBuffer* commands = std::exchange(commands_, nullptr);
        return commands != nullptr && SDL_CancelGPUCommandBuffer(commands);
    }

    [[nodiscard]] bool Submit() noexcept
    {
        SDL_GPUCommandBuffer* commands = std::exchange(commands_, nullptr);
        return commands != nullptr && SDL_SubmitGPUCommandBuffer(commands);
    }

private:
    SDL_GPUCommandBuffer* commands_ = nullptr;
    CommandBufferUnwindAction unwind_action_ = CommandBufferUnwindAction::Cancel;
};

void SaturatingIncrement(std::uint64_t& value) noexcept
{
    if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
}

void SaturatingAdd(std::uint64_t& value, const std::uint64_t added) noexcept
{
    const std::uint64_t maximum = std::numeric_limits<std::uint64_t>::max();
    value = added > maximum - value ? maximum : value + added;
}

[[nodiscard]] constexpr bool IsDefaultHandle(
    const runtime::RenderTextureHandle& handle) noexcept
{
    return handle == runtime::RenderTextureHandle{};
}

enum class FrameSubmissionKind
{
    Blit,
    Clear,
    UnavailableSwapchain,
};

struct ResolvedTextureBlit
{
    SDL_GPUTexture* texture = nullptr;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
};

} // namespace

struct SdlGpuHost::Impl
{
    explicit Impl(runtime::RenderTexturePool pool)
        : texture_pool(std::move(pool)),
          texture_slots(texture_pool.Snapshot().slot_capacity, nullptr)
    {
    }

    ~Impl()
    {
        if (device != nullptr)
        {
            SDL_WaitForGPUIdle(device);
            for (SDL_GPUTexture*& texture : texture_slots)
            {
                if (texture != nullptr)
                {
                    SDL_ReleaseGPUTexture(device, texture);
                    texture = nullptr;
                }
            }
            if (window_claimed && window != nullptr)
                SDL_ReleaseWindowFromGPUDevice(device, window);
            SDL_DestroyGPUDevice(device);
        }
        if (window != nullptr)
            SDL_DestroyWindow(window);
        if (subsystems_initialized)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    runtime::RenderTexturePool texture_pool;
    std::vector<SDL_GPUTexture*> texture_slots;
    bool subsystems_initialized = false;
    bool window_claimed = false;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    std::string driver;
    std::uint64_t successful_uploads = 0U;
    std::uint64_t successful_upload_logical_bytes = 0U;
    std::uint64_t successful_releases = 0U;
    std::uint64_t frame_submissions = 0U;
    std::uint64_t blit_submissions = 0U;
    std::uint64_t successful_blit_draws = 0U;
    std::uint64_t clear_submissions = 0U;
    std::uint64_t unavailable_swapchain_submissions = 0U;
    std::uint64_t rejected_nondefault_texture_handles = 0U;
};

std::expected<SdlGpuHost, std::string> SdlGpuHost::Create(
    const SdlPlatformService& platform, const bool debug_device,
    const runtime::RenderTexturePoolConfig texture_config)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");

    auto created_pool = runtime::RenderTexturePool::Create(texture_config);
    if (!created_pool)
        return std::unexpected(PoolError("render texture pool creation", created_pool.error()));

    std::unique_ptr<Impl> impl;
    try
    {
        impl = std::make_unique<Impl>(std::move(*created_pool));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render texture backend table allocation failed");
    }

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        return std::unexpected(SdlError("SDL_InitSubSystem(video)"));
    impl->subsystems_initialized = true;

    impl->window = SDL_CreateWindow("OpenOmega - native runtime", 1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (impl->window == nullptr)
        return std::unexpected(SdlError("SDL_CreateWindow"));

    constexpr SDL_GPUShaderFormat shader_formats = static_cast<SDL_GPUShaderFormat>(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL);
    impl->device = SDL_CreateGPUDevice(shader_formats, debug_device, nullptr);
    if (impl->device == nullptr)
        return std::unexpected(SdlError("SDL_CreateGPUDevice"));
    if (!SDL_ClaimWindowForGPUDevice(impl->device, impl->window))
        return std::unexpected(SdlError("SDL_ClaimWindowForGPUDevice"));
    impl->window_claimed = true;

    const char* driver = SDL_GetGPUDeviceDriver(impl->device);
    impl->driver = driver != nullptr ? driver : "unknown";
    return SdlGpuHost(std::move(impl));
}

SdlGpuHost::SdlGpuHost(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SdlGpuHost::~SdlGpuHost() = default;
SdlGpuHost::SdlGpuHost(SdlGpuHost&&) noexcept = default;

std::expected<runtime::RenderTextureHandle, std::string> SdlGpuHost::UploadRgba8Texture(
    const runtime::Rgba8TextureUploadView upload)
{
    try
    {
        auto reservation = impl_->texture_pool.Reserve(upload);
        if (!reservation)
            return std::unexpected(PoolError("render texture reserve", reservation.error()));
        ReservationRollbackGuard reservation_guard(impl_->texture_pool, *reservation);

        const std::uint32_t slot_index = reservation->handle.slot_index;
        if (slot_index >= impl_->texture_slots.size() ||
            impl_->texture_slots[slot_index] != nullptr)
        {
            return std::unexpected("render texture backend slot invariant failed");
        }
        if (upload.pixels.size() > std::numeric_limits<std::uint32_t>::max())
        {
            return std::unexpected(
                "render texture upload exceeds the SDL transfer-buffer size limit");
        }

        const SDL_GPUTextureCreateInfo texture_info{
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = reservation->width,
            .height = reservation->height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = 0,
        };
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(impl_->device, &texture_info);
        if (texture == nullptr)
            return std::unexpected(SdlError("render texture create"));
        TextureGuard texture_guard(impl_->device, texture);

        const SDL_GPUTransferBufferCreateInfo transfer_info{
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = static_cast<std::uint32_t>(upload.pixels.size()),
            .props = 0,
        };
        SDL_GPUTransferBuffer* transfer =
            SDL_CreateGPUTransferBuffer(impl_->device, &transfer_info);
        if (transfer == nullptr)
            return std::unexpected(SdlError("render texture transfer-buffer create"));
        TransferBufferGuard transfer_guard(impl_->device, transfer);

        void* mapped = SDL_MapGPUTransferBuffer(impl_->device, transfer, false);
        if (mapped == nullptr)
            return std::unexpected(SdlError("render texture transfer-buffer map"));
        std::memcpy(mapped, upload.pixels.data(), upload.pixels.size());
        SDL_UnmapGPUTransferBuffer(impl_->device, transfer);

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (commands == nullptr)
            return std::unexpected(SdlError("render texture command-buffer acquire"));
        CommandBufferGuard command_guard(commands);

        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
        if (copy == nullptr)
        {
            std::string error = SdlError("render texture copy-pass begin");
            if (!command_guard.Cancel())
                error += "; " + SdlError("render texture command-buffer cancel");
            return std::unexpected(std::move(error));
        }

        const SDL_GPUTextureTransferInfo source{
            .transfer_buffer = transfer,
            .offset = 0,
            .pixels_per_row = reservation->width,
            .rows_per_layer = reservation->height,
        };
        const SDL_GPUTextureRegion destination{
            .texture = texture,
            .mip_level = 0,
            .layer = 0,
            .x = 0,
            .y = 0,
            .z = 0,
            .w = reservation->width,
            .h = reservation->height,
            .d = 1,
        };
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        if (!command_guard.Submit())
            return std::unexpected(SdlError("render texture command-buffer submit"));

        auto published = impl_->texture_pool.Publish(*reservation);
        if (!published)
        {
            std::string error = PoolError("render texture publish", published.error());
            if (auto idle = WaitForIdle(); !idle)
                error += "; " + idle.error();
            return std::unexpected(std::move(error));
        }

        impl_->texture_slots[slot_index] = texture;
        texture_guard.Dismiss();
        reservation_guard.Dismiss();
        SaturatingIncrement(impl_->successful_uploads);
        SaturatingAdd(impl_->successful_upload_logical_bytes, reservation->logical_bytes);
        return *published;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render texture upload error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("render texture upload failed unexpectedly");
    }
}

std::expected<void, std::string> SdlGpuHost::ReleaseTexture(
    const runtime::RenderTextureHandle& handle)
{
    auto metadata = impl_->texture_pool.Get(handle);
    if (!metadata)
    {
        if (!IsDefaultHandle(handle))
            SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
        return std::unexpected(PoolError("render texture resolve for release", metadata.error()));
    }

    const std::uint32_t slot_index = metadata->handle.slot_index;
    if (slot_index >= impl_->texture_slots.size() ||
        impl_->texture_slots[slot_index] == nullptr)
    {
        SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
        return std::unexpected("render texture backend slot invariant failed during release");
    }

    auto idle = WaitForIdle();
    if (!idle)
        return idle;

    auto released = impl_->texture_pool.Release(handle);
    if (!released)
        return std::unexpected(PoolError("render texture release", released.error()));

    SDL_ReleaseGPUTexture(impl_->device, impl_->texture_slots[slot_index]);
    impl_->texture_slots[slot_index] = nullptr;
    SaturatingIncrement(impl_->successful_releases);
    return {};
}

std::expected<void, std::string> SdlGpuHost::WaitForIdle()
{
    if (!SDL_WaitForGPUIdle(impl_->device))
        return std::unexpected(SdlError("SDL_WaitForGPUIdle"));
    return {};
}

std::expected<void, std::string> SdlGpuHost::RenderFrame(
    const runtime::RenderFramePacket& packet)
{
    try
    {
        const std::span<const runtime::RenderTextureBlitCommand> draw_commands =
            packet.draw_list.commands();
        std::array<ResolvedTextureBlit,
            runtime::kMaximumRenderTextureBlitsPerFrame> resolved_blits{};
        std::array<runtime::RenderSourceRectPixels,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_sources{};
        std::array<SDL_GPUFilter,
            runtime::kMaximumRenderTextureBlitsPerFrame> mapped_filters{};
        std::array<runtime::RenderTextureBlitPlan,
            runtime::kMaximumRenderTextureBlitsPerFrame> blit_plans{};

        // Resolve the complete handle set before interpreting any remaining command fields.
        // This keeps a stale later generation from permitting partial validation or any
        // GPU-side prefix work.
        for (std::size_t index = 0U; index < draw_commands.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = draw_commands[index];
            auto metadata = impl_->texture_pool.Get(draw.texture);
            if (!metadata)
            {
                SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
                return std::unexpected(
                    PoolError("render frame draw texture resolve", metadata.error()));
            }

            const std::uint32_t slot_index = metadata->handle.slot_index;
            if (slot_index >= impl_->texture_slots.size() ||
                impl_->texture_slots[slot_index] == nullptr)
            {
                SaturatingIncrement(impl_->rejected_nondefault_texture_handles);
                return std::unexpected(
                    "render frame draw texture backend slot invariant failed");
            }
            resolved_blits[index] = ResolvedTextureBlit{
                .texture = impl_->texture_slots[slot_index],
                .width = metadata->width,
                .height = metadata->height,
            };
        }

        // Convert every normalized source crop and backend filter before acquiring GPU work.
        // These fixed arrays are the only command-specific storage used after acquisition.
        for (std::size_t index = 0U; index < draw_commands.size(); ++index)
        {
            const runtime::RenderTextureBlitCommand& draw = draw_commands[index];
            const ResolvedTextureBlit& resolved = resolved_blits[index];
            auto source = runtime::MapTextureSourceRect(
                draw.source, resolved.width, resolved.height);
            if (!source)
            {
                return std::unexpected(std::string(
                    "render frame source rectangle mapping failed: ") +
                    std::string(source.error().message));
            }
            mapped_sources[index] = *source;

            switch (draw.filter_mode)
            {
            case runtime::RenderTextureFilterMode::Nearest:
                mapped_filters[index] = SDL_GPU_FILTER_NEAREST;
                break;
            case runtime::RenderTextureFilterMode::Linear:
                mapped_filters[index] = SDL_GPU_FILTER_LINEAR;
                break;
            default:
                return std::unexpected("render frame texture filter mode is invalid");
            }
        }

        // Reserve all error storage before acquiring the command buffer. The active command
        // path below uses only fixed-capacity values and bounded writes into this existing buffer.
        std::string post_acquire_error;
        post_acquire_error.reserve(kPostAcquireErrorCapacity);

        SDL_GPUCommandBuffer* gpu_commands = SDL_AcquireGPUCommandBuffer(impl_->device);
        if (gpu_commands == nullptr)
        {
            SetSdlErrorBounded(post_acquire_error, "SDL_AcquireGPUCommandBuffer");
            return std::unexpected(std::move(post_acquire_error));
        }
        CommandBufferGuard command_guard(gpu_commands);

        SDL_GPUTexture* swapchain = nullptr;
        std::uint32_t width = 0U;
        std::uint32_t height = 0U;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(
                gpu_commands, impl_->window, &swapchain, &width, &height))
        {
            SetSdlErrorBounded(
                post_acquire_error, "SDL_WaitAndAcquireGPUSwapchainTexture");
            if (!command_guard.Cancel())
            {
                AppendSdlErrorBounded(post_acquire_error,
                    "SDL_CancelGPUCommandBuffer after acquire failure");
            }
            return std::unexpected(std::move(post_acquire_error));
        }
        command_guard.SubmitOnUnwind();

        FrameSubmissionKind submission_kind = FrameSubmissionKind::UnavailableSwapchain;
        if (swapchain != nullptr && width != 0U && height != 0U)
        {
            if (draw_commands.empty())
            {
                submission_kind = FrameSubmissionKind::Clear;
            }
            else
            {
                // Plan the complete frame before recording the clear or any source-order blit.
                // A planning failure therefore submits an empty acquired buffer, with no visible
                // prefix and no successful-frame counters.
                for (std::size_t index = 0U; index < draw_commands.size(); ++index)
                {
                    const runtime::RenderTextureBlitCommand& draw = draw_commands[index];
                    auto plan = runtime::PlanTextureBlit(mapped_sources[index],
                        draw.destination, draw.fit_mode, width, height);
                    if (!plan)
                    {
                        post_acquire_error.clear();
                        AppendBounded(post_acquire_error,
                            "render frame texture blit planning failed: ");
                        AppendBounded(post_acquire_error, plan.error().message);
                        if (!command_guard.Submit())
                        {
                            AppendSdlErrorBounded(post_acquire_error,
                                "command-buffer submit after blit planning failure");
                        }
                        return std::unexpected(std::move(post_acquire_error));
                    }
                    blit_plans[index] = *plan;
                }
                submission_kind = FrameSubmissionKind::Blit;
            }

            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            if (draw_commands.empty())
            {
                const float pulse =
                    static_cast<float>((packet.rendered_frame_index % 240U) / 239.0);
                target.clear_color = SDL_FColor{
                    0.025F + pulse * 0.025F, 0.035F, 0.065F + pulse * 0.04F, 1.0F};
            }
            else
            {
                target.clear_color = SDL_FColor{0.015F, 0.02F, 0.04F, 1.0F};
            }
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;
            SDL_GPURenderPass* pass =
                SDL_BeginGPURenderPass(gpu_commands, &target, 1, nullptr);
            if (pass == nullptr)
            {
                SetSdlErrorBounded(post_acquire_error, "SDL_BeginGPURenderPass");
                if (!command_guard.Submit())
                {
                    AppendSdlErrorBounded(post_acquire_error,
                        "command-buffer submit after render-pass failure");
                }
                return std::unexpected(std::move(post_acquire_error));
            }
            SDL_EndGPURenderPass(pass);

            for (std::size_t index = 0U; index < draw_commands.size(); ++index)
            {
                const ResolvedTextureBlit& resolved = resolved_blits[index];
                const runtime::RenderTextureBlitPlan& plan = blit_plans[index];

                const SDL_GPUBlitInfo blit{
                    .source = {
                        .texture = resolved.texture,
                        .mip_level = 0,
                        .layer_or_depth_plane = 0,
                        .x = plan.source.left,
                        .y = plan.source.top,
                        .w = plan.source.right - plan.source.left,
                        .h = plan.source.bottom - plan.source.top,
                    },
                    .destination = {
                        .texture = swapchain,
                        .mip_level = 0,
                        .layer_or_depth_plane = 0,
                        .x = plan.destination.left,
                        .y = plan.destination.top,
                        .w = plan.destination.right - plan.destination.left,
                        .h = plan.destination.bottom - plan.destination.top,
                    },
                    .load_op = SDL_GPU_LOADOP_LOAD,
                    .clear_color = {},
                    .flip_mode = SDL_FLIP_NONE,
                    .filter = mapped_filters[index],
                    .cycle = false,
                    .padding1 = 0,
                    .padding2 = 0,
                    .padding3 = 0,
                };
                SDL_BlitGPUTexture(gpu_commands, &blit);
            }
        }

        if (!command_guard.Submit())
        {
            SetSdlErrorBounded(post_acquire_error, "SDL_SubmitGPUCommandBuffer");
            return std::unexpected(std::move(post_acquire_error));
        }

        SaturatingIncrement(impl_->frame_submissions);
        switch (submission_kind)
        {
        case FrameSubmissionKind::Blit:
            SaturatingIncrement(impl_->blit_submissions);
            SaturatingAdd(impl_->successful_blit_draws,
                static_cast<std::uint64_t>(draw_commands.size()));
            break;
        case FrameSubmissionKind::Clear:
            SaturatingIncrement(impl_->clear_submissions);
            break;
        case FrameSubmissionKind::UnavailableSwapchain:
            SaturatingIncrement(impl_->unavailable_swapchain_submissions);
            break;
        }
        return {};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected("render frame error allocation failed");
    }
    catch (...)
    {
        return std::unexpected("render frame failed unexpectedly");
    }
}

runtime::RenderTexturePoolSnapshot SdlGpuHost::TextureSnapshot() const noexcept
{
    return impl_->texture_pool.Snapshot();
}

GpuHostSnapshot SdlGpuHost::Snapshot() const noexcept
{
    return GpuHostSnapshot{
        .textures = impl_->texture_pool.Snapshot(),
        .successful_uploads = impl_->successful_uploads,
        .successful_upload_logical_bytes = impl_->successful_upload_logical_bytes,
        .successful_releases = impl_->successful_releases,
        .frame_submissions = impl_->frame_submissions,
        .blit_submissions = impl_->blit_submissions,
        .successful_blit_draws = impl_->successful_blit_draws,
        .clear_submissions = impl_->clear_submissions,
        .unavailable_swapchain_submissions = impl_->unavailable_swapchain_submissions,
        .rejected_nondefault_texture_handles =
            impl_->rejected_nondefault_texture_handles,
    };
}

std::string_view SdlGpuHost::driver_name() const noexcept
{
    return impl_->driver;
}
} // namespace omega::app
