#include "omega_app.h"
#include "native_persistence.h"
#include "run_replay_session.h"
#include "startup_failure_dialog.h"

#include "omega/persistence/native_save_path.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/content_startup_diagnostic.h"
#include "omega/runtime/launch_options.h"
#include "omega/runtime/runtime_config_discovery.h"
#include "omega/runtime/runtime_settings.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
#if defined(_WIN32)
[[nodiscard]] std::optional<std::filesystem::path> ReadWideEnvironmentPath(
    const wchar_t* const name)
{
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const wchar_t* const value = _wgetenv(name);
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (value == nullptr)
        return std::nullopt;
    return std::filesystem::path(value);
}
#else
[[nodiscard]] std::optional<std::filesystem::path> ReadEnvironmentPath(
    const char* const name)
{
    const char* const value = std::getenv(name);
    if (value == nullptr)
        return std::nullopt;
    return std::filesystem::path(value);
}
#endif

[[nodiscard]] std::expected<std::optional<std::filesystem::path>, std::string>
CaptureDefaultRuntimeConfigPath()
{
    try
    {
        omega::runtime::RuntimeConfigSearchRoots roots;
        const omega::runtime::RuntimeConfigPlatform platform =
            omega::runtime::HostRuntimeConfigPlatform();
#if defined(_WIN32)
        roots.local_app_data = ReadWideEnvironmentPath(L"LOCALAPPDATA");
#elif defined(__APPLE__)
        roots.home = ReadEnvironmentPath("HOME");
#else
        roots.xdg_config_home = ReadEnvironmentPath("XDG_CONFIG_HOME");
        roots.home = ReadEnvironmentPath("HOME");
#endif
        return omega::runtime::ResolveDefaultRuntimeConfigPath(platform, roots);
    }
    catch (...)
    {
        return std::unexpected(
            "runtime configuration default profile: unable to resolve default config path");
    }
}

[[nodiscard]] std::expected<std::filesystem::path, std::string>
CaptureDefaultNativeSavePath()
{
    try
    {
        omega::persistence::NativeSaveSearchRoots roots;
        const omega::persistence::NativeSavePlatform platform =
            omega::persistence::HostNativeSavePlatform();
#if defined(_WIN32)
        roots.local_app_data = ReadWideEnvironmentPath(L"LOCALAPPDATA");
#elif defined(__APPLE__)
        roots.home = ReadEnvironmentPath("HOME");
#else
        roots.xdg_data_home = ReadEnvironmentPath("XDG_DATA_HOME");
        roots.home = ReadEnvironmentPath("HOME");
#endif
        auto path = omega::persistence::ResolveDefaultNativeSavePath(platform, roots);
        if (!path)
        {
            return std::unexpected(
                "native persistence default path: no usable absolute platform data root");
        }
        return std::move(*path);
    }
    catch (...)
    {
        return std::unexpected(
            "native persistence default path: unable to resolve native-save path");
    }
}

void PresentStartupFailureDialogAfterStderr(
    const omega::app::StartupFailureStage stage,
    const std::string_view category,
    const std::string_view detail)
{
    std::cerr.flush();
    const auto policy = omega::app::ReadStartupFailureDialogPolicyFromEnvironment();
    static_cast<void>(omega::app::TryShowStartupFailureDialog(
        {.stage = stage, .category = category, .detail = detail}, policy));
}

void PrintRuntimeConfigurationError(const std::string_view detail)
{
    std::cerr << detail << '\n';
    PresentStartupFailureDialogAfterStderr(
        omega::app::StartupFailureStage::RuntimeConfiguration,
        "runtime-configuration", detail);
}

void PrintRuntimeSettingsError(const std::string_view detail)
{
    std::cerr << "runtime configuration: " << detail << '\n';
    PresentStartupFailureDialogAfterStderr(
        omega::app::StartupFailureStage::RuntimeSettings,
        "runtime-settings", detail);
}

void PrintContentLaunchProfileError(
    const omega::runtime::ContentLaunchProfileError& error)
{
    const std::string_view category =
        omega::runtime::ContentLaunchProfileErrorCodeName(error.code);
    std::cerr << "content launch profile [" << category << "]: " << error.message << '\n';
    PresentStartupFailureDialogAfterStderr(
        omega::app::StartupFailureStage::ContentLaunchProfile,
        category, error.message);
}

