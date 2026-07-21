#include "omega_app.h"
#include "diagnostic_actor_marker.h"
#include "opening_movie_player.h"
#include "opening_movie_safety.h"
#include "run_replay_session.h"

#include "omega/gameplay/debug_locomotion.h"
#include "omega/runtime/level_texture_topology_preview.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace omega::app
{
namespace
{
// Refill when the 4,096-frame ring has at least this much space. The queued lead therefore stays
// between roughly 53 and 85 ms at 48 kHz during steady playback.
constexpr std::uint64_t kOpeningMovieAudioRefillFrames = 1'536U;
static_assert(kOpeningMovieAudioRefillFrames <=
              SdlAudioService::kOpeningMovieQueueCapacityFrames);
static_assert(kOpeningMovieAudioClockRateHz ==
              static_cast<std::uint64_t>(SdlAudioService::kSampleRate));
constexpr profiles::ProfileId kFirstProfileId = profiles::ProfileId::FromBytes(
    std::array<std::uint8_t, 16U>{0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U,
                                   0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U});
constexpr std::array<std::byte, 4U> kDiagnosticActorMarkerRgba8{
    std::byte{255U}, std::byte{64U}, std::byte{224U}, std::byte{255U}};
} // namespace

std::expected<OmegaApp, std::string> OmegaApp::Create(runtime::ConfigStore config,
    const runtime::RuntimeSettings& settings, runtime::ContentStartupState content,
    NativePersistence native_persistence, const bool debug_device,
    std::optional<std::filesystem::path> opening_movie_path)
{
    return CreateWithTextureConfig(std::move(config), settings, std::move(content),
        std::make_unique<NativePersistence>(std::move(native_persistence)),
        debug_device, {}, std::move(opening_movie_path));
}

std::expected<OmegaApp, std::string> OmegaApp::CreateWithTextureConfig(
    runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
    runtime::ContentStartupState content,
    std::unique_ptr<NativePersistence> native_persistence, const bool debug_device,
    const runtime::RenderTexturePoolConfig texture_config,
    std::optional<std::filesystem::path> opening_movie_path)
{
    return CreateWithTextureConfigAndOpeningMoviePlayback(std::move(config),
        settings, std::move(content), std::move(native_persistence), debug_device,
        texture_config, std::move(opening_movie_path), nullptr);
}

std::expected<OmegaApp, std::string>
OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
    runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
    runtime::ContentStartupState content,
    std::unique_ptr<NativePersistence> native_persistence, const bool debug_device,
    const runtime::RenderTexturePoolConfig texture_config,
    std::optional<std::filesystem::path> opening_movie_path,
    std::unique_ptr<OpeningMoviePlayback> opening_movie_playback)
{
    if (opening_movie_path && opening_movie_playback)
    {
        return std::unexpected(
            std::string{"opening movie source selection is ambiguous"});
    }

    const auto classified_content = runtime::ClassifyContentStartupState(content);
    if (!classified_content)
    {
        return std::unexpected(
            std::string("content startup state: inconsistent-ownership"));
    }
    const runtime::ContentStartupStage content_stage = *classified_content;

    FrontEndStartupModel front_end_startup_model{};
    if (native_persistence != nullptr)
    {
        const auto projected = MakeFrontEndStartupModel(native_persistence->startup_profiles());
        if (!projected)
        {
            return std::unexpected("front-end startup model: " +
                                   std::string(FrontEndModelErrorMessage(projected.error())));
        }
        front_end_startup_model = *projected;
    }

    auto config_owner = std::make_unique<runtime::ConfigStore>(std::move(config));
    auto content_owner = std::make_unique<runtime::ContentStartupState>(std::move(content));
    auto stderr_sink = std::make_unique<runtime::StderrLogSink>();

    auto created_ring = runtime::RingLogSink::Create(settings.log_ring_capacity);
    if (!created_ring)
        return std::unexpected("log ring: " + created_ring.error());
    auto ring_sink = std::move(*created_ring);

    auto created_log = runtime::LogService::Create(runtime::LogServiceConfig{
        .minimum_severity = settings.minimum_log_severity,
        .max_category_bytes = 32U,
        .max_message_bytes = 512U,
        .sinks = {stderr_sink.get(), ring_sink.get()},
    });
    if (!created_log)
        return std::unexpected("logging service: " + created_log.error());
    auto log = std::make_unique<runtime::LogService>(std::move(*created_log));

    auto created_jobs = runtime::JobService::Create(settings.jobs);
    if (!created_jobs)
    {
        log->Error("startup", "job service: " + created_jobs.error());
        return std::unexpected("job service: " + created_jobs.error());
    }
    auto jobs = std::make_unique<runtime::JobService>(std::move(*created_jobs));

    std::unique_ptr<runtime::AssetService> assets;
    if (content_owner->level_texture_store)
    {
        if (!content_owner->game_data)
        {
            constexpr std::string_view error =
                "asset service requires an available game-data service";
            log->Error("startup", error);
            return std::unexpected(std::string(error));
        }

        auto created_assets = runtime::AssetService::Create(
            *jobs, *content_owner->game_data, *content_owner->level_texture_store);
        if (!created_assets)
        {
            const std::string error = "asset service: " + std::string(
                runtime::AssetServiceErrorCodeName(created_assets.error().code));
            log->Error("startup", error);
            return std::unexpected(error);
        }
        assets = std::move(*created_assets);
    }

    auto created_scheduler = runtime::FrameScheduler::Create(settings.frame);
    if (!created_scheduler)
    {
        log->Error("startup", "frame scheduler: " + created_scheduler.error());
        return std::unexpected("frame scheduler: " + created_scheduler.error());
    }
    auto frame_scheduler =
        std::make_unique<runtime::FrameScheduler>(std::move(*created_scheduler));

    constexpr std::array bindings{
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_ESCAPE),
            .action = kQuitAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_BACK),
            .action = kQuitAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_BACKSPACE),
            .action = kFrontEndCancelAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_EAST),
            .action = kFrontEndCancelAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_F1),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_RETURN),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_KP_ENTER),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_START),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_SOUTH),
            .action = kFrontEndPrimaryAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_W),
            .action = kDebugMoveForwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_UP),
            .action = kDebugMoveForwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_UP),
            .action = kDebugMoveForwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_S),
            .action = kDebugMoveBackwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_DOWN),
            .action = kDebugMoveBackwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
            .action = kDebugMoveBackwardAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_A),
            .action = kDebugMoveLeftAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_LEFT),
            .action = kDebugMoveLeftAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_D),
            .action = kDebugMoveRightAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
            .action = kDebugMoveRightAction,
        },
    };
    auto binding_table = runtime::InputBindingTable::FromBindings(bindings);
    if (!binding_table)
    {
        log->Error("startup", "input bindings: " + binding_table.error());
        return std::unexpected("input bindings: " + binding_table.error());
    }
    auto created_input = runtime::InputTracker::Create(
        std::move(*binding_table), settings.max_input_events_per_frame);
    if (!created_input)
    {
        log->Error("startup", "input tracker: " + created_input.error());
        return std::unexpected("input tracker: " + created_input.error());
    }
    auto input = std::make_unique<runtime::InputTracker>(std::move(*created_input));

    auto created_simulation = simulation::SimulationWorld::Create(
        {.fixed_step = settings.frame.simulation_step});
    if (!created_simulation)
    {
        log->Error("startup", "simulation world: " + created_simulation.error());
        return std::unexpected("simulation world: " + created_simulation.error());
    }
    auto simulation =
        std::make_unique<simulation::SimulationWorld>(std::move(*created_simulation));
    auto created_debug_locomotion_entity =
        simulation->CreatePositionedEntity(simulation::Position3{});
    if (!created_debug_locomotion_entity)
    {
        constexpr std::string_view error =
            "debug locomotion positioned entity creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    const simulation::EntityId debug_locomotion_entity =
        *created_debug_locomotion_entity;

    runtime::DebugImage no_level_diagnostic_image;
    if (!content_owner->debug_image)
        no_level_diagnostic_image = BuildProjectFrontEndDiagnosticPlayImage();
    const runtime::DebugImage &diagnostic_image =
        content_owner->debug_image ? *content_owner->debug_image : no_level_diagnostic_image;
    const runtime::DebugImage diagnostic_actor_marker_image{
        .width = 1U,
        .height = 1U,
        .rgba8_pixels = std::vector<std::byte>(
            kDiagnosticActorMarkerRgba8.begin(), kDiagnosticActorMarkerRgba8.end()),
    };

    runtime::DebugImage asset_topology_image;
    std::optional<runtime::DebugImage> asset_transfer_image;
    if (content_stage == runtime::ContentStartupStage::LevelContent)
    {
        auto built_asset_preview = runtime::BuildFirstLevelTextureDiagnosticPreview(
            *assets, *content_owner->level_texture_store);
        if (!built_asset_preview)
        {
            const std::string error(built_asset_preview.error().message);
            log->Error("startup", error);
            return std::unexpected(error);
        }
        asset_topology_image = std::move(built_asset_preview->topology_image);
        asset_transfer_image =
            std::move(built_asset_preview->packed24_transfer_image);
        if (built_asset_preview->packed24_transfer_error_code)
        {
            const auto rejection =
                *built_asset_preview->packed24_transfer_error_code;
            log->Info("startup", "packed-24 transfer diagnostic unavailable: " +
                                     std::string(
                                         runtime::Packed24TransferDebugImageErrorCodeName(
                                             rejection)));
        }
    }
    else
    {
        auto built_asset_topology = BuildProjectFrontEndAssetTopologyImage();
        if (!built_asset_topology)
        {
            const std::string error = "project diagnostic asset topology image: " +
                                      std::string(
                                          runtime::TextureStorageTopologyDebugImageErrorCodeName(
                                              built_asset_topology.error().code));
            log->Error("startup", error);
            return std::unexpected(error);
        }
        asset_topology_image = std::move(*built_asset_topology);
    }

    auto created_platform = SdlPlatformService::Create();
    if (!created_platform)
    {
        log->Error("startup", "SDL platform service: " + created_platform.error());
        return std::unexpected("SDL platform service: " + created_platform.error());
    }
    auto platform = std::make_unique<SdlPlatformService>(std::move(*created_platform));

    auto created_sdl_input = SdlInputService::Create(*platform);
    if (!created_sdl_input)
    {
        log->Error("startup", "SDL input service: " + created_sdl_input.error());
        return std::unexpected("SDL input service: " + created_sdl_input.error());
    }
    auto sdl_input = std::make_unique<SdlInputService>(std::move(*created_sdl_input));

    auto created_audio = SdlAudioService::Create(*platform);
    if (!created_audio)
    {
        log->Error("startup", "SDL audio service: " + created_audio.error());
        return std::unexpected("SDL audio service: " + created_audio.error());
    }
    auto audio = std::make_unique<SdlAudioService>(std::move(*created_audio));

    auto created_host = SdlGpuHost::Create(*platform, debug_device, texture_config);
    if (!created_host)
    {
        log->Error("startup", "SDL/GPU host: " + created_host.error());
        return std::unexpected(created_host.error());
    }
    auto host = std::make_unique<SdlGpuHost>(std::move(*created_host));
    // Adopt an injected test source immediately after the host exists so every later startup
    // failure destroys playback before the GPU and process-global SDL owners.
    std::unique_ptr<OpeningMoviePlayback> opening_movie_candidate =
        std::move(opening_movie_playback);

    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 full_target{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    constexpr runtime::RenderTargetRectQ16 menu_target{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    // Project-authored diagnostic split only. It assigns no retail layout or
    // texture semantics.
    constexpr runtime::RenderTargetRectQ16 asset_topology_split_target{
        .left = 2048U,
        .top = 2048U,
        .right = 13824U,
        .bottom = 15872U,
    };
    constexpr runtime::RenderTargetRectQ16 asset_transfer_target{
        .left = 14848U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    constexpr runtime::RenderSourceRectQ16 menu_selection_source{
        .left = 18432U,
        .top = 9103U,
        .right = 59392U,
        .bottom = 14563U,
    };
    // Project-owned solid cyan sample from the already-uploaded Profiles card
    // border. Startup draw lists stretch it into the three cursor markers.
    constexpr runtime::RenderSourceRectQ16 profile_selection_source{
        .left = 0U,
        .top = 0U,
        .right = 512U,
        .bottom = 512U,
    };
    constexpr std::array menu_selection_targets{
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 7424U,
            .right = 4352U,
            .bottom = 8960U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 9344U,
            .right = 4352U,
            .bottom = 10880U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 11264U,
            .right = 4352U,
            .bottom = 12800U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 13184U,
            .right = 4352U,
            .bottom = 14720U,
        },
    };
    static_assert(menu_selection_targets.size() == kFrontEndMainRowCount);
    static_assert(kFrontEndVisibleProfiles <= kFrontEndMainRowCount);

    runtime::RenderTextureHandle diagnostic_texture;
    runtime::RenderTextureHandle diagnostic_actor_marker_texture;
    runtime::RenderDrawList diagnostic_actor_draw_list;
    FrontEndPresentation front_end_presentation;
    std::optional<FrontEndPresentation> first_profile_presentation;
    runtime::RenderTextureHandle diagnostic_controls_texture;
    runtime::RenderTextureHandle diagnostic_asset_topology_texture;
    runtime::RenderTextureHandle diagnostic_asset_transfer_texture;
    constexpr std::size_t kDiagnosticBaseCommandCapacity = 1U;
    constexpr std::size_t kDiagnosticMaximumOverlayCommandCapacity = 2U;
    constexpr std::size_t kDiagnosticCommandCapacity =
        kDiagnosticBaseCommandCapacity + kDiagnosticMaximumOverlayCommandCapacity;
    static_assert(kDiagnosticCommandCapacity <=
                  runtime::kMaximumRenderTextureBlitsPerFrame);
    std::array<runtime::RenderTextureBlitCommand, kDiagnosticCommandCapacity>
        diagnostic_commands{};
    std::size_t diagnostic_command_count = 0U;
    auto uploaded = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = diagnostic_image.width,
        .height = diagnostic_image.height,
        .pixels = diagnostic_image.pixels(),
    });
    if (!uploaded)
    {
        const std::string error =
            "SDL/GPU diagnostic texture upload: " + uploaded.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }

    diagnostic_texture = *uploaded;
    diagnostic_commands[diagnostic_command_count++] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_texture,
            .source = full_source,
            .destination = full_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };

    auto created_hidden_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_command_count});
    if (!created_hidden_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic hidden draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_hidden_draw_list = std::move(*created_hidden_draw_list);
    const std::size_t diagnostic_base_command_count = diagnostic_command_count;

    const auto build_front_end_presentation =
        [&](const FrontEndStartupModel& model,
            const FrontEndCapabilities capabilities)
        -> std::expected<FrontEndPresentation, std::string>
    {
        FrontEndPresentation presentation;
        const runtime::DebugImage menu_image =
            BuildProjectFrontEndMainImage(content_stage, model.total_profiles);
        auto uploaded_menu = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
            .width = menu_image.width,
            .height = menu_image.height,
            .pixels = menu_image.pixels(),
        });
        if (!uploaded_menu)
        {
            return std::unexpected(
                "SDL/GPU front-end main texture upload: " + uploaded_menu.error());
        }
        presentation.main_texture = *uploaded_menu;
        diagnostic_commands[diagnostic_base_command_count] =
            runtime::RenderTextureBlitCommand{
                .texture = presentation.main_texture,
                .source = full_source,
                .destination = menu_target,
                .fit_mode = runtime::RenderTextureFitMode::Stretch,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };

        for (std::size_t row = 0U; row < menu_selection_targets.size(); ++row)
        {
            diagnostic_commands[diagnostic_base_command_count + 1U] =
                runtime::RenderTextureBlitCommand{
                    .texture = presentation.main_texture,
                    .source = menu_selection_source,
                    .destination = menu_selection_targets[row],
                    .fit_mode = runtime::RenderTextureFitMode::Stretch,
                    .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                };
            auto created_draw_list = runtime::RenderDrawList::Create(
                std::span<const runtime::RenderTextureBlitCommand>{
                    diagnostic_commands.data(), diagnostic_base_command_count + 2U});
            if (!created_draw_list)
            {
                return std::unexpected(
                    std::string{"SDL/GPU front-end main draw-list creation failed"});
            }
            presentation.main_draw_lists[row] = std::move(*created_draw_list);
        }

        const runtime::DebugImage profiles_image =
            BuildProjectFrontEndProfilesImage(model, capabilities);
        auto uploaded_profiles = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
            .width = profiles_image.width,
            .height = profiles_image.height,
            .pixels = profiles_image.pixels(),
        });
        if (!uploaded_profiles)
        {
            return std::unexpected(
                "SDL/GPU front-end profiles texture upload: " + uploaded_profiles.error());
        }
        presentation.profiles_texture = *uploaded_profiles;
        diagnostic_commands[diagnostic_base_command_count] =
            runtime::RenderTextureBlitCommand{
                .texture = presentation.profiles_texture,
                .source = full_source,
                .destination = menu_target,
                .fit_mode = runtime::RenderTextureFitMode::Stretch,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };
        auto created_profiles_draw_list = runtime::RenderDrawList::Create(
            std::span<const runtime::RenderTextureBlitCommand>{
                diagnostic_commands.data(), diagnostic_base_command_count + 1U});
        if (!created_profiles_draw_list)
        {
            return std::unexpected(
                std::string{"SDL/GPU front-end profiles draw-list creation failed"});
        }
        presentation.profiles_draw_list = std::move(*created_profiles_draw_list);

        for (std::size_t slot = 0U;
             slot < presentation.profile_selection_draw_lists.size(); ++slot)
        {
            diagnostic_commands[diagnostic_base_command_count + 1U] =
                runtime::RenderTextureBlitCommand{
                    .texture = presentation.profiles_texture,
                    .source = profile_selection_source,
                    .destination = menu_selection_targets[slot],
                    .fit_mode = runtime::RenderTextureFitMode::Stretch,
                    .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                };
            auto created_draw_list = runtime::RenderDrawList::Create(
                std::span<const runtime::RenderTextureBlitCommand>{
                    diagnostic_commands.data(), diagnostic_base_command_count + 2U});
            if (!created_draw_list)
            {
                return std::unexpected(std::string{
                    "SDL/GPU front-end profile selection draw-list creation failed"});
            }
            presentation.profile_selection_draw_lists[slot] =
                std::move(*created_draw_list);
        }
        return presentation;
    };

    const bool can_create_first_profile = native_persistence != nullptr &&
        front_end_startup_model.total_profiles == 0U;
    auto created_front_end_presentation = build_front_end_presentation(
        front_end_startup_model,
        FrontEndCapabilities{.can_create_first_profile = can_create_first_profile});
    if (!created_front_end_presentation)
    {
        log->Error("startup", created_front_end_presentation.error());
        return std::unexpected(std::move(created_front_end_presentation.error()));
    }
    front_end_presentation = std::move(*created_front_end_presentation);

    if (can_create_first_profile)
    {
        const std::array preview_profiles{
            profiles::ProfileSummary{
                .id = kFirstProfileId,
                .metadata = profiles::ProfileMetadata{
                    .display_name = std::string{kFrontEndFirstProfileDisplayName},
                    .created_unix_milliseconds = 0U,
                    .modified_unix_milliseconds = 0U,
                },
                .metadata_revision = 1U,
            },
        };
        const auto preview_model = MakeFrontEndStartupModel(preview_profiles);
        if (!preview_model)
        {
            constexpr std::string_view error =
                "front-end first-profile preview model creation failed";
            log->Error("startup", error);
            return std::unexpected(std::string{error});
        }
        auto created_first_profile_presentation =
            build_front_end_presentation(*preview_model, {});
        if (!created_first_profile_presentation)
        {
            log->Error("startup", created_first_profile_presentation.error());
            return std::unexpected(
                std::move(created_first_profile_presentation.error()));
        }
        first_profile_presentation =
            std::move(*created_first_profile_presentation);
    }

    const runtime::DebugImage controls_image = BuildProjectFrontEndControlsImage();
    auto uploaded_controls = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = controls_image.width,
        .height = controls_image.height,
        .pixels = controls_image.pixels(),
    });
    if (!uploaded_controls)
    {
        const std::string error =
            "SDL/GPU diagnostic controls texture upload: " + uploaded_controls.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_controls_texture = *uploaded_controls;
    diagnostic_commands[diagnostic_base_command_count] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_controls_texture,
            .source = full_source,
            .destination = menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    auto created_controls_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_base_command_count + 1U});
    if (!created_controls_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic controls draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_controls_draw_list = std::move(*created_controls_draw_list);

    auto uploaded_asset_topology = host->UploadRgba8Texture(
        runtime::Rgba8TextureUploadView{
            .width = asset_topology_image.width,
            .height = asset_topology_image.height,
            .pixels = asset_topology_image.pixels(),
        });
    if (!uploaded_asset_topology)
    {
        const std::string error =
            "SDL/GPU diagnostic asset topology texture upload: " +
            uploaded_asset_topology.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_asset_topology_texture = *uploaded_asset_topology;

    auto uploaded_actor_marker = host->UploadRgba8Texture(
        runtime::Rgba8TextureUploadView{
            .width = diagnostic_actor_marker_image.width,
            .height = diagnostic_actor_marker_image.height,
            .pixels = diagnostic_actor_marker_image.pixels(),
        });
    if (!uploaded_actor_marker)
    {
        const std::string error =
            "SDL/GPU diagnostic actor marker texture upload: " +
            uploaded_actor_marker.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_actor_marker_texture = *uploaded_actor_marker;
    if (diagnostic_hidden_draw_list.size() != 1U)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic actor draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    const std::array diagnostic_actor_commands{
        diagnostic_hidden_draw_list.commands().front(),
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_actor_marker_texture,
            .source = full_source,
            .destination = PlanProjectDiagnosticActorMarkerDestination(
                simulation::Position3{}),
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        },
    };
    auto created_actor_draw_list = runtime::RenderDrawList::Create(
        diagnostic_actor_commands);
    if (!created_actor_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic actor draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    diagnostic_actor_draw_list = std::move(*created_actor_draw_list);

    diagnostic_commands[diagnostic_base_command_count] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_asset_topology_texture,
            .source = full_source,
            .destination = asset_transfer_image
                ? asset_topology_split_target
                : menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    std::size_t diagnostic_asset_topology_command_count =
        diagnostic_base_command_count + 1U;
    if (asset_transfer_image)
    {
        auto uploaded_asset_transfer = host->UploadRgba8Texture(
            runtime::Rgba8TextureUploadView{
                .width = asset_transfer_image->width,
                .height = asset_transfer_image->height,
                .pixels = asset_transfer_image->pixels(),
            });
        if (!uploaded_asset_transfer)
        {
            log->Info("startup",
                "packed-24 transfer diagnostic unavailable: upload-failed");
        }
        else
        {
            diagnostic_asset_transfer_texture = *uploaded_asset_transfer;
            diagnostic_commands[diagnostic_asset_topology_command_count++] =
                runtime::RenderTextureBlitCommand{
                    .texture = diagnostic_asset_transfer_texture,
                    .source = full_source,
                    .destination = asset_transfer_target,
                    .fit_mode = runtime::RenderTextureFitMode::Contain,
                    .filter_mode = runtime::RenderTextureFilterMode::Nearest,
                };
        }
    }
    diagnostic_commands[diagnostic_base_command_count].destination =
        diagnostic_asset_transfer_texture.valid() ? asset_topology_split_target : menu_target;
    auto created_asset_topology_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_asset_topology_command_count});
    if (!created_asset_topology_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic asset topology draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_asset_topology_draw_list =
        std::move(*created_asset_topology_draw_list);

    std::unique_ptr<OpeningMoviePlayback> opening_movie_player;
    runtime::RenderTextureHandle opening_movie_texture;
    runtime::RenderDrawList opening_movie_draw_list;
    BootSequenceState boot_sequence_state{};
    if (opening_movie_path)
    {
        auto created_opening_movie = OpeningMoviePlayer::Create(*opening_movie_path);
        if (!created_opening_movie)
        {
            log->Warning("opening_movie",
                OpeningMoviePlayerErrorMessage(created_opening_movie.error().code));
        }
        else
        {
            opening_movie_candidate =
                std::make_unique<OpeningMoviePlayer>(std::move(*created_opening_movie));
        }
    }
    if (opening_movie_candidate)
    {
        const std::uint32_t movie_width = opening_movie_candidate->width();
        const std::uint32_t movie_height = opening_movie_candidate->height();
        const std::uint64_t safety_duration_ticks =
            opening_movie_candidate->safety_duration_ticks();
        const std::uint64_t logical_bytes =
            static_cast<std::uint64_t>(movie_width) * movie_height * 4U;
        if (movie_width == 0U || movie_height == 0U ||
            movie_width > media::kMaximumNv12FrameWidth ||
            movie_height > media::kMaximumNv12FrameHeight ||
            safety_duration_ticks == 0U ||
            safety_duration_ticks > kOpeningMovieMaximumSafetyTicks ||
            logical_bytes > static_cast<std::uint64_t>(
                                std::numeric_limits<std::size_t>::max()))
        {
            log->Warning("opening_movie",
                "opening movie presentation rejected invalid metadata");
        }
        else
        {
            std::vector<std::byte> black_frame(
                static_cast<std::size_t>(logical_bytes), std::byte{0});
            for (std::size_t alpha = 3U; alpha < black_frame.size(); alpha += 4U)
                black_frame[alpha] = std::byte{255};

            auto uploaded_opening_movie =
                host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
                    .width = movie_width,
                    .height = movie_height,
                    .pixels = black_frame,
                });
            if (!uploaded_opening_movie)
            {
                log->Warning("opening_movie",
                    "opening movie presentation texture upload failed");
            }
            else
            {
                constexpr runtime::RenderTargetRectQ16 movie_target{
                    .left = 0U,
                    .top = 0U,
                    .right = runtime::kNormalizedRenderExtent,
                    .bottom = runtime::kNormalizedRenderExtent,
                };
                const std::array movie_commands{
                    runtime::RenderTextureBlitCommand{
                        .texture = *uploaded_opening_movie,
                        .source = full_source,
                        .destination = movie_target,
                        .fit_mode = runtime::RenderTextureFitMode::Contain,
                        .filter_mode = runtime::RenderTextureFilterMode::Linear,
                    },
                };
                auto created_opening_movie_draw_list =
                    runtime::RenderDrawList::Create(movie_commands);
                if (!created_opening_movie_draw_list)
                {
                    log->Warning("opening_movie",
                        "opening movie presentation draw-list creation failed");
                    static_cast<void>(host->ReleaseTexture(*uploaded_opening_movie));
                }
                else
                {
                    opening_movie_texture = *uploaded_opening_movie;
                    opening_movie_draw_list =
                        std::move(*created_opening_movie_draw_list);
                    boot_sequence_state = InitialBootSequenceState(
                        BootSequenceConfig{
                            .duration_ticks = safety_duration_ticks,
                            .source_available = true,
                        });
                    opening_movie_player = std::move(opening_movie_candidate);
                    log->Info("opening_movie",
                        "opening movie presentation is ready");
                }
            }
        }
    }

    log->Info("startup", "runtime services ready with " +
                             std::to_string(jobs->worker_count()) + " workers and " +
                             std::string(audio->driver_name()) + " audio");

    return OmegaApp(std::move(native_persistence), std::move(config_owner), std::move(content_owner),
                    std::move(stderr_sink), std::move(ring_sink), std::move(log), std::move(jobs), std::move(assets),
                    std::move(frame_scheduler), std::move(input), std::move(simulation), debug_locomotion_entity,
                    std::move(platform), std::move(sdl_input), std::move(audio), std::move(host),
                    std::move(opening_movie_player), opening_movie_texture,
                    std::move(opening_movie_draw_list), boot_sequence_state, diagnostic_texture,
                    diagnostic_actor_marker_texture,
                    std::move(diagnostic_actor_draw_list),
                    std::move(front_end_presentation),
                    std::move(first_profile_presentation), diagnostic_controls_texture,
                    diagnostic_asset_topology_texture, diagnostic_asset_transfer_texture,
                    std::move(diagnostic_hidden_draw_list),
                    std::move(diagnostic_controls_draw_list),
                    std::move(diagnostic_asset_topology_draw_list), content_stage, front_end_startup_model);
}

