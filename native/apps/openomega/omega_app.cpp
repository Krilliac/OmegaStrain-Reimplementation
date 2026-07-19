#include "omega_app.h"
#include "run_replay_session.h"

#include "omega/gameplay/debug_locomotion.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <utility>

namespace omega::app
{
std::expected<OmegaApp, std::string> OmegaApp::Create(runtime::ConfigStore config,
    const runtime::RuntimeSettings& settings, runtime::ContentStartupState content,
    const bool debug_device)
{
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
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_F1),
            .action = kDiagnosticMenuToggleAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::GamepadButton,
            .code = static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_START),
            .action = kDiagnosticMenuToggleAction,
        },
        runtime::InputBinding{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(SDL_SCANCODE_W),
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

    auto built_asset_topology = BuildProjectDiagnosticAssetTopologyImage();
    if (!built_asset_topology)
    {
        const std::string error = "project diagnostic asset topology image: " +
                                  std::string(
                                      runtime::TextureStorageTopologyDebugImageErrorCodeName(
                                          built_asset_topology.error().code));
        log->Error("startup", error);
        return std::unexpected(error);
    }
    runtime::DebugImage asset_topology_image = std::move(*built_asset_topology);

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

    auto created_host = SdlGpuHost::Create(*platform, debug_device);
    if (!created_host)
    {
        log->Error("startup", "SDL/GPU host: " + created_host.error());
        return std::unexpected(created_host.error());
    }
    auto host = std::make_unique<SdlGpuHost>(std::move(*created_host));

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
    constexpr runtime::RenderSourceRectQ16 menu_selection_source{
        .left = 18432U,
        .top = 9103U,
        .right = 59392U,
        .bottom = 14563U,
    };
    constexpr std::array menu_selection_targets{
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 7424U,
            .right = 4352U,
            .bottom = 9344U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 10304U,
            .right = 4352U,
            .bottom = 12224U,
        },
        runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 13184U,
            .right = 4352U,
            .bottom = 15104U,
        },
    };
    static_assert(menu_selection_targets.size() == kDiagnosticMenuRowCount);

    runtime::RenderTextureHandle diagnostic_texture;
    runtime::RenderTextureHandle diagnostic_menu_texture;
    runtime::RenderTextureHandle diagnostic_controls_texture;
    runtime::RenderTextureHandle diagnostic_asset_topology_texture;
    std::array<runtime::RenderTextureBlitCommand, 3U> diagnostic_commands{};
    std::size_t diagnostic_command_count = 0U;
    if (content_owner->debug_image)
    {
        const runtime::DebugImage& image = *content_owner->debug_image;
        auto uploaded = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
            .width = image.width,
            .height = image.height,
            .pixels = image.pixels(),
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
    }

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

    const runtime::DebugImage menu_image = BuildProjectDiagnosticMenuImage();
    auto uploaded_menu = host->UploadRgba8Texture(runtime::Rgba8TextureUploadView{
        .width = menu_image.width,
        .height = menu_image.height,
        .pixels = menu_image.pixels(),
    });
    if (!uploaded_menu)
    {
        const std::string error =
            "SDL/GPU diagnostic menu texture upload: " + uploaded_menu.error();
        log->Error("startup", error);
        return std::unexpected(error);
    }
    diagnostic_menu_texture = *uploaded_menu;
    diagnostic_commands[diagnostic_command_count++] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_menu_texture,
            .source = full_source,
            .destination = menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Stretch,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };

    std::array<runtime::RenderDrawList, kDiagnosticMenuRowCount>
        diagnostic_visible_draw_lists;
    for (std::size_t row = 0U; row < menu_selection_targets.size(); ++row)
    {
        diagnostic_commands[diagnostic_command_count] =
            runtime::RenderTextureBlitCommand{
                .texture = diagnostic_menu_texture,
                .source = menu_selection_source,
                .destination = menu_selection_targets[row],
                .fit_mode = runtime::RenderTextureFitMode::Stretch,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            };
        auto created_visible_draw_list = runtime::RenderDrawList::Create(
            std::span<const runtime::RenderTextureBlitCommand>{
                diagnostic_commands.data(), diagnostic_command_count + 1U});
        if (!created_visible_draw_list)
        {
            constexpr std::string_view error =
                "SDL/GPU diagnostic visible draw-list creation failed";
            log->Error("startup", error);
            return std::unexpected(std::string(error));
        }
        diagnostic_visible_draw_lists[row] = std::move(*created_visible_draw_list);
    }

    const runtime::DebugImage controls_image = BuildProjectDiagnosticControlsImage();
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
    diagnostic_commands[diagnostic_base_command_count] =
        runtime::RenderTextureBlitCommand{
            .texture = diagnostic_asset_topology_texture,
            .source = full_source,
            .destination = menu_target,
            .fit_mode = runtime::RenderTextureFitMode::Contain,
            .filter_mode = runtime::RenderTextureFilterMode::Nearest,
        };
    auto created_asset_topology_draw_list = runtime::RenderDrawList::Create(
        std::span<const runtime::RenderTextureBlitCommand>{
            diagnostic_commands.data(), diagnostic_base_command_count + 1U});
    if (!created_asset_topology_draw_list)
    {
        constexpr std::string_view error =
            "SDL/GPU diagnostic asset topology draw-list creation failed";
        log->Error("startup", error);
        return std::unexpected(std::string(error));
    }
    auto diagnostic_asset_topology_draw_list =
        std::move(*created_asset_topology_draw_list);

    log->Info("startup", "runtime services ready with " +
                             std::to_string(jobs->worker_count()) + " workers and " +
                             std::string(audio->driver_name()) + " audio");

    return OmegaApp(std::move(config_owner), std::move(content_owner), std::move(stderr_sink),
        std::move(ring_sink), std::move(log), std::move(jobs), std::move(assets),
        std::move(frame_scheduler), std::move(input), std::move(simulation),
        debug_locomotion_entity,
        std::move(platform), std::move(sdl_input), std::move(audio), std::move(host),
        diagnostic_texture, diagnostic_menu_texture, diagnostic_controls_texture,
        diagnostic_asset_topology_texture,
        std::move(diagnostic_hidden_draw_list), std::move(diagnostic_visible_draw_lists),
        std::move(diagnostic_controls_draw_list),
        std::move(diagnostic_asset_topology_draw_list));
}

