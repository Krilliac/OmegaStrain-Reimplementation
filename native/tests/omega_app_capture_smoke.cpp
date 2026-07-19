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
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

namespace omega::app::detail
{
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
        return true;
    }

    static void ClearDiagnosticDraw(OmegaApp& app) noexcept
    {
        app.diagnostic_hidden_draw_list_ = {};
        for (runtime::RenderDrawList& draw_list : app.diagnostic_visible_draw_lists_)
            draw_list = {};
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
                  static_cast<std::uint16_t>(SDL_SCANCODE_S),
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
        "the synthetic W/S/A/D and gamepad dpad bindings expose action IDs 2 through 5");
    Check(OmegaAppTestAccess::HasInputBinding(*app, InputDevice::Keyboard,
              static_cast<std::uint16_t>(SDL_SCANCODE_F1),
              omega::app::kDiagnosticMenuToggleAction) &&
              OmegaAppTestAccess::HasInputBinding(*app, InputDevice::GamepadButton,
                  static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_START),
                  omega::app::kDiagnosticMenuToggleAction),
        "F1 and gamepad Start bind the project diagnostic-menu action 6");

    const omega::runtime::RenderTextureHandle diagnostic_texture =
        OmegaAppTestAccess::DiagnosticTexture(*app);
    const omega::runtime::RenderTextureHandle diagnostic_menu_texture =
        OmegaAppTestAccess::DiagnosticMenuTexture(*app);
    const omega::runtime::RenderDrawList initial_hidden_draw_list =
        OmegaAppTestAccess::DiagnosticHiddenDrawList(*app);
    const std::array<omega::runtime::RenderDrawList,
        omega::app::kDiagnosticMenuRowCount> initial_visible_draw_lists =
        OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app);
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
    Check(!diagnostic_texture.valid() && diagnostic_menu_texture.valid() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState() &&
              initial_hidden_draw_list.empty() && visible_lists_are_exact &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[0]),
        "the zero-file host starts in MainMenu row zero with three exact card-and-marker lists");

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
    OmegaAppTestAccess::SetDiagnosticMenuState(
        *app, omega::app::InitialDiagnosticMenuState());

    constexpr std::uint64_t kDiagnosticMenuLogicalBytes = 128ULL * 72ULL * 4ULL;
    const omega::app::GpuHostSnapshot initial_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(initial_gpu.successful_uploads == 1U &&
              initial_gpu.successful_upload_logical_bytes ==
                  kDiagnosticMenuLogicalBytes &&
              initial_gpu.successful_releases == 0U &&
              initial_gpu.textures.reserved_slots == 0U &&
              initial_gpu.textures.resident_slots == 1U &&
              initial_gpu.textures.resident_logical_bytes ==
                  kDiagnosticMenuLogicalBytes,
        "the project menu is uploaded exactly once and remains host-resident");

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
                                  PushKey(SDL_SCANCODE_F1, true) &&
                                  PushKey(SDL_SCANCODE_W, true);
    // Keep the real SDL pump busy for longer than the minimum synthetic step without sleeping.
    // Duplicate level reports are explicitly accepted no-ops after the first held transition.
    for (std::size_t index = 0U; movement_events_queued && index < 2'048U; ++index)
        movement_events_queued = PushKey(SDL_SCANCODE_W, true);
    Check(movement_events_queued,
        "the same-frame F1 and movement fixture enters the real SDL event queue");
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
        "one F1-plus-W frame enters diagnostic play and applies movement nonmodally");
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
              DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
                  initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app),
                  initial_visible_draw_lists) &&
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
            "normal capture records the exact six-action schema and simultaneous F1/W edges");
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
                  omega::simulation::Position3{},
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
              normal_result.planned_simulation_steps ==
                  captured_plan->simulation_steps &&
              normal_result.executed_simulation_steps ==
                  captured_plan->simulation_steps &&
              replay_session.state() ==
                  omega::app::RunReplaySessionState::Complete &&
              replay_session.remaining_frames() == 0U,
        "replay ignores action 6 for locomotion and reaches the captured fresh-world position");

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
        .selected_row = omega::app::DiagnosticMenuRow::ReservedProjectOne,
    };
    constexpr omega::app::DiagnosticMenuState kMainMenuRowTwo{
        .mode = omega::app::DiagnosticMenuMode::MainMenu,
        .selected_row = omega::app::DiagnosticMenuRow::ReservedProjectTwo,
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
        "held primary and forward release enter the SDL queue");
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
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  position_after_primary &&
              IsOneHiddenMenuSubmission(normal_gpu, held_gpu),
        "held action 6 does not repeat its press edge or reopen the menu");

    Check(PushKey(SDL_SCANCODE_F1, false), "primary release enters the SDL queue");
    Check(RunPlainFrame(), "primary release frame completes");
    const omega::app::GpuHostSnapshot released_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero &&
              IsOneHiddenMenuSubmission(held_gpu, released_gpu),
        "primary release preserves DiagnosticPlay with a clear-only frame");

    Check(PushKey(SDL_SCANCODE_F1, true), "fresh primary edge enters the SDL queue");
    Check(RunPlainFrame(), "fresh primary frame completes");
    const omega::app::GpuHostSnapshot reopened_gpu =
        OmegaAppTestAccess::GpuSnapshot(*app);
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState() &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[0]) &&
              IsOneVisibleMenuSubmission(released_gpu, reopened_gpu),
        "F1 reopens MainMenu at row zero with exactly two resident blits");
    Check(PushKey(SDL_SCANCODE_F1, false), "reopened primary releases");
    Check(RunPlainFrame(), "reopened release frame completes");

    const std::uint64_t next_edge_index =
        OmegaAppTestAccess::NextInputFrameIndex(*app);
    Check(PushKey(SDL_SCANCODE_S, true), "next-row edge enters the SDL queue");
    auto next_edge = app->RunWithCapture(1);
    Check(next_edge.has_value(), "next-row edge captures");
    if (!next_edge)
        return EXIT_FAILURE;
    const auto* next_pair = next_edge->trace_pair();
    const auto next_action = next_pair != nullptr
                                 ? next_pair->input_trace().ActionAt(
                                       0U, omega::app::kDiagnosticMenuNextAction)
                                 : std::nullopt;
    Check(next_pair != nullptr &&
              next_pair->input_trace().first_frame_index() == next_edge_index &&
              next_action && next_action->held && next_action->pressed &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne &&
              DrawListsEqual(OmegaAppTestAccess::CurrentDiagnosticDrawList(*app),
                  initial_visible_draw_lists[1]),
        "S/action 3 moves row zero to row one on its press edge");
    Check(PushKey(SDL_SCANCODE_S, true), "held next level enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne,
        "held action 3 does not repeat navigation");

    Check(PushKey(SDL_SCANCODE_S, false) && PushKey(SDL_SCANCODE_F1, true) &&
              PushKey(SDL_SCANCODE_W, true),
        "reserved primary plus previous edge enter together");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kMainMenuRowOne,
        "reserved primary is inert and has priority over simultaneous navigation");
    Check(PushKey(SDL_SCANCODE_F1, false) && PushKey(SDL_SCANCODE_W, false),
        "reserved-primary controls release");
    Check(RunPlainFrame(), "reserved-primary release frame completes");

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

    for (int row = 0; row < 2; ++row)
    {
        Check(PushKey(SDL_SCANCODE_W, true), "previous-row edge enters the SDL queue");
        Check(RunPlainFrame(), "previous-row frame completes");
        Check(PushKey(SDL_SCANCODE_W, false), "previous-row edge releases");
        Check(RunPlainFrame(), "previous-row release completes");
    }
    Check(OmegaAppTestAccess::DiagnosticMenu(*app) ==
              omega::app::InitialDiagnosticMenuState(),
        "two previous edges return row two to row zero");
    Check(PushKey(SDL_SCANCODE_W, true), "upper-bound edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) ==
                  omega::app::InitialDiagnosticMenuState(),
        "previous clamps at row zero instead of wrapping");
    Check(PushKey(SDL_SCANCODE_W, false) && PushKey(SDL_SCANCODE_F1, true),
        "row-zero primary edge enters the SDL queue");
    Check(RunPlainFrame() &&
              OmegaAppTestAccess::DiagnosticMenu(*app) == kDiagnosticPlayRowZero,
        "row-zero primary enters DiagnosticPlay");
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
              SameTextureResidency(initial_gpu, ready_gpu),
        "navigation preserves all immutable menu resources and the single upload");

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
              OmegaAppTestAccess::DebugLocomotionPosition(*app) ==
                  debug_position_before_terminal &&
              DrawListsEqual(OmegaAppTestAccess::DiagnosticHiddenDrawList(*app),
                  initial_hidden_draw_list) &&
              DrawListArraysEqual(OmegaAppTestAccess::DiagnosticVisibleDrawLists(*app),
                  initial_visible_draw_lists) &&
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
        "plain Run adds one hidden clear submission without reuploading the menu");

    if (failures == 0)
        std::cout << "omega_app_capture_smoke: passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