OmegaApp::OmegaApp(
    std::unique_ptr<NativePersistence> native_persistence, std::unique_ptr<runtime::ConfigStore> config,
    std::unique_ptr<runtime::ContentStartupState> content, std::unique_ptr<runtime::StderrLogSink> stderr_sink,
    std::unique_ptr<runtime::RingLogSink> ring_sink, std::unique_ptr<runtime::LogService> log,
    std::unique_ptr<runtime::JobService> jobs, std::unique_ptr<runtime::AssetService> assets,
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler, std::unique_ptr<runtime::InputTracker> input,
    std::unique_ptr<simulation::SimulationWorld> simulation, const simulation::EntityId debug_locomotion_entity,
    std::unique_ptr<SdlPlatformService> platform, std::unique_ptr<SdlInputService> sdl_input,
    std::unique_ptr<SdlAudioService> audio, std::unique_ptr<SdlGpuHost> host,
    std::unique_ptr<OpeningMoviePlayback> opening_movie_player,
    const runtime::RenderTextureHandle opening_movie_texture,
    runtime::RenderDrawList opening_movie_draw_list,
    const BootSequenceState boot_sequence_state,
    const runtime::RenderTextureHandle diagnostic_texture,
    const runtime::RenderTextureHandle diagnostic_actor_marker_texture,
    runtime::RenderDrawList diagnostic_actor_draw_list,
    FrontEndPresentation front_end_presentation,
    std::optional<FrontEndPresentation> first_profile_presentation,
    const runtime::RenderTextureHandle diagnostic_controls_texture,
    const runtime::RenderTextureHandle diagnostic_asset_topology_texture,
    const runtime::RenderTextureHandle diagnostic_asset_transfer_texture,
    runtime::RenderDrawList diagnostic_hidden_draw_list,
    runtime::RenderDrawList diagnostic_controls_draw_list,
    runtime::RenderDrawList diagnostic_asset_topology_draw_list, const runtime::ContentStartupStage content_stage,
    const FrontEndStartupModel front_end_startup_model) noexcept
    : native_persistence_(std::move(native_persistence)), config_(std::move(config)), content_(std::move(content)),
      stderr_sink_(std::move(stderr_sink)), ring_sink_(std::move(ring_sink)), log_(std::move(log)),
      jobs_(std::move(jobs)), assets_(std::move(assets)), frame_scheduler_(std::move(frame_scheduler)),
      input_(std::move(input)), simulation_(std::move(simulation)), debug_locomotion_entity_(debug_locomotion_entity),
      platform_(std::move(platform)), sdl_input_(std::move(sdl_input)), audio_(std::move(audio)),
      host_(std::move(host)), opening_movie_player_(std::move(opening_movie_player)),
      opening_movie_texture_(opening_movie_texture),
      opening_movie_draw_list_(std::move(opening_movie_draw_list)),
      boot_sequence_state_(boot_sequence_state), diagnostic_texture_(diagnostic_texture),
      diagnostic_actor_marker_texture_(diagnostic_actor_marker_texture),
      diagnostic_actor_draw_list_(std::move(diagnostic_actor_draw_list)),
      front_end_presentation_(std::move(front_end_presentation)),
      first_profile_presentation_(std::move(first_profile_presentation)),
      diagnostic_controls_texture_(diagnostic_controls_texture),
      diagnostic_asset_topology_texture_(diagnostic_asset_topology_texture),
      diagnostic_asset_transfer_texture_(diagnostic_asset_transfer_texture),
      diagnostic_hidden_draw_list_(std::move(diagnostic_hidden_draw_list)),
      diagnostic_controls_draw_list_(std::move(diagnostic_controls_draw_list)),
      diagnostic_asset_topology_draw_list_(std::move(diagnostic_asset_topology_draw_list)),
      content_stage_(content_stage), front_end_startup_model_(front_end_startup_model),
      front_end_state_(InitialFrontEndState()),
      can_create_first_profile_(first_profile_presentation_.has_value())
{
}

