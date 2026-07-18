#include "sdl_gpu_host.h"

#include "sdl_platform_service.h"

#include "omega/runtime/debug_image.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/log_service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>

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

[[nodiscard]] std::optional<runtime::InputEvent> TranslateInputEvent(
    const SDL_Event& event) noexcept
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.key.repeat)
            return std::nullopt;
        return runtime::InputEvent{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(event.key.scancode),
            .pressed = event.type == SDL_EVENT_KEY_DOWN,
        };
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return runtime::InputEvent{
            .device = runtime::InputDevice::MouseButton,
            .code = event.button.button,
            .pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
        };
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        return runtime::InputEvent{
            .device = runtime::InputDevice::GamepadButton,
            .code = event.gbutton.button,
            .pressed = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        };
    default:
        return std::nullopt;
    }
}
} // namespace

struct SdlGpuHost::Impl
{
    ~Impl()
    {
        if (gamepad != nullptr)
            SDL_CloseGamepad(gamepad);
        if (device != nullptr)
        {
            SDL_WaitForGPUIdle(device);
            if (debug_texture != nullptr)
                SDL_ReleaseGPUTexture(device, debug_texture);
            if (window_claimed && window != nullptr)
                SDL_ReleaseWindowFromGPUDevice(device, window);
            SDL_DestroyGPUDevice(device);
        }
        if (window != nullptr)
            SDL_DestroyWindow(window);
        if (subsystems_initialized)
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD);
    }

    bool subsystems_initialized = false;
    bool window_claimed = false;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID gamepad_id = 0;
    SDL_GPUTexture* debug_texture = nullptr;
    std::uint32_t debug_width = 0;
    std::uint32_t debug_height = 0;
    std::string driver;
};

std::expected<SdlGpuHost, std::string> SdlGpuHost::Create(
    const SdlPlatformService& platform,
    const runtime::DebugImage* debug_image, const bool debug_device)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");

    auto impl = std::make_unique<Impl>();
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        return std::unexpected(SdlError("SDL_InitSubSystem(video/gamepad)"));
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

    if (debug_image == nullptr)
        return SdlGpuHost(std::move(impl));
    if (debug_image->width == 0 || debug_image->height == 0 ||
        debug_image->pixels().size() !=
            static_cast<std::uint64_t>(debug_image->width) * debug_image->height * 4U ||
        debug_image->pixels().size() > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected("debug image has invalid RGBA8 dimensions");

    const SDL_GPUTextureCreateInfo texture_info{
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = debug_image->width,
        .height = debug_image->height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = 0,
    };
    impl->debug_texture = SDL_CreateGPUTexture(impl->device, &texture_info);
    if (impl->debug_texture == nullptr)
        return std::unexpected(SdlError("SDL_CreateGPUTexture"));
    impl->debug_width = debug_image->width;
    impl->debug_height = debug_image->height;

    const SDL_GPUTransferBufferCreateInfo transfer_info{
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<std::uint32_t>(debug_image->pixels().size()),
        .props = 0,
    };
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(impl->device, &transfer_info);
    if (transfer == nullptr)
        return std::unexpected(SdlError("SDL_CreateGPUTransferBuffer"));
    void* mapped = SDL_MapGPUTransferBuffer(impl->device, transfer, false);
    if (mapped == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(impl->device, transfer);
        return std::unexpected(SdlError("SDL_MapGPUTransferBuffer"));
    }
    std::memcpy(mapped, debug_image->pixels().data(), debug_image->pixels().size());
    SDL_UnmapGPUTransferBuffer(impl->device, transfer);

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl->device);
    if (commands == nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(impl->device, transfer);
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    if (copy == nullptr)
    {
        SDL_CancelGPUCommandBuffer(commands);
        SDL_ReleaseGPUTransferBuffer(impl->device, transfer);
        return std::unexpected(SdlError("SDL_BeginGPUCopyPass"));
    }
    const SDL_GPUTextureTransferInfo source{
        .transfer_buffer = transfer,
        .offset = 0,
        .pixels_per_row = debug_image->width,
        .rows_per_layer = debug_image->height,
    };
    const SDL_GPUTextureRegion destination{
        .texture = impl->debug_texture,
        .mip_level = 0,
        .layer = 0,
        .x = 0,
        .y = 0,
        .z = 0,
        .w = debug_image->width,
        .h = debug_image->height,
        .d = 1,
    };
    SDL_UploadToGPUTexture(copy, &source, &destination, false);
    SDL_EndGPUCopyPass(copy);
    const bool submitted = SDL_SubmitGPUCommandBuffer(commands);
    SDL_ReleaseGPUTransferBuffer(impl->device, transfer);
    if (!submitted)
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBuffer"));
    return SdlGpuHost(std::move(impl));
}

