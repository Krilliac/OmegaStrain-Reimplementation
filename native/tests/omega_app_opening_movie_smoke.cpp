#include "opening_movie_player.h"
#include "opening_movie_safety.h"
#include "omega_app.h"

#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/runtime_settings.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace omega::app::detail
{
struct OmegaAppTestAccess final
{
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        std::unique_ptr<OpeningMoviePlayback> opening_movie_playback,
        std::optional<std::filesystem::path> opening_movie_path = std::nullopt)
    {
        return OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
            std::move(config), settings, runtime::ContentStartupState{}, nullptr,
            false, {}, std::move(opening_movie_path),
            std::move(opening_movie_playback));
    }

    [[nodiscard]] static BootSequenceState BootSequence(
        const OmegaApp& app) noexcept
    {
        return app.boot_sequence_state_;
    }

    [[nodiscard]] static FrontEndState FrontEnd(const OmegaApp& app) noexcept
    {
        return app.front_end_state_;
    }

    [[nodiscard]] static simulation::SimulationState Simulation(
        const OmegaApp& app) noexcept
    {
        return app.simulation_->Snapshot();
    }

    [[nodiscard]] static GpuHostSnapshot Gpu(const OmegaApp& app) noexcept
    {
        return app.host_->Snapshot();
    }

    [[nodiscard]] static AudioServiceSnapshot Audio(const OmegaApp& app) noexcept
    {
        return app.audio_->Snapshot();
    }

    [[nodiscard]] static SdlAudioService* MutableAudio(OmegaApp& app) noexcept
    {
        return app.audio_.get();
    }

    [[nodiscard]] static std::vector<runtime::LogRecord> Logs(
        const OmegaApp& app)
    {
        return app.ring_sink_ ? app.ring_sink_->Snapshot()
                              : std::vector<runtime::LogRecord>{};
    }

    [[nodiscard]] static bool HasOpeningMoviePlayback(
        const OmegaApp& app) noexcept
    {
        return app.opening_movie_player_ != nullptr;
    }

    [[nodiscard]] static bool HasOpeningMovieTexture(
        const OmegaApp& app) noexcept
    {
        return app.opening_movie_texture_.valid();
    }

    [[nodiscard]] static std::size_t OpeningMovieDrawCommandCount(
        const OmegaApp& app) noexcept
    {
        return app.opening_movie_draw_list_.commands().size();
    }

    [[nodiscard]] static OpeningMovieAudioClockState OpeningMovieAudioClock(
        const OmegaApp& app) noexcept
    {
        return app.opening_movie_audio_clock_;
    }

    [[nodiscard]] static bool OpeningMovieAudioScratchIsZero(
        const OmegaApp& app) noexcept
    {
        return std::all_of(app.opening_movie_audio_scratch_.begin(),
            app.opening_movie_audio_scratch_.end(),
            [](const std::int16_t sample) { return sample == 0; });
    }
};
} // namespace omega::app::detail