void PrintContentError(const omega::runtime::ContentStartupError& error)
{
    const auto diagnostic = omega::runtime::DescribeContentStartupError(error);
    if (!diagnostic)
    {
        constexpr std::string_view category = "inconsistent-error";
        constexpr std::string_view detail =
            "content startup error representation is inconsistent";
        std::cerr << "content startup [" << category << "]: " << detail << '\n';
        PresentStartupFailureDialogAfterStderr(
            omega::app::StartupFailureStage::ContentStartup,
            category, detail);
        return;
    }

    std::cerr << "content startup [" << diagnostic->category << "]: "
              << diagnostic->message << '\n';
    PresentStartupFailureDialogAfterStderr(
        omega::app::StartupFailureStage::ContentStartup,
        diagnostic->category, diagnostic->message);
}

void PrintNativePersistencePathError(const std::string_view detail)
{
    constexpr std::string_view category = "path-unavailable";
    std::cerr << "native persistence [" << category << "]: " << detail << '\n';
    PresentStartupFailureDialogAfterStderr(
        omega::app::StartupFailureStage::NativePersistence, category, detail);
}

void PrintNativePersistenceError(
    const omega::app::NativePersistenceStartupError& error)
{
    const std::string_view category =
        omega::app::NativePersistenceStartupErrorCodeName(error.code);
    std::cerr << "native persistence [" << category << "]: " << error.message << '\n';
    PresentStartupFailureDialogAfterStderr(
        omega::app::StartupFailureStage::NativePersistence, category, error.message);
}

