#include "opening_movie_player.h"
#include "opening_movie_safety.h"
#include "omega_app.h"

#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/frame_scheduler.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/runtime_settings.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
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
#include <system_error>
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
        std::optional<std::filesystem::path> opening_movie_path = std::nullopt,
        std::unique_ptr<NativePersistence> native_persistence = nullptr)
    {
        return OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
            std::move(config), settings, runtime::ContentStartupState{},
            std::move(native_persistence), false, {}, std::move(opening_movie_path),
            std::nullopt, std::move(opening_movie_playback));
    }

    [[nodiscard]] static std::expected<OmegaApp, std::string> CreateFromSource(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        asset::OpeningMovieSource opening_movie_source,
        std::unique_ptr<NativePersistence> native_persistence)
    {
        return OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
            std::move(config), settings, runtime::ContentStartupState{},
            std::move(native_persistence), false, {}, std::nullopt,
            std::optional<asset::OpeningMovieSource>{std::move(opening_movie_source)},
            nullptr);
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

    [[nodiscard]] static FrontEndStartupModel FrontEndModel(
        const OmegaApp& app) noexcept
    {
        return app.front_end_startup_model_;
    }

    [[nodiscard]] static FrontEndCharacterStartupModel FrontEndCharacterModel(
        const OmegaApp& app) noexcept
    {
        return app.front_end_character_startup_model_;
    }

    [[nodiscard]] static bool CanCreateFirstProfile(
        const OmegaApp& app) noexcept
    {
        return app.can_create_first_profile_;
    }

    [[nodiscard]] static bool CanCreateFirstCharacter(
        const OmegaApp& app) noexcept
    {
        return app.can_create_first_character_;
    }

    [[nodiscard]] static std::optional<profiles::ProfileId> ActiveProfile(
        const OmegaApp& app) noexcept
    {
        return app.active_profile_id_;
    }

    [[nodiscard]] static std::optional<profiles::CharacterId> ActiveCharacter(
        const OmegaApp& app) noexcept
    {
        return app.active_character_id_;
    }

    [[nodiscard]] static std::optional<profiles::ProfileId>
    PersistedConfirmedProfile(const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->persisted_confirmed_profile_id();
    }

    [[nodiscard]] static std::optional<profiles::CharacterId>
    PersistedConfirmedCharacter(const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->persisted_confirmed_character_id();
    }

    [[nodiscard]] static std::optional<std::uint64_t> PersistenceGeneration(
        const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->database().generation();
    }

    [[nodiscard]] static std::optional<std::size_t> PersistenceRecordCount(
        const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->database().record_count();
    }

    [[nodiscard]] static std::optional<std::size_t>
    PersistenceLogicalValueBytes(const OmegaApp& app) noexcept
    {
        if (!app.native_persistence_)
            return std::nullopt;
        return app.native_persistence_->database().logical_value_bytes();
    }

    [[nodiscard]] static bool ArmFirstProfileTimestamp(
        OmegaApp& app, const std::uint64_t timestamp) noexcept
    {
        if (timestamp > profiles::kProfileTimestampMaxUnixMilliseconds ||
            app.first_profile_timestamp_override_for_testing_)
        {
            return false;
        }
        app.first_profile_timestamp_override_for_testing_ = timestamp;
        return true;
    }

    [[nodiscard]] static bool ArmNextRunElapsed(
        OmegaApp& app, const std::chrono::nanoseconds elapsed) noexcept
    {
        if (elapsed < std::chrono::nanoseconds::zero() ||
            app.next_run_elapsed_override_for_testing_)
        {
            return false;
        }
        app.next_run_elapsed_override_for_testing_ = elapsed;
        return true;
    }

    [[nodiscard]] static std::optional<std::size_t> ProfileCatalogCount(
        OmegaApp& app)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto listed = app.native_persistence_->profiles().List();
        if (!listed)
            return std::nullopt;
        return listed->size();
    }

    [[nodiscard]] static std::optional<profiles::ProfileSummary> ReadProfile(
        OmegaApp& app, const profiles::ProfileId id)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto read = app.native_persistence_->profiles().Read(id);
        if (!read || !*read)
            return std::nullopt;
        return std::move(**read);
    }

    [[nodiscard]] static std::optional<std::size_t> CharacterCatalogCount(
        OmegaApp& app, const profiles::ProfileId profile_id)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto listed = app.native_persistence_->characters().List(profile_id);
        if (!listed)
            return std::nullopt;
        return listed->size();
    }

    [[nodiscard]] static std::optional<profiles::CharacterSummary>
    ReadCharacter(OmegaApp& app, const profiles::ProfileId profile_id,
        const profiles::CharacterId character_id)
    {
        if (!app.native_persistence_)
            return std::nullopt;
        auto read = app.native_persistence_->characters().Read(
            profile_id, character_id);
        if (!read || !*read)
            return std::nullopt;
        return std::move(**read);
    }
};
} // namespace omega::app::detail