namespace
{
using omega::app::AudioServiceSnapshot;
using omega::app::BootSequencePhase;
using omega::app::FrontEndMainRow;
using omega::app::FrontEndMode;
using omega::app::FrontEndProfileSlot;
using omega::app::FrontEndState;
using omega::app::GpuHostSnapshot;
using omega::app::OpeningMoviePlayback;
using omega::app::OpeningMoviePlayerError;
using omega::app::OpeningMoviePlayerErrorCode;
using omega::app::OpeningMoviePlayerStatus;
using omega::app::OpeningMoviePlayerUpdate;
using omega::app::OmegaApp;
using omega::app::SdlAudioService;
using omega::app::detail::OmegaAppTestAccess;

constexpr std::uint32_t kGeneratedWidth = 2U;
constexpr std::uint32_t kGeneratedHeight = 2U;
constexpr std::uint64_t kGeneratedLogicalBytes =
    static_cast<std::uint64_t>(kGeneratedWidth) * kGeneratedHeight * 4U;
constexpr std::uint64_t kGeneratedSafetyTicks = 60'000'000U;

int failures = 0;

void Check(const bool condition, const std::string_view context,
    const std::string_view detail)
{
    if (!condition)
    {
        std::cerr << "FAILED [" << context << "]: " << detail << '\n';
        ++failures;
    }
}

[[nodiscard]] bool PushKey(
    const SDL_Scancode scancode, const bool pressed)
{
    SDL_Event event{};
    event.type = pressed ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = scancode;
    event.key.down = pressed;
    return SDL_PushEvent(&event);
}

[[nodiscard]] std::string SyntheticHostilePath()
{
    std::string path{"C:"};
    path.push_back('\\');
    path += "Users";
    path.push_back('\\');
    path += "Private";
    path += "Owner";
    path.push_back('\\');
    path += "SecretVault";
    path.push_back('\\');
    path += "launch.pss";
    return path;
}

enum class GeneratedPlaybackMode
{
    NaturalCompletion,
    ScheduledSkip,
    AdvanceFailure,
};

struct GeneratedPlaybackObservation
{
    std::size_t advance_calls = 0U;
    std::size_t audio_read_calls = 0U;
    std::uint64_t audio_frames_returned = 0U;
    std::size_t destruction_count = 0U;
    bool skip_event_attempted = false;
    bool skip_event_queued = false;
    SdlAudioService* audio_for_fault_injection = nullptr;
    bool queue_rejection_attempted = false;
    bool queue_rejection_accepted = false;
    bool control_fault_attempted = false;
    bool wrong_thread_discard_succeeded = false;
};

struct GeneratedPlaybackConfig
{
    GeneratedPlaybackMode mode = GeneratedPlaybackMode::NaturalCompletion;
    std::size_t complete_after_advance = 2U;
    std::optional<std::size_t> queue_skip_after_advance;
    bool supply_audio = false;
    std::uint32_t presentation_width = kGeneratedWidth;
    std::uint32_t presentation_height = kGeneratedHeight;
    std::uint64_t safety_duration_ticks = kGeneratedSafetyTicks;
    bool over_report_audio_frames = false;
    bool inject_queue_rejection_on_first_advance = false;
    bool inject_control_fault_on_first_advance = false;
};

class GeneratedOpeningMovie final : public OpeningMoviePlayback
{
public:
    GeneratedOpeningMovie(GeneratedPlaybackConfig config,
        std::shared_ptr<GeneratedPlaybackObservation> observation)
        : config_(config), observation_(std::move(observation)),
          hostile_error_message_(SyntheticHostilePath())
    {
        frame_.width = kGeneratedWidth;
        frame_.height = kGeneratedHeight;
        frame_.pixels.resize(static_cast<std::size_t>(kGeneratedLogicalBytes),
            std::byte{0});
        for (std::size_t alpha = 3U; alpha < frame_.pixels.size(); alpha += 4U)
            frame_.pixels[alpha] = std::byte{255};
    }

    ~GeneratedOpeningMovie() noexcept override
    {
        ++observation_->destruction_count;
    }

    [[nodiscard]] std::expected<OpeningMoviePlayerUpdate,
        OpeningMoviePlayerError>
    Advance(const std::chrono::nanoseconds) override
    {
        ++observation_->advance_calls;
        if (config_.inject_queue_rejection_on_first_advance &&
            observation_->advance_calls == 1U &&
            observation_->audio_for_fault_injection != nullptr)
        {
            observation_->queue_rejection_attempted = true;
            observation_->queue_rejection_accepted =
                observation_->audio_for_fault_injection->QueueOpeningMoviePcm16(
                    std::span<const std::int16_t>{});
        }
        if (config_.inject_control_fault_on_first_advance &&
            observation_->advance_calls == 1U &&
            observation_->audio_for_fault_injection != nullptr)
        {
            observation_->control_fault_attempted = true;
            SdlAudioService* const audio =
                observation_->audio_for_fault_injection;
            std::thread wrong_thread([observation = observation_, audio]() {
                observation->wrong_thread_discard_succeeded =
                    audio->DiscardOpeningMovieAudio();
            });
            wrong_thread.join();
        }
        if (config_.mode == GeneratedPlaybackMode::AdvanceFailure)
        {
            return std::unexpected(OpeningMoviePlayerError{
                .code = OpeningMoviePlayerErrorCode::DecoderFailed,
                .message = hostile_error_message_,
            });
        }

        const bool frame_updated = !frame_published_;
        frame_published_ = true;
        if (config_.queue_skip_after_advance &&
            observation_->advance_calls == *config_.queue_skip_after_advance)
        {
            observation_->skip_event_attempted = true;
            observation_->skip_event_queued =
                PushKey(SDL_SCANCODE_RETURN, true);
        }

        const bool completed =
            config_.mode == GeneratedPlaybackMode::NaturalCompletion &&
            observation_->advance_calls >= config_.complete_after_advance;
        return OpeningMoviePlayerUpdate{
            .status = completed ? OpeningMoviePlayerStatus::Completed
                                : OpeningMoviePlayerStatus::Playing,
            .frame_updated = frame_updated,
            .current_frame = &frame_,
        };
    }