void PrintApplicationStartupError(const std::string_view detail)
{
    // Preserve the established stderr diagnostic. The packaged dialog is deliberately fixed:
    // SDL and backend messages can contain host paths or device identifiers.
    std::cerr << detail << '\n';
    constexpr auto request = omega::app::ApplicationStartupFailureDialogRequest();
    PresentStartupFailureDialogAfterStderr(
        request.stage, request.category, request.detail);
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

void PrintRunReplayError(const omega::app::RunReplayError& error)
{
    std::cerr << "runtime capture replay ["
              << omega::app::RunReplayOperationName(error.operation) << '/'
              << omega::app::RunReplayErrorCodeName(error.code);
    if (error.replay_error)
    {
        std::cerr << '/'
                  << omega::runtime::RunCaptureReplayErrorCodeName(
                         error.replay_error->code);
    }
    std::cerr << "]: " << error.message << '\n';
}

[[nodiscard]] constexpr std::string_view ReplayFrontEndModeName(
    const omega::app::FrontEndMode mode) noexcept
{
    switch (mode)
    {
    case omega::app::FrontEndMode::Main:
        return "Main";
    case omega::app::FrontEndMode::Profiles:
        return "Profiles";
    case omega::app::FrontEndMode::Characters:
        return "Characters";
    case omega::app::FrontEndMode::BriefingRoom:
        return "BriefingRoom";
    case omega::app::FrontEndMode::DiagnosticPlay:
        return "DiagnosticPlay";
    case omega::app::FrontEndMode::Controls:
        return "Controls";
    case omega::app::FrontEndMode::AssetTopology:
        return "AssetTopology";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view ReplayFrontEndMainRowName(
    const omega::app::FrontEndMainRow row) noexcept
{
    switch (row)
    {
    case omega::app::FrontEndMainRow::StartDiagnostic:
        return "StartDiagnostic";
    case omega::app::FrontEndMainRow::Profiles:
        return "Profiles";
    case omega::app::FrontEndMainRow::Controls:
        return "Controls";
    case omega::app::FrontEndMainRow::AssetTopology:
        return "AssetTopology";
    }
    return "Unknown";
}

[[nodiscard]] constexpr std::string_view ReplayFrontEndProfileSlotName(
    const omega::app::FrontEndProfileSlot slot) noexcept
{
    switch (slot)
    {
    case omega::app::FrontEndProfileSlot::First:
        return "First";
    case omega::app::FrontEndProfileSlot::Second:
        return "Second";
    case omega::app::FrontEndProfileSlot::Third:
        return "Third";
    }
    return "Unknown";
}

[[nodiscard]] bool IsZeroOriginScheduler(
    const omega::runtime::FrameSchedulerState& state) noexcept
{
    return state == omega::runtime::FrameSchedulerState{.config = state.config};
}

[[nodiscard]] bool SameDiagnosticProximityState(
    const std::optional<omega::gameplay::DiagnosticProximityTriggerState>& left,
    const std::optional<omega::gameplay::DiagnosticProximityTriggerState>& right) noexcept
{
    if (left.has_value() != right.has_value())
        return false;
    return !left ||
           (left->inside == right->inside &&
               left->objective_complete == right->objective_complete);
}

[[nodiscard]] bool ReplayFreshCapture(
    omega::app::RunCaptureOutcome&& outcome,
    const omega::app::RunResult capture_result,
    const omega::runtime::FrameSchedulerState capture_before,
    const omega::runtime::FrameSchedulerState capture_after,
    const omega::gameplay::DiagnosticTargetFireState
        capture_diagnostic_target_fire_state,
    const omega::gameplay::DiagnosticMissionLifecycleState
        capture_diagnostic_mission_lifecycle_state,
    const omega::gameplay::DiagnosticProximityTriggerState
        capture_diagnostic_proximity_trigger_state,
    const std::optional<omega::simulation::Position3>
        capture_diagnostic_actor_position,
    const omega::app::FrontEndState capture_front_end_state,
    const std::size_t front_end_total_profile_count,
    const std::uint8_t front_end_visible_profile_slots,
    const std::array<std::uint8_t, omega::app::kFrontEndVisibleProfiles>&
        front_end_visible_character_slots_by_profile,
    const std::array<std::size_t, omega::app::kFrontEndVisibleProfiles>&
        front_end_total_character_counts_by_profile)
{
    auto traces = std::move(outcome).TakeTracePair();
    if (!traces)
    {
        std::cerr << "runtime capture replay: complete capture trace pair is absent\n";
        return false;
    }

    omega::app::RunReplaySessionConfig config{};
    config.scheduler = capture_before.config;
    constexpr std::array debug_locomotion_actions{
        omega::app::kDebugMoveForwardAction,
        omega::app::kDebugMoveBackwardAction,
        omega::app::kDebugMoveLeftAction,
        omega::app::kDebugMoveRightAction,
    };
    config.enable_debug_locomotion = std::ranges::includes(
        traces->input_trace().actions(), debug_locomotion_actions);
    constexpr std::array debug_target_fire_actions{
        omega::app::kDebugFireAction,
        omega::app::kDebugTargetAction,
    };
    config.enable_debug_target_fire = std::ranges::includes(
        traces->input_trace().actions(), debug_target_fire_actions);
    config.enable_debug_mission_lifecycle =
        config.enable_debug_locomotion && config.enable_debug_target_fire;
    if (front_end_total_profile_count > omega::app::kFrontEndMaximumProfiles)
    {
        std::cerr << "runtime capture replay: profile snapshot exceeds the project limit\n";
        return false;
    }
    config.front_end_visible_profile_slots = front_end_visible_profile_slots;
    config.front_end_total_profile_count = front_end_total_profile_count;
    config.front_end_visible_character_slots_by_profile =
        front_end_visible_character_slots_by_profile;
    config.front_end_total_character_counts_by_profile =
        front_end_total_character_counts_by_profile;
    config.front_end_capabilities.can_create_first_profile =
        front_end_visible_profile_slots == 0U &&
        front_end_total_profile_count == 0U;
    // This path always follows a capture from a persistence-backed app, so the
    // replay advertises diagnostic-start support and mirrors its independent
    // confirmation requirement. The replay-local mirror stays closed here for
    // the same reason the app's session activation does: startup never copies a
    // durable confirmation, so only a replayed selection can open it.
    config.front_end_capabilities.can_start_diagnostic_campaign = true;
    config.front_end_capabilities.requires_active_profile_for_diagnostic_play = true;
    config.front_end_capabilities.supports_character_selection = true;
    config.front_end_capabilities.can_create_first_character = true;
    config.front_end_capabilities.requires_active_character_for_diagnostic_play = true;
    config.initial_front_end_state = omega::app::PlanProjectFrontEndStartupState(
        static_cast<std::uint16_t>(front_end_total_profile_count),
        front_end_visible_profile_slots, config.front_end_capabilities);
    auto created = omega::app::RunReplaySession::Create(std::move(*traces), config);
    if (!created)
    {
        PrintRunReplayError(created.error());
        return false;
    }
    omega::app::RunReplaySession replay = std::move(*created);

    std::uint64_t replayed_frames = 0U;
    std::uint64_t planned_steps = 0U;
    std::uint64_t clamped_frames = 0U;
    std::uint64_t dropped_frames = 0U;
    while (replay.state() == omega::app::RunReplaySessionState::Ready)
    {
        auto frame = replay.Next();
        if (!frame)
        {
            PrintRunReplayError(frame.error());
            return false;
        }
        const auto plan = frame->frame_plan();
        if (!frame->elapsed() || frame->terminal_input() || !plan)
        {
            std::cerr << "runtime capture replay: normal frame shape is invalid\n";
            return false;
        }
        if (planned_steps > std::numeric_limits<std::uint64_t>::max() -
                                plan->simulation_steps)
        {
            std::cerr << "runtime capture replay: aggregate step count exhausted\n";
            return false;
        }
        ++replayed_frames;
        planned_steps += plan->simulation_steps;
        clamped_frames += plan->clamped_delta ? 1U : 0U;
        dropped_frames += plan->dropped_time ? 1U : 0U;
    }

    if (replay.state() != omega::app::RunReplaySessionState::Complete ||
        replay.remaining_frames() != 0U)
    {
        std::cerr << "runtime capture replay: replay session did not complete\n";
        return false;
    }
    const auto scheduler = replay.scheduler_state();
    const auto simulation = replay.simulation_state();
    const auto replay_debug_position = replay.debug_locomotion_position();
    const auto replay_diagnostic_target =
        replay.diagnostic_target_fire_state();
    const auto replay_diagnostic_proximity =
        replay.diagnostic_proximity_trigger_state();
    const auto replay_diagnostic_mission =
        replay.diagnostic_mission_lifecycle_state();
    const auto replay_front_end = replay.front_end_state();
    if (!scheduler || !simulation || !replay_front_end)
    {
        std::cerr << "runtime capture replay: final owner snapshots are unavailable\n";
        return false;
    }
    if (replayed_frames != capture_result.input_frames ||
        planned_steps != capture_result.planned_simulation_steps ||
        planned_steps != capture_result.executed_simulation_steps ||
        clamped_frames != capture_result.clamped_frame_count ||
        dropped_frames != capture_result.dropped_time_frame_count)
    {
        std::cerr << "runtime capture replay: capture and replay aggregates differ\n";
        return false;
    }
    if (*scheduler != capture_after)
    {
        std::cerr << "runtime capture replay: final scheduler states differ\n";
        return false;
    }
    const std::optional<omega::gameplay::DiagnosticTargetFireState>
        expected_diagnostic_target = config.enable_debug_target_fire
        ? std::optional<omega::gameplay::DiagnosticTargetFireState>{
              capture_diagnostic_target_fire_state}
        : std::nullopt;
    if (replay_diagnostic_target != expected_diagnostic_target)
    {
        std::cerr << "runtime capture replay: final diagnostic target states differ\n";
        return false;
    }
    const std::optional<omega::gameplay::DiagnosticProximityTriggerState>
        expected_diagnostic_proximity = config.enable_debug_locomotion
        ? std::optional<omega::gameplay::DiagnosticProximityTriggerState>{
              capture_diagnostic_proximity_trigger_state}
        : std::nullopt;
    if (!SameDiagnosticProximityState(
            replay_diagnostic_proximity, expected_diagnostic_proximity))
    {
        std::cerr << "runtime capture replay: final diagnostic proximity states differ\n";
        return false;
    }
    const std::optional<omega::gameplay::DiagnosticMissionLifecycleState>
        expected_diagnostic_mission = config.enable_debug_mission_lifecycle
        ? std::optional<omega::gameplay::DiagnosticMissionLifecycleState>{
              capture_diagnostic_mission_lifecycle_state}
        : std::nullopt;
    if (replay_diagnostic_mission != expected_diagnostic_mission)
    {
        std::cerr << "runtime capture replay: final diagnostic mission states differ\n";
        return false;
    }
    const std::optional<omega::simulation::Position3>
        expected_diagnostic_actor_position = config.enable_debug_locomotion
        ? capture_diagnostic_actor_position
        : std::nullopt;
    if (replay_debug_position != expected_diagnostic_actor_position)
    {
        std::cerr << "runtime capture replay: final diagnostic actor positions differ\n";
        return false;
    }
    if (*replay_front_end != capture_front_end_state)
    {
        std::cerr << "runtime capture replay: final front-end states differ\n";
        return false;
    }

    const std::int64_t step_count = capture_before.config.simulation_step.count();
    if (step_count <= 0 ||
        capture_result.executed_simulation_steps >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max() / step_count))
    {
        std::cerr << "runtime capture replay: simulated time is not representable\n";
        return false;
    }
    const std::chrono::nanoseconds expected_time{
        static_cast<std::int64_t>(capture_result.executed_simulation_steps) * step_count};
    const std::uint32_t expected_alive_entities =
        config.enable_debug_locomotion ? 1U : 0U;
    if (simulation->completed_steps != capture_result.executed_simulation_steps ||
        simulation->completed_steps != capture_result.planned_simulation_steps ||
        simulation->simulated_time != expected_time ||
        simulation->alive_entities != expected_alive_entities ||
        replay_debug_position.has_value() != config.enable_debug_locomotion)
    {
        std::cerr << "runtime capture replay: fresh simulation state differs\n";
        return false;
    }

    std::cout << "OpenOmega fresh replay: replayed_frames=" << replayed_frames
              << " planned_simulation_steps=" << planned_steps
              << " completed_simulation_steps=" << simulation->completed_steps
              << " clamped_frames=" << clamped_frames
              << " dropped_frames=" << dropped_frames
              << " front_end=" << ReplayFrontEndModeName(replay_front_end->mode)
              << '/' << ReplayFrontEndMainRowName(
                            replay_front_end->selected_main_row)
              << '/' << ReplayFrontEndProfileSlotName(
                            replay_front_end->selected_profile_slot)
              << " completion=complete\n";
    return true;
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

    std::optional<std::filesystem::path> default_profile_path;
    if (!options->config_path)
    {
        auto discovered_profile_path = CaptureDefaultRuntimeConfigPath();
        if (!discovered_profile_path)
        {
            PrintRuntimeConfigurationError(discovered_profile_path.error());
            return EXIT_FAILURE;
        }
        default_profile_path = std::move(*discovered_profile_path);
    }

    auto config = options->config_path
                      ? omega::runtime::LoadRuntimeConfig(*options)
                      : omega::runtime::LoadRuntimeConfig(*options, default_profile_path);
    if (!config)
    {
        PrintRuntimeConfigurationError(config.error());
        return EXIT_FAILURE;
    }
    auto settings = omega::runtime::ResolveRuntimeSettings(*config);
    if (!settings)
    {
        PrintRuntimeSettingsError(settings.error());
        return EXIT_FAILURE;
    }

    auto content_profile = omega::runtime::ResolveContentLaunchProfile(*options, *config);
    if (!content_profile)
    {
        PrintContentLaunchProfileError(content_profile.error());
        return EXIT_FAILURE;
    }
    options->data_root.reset();
    options->level_code.reset();
    if (content_profile->has_value())
    {
        options->data_root = (*content_profile)->data_root;
        options->level_code = (*content_profile)->level_code;
        if (!options->opening_movie_path && !options->opening_movie_member)
        {
            options->opening_movie_member =
                (*content_profile)->opening_movie_member;
        }
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

    if (options->probe_only)
    {
        std::cout << "OpenOmega native shell: rendered_frames=0\n";
        return EXIT_SUCCESS;
    }

    auto native_save_path = CaptureDefaultNativeSavePath();
    if (!native_save_path)
    {
        PrintNativePersistencePathError(native_save_path.error());
        return EXIT_FAILURE;
    }
    auto native_persistence =
        omega::app::NativePersistence::Bootstrap(std::move(*native_save_path));
    if (!native_persistence)
    {
        PrintNativePersistenceError(native_persistence.error());
        return EXIT_FAILURE;
    }
    const std::size_t startup_profile_count = native_persistence->startup_profiles().size();
    const std::uint8_t front_end_visible_profile_slots = static_cast<std::uint8_t>(
        std::min(startup_profile_count, omega::app::kFrontEndVisibleProfiles));
    std::array<std::uint8_t, omega::app::kFrontEndVisibleProfiles>
        front_end_visible_character_slots_by_profile{};
    std::array<std::size_t, omega::app::kFrontEndVisibleProfiles>
        front_end_total_character_counts_by_profile{};
    if (options->capture_run && options->replay_capture)
    {
        const auto startup_profiles = native_persistence->startup_profiles();
        for (std::size_t slot = 0U; slot < front_end_visible_profile_slots; ++slot)
        {
            auto characters = native_persistence->characters().ListBounded(
                startup_profiles[slot].id, omega::app::kFrontEndMaximumCharacters);
            if (!characters)
            {
                std::cerr << "runtime capture replay: character snapshot unavailable ["
                          << omega::profiles::CharacterCatalogErrorCodeName(
                                 characters.error().code)
                          << "]\n";
                return EXIT_FAILURE;
            }
            front_end_total_character_counts_by_profile[slot] = characters->size();
            front_end_visible_character_slots_by_profile[slot] =
                static_cast<std::uint8_t>(std::min(
                    characters->size(), omega::app::kFrontEndVisibleCharacters));
        }
    }
    std::cout << "OpenOmega native persistence: profiles=" << startup_profile_count << '\n';

    if (options->frame_limit == 0)
    {
        std::cout << "OpenOmega native shell: rendered_frames=0\n";
        return EXIT_SUCCESS;
    }

#if defined(OMEGA_GPU_DEBUG)
    constexpr bool debug_device = true;
#else
    constexpr bool debug_device = false;
#endif
    if (options->front_end_presentation_mode ==
        omega::runtime::FrontEndPresentationMode::DeveloperDiagnostics)
    {
        std::cout << "OpenOmega mode: DEVELOPER DIAGNOSTICS "
                     "(project-authored presentation/gameplay)\n";
    }
    else
    {
        std::cout << "OpenOmega mode: retail game-data presentation required\n";
    }
    std::optional<omega::asset::OpeningMovieSource> opening_movie_source;
    if (options->opening_movie_member)
    {
        if (!content.game_data)
        {
            std::cerr << "opening movie member: "
                      << omega::content::GameDataErrorCodeName(
                             omega::content::GameDataErrorCode::MissingRequiredFile)
                      << '\n';
        }
        else
        {
            auto loaded = content.game_data->LoadOpeningMovieSource(
                *options->opening_movie_member);
            if (!loaded)
            {
                std::cerr << "opening movie member: "
                          << omega::content::GameDataErrorCodeName(loaded.error().code)
                          << '\n';
            }
            else
            {
                opening_movie_source.emplace(std::move(*loaded));
            }
        }
    }

    auto app = [&]() -> std::expected<omega::app::OmegaApp, std::string> {
        if (opening_movie_source)
        {
            return omega::app::OmegaApp::Create(std::move(*config), *settings,
                std::move(content), std::move(*native_persistence), debug_device,
                options->front_end_presentation_mode,
                std::move(*opening_movie_source));
        }
        return omega::app::OmegaApp::Create(std::move(*config), *settings,
            std::move(content), std::move(*native_persistence), debug_device,
            options->front_end_presentation_mode,
            std::move(options->opening_movie_path));
    }();
    if (!app)
    {
        PrintApplicationStartupError(app.error());
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
        if (!options->replay_capture)
            return EXIT_SUCCESS;

        const omega::runtime::FrameSchedulerState capture_before =
            outcome.scheduler_state_before();
        const omega::runtime::FrameSchedulerState capture_after =
            outcome.scheduler_state_after();
        if (!IsZeroOriginScheduler(capture_before))
        {
            std::cerr << "runtime capture replay: capture scheduler did not start at zero\n";
            return EXIT_FAILURE;
        }
        if (!ReplayFreshCapture(std::move(*capture), capture_result,
                capture_before, capture_after,
                app->diagnostic_target_fire_state(),
                app->diagnostic_mission_lifecycle_state(),
                app->diagnostic_proximity_trigger_state(),
                app->diagnostic_actor_position(), app->front_end_state(),
                startup_profile_count,
                front_end_visible_profile_slots,
                front_end_visible_character_slots_by_profile,
                front_end_total_character_counts_by_profile))
        {
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