OmegaApp::~OmegaApp() noexcept
{
    const bool opening_movie_audio_contained = ContainOpeningMovieAudio();
    if (!opening_movie_audio_contained && log_ != nullptr)
    {
        try
        {
            log_->Warning("shutdown",
                "opening movie audio containment failed; final SDL audio cleanup will continue");
        }
        catch (...)
        {
            // Destruction remains noexcept even if bounded shutdown logging
            // cannot allocate.
        }
    }
    opening_movie_draw_list_ = {};
    opening_movie_player_.reset();
    diagnostic_asset_topology_draw_list_ = {};
    diagnostic_controls_draw_list_ = {};
    diagnostic_actor_draw_list_ = {};
    const auto clear_front_end_draw_lists = [](FrontEndPresentation& presentation) noexcept
    {
        for (runtime::RenderDrawList& draw_list :
             presentation.profile_selection_draw_lists)
            draw_list = {};
        presentation.profiles_draw_list = {};
        for (runtime::RenderDrawList& draw_list : presentation.main_draw_lists)
            draw_list = {};
    };
    if (first_profile_presentation_)
        clear_front_end_draw_lists(*first_profile_presentation_);
    clear_front_end_draw_lists(front_end_presentation_);
    diagnostic_hidden_draw_list_ = {};

    const auto release_texture = [this](const runtime::RenderTextureHandle texture,
                                     const std::string_view failure_message) noexcept
    {
        if (host_ == nullptr || !texture.valid())
            return;

        bool release_failed = false;
        try
        {
            release_failed = !host_->ReleaseTexture(texture);
        }
        catch (...)
        {
            // The host remains the authoritative owner and releases all surviving
            // resources.
            release_failed = true;
        }

        if (release_failed && log_ != nullptr)
        {
            try
            {
                log_->Warning("shutdown", failure_message);
            }
            catch (...)
            {
                // Destruction remains noexcept even if bounded shutdown logging
                // cannot allocate.
            }
        }
    };

    release_texture(opening_movie_texture_, "opening movie texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_asset_transfer_texture_, "diagnostic asset transfer texture release failed; SDL/GPU "
                                                        "host cleanup will retry");
    release_texture(diagnostic_asset_topology_texture_, "diagnostic asset topology texture release failed; SDL/GPU "
                                                        "host cleanup will retry");
    release_texture(diagnostic_controls_texture_, "diagnostic controls texture release failed; SDL/GPU host "
                                                  "cleanup will retry");
    if (first_profile_presentation_)
    {
        release_texture(first_profile_presentation_->profiles_texture,
            "inactive front-end profiles texture release failed; SDL/GPU host cleanup will retry");
        release_texture(first_profile_presentation_->main_texture,
            "inactive front-end main texture release failed; SDL/GPU host cleanup will retry");
    }
    release_texture(front_end_presentation_.profiles_texture,
        "front-end profiles texture release failed; SDL/GPU host cleanup will retry");
    release_texture(front_end_presentation_.main_texture,
        "front-end main texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_actor_marker_texture_,
        "diagnostic actor marker texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_texture_, "diagnostic texture release failed; SDL/GPU host cleanup will retry");
    opening_movie_texture_ = {};
    diagnostic_asset_topology_texture_ = {};
    diagnostic_asset_transfer_texture_ = {};
    diagnostic_controls_texture_ = {};
    if (first_profile_presentation_)
    {
        first_profile_presentation_->profiles_texture = {};
        first_profile_presentation_->main_texture = {};
    }
    front_end_presentation_.profiles_texture = {};
    front_end_presentation_.main_texture = {};
    diagnostic_actor_marker_texture_ = {};
    diagnostic_texture_ = {};
}

bool OmegaApp::ContainOpeningMovieAudio() noexcept
{
    std::fill(opening_movie_audio_scratch_.begin(), opening_movie_audio_scratch_.end(),
        std::int16_t{0});
    opening_movie_audio_clock_ = {};
    return audio_ == nullptr || audio_->DiscardOpeningMovieAudio();
}

OpeningMovieAudioFaultCounters OmegaApp::OpeningMovieAudioFaultCountersOf(
    const AudioServiceSnapshot& snapshot) noexcept
{
    return OpeningMovieAudioFaultCounters{
        .callback_failures = snapshot.callback_failures,
        .opening_movie_control_failures = snapshot.opening_movie_control_failures,
        .opening_movie_underrun_frames = snapshot.opening_movie_underrun_frames,
        .opening_movie_queue_rejections = snapshot.opening_movie_queue_rejections,
    };
}

void OmegaApp::ReleaseOpeningMovieForFrontEnd()
{
    if (IsBootSequenceActive(boot_sequence_state_))
    {
        boot_sequence_state_ = BootSequenceState{
            .phase = BootSequencePhase::Failed,
            .position_ticks = boot_sequence_state_.position_ticks,
            .duration_ticks = boot_sequence_state_.duration_ticks,
        };
    }

    opening_movie_player_.reset();
    opening_movie_draw_list_ = {};
    if (!opening_movie_texture_.valid())
        return;

    auto released = host_->ReleaseTexture(opening_movie_texture_);
    if (!released)
    {
        log_->Warning("opening_movie",
            "opening movie texture release failed; host cleanup will retry");
        return;
    }
    opening_movie_texture_ = {};
}

bool OmegaApp::FinishOpeningMovieFrontEndTransition(
    AudioServiceSnapshot& audio_fault_baseline)
{
    const bool contained = ContainOpeningMovieAudio();
    if (audio_ != nullptr)
        audio_fault_baseline = audio_->Snapshot();
    ReleaseOpeningMovieForFrontEnd();
    return contained;
}

OmegaApp::OmegaApp(OmegaApp&&) noexcept = default;

std::expected<RunResult, std::string> OmegaApp::Run(const int frame_limit)
{
    auto first_elapsed_override =
        std::exchange(next_run_elapsed_override_for_testing_, std::nullopt);
    RunLoopResult loop =
        RunLoop(frame_limit, nullptr, std::move(first_elapsed_override));
    if (loop.operational_error)
        return std::unexpected(std::move(*loop.operational_error));
    return loop.result;
}

std::expected<RunCaptureOutcome, std::string> OmegaApp::RunWithCapture(
    const int frame_limit)
{
    auto first_elapsed_override =
        std::exchange(next_run_elapsed_override_for_testing_, std::nullopt);
    const auto planned = detail::PlanFiniteRunCapture(
        frame_limit, input_->next_frame_index());
    if (!planned)
    {
        return std::unexpected(std::string(
            detail::FiniteRunCapturePlanErrorMessage(planned.error())));
    }

    auto created = runtime::RunCaptureSession::Create(
        planned->session, input_->bindings().actions());
    if (!created)
        return std::unexpected(detail::FormatRunCaptureSessionError(created.error()));

    runtime::RunCaptureSession capture_session = std::move(*created);
    const runtime::FrameSchedulerState scheduler_state_before =
        frame_scheduler_->Snapshot();

    if (planned->requested_frames == 0U)
    {
        auto finished = std::move(capture_session).Finish();
        const runtime::FrameSchedulerState scheduler_state_after =
            frame_scheduler_->Snapshot();
        if (!finished)
        {
            return RunCaptureOutcome(planned->requested_frames, RunResult{},
                RunCaptureCompletion::CaptureFailure, scheduler_state_before,
                scheduler_state_after,
                detail::FormatRunCaptureSessionError(finished.error()), std::nullopt);
        }
        return RunCaptureOutcome(planned->requested_frames, RunResult{},
            RunCaptureCompletion::FrameLimitReached, scheduler_state_before,
            scheduler_state_after, std::nullopt,
            std::optional<runtime::RunCaptureTracePair>{
                std::in_place, std::move(*finished)});
    }

    RunLoopResult loop = RunLoop(
        frame_limit, &capture_session, std::move(first_elapsed_override));
    const runtime::FrameSchedulerState scheduler_state_after =
        frame_scheduler_->Snapshot();

    if (loop.capture_error)
    {
        std::optional<std::string> failure{
            std::in_place,
            detail::FormatRunCaptureSessionError(*loop.capture_error)};
        auto finished = std::move(capture_session).Finish();
        if (!finished)
        {
            return RunCaptureOutcome(planned->requested_frames, loop.result,
                RunCaptureCompletion::CaptureFailure, scheduler_state_before,
                scheduler_state_after, std::move(failure), std::nullopt);
        }
        return RunCaptureOutcome(planned->requested_frames, loop.result,
            RunCaptureCompletion::CaptureFailure, scheduler_state_before,
            scheduler_state_after, std::move(failure),
            std::optional<runtime::RunCaptureTracePair>{
                std::in_place, std::move(*finished)});
    }

    auto finished = std::move(capture_session).Finish();
    if (!finished)
    {
        return RunCaptureOutcome(planned->requested_frames, loop.result,
            RunCaptureCompletion::CaptureFailure, scheduler_state_before,
            scheduler_state_after,
            detail::FormatRunCaptureSessionError(finished.error()), std::nullopt);
    }

    RunCaptureCompletion completion = RunCaptureCompletion::FrameLimitReached;
    if (loop.operational_error)
        completion = RunCaptureCompletion::OperationalFailure;
    else if (loop.result.quit_requested)
        completion = RunCaptureCompletion::QuitRequested;

    return RunCaptureOutcome(planned->requested_frames, loop.result, completion,
        scheduler_state_before, scheduler_state_after,
        std::move(loop.operational_error),
        std::optional<runtime::RunCaptureTracePair>{
            std::in_place, std::move(*finished)});
}

OmegaApp::RunLoopResult OmegaApp::RunLoop(
    const int frame_limit, runtime::RunCaptureSession* const capture_session,
    std::optional<std::chrono::nanoseconds> first_elapsed_override)
{
    using Clock = std::chrono::steady_clock;

    log_->Info("runtime", "entering native host loop");
    RunResult result;
    bool running = true;
    auto previous_frame = Clock::now();
    AudioServiceSnapshot audio_fault_baseline = audio_->Snapshot();
    while (running && (frame_limit < 0 || result.rendered_frames < frame_limit))
    {
        const auto next_rendered_frame_count =
            detail::CheckedNextRenderedFrameCount(result.rendered_frames);
        if (!next_rendered_frame_count)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            constexpr std::string_view error =
                "run-local rendered frame counter exhausted";
            log_->Error("render", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }

        const InputPumpResult events = sdl_input_->PumpEvents(*input_, *log_);
        const runtime::InputSnapshot input_snapshot = input_->EndFrame();
        if (capture_session != nullptr)
        {
            const auto captured = capture_session->AppendInput(input_snapshot);
            if (!captured)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::nullopt,
                    .capture_error = captured.error(),
                };
            }
        }
        ++result.input_frames;
        if (input_snapshot.rejected_event_count() != 0U)
        {
            log_->Warning("input", "rejected " +
                                       std::to_string(input_snapshot.rejected_event_count()) +
                                       " host events in one frame");
        }
        if (capture_session != nullptr)
        {
            const bool host_quit_requested = events.quit_requested;
            const bool logical_quit_pressed = input_snapshot.WasPressed(kQuitAction);
            if (host_quit_requested || logical_quit_pressed)
            {
                const auto marked = capture_session->MarkTerminal(
                    host_quit_requested, logical_quit_pressed);
                if (!marked)
                {
                    (void)ContainOpeningMovieAudio();
                    jobs_->WaitForIdle();
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::nullopt,
                        .capture_error = marked.error(),
                    };
                }
                running = false;
                result.quit_requested = true;
                break;
            }
        }
        else if (events.quit_requested || input_snapshot.WasPressed(kQuitAction))
        {
            running = false;
            result.quit_requested = true;
            break;
        }

        const auto current_frame = Clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            current_frame - previous_frame);
        previous_frame = current_frame;
        if (first_elapsed_override)
        {
            elapsed = *first_elapsed_override;
            first_elapsed_override.reset();
        }
        if (capture_session != nullptr)
        {
            const auto captured = capture_session->AppendElapsed(elapsed);
            if (!captured)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::nullopt,
                    .capture_error = captured.error(),
                };
            }
        }

        const bool movie_was_active = IsBootSequenceActive(boot_sequence_state_);
        if (movie_was_active)
        {
            const bool primary_pressed =
                input_snapshot.WasPressed(kFrontEndPrimaryAction);
            bool source_failed = false;
            bool source_completed = false;
            if (!primary_pressed)
            {
                AudioServiceSnapshot movie_audio = audio_->Snapshot();
                const OpeningMovieAudioFault movie_audio_fault =
                    ClassifyOpeningMovieAudioFault(
                        OpeningMovieAudioFaultCountersOf(audio_fault_baseline),
                        OpeningMovieAudioFaultCountersOf(movie_audio));
                if (DisposeOpeningMovieAudioFault(movie_audio_fault, true) ==
                    OpeningMovieAudioFaultDisposition::FailOpen)
                {
                    log_->Warning("opening_movie",
                        OpeningMovieAudioFaultMessage(movie_audio_fault));
                    source_failed = true;
                }
                else if (opening_movie_player_ == nullptr)
                {
                    source_failed = true;
                }
                else
                {
                    std::chrono::nanoseconds presentation_elapsed{0};
                    const OpeningMovieAudioClockResult clock_step =
                        AdvanceOpeningMovieAudioClock(
                            opening_movie_audio_clock_,
                            OpeningMovieAudioClockObservation{
                                .session_generation =
                                    movie_audio.opening_movie_session_generation,
                                .timeline_frames =
                                    movie_audio.opening_movie_timeline_frames,
                            });
                    if (!clock_step)
                    {
                        log_->Warning("opening_movie",
                            OpeningMovieAudioClockErrorMessage(clock_step.error()));
                        source_failed = true;
                    }
                    else
                    {
                        opening_movie_audio_clock_ = clock_step->state;
                        presentation_elapsed = clock_step->presentation_elapsed;
                    }

                    if (!source_failed)
                    {
                        auto movie_update = opening_movie_player_->Advance(presentation_elapsed);
                        if (!movie_update)
                        {
                            log_->Warning("opening_movie",
                                OpeningMoviePlayerErrorMessage(movie_update.error().code));
                            source_failed = true;
                        }
                        else
                        {
                            const bool video_completed = movie_update->status ==
                                OpeningMoviePlayerStatus::Completed;
                            if (movie_update->frame_updated)
                            {
                                if (movie_update->current_frame == nullptr)
                                {
                                    log_->Warning("opening_movie",
                                        "opening movie published an empty frame update");
                                    source_failed = true;
                                }
                                else
                                {
                                    const media::Rgba8VideoFrame& frame =
                                        *movie_update->current_frame;
                                    auto updated = host_->UpdateRgba8Texture(
                                        opening_movie_texture_,
                                        runtime::Rgba8TextureUploadView{
                                            .width = frame.width,
                                            .height = frame.height,
                                            .pixels = frame.pixels,
                                        });
                                    if (!updated)
                                    {
                                        log_->Warning("opening_movie",
                                            "opening movie presentation texture update failed");
                                        source_failed = true;
                                    }
                                }
                            }

                            if (!source_failed && movie_update->current_frame != nullptr &&
                                !opening_movie_player_->audio_finished())
                            {
                                const std::uint64_t available_frames =
                                    audio_->OpeningMovieAvailableFrames();
                                if (available_frames >= kOpeningMovieAudioRefillFrames)
                                {
                                    const auto output = std::span<std::int16_t>(
                                        opening_movie_audio_scratch_)
                                                            .first(static_cast<std::size_t>(
                                                                available_frames *
                                                                SdlAudioService::kChannelCount));
                                    auto decoded =
                                        opening_movie_player_->ReadAudioFrames(output);
                                    if (!decoded)
                                    {
                                        log_->Warning("opening_movie",
                                            OpeningMoviePlayerErrorMessage(
                                                decoded.error().code));
                                        source_failed = true;
                                    }
                                    else if (*decoded > available_frames)
                                    {
                                        log_->Warning("opening_movie",
                                            "opening movie PCM decode exceeded the requested "
                                            "refill");
                                        source_failed = true;
                                    }
                                    else if (*decoded != 0U)
                                    {
                                        const auto samples = output.first(
                                            static_cast<std::size_t>(
                                                *decoded * SdlAudioService::kChannelCount));
                                        const AudioServiceSnapshot before_queue =
                                            audio_->Snapshot();
                                        if (!audio_->QueueOpeningMoviePcm16(samples,
                                                opening_movie_player_->audio_finished()))
                                        {
                                            log_->Warning("opening_movie",
                                                "opening movie audio queue rejected a bounded "
                                                "refill");
                                            source_failed = true;
                                        }
                                        else if (!opening_movie_audio_clock_.started)
                                        {
                                            const AudioServiceSnapshot after_queue =
                                                audio_->Snapshot();
                                            const OpeningMovieAudioClockResult clock_start =
                                                StartOpeningMovieAudioClock(
                                                    opening_movie_audio_clock_,
                                                    OpeningMovieAudioClockStartSignals{
                                                        .video_frame_available =
                                                            movie_update->current_frame != nullptr,
                                                        .pcm_queue_accepted = true,
                                                        .before_queue =
                                                            OpeningMovieAudioClockObservation{
                                                                .session_generation = before_queue
                                                                    .opening_movie_session_generation,
                                                                .timeline_frames = before_queue
                                                                    .opening_movie_timeline_frames,
                                                            },
                                                        .after_queue =
                                                            OpeningMovieAudioClockObservation{
                                                                .session_generation = after_queue
                                                                    .opening_movie_session_generation,
                                                                .timeline_frames = after_queue
                                                                    .opening_movie_timeline_frames,
                                                            },
                                                    });
                                            if (!clock_start)
                                            {
                                                log_->Warning("opening_movie",
                                                    OpeningMovieAudioClockErrorMessage(
                                                        clock_start.error()));
                                                source_failed = true;
                                            }
                                            else
                                            {
                                                opening_movie_audio_clock_ = clock_start->state;
                                            }
                                        }
                                    }
                                }
                            }

                            if (!source_failed && video_completed &&
                                opening_movie_player_->audio_finished())
                            {
                                const AudioServiceSnapshot audio = audio_->Snapshot();
                                source_completed = audio.opening_movie_queued_frames == 0U &&
                                    !audio.opening_movie_active;
                            }
                        }
                    }
                }
            }

            const auto elapsed_microseconds =
                std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
            const std::uint64_t elapsed_ticks = elapsed_microseconds > 0
                ? static_cast<std::uint64_t>(elapsed_microseconds)
                : 0U;
            const BootSequenceReduction boot = ReduceBootSequence(
                boot_sequence_state_,
                BootSequenceInput{
                    .elapsed_ticks = elapsed_ticks,
                    .primary_pressed = primary_pressed,
                    .source_failed = source_failed,
                    .source_completed = source_completed,
                });
            boot_sequence_state_ = boot.state;
            if (boot.entered_front_end)
            {
                if (boot.completion_cause == BootSequenceCompletionCause::SafetyTimeout)
                {
                    log_->Warning("opening_movie",
                        "opening movie safety timeout reached; entering front end");
                }
                const bool contained = FinishOpeningMovieFrontEndTransition(
                    audio_fault_baseline);
                if (!contained)
                {
                    jobs_->WaitForIdle();
                    constexpr std::string_view error =
                        "opening movie audio containment failed during the front-end transition";
                    log_->Error("opening_movie", error);
                    return RunLoopResult{
                        .result = result,
                        .operational_error = std::string(error),
                        .capture_error = std::nullopt,
                    };
                }
            }
        }
        else
        {
            const FrontEndReduction front_end =
                ReduceFrontEnd(front_end_state_,
                    FrontEndInputEdges{
                        .primary_pressed = input_snapshot.WasPressed(kFrontEndPrimaryAction),
                        .previous_pressed = input_snapshot.WasPressed(kFrontEndPreviousAction),
                        .next_pressed = input_snapshot.WasPressed(kFrontEndNextAction),
                        .cancel_pressed = input_snapshot.WasPressed(kFrontEndCancelAction),
                    },
                    front_end_startup_model_.visible_profiles,
                    FrontEndCapabilities{
                        .can_create_first_profile = can_create_first_profile_,
                    });
            front_end_state_ = front_end.state;
            auto applied = ApplyFrontEndCommand(front_end.command);
            if (!applied)
            {
                jobs_->WaitForIdle();
                log_->Error("profiles", applied.error());
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::move(applied.error()),
                    .capture_error = std::nullopt,
                };
            }
        }
        const bool simulation_allowed =
            !movie_was_active && FrontEndAllowsSimulation(front_end_state_);
        const std::chrono::nanoseconds effective_elapsed = simulation_allowed
            ? elapsed
            : std::chrono::nanoseconds::zero();
        const runtime::FramePlan plan = frame_scheduler_->BeginFrame(effective_elapsed);
        if (plan.simulation_steps >
            std::numeric_limits<std::uint64_t>::max() - result.planned_simulation_steps)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            constexpr std::string_view error = "run-local simulation step counter exhausted";
            log_->Error("simulation", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }
        result.planned_simulation_steps += plan.simulation_steps;
        if (plan.clamped_delta)
        {
            ++result.clamped_frame_count;
            if (result.clamped_frame_count == 1U)
                log_->Warning("frame", "wall-time delta reached the configured clamp");
        }
        if (plan.dropped_time)
        {
            ++result.dropped_time_frame_count;
            if (result.dropped_time_frame_count == 1U)
                log_->Warning(
                    "frame", "fixed-step backlog exceeded the configured frame budget");
        }

        simulation::SimulationStepInput simulation_input{};
        if (simulation_allowed)
        {
            const auto planned_translation = gameplay::PlanDebugLocomotionStep(
                gameplay::DigitalMoveCommand{
                    .lateral = static_cast<std::int8_t>(
                        (input_snapshot.IsHeld(kDebugMoveRightAction) ? 1 : 0) -
                        (input_snapshot.IsHeld(kDebugMoveLeftAction) ? 1 : 0)),
                    .longitudinal = static_cast<std::int8_t>(
                        (input_snapshot.IsHeld(kDebugMoveForwardAction) ? 1 : 0) -
                        (input_snapshot.IsHeld(kDebugMoveBackwardAction) ? 1 : 0)),
                });
            if (!planned_translation)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                constexpr std::string_view error = "debug locomotion planning failed";
                log_->Error("simulation", error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::string(error),
                    .capture_error = std::nullopt,
                };
            }
            simulation_input.translation = simulation::EntityTranslation{
                .entity = debug_locomotion_entity_,
                .delta = *planned_translation,
            };
        }

        for (std::uint32_t step = 0; step < plan.simulation_steps; ++step)
        {
            if (simulation_->AdvanceOneStep(simulation_input) !=
                simulation::SimulationStepResult::Advanced)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                constexpr std::string_view error =
                    "simulation world representation exhausted";
                log_->Error("simulation", error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::string(error),
                    .capture_error = std::nullopt,
                };
            }
            ++result.executed_simulation_steps;
        }

        const simulation::SimulationState simulation_snapshot = simulation_->Snapshot();
        const bool movie_is_active = IsBootSequenceActive(boot_sequence_state_);
        if (!movie_is_active && FrontEndAllowsSimulation(front_end_state_))
        {
            auto refreshed_actor_draw_list = RefreshDiagnosticActorDrawList();
            if (!refreshed_actor_draw_list)
            {
                (void)ContainOpeningMovieAudio();
                jobs_->WaitForIdle();
                log_->Error("render", refreshed_actor_draw_list.error());
                return RunLoopResult{
                    .result = result,
                    .operational_error =
                        std::move(refreshed_actor_draw_list.error()),
                    .capture_error = std::nullopt,
                };
            }
        }
        constexpr runtime::RenderClearColorRgba8 kOpeningMovieClearColor{
            .red = 0U,
            .green = 0U,
            .blue = 0U,
            .alpha = 255U,
        };
        const runtime::RenderFramePacket render_packet{
            .rendered_frame_index = static_cast<std::uint64_t>(result.rendered_frames),
            .completed_simulation_steps = simulation_snapshot.completed_steps,
            .simulated_time = simulation_snapshot.simulated_time,
            .alive_entities = simulation_snapshot.alive_entities,
            .clear_color = movie_is_active
                ? kOpeningMovieClearColor
                : runtime::kDefaultRenderClearColor,
            .draw_list = movie_is_active
                ? opening_movie_draw_list_
                : CurrentFrontEndDrawList(),
        };
        auto rendered = host_->RenderFrame(render_packet);
        if (!rendered)
        {
            (void)ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            log_->Error("render", rendered.error());
            return RunLoopResult{
                .result = result,
                .operational_error = rendered.error(),
                .capture_error = std::nullopt,
            };
        }
        result.rendered_frames = *next_rendered_frame_count;

        const AudioServiceSnapshot audio_health = audio_->Snapshot();
        const OpeningMovieAudioFault audio_fault =
            ClassifyOpeningMovieAudioFault(
                OpeningMovieAudioFaultCountersOf(audio_fault_baseline),
                OpeningMovieAudioFaultCountersOf(audio_health));
        const OpeningMovieAudioFaultDisposition audio_fault_disposition =
            DisposeOpeningMovieAudioFault(audio_fault, movie_was_active);
        if (audio_fault_disposition ==
            OpeningMovieAudioFaultDisposition::FailOpen)
        {
            log_->Warning("opening_movie",
                OpeningMovieAudioFaultMessage(audio_fault));
            const bool contained = FinishOpeningMovieFrontEndTransition(
                audio_fault_baseline);
            if (!contained)
            {
                jobs_->WaitForIdle();
                constexpr std::string_view error =
                    "opening movie audio containment failed during the front-end transition";
                log_->Error("opening_movie", error);
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::string(error),
                    .capture_error = std::nullopt,
                };
            }
        }
        else if (audio_fault_disposition ==
            OpeningMovieAudioFaultDisposition::Fatal)
        {
            const bool contained = ContainOpeningMovieAudio();
            jobs_->WaitForIdle();
            const std::string_view error = GeneralAudioFaultMessage(audio_fault);
            log_->Error("audio", error);
            if (!contained)
                log_->Error("audio", "audio containment also reported a control failure");
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }
    }

    const bool movie_window_open_at_exit =
        IsBootSequenceActive(boot_sequence_state_);
    const bool contained = ContainOpeningMovieAudio();
    jobs_->WaitForIdle();
    const AudioServiceSnapshot audio = audio_->Snapshot();
    const OpeningMovieAudioFault exit_audio_fault =
        ClassifyOpeningMovieAudioFault(
            OpeningMovieAudioFaultCountersOf(audio_fault_baseline),
            OpeningMovieAudioFaultCountersOf(audio));
    const OpeningMovieAudioFaultDisposition exit_audio_fault_disposition =
        DisposeOpeningMovieAudioFault(
            exit_audio_fault, movie_window_open_at_exit);
    if (exit_audio_fault_disposition ==
        OpeningMovieAudioFaultDisposition::FailOpen)
    {
        log_->Warning("opening_movie",
            OpeningMovieAudioFaultMessage(exit_audio_fault));
        ReleaseOpeningMovieForFrontEnd();
    }
    if (!contained)
    {
        constexpr std::string_view error =
            "audio playback containment operation failed";
        log_->Error("audio", error);
        return RunLoopResult{
            .result = result,
            .operational_error = std::string(error),
            .capture_error = std::nullopt,
        };
    }
    if (exit_audio_fault_disposition ==
        OpeningMovieAudioFaultDisposition::Fatal)
    {
        const std::string_view error = GeneralAudioFaultMessage(exit_audio_fault);
        log_->Error("audio", error);
        return RunLoopResult{
            .result = result,
            .operational_error = std::string(error),
            .capture_error = std::nullopt,
        };
    }
    result.audio_callback_count = audio.callback_count;
    result.audio_frames_provided = audio.provided_frames;
    log_->Info("runtime", "host loop ended after " +
                              std::to_string(result.rendered_frames) + " rendered frames and " +
                              std::to_string(result.executed_simulation_steps) +
                              " executed simulation steps");
    return RunLoopResult{
        .result = result,
        .operational_error = std::nullopt,
        .capture_error = std::nullopt,
    };
}