    [[nodiscard]] std::expected<std::uint64_t, OpeningMoviePlayerError>
    ReadAudioFrames(
        const std::span<std::int16_t> interleaved_samples) override
    {
        ++observation_->audio_read_calls;
        if (!config_.supply_audio || audio_delivered_)
            return 0U;
        constexpr std::uint64_t kGeneratedAudioFrames = 256U;
        constexpr std::size_t kGeneratedAudioSamples =
            static_cast<std::size_t>(kGeneratedAudioFrames) *
            omega::app::SdlAudioService::kChannelCount;
        if (interleaved_samples.size() %
                static_cast<std::size_t>(omega::app::SdlAudioService::kChannelCount) !=
                0U ||
            interleaved_samples.size() < kGeneratedAudioSamples)
        {
            return std::unexpected(OpeningMoviePlayerError{
                .code = OpeningMoviePlayerErrorCode::AudioDecodeFailed,
                .message = omega::app::OpeningMoviePlayerErrorMessage(
                    OpeningMoviePlayerErrorCode::AudioDecodeFailed),
            });
        }
        if (config_.over_report_audio_frames)
        {
            std::fill(interleaved_samples.begin(), interleaved_samples.end(),
                std::int16_t{1});
            return static_cast<std::uint64_t>(interleaved_samples.size() /
                       static_cast<std::size_t>(
                           omega::app::SdlAudioService::kChannelCount)) +
                1U;
        }

        std::fill_n(interleaved_samples.begin(), kGeneratedAudioSamples,
            std::int16_t{0});
        audio_delivered_ = true;
        observation_->audio_frames_returned += kGeneratedAudioFrames;
        return kGeneratedAudioFrames;
    }

    [[nodiscard]] bool audio_finished() const noexcept override
    {
        return !config_.supply_audio || audio_delivered_;
    }

    [[nodiscard]] std::uint32_t width() const noexcept override
    {
        return config_.presentation_width;
    }

    [[nodiscard]] std::uint32_t height() const noexcept override
    {
        return config_.presentation_height;
    }