namespace
{
using omega::app::AudioServiceSnapshot;
using omega::app::BootSequencePhase;
using omega::app::FrontEndMainRow;
using omega::app::FrontEndMode;
using omega::app::FrontEndCharacterSlot;
using omega::app::FrontEndProfileSlot;
using omega::app::FrontEndState;
using omega::app::GpuHostSnapshot;
using omega::app::NativePersistence;
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
constexpr std::uint64_t kEmptyProfilePresentationLogicalBytes = 233'476U;
constexpr std::uint64_t kEmptyProfileMoviePresentationLogicalBytes =
    kEmptyProfilePresentationLogicalBytes + kGeneratedLogicalBytes;
constexpr std::uint64_t kCharacterCardLogicalBytes = 128ULL * 72ULL * 4ULL;
constexpr FrontEndState kProfilesFirst{
    .mode = FrontEndMode::Profiles,
    .selected_main_row = FrontEndMainRow::Profiles,
    .selected_profile_slot = FrontEndProfileSlot::First,
};
constexpr FrontEndState kReturnedProfilesRow{
    .mode = FrontEndMode::Main,
    .selected_main_row = FrontEndMainRow::Profiles,
    .selected_profile_slot = FrontEndProfileSlot::First,
};
constexpr FrontEndState kMainStart{
    .mode = FrontEndMode::Main,
    .selected_main_row = FrontEndMainRow::StartDiagnostic,
    .selected_profile_slot = FrontEndProfileSlot::First,
};
constexpr FrontEndState kCharactersFirst{
    .mode = FrontEndMode::Characters,
    .selected_main_row = FrontEndMainRow::Profiles,
    .selected_profile_slot = FrontEndProfileSlot::First,
    .selected_character_slot = FrontEndCharacterSlot::First,
};
constexpr FrontEndState kBriefingRoom{
    .mode = FrontEndMode::BriefingRoom,
    .selected_main_row = FrontEndMainRow::StartDiagnostic,
    .selected_profile_slot = FrontEndProfileSlot::First,
    .selected_character_slot = FrontEndCharacterSlot::First,
};
constexpr FrontEndState kDiagnosticPlay{
    .mode = FrontEndMode::DiagnosticPlay,
    .selected_main_row = FrontEndMainRow::StartDiagnostic,
    .selected_profile_slot = FrontEndProfileSlot::First,
    .selected_character_slot = FrontEndCharacterSlot::First,
};

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

class TempDirectory final
{
public:
    explicit TempDirectory(const std::string_view label)
    {
        static std::atomic<std::uint64_t> next{0U};
        std::error_code error;
        const std::filesystem::path temporary =
            std::filesystem::temp_directory_path(error);
        if (!error)
        {
            const std::uint64_t process_id = static_cast<std::uint64_t>(
#if defined(_WIN32)
                _getpid()
#else
                getpid()
#endif
            );
            for (std::size_t attempt = 0U; attempt < 32U; ++attempt)
            {
                const auto tick =
                    std::chrono::steady_clock::now().time_since_epoch().count();
                const std::filesystem::path candidate = temporary /
                    ("openomega-opening-movie-smoke-" + std::string(label) + "-" +
                        std::to_string(process_id) + "-" +
                        std::to_string(tick) + "-" +
                        std::to_string(next.fetch_add(1U)));
                error.clear();
                if (std::filesystem::create_directory(candidate, error))
                {
                    root_ = candidate;
                    break;
                }
                if (error)
                    break;
            }
        }
        Check(!error && !root_.empty(), label,
            "the generated native-persistence directory is created");
    }

    ~TempDirectory()
    {
        if (root_.empty())
            return;
        std::error_code error;
        std::filesystem::remove_all(root_, error);
        Check(!error, "temporary-persistence-cleanup",
            "the generated native-persistence directory is removed");
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return root_;
    }

private:
    std::filesystem::path root_;
};

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

[[nodiscard]] std::expected<OmegaApp, std::string> CreatePersistentApp(
    std::unique_ptr<OpeningMoviePlayback> playback,
    NativePersistence persistence)
{
    auto config = omega::runtime::ParseConfigText("");
    if (!config)
        return std::unexpected("test config: " + config.error());
    return OmegaAppTestAccess::Create(std::move(*config), TestSettings(),
        std::move(playback), std::nullopt,
        std::make_unique<NativePersistence>(std::move(persistence)));
}

[[nodiscard]] std::expected<OmegaApp, std::string> CreatePersistentAppFromSource(
    omega::asset::OpeningMovieSource source, NativePersistence persistence)
{
    auto config = omega::runtime::ParseConfigText("");
    if (!config)
        return std::unexpected("test config: " + config.error());
    return OmegaAppTestAccess::CreateFromSource(std::move(*config), TestSettings(),
        std::move(source),
        std::make_unique<NativePersistence>(std::move(persistence)));
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
    const std::uint64_t expected_control_failure_delta = 0U,
    const std::uint64_t expected_front_end_draws = 3U)
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
              before.gpu.successful_blit_draws + expected_movie_frames +
                  expected_front_end_draws,
        context,
        "movie frames precede the exact project front-end transition draw count");
}