SdlGpuHost::SdlGpuHost(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SdlGpuHost::~SdlGpuHost() = default;
SdlGpuHost::SdlGpuHost(SdlGpuHost&&) noexcept = default;
SdlGpuHost& SdlGpuHost::operator=(SdlGpuHost&&) noexcept = default;

HostEventResult SdlGpuHost::PumpEvents(
    runtime::InputTracker& input, runtime::LogService& log)
{
    HostEventResult result;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            result.quit_requested = true;
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            input.ResetAllControls();
        }
        else if (event.type == SDL_EVENT_GAMEPAD_ADDED && impl_->gamepad == nullptr)
        {
            impl_->gamepad = SDL_OpenGamepad(event.gdevice.which);
            if (impl_->gamepad == nullptr)
            {
                log.Warning("input", SdlError("SDL_OpenGamepad"));
            }
            else
            {
                impl_->gamepad_id = event.gdevice.which;
                log.Info("input", "opened the primary SDL gamepad");
            }
        }
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && impl_->gamepad != nullptr &&
                 event.gdevice.which == impl_->gamepad_id)
        {
            SDL_CloseGamepad(impl_->gamepad);
            impl_->gamepad = nullptr;
            impl_->gamepad_id = 0;
            input.ResetDevice(runtime::InputDevice::GamepadButton);
            log.Info("input", "closed the removed primary SDL gamepad");
        }

        if (const auto translated = TranslateInputEvent(event))
        {
            const auto accepted = input.PushEvent(*translated);
            (void)accepted;
        }
    }
    return result;
}

std::expected<void, std::string> SdlGpuHost::RenderFrame(
    const std::uint64_t rendered_frame_index)
{
    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(impl_->device);
    if (commands == nullptr)
        return std::unexpected(SdlError("SDL_AcquireGPUCommandBuffer"));

    SDL_GPUTexture* swapchain = nullptr;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    if (!SDL_WaitAndAcquireGPUSwapchainTexture(
            commands, impl_->window, &swapchain, &width, &height))
    {
        SDL_CancelGPUCommandBuffer(commands);
        return std::unexpected(SdlError("SDL_WaitAndAcquireGPUSwapchainTexture"));
    }

    if (swapchain != nullptr && width != 0 && height != 0 && impl_->debug_texture != nullptr)
    {
        std::uint32_t destination_width = width;
        std::uint32_t destination_height = height;
        if (static_cast<std::uint64_t>(width) * impl_->debug_height <=
            static_cast<std::uint64_t>(height) * impl_->debug_width)
        {
            destination_height = std::max(1U, static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(width) * impl_->debug_height /
                impl_->debug_width));
        }
        else
        {
            destination_width = std::max(1U, static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(height) * impl_->debug_width /
                impl_->debug_height));
        }
        const SDL_GPUBlitInfo blit{
            .source = {
                .texture = impl_->debug_texture,
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = 0,
                .y = 0,
                .w = impl_->debug_width,
                .h = impl_->debug_height,
            },
            .destination = {
                .texture = swapchain,
                .mip_level = 0,
                .layer_or_depth_plane = 0,
                .x = (width - destination_width) / 2U,
                .y = (height - destination_height) / 2U,
                .w = destination_width,
                .h = destination_height,
            },
            .load_op = SDL_GPU_LOADOP_CLEAR,
            .clear_color = {0.015F, 0.02F, 0.04F, 1.0F},
            .flip_mode = SDL_FLIP_NONE,
            .filter = SDL_GPU_FILTER_NEAREST,
            .cycle = false,
            .padding1 = 0,
            .padding2 = 0,
            .padding3 = 0,
        };
        SDL_BlitGPUTexture(commands, &blit);
    }
    else if (swapchain != nullptr && width != 0 && height != 0)
    {
        const float pulse = static_cast<float>((rendered_frame_index % 240U) / 239.0);
        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain;
        target.clear_color = SDL_FColor{
            0.025F + pulse * 0.025F, 0.035F, 0.065F + pulse * 0.04F, 1.0F};
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
        if (pass == nullptr)
        {
            SDL_CancelGPUCommandBuffer(commands);
            return std::unexpected(SdlError("SDL_BeginGPURenderPass"));
        }
        SDL_EndGPURenderPass(pass);
    }

    if (!SDL_SubmitGPUCommandBuffer(commands))
        return std::unexpected(SdlError("SDL_SubmitGPUCommandBuffer"));
    return {};
}

std::string_view SdlGpuHost::driver_name() const noexcept
{
    return impl_->driver;
}
} // namespace omega::app