    [[nodiscard]] std::uint64_t safety_duration_ticks() const noexcept override
    {
        return config_.safety_duration_ticks;
    }

private:
    GeneratedPlaybackConfig config_;
    std::shared_ptr<GeneratedPlaybackObservation> observation_;
    omega::media::Rgba8VideoFrame frame_;
    std::string hostile_error_message_;
    bool frame_published_ = false;
    bool audio_delivered_ = false;
};

[[nodiscard]] omega::runtime::RuntimeSettings TestSettings()
{
    omega::runtime::RuntimeSettings settings;
    settings.jobs.worker_count = 1U;
    settings.jobs.max_pending_jobs = 8U;
    settings.frame.simulation_step = omega::runtime::kMinimumSimulationStep;
    settings.frame.max_steps_per_frame = 8U;
    settings.frame.max_frame_delta =
        omega::runtime::kMinimumSimulationStep * 8;
    settings.max_input_events_per_frame =
        omega::runtime::InputTracker::kMaxEventsPerFrameLimit;
    return settings;
}

[[nodiscard]] std::expected<OmegaApp, std::string> CreateApp(
    std::unique_ptr<OpeningMoviePlayback> playback,
    std::optional<std::filesystem::path> path = std::nullopt)
{
    auto config = omega::runtime::ParseConfigText("");
    if (!config)
        return std::unexpected("test config: " + config.error());
    return OmegaAppTestAccess::Create(std::move(*config), TestSettings(),
        std::move(playback), std::move(path));
}

[[nodiscard]] bool SameSimulationState(
    const omega::simulation::SimulationState& left,
    const omega::simulation::SimulationState& right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
}

struct AppBaseline
{
    GpuHostSnapshot gpu{};
    AudioServiceSnapshot audio{};
    omega::simulation::SimulationState simulation{};
};

[[nodiscard]] AppBaseline CaptureBaseline(const OmegaApp& app) noexcept
{
    return AppBaseline{
        .gpu = OmegaAppTestAccess::Gpu(app),
        .audio = OmegaAppTestAccess::Audio(app),
        .simulation = OmegaAppTestAccess::Simulation(app),
    };
}

void CheckTransitionCleanup(const OmegaApp& app, const AppBaseline& before,
    const std::uint64_t expected_updates,
    const std::uint64_t expected_rendered_frames,
    const std::uint64_t expected_movie_frames,
    const std::string_view context,
    const std::uint64_t expected_control_failure_delta = 0U)
{
    const GpuHostSnapshot gpu = OmegaAppTestAccess::Gpu(app);
    const AudioServiceSnapshot audio = OmegaAppTestAccess::Audio(app);
    Check(!OmegaAppTestAccess::HasOpeningMoviePlayback(app), context,
        "playback ownership is released");
    Check(!OmegaAppTestAccess::HasOpeningMovieTexture(app), context,
        "movie texture handle is cleared");
    Check(OmegaAppTestAccess::OpeningMovieDrawCommandCount(app) == 0U,
        context, "movie draw list is cleared");
    Check(OmegaAppTestAccess::OpeningMovieAudioClock(app) ==
              omega::app::OpeningMovieAudioClockState{},
        context, "audio presentation clock is reset");
    Check(OmegaAppTestAccess::OpeningMovieAudioScratchIsZero(app), context,
        "decoded PCM scratch is zeroed");
    Check(audio.opening_movie_queued_frames == 0U &&
              !audio.opening_movie_active &&
              !audio.opening_movie_session_running &&
              !audio.opening_movie_device_resumed,
        context, "real SDL movie audio is inactive and empty");
    Check(audio.opening_movie_control_failures ==
              before.audio.opening_movie_control_failures +
                  expected_control_failure_delta,
        context, "audio control-failure accounting matches the script");
    Check(audio.opening_movie_discard_count ==
              before.audio.opening_movie_discard_count + 2U,
        context, "transition and run epilogue each perform containment");
    Check(SameSimulationState(
              OmegaAppTestAccess::Simulation(app), before.simulation),
        context, "modal movie and transition frames do not simulate");
    Check(gpu.textures.resident_slots + 1U ==
              before.gpu.textures.resident_slots &&
              gpu.textures.resident_logical_bytes + kGeneratedLogicalBytes ==
                  before.gpu.textures.resident_logical_bytes,
        context, "the generated movie texture leaves residency");
    Check(gpu.successful_releases == before.gpu.successful_releases + 1U,
        context, "the generated movie texture releases exactly once");
    Check(gpu.successful_updates ==
              before.gpu.successful_updates + expected_updates,
        context, "only scripted frame publications update the movie texture");
    Check(gpu.frame_submissions ==
              before.gpu.frame_submissions + expected_rendered_frames &&
              gpu.blit_submissions ==
                  before.gpu.blit_submissions + expected_rendered_frames,
        context, "every scripted frame submits one nonempty draw list");
    Check(gpu.successful_blit_draws ==
              before.gpu.successful_blit_draws + expected_movie_frames + 3U,
        context, "movie frames precede one three-blit menu transition frame");
}

void CheckAmbiguousSourceRejection()
{
    constexpr std::string_view context = "ambiguous-source";
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    auto playback = std::make_unique<GeneratedOpeningMovie>(
        GeneratedPlaybackConfig{}, observation);
    const SDL_InitFlags sdl_before = SDL_WasInit(0);
    const std::string hostile_path = SyntheticHostilePath();
    auto app = CreateApp(std::move(playback),
        std::filesystem::path{hostile_path});
    Check(!app && app.error() ==
              "opening movie source selection is ambiguous",
        context, "path plus injected playback is rejected categorically");
    Check(!app || app.error().find(hostile_path) == std::string::npos,
        context, "ambiguous-source error never echoes the supplied path");
    Check(observation->destruction_count == 1U, context,
        "rejected injected playback is destroyed exactly once");
    Check(SDL_WasInit(0) == sdl_before, context,
        "ambiguous source selection fails before SDL startup");
}

void CheckInvalidMetadataRejection()
{
    struct InvalidMetadataCase
    {
        std::uint32_t width;
        std::uint32_t height;
        std::uint64_t safety_ticks;
        std::string_view context;
    };
    constexpr std::array cases{
        InvalidMetadataCase{0U, kGeneratedHeight, kGeneratedSafetyTicks,
            "zero-width-metadata"},
        InvalidMetadataCase{omega::media::kMaximumNv12FrameWidth + 1U,
            kGeneratedHeight, kGeneratedSafetyTicks, "oversized-width-metadata"},
        InvalidMetadataCase{kGeneratedWidth, 0U, kGeneratedSafetyTicks,
            "zero-height-metadata"},
        InvalidMetadataCase{kGeneratedWidth,
            omega::media::kMaximumNv12FrameHeight + 1U,
            kGeneratedSafetyTicks, "oversized-height-metadata"},
        InvalidMetadataCase{kGeneratedWidth, kGeneratedHeight, 0U,
            "zero-safety-metadata"},
        InvalidMetadataCase{kGeneratedWidth, kGeneratedHeight,
            omega::app::kOpeningMovieMaximumSafetyTicks + 1U,
            "oversized-safety-metadata"},
    };

    for (const InvalidMetadataCase& invalid : cases)
    {
        auto observation = std::make_shared<GeneratedPlaybackObservation>();
        GeneratedPlaybackConfig config{};
        config.presentation_width = invalid.width;
        config.presentation_height = invalid.height;
        config.safety_duration_ticks = invalid.safety_ticks;
        auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
            config, observation));
        Check(app.has_value(), invalid.context,
            "invalid movie metadata fails open without failing app creation");
        if (!app)
            continue;