std::expected<void, std::string> OmegaApp::CreateFirstProfile()
{
    if (!can_create_first_profile_ || !first_profile_presentation_ ||
        native_persistence_ == nullptr ||
        front_end_startup_model_.total_profiles != 0U ||
        front_end_startup_model_.visible_profiles != 0U)
    {
        return std::unexpected(
            std::string{"first profile creation is not available"});
    }

    auto live_profiles = native_persistence_->profiles().List();
    if (!live_profiles)
    {
        return std::unexpected(
            "first profile catalog check failed: " +
            std::string(profiles::ProfileCatalogErrorCodeName(
                live_profiles.error().code)));
    }
    if (!live_profiles->empty())
    {
        return std::unexpected(
            std::string{"first profile creation requires an empty catalog"});
    }

    std::uint64_t timestamp = 0U;
    if (first_profile_timestamp_override_for_testing_)
    {
        timestamp = *first_profile_timestamp_override_for_testing_;
        first_profile_timestamp_override_for_testing_.reset();
    }
    else
    {
        const auto elapsed = std::chrono::system_clock::now().time_since_epoch();
        if (elapsed < std::chrono::system_clock::duration::zero())
        {
            return std::unexpected(
                std::string{"system clock precedes the supported UTC epoch"});
        }
        const auto milliseconds =
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
        if (milliseconds < 0)
        {
            return std::unexpected(
                std::string{"system clock precedes the supported UTC epoch"});
        }
        timestamp = static_cast<std::uint64_t>(milliseconds);
    }
    if (timestamp > profiles::kProfileTimestampMaxUnixMilliseconds)
    {
        return std::unexpected(
            std::string{"system clock exceeds the supported UTC range"});
    }

    try
    {
        std::array prospective_profiles{
            profiles::ProfileSummary{
                .id = kFirstProfileId,
                .metadata = profiles::ProfileMetadata{
                    .display_name = std::string{kFrontEndFirstProfileDisplayName},
                    .created_unix_milliseconds = timestamp,
                    .modified_unix_milliseconds = timestamp,
                },
                .metadata_revision = 1U,
            },
        };
        const auto projected_model =
            MakeFrontEndStartupModel(prospective_profiles);
        if (!projected_model)
        {
            return std::unexpected(
                std::string{"first profile projection failed"});
        }

        auto created = native_persistence_->profiles().Create(
            kFirstProfileId, std::move(prospective_profiles[0].metadata));
        if (!created)
        {
            return std::unexpected(
                "first profile creation failed: " +
                std::string(profiles::ProfileCatalogErrorCodeName(
                    created.error().code)));
        }

        // Every potentially failing allocation, catalog read, and GPU operation
        // completed before the durable mutation. These fixed-value swaps cannot
        // strand the database and visible presentation in different states.
        static_assert(std::is_nothrow_swappable_v<FrontEndPresentation>);
        std::swap(front_end_presentation_, *first_profile_presentation_);
        front_end_startup_model_ = *projected_model;
        can_create_first_profile_ = false;
        return {};
    }
    catch (const std::exception&)
    {
        return std::unexpected(
            std::string{"first profile preparation failed"});
    }
    catch (...)
    {
        return std::unexpected(
            std::string{"first profile preparation failed"});
    }
}

