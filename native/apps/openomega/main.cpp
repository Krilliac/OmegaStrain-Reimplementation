#include "sdl_gpu_host.h"

#include "omega/runtime/content_startup.h"
#include "omega/runtime/launch_options.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
void PrintContentError(const omega::runtime::ContentStartupError& error)
{
    std::cerr << "content startup [";
    if (error.game_data_error)
        std::cerr << omega::content::GameDataErrorCodeName(error.game_data_error->code);
    else
        std::cerr << omega::runtime::ContentStartupErrorCodeName(error.code);
    std::cerr << "]: " << error.message << '\n';
}
} // namespace

int main(const int argc, char** argv)
{
    std::vector<std::string_view> arguments;
    arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0U);
    for (int index = 1; index < argc; ++index)
        arguments.emplace_back(argv[index]);

    auto options = omega::runtime::ParseLaunchOptions(arguments);
    if (!options)
    {
        std::cerr << options.error() << '\n' << omega::runtime::LaunchUsage();
        return EXIT_FAILURE;
    }
    if (options->show_help)
    {
        std::cout << omega::runtime::LaunchUsage();
        return EXIT_SUCCESS;
    }

    auto startup = omega::runtime::StartContent(*options);
    if (!startup)
    {
        PrintContentError(startup.error());
        return EXIT_FAILURE;
    }
    auto content = std::move(*startup);
    if (content.game_data)
    {
        std::cout << "OpenOmega content: build="
                  << omega::content::RetailBuildName(content.game_data->identity().build)
                  << " executable=" << content.game_data->identity().boot_executable << '\n';

        if (content.level_manifest)
        {
            std::cout << "OpenOmega level: code=" << *options->level_code
                      << " terrain_cells=" << content.level_manifest->terrain_cells.size()
                      << " view=synthetic-manifest-grid\n";
        }
    }

    if (options->probe_only || options->frame_limit == 0)
    {
        std::cout << "OpenOmega native shell: rendered_frames=0\n";
        return EXIT_SUCCESS;
    }

#if defined(OMEGA_GPU_DEBUG)
    constexpr bool debug_device = true;
#else
    constexpr bool debug_device = false;
#endif
    auto host = omega::app::SdlGpuHost::Create(
        content.debug_image ? &*content.debug_image : nullptr, debug_device);
    if (!host)
    {
        std::cerr << host.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "OpenOmega native shell: GPU driver=" << host->driver_name() << '\n';

    auto run = host->Run(options->frame_limit);
    if (!run)
    {
        std::cerr << "runtime loop: " << run.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "OpenOmega native shell: rendered_frames=" << run->rendered_frames << '\n';
    if (options->frame_limit >= 0 && run->rendered_frames != options->frame_limit)
    {
        std::cerr << "runtime loop ended before the requested frame limit\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