        Check(observation->advance_calls == 0U &&
                  observation->audio_read_calls == 0U &&
                  observation->destruction_count == 1U,
            invalid.context,
            "rejected playback is never invoked and is destroyed exactly once");
        Check(!OmegaAppTestAccess::HasOpeningMoviePlayback(*app) &&
                  !OmegaAppTestAccess::HasOpeningMovieTexture(*app) &&
                  OmegaAppTestAccess::OpeningMovieDrawCommandCount(*app) == 0U,
            invalid.context,
            "invalid metadata cannot publish presentation resources");
        Check(OmegaAppTestAccess::BootSequence(*app) ==
                  omega::app::BootSequenceState{} &&
                  OmegaAppTestAccess::FrontEnd(*app) ==
                      omega::app::InitialFrontEndState(),
            invalid.context,
            "invalid metadata preserves immediate main-menu startup");

        std::size_t warning_count = 0U;
        for (const omega::runtime::LogRecord& record :
            OmegaAppTestAccess::Logs(*app))
        {
            if (record.category == "opening_movie" &&
                record.severity == omega::runtime::LogSeverity::Warning &&
                record.message ==
                    "opening movie presentation rejected invalid metadata")
            {
                ++warning_count;
            }
        }
        Check(warning_count == 1U, invalid.context,
            "metadata rejection emits one fixed categorical warning");
    }
}