std::expected<void, std::string> OmegaApp::ApplyFrontEndCommand(
    const FrontEndCommand command)
{
    if (command.type == FrontEndCommandType::CreateFirstProfile)
    {
        if (command.profile_slot != FrontEndProfileSlot::First)
        {
            return std::unexpected(
                std::string{"first profile command selected an invalid slot"});
        }
        return CreateFirstProfile();
    }
    if (command.type != FrontEndCommandType::SetActiveProfile)
        return {};

    const std::size_t slot = static_cast<std::size_t>(command.profile_slot);
    if (slot >= kFrontEndVisibleProfiles || slot >= front_end_startup_model_.visible_profiles ||
        slot >= front_end_startup_model_.total_profiles)
        return {};

    const std::optional<profiles::ProfileId>& profile_id = front_end_startup_model_.profiles[slot].id;
    if (!profile_id)
        return {};
    active_profile_id_ = *profile_id;
    return {};
}

std::expected<void, std::string> OmegaApp::RefreshDiagnosticActorDrawList()
{
    if (simulation_ == nullptr)
    {
        return std::unexpected(
            std::string{"diagnostic actor position is unavailable"});
    }
    const std::optional<simulation::Position3> position =
        simulation_->PositionOf(debug_locomotion_entity_);
    if (!position)
    {
        return std::unexpected(
            std::string{"diagnostic actor position is unavailable"});
    }

    constexpr runtime::RenderSourceRectQ16 full_source{
        .left = 0U,
        .top = 0U,
        .right = runtime::kNormalizedRenderExtent,
        .bottom = runtime::kNormalizedRenderExtent,
    };
    std::array<runtime::RenderTextureBlitCommand, 2U> commands{};
    const std::span<const runtime::RenderTextureBlitCommand> base_commands =
        diagnostic_hidden_draw_list_.commands();
    if (base_commands.size() > 1U)
    {
        return std::unexpected(
            std::string{"diagnostic actor draw-list creation failed"});
    }

    std::size_t command_count = 0U;
    if (!base_commands.empty())
        commands[command_count++] = base_commands.front();
    commands[command_count++] = runtime::RenderTextureBlitCommand{
        .texture = diagnostic_actor_marker_texture_,
        .source = full_source,
        .destination =
            PlanProjectDiagnosticActorMarkerDestination(*position),
        .fit_mode = runtime::RenderTextureFitMode::Stretch,
        .filter_mode = runtime::RenderTextureFilterMode::Nearest,
    };
    auto created = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            commands.data(), command_count});
    if (!created)
    {
        return std::unexpected(
            std::string{"diagnostic actor draw-list creation failed"});
    }
    diagnostic_actor_draw_list_ = std::move(*created);
    return {};
}