[[nodiscard]] bool SameGpuResourceState(
    const GpuHostSnapshot& left, const GpuHostSnapshot& right) noexcept
{
    return left.textures == right.textures &&
           left.successful_uploads == right.successful_uploads &&
           left.successful_upload_logical_bytes ==
               right.successful_upload_logical_bytes &&
           left.successful_updates == right.successful_updates &&
           left.successful_update_logical_bytes ==
               right.successful_update_logical_bytes &&
           left.successful_releases == right.successful_releases;
}

void CheckPersistentMovieStartup(OmegaApp& app, const std::string_view context)
{
    const GpuHostSnapshot gpu = OmegaAppTestAccess::Gpu(app);
    Check(OmegaAppTestAccess::FrontEnd(app) == kProfilesFirst &&
              OmegaAppTestAccess::FrontEndModel(app) ==
                  omega::app::FrontEndStartupModel{} &&
              OmegaAppTestAccess::CanCreateFirstProfile(app) &&
              !OmegaAppTestAccess::ActiveProfile(app) &&
              OmegaAppTestAccess::ProfileCatalogCount(app) ==
                  std::optional<std::size_t>{0U},
        context,
        "empty native persistence derives Profiles without creating or selecting a profile");
    Check(gpu.successful_uploads == 9U &&
              gpu.successful_upload_logical_bytes ==
                  kEmptyProfileMoviePresentationLogicalBytes &&
              gpu.successful_releases == 0U &&
              gpu.textures.resident_slots == 9U &&
              gpu.textures.reserved_slots == 0U &&
              gpu.textures.resident_logical_bytes ==
                  kEmptyProfileMoviePresentationLogicalBytes,
        context,
        "the generated 2x2 movie coexists with the exact nine-slot empty-profile presentation");
}

[[nodiscard]] bool RunOneModalFrameWithExactDraws(OmegaApp& app,
    const std::uint64_t expected_draws, const std::string_view context)
{
    const GpuHostSnapshot before_gpu = OmegaAppTestAccess::Gpu(app);
    const auto before_simulation = OmegaAppTestAccess::Simulation(app);
    auto run = app.Run(1);
    const GpuHostSnapshot after_gpu = OmegaAppTestAccess::Gpu(app);
    const bool ran = run && run->rendered_frames == 1 &&
        run->input_frames == 1U && run->planned_simulation_steps == 0U &&
        run->executed_simulation_steps == 0U && !run->quit_requested;
    Check(ran, context,
        "the scripted project front-end frame renders once without simulation or quit");
    Check(SameSimulationState(
              before_simulation, OmegaAppTestAccess::Simulation(app)),
        context, "the scripted project front-end frame remains modal");
    Check(SameGpuResourceState(before_gpu, after_gpu), context,
        "the scripted project front-end command performs no GPU resource mutation");
    Check(after_gpu.frame_submissions == before_gpu.frame_submissions + 1U &&
              after_gpu.blit_submissions == before_gpu.blit_submissions + 1U &&
              after_gpu.successful_blit_draws ==
                  before_gpu.successful_blit_draws + expected_draws,
        context, "the scripted project front-end frame has the exact draw count");
    return ran;
}