void CheckNaturalCompletionAndActionableMenu()
{
    constexpr std::string_view context = "natural-completion";
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
        GeneratedPlaybackConfig{
            .mode = GeneratedPlaybackMode::NaturalCompletion,
            .complete_after_advance = 2U,
        },
        observation));
    Check(app.has_value(), context, "generated playback app starts");
    if (!app)
        return;

    Check(OmegaAppTestAccess::HasOpeningMoviePlayback(*app) &&
              OmegaAppTestAccess::HasOpeningMovieTexture(*app) &&
              OmegaAppTestAccess::OpeningMovieDrawCommandCount(*app) == 1U,
        context, "generated playback owns one ready presentation resource");
    const AppBaseline before = CaptureBaseline(*app);
    auto completed = app->Run(2);
    Check(completed && completed->rendered_frames == 2 &&
              completed->input_frames == 2U &&
              completed->planned_simulation_steps == 0U &&
              completed->executed_simulation_steps == 0U &&
              !completed->quit_requested,
        context, "two scripted frames complete without simulation or quit");
    Check(observation->advance_calls == 2U &&
              observation->audio_read_calls == 0U &&
              observation->destruction_count == 1U,
        context, "natural EOS owns the exact fake-player lifetime");
    const auto boot = OmegaAppTestAccess::BootSequence(*app);
    Check(boot.phase == BootSequencePhase::Completed &&
              boot.position_ticks == boot.duration_ticks,
        context, "source EOS completes the modal boot state");
    Check(OmegaAppTestAccess::FrontEnd(*app) ==
              omega::app::InitialFrontEndState(),
        context, "completion enters the initial main menu");
    CheckTransitionCleanup(*app, before, 1U, 2U, 1U, context);

    Check(PushKey(SDL_SCANCODE_DOWN, true), context,
        "a fresh next-action edge enters the SDL queue");
    auto navigated = app->Run(1);
    Check(navigated && navigated->rendered_frames == 1 &&
              navigated->planned_simulation_steps == 0U &&
              navigated->executed_simulation_steps == 0U,
        context, "the first post-movie menu frame remains modal");
    Check(OmegaAppTestAccess::FrontEnd(*app) == FrontEndState{
              .mode = FrontEndMode::Main,
              .selected_main_row = FrontEndMainRow::Profiles,
              .selected_profile_slot = FrontEndProfileSlot::First,
          },
        context, "the next frame accepts menu navigation");
}

void CheckScheduledSkip(const std::size_t advance_count,
    const std::string_view context)
{
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    GeneratedPlaybackConfig config{
        .mode = GeneratedPlaybackMode::ScheduledSkip,
        .complete_after_advance = advance_count + 2U,
        .queue_skip_after_advance = advance_count == 0U
            ? std::nullopt
            : std::optional<std::size_t>{advance_count},
        .supply_audio = advance_count != 0U,
    };
    auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
        config, observation));
    Check(app.has_value(), context, "generated playback app starts");
    if (!app)
        return;

    if (advance_count == 0U)
    {
        Check(PushKey(SDL_SCANCODE_RETURN, true), context,
            "early primary edge enters the SDL queue");
    }
    const AppBaseline before = CaptureBaseline(*app);
    const int frame_limit = static_cast<int>(advance_count + 1U);
    auto skipped = app->Run(frame_limit);
    Check(skipped && skipped->rendered_frames == frame_limit &&
              skipped->input_frames == advance_count + 1U &&
              skipped->planned_simulation_steps == 0U &&
              skipped->executed_simulation_steps == 0U &&
              !skipped->quit_requested,
        context, "script reaches its skip frame without simulation or quit");
    Check(observation->advance_calls == advance_count &&
              observation->destruction_count == 1U,
        context, "primary skip bypasses the player on the transition frame");
    if (advance_count == 0U)
    {
        Check(!observation->skip_event_attempted &&
                  observation->audio_read_calls == 0U,
            context, "early skip occurs before video or PCM publication");
    }
    else
    {
        Check(observation->skip_event_attempted &&
                  observation->skip_event_queued,
            context, "the selected playback position schedules one primary edge");
        Check(observation->audio_read_calls != 0U &&
                  observation->audio_frames_returned != 0U,
            context, "mid/late playback supplies generated PCM before skip");
        Check(OmegaAppTestAccess::Audio(*app)
                      .opening_movie_session_generation ==
                  before.audio.opening_movie_session_generation + 1U,
            context, "generated PCM starts exactly one real SDL audio session");
    }
    Check(OmegaAppTestAccess::BootSequence(*app).phase ==
              BootSequencePhase::Skipped,
        context, "primary input records a skipped boot sequence");
    Check(OmegaAppTestAccess::FrontEnd(*app) ==
              omega::app::InitialFrontEndState(),
        context, "the skip primary edge is not forwarded into the menu");
    CheckTransitionCleanup(*app, before, advance_count == 0U ? 0U : 1U,
        advance_count + 1U, advance_count, context);
}