OmegaApp::OmegaApp(std::unique_ptr<runtime::ConfigStore> config,
    std::unique_ptr<runtime::ContentStartupState> content,
    std::unique_ptr<runtime::StderrLogSink> stderr_sink,
    std::unique_ptr<runtime::RingLogSink> ring_sink,
    std::unique_ptr<runtime::LogService> log, std::unique_ptr<runtime::JobService> jobs,
    std::unique_ptr<runtime::AssetService> assets,
    std::unique_ptr<runtime::FrameScheduler> frame_scheduler,
    std::unique_ptr<runtime::InputTracker> input,
    std::unique_ptr<simulation::SimulationWorld> simulation,
    const simulation::EntityId debug_locomotion_entity,
    std::unique_ptr<SdlPlatformService> platform,
    std::unique_ptr<SdlInputService> sdl_input,
    std::unique_ptr<SdlAudioService> audio,
    std::unique_ptr<SdlGpuHost> host,
    const runtime::RenderTextureHandle diagnostic_texture,
    const runtime::RenderTextureHandle diagnostic_menu_texture,
    const runtime::RenderTextureHandle diagnostic_controls_texture,
    const runtime::RenderTextureHandle diagnostic_asset_topology_texture,
    runtime::RenderDrawList diagnostic_hidden_draw_list,
    std::array<runtime::RenderDrawList, kDiagnosticMenuRowCount>
        diagnostic_visible_draw_lists,
    runtime::RenderDrawList diagnostic_controls_draw_list,
    runtime::RenderDrawList diagnostic_asset_topology_draw_list) noexcept
    : config_(std::move(config)), content_(std::move(content)),
      stderr_sink_(std::move(stderr_sink)), ring_sink_(std::move(ring_sink)),
      log_(std::move(log)), jobs_(std::move(jobs)), assets_(std::move(assets)),
      frame_scheduler_(std::move(frame_scheduler)), input_(std::move(input)),
      simulation_(std::move(simulation)),
      debug_locomotion_entity_(debug_locomotion_entity), platform_(std::move(platform)),
      sdl_input_(std::move(sdl_input)), audio_(std::move(audio)), host_(std::move(host)),
      diagnostic_texture_(diagnostic_texture),
      diagnostic_menu_texture_(diagnostic_menu_texture),
      diagnostic_controls_texture_(diagnostic_controls_texture),
      diagnostic_asset_topology_texture_(diagnostic_asset_topology_texture),
      diagnostic_hidden_draw_list_(std::move(diagnostic_hidden_draw_list)),
      diagnostic_visible_draw_lists_(std::move(diagnostic_visible_draw_lists)),
      diagnostic_controls_draw_list_(std::move(diagnostic_controls_draw_list)),
      diagnostic_asset_topology_draw_list_(
          std::move(diagnostic_asset_topology_draw_list)),
      diagnostic_menu_state_(InitialDiagnosticMenuState())
{
}

OmegaApp::~OmegaApp() noexcept
{
    diagnostic_asset_topology_draw_list_ = {};
    diagnostic_controls_draw_list_ = {};
    for (runtime::RenderDrawList& draw_list : diagnostic_visible_draw_lists_)
        draw_list = {};
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
            // The host remains the authoritative owner and releases all surviving resources.
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
                // Destruction remains noexcept even if bounded shutdown logging cannot allocate.
            }
        }
    };

    release_texture(diagnostic_asset_topology_texture_,
        "diagnostic asset topology texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_controls_texture_,
        "diagnostic controls texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_menu_texture_,
        "diagnostic menu texture release failed; SDL/GPU host cleanup will retry");
    release_texture(diagnostic_texture_,
        "diagnostic texture release failed; SDL/GPU host cleanup will retry");
    diagnostic_asset_topology_texture_ = {};
    diagnostic_controls_texture_ = {};
    diagnostic_menu_texture_ = {};
    diagnostic_texture_ = {};
}
OmegaApp::OmegaApp(OmegaApp&&) noexcept = default;

