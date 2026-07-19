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
        app.diagnostic_draw_list_ = std::move(*created);
        return true;
    }

    static void ClearDiagnosticDraw(OmegaApp& app) noexcept
    {
        app.diagnostic_draw_list_ = {};
    }

    [[nodiscard]] static GpuHostSnapshot GpuSnapshot(const OmegaApp& app) noexcept
    {
        return app.host_->Snapshot();
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
    auto app = omega::app::OmegaApp::Create(std::move(*config), settings,
        omega::runtime::ContentStartupState{}, false);
    Check(app.has_value(), "the zero-file OmegaApp fixture starts");
    if (!app)
    {
        std::cerr << app.error() << '\n';
        return EXIT_FAILURE;
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
              host->scheduler_state_before() == host->scheduler_state_after(),
        "host quit ends before clock, scheduler, simulation, and rendering work");
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
              logical->result().rendered_frames == 0,
        "logical quit also ends before an elapsed sample or render");
    if (logical_pair != nullptr && logical_terminal)
    {
        Check(logical_pair->input_trace().first_frame_index() == 1U &&
                  logical_pair->scheduler_elapsed_trace().frame_count() == 0U &&
                  logical_terminal->frame_index == 1U &&
                  !logical_terminal->host_quit_requested &&
                  logical_terminal->logical_quit_pressed,
            "logical quit retains its distinct owned reason and continued index");
    }

    Check(PushEscape(false), "the Escape release enters the SDL queue");
    auto normal = app->RunWithCapture(1);
    Check(normal.has_value(), "a released quit action permits one captured render");
    if (!normal)
        return EXIT_FAILURE;
    const auto* normal_pair = normal->trace_pair();
    const RunResult normal_result = normal->result();
    Check(normal->completion() == RunCaptureCompletion::FrameLimitReached &&
              !normal->failure() && normal_pair != nullptr &&
              normal_result.input_frames == 1U && normal_result.rendered_frames == 1 &&
              !normal_result.quit_requested,
        "one normal frame publishes aligned input, elapsed, and render counts");

    const omega::runtime::FrameSchedulerState normal_before =
        normal->scheduler_state_before();
    const omega::runtime::FrameSchedulerState normal_after =
        normal->scheduler_state_after();
    std::optional<omega::runtime::InputTraceFrameState> captured_input;
    std::optional<omega::runtime::SchedulerElapsedFrameState> captured_elapsed;
    std::optional<omega::runtime::FramePlan> captured_plan;
    std::array<std::uint32_t, omega::runtime::InputBindingTable::kMaxActions>
        captured_actions{};
    std::array<omega::runtime::InputTraceActionState,
        omega::runtime::InputBindingTable::kMaxActions>
        captured_action_states{};
    std::size_t captured_action_count = 0U;
    bool captured_action_states_valid = true;
    if (normal_pair != nullptr)
    {
        captured_input = normal_pair->input_trace().FrameAt(0U);
        captured_elapsed = normal_pair->scheduler_elapsed_trace().FrameAt(0U);
        const auto action_schema = normal_pair->input_trace().actions();
        captured_action_count = action_schema.size();
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
                  captured_input && captured_elapsed && captured_action_count > 0U &&
                  captured_action_states_valid,
            "normal capture aligns the continued input index with one elapsed sample");
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
              captured_action_states_valid,
        "the real capture publishes complete owned replay inputs");
    if (!replay_traces || !captured_input || !captured_elapsed || !captured_plan ||
        !captured_action_states_valid)
    {
        return EXIT_FAILURE;
    }

    auto replay_created = omega::app::RunReplaySession::Create(
        std::move(*replay_traces),
        omega::app::RunReplaySessionConfig{.scheduler = normal_before.config});
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
              replay_simulation_before->alive_entities == 0U,
        "real-host replay begins with the captured scheduler boundary and a fresh world");

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
              replay_frame->elapsed() == captured_elapsed->elapsed &&
              !replay_frame->terminal_input() && replay_plan &&
              replay_plan->simulation_steps == captured_plan->simulation_steps &&
              replay_plan->interpolation_alpha == captured_plan->interpolation_alpha &&
              replay_plan->clamped_delta == captured_plan->clamped_delta &&
              replay_plan->dropped_time == captured_plan->dropped_time,
        "app replay preserves the actual input, elapsed value, and exact scheduler plan");

    const auto replay_scheduler_after = replay_session.scheduler_state();
    const auto replay_simulation_after = replay_session.simulation_state();
    const auto expected_fresh_time = normal_before.config.simulation_step *
                                     captured_plan->simulation_steps;
    Check(replay_scheduler_after && *replay_scheduler_after == normal_after &&
              replay_simulation_after &&
              replay_simulation_after->completed_steps ==
                  captured_plan->simulation_steps &&
              replay_simulation_after->simulated_time == expected_fresh_time &&
              replay_simulation_after->alive_entities == 0U &&
              normal_result.planned_simulation_steps ==
                  captured_plan->simulation_steps &&
              normal_result.executed_simulation_steps ==
                  captured_plan->simulation_steps &&
              replay_session.state() ==
                  omega::app::RunReplaySessionState::Complete &&
              replay_session.remaining_frames() == 0U,
        "real-host replay reaches the captured scheduler state in a fresh completed world");

    const auto replay_complete = replay_session.Next();
    Check(!replay_complete &&
              replay_complete.error().operation == omega::app::RunReplayOperation::Next &&
              replay_complete.error().code ==
                  omega::app::RunReplayErrorCode::ReplayComplete &&
              !replay_complete.error().replay_error,
        "the consumed real-host capture reports stable app replay completion");

    Check(PushEscape(true) && PushQuit(),
        "simultaneous logical and host quit events enter the SDL queue");
    auto both = app->RunWithCapture(1);
    Check(both.has_value(), "simultaneous quit reasons publish a terminal capture");
    if (!both)
        return EXIT_FAILURE;
    const auto both_terminal = both->terminal_input();
    Check(both->completion() == RunCaptureCompletion::QuitRequested && both_terminal &&
              both_terminal->frame_index == 3U &&
              both_terminal->host_quit_requested &&
              both_terminal->logical_quit_pressed &&
              both->scheduler_state_before() == normal_after &&
              both->scheduler_state_after() == normal_after,
        "both quit reasons survive one terminal frame without scheduler mutation");

    Check(PushEscape(false), "the final Escape release enters the SDL queue");
    Check(omega::app::detail::OmegaAppTestAccess::InstallUnownedDiagnosticDraw(*app),
        "the operational-failure fixture installs an unowned diagnostic draw");
    const omega::app::GpuHostSnapshot failure_gpu_before =
        omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*app);
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
    Check(omega::app::detail::OmegaAppTestAccess::GpuSnapshot(*app) ==
              expected_failure_gpu,
        "the rejected handle changes only its pre-acquisition diagnostic counter");
    if (failed_pair != nullptr)
    {
        Check(failed_pair->input_trace().first_frame_index() == 4U &&
                  failed_pair->input_trace().frame_count() == 1U &&
                  failed_pair->scheduler_elapsed_trace().first_frame_index() == 4U &&
                  failed_pair->scheduler_elapsed_trace().frame_count() == 1U &&
                  !failed_pair->terminal_input(),
            "the failed render remains after one exact paired input and elapsed sample");
    }
    const omega::runtime::FrameSchedulerState failed_after =
        failed->scheduler_state_after();

    omega::app::detail::OmegaAppTestAccess::ClearDiagnosticDraw(*app);
    auto continued = app->RunWithCapture(1);
    Check(continued.has_value(), "capture continues after clearing the render fixture");
    if (!continued)
        return EXIT_FAILURE;
    const auto* continued_pair = continued->trace_pair();
    Check(continued->completion() == RunCaptureCompletion::FrameLimitReached &&
              continued_pair != nullptr &&
              continued->scheduler_state_before() == failed_after &&
              continued->result().input_frames == 1U &&
              continued->result().rendered_frames == 1,
        "capture resumes from the failed run's exact scheduler boundary");
    if (continued_pair != nullptr)
    {
        Check(continued_pair->input_trace().first_frame_index() == 5U &&
                  continued_pair->scheduler_elapsed_trace().first_frame_index() == 5U,
            "sequential capture continues the global input frame index");
    }

    const auto plain = app->Run(1);
    Check(plain && plain->rendered_frames == 1 && plain->input_frames == 1U &&
              !plain->quit_requested,
        "the unchanged plain Run path still renders one frame after captured runs");

    if (failures == 0)
        std::cout << "omega_app_capture_smoke: passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