void CheckOverReportedPcmRejection()
{
    constexpr std::string_view context = "over-reported-pcm";
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
        GeneratedPlaybackConfig{
            .supply_audio = true,
            .over_report_audio_frames = true,
        },
        observation));
    Check(app.has_value(), context, "generated playback app starts");
    if (!app)
        return;

    const AppBaseline before = CaptureBaseline(*app);
    auto rejected = app->Run(1);
    Check(rejected && rejected->rendered_frames == 1 &&
              rejected->planned_simulation_steps == 0U &&
              rejected->executed_simulation_steps == 0U,
        context, "over-reported PCM fails open without simulation");
    Check(observation->advance_calls == 1U &&
              observation->audio_read_calls == 1U &&
              observation->audio_frames_returned == 0U &&
              observation->destruction_count == 1U,
        context,
        "one oversized refill result is rejected before audio acceptance");
    Check(OmegaAppTestAccess::BootSequence(*app).phase ==
              BootSequencePhase::Failed &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState(),
        context, "oversized refill fails open to the main menu");
    Check(OmegaAppTestAccess::Audio(*app).opening_movie_session_generation ==
              before.audio.opening_movie_session_generation,
        context, "oversized refill never starts an SDL audio session");
    CheckTransitionCleanup(*app, before, 1U, 1U, 0U, context);

    std::size_t warning_count = 0U;
    for (const omega::runtime::LogRecord& record : OmegaAppTestAccess::Logs(*app))
    {
        if (record.category == "opening_movie" &&
            record.severity == omega::runtime::LogSeverity::Warning &&
            record.message ==
                "opening movie PCM decode exceeded the requested refill")
        {
            ++warning_count;
        }
    }
    Check(warning_count == 1U, context,
        "oversized refill emits one fixed categorical warning");
}

void CheckHostileRuntimeErrorRedaction()
{
    constexpr std::string_view context = "runtime-error-redaction";
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
        GeneratedPlaybackConfig{
            .mode = GeneratedPlaybackMode::AdvanceFailure,
        },
        observation));
    Check(app.has_value(), context, "generated failing playback app starts");
    if (!app)
        return;

    const AppBaseline before = CaptureBaseline(*app);
    auto failed_open = app->Run(1);
    Check(failed_open && failed_open->rendered_frames == 1 &&
              failed_open->planned_simulation_steps == 0U &&
              failed_open->executed_simulation_steps == 0U,
        context, "player failure enters the menu without an operational failure");
    Check(observation->advance_calls == 1U &&
              observation->destruction_count == 1U,
        context, "failed playback is called and destroyed exactly once");
    Check(OmegaAppTestAccess::BootSequence(*app).phase ==
              BootSequencePhase::Failed &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState(),
        context, "categorical player failure fails open to the main menu");
    CheckTransitionCleanup(*app, before, 0U, 1U, 0U, context);

    std::size_t canonical_warning_count = 0U;
    bool leaked_hostile_detail = false;
    const std::string hostile_path = SyntheticHostilePath();
    const std::string_view canonical = omega::app::OpeningMoviePlayerErrorMessage(
        OpeningMoviePlayerErrorCode::DecoderFailed);
    for (const omega::runtime::LogRecord& record : OmegaAppTestAccess::Logs(*app))
    {
        if (record.category != "opening_movie")
            continue;
        if (record.severity == omega::runtime::LogSeverity::Warning &&
            record.message == canonical)
        {
            ++canonical_warning_count;
        }
        leaked_hostile_detail = leaked_hostile_detail ||
            record.message.find(hostile_path) != std::string::npos ||
            record.message.find("PrivateOwner") != std::string::npos ||
            record.message.find("SecretVault") != std::string::npos ||
            record.message.find("launch.pss") != std::string::npos ||
            record.message.find('/') != std::string::npos ||
            record.message.find('\\') != std::string::npos;
    }
    Check(canonical_warning_count == 1U, context,
        "the app logs one fixed error selected only by code");
    Check(!leaked_hostile_detail, context,
        "the playback-supplied path-bearing message never escapes");
}

