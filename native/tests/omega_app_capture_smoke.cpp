#include "diagnostic_menu.h"
#include "omega_app.h"
#include "run_replay_session.h"

#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_trace.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/runtime_settings.h"
#include "omega/runtime/scheduler_elapsed_trace.h"

#include <SDL3/SDL.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app::detail
{
struct SdlGpuHostTestAccess final
{
    [[nodiscard]] static std::expected<
        std::array<runtime::RenderClearColorRgba8, 16U>, std::string>
        ReadbackBlitsForTesting(
            SdlGpuHost& host, const runtime::RenderFramePacket& packet)
    {
        return host.ReadbackBlitsForTesting(packet);
    }
};

struct OmegaAppTestAccess final
{
    [[nodiscard]] static bool InstallUnownedDiagnosticDraw(OmegaApp& app)
    {
        constexpr runtime::RenderTextureHandle unowned_texture{
            .pool_identity = std::numeric_limits<std::uint64_t>::max(),
            .generation = std::numeric_limits<std::uint64_t>::max(),
            .slot_index = std::numeric_limits<std::uint32_t>::max(),
        };
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
        constexpr std::array commands{
            runtime::RenderTextureBlitCommand{
                .texture = unowned_texture,
                .source = full_source,
                .destination = full_target,
                .fit_mode = runtime::RenderTextureFitMode::Contain,
                .filter_mode = runtime::RenderTextureFilterMode::Nearest,
            },
        };
        auto created = runtime::RenderDrawList::Create(commands);
        if (!created)
            return false;
        app.diagnostic_hidden_draw_list_ = *created;
        for (runtime::RenderDrawList& draw_list : app.diagnostic_visible_draw_lists_)
            draw_list = *created;
        app.diagnostic_controls_draw_list_ = *created;
        app.diagnostic_asset_topology_draw_list_ = *created;
        return true;
    }

    static void ClearDiagnosticDraw(OmegaApp& app) noexcept
    {
        app.diagnostic_hidden_draw_list_ = {};
        for (runtime::RenderDrawList& draw_list : app.diagnostic_visible_draw_lists_)
            draw_list = {};
        app.diagnostic_controls_draw_list_ = {};
        app.diagnostic_asset_topology_draw_list_ = {};
    }

    [[nodiscard]] static GpuHostSnapshot GpuSnapshot(const OmegaApp& app) noexcept
    {
        return app.host_->Snapshot();
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_texture_;
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticMenuTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_menu_texture_;
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticControlsTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_controls_texture_;
    }

    [[nodiscard]] static runtime::RenderTextureHandle DiagnosticAssetTopologyTexture(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_asset_topology_texture_;
    }

    [[nodiscard]] static const runtime::RenderDrawList& DiagnosticHiddenDrawList(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_hidden_draw_list_;
    }

    [[nodiscard]] static const std::array<runtime::RenderDrawList,
        kDiagnosticMenuRowCount>& DiagnosticVisibleDrawLists(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_visible_draw_lists_;
    }

    [[nodiscard]] static const runtime::RenderDrawList& DiagnosticControlsDrawList(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_controls_draw_list_;
    }

    [[nodiscard]] static const runtime::RenderDrawList&
    DiagnosticAssetTopologyDrawList(const OmegaApp& app) noexcept
    {
        return app.diagnostic_asset_topology_draw_list_;
    }

    [[nodiscard]] static const runtime::RenderDrawList& CurrentDiagnosticDrawList(
        const OmegaApp& app) noexcept
    {
        return app.CurrentDiagnosticDrawList();
    }

    [[nodiscard]] static DiagnosticMenuState DiagnosticMenu(
        const OmegaApp& app) noexcept
    {
        return app.diagnostic_menu_state_;
    }

    static void SetDiagnosticMenuState(
        OmegaApp& app, const DiagnosticMenuState state) noexcept
    {
        app.diagnostic_menu_state_ = state;
    }

    [[nodiscard]] static std::optional<simulation::Position3>
    DebugLocomotionPosition(const OmegaApp& app) noexcept
    {
        if (!app.simulation_)
            return std::nullopt;
        return app.simulation_->PositionOf(app.debug_locomotion_entity_);
    }

    [[nodiscard]] static runtime::FrameSchedulerState SchedulerSnapshot(
        const OmegaApp& app) noexcept
    {
        return app.frame_scheduler_->Snapshot();
    }

    [[nodiscard]] static simulation::SimulationState SimulationSnapshot(
        const OmegaApp& app) noexcept
    {
        return app.simulation_->Snapshot();
    }

    [[nodiscard]] static SdlGpuHost& Host(OmegaApp& app) noexcept
    {
        return *app.host_;
    }

    [[nodiscard]] static bool HasInputBinding(const OmegaApp& app,
        const runtime::InputDevice device, const std::uint16_t code,
        const std::uint32_t action) noexcept
    {
        if (!app.input_)
            return false;
        for (const runtime::InputBinding& binding : app.input_->bindings().bindings())
        {
            if (binding.device == device && binding.code == code &&
                binding.action == action)
            {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] static std::size_t InputBindingCount(
        const OmegaApp& app) noexcept
    {
        return app.input_ ? app.input_->bindings().bindings().size() : 0U;
    }

    [[nodiscard]] static std::size_t InputActionCount(
        const OmegaApp& app) noexcept
    {
        return app.input_ ? app.input_->bindings().actions().size() : 0U;
    }

    [[nodiscard]] static std::uint64_t NextInputFrameIndex(
        const OmegaApp& app) noexcept
    {
        return app.input_ ? app.input_->next_frame_index() : 0U;
    }
};
} // namespace omega::app::detail

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool DrawListsEqual(const omega::runtime::RenderDrawList& left,
    const omega::runtime::RenderDrawList& right) noexcept
{
    const auto left_commands = left.commands();
    const auto right_commands = right.commands();
    if (left_commands.size() != right_commands.size())
        return false;
    for (std::size_t index = 0U; index < left_commands.size(); ++index)
    {
        if (left_commands[index] != right_commands[index])
            return false;
    }
    return true;
}

[[nodiscard]] bool DrawListArraysEqual(
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kDiagnosticMenuRowCount>& left,
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kDiagnosticMenuRowCount>& right) noexcept
{
    for (std::size_t index = 0U; index < left.size(); ++index)
    {
        if (!DrawListsEqual(left[index], right[index]))
            return false;
    }
    return true;
}

[[nodiscard]] bool SameTextureResidency(const omega::app::GpuHostSnapshot& left,
    const omega::app::GpuHostSnapshot& right) noexcept
{
    return left.textures == right.textures &&
           left.successful_uploads == right.successful_uploads &&
           left.successful_upload_logical_bytes ==
               right.successful_upload_logical_bytes &&
           left.successful_releases == right.successful_releases;
}

[[nodiscard]] bool SameSimulationState(
    const omega::simulation::SimulationState& left,
    const omega::simulation::SimulationState& right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
}

[[nodiscard]] bool IsOneVisibleMenuSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 2U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

[[nodiscard]] bool IsOneModalCardSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions + 1U &&
           after.successful_blit_draws == before.successful_blit_draws + 1U &&
           after.clear_submissions == before.clear_submissions &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

[[nodiscard]] bool IsOneHiddenMenuSubmission(
    const omega::app::GpuHostSnapshot& before,
    const omega::app::GpuHostSnapshot& after) noexcept
{
    return SameTextureResidency(before, after) &&
           after.frame_submissions == before.frame_submissions + 1U &&
           after.blit_submissions == before.blit_submissions &&
           after.successful_blit_draws == before.successful_blit_draws &&
           after.clear_submissions == before.clear_submissions + 1U &&
           after.unavailable_swapchain_submissions ==
               before.unavailable_swapchain_submissions &&
           after.rejected_nondefault_texture_handles ==
               before.rejected_nondefault_texture_handles;
}

struct ExpectedSchedulerAdvance
{
    omega::runtime::FrameSchedulerState state;
    omega::runtime::FramePlan plan;
};

[[nodiscard]] ExpectedSchedulerAdvance AdvanceSchedulerSnapshot(
    const omega::runtime::FrameSchedulerState before,
    const std::chrono::nanoseconds elapsed) noexcept
{
    ExpectedSchedulerAdvance expected{
        .state = before,
        .plan = {},
    };
    std::chrono::nanoseconds delta = elapsed;
    if (delta < std::chrono::nanoseconds::zero())
        delta = std::chrono::nanoseconds::zero();
    if (delta > before.config.max_frame_delta)
    {
        delta = before.config.max_frame_delta;
        expected.plan.clamped_delta = true;
    }

    const std::chrono::nanoseconds accumulated = before.accumulated_remainder + delta;
    const std::int64_t step = before.config.simulation_step.count();
    const std::int64_t available = accumulated.count() / step;
    const std::int64_t budget =
        static_cast<std::int64_t>(before.config.max_steps_per_frame);
    std::int64_t planned = available;
    if (available > budget)
    {
        planned = budget;
        expected.plan.dropped_time = true;
        expected.state.total_dropped_time =
            omega::runtime::detail::SaturatingAddNanoseconds(
                before.total_dropped_time,
                std::chrono::nanoseconds{(available - budget) * step});
    }
    expected.plan.simulation_steps = static_cast<std::uint32_t>(planned);
    expected.state.accumulated_remainder =
        std::chrono::nanoseconds{accumulated.count() % step};
    expected.state.total_planned_steps += static_cast<std::uint64_t>(planned);
    expected.plan.interpolation_alpha =
        static_cast<double>(expected.state.accumulated_remainder.count()) /
        static_cast<double>(step);
    return expected;
}

[[nodiscard]] bool PushQuit()
{
    SDL_Event event{};
    event.type = SDL_EVENT_QUIT;
    return SDL_PushEvent(&event);
}

[[nodiscard]] bool PushEscape(const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.down = down;
    return SDL_PushEvent(&event);
}

[[nodiscard]] bool PushKey(const SDL_Scancode scancode, const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = down;
    return SDL_PushEvent(&event);
}
} // namespace

int main()
{
    using omega::app::RunCaptureCompletion;
    using omega::app::RunResult;

    auto config = omega::runtime::ParseConfigText("");
    Check(config.has_value(), "an empty project configuration parses");
    if (!config)
        return EXIT_FAILURE;

    omega::runtime::RuntimeSettings settings;
    settings.jobs.worker_count = 1U;
    settings.jobs.max_pending_jobs = 8U;
    settings.frame.simulation_step = omega::runtime::kMinimumSimulationStep;
    settings.frame.max_steps_per_frame = 8U;
    settings.frame.max_frame_delta =
        omega::runtime::kMinimumSimulationStep * 8;
    settings.max_input_events_per_frame =
        omega::runtime::InputTracker::kMaxEventsPerFrameLimit;
    auto app = omega::app::OmegaApp::Create(std::move(*config), settings,
        omega::runtime::ContentStartupState{}, false);
    Check(app.has_value(), "the zero-file OmegaApp fixture starts");
    if (!app)
    {
        std::cerr << app.error() << '\n';
        return EXIT_FAILURE;
    }

    using omega::runtime::InputDevice;
    using omega::app::detail::OmegaAppTestAccess;
    Check(OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
              omega::simulation::Position3{},
        "the host creates one positioned synthetic diagnostic entity at the origin");
    Check(OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
              static_cast<std::uint16_t>(SDL_SCANCODE_W),
              omega::app::kDebugMoveForwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_UP),
                  omega::app::kDebugMoveForwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_S),
                  omega::app::kDebugMoveBackwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_DOWN),
                  omega::app::kDebugMoveBackwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_A),
                  omega::app::kDebugMoveLeftAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_D),
                  omega::app::kDebugMoveRightAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_UP),
                  omega::app::kDebugMoveForwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_DOWN),
                  omega::app::kDebugMoveBackwardAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_LEFT),
                  omega::app::kDebugMoveLeftAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_DPAD_RIGHT),
                  omega::app::kDebugMoveRightAction),
        "the synthetic W/S, Up/Down, A/D, and gamepad dpad bindings expose action IDs 2 through 5");
    Check(OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
              static_cast<std::uint16_t>(SDL_SCANCODE_F1),
              omega::app::kDiagnosticMenuToggleAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_RETURN),
                  omega::app::kDiagnosticMenuToggleAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
                  static_cast<std::uint16_t>(SDL_SCANCODE_KP_ENTER),
                  omega::app::kDiagnosticMenuToggleAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_START),
                  omega::app::kDiagnosticMenuToggleAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_SOUTH),
                  omega::app::kDiagnosticMenuToggleAction) &&
              OmegaAppTestAccess::InputBindingCount(*app) == 17U &&
              OmegaAppTestAccess::InputActionCount(*app) == 6U,
        "seventeen physical bindings preserve the six-action schema while "
        "five confirmation controls share action 6");

    const omega::runtime::RenderTextureHandle diagnostic_texture =
        OmegaAppTestAccess::DiagnosticTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_menu_texture =
        OmegaAppTestAccess::DiagnosticMenuTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_controls_texture =
        OmegaAppTestAccess::DiagnosticControlsTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_asset_topology_texture =
        OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app);
    const omega::runtime::RenderDrawList initial_hidden_draw_list =
        OmegaAppTestAccess::DiagnosticHiddenDrawList(*app);
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kDiagnosticMenuRowCount> initial_visible_draw_lists =
        OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app);
    const omega::runtime::RenderDrawList initial_controls_draw_list =
        OmegaAppTestAccess::DiagnosticControlsDrawList(*app);
    const omega::runtime::RenderDrawList initial_asset_topology_draw_list =
        OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(*app);
    constexpr omega::runtime::RenderSourceRectQ16 kFullMenuSource{
        .left = 0U,
        .top = 0U,
        .right = omega::runtime::kNormalizedRenderExtent,
        .bottom = omega::runtime::kNormalizedRenderExtent,
    };
    constexpr omega::runtime::RenderTargetRectQ16 kMenuDestination{
        .left = 2048U,
        .top = 2048U,
        .right = 26624U,
        .bottom = 15872U,
    };
    constexpr omega::runtime::RenderSourceRectQ16 kMenuSelectionSource{
        .left = 18432U,
        .top = 9103U,
        .right = 59392U,
        .bottom = 14563U,
    };
    constexpr std::array kMenuSelectionTargets{
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 7424U,
            .right = 4352U,
            .bottom = 9344U,
        },
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 10304U,
            .right = 4352U,
            .bottom = 12224U,
        },
        omega::runtime::RenderTargetRectQ16{
            .left = 3584U,
            .top = 13184U,
            .right = 4352U,
            .bottom = 15104U,
        },
    };
    static_assert(kMenuSelectionTargets.size() ==
                  omega::app::kDiagnosticMenuRowCount);
    const auto hidden_commands = initial_hidden_draw_list.commands();
    bool visible_lists_are_exact = true;
    for (std::size_t row = 0U; row < initial_visible_draw_lists.size(); ++row)
    {
        const auto commands = initial_visible_draw_lists[row].commands();
        visible_lists_are_exact = visible_lists_are_exact &&
                                  commands.size() == hidden_commands.size() + 2U;
        for (std::size_t index = 0U;
             visible_lists_are_exact && index < hidden_commands.size(); ++index)
        {
            visible_lists_are_exact = commands[index] == hidden_commands[index];
        }
        if (!visible_lists_are_exact)
            break;

        const auto& card = commands[hidden_commands.size()];
        const auto& marker = commands[hidden_commands.size() + 1U];
        visible_lists_are_exact =
            card.texture == diagnostic_menu_texture &&
            card.source == kFullMenuSource && card.destination == kMenuDestination &&
            card.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            card.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest &&
            marker.texture == diagnostic_menu_texture &&
            marker.source == kMenuSelectionSource &&
            marker.destination == kMenuSelectionTargets[row] &&
            marker.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            marker.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest;
    }
    const auto controls_commands = initial_controls_draw_list.commands();
    bool controls_list_is_exact =
        controls_commands.size() == hidden_commands.size() + 1U;
    for (std::size_t index = 0U;
         controls_list_is_exact && index < hidden_commands.size(); ++index)
    {
        controls_list_is_exact = controls_commands[index] == hidden_commands[index];
    }
    if (controls_list_is_exact)
    {
        const auto& controls_card = controls_commands[hidden_commands.size()];
        controls_list_is_exact =
            controls_card.texture == diagnostic_controls_texture &&
            controls_card.source == kFullMenuSource &&
            controls_card.destination == kMenuDestination &&
            controls_card.fit_mode == omega::runtime::RenderTextureFitMode::Stretch &&
            controls_card.filter_mode == omega::runtime::RenderTextureFilterMode::Nearest;
    }
    const auto asset_topology_commands = initial_asset_topology_draw_list.commands();
    bool asset_topology_list_is_exact =
        asset_topology_commands.size() == hidden_commands.size() + 1U;
    for (std::size_t index = 0U;
         asset_topology_list_is_exact && index < hidden_commands.size(); ++index)
    {
        asset_topology_list_is_exact =
            asset_topology_commands[index] == hidden_commands[index];
    }
    if (asset_topology_list_is_exact)
    {
        const auto& asset_topology_card =
            asset_topology_commands[hidden_commands.size()];
        asset_topology_list_is_exact =
            asset_topology_card.texture == diagnostic_asset_topology_texture &&
            asset_topology_card.source == kFullMenuSource &&
            asset_topology_card.destination == kMenuDestination &&
            asset_topology_card.fit_mode ==
                omega::runtime::RenderTextureFitMode::Contain &&
            asset_topology_card.filter_mode ==
                omega::runtime::RenderTextureFilterMode::Nearest;
    }
    Check(!diagnostic_texture.valid() && diagnostic_menu_texture.valid() &&
              diagnostic_controls_texture.valid() &&
              diagnostic_asset_topology_texture.valid() &&
              diagnostic_menu_texture != diagnostic_controls_texture &&
              diagnostic_menu_texture != diagnostic_asset_topology_texture &&
              diagnostic_controls_texture != diagnostic_asset_topology_texture &&
              OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState() &&
              initial_hidden_draw_list.empty() && visible_lists_are_exact &&
              controls_list_is_exact && asset_topology_list_is_exact &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[0]),
        "the zero-file host owns distinct exact MainMenu, Controls, and AssetTopology presentation resources");

    OmegaAppTestAccess::SetDiagnosticMenuState(*app,
        omega::app::DiagnosticMenuState{
            .mode = static_cast<omega::app::DiagnosticMenuMode>(255U),
            .selected_row = omega::app::DiagnosticMenuRow::StartDiagnosticPlay,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
              initial_hidden_draw_list),
        "an invalid menu mode selects the fail-closed hidden draw list");
    OmegaAppTestAccess::SetDiagnosticMenuState(*app,
        omega::app::DiagnosticMenuState{
            .mode = omega::app::DiagnosticMenuMode::MainMenu,
            .selected_row = static_cast<omega::app::DiagnosticMenuRow>(255U),
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
              initial_hidden_draw_list),
        "an invalid main-menu row selects the fail-closed hidden draw list");
    OmegaAppTestAccess::SetDiagnosticMenuState(*app,
        omega::app::DiagnosticMenuState{
            .mode = omega::app::DiagnosticMenuMode::Controls,
            .selected_row = omega::app::DiagnosticMenuRow::ShowControls,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
              initial_controls_draw_list),
        "a valid Controls state selects the exact immutable controls draw list");
    OmegaAppTestAccess::SetDiagnosticMenuState(*app,
        omega::app::DiagnosticMenuState{
            .mode = omega::app::DiagnosticMenuMode::Controls,
            .selected_row = static_cast<omega::app::DiagnosticMenuRow>(255U),
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
              initial_hidden_draw_list),
        "an invalid Controls row selects the fail-closed hidden draw list");
    OmegaAppTestAccess::SetDiagnosticMenuState(*app,
        omega::app::DiagnosticMenuState{
            .mode = omega::app::DiagnosticMenuMode::AssetTopology,
            .selected_row = omega::app::DiagnosticMenuRow::ShowAssetTopology,
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
              initial_asset_topology_draw_list),
        "a valid AssetTopology state selects the exact immutable topology draw list");
    OmegaAppTestAccess::SetDiagnosticMenuState(*app,
        omega::app::DiagnosticMenuState{
            .mode = omega::app::DiagnosticMenuMode::AssetTopology,
            .selected_row = static_cast<omega::app::DiagnosticMenuRow>(255U),
        });
    Check(DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
              initial_hidden_draw_list),
        "an invalid AssetTopology row selects the fail-closed hidden draw list");
    OmegaAppTestAccess::SetDiagnosticMenuState(
        *app, omega::app::InitialDiagnosticMenuState());

    constexpr std::uint64_t kDiagnosticMenuLogicalBytes = 128ULL * 72ULL * 4ULL;
    constexpr std::uint64_t kDiagnosticPresentationLogicalBytes =
        kDiagnosticMenuLogicalBytes * 2ULL + 96ULL * 32ULL * 4ULL;
    const omega::app::GpuHostSnapshot initial_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(initial_gpu.successful_uploads == 3U &&
              initial_gpu.successful_upload_logical_bytes ==
                  kDiagnosticPresentationLogicalBytes &&
              initial_gpu.successful_releases == 0U &&
              initial_gpu.textures.reserved_slots == 0U &&
              initial_gpu.textures.resident_slots == 3U &&
              initial_gpu.textures.resident_logical_bytes ==
                  kDiagnosticPresentationLogicalBytes,
        "the two 128x72 cards and one 96x32 topology card own exactly 86,016 resident logical bytes");

    constexpr std::array menu_probe_coordinates{
        std::array{4U, 4U}, std::array{0U, 0U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{8U, 23U},
        std::array{24U, 22U}, std::array{62U, 22U}, std::array{9U, 30U},
        std::array{16U, 30U}, std::array{17U, 30U}, std::array{77U, 30U},
        std::array{44U, 45U}, std::array{45U, 45U}, std::array{68U, 60U},
        std::array{69U, 62U},
    };
    constexpr omega::runtime::RenderClearColorRgba8 probe_background{
        .red = 8U, .green = 12U, .blue = 24U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 probe_cyan{
        .red = 112U, .green = 220U, .blue = 255U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 probe_slate{
        .red = 28U, .green = 38U, .blue = 58U, .alpha = 255U};
    constexpr omega::runtime::RenderClearColorRgba8 probe_amber{
        .red = 255U, .green = 196U, .blue = 64U, .alpha = 255U};
    constexpr std::array expected_menu_probe_readback{
        probe_background, probe_cyan, probe_slate, probe_cyan,
        probe_amber, probe_cyan, probe_cyan, probe_cyan,
        probe_cyan, probe_slate, probe_cyan, probe_cyan,
        probe_slate, probe_cyan, probe_cyan, probe_cyan,
    };
    constexpr auto source_begin = [](const std::uint32_t coordinate,
                                      const std::uint32_t dimension) noexcept {
        return static_cast<std::uint32_t>(
            (static_cast<std::uint64_t>(coordinate) *
                 omega::runtime::kNormalizedRenderExtent +
                dimension - 1U) /
            dimension);
    };
    constexpr auto source_end = [](const std::uint32_t coordinate,
                                    const std::uint32_t dimension) noexcept {
        return static_cast<std::uint32_t>(
            static_cast<std::uint64_t>(coordinate + 1U) *
            omega::runtime::kNormalizedRenderExtent / dimension);
    };
    constexpr auto destination_edge = [](const std::uint32_t coordinate) noexcept {
        return coordinate * (omega::runtime::kNormalizedRenderExtent / 4U);
    };
    std::array<omega::runtime::RenderTextureBlitCommand, 16U> menu_probe_commands{};
    for (std::size_t index = 0U; index < menu_probe_commands.size(); ++index)
    {
        const std::uint32_t x = menu_probe_coordinates[index][0];
        const std::uint32_t y = menu_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        menu_probe_commands[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = diagnostic_menu_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(x, omega::app::kDiagnosticMenuImageWidth),
                .top = source_begin(y, omega::app::kDiagnosticMenuImageHeight),
                .right = source_end(x, omega::app::kDiagnosticMenuImageWidth),
                .bottom = source_end(y, omega::app::kDiagnosticMenuImageHeight),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto menu_probe_draw_list =
        omega::runtime::RenderDrawList::Create(menu_probe_commands);
    Check(menu_probe_draw_list.has_value(),
        "the sixteen one-texel menu readback commands form a valid draw list");
    if (menu_probe_draw_list)
    {
        omega::runtime::RenderFramePacket menu_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *menu_probe_draw_list,
        };
        auto menu_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), menu_probe_packet);
        Check(menu_probe_readback &&
                  *menu_probe_readback == expected_menu_probe_readback,
            "the resident menu texture preserves the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private menu readback seam leaves every production GPU counter unchanged");
    }

    constexpr std::array controls_probe_coordinates{
        std::array{4U, 4U}, std::array{0U, 0U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{40U, 12U}, std::array{42U, 11U},
        std::array{43U, 11U}, std::array{8U, 23U}, std::array{20U, 25U},
        std::array{13U, 25U}, std::array{20U, 32U}, std::array{13U, 39U},
        std::array{12U, 46U}, std::array{33U, 48U}, std::array{22U, 55U},
        std::array{12U, 62U},
    };
    constexpr std::array expected_controls_probe_readback{
        probe_background, probe_cyan, probe_slate, probe_cyan,
        probe_amber, probe_amber, probe_cyan, probe_background,
        probe_cyan, probe_slate, probe_cyan, probe_cyan,
        probe_cyan, probe_cyan, probe_cyan, probe_cyan,
    };
    for (std::size_t index = 0U; index < menu_probe_commands.size(); ++index)
    {
        const std::uint32_t x = controls_probe_coordinates[index][0];
        const std::uint32_t y = controls_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        menu_probe_commands[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = diagnostic_controls_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(x, omega::app::kDiagnosticMenuImageWidth),
                .top = source_begin(y, omega::app::kDiagnosticMenuImageHeight),
                .right = source_end(x, omega::app::kDiagnosticMenuImageWidth),
                .bottom = source_end(y, omega::app::kDiagnosticMenuImageHeight),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto controls_probe_draw_list =
        omega::runtime::RenderDrawList::Create(menu_probe_commands);
    Check(controls_probe_draw_list.has_value(),
        "the sixteen one-texel controls readback commands form a valid draw list");
    if (controls_probe_draw_list)
    {
        omega::runtime::RenderFramePacket controls_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *controls_probe_draw_list,
        };
        auto controls_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), controls_probe_packet);
        Check(controls_probe_readback &&
                  *controls_probe_readback == expected_controls_probe_readback,
            "the resident controls texture preserves the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private controls readback seam leaves every production GPU counter unchanged");
    }

    constexpr std::array asset_topology_probe_coordinates{
        std::array{0U, 0U}, std::array{1U, 1U}, std::array{4U, 4U},
        std::array{5U, 4U}, std::array{5U, 5U}, std::array{8U, 8U},
        std::array{9U, 8U}, std::array{11U, 9U}, std::array{12U, 9U},
        std::array{13U, 9U}, std::array{15U, 9U}, std::array{27U, 27U},
        std::array{41U, 9U}, std::array{43U, 8U}, std::array{73U, 9U},
        std::array{91U, 27U},
    };
    constexpr std::array expected_asset_topology_probe_readback{
        probe_slate, probe_background, probe_cyan, probe_background,
        probe_cyan, probe_cyan, probe_background, probe_cyan,
        probe_cyan, probe_background, probe_cyan, probe_amber,
        probe_cyan, probe_background, probe_cyan, probe_amber,
    };
    for (std::size_t index = 0U; index < menu_probe_commands.size(); ++index)
    {
        const std::uint32_t x = asset_topology_probe_coordinates[index][0];
        const std::uint32_t y = asset_topology_probe_coordinates[index][1];
        const std::uint32_t column = static_cast<std::uint32_t>(index % 4U);
        const std::uint32_t row = static_cast<std::uint32_t>(index / 4U);
        menu_probe_commands[index] = omega::runtime::RenderTextureBlitCommand{
            .texture = diagnostic_asset_topology_texture,
            .source = omega::runtime::RenderSourceRectQ16{
                .left = source_begin(x, 96U),
                .top = source_begin(y, 32U),
                .right = source_end(x, 96U),
                .bottom = source_end(y, 32U),
            },
            .destination = omega::runtime::RenderTargetRectQ16{
                .left = destination_edge(column),
                .top = destination_edge(row),
                .right = destination_edge(column + 1U),
                .bottom = destination_edge(row + 1U),
            },
            .fit_mode = omega::runtime::RenderTextureFitMode::Stretch,
            .filter_mode = omega::runtime::RenderTextureFilterMode::Nearest,
        };
    }
    auto asset_topology_probe_draw_list =
        omega::runtime::RenderDrawList::Create(menu_probe_commands);
    Check(asset_topology_probe_draw_list.has_value(),
        "the sixteen one-texel asset-topology readback commands form a valid draw list");
    if (asset_topology_probe_draw_list)
    {
        omega::runtime::RenderFramePacket asset_topology_probe_packet{
            .clear_color = omega::runtime::kDefaultRenderClearColor,
            .draw_list = *asset_topology_probe_draw_list,
        };
        auto asset_topology_probe_readback =
            omega::app::detail::SdlGpuHostTestAccess::ReadbackBlitsForTesting(
                OmegaAppTestAccess::Host(*app), asset_topology_probe_packet);
        Check(asset_topology_probe_readback &&
                  *asset_topology_probe_readback ==
                      expected_asset_topology_probe_readback,
            "the resident asset-topology texture preserves the exact sixteen-probe RGBA8 grid on GPU");
        Check(OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
            "the private asset-topology readback seam leaves every production GPU counter unchanged");
    }

    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);
    Check(PushQuit(), "a host-quit event enters the SDL queue");

    const auto negative = app->RunWithCapture(-1);
    Check(!negative, "negative capture planning rejects before event consumption");
    if (!negative)
    {
        Check(negative.error() ==
                  omega::app::detail::FiniteRunCapturePlanErrorMessage(
                      omega::app::detail::FiniteRunCapturePlanError::NegativeFrameLimit),
            "negative planning returns its fixed pre-loop error");
    }

    auto empty = app->RunWithCapture(0);
    Check(empty.has_value(), "zero-frame capture publishes without entering the loop");
    if (!empty)
        return EXIT_FAILURE;
    const auto* empty_pair = empty->trace_pair();
    Check(empty->requested_frame_limit() == 0U &&
              empty->completion() == RunCaptureCompletion::FrameLimitReached &&
              empty->result() == RunResult{} && !empty->failure() &&
              empty->scheduler_state_before() == empty->scheduler_state_after() &&
              empty_pair != nullptr,
        "zero-frame capture owns an empty no-work outcome");
    if (empty_pair != nullptr)
    {
        Check(empty_pair->input_trace().first_frame_index() == 0U &&
                  empty_pair->input_trace().maximum_frames() == 1U &&
                  empty_pair->input_trace().frame_count() == 0U &&
                  empty_pair->scheduler_elapsed_trace().maximum_frames() == 1U &&
                  empty_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  !empty_pair->terminal_input(),
            "zero-frame capture retains capacity one without advancing input");
    }

    auto host = app->RunWithCapture(1);
    Check(host.has_value(), "the queued host quit publishes a terminal capture");
    if (!host)
        return EXIT_FAILURE;
    const auto* host_pair = host->trace_pair();
    const auto host_terminal = host->terminal_input();
    const RunResult host_result = host->result();
    Check(host->completion() == RunCaptureCompletion::QuitRequested &&
              !host->failure() && host_pair != nullptr && host_terminal &&
              host_result.input_frames == 1U && host_result.rendered_frames == 0 &&
              host_result.quit_requested &&
              host->scheduler_state_before() == host->scheduler_state_after() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState() &&
              OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
        "host quit preserves startup menu, scheduler, GPU, and render resources");
    if (host_pair != nullptr && host_terminal)
    {
        Check(host_pair->input_trace().first_frame_index() == 0U &&
                  host_pair->input_trace().frame_count() == 1U &&
                  host_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  host_terminal->frame_index == 0U &&
                  host_terminal->host_quit_requested &&
                  !host_terminal->logical_quit_pressed,
            "host quit owns the exact first terminal input and both reason flags");
    }

    Check(PushEscape(true), "an Escape press enters the SDL queue");
    auto logical = app->RunWithCapture(1);
    Check(logical.has_value(), "logical quit publishes a terminal capture");
    if (!logical)
        return EXIT_FAILURE;
    const auto* logical_pair = logical->trace_pair();
    const auto logical_terminal = logical->terminal_input();
    Check(logical->completion() == RunCaptureCompletion::QuitRequested &&
              logical_pair != nullptr && logical_terminal &&
              logical->result().input_frames == 1U &&
              logical->result().rendered_frames == 0 &&
              OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState() &&
              OmegaAppTestAccess::GpuSnapshot(*app) == initial_gpu,
        "logical quit also preserves startup menu and ends before rendering");
    if (logical_pair != nullptr && logical_terminal)
    {
        Check(logical_pair->input_trace().first_frame_index() == 1U &&
                  logical_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  logical_terminal->frame_index == 1U &&
                  !logical_terminal->host_quit_requested &&
                  logical_terminal->logical_quit_pressed,
            "logical quit retains its distinct owned reason and continued index");
    }

    bool movement_events_queued = PushEscape(false) &&
                                  PushKey(SDL_SCANCODE_RETURN, true) &&
                                  PushKey(SDL_SCANCODE_W, true);
    // Keep the real SDL pump busy for longer than the minimum synthetic step without sleeping.
    // Duplicate level reports are explicitly accepted no-ops after the first held transition.
    for (std::size_t index = 0U; movement_events_queued && index < 2'048U; ++index)
        movement_events_queued = PushKey(SDL_SCANCODE_W, true);
    Check(movement_events_queued,
        "the same-frame Return and movement fixture enters the real SDL event queue");
    auto normal = app->RunWithCapture(1);
    Check(normal.has_value(), "a released quit action permits one captured render");
    if (!normal)
        return EXIT_FAILURE;
    const auto* normal_pair = normal->trace_pair();
    const RunResult normal_result = normal->result();
    const auto normal_debug_position =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    Check(normal->completion() == RunCaptureCompletion::FrameLimitReached &&
              !normal->failure() && normal_pair != nullptr &&
              normal_result.input_frames == 1U && normal_result.rendered_frames == 1 &&
              !normal_result.quit_requested &&
              normal_result.executed_simulation_steps > 0U &&
              normal_debug_position &&
              normal_debug_position->x == 0 && normal_debug_position->y == 0 &&
              normal_debug_position->z == static_cast<std::int64_t>(
                  normal_result.executed_simulation_steps),
        "one Return-plus-W frame enters diagnostic play and applies movement nonmodally");
    const omega::app::GpuHostSnapshot normal_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::DiagnosticMenuState{
                      .mode = omega::app::DiagnosticMenuMode::DiagnosticPlay,
                      .selected_row =
                          omega::app::DiagnosticMenuRow::StartDiagnosticPlay,
                  } &&
              OmegaAppTestAccess::DiagnosticMenuTexture(*app) ==
                  diagnostic_menu_texture &&
              OmegaAppTestAccess::DiagnosticControlsTexture(*app) ==
                  diagnostic_controls_texture &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
                  initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app),
                  initial_visible_draw_lists) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticControlsDrawList(*app),
                  initial_controls_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_hidden_draw_list) &&
              IsOneHiddenMenuSubmission(initial_gpu, normal_gpu),
        "primary priority enters DiagnosticPlay with one clear-only frame and no reupload");

    const omega::runtime::FrameSchedulerState normal_before =
        normal->scheduler_state_before();
    const omega::runtime::FrameSchedulerState normal_after =
        normal->scheduler_state_after();
    std::optional<omega::runtime::InputTraceFrameState> captured_input;
    std::optional<omega::runtime::SchedulerElapsedFrameState> captured_elapsed;
    std::optional<omega::runtime::FramePlan> captured_plan;
    std::optional<omega::runtime::InputTraceActionState> captured_forward;
    std::optional<omega::runtime::InputTraceActionState> captured_menu_toggle;
    std::array<std::uint32_t, omega::runtime::InputBindingTable::kMaxActions>
        captured_actions{};
    std::array<omega::runtime::InputTraceActionState,
        omega::runtime::InputBindingTable::kMaxActions>
        captured_action_states{};
    std::size_t captured_action_count = 0U;
    bool captured_action_schema_exact = false;
    bool captured_action_states_valid = true;
    if (normal_pair != nullptr)
    {
        captured_input = normal_pair->input_trace().FrameAt(0U);
        captured_elapsed = normal_pair->scheduler_elapsed_trace().FrameAt(0U);
        captured_forward = normal_pair->input_trace().ActionAt(
            0U, omega::app::kDebugMoveForwardAction);
        captured_menu_toggle = normal_pair->input_trace().ActionAt(
            0U, omega::app::kDiagnosticMenuToggleAction);
        const auto action_schema = normal_pair->input_trace().actions();
        captured_action_count = action_schema.size();
        constexpr std::array<std::uint32_t, 6U> kExpectedActions{
            1U,
            omega::app::kDebugMoveForwardAction,
            omega::app::kDebugMoveBackwardAction,
            omega::app::kDebugMoveLeftAction,
            omega::app::kDebugMoveRightAction,
            omega::app::kDiagnosticMenuToggleAction,
        };
        captured_action_schema_exact =
            action_schema.size() == kExpectedActions.size();
        for (std::size_t index = 0U;
             captured_action_schema_exact && index < kExpectedActions.size(); ++index)
        {
            captured_action_schema_exact = action_schema[index] == kExpectedActions[index];
        }
        for (std::size_t index = 0U; index < captured_action_count; ++index)
        {
            captured_actions[index] = action_schema[index];
            const auto action_state =
                normal_pair->input_trace().ActionAt(0U, action_schema[index]);
            if (!action_state)
            {
                captured_action_states_valid = false;
                break;
            }
            captured_action_states[index] = *action_state;
        }
        Check(normal_pair->input_trace().first_frame_index() == 2U &&
                  normal_pair->input_trace().frame_count() == 1U &&
                  normal_pair->scheduler_elapsed_trace().frame_count() == 1U &&
                  captured_input && captured_elapsed && captured_action_count == 6U &&
                  captured_action_schema_exact && captured_action_states_valid &&
                  captured_forward && captured_menu_toggle &&
                  captured_forward->held && captured_forward->pressed &&
                  !captured_forward->released && captured_menu_toggle->held &&
                  captured_menu_toggle->pressed && !captured_menu_toggle->released,
            "normal capture records the exact six-action schema and simultaneous Return/W edges");
        if (captured_elapsed)
        {
            auto replay = omega::runtime::FrameScheduler::Create(normal_before.config);
            Check(replay.has_value(), "the captured scheduler configuration revalidates");
            if (replay)
            {
                captured_plan = replay->BeginFrame(captured_elapsed->elapsed);
                Check(replay->Snapshot() == normal_after &&
                          captured_plan->simulation_steps ==
                              normal_result.planned_simulation_steps,
                    "the exact captured elapsed value reproduces scheduler state");
            }
        }
    }

    auto replay_traces = std::move(*normal).TakeTracePair();
    Check(replay_traces && captured_input && captured_elapsed && captured_plan &&
              captured_menu_toggle && captured_action_schema_exact &&
              captured_action_states_valid,
        "the real capture publishes complete owned replay inputs");
    if (!replay_traces || !captured_input || !captured_elapsed || !captured_plan ||
        !captured_menu_toggle || !captured_action_schema_exact ||
        !captured_action_states_valid)
    {
        return EXIT_FAILURE;
    }

    auto replay_created = omega::app::RunReplaySession::Create(
        std::move(*replay_traces),
        omega::app::RunReplaySessionConfig{
            .scheduler = normal_before.config,
            .enable_debug_locomotion = true,
            .initial_diagnostic_menu_state =
                omega::app::InitialDiagnosticMenuState(),
        });
    Check(replay_created.has_value(),
        "the actual real-host capture creates a fresh app replay session");
    if (!replay_created)
        return EXIT_FAILURE;
    omega::app::RunReplaySession replay_session = std::move(*replay_created);

    const auto replay_scheduler_before = replay_session.scheduler_state();
    const auto replay_simulation_before = replay_session.simulation_state();
    Check(replay_session.state() == omega::app::RunReplaySessionState::Ready &&
              replay_session.remaining_frames() == 1U && replay_scheduler_before &&
              *replay_scheduler_before == normal_before && replay_simulation_before &&
              replay_simulation_before->completed_steps == 0U &&
              replay_simulation_before->simulated_time ==
                  std::chrono::nanoseconds::zero() &&
              replay_simulation_before->alive_entities == 1U &&
              replay_session.debug_locomotion_position() ==
                  omega::simulation::Position3{} &&
              replay_session.diagnostic_menu_state() ==
                  omega::app::InitialDiagnosticMenuState(),
        "real-host replay begins with a fresh positioned synthetic diagnostic entity");

    auto replay_frame = replay_session.Next();
    Check(replay_frame.has_value(), "the actual captured frame advances through app replay");
    if (!replay_frame)
        return EXIT_FAILURE;

    bool replay_actions_match =
        replay_frame->input().actions().size() == captured_action_count;
    for (std::size_t index = 0U;
         replay_actions_match && index < captured_action_count; ++index)
    {
        const std::uint32_t action = captured_actions[index];
        const auto& expected = captured_action_states[index];
        replay_actions_match = replay_frame->input().actions()[index] == action &&
                               replay_frame->input().IsHeld(action) == expected.held &&
                               replay_frame->input().WasPressed(action) == expected.pressed &&
                               replay_frame->input().WasReleased(action) == expected.released;
    }
    const auto replay_plan = replay_frame->frame_plan();
    Check(replay_frame->input().frame_index() == captured_input->frame_index &&
              replay_frame->input().accepted_event_count() ==
                  captured_input->accepted_event_count &&
              replay_frame->input().rejected_event_count() ==
                  captured_input->rejected_event_count &&
              replay_actions_match &&
              replay_frame->input().IsHeld(
                  omega::app::kDiagnosticMenuToggleAction) &&
              replay_frame->input().WasPressed(
                  omega::app::kDiagnosticMenuToggleAction) &&
              !replay_frame->input().WasReleased(
                  omega::app::kDiagnosticMenuToggleAction) &&
              replay_frame->elapsed() == captured_elapsed->elapsed &&
              !replay_frame->terminal_input() && replay_plan &&
              replay_plan->simulation_steps == captured_plan->simulation_steps &&
              replay_plan->interpolation_alpha == captured_plan->interpolation_alpha &&
              replay_plan->clamped_delta == captured_plan->clamped_delta &&
              replay_plan->dropped_time == captured_plan->dropped_time,
        "app replay carries action 6 with the actual input and exact scheduler plan");

    const auto replay_scheduler_after = replay_session.scheduler_state();
    const auto replay_simulation_after = replay_session.simulation_state();
    const auto expected_fresh_time = normal_before.config.simulation_step *
                                     captured_plan->simulation_steps;
    Check(replay_scheduler_after && *replay_scheduler_after == normal_after &&
              replay_simulation_after &&
              replay_simulation_after->completed_steps ==
                  captured_plan->simulation_steps &&
              replay_simulation_after->simulated_time == expected_fresh_time &&
              replay_simulation_after->alive_entities == 1U &&
              replay_session.debug_locomotion_position() == normal_debug_position &&
              replay_session.diagnostic_menu_state() ==
                  omega::app::DiagnosticMenuState{} &&
              normal_result.planned_simulation_steps ==
                  captured_plan->simulation_steps &&
              normal_result.executed_simulation_steps ==
                  captured_plan->simulation_steps &&
              replay_session.state() ==
                  omega::app::RunReplaySessionState::Complete &&
              replay_session.remaining_frames() == 0U,
        "replay applies action 6 as menu activation and reaches the captured fresh-world position");

    const auto replay_complete = replay_session.Next();
    Check(!replay_complete &&
              replay_complete.error().operation == omega::app::RunReplayOperation::Next &&
              replay_complete.error().code ==
                  omega::app::RunReplayErrorCode::ReplayComplete &&
              !replay_complete.error().replay_error,
        "the consumed real-host capture reports stable app replay completion");

    constexpr omega::app::DiagnosticMenuState kDiagnosticPlayRowZero{};
    constexpr omega::app::DiagnosticMenuState kMainMenuRowOne{
        .mode = omega::app::DiagnosticMenuMode::MainMenu,
        .selected_row = omega::app::DiagnosticMenuRow::ShowControls,
    };
    constexpr omega::app::DiagnosticMenuState kControlsRowOne{
        .mode = omega::app::DiagnosticMenuMode::Controls,
        .selected_row = omega::app::DiagnosticMenuRow::ShowControls,
    };
    constexpr omega::app::DiagnosticMenuState kMainMenuRowTwo{
        .mode = omega::app::DiagnosticMenuMode::MainMenu,
        .selected_row = omega::app::DiagnosticMenuRow::ShowAssetTopology,
    };
    const auto RunPlainFrame = [&app]() {
        const auto result = app->Run(1);
        Check(result && result->input_frames == 1U && result->rendered_frames == 1 &&
                  !result->quit_requested,
            "one menu navigation frame completes");
        return result.has_value();
    };

    const auto position_after_primary =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    const std::uint64_t held_primary_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushKey(SDL_SCANCODE_W, false),
        "the F1 alias and forward release enter while Return keeps action 6 held");
    auto held_primary = app->RunWithCapture(1);
    Check(held_primary.has_value(), "held primary renders DiagnosticPlay once");
    if (!held_primary)
        return EXIT_FAILURE;
    const auto* held_pair = held_primary->trace_pair();
    const auto held_action = held_pair != nullptr
                                 ? held_pair->input_trace().ActionAt(
                                       0U, omega::app::kDiagnosticMenuPrimaryAction)
                                 : std::nullopt;
    const omega::app::GpuHostSnapshot held_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(held_pair != nullptr &&
              held_pair->input_trace().first_frame_index() == held_primary_index &&
              held_action && held_action->held && !held_action->pressed &&
              !held_action->released &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneHiddenMenuSubmission(normal_gpu, held_gpu),
        "a second physical alias does not repeat the held action-6 press edge or reopen the menu");

    const std::uint64_t nonfinal_release_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_RETURN, false),
        "Return releases while the F1 alias remains held");
    auto nonfinal_release = app->RunWithCapture(1);
    Check(nonfinal_release.has_value(),
        "the non-final action-6 alias release captures");
    if (!nonfinal_release)
        return EXIT_FAILURE;
    const auto* nonfinal_release_pair = nonfinal_release->trace_pair();
    const auto nonfinal_release_action = nonfinal_release_pair != nullptr
                                             ? nonfinal_release_pair->input_trace().ActionAt(
                                                   0U, omega::app::kDiagnosticMenuPrimaryAction)
                                             : std::nullopt;
    const omega::app::GpuHostSnapshot nonfinal_release_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(nonfinal_release_pair != nullptr &&
              nonfinal_release_pair->input_trace().first_frame_index() ==
                  nonfinal_release_index &&
              nonfinal_release_action && nonfinal_release_action->held &&
              !nonfinal_release_action->pressed &&
              !nonfinal_release_action->released &&
              nonfinal_release->result().input_frames == 1U &&
              nonfinal_release->result().rendered_frames == 1 &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneHiddenMenuSubmission(held_gpu, nonfinal_release_gpu),
        "releasing Return cannot release action 6 or mutate the menu while F1 remains held");

    const std::uint64_t final_release_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, false),
        "the last held action-6 alias releases");
    auto final_release = app->RunWithCapture(1);
    Check(final_release.has_value(), "the final action-6 alias release captures");
    if (!final_release)
        return EXIT_FAILURE;
    const auto* final_release_pair = final_release->trace_pair();
    const auto final_release_action = final_release_pair != nullptr
                                          ? final_release_pair->input_trace().ActionAt(
                                                0U, omega::app::kDiagnosticMenuPrimaryAction)
                                          : std::nullopt;
    const omega::app::GpuHostSnapshot final_release_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(final_release_pair != nullptr &&
              final_release_pair->input_trace().first_frame_index() ==
                  final_release_index &&
              final_release_action && !final_release_action->held &&
              !final_release_action->pressed && final_release_action->released &&
              final_release->result().input_frames == 1U &&
              final_release->result().rendered_frames == 1 &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneHiddenMenuSubmission(
                  nonfinal_release_gpu, final_release_gpu),
        "only the last physical alias release emits the logical action-6 release edge");

    Check(PushKey(SDL_SCANCODE_KP_ENTER, true),
        "a fresh keypad Enter primary edge enters the SDL queue");
    Check(RunPlainFrame(), "the keypad Enter primary frame completes");
    const omega::app::GpuHostSnapshot reopened_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState() &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[0]) &&
              IsOneVisibleMenuSubmission(final_release_gpu, reopened_gpu),
        "keypad Enter reopens MainMenu at row zero with exactly two resident blits");
    Check(PushKey(SDL_SCANCODE_KP_ENTER, false),
        "the reopened keypad Enter primary releases");
    Check(RunPlainFrame(), "reopened release frame completes");

    const std::uint64_t next_edge_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    const omega::runtime::FrameSchedulerState modal_scheduler_before =
        OmegaAppTestAccess::SchedulerSnapshot(*app);
    const omega::simulation::SimulationState modal_simulation_before =
        OmegaAppTestAccess::SimulationSnapshot(*app);
    const auto modal_position_before =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    const omega::app::GpuHostSnapshot modal_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    bool modal_events_queued = PushKey(SDL_SCANCODE_DOWN, true);
    for (std::size_t index = 0U; modal_events_queued && index < 4'095U; ++index)
        modal_events_queued = PushKey(SDL_SCANCODE_DOWN, true);
    Check(modal_events_queued,
        "the Down-arrow next-row edge and timing workload enter the SDL queue");
    auto next_edge = app->RunWithCapture(1);
    Check(next_edge.has_value(), "next-row edge captures");
    if (!next_edge)
        return EXIT_FAILURE;
    const auto* next_pair = next_edge->trace_pair();
    const auto next_action = next_pair != nullptr
                                 ? next_pair->input_trace().ActionAt(
                                       0U, omega::app::kDiagnosticMenuNextAction)
                                 : std::nullopt;
    const auto next_elapsed = next_pair != nullptr
                                  ? next_pair->scheduler_elapsed_trace().FrameAt(0U)
                                  : std::nullopt;
    const RunResult next_result = next_edge->result();
    const omega::app::GpuHostSnapshot modal_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    const omega::simulation::SimulationState modal_simulation_after =
        OmegaAppTestAccess::SimulationSnapshot(*app);
    Check(next_pair != nullptr &&
              next_pair->input_trace().first_frame_index() == next_edge_index &&
              next_action && next_action->held && next_action->pressed &&
              next_elapsed &&
              next_elapsed->elapsed > settings.frame.simulation_step &&
              next_result.input_frames == 1U && next_result.rendered_frames == 1 &&
              next_result.planned_simulation_steps == 0U &&
              next_result.executed_simulation_steps == 0U &&
              next_edge->scheduler_state_before() == modal_scheduler_before &&
              next_edge->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              modal_simulation_after.completed_steps ==
                  modal_simulation_before.completed_steps &&
              modal_simulation_after.simulated_time ==
                  modal_simulation_before.simulated_time &&
              modal_simulation_after.alive_entities ==
                  modal_simulation_before.alive_entities &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[1]) &&
              IsOneVisibleMenuSubmission(modal_gpu_before, modal_gpu_after),
        "a real Down-arrow sample above one fixed step navigates and renders "
        "while the modal menu freezes scheduler, world, and locomotion");

    const std::uint64_t held_next_alias_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_S, true),
        "the S alias enters while Down keeps action 3 held");
    auto held_next_alias = app->RunWithCapture(1);
    Check(held_next_alias.has_value(), "the held next-action alias captures");
    if (!held_next_alias)
        return EXIT_FAILURE;
    const auto* held_next_alias_pair = held_next_alias->trace_pair();
    const auto held_next_alias_action = held_next_alias_pair != nullptr
                                            ? held_next_alias_pair->input_trace().ActionAt(
                                                  0U, omega::app::kDiagnosticMenuNextAction)
                                            : std::nullopt;
    const omega::app::GpuHostSnapshot held_next_alias_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(held_next_alias_pair != nullptr &&
              held_next_alias_pair->input_trace().first_frame_index() ==
                  held_next_alias_index &&
              held_next_alias_action && held_next_alias_action->held &&
              !held_next_alias_action->pressed &&
              !held_next_alias_action->released &&
              held_next_alias->result().planned_simulation_steps == 0U &&
              held_next_alias->result().executed_simulation_steps == 0U &&
              held_next_alias->scheduler_state_before() == modal_scheduler_before &&
              held_next_alias->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  modal_position_before &&
              IsOneVisibleMenuSubmission(modal_gpu_after, held_next_alias_gpu),
        "a second physical action-3 alias cannot repeat navigation or advance modal owners");

    const std::uint64_t nonfinal_next_release_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_DOWN, false),
        "Down releases while the S alias keeps action 3 held");
    auto nonfinal_next_release = app->RunWithCapture(1);
    Check(nonfinal_next_release.has_value(),
        "the non-final next-action release captures");
    if (!nonfinal_next_release)
        return EXIT_FAILURE;
    const auto* nonfinal_next_release_pair = nonfinal_next_release->trace_pair();
    const auto nonfinal_next_release_action =
        nonfinal_next_release_pair != nullptr
            ? nonfinal_next_release_pair->input_trace().ActionAt(
                  0U, omega::app::kDiagnosticMenuNextAction)
            : std::nullopt;
    const omega::app::GpuHostSnapshot nonfinal_next_release_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(nonfinal_next_release_pair != nullptr &&
              nonfinal_next_release_pair->input_trace().first_frame_index() ==
                  nonfinal_next_release_index &&
              nonfinal_next_release_action &&
              nonfinal_next_release_action->held &&
              !nonfinal_next_release_action->pressed &&
              !nonfinal_next_release_action->released &&
              nonfinal_next_release->result().planned_simulation_steps == 0U &&
              nonfinal_next_release->result().executed_simulation_steps == 0U &&
              nonfinal_next_release->scheduler_state_before() ==
                  modal_scheduler_before &&
              nonfinal_next_release->scheduler_state_after() ==
                  modal_scheduler_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  modal_position_before &&
              IsOneVisibleMenuSubmission(
                  held_next_alias_gpu, nonfinal_next_release_gpu),
        "releasing Down cannot release action 3 or mutate the menu while S remains held");

    const omega::app::GpuHostSnapshot controls_entry_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    bool controls_entry_events = PushKey(SDL_SCANCODE_S, false) &&
                                 PushKey(SDL_SCANCODE_F1, true) &&
                                 PushKey(SDL_SCANCODE_W, true);
    for (std::size_t index = 0U; controls_entry_events && index < 4'093U; ++index)
        controls_entry_events = PushKey(SDL_SCANCODE_W, true);
    Check(controls_entry_events,
        "primary, previous, and Controls timing events enter together");
    auto controls_entry = app->RunWithCapture(1);
    Check(controls_entry.has_value(), "MainMenu-to-Controls activation captures");
    if (!controls_entry)
        return EXIT_FAILURE;
    const auto* controls_entry_pair = controls_entry->trace_pair();
    const auto controls_entry_elapsed = controls_entry_pair != nullptr
                                            ? controls_entry_pair->scheduler_elapsed_trace().FrameAt(0U)
                                            : std::nullopt;
    const auto controls_entry_primary = controls_entry_pair != nullptr
                                            ? controls_entry_pair->input_trace().ActionAt(
                                                  0U, omega::app::kDiagnosticMenuPrimaryAction)
                                            : std::nullopt;
    const auto controls_entry_next = controls_entry_pair != nullptr
                                         ? controls_entry_pair->input_trace().ActionAt(
                                               0U, omega::app::kDiagnosticMenuNextAction)
                                         : std::nullopt;
    const RunResult controls_entry_result = controls_entry->result();
    const omega::app::GpuHostSnapshot controls_entry_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(controls_entry_pair != nullptr && controls_entry_elapsed &&
              controls_entry_elapsed->elapsed > settings.frame.simulation_step &&
              controls_entry_primary && controls_entry_primary->held &&
              controls_entry_primary->pressed &&
              controls_entry_next && !controls_entry_next->held &&
              !controls_entry_next->pressed && controls_entry_next->released &&
              controls_entry_result.input_frames == 1U &&
              controls_entry_result.rendered_frames == 1 &&
              controls_entry_result.planned_simulation_steps == 0U &&
              controls_entry_result.executed_simulation_steps == 0U &&
              controls_entry->scheduler_state_before() == modal_scheduler_before &&
              controls_entry->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kControlsRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_controls_draw_list) &&
              IsOneModalCardSubmission(
                  controls_entry_gpu_before, controls_entry_gpu_after),
        "the last action-3 alias release emits once while primary priority enters "
        "Controls and every simulation owner stays frozen");

    Check(PushKey(SDL_SCANCODE_F1, true), "held Controls primary enters the queue");
    auto controls_held = app->RunWithCapture(1);
    Check(controls_held &&
              controls_held->result().planned_simulation_steps == 0U &&
              controls_held->result().executed_simulation_steps == 0U &&
              controls_held->scheduler_state_before() == modal_scheduler_before &&
              controls_held->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kControlsRowOne &&
              IsOneModalCardSubmission(controls_entry_gpu_after,
                  OmegaAppTestAccess::GpuSnapshot(*app)),
        "held primary does not repeat and Controls remains a one-card modal frame");

    Check(PushKey(SDL_SCANCODE_F1, false) && PushKey(SDL_SCANCODE_W, false),
        "Controls primary and held movement release");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kControlsRowOne &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before,
        "release edges preserve Controls and its frozen scheduler");

    const std::uint64_t controls_terminal_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    const omega::app::GpuHostSnapshot controls_terminal_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushEscape(true) && PushQuit(),
        "Controls primary and simultaneous terminal reasons enter the queue");
    auto controls_terminal = app->RunWithCapture(1);
    Check(controls_terminal.has_value(), "Controls terminal precedence captures");
    if (!controls_terminal)
        return EXIT_FAILURE;
    const auto* controls_terminal_pair = controls_terminal->trace_pair();
    const auto controls_terminal_reason = controls_terminal->terminal_input();
    const auto controls_terminal_primary = controls_terminal_pair != nullptr
                                               ? controls_terminal_pair->input_trace().ActionAt(
                                                     0U,
                                                     omega::app::kDiagnosticMenuPrimaryAction)
                                               : std::nullopt;
    Check(controls_terminal->completion() == RunCaptureCompletion::QuitRequested &&
              controls_terminal_reason && controls_terminal_pair != nullptr &&
              controls_terminal_reason->frame_index == controls_terminal_index &&
              controls_terminal_reason->host_quit_requested &&
              controls_terminal_reason->logical_quit_pressed &&
              controls_terminal_primary && controls_terminal_primary->held &&
              controls_terminal_primary->pressed &&
              controls_terminal->scheduler_state_before() == modal_scheduler_before &&
              controls_terminal->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kControlsRowOne &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_controls_draw_list) &&
              OmegaAppTestAccess::GpuSnapshot(*app) == controls_terminal_gpu_before,
        "terminal resolution captures the Controls primary edge without reducing, rendering, or mutating any owner");

    Check(PushEscape(false) && PushKey(SDL_SCANCODE_F1, false),
        "Controls terminal inputs release");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kControlsRowOne,
        "terminal release resumes the unchanged Controls screen");

    const omega::app::GpuHostSnapshot controls_exit_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    bool controls_exit_events = PushKey(SDL_SCANCODE_F1, true);
    for (std::size_t index = 0U; controls_exit_events && index < 4'095U; ++index)
        controls_exit_events = PushKey(SDL_SCANCODE_F1, true);
    Check(controls_exit_events, "fresh Controls return edge and timing workload enter");
    auto controls_exit = app->RunWithCapture(1);
    Check(controls_exit.has_value(), "Controls-to-MainMenu return captures");
    if (!controls_exit)
        return EXIT_FAILURE;
    const auto* controls_exit_pair = controls_exit->trace_pair();
    const auto controls_exit_elapsed = controls_exit_pair != nullptr
                                           ? controls_exit_pair->scheduler_elapsed_trace().FrameAt(0U)
                                           : std::nullopt;
    const RunResult controls_exit_result = controls_exit->result();
    const omega::app::GpuHostSnapshot controls_exit_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(controls_exit_pair != nullptr && controls_exit_elapsed &&
              controls_exit_elapsed->elapsed > settings.frame.simulation_step &&
              controls_exit_result.input_frames == 1U &&
              controls_exit_result.rendered_frames == 1 &&
              controls_exit_result.planned_simulation_steps == 0U &&
              controls_exit_result.executed_simulation_steps == 0U &&
              controls_exit->scheduler_state_before() == modal_scheduler_before &&
              controls_exit->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[1]) &&
              IsOneVisibleMenuSubmission(
                  controls_exit_gpu_before, controls_exit_gpu_after),
        "fresh primary returns Controls to MainMenu row one on the same frame without advancing accumulated menu time");
    Check(PushKey(SDL_SCANCODE_F1, false), "returned MainMenu primary releases");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne,
        "return release preserves MainMenu row one");

    Check(PushKey(SDL_SCANCODE_S, true), "row-two edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[2]),
        "next moves row one to row two");
    Check(PushKey(SDL_SCANCODE_S, false), "row-two edge releases");
    Check(RunPlainFrame(), "row-two release completes");
    Check(PushKey(SDL_SCANCODE_S, true), "lower-bound edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowTwo,
        "next clamps at row two instead of wrapping");
    Check(PushKey(SDL_SCANCODE_S, false), "lower-bound edge releases");
    Check(RunPlainFrame(), "lower-bound release completes");
    Check(PushKey(SDL_SCANCODE_W, true) && PushKey(SDL_SCANCODE_S, true),
        "simultaneous navigation edges enter the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowTwo,
        "simultaneous previous and next edges are neutral");
    Check(PushKey(SDL_SCANCODE_W, false) && PushKey(SDL_SCANCODE_S, false),
        "simultaneous navigation controls release");
    Check(RunPlainFrame(), "simultaneous navigation release completes");

    constexpr omega::app::DiagnosticMenuState kAssetTopologyRowTwo{
        .mode = omega::app::DiagnosticMenuMode::AssetTopology,
        .selected_row = omega::app::DiagnosticMenuRow::ShowAssetTopology,
    };
    const omega::app::GpuHostSnapshot topology_entry_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    bool topology_entry_events = PushKey(SDL_SCANCODE_F1, true) &&
                                 PushKey(SDL_SCANCODE_W, true);
    for (std::size_t index = 0U; topology_entry_events && index < 4'094U; ++index)
        topology_entry_events = PushKey(SDL_SCANCODE_W, true);
    Check(topology_entry_events,
        "primary, previous, and asset-topology timing events enter together");
    auto topology_entry = app->RunWithCapture(1);
    Check(topology_entry.has_value(), "MainMenu-to-AssetTopology activation captures");
    if (!topology_entry)
        return EXIT_FAILURE;
    const auto* topology_entry_pair = topology_entry->trace_pair();
    const auto topology_entry_elapsed = topology_entry_pair != nullptr
                                            ? topology_entry_pair->scheduler_elapsed_trace().FrameAt(0U)
                                            : std::nullopt;
    const auto topology_entry_primary = topology_entry_pair != nullptr
                                            ? topology_entry_pair->input_trace().ActionAt(
                                                  0U, omega::app::kDiagnosticMenuPrimaryAction)
                                            : std::nullopt;
    const RunResult topology_entry_result = topology_entry->result();
    const omega::app::GpuHostSnapshot topology_entry_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_entry_pair != nullptr && topology_entry_elapsed &&
              topology_entry_elapsed->elapsed > settings.frame.simulation_step &&
              topology_entry_primary && topology_entry_primary->held &&
              topology_entry_primary->pressed &&
              topology_entry_result.input_frames == 1U &&
              topology_entry_result.rendered_frames == 1 &&
              topology_entry_result.planned_simulation_steps == 0U &&
              topology_entry_result.executed_simulation_steps == 0U &&
              topology_entry->scheduler_state_before() == modal_scheduler_before &&
              topology_entry->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kAssetTopologyRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(
                  topology_entry_gpu_before, topology_entry_gpu_after),
        "primary priority enters AssetTopology on the same frame while raw elapsed remains captured and every simulation owner stays frozen");

    Check(PushKey(SDL_SCANCODE_F1, true),
        "held AssetTopology primary enters the queue");
    auto topology_held = app->RunWithCapture(1);
    Check(topology_held.has_value(), "held AssetTopology primary captures");
    if (!topology_held)
        return EXIT_FAILURE;
    const auto* topology_held_pair = topology_held->trace_pair();
    const auto topology_held_primary = topology_held_pair != nullptr
                                           ? topology_held_pair->input_trace().ActionAt(
                                                 0U, omega::app::kDiagnosticMenuPrimaryAction)
                                           : std::nullopt;
    const omega::app::GpuHostSnapshot topology_held_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_held_primary && topology_held_primary->held &&
              !topology_held_primary->pressed && !topology_held_primary->released &&
              topology_held->result().planned_simulation_steps == 0U &&
              topology_held->result().executed_simulation_steps == 0U &&
              topology_held->scheduler_state_before() == modal_scheduler_before &&
              topology_held->scheduler_state_after() == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kAssetTopologyRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(topology_entry_gpu_after, topology_held_gpu),
        "held primary does not repeat and AssetTopology remains an exact one-card modal frame");

    Check(PushKey(SDL_SCANCODE_F1, false) && PushKey(SDL_SCANCODE_W, false),
        "AssetTopology primary and held navigation release");
    auto topology_released = app->RunWithCapture(1);
    Check(topology_released.has_value(), "AssetTopology release captures");
    if (!topology_released)
        return EXIT_FAILURE;
    const auto* topology_released_pair = topology_released->trace_pair();
    const auto topology_released_primary = topology_released_pair != nullptr
                                               ? topology_released_pair->input_trace().ActionAt(
                                                     0U,
                                                     omega::app::kDiagnosticMenuPrimaryAction)
                                               : std::nullopt;
    const omega::app::GpuHostSnapshot topology_released_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_released_primary && !topology_released_primary->held &&
              !topology_released_primary->pressed && topology_released_primary->released &&
              topology_released->result().planned_simulation_steps == 0U &&
              topology_released->result().executed_simulation_steps == 0U &&
              topology_released->scheduler_state_before() == modal_scheduler_before &&
              topology_released->scheduler_state_after() == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kAssetTopologyRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(topology_held_gpu, topology_released_gpu),
        "release edges preserve AssetTopology, its one-card render, and every frozen simulation owner");

    const std::uint64_t topology_terminal_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushEscape(true) && PushQuit(),
        "AssetTopology primary and simultaneous terminal reasons enter the queue");
    auto topology_terminal = app->RunWithCapture(1);
    Check(topology_terminal.has_value(), "AssetTopology terminal precedence captures");
    if (!topology_terminal)
        return EXIT_FAILURE;
    const auto* topology_terminal_pair = topology_terminal->trace_pair();
    const auto topology_terminal_reason = topology_terminal->terminal_input();
    const auto topology_terminal_primary = topology_terminal_pair != nullptr
                                               ? topology_terminal_pair->input_trace().ActionAt(
                                                     0U,
                                                     omega::app::kDiagnosticMenuPrimaryAction)
                                               : std::nullopt;
    Check(topology_terminal->completion() == RunCaptureCompletion::QuitRequested &&
              topology_terminal_reason && topology_terminal_pair != nullptr &&
              topology_terminal_reason->frame_index == topology_terminal_index &&
              topology_terminal_reason->host_quit_requested &&
              topology_terminal_reason->logical_quit_pressed &&
              topology_terminal_primary && topology_terminal_primary->held &&
              topology_terminal_primary->pressed &&
              topology_terminal->scheduler_state_before() == modal_scheduler_before &&
              topology_terminal->scheduler_state_after() == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kAssetTopologyRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_asset_topology_draw_list) &&
              OmegaAppTestAccess::GpuSnapshot(*app) == topology_released_gpu,
        "terminal resolution captures the AssetTopology primary edge without reducing, rendering, or mutating any owner");

    Check(PushEscape(false) && PushKey(SDL_SCANCODE_F1, false),
        "AssetTopology terminal inputs release");
    const omega::app::GpuHostSnapshot topology_terminal_release_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(RunPlainFrame(), "AssetTopology terminal release frame completes");
    const omega::app::GpuHostSnapshot topology_terminal_release_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) == kAssetTopologyRowTwo &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_asset_topology_draw_list) &&
              IsOneModalCardSubmission(topology_terminal_release_gpu_before,
                  topology_terminal_release_gpu_after),
        "terminal release resumes the unchanged AssetTopology one-card screen");

    const omega::app::GpuHostSnapshot topology_exit_gpu_before =
        OmegaAppTestAccess::GpuSnapshot(*app);
    bool topology_exit_events = PushKey(SDL_SCANCODE_F1, true);
    for (std::size_t index = 0U; topology_exit_events && index < 4'095U; ++index)
        topology_exit_events = PushKey(SDL_SCANCODE_F1, true);
    Check(topology_exit_events,
        "fresh AssetTopology return edge and timing workload enter");
    auto topology_exit = app->RunWithCapture(1);
    Check(topology_exit.has_value(), "AssetTopology-to-MainMenu return captures");
    if (!topology_exit)
        return EXIT_FAILURE;
    const auto* topology_exit_pair = topology_exit->trace_pair();
    const auto topology_exit_elapsed = topology_exit_pair != nullptr
                                           ? topology_exit_pair->scheduler_elapsed_trace().FrameAt(0U)
                                           : std::nullopt;
    const RunResult topology_exit_result = topology_exit->result();
    const omega::app::GpuHostSnapshot topology_exit_gpu_after =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(topology_exit_pair != nullptr && topology_exit_elapsed &&
              topology_exit_elapsed->elapsed > settings.frame.simulation_step &&
              topology_exit_result.input_frames == 1U &&
              topology_exit_result.rendered_frames == 1 &&
              topology_exit_result.planned_simulation_steps == 0U &&
              topology_exit_result.executed_simulation_steps == 0U &&
              topology_exit->scheduler_state_before() == modal_scheduler_before &&
              topology_exit->scheduler_state_after() == modal_scheduler_before &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == modal_scheduler_before &&
              SameSimulationState(OmegaAppTestAccess::SimulationSnapshot(*app),
                  modal_simulation_before) &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) == modal_position_before &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowTwo &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[2]) &&
              IsOneVisibleMenuSubmission(
                  topology_exit_gpu_before, topology_exit_gpu_after),
        "fresh primary returns AssetTopology to MainMenu row two on the same frame without advancing accumulated modal time");
    Check(PushKey(SDL_SCANCODE_F1, false),
        "returned AssetTopology MainMenu primary releases");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowTwo,
        "AssetTopology return release preserves MainMenu row two");

    for (int row = 0; row < 2; ++row)
    {
        Check(PushKey(SDL_SCANCODE_UP, true),
            "Up-arrow previous-row edge enters the SDL queue");
        Check(RunPlainFrame(), "Up-arrow previous-row frame completes");
        Check(PushKey(SDL_SCANCODE_UP, false),
            "Up-arrow previous-row edge releases");
        Check(RunPlainFrame(), "Up-arrow previous-row release completes");
    }
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) ==
              omega::app::InitialDiagnosticMenuState(),
        "two previous edges return row two to row zero");
    Check(PushKey(SDL_SCANCODE_UP, true),
        "Up-arrow upper-bound edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState(),
        "Up-arrow previous clamps at row zero instead of wrapping");
    const omega::runtime::FrameSchedulerState play_resume_scheduler_before =
        OmegaAppTestAccess::SchedulerSnapshot(*app);
    Check(PushKey(SDL_SCANCODE_UP, false) && PushKey(SDL_SCANCODE_F1, true),
        "the Up-arrow alias releases as the row-zero primary edge enters");
    auto play_resume = app->RunWithCapture(1);
    Check(play_resume.has_value(), "row-zero primary activation captures");
    if (!play_resume)
        return EXIT_FAILURE;
    const auto* play_resume_pair = play_resume->trace_pair();
    const auto play_resume_elapsed = play_resume_pair != nullptr
                                         ? play_resume_pair->scheduler_elapsed_trace().FrameAt(0U)
                                         : std::nullopt;
    std::optional<ExpectedSchedulerAdvance> expected_play_resume;
    if (play_resume_elapsed)
    {
        expected_play_resume = AdvanceSchedulerSnapshot(
            play_resume_scheduler_before, play_resume_elapsed->elapsed);
    }
    const RunResult play_resume_result = play_resume->result();
    Check(play_resume_pair != nullptr && play_resume_elapsed && expected_play_resume &&
              play_resume_scheduler_before == modal_scheduler_before &&
              play_resume->scheduler_state_before() == play_resume_scheduler_before &&
              play_resume->scheduler_state_after() == expected_play_resume->state &&
              OmegaAppTestAccess::SchedulerSnapshot(*app) == expected_play_resume->state &&
              play_resume_result.planned_simulation_steps ==
                  expected_play_resume->plan.simulation_steps &&
              play_resume_result.executed_simulation_steps ==
                  expected_play_resume->plan.simulation_steps &&
              play_resume_result.clamped_frame_count ==
                  (expected_play_resume->plan.clamped_delta ? 1U : 0U) &&
              play_resume_result.dropped_time_frame_count ==
                  (expected_play_resume->plan.dropped_time ? 1U : 0U) &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero,
        "row-zero activation resumes from the frozen scheduler using only its own captured elapsed sample, with no modal-card catch-up");
    Check(PushKey(SDL_SCANCODE_F1, false), "row-zero primary releases");
    auto ready_for_terminal = app->RunWithCapture(1);
    Check(ready_for_terminal.has_value(),
        "DiagnosticPlay is ready for a terminal-priority frame");
    if (!ready_for_terminal)
        return EXIT_FAILURE;
    const omega::app::GpuHostSnapshot ready_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);

    Check(DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
              initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app),
                   initial_visible_draw_lists) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticControlsDrawList(*app),
                   initial_controls_draw_list) &&
              DrawListsEqual(
                  OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(*app),
                  initial_asset_topology_draw_list) &&
              OmegaAppTestAccess::DiagnosticMenuTexture(*app) ==
                  diagnostic_menu_texture &&
              OmegaAppTestAccess::DiagnosticControlsTexture(*app) ==
                  diagnostic_controls_texture &&
              OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app) ==
                  diagnostic_asset_topology_texture &&
              SameTextureResidency(initial_gpu, ready_gpu),
        "navigation preserves all three immutable card resources and their three uploads");

    const auto debug_position_before_terminal =
        OmegaAppTestAccess::DebugLocomotionPosition(*app);
    const omega::runtime::FrameSchedulerState scheduler_before_terminal =
        ready_for_terminal->scheduler_state_after();
    const std::uint64_t terminal_frame_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_F1, true) && PushEscape(true) && PushQuit(),
        "a fresh menu edge and simultaneous quit reasons enter the SDL queue");
    auto both = app->RunWithCapture(1);
    Check(both.has_value(), "simultaneous quit reasons publish a terminal capture");
    if (!both)
        return EXIT_FAILURE;
    const auto* both_pair = both->trace_pair();
    const auto both_terminal = both->terminal_input();
    const auto terminal_menu_action = both_pair != nullptr
                                          ? both_pair->input_trace().ActionAt(
                                                0U,
                                                omega::app::kDiagnosticMenuToggleAction)
                                          : std::nullopt;
    const omega::app::GpuHostSnapshot terminal_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(both->completion() == RunCaptureCompletion::QuitRequested && both_terminal &&
              both_pair != nullptr &&
              both_terminal->frame_index == terminal_frame_index &&
              both_terminal->host_quit_requested &&
              both_terminal->logical_quit_pressed &&
              terminal_menu_action && terminal_menu_action->held &&
              terminal_menu_action->pressed && !terminal_menu_action->released &&
              both->scheduler_state_before() == scheduler_before_terminal &&
              both->scheduler_state_after() == scheduler_before_terminal &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DiagnosticMenuTexture(*app) ==
                  diagnostic_menu_texture &&
              OmegaAppTestAccess::DiagnosticControlsTexture(*app) ==
                   diagnostic_controls_texture &&
              OmegaAppTestAccess::DiagnosticAssetTopologyTexture(*app) ==
                  diagnostic_asset_topology_texture &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  debug_position_before_terminal &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
                  initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app),
                  initial_visible_draw_lists) &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticControlsDrawList(*app),
                   initial_controls_draw_list) &&
              DrawListsEqual(
                  OmegaAppTestAccess::DiagnosticAssetTopologyDrawList(*app),
                  initial_asset_topology_draw_list) &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_hidden_draw_list) &&
              terminal_gpu == ready_gpu,
        "a terminal action-6 edge performs no render or menu/resource mutation");

    Check(PushEscape(false) && PushKey(SDL_SCANCODE_F1, false),
        "the final Escape and F1 releases enter the SDL queue");
    Check(omega::app::detail::OmegaAppTestAccess::InstallUnownedDiagnosticDraw(*app),
        "the operational-failure fixture installs an unowned diagnostic draw");
    const omega::app::GpuHostSnapshot failure_gpu_before =
        omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*app);
    const std::uint64_t failure_frame_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    auto failed = app->RunWithCapture(1);
    Check(failed.has_value(), "a render error publishes a partial capture outcome");
    if (!failed)
        return EXIT_FAILURE;
    const auto* failed_pair = failed->trace_pair();
    const RunResult failed_result = failed->result();
    Check(failed->completion() == RunCaptureCompletion::OperationalFailure &&
              failed->failure() == std::optional<std::string_view>{
                                       "render frame draw texture resolve: invalid-handle"} &&
              failed_pair != nullptr &&
              failed_result.input_frames == 1U && failed_result.rendered_frames == 0 &&
              failed_result.planned_simulation_steps ==
                  failed_result.executed_simulation_steps &&
              !failed_result.quit_requested,
        "a real render failure retains partial counters, failure text, and traces");
    omega::app::GpuHostSnapshot expected_failure_gpu = failure_gpu_before;
    ++expected_failure_gpu.rejected_nondefault_texture_handles;
    const omega::app::GpuHostSnapshot failure_gpu_after =
        omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*app);
    Check(failure_gpu_after == expected_failure_gpu,
        "the rejected handle changes only its pre-acquisition diagnostic counter");
    if (failed_pair != nullptr)
    {
        Check(failed_pair->input_trace().first_frame_index() == failure_frame_index &&
                  failed_pair->input_trace().frame_count() == 1U &&
                  failed_pair->scheduler_elapsed_trace().first_frame_index() ==
                      failure_frame_index &&
                  failed_pair->scheduler_elapsed_trace().frame_count() == 1U &&
                  !failed_pair->terminal_input(),
            "the failed render remains after one exact paired input and elapsed sample");
    }
    const omega::runtime::FrameSchedulerState failed_after =
        failed->scheduler_state_after();

    omega::app::detail::OmegaAppTestAccess::ClearDiagnosticDraw(*app);
    const std::uint64_t continued_frame_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    auto continued = app->RunWithCapture(1);
    Check(continued.has_value(), "capture continues after clearing the render fixture");
    if (!continued)
        return EXIT_FAILURE;
    const auto* continued_pair = continued->trace_pair();
    const omega::app::GpuHostSnapshot continued_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(continued->completion() == RunCaptureCompletion::FrameLimitReached &&
              continued_pair != nullptr &&
              continued->scheduler_state_before() == failed_after &&
              continued->result().input_frames == 1U &&
              continued->result().rendered_frames == 1 &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              IsOneHiddenMenuSubmission(failure_gpu_after, continued_gpu),
        "capture resumes with one clear-only hidden submission at the scheduler boundary");
    if (continued_pair != nullptr)
    {
        Check(continued_pair->input_trace().first_frame_index() ==
                      continued_frame_index &&
                  continued_pair->scheduler_elapsed_trace().first_frame_index() ==
                      continued_frame_index,
            "sequential capture continues the global input frame index");
    }

    const auto plain = app->Run(1);
    const omega::app::GpuHostSnapshot plain_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(plain && plain->rendered_frames == 1 && plain->input_frames == 1U &&
              !plain->quit_requested &&
              IsOneHiddenMenuSubmission(continued_gpu, plain_gpu),
        "plain Run adds one hidden clear submission without reuploading any card");

    if (failures == 0)
        std::cout << "omega_app_capture_smoke: passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