[[nodiscard]] bool RunOneCharacterMenuFrameWithTextureDelta(OmegaApp& app,
    const std::uint64_t expected_uploads,
    const std::uint64_t expected_releases,
    const std::uint64_t expected_draws, const std::string_view context)
{
    const GpuHostSnapshot before_gpu = OmegaAppTestAccess::Gpu(app);
    const auto before_simulation = OmegaAppTestAccess::Simulation(app);
    auto run = app.Run(1);
    const GpuHostSnapshot after_gpu = OmegaAppTestAccess::Gpu(app);
    const bool ran = run && run->rendered_frames == 1 &&
        run->input_frames == 1U && run->planned_simulation_steps == 0U &&
        run->executed_simulation_steps == 0U && !run->quit_requested;
    Check(ran, context,
        "the scripted character-menu frame renders once without simulation or quit");
    Check(SameSimulationState(
              before_simulation, OmegaAppTestAccess::Simulation(app)),
        context, "the scripted character-menu frame remains modal");
    Check(after_gpu.successful_uploads ==
              before_gpu.successful_uploads + expected_uploads &&
              after_gpu.successful_upload_logical_bytes ==
                  before_gpu.successful_upload_logical_bytes +
                      expected_uploads * kCharacterCardLogicalBytes &&
              after_gpu.successful_updates == before_gpu.successful_updates &&
              after_gpu.successful_update_logical_bytes ==
                  before_gpu.successful_update_logical_bytes &&
              after_gpu.successful_releases ==
                  before_gpu.successful_releases + expected_releases,
        context,
        "the scripted character-menu frame has the exact upload and release delta");
    Check(after_gpu.textures.slot_capacity ==
              before_gpu.textures.slot_capacity &&
              after_gpu.textures.reserved_slots ==
                  before_gpu.textures.reserved_slots &&
              after_gpu.textures.retired_slots ==
                  before_gpu.textures.retired_slots &&
              after_gpu.textures.reserved_logical_bytes ==
                  before_gpu.textures.reserved_logical_bytes &&
              after_gpu.textures.resident_slots + expected_releases ==
                  before_gpu.textures.resident_slots + expected_uploads &&
              after_gpu.textures.resident_logical_bytes +
                      expected_releases * kCharacterCardLogicalBytes ==
                  before_gpu.textures.resident_logical_bytes +
                      expected_uploads * kCharacterCardLogicalBytes,
        context,
        "the scripted character-menu frame has the exact texture residency delta");
    Check(after_gpu.frame_submissions == before_gpu.frame_submissions + 1U &&
              after_gpu.blit_submissions == before_gpu.blit_submissions + 1U &&
              after_gpu.successful_blit_draws ==
                  before_gpu.successful_blit_draws + expected_draws &&
              after_gpu.clear_submissions == before_gpu.clear_submissions &&
              after_gpu.unavailable_swapchain_submissions ==
                  before_gpu.unavailable_swapchain_submissions &&
              after_gpu.rejected_nondefault_texture_handles ==
                  before_gpu.rejected_nondefault_texture_handles,
        context,
        "the scripted character-menu frame has the exact submission and draw count");
    return ran;
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
    const std::string_view context,
    const SDL_Scancode early_skip_scancode = SDL_SCANCODE_RETURN,
    const bool verify_held_skip_alias = false)
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
        Check(PushKey(early_skip_scancode, true), context,
            "early skip edge enters the SDL queue");
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
    if (verify_held_skip_alias)
    {
        const bool held_alias_frame =
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(held_alias_frame &&
                  OmegaAppTestAccess::FrontEnd(*app) ==
                      omega::app::InitialFrontEndState(),
            context,
            "the held fire alias cannot repeat into the first subsequent menu input frame");
        const bool released_alias_frame =
            PushKey(early_skip_scancode, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(released_alias_frame &&
                  OmegaAppTestAccess::FrontEnd(*app) ==
                      omega::app::InitialFrontEndState(),
            context,
            "the fire-alias release is consumed without selecting a menu row");
    }
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

enum class PersistentMovieEntry
{
    NaturalCompletion,
    PrimarySkip,
};

void CheckPersistenceBackedMovieProfileFlow(
    const PersistentMovieEntry entry, const std::string_view context,
    const std::uint64_t creation_timestamp)
{
    TempDirectory directory(context);
    if (directory.path().empty())
        return;

    const auto first_profile_id = omega::profiles::ProfileId::Parse(
        "00000000000000000000000000000001");
    const auto first_character_id = omega::profiles::CharacterId::Parse(
        "00000000000000000000000000000001");
    auto persistence = NativePersistence::Bootstrap(directory.path());
    Check(first_profile_id && first_character_id && persistence &&
              persistence->startup_profiles().empty(),
        context,
        "the fixed profile and character IDs parse and a fresh generated native-persistence database starts exactly empty");
    if (!first_profile_id || !first_character_id || !persistence ||
        !persistence->startup_profiles().empty())
    {
        return;
    }

    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    GeneratedPlaybackConfig playback_config{
        .mode = entry == PersistentMovieEntry::NaturalCompletion
            ? GeneratedPlaybackMode::NaturalCompletion
            : GeneratedPlaybackMode::ScheduledSkip,
        .complete_after_advance = 2U,
    };
    {
        auto app = CreatePersistentApp(
            std::make_unique<GeneratedOpeningMovie>(
                playback_config, observation),
            std::move(*persistence));
        Check(app.has_value(), context,
            "the generated movie starts with empty native persistence");
        if (!app)
            return;

        CheckPersistentMovieStartup(*app, context);
        const AppBaseline before_transition = CaptureBaseline(*app);
        if (entry == PersistentMovieEntry::NaturalCompletion)
        {
            auto completed = app->Run(2);
            Check(completed && completed->rendered_frames == 2 &&
                      completed->input_frames == 2U &&
                      completed->planned_simulation_steps == 0U &&
                      completed->executed_simulation_steps == 0U &&
                      !completed->quit_requested,
                context,
                "natural EOS renders one movie frame and one Profiles transition frame");
            Check(observation->advance_calls == 2U &&
                      observation->audio_read_calls == 0U &&
                      observation->destruction_count == 1U &&
                      OmegaAppTestAccess::BootSequence(*app).phase ==
                          BootSequencePhase::Completed,
                context,
                "natural EOS owns the exact generated playback lifetime");
            CheckTransitionCleanup(*app, before_transition, 1U, 2U, 1U,
                context, 0U, 2U);
        }
        else
        {
            Check(PushKey(SDL_SCANCODE_F1, true), context,
                "the movie-skip Primary edge enters the SDL queue");
            auto skipped = app->Run(1);
            Check(skipped && skipped->rendered_frames == 1 &&
                      skipped->input_frames == 1U &&
                      skipped->planned_simulation_steps == 0U &&
                      skipped->executed_simulation_steps == 0U &&
                      !skipped->quit_requested,
                context,
                "the Primary skip renders one Profiles transition frame without simulation");
            Check(observation->advance_calls == 0U &&
                      observation->audio_read_calls == 0U &&
                      observation->destruction_count == 1U &&
                      OmegaAppTestAccess::BootSequence(*app).phase ==
                          BootSequencePhase::Skipped,
                context,
                "the early Primary edge skips before movie publication");
            CheckTransitionCleanup(*app, before_transition, 0U, 1U, 0U,
                context, 0U, 2U);
        }

        const GpuHostSnapshot after_transition = OmegaAppTestAccess::Gpu(*app);
        Check(OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst &&
                  OmegaAppTestAccess::CanCreateFirstProfile(*app) &&
                  !OmegaAppTestAccess::ActiveProfile(*app) &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{0U},
            context,
            "movie completion enters Profiles without an implicit create or selection");
        Check(after_transition.successful_uploads == 9U &&
                  after_transition.successful_upload_logical_bytes ==
                      kEmptyProfileMoviePresentationLogicalBytes &&
                  after_transition.successful_releases == 1U &&
                  after_transition.textures.resident_slots == 8U &&
                  after_transition.textures.reserved_slots == 0U &&
                  after_transition.textures.resident_logical_bytes ==
                      kEmptyProfilePresentationLogicalBytes,
            context,
            "front-end entry releases exactly the 16-byte movie texture from the nine-slot coexistence budget");

        if (entry == PersistentMovieEntry::PrimarySkip)
        {
            const bool released_skip = PushKey(SDL_SCANCODE_F1, false) &&
                RunOneModalFrameWithExactDraws(*app, 2U, context);
            Check(released_skip &&
                      OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst &&
                      OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                          std::optional<std::size_t>{0U} &&
                      !OmegaAppTestAccess::ActiveProfile(*app),
                context,
                "the swallowed skip edge must release before a distinct create press");
        }

        const bool timestamp_armed =
            OmegaAppTestAccess::ArmFirstProfileTimestamp(
                *app, creation_timestamp);
        const bool create_pressed = PushKey(SDL_SCANCODE_F1, true);
        const bool create_frame =
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        const auto created_profile =
            OmegaAppTestAccess::ReadProfile(*app, *first_profile_id);
        const auto created_model = OmegaAppTestAccess::FrontEndModel(*app);
        Check(timestamp_armed && create_pressed && create_frame &&
                  OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst &&
                  !OmegaAppTestAccess::CanCreateFirstProfile(*app) &&
                  !OmegaAppTestAccess::ActiveProfile(*app) &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{1U} &&
                  created_model.total_profiles == 1U &&
                  created_model.visible_profiles == 1U &&
                  created_model.profiles[0].id == *first_profile_id &&
                  created_profile && created_profile->id == *first_profile_id &&
                  created_profile->metadata.display_name ==
                      omega::app::kFrontEndFirstProfileDisplayName &&
                  created_profile->metadata.created_unix_milliseconds ==
                      creation_timestamp &&
                  created_profile->metadata.modified_unix_milliseconds ==
                      creation_timestamp &&
                  created_profile->metadata_revision == 1U &&
                  !OmegaAppTestAccess::PersistedConfirmedProfile(*app) &&
                  OmegaAppTestAccess::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{1U} &&
                  OmegaAppTestAccess::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{1U} &&
                  OmegaAppTestAccess::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{41U},
            context,
            "only the explicit post-movie create edge durably publishes the 41-byte fixed-ID PROFILE 1 without activation or a durable confirmation");

        const bool create_released = PushKey(SDL_SCANCODE_F1, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(create_released &&
                  OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst &&
                  !OmegaAppTestAccess::ActiveProfile(*app) &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{1U},
            context,
            "the explicit create release is inert and leaves PROFILE 1 unselected");

        // The created profile is still unconfirmed, so the post-movie front end
        // must leave diagnostic entry inert. Explicit navigation then reaches
        // the surface that can satisfy the gate. RunOneModalFrameWithExactDraws
        // proves each of these frames simulates nothing and allocates no GPU
        // resource.
        const bool unconfirmed_cancelled = PushKey(SDL_SCANCODE_BACKSPACE, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context) &&
            PushKey(SDL_SCANCODE_BACKSPACE, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(unconfirmed_cancelled &&
                  OmegaAppTestAccess::FrontEnd(*app) == kReturnedProfilesRow &&
                  !OmegaAppTestAccess::ActiveProfile(*app),
            context,
            "Cancel leaves the unconfirmed post-movie Profiles surface for its Main row");

        const bool unconfirmed_navigated = PushKey(SDL_SCANCODE_UP, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context) &&
            PushKey(SDL_SCANCODE_UP, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(unconfirmed_navigated &&
                  OmegaAppTestAccess::FrontEnd(*app) == kMainStart &&
                  !OmegaAppTestAccess::ActiveProfile(*app),
            context,
            "the unconfirmed post-movie front end selects its START DIAGNOSTIC row");

        const bool unconfirmed_start_inert = PushKey(SDL_SCANCODE_F1, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(unconfirmed_start_inert &&
                  OmegaAppTestAccess::FrontEnd(*app) == kMainStart &&
                  !OmegaAppTestAccess::ActiveProfile(*app) &&
                  !OmegaAppTestAccess::PersistedConfirmedProfile(*app) &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{1U},
            context,
            "an unconfirmed Start Diagnostic edge is inert without simulation, activation, durable confirmation, or catalog mutation");

        const bool inert_start_released = PushKey(SDL_SCANCODE_F1, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(inert_start_released &&
                  OmegaAppTestAccess::FrontEnd(*app) == kMainStart,
            context,
            "the inert post-movie entry edge releases before explicit profile navigation");

        const bool profiles_navigated = PushKey(SDL_SCANCODE_DOWN, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context) &&
            PushKey(SDL_SCANCODE_DOWN, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(profiles_navigated &&
                  OmegaAppTestAccess::FrontEnd(*app) == kReturnedProfilesRow,
            context,
            "explicit post-movie navigation selects the Profiles main row");

        const bool profiles_opened = PushKey(SDL_SCANCODE_F1, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context) &&
            PushKey(SDL_SCANCODE_F1, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(profiles_opened &&
                  OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst,
            context,
            "Primary explicitly opens the unmarked post-movie Profiles surface before confirmation");

        const bool select_pressed = PushKey(SDL_SCANCODE_F1, true) &&
            RunOneCharacterMenuFrameWithTextureDelta(
                *app, 2U, 0U, 2U, context);
        const auto empty_character_model =
            OmegaAppTestAccess::FrontEndCharacterModel(*app);
        Check(select_pressed &&
                  OmegaAppTestAccess::FrontEnd(*app) == kCharactersFirst &&
                  OmegaAppTestAccess::ActiveProfile(*app) == *first_profile_id &&
                  OmegaAppTestAccess::PersistedConfirmedProfile(*app) ==
                      *first_profile_id &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{1U} &&
                  OmegaAppTestAccess::CharacterCatalogCount(
                      *app, *first_profile_id) ==
                      std::optional<std::size_t>{0U} &&
                  empty_character_model ==
                      omega::app::FrontEndCharacterStartupModel{} &&
                  OmegaAppTestAccess::CanCreateFirstCharacter(*app) &&
                  !OmegaAppTestAccess::ActiveCharacter(*app) &&
                  !OmegaAppTestAccess::PersistedConfirmedCharacter(*app) &&
                  OmegaAppTestAccess::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{2U} &&
                  OmegaAppTestAccess::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{2U} &&
                  OmegaAppTestAccess::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{73U},
            context,
            "a later distinct Primary edge confirms PROFILE 1 and enters empty Characters with both fixed character cards resident");

        const bool select_released = PushKey(SDL_SCANCODE_F1, false) &&
            RunOneModalFrameWithExactDraws(*app, 2U, context);
        Check(select_released &&
                  OmegaAppTestAccess::FrontEnd(*app) == kCharactersFirst &&
                  OmegaAppTestAccess::ActiveProfile(*app) == *first_profile_id &&
                  OmegaAppTestAccess::CanCreateFirstCharacter(*app),
            context,
            "the explicit profile-selection release preserves the empty Characters route");

        const bool character_create_pressed =
            PushKey(SDL_SCANCODE_F1, true) &&
            RunOneCharacterMenuFrameWithTextureDelta(
                *app, 0U, 1U, 3U, context);
        const auto created_character = OmegaAppTestAccess::ReadCharacter(
            *app, *first_profile_id, *first_character_id);
        const auto created_character_model =
            OmegaAppTestAccess::FrontEndCharacterModel(*app);
        Check(character_create_pressed &&
                  OmegaAppTestAccess::FrontEnd(*app) == kCharactersFirst &&
                  !OmegaAppTestAccess::CanCreateFirstCharacter(*app) &&
                  OmegaAppTestAccess::CharacterCatalogCount(
                      *app, *first_profile_id) ==
                      std::optional<std::size_t>{1U} &&
                  created_character_model.total_characters == 1U &&
                  created_character_model.visible_characters == 1U &&
                  created_character_model.characters[0].id ==
                      *first_character_id &&
                  created_character &&
                  created_character->id == *first_character_id &&
                  created_character->metadata.display_name ==
                      omega::app::kFrontEndFirstCharacterDisplayName &&
                  created_character->metadata.created_unix_milliseconds ==
                      created_character->metadata.modified_unix_milliseconds &&
                  created_character->metadata_revision == 3U &&
                  !OmegaAppTestAccess::ActiveCharacter(*app) &&
                  !OmegaAppTestAccess::PersistedConfirmedCharacter(*app) &&
                  OmegaAppTestAccess::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{3U} &&
                  OmegaAppTestAccess::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{3U} &&
                  OmegaAppTestAccess::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{125U},
            context,
            "Primary creates fixed-ID DIAGNOSTIC CHARACTER, keeps Characters modal, and releases the obsolete empty card");

        const bool character_create_released =
            PushKey(SDL_SCANCODE_F1, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(character_create_released &&
                  OmegaAppTestAccess::FrontEnd(*app) == kCharactersFirst &&
                  !OmegaAppTestAccess::ActiveCharacter(*app),
            context,
            "the explicit character-creation release leaves the new character unselected");

        const bool character_select_pressed = PushKey(SDL_SCANCODE_F1, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(character_select_pressed &&
                  OmegaAppTestAccess::FrontEnd(*app) == kBriefingRoom &&
                  OmegaAppTestAccess::ActiveProfile(*app) == *first_profile_id &&
                  OmegaAppTestAccess::ActiveCharacter(*app) ==
                      *first_character_id &&
                  OmegaAppTestAccess::PersistedConfirmedCharacter(*app) ==
                      *first_character_id &&
                  OmegaAppTestAccess::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{4U} &&
                  OmegaAppTestAccess::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{4U} &&
                  OmegaAppTestAccess::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{173U},
            context,
            "Primary confirms DIAGNOSTIC CHARACTER and enters the Briefing Room mission selector after the exact 48-byte active-character write");

        const bool character_select_released =
            PushKey(SDL_SCANCODE_F1, false) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(character_select_released &&
                  OmegaAppTestAccess::FrontEnd(*app) == kBriefingRoom &&
                  OmegaAppTestAccess::ActiveCharacter(*app) ==
                      *first_character_id,
            context,
            "the explicit character-selection release preserves the Briefing Room mission selector");

        const bool diagnostic_started = OmegaAppTestAccess::ArmNextRunElapsed(
                                            *app, std::chrono::nanoseconds::zero()) &&
            PushKey(SDL_SCANCODE_F1, true) &&
            RunOneModalFrameWithExactDraws(*app, 3U, context);
        Check(diagnostic_started &&
                  OmegaAppTestAccess::FrontEnd(*app) == kDiagnosticPlay &&
                  OmegaAppTestAccess::ActiveProfile(*app) == *first_profile_id &&
                  OmegaAppTestAccess::PersistedConfirmedProfile(*app) ==
                      *first_profile_id &&
                  OmegaAppTestAccess::ActiveCharacter(*app) ==
                      *first_character_id &&
                  OmegaAppTestAccess::PersistedConfirmedCharacter(*app) ==
                      *first_character_id &&
                  OmegaAppTestAccess::PersistenceGeneration(*app) ==
                      std::optional<std::uint64_t>{5U} &&
                  OmegaAppTestAccess::PersistenceRecordCount(*app) ==
                      std::optional<std::size_t>{5U} &&
                  OmegaAppTestAccess::PersistenceLogicalValueBytes(*app) ==
                      std::optional<std::size_t>{221U},
            context,
            "mission selection commits the exact 48-byte character-owned session checkpoint before publishing DiagnosticPlay");

    }

    auto reopened = NativePersistence::Bootstrap(directory.path());
    std::optional<omega::profiles::CharacterSummary> reopened_character;
    if (reopened)
    {
        auto read = reopened->characters().Read(
            *first_profile_id, *first_character_id);
        if (read && *read)
            reopened_character = std::move(**read);
    }
    Check(reopened && reopened->startup_profiles().size() == 1U &&
              reopened->persisted_confirmed_profile_id() == first_profile_id &&
              reopened->persisted_confirmed_character_id() ==
                  first_character_id &&
              reopened->database().generation() == 5U &&
              reopened->database().record_count() == 5U &&
              reopened->database().logical_value_bytes() == 221U &&
              reopened->startup_profiles()[0].id == *first_profile_id &&
              reopened->startup_profiles()[0].metadata.display_name ==
                  omega::app::kFrontEndFirstProfileDisplayName &&
              reopened->startup_profiles()[0].metadata.created_unix_milliseconds ==
                  creation_timestamp &&
              reopened->startup_profiles()[0].metadata.modified_unix_milliseconds ==
                  creation_timestamp &&
              reopened->startup_profiles()[0].metadata_revision == 1U &&
              reopened_character &&
              reopened_character->id == *first_character_id &&
              reopened_character->metadata.display_name ==
                  omega::app::kFrontEndFirstCharacterDisplayName &&
              reopened_character->metadata.created_unix_milliseconds ==
                  reopened_character->metadata.modified_unix_milliseconds &&
              reopened_character->metadata_revision == 3U,
        context,
        "reopening validates PROFILE 1, DIAGNOSTIC CHARACTER, both durable confirmations, and the exact five-record 221-byte session totals");
}

void CheckPersistenceBackedMovieFailureRoute()
{
    constexpr std::string_view context = "persistent-fail-open-profiles";
    TempDirectory directory(context);
    if (directory.path().empty())
        return;

    auto persistence = NativePersistence::Bootstrap(directory.path());
    Check(persistence && persistence->startup_profiles().empty(), context,
        "the generated failure-route native persistence starts exactly empty");
    if (!persistence || !persistence->startup_profiles().empty())
        return;

    auto observation = std::make_shared<GeneratedPlaybackObservation>();
    {
        auto app = CreatePersistentApp(
            std::make_unique<GeneratedOpeningMovie>(
                GeneratedPlaybackConfig{
                    .mode = GeneratedPlaybackMode::AdvanceFailure,
                },
                observation),
            std::move(*persistence));
        Check(app.has_value(), context,
            "the generated failing movie starts with empty native persistence");
        if (!app)
            return;

        CheckPersistentMovieStartup(*app, context);
        const AppBaseline before = CaptureBaseline(*app);
        auto failed_open = app->Run(1);
        Check(failed_open && failed_open->rendered_frames == 1 &&
                  failed_open->input_frames == 1U &&
                  failed_open->planned_simulation_steps == 0U &&
                  failed_open->executed_simulation_steps == 0U &&
                  !failed_open->quit_requested,
            context,
            "the bounded decoder failure renders one Profiles transition frame");
        Check(observation->advance_calls == 1U &&
                  observation->destruction_count == 1U &&
                  OmegaAppTestAccess::BootSequence(*app).phase ==
                      BootSequencePhase::Failed &&
                  OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst &&
                  OmegaAppTestAccess::CanCreateFirstProfile(*app) &&
                  !OmegaAppTestAccess::ActiveProfile(*app) &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{0U},
            context,
            "movie failure fails open to Profiles without profile mutation");
        CheckTransitionCleanup(*app, before, 0U, 1U, 0U,
            context, 0U, 2U);
    }

    auto reopened = NativePersistence::Bootstrap(directory.path());
    Check(reopened && reopened->startup_profiles().empty(), context,
        "the fail-open route leaves the isolated durable catalog empty");
}

void CheckOwnedSourceCreationFailureRoute()
{
    constexpr std::string_view context = "owned-source-create-fail-open-profiles";
    TempDirectory directory(context);
    if (directory.path().empty())
        return;

    auto persistence = NativePersistence::Bootstrap(directory.path());
    Check(persistence && persistence->startup_profiles().empty(), context,
        "the owned-source failure route starts with empty native persistence");
    if (!persistence || !persistence->startup_profiles().empty())
        return;

    std::vector<std::byte> malformed_bytes{std::byte{0x7FU}};
    auto source = omega::asset::OpeningMovieSource::Create(
        std::move(malformed_bytes));
    Check(source.has_value(), context,
        "the malformed generated payload is still a bounded owned source");
    if (!source)
        return;

    {
        auto app = CreatePersistentAppFromSource(
            std::move(*source), std::move(*persistence));
        Check(app.has_value(), context,
            "owned movie-source validation failure does not fail app creation");
        if (!app)
            return;

        Check(OmegaAppTestAccess::BootSequence(*app) ==
                  omega::app::BootSequenceState{} &&
                  OmegaAppTestAccess::FrontEnd(*app) == kProfilesFirst &&
                  OmegaAppTestAccess::CanCreateFirstProfile(*app) &&
                  !OmegaAppTestAccess::ActiveProfile(*app) &&
                  OmegaAppTestAccess::ProfileCatalogCount(*app) ==
                      std::optional<std::size_t>{0U},
            context,
            "owned source creation failure opens Profiles without profile mutation");
        Check(!OmegaAppTestAccess::HasOpeningMoviePlayback(*app) &&
                  !OmegaAppTestAccess::HasOpeningMovieTexture(*app) &&
                  OmegaAppTestAccess::OpeningMovieDrawCommandCount(*app) == 0U,
            context,
            "owned source creation failure publishes no movie resource");

        std::size_t canonical_warning_count = 0U;
        for (const omega::runtime::LogRecord& record :
            OmegaAppTestAccess::Logs(*app))
        {
            if (record.category == "opening_movie" &&
                record.severity == omega::runtime::LogSeverity::Warning &&
                record.message == omega::app::OpeningMoviePlayerErrorMessage(
                    OpeningMoviePlayerErrorCode::ProgramStreamRejected))
            {
                ++canonical_warning_count;
            }
        }
        Check(canonical_warning_count == 1U, context,
            "owned source creation failure logs one identity-free categorical warning");
    }

    auto reopened = NativePersistence::Bootstrap(directory.path());
    Check(reopened && reopened->startup_profiles().empty(), context,
        "the owned-source fail-open route leaves durable profiles empty");
}
} // namespace

int main()
{
    CheckAmbiguousSourceRejection();
    CheckInvalidMetadataRejection();
    CheckNaturalCompletionAndActionableMenu();
    CheckScheduledSkip(0U, "early-skip");
    CheckScheduledSkip(0U, "space-fire-alias-skip", SDL_SCANCODE_SPACE, true);
    CheckScheduledSkip(2U, "mid-skip");
    CheckScheduledSkip(5U, "late-skip");
    CheckOverReportedPcmRejection();
    CheckHostileRuntimeErrorRedaction();
    CheckLateAudioFaultFailsOpenToActionableMenu();
    CheckLateControlFaultRebaselinesForMenu();
    CheckPersistenceBackedMovieProfileFlow(
        PersistentMovieEntry::NaturalCompletion,
        "persistent-natural-completion", 1'725'000'000'123ULL);
    CheckPersistenceBackedMovieProfileFlow(
        PersistentMovieEntry::PrimarySkip,
        "persistent-primary-skip", 1'725'000'000'124ULL);
    CheckPersistenceBackedMovieFailureRoute();
    CheckOwnedSourceCreationFailureRoute();

    if (failures == 0)
        std::cout << "omega_app_opening_movie_smoke: all checks passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
