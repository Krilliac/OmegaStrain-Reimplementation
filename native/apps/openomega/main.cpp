#include "omega_app.h"

#include "omega/runtime/content_startup.h"
#include "omega/runtime/launch_options.h"
#include "omega/runtime/runtime_settings.h"

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
    else if (error.level_texture_error)
        std::cerr << omega::content::LevelTextureStoreErrorCodeName(
            error.level_texture_error->code);
    else
        std::cerr << omega::runtime::ContentStartupErrorCodeName(error.code);
    std::cerr << "]: " << error.message << '\n';
}

void PrintRunCaptureDiagnostics(const omega::app::RunCaptureOutcome& outcome)
{
    const auto* const trace_pair = outcome.trace_pair();
    const auto terminal = outcome.terminal_input();

    std::cout << "OpenOmega run capture: requested_frames="
              << outcome.requested_frame_limit()
              << " completion="
              << omega::app::RunCaptureCompletionName(outcome.completion())
              << " trace_pair=" << (trace_pair != nullptr ? "present" : "absent");
    if (trace_pair != nullptr)
    {
        std::cout << " input_trace_frames=" << trace_pair->input_trace().frame_count()
                  << " scheduler_elapsed_trace_frames="
                  << trace_pair->scheduler_elapsed_trace().frame_count();
    }
    std::cout << " terminal=" << (terminal ? "present" : "absent");
    if (terminal)
    {
        std::cout << " terminal_frame_index=" << terminal->frame_index
                  << " terminal_host_quit=" << (terminal->host_quit_requested ? 1 : 0)
                  << " terminal_logical_quit="
                  << (terminal->logical_quit_pressed ? 1 : 0);
    }
    std::cout << '\n';

    const omega::runtime::FrameSchedulerState before =
        outcome.scheduler_state_before();
    const omega::runtime::FrameSchedulerState after =
        outcome.scheduler_state_after();
    std::cout << "OpenOmega run capture scheduler: before_planned_steps="
              << before.total_planned_steps
              << " after_planned_steps=" << after.total_planned_steps
              << " before_remainder_ns=" << before.accumulated_remainder.count()
              << " after_remainder_ns=" << after.accumulated_remainder.count()
              << " before_dropped_time_ns=" << before.total_dropped_time.count()
              << " after_dropped_time_ns=" << after.total_dropped_time.count()
              << '\n';
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

    auto config = omega::runtime::LoadRuntimeConfig(*options);
    if (!config)
    {
        std::cerr << config.error() << '\n';
        return EXIT_FAILURE;
    }
    auto settings = omega::runtime::ResolveRuntimeSettings(*config);
    if (!settings)
    {
        std::cerr << "runtime configuration: " << settings.error() << '\n';
        return EXIT_FAILURE;
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

        if (content.level_manifest && content.level_content)
        {
            std::cout << "OpenOmega level: code=" << *options->level_code
                       << " terrain_cells=" << content.level_manifest->terrain_cells.size()
                       << " spatial_meshes="
                       << content.level_content->spatial.terrain_cells.size()
                       << " view=synthetic-spatial-wireframe\n";
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
    auto app = omega::app::OmegaApp::Create(
        std::move(*config), *settings, std::move(content), debug_device);
    if (!app)
    {
        std::cerr << app.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "OpenOmega native shell: GPU driver=" << app->driver_name()
              << " audio_driver=" << app->audio_driver_name()
              << " audio_format=f32/" << app->audio_sample_rate() << "Hz/"
              << app->audio_channel_count() << "ch\n";

    if (options->capture_run)
    {
        auto capture = app->RunWithCapture(options->frame_limit);
        if (!capture)
        {
            std::cerr << "runtime capture: " << capture.error() << '\n';
            return EXIT_FAILURE;
        }

        const omega::app::RunCaptureOutcome& outcome = *capture;
        const omega::app::RunResult capture_result = outcome.result();
        std::cout << "OpenOmega native shell: rendered_frames="
                  << capture_result.rendered_frames
                  << " planned_simulation_steps="
                  << capture_result.planned_simulation_steps
                  << " executed_simulation_steps="
                  << capture_result.executed_simulation_steps
                  << " input_frames=" << capture_result.input_frames
                  << " audio_callbacks=" << capture_result.audio_callback_count
                  << " audio_frames_provided=" << capture_result.audio_frames_provided
                  << '\n';
        PrintRunCaptureDiagnostics(outcome);
        if (!omega::app::detail::IsCompleteRunCaptureOutcome(
                outcome, options->frame_limit))
        {
            std::cerr << "runtime capture did not produce a complete consistent run: completion="
                      << omega::app::RunCaptureCompletionName(outcome.completion());
            if (const auto failure = outcome.failure())
                std::cerr << " failure=" << *failure;
            std::cerr << '\n';
            return EXIT_FAILURE;
        }
        return EXIT_SUCCESS;
    }

    auto run = app->Run(options->frame_limit);
    if (!run)
    {
        std::cerr << "runtime loop: " << run.error() << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "OpenOmega native shell: rendered_frames=" << run->rendered_frames
              << " planned_simulation_steps=" << run->planned_simulation_steps
              << " executed_simulation_steps=" << run->executed_simulation_steps
              << " input_frames=" << run->input_frames
              << " audio_callbacks=" << run->audio_callback_count
              << " audio_frames_provided=" << run->audio_frames_provided << '\n';
    if (options->frame_limit >= 0 && run->rendered_frames != options->frame_limit)
    {
        std::cerr << "runtime loop ended before the requested frame limit\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
