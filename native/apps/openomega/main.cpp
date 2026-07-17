#include <SDL3/SDL.h>

#include <charconv>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
class SdlLifetime final
{
public:
    SdlLifetime() = default;
    ~SdlLifetime() { SDL_Quit(); }
    SdlLifetime(const SdlLifetime&) = delete;
    SdlLifetime& operator=(const SdlLifetime&) = delete;
};

[[nodiscard]] int ParseFrameLimit(const int argc, char** argv)
{
    constexpr std::string_view prefix = "--frames=";
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view argument(argv[index]);
        if (!argument.starts_with(prefix))
            continue;
        int value = -1;
        const auto result = std::from_chars(argument.data() + prefix.size(),
            argument.data() + argument.size(), value);
        if (result.ec == std::errc{} && result.ptr == argument.data() + argument.size() && value >= 0)
            return value;
        return -2;
    }
    return -1;
}

[[nodiscard]] int Fail(const std::string_view operation)
{
    std::cerr << operation << ": " << SDL_GetError() << '\n';
    return EXIT_FAILURE;
}
} // namespace

int main(const int argc, char** argv)
{
    const int frame_limit = ParseFrameLimit(argc, argv);
    if (frame_limit == -2)
    {
        std::cerr << "--frames requires a non-negative integer\n";
        return EXIT_FAILURE;
    }

    SDL_SetAppMetadata("OpenOmega", "0.1.0", "io.github.krilliac.openomega");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO))
        return Fail("SDL_Init");
    const SdlLifetime sdl_lifetime;

    SDL_Window* window = SDL_CreateWindow(
        "OpenOmega - native runtime", 1280, 720, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
        return Fail("SDL_CreateWindow");

#if defined(OMEGA_GPU_DEBUG)
    constexpr bool debug_device = true;
#else
    constexpr bool debug_device = false;
#endif
    constexpr SDL_GPUShaderFormat shader_formats = static_cast<SDL_GPUShaderFormat>(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL);
    SDL_GPUDevice* device = SDL_CreateGPUDevice(shader_formats, debug_device, nullptr);
    if (device == nullptr)
    {
        SDL_DestroyWindow(window);
        return Fail("SDL_CreateGPUDevice");
    }
    if (!SDL_ClaimWindowForGPUDevice(device, window))
    {
        SDL_DestroyGPUDevice(device);
        SDL_DestroyWindow(window);
        return Fail("SDL_ClaimWindowForGPUDevice");
    }

    const char* driver = SDL_GetGPUDeviceDriver(device);
    std::cout << "OpenOmega native shell: GPU driver=" << (driver != nullptr ? driver : "unknown") << '\n';

    bool running = true;
    int rendered_frames = 0;
    while (running && (frame_limit < 0 || rendered_frames < frame_limit))
    {
        SDL_Event event{};
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }
        if (!running)
            break;

        SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(device);
        if (commands == nullptr)
        {
            running = false;
            break;
        }

        SDL_GPUTexture* swapchain = nullptr;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, window, &swapchain, &width, &height))
        {
            SDL_CancelGPUCommandBuffer(commands);
            running = false;
            break;
        }

        if (swapchain != nullptr)
        {
            const float pulse = static_cast<float>((rendered_frames % 240) / 239.0);
            SDL_GPUColorTargetInfo target{};
            target.texture = swapchain;
            target.clear_color = SDL_FColor{0.025F + pulse * 0.025F, 0.035F, 0.065F + pulse * 0.04F, 1.0F};
            target.load_op = SDL_GPU_LOADOP_CLEAR;
            target.store_op = SDL_GPU_STOREOP_STORE;

            SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(commands, &target, 1, nullptr);
            if (pass == nullptr)
            {
                SDL_CancelGPUCommandBuffer(commands);
                running = false;
                break;
            }
            SDL_EndGPURenderPass(pass);
        }

        if (!SDL_SubmitGPUCommandBuffer(commands))
        {
            running = false;
            break;
        }
        ++rendered_frames;
    }

    if (!running && (frame_limit < 0 || rendered_frames < frame_limit) && SDL_GetError()[0] != '\0')
        std::cerr << "runtime loop: " << SDL_GetError() << '\n';

    SDL_WaitForGPUIdle(device);
    SDL_ReleaseWindowFromGPUDevice(device, window);
    SDL_DestroyGPUDevice(device);
    SDL_DestroyWindow(window);

    std::cout << "OpenOmega native shell: rendered_frames=" << rendered_frames << '\n';
    return running || rendered_frames == frame_limit ? EXIT_SUCCESS : EXIT_FAILURE;
}