void CheckLateAudioFaultFailsOpenToActionableMenu()
{
    constexpr std::string_view context = "late-audio-fault";
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
        GeneratedPlaybackConfig{
            .mode = GeneratedPlaybackMode::ScheduledSkip,
            .inject_queue_rejection_on_first_advance = true,
        },
        observation));
    Check(app.has_value(), context, "generated playback app starts");
    if (!app)
        return;

    observation->audio_for_fault_injection =
        OmegaAppTestAccess::MutableAudio(*app);
    const AppBaseline before = CaptureBaseline(*app);
    auto failed_open = app->Run(2);
    Check(failed_open && failed_open->rendered_frames == 2 &&
              failed_open->planned_simulation_steps == 0U &&
              failed_open->executed_simulation_steps == 0U,
        context,
        "a fault raised after the pre-render sample keeps the run alive");
    Check(observation->queue_rejection_attempted &&
              !observation->queue_rejection_accepted &&
              OmegaAppTestAccess::Audio(*app).opening_movie_queue_rejections ==
                  before.audio.opening_movie_queue_rejections + 1U,
        context, "one real audio-service rejection drives the late-fault path");
    Check(observation->advance_calls == 1U &&
              observation->destruction_count == 1U,
        context, "the fault releases playback after its first frame");
    Check(OmegaAppTestAccess::BootSequence(*app).phase ==
              BootSequencePhase::Failed &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState(),
        context, "the late fault enters the initial main menu");
    CheckTransitionCleanup(*app, before, 1U, 2U, 1U, context);

    std::size_t warning_count = 0U;
    for (const omega::runtime::LogRecord& record :
        OmegaAppTestAccess::Logs(*app))
    {
        if (record.category == "opening_movie" &&
            record.severity == omega::runtime::LogSeverity::Warning &&
            record.message ==
                omega::app::OpeningMovieAudioFaultMessage(
                    omega::app::OpeningMovieAudioFault::QueueRejection))
        {
            ++warning_count;
        }
    }
    Check(warning_count == 1U, context,
        "the late fault emits one categorical path-free warning");

    Check(PushKey(SDL_SCANCODE_DOWN, true), context,
        "a post-fault menu edge enters the SDL queue");
    auto navigated = app->Run(1);
    Check(navigated && OmegaAppTestAccess::FrontEnd(*app) == FrontEndState{
              .mode = FrontEndMode::Main,
              .selected_main_row = FrontEndMainRow::Profiles,
              .selected_profile_slot = FrontEndProfileSlot::First,
          },
        context, "the next frame accepts main-menu navigation");
}

void CheckLateControlFaultRebaselinesForMenu()
{
    constexpr std::string_view context = "late-control-fault";
    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    auto app = CreateApp(std::make_unique<GeneratedOpeningMovie>(
        GeneratedPlaybackConfig{
            .mode = GeneratedPlaybackMode::ScheduledSkip,
            .inject_control_fault_on_first_advance = true,
        },
        observation));
    Check(app.has_value(), context, "generated playback app starts");
    if (!app)
        return;

    observation->audio_for_fault_injection =
        OmegaAppTestAccess::MutableAudio(*app);
    const AppBaseline before = CaptureBaseline(*app);
    auto failed_open = app->Run(2);
    Check(failed_open && failed_open->rendered_frames == 2 &&
              failed_open->planned_simulation_steps == 0U &&
              failed_open->executed_simulation_steps == 0U,
        context,
        "a late control fault is handled once and the next menu frame succeeds");
    Check(observation->control_fault_attempted &&
              !observation->wrong_thread_discard_succeeded &&
              OmegaAppTestAccess::Audio(*app).opening_movie_control_failures ==
                  before.audio.opening_movie_control_failures + 1U,
        context, "one real wrong-thread discard raises the control counter");
    Check(observation->advance_calls == 1U &&
              observation->destruction_count == 1U,
        context, "the control fault releases playback after its first frame");
    Check(OmegaAppTestAccess::BootSequence(*app).phase ==
              BootSequencePhase::Failed &&
              OmegaAppTestAccess::FrontEnd(*app) ==
                  omega::app::InitialFrontEndState(),
        context, "the control fault enters the initial main menu");
    CheckTransitionCleanup(*app, before, 1U, 2U, 1U, context, 1U);
}
} // namespace

int main()
{
    CheckAmbiguousSourceRejection();
    CheckInvalidMetadataRejection();
    CheckNaturalCompletionAndActionableMenu();
    CheckScheduledSkip(0U, "early-skip");
    CheckScheduledSkip(2U, "mid-skip");
    CheckScheduledSkip(5U, "late-skip");
    CheckOverReportedPcmRejection();
    CheckHostileRuntimeErrorRedaction();
    CheckLateAudioFaultFailsOpenToActionableMenu();
    CheckLateControlFaultRebaselinesForMenu();

    if (failures == 0)
        std::cout << "omega_app_opening_movie_smoke: all checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