std::expected<RunResult, std::string> OmegaApp::Run(const int frame_limit)
{
    RunLoopResult loop = RunLoop(frame_limit, nullptr);
    if (loop.operational_error)
        return std::unexpected(std::move(*loop.operational_error));
    return loop.result;
}

std::expected<RunCaptureOutcome, std::string> OmegaApp::RunWithCapture(
    const int frame_limit)
{
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

    RunLoopResult loop = RunLoop(frame_limit, &capture_session);
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
    const int frame_limit, runtime::RunCaptureSession* const capture_session)
{
    using Clock = std::chrono::steady_clock;

    log_->Info("runtime", "entering native host loop");
    RunResult result;
    bool running = true;
    auto previous_frame = Clock::now();
    while (running && (frame_limit < 0 || result.rendered_frames < frame_limit))
    {
        const InputPumpResult events = sdl_input_->PumpEvents(*input_, *log_);
        const runtime::InputSnapshot input_snapshot = input_->EndFrame();
        if (capture_session != nullptr)
        {
            const auto captured = capture_session->AppendInput(input_snapshot);
            if (!captured)
            {
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

        diagnostic_menu_state_ = UpdateDiagnosticMenu(diagnostic_menu_state_,
            DiagnosticMenuInputEdges{
                .primary_pressed =
                    input_snapshot.WasPressed(kDiagnosticMenuPrimaryAction),
                .previous_pressed =
                    input_snapshot.WasPressed(kDiagnosticMenuPreviousAction),
                .next_pressed = input_snapshot.WasPressed(kDiagnosticMenuNextAction),
            });
        const bool simulation_allowed =
            DiagnosticMenuAllowsSimulation(diagnostic_menu_state_);

        const auto current_frame = Clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            current_frame - previous_frame);
        if (capture_session != nullptr)
        {
            const auto captured = capture_session->AppendElapsed(elapsed);
            if (!captured)
            {
                jobs_->WaitForIdle();
                return RunLoopResult{
                    .result = result,
                    .operational_error = std::nullopt,
                    .capture_error = captured.error(),
                };
            }
        }
        const std::chrono::nanoseconds effective_elapsed = simulation_allowed
            ? elapsed
            : std::chrono::nanoseconds::zero();
        const runtime::FramePlan plan = frame_scheduler_->BeginFrame(effective_elapsed);
        previous_frame = current_frame;
        if (plan.simulation_steps >
            std::numeric_limits<std::uint64_t>::max() - result.planned_simulation_steps)
        {
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
        const runtime::RenderFramePacket render_packet{
            .rendered_frame_index = static_cast<std::uint64_t>(result.rendered_frames),
            .completed_simulation_steps = simulation_snapshot.completed_steps,
            .simulated_time = simulation_snapshot.simulated_time,
            .alive_entities = simulation_snapshot.alive_entities,
            .clear_color = runtime::kDefaultRenderClearColor,
            .draw_list = CurrentDiagnosticDrawList(),
        };
        auto rendered = host_->RenderFrame(render_packet);
        if (!rendered)
        {
            jobs_->WaitForIdle();
            log_->Error("render", rendered.error());
            return RunLoopResult{
                .result = result,
                .operational_error = rendered.error(),
                .capture_error = std::nullopt,
            };
        }
        ++result.rendered_frames;

        if (audio_->Snapshot().callback_failures != 0U)
        {
            jobs_->WaitForIdle();
            constexpr std::string_view error =
                "audio callback failed to provide playback data";
            log_->Error("audio", error);
            return RunLoopResult{
                .result = result,
                .operational_error = std::string(error),
                .capture_error = std::nullopt,
            };
        }
    }

    jobs_->WaitForIdle();
    const AudioServiceSnapshot audio = audio_->Snapshot();
    if (audio.callback_failures != 0U)
    {
        constexpr std::string_view error = "audio callback failed to provide playback data";
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

const runtime::RenderDrawList& OmegaApp::CurrentDiagnosticDrawList() const noexcept
{
    const std::size_t selected_row =
        static_cast<std::size_t>(diagnostic_menu_state_.selected_row);
    if (selected_row >= diagnostic_visible_draw_lists_.size())
        return diagnostic_hidden_draw_list_;

    switch (diagnostic_menu_state_.mode)
    {
    case DiagnosticMenuMode::MainMenu:
        return diagnostic_visible_draw_lists_[selected_row];
    case DiagnosticMenuMode::Controls:
        return diagnostic_controls_draw_list_;
    case DiagnosticMenuMode::AssetTopology:
        return diagnostic_asset_topology_draw_list_;
    case DiagnosticMenuMode::DiagnosticPlay:
        return diagnostic_hidden_draw_list_;
    }
    return diagnostic_hidden_draw_list_;
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
} // namespace omega::app