const runtime::RenderDrawList &OmegaApp::CurrentFrontEndDrawList() const noexcept
{
    const FrontEndView view = BuildFrontEndView(front_end_state_, content_stage_, front_end_startup_model_);
    const std::size_t selected_main_row = static_cast<std::size_t>(view.selected_main_row);
    if (selected_main_row >= front_end_presentation_.main_draw_lists.size())
        return front_end_presentation_.main_draw_lists.front();

    switch (view.mode)
    {
    case FrontEndMode::Main:
        return front_end_presentation_.main_draw_lists[selected_main_row];
    case FrontEndMode::Profiles:
    {
        const std::size_t profile_slot = static_cast<std::size_t>(view.selected_profile_slot);
        if (profile_slot < view.profiles.visible_profiles &&
            profile_slot < front_end_presentation_.profile_selection_draw_lists.size())
        {
            return front_end_presentation_.profile_selection_draw_lists[profile_slot];
        }
        return front_end_presentation_.profiles_draw_list;
    }
    case FrontEndMode::Controls:
        return diagnostic_controls_draw_list_;
    case FrontEndMode::AssetTopology:
        return diagnostic_asset_topology_draw_list_;
    case FrontEndMode::DiagnosticPlay:
        return diagnostic_actor_draw_list_;
    }
    return front_end_presentation_.main_draw_lists.front();
}

std::string_view OmegaApp::driver_name() const noexcept
{
    return host_->driver_name();
}

std::string_view OmegaApp::audio_driver_name() const noexcept
{
    return audio_->driver_name();
}

int OmegaApp::audio_sample_rate() const noexcept
{
    return SdlAudioService::kSampleRate;
}

int OmegaApp::audio_channel_count() const noexcept
{
    return SdlAudioService::kChannelCount;
}

std::optional<profiles::ProfileId> OmegaApp::active_profile_id() const noexcept
{
    return active_profile_id_;
}
} // namespace omega::app
