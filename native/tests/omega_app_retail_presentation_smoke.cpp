#include "opening_movie_player.h"
#include "omega_app.h"

#include "omega/runtime/config_service.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/runtime_settings.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::app::detail
{
struct OmegaAppTestAccess final
{
    [[nodiscard]] static std::expected<OmegaApp, std::string> Create(
        runtime::ConfigStore config, const runtime::RuntimeSettings& settings,
        std::unique_ptr<OpeningMoviePlayback> opening_movie_playback,
        const runtime::FrontEndPresentationMode presentation_mode)
    {
        return OmegaApp::CreateWithTextureConfigAndOpeningMoviePlayback(
            std::move(config), settings, runtime::ContentStartupState{}, nullptr,
            false, {}, std::nullopt, std::nullopt,
            std::move(opening_movie_playback), presentation_mode);
    }

    [[nodiscard]] static GpuHostSnapshot Gpu(const OmegaApp& app) noexcept
    {
        return app.host_->Snapshot();
    }

    [[nodiscard]] static simulation::SimulationState Simulation(
        const OmegaApp& app) noexcept
    {
        return app.simulation_->Snapshot();
    }

    [[nodiscard]] static BootSequenceState BootSequence(
        const OmegaApp& app) noexcept
    {
        return app.boot_sequence_state_;
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
};
} // namespace omega::app::detail

namespace
{
using omega::app::GpuHostSnapshot;
using omega::app::OpeningMoviePlayback;
using omega::app::OpeningMoviePlayerError;
using omega::app::OpeningMoviePlayerStatus;
using omega::app::OpeningMoviePlayerUpdate;
using omega::app::OmegaApp;
using omega::app::detail::OmegaAppTestAccess;

constexpr std::uint32_t kMovieWidth = 2U;
constexpr std::uint32_t kMovieHeight = 2U;
constexpr std::uint64_t kMovieLogicalBytes =
    static_cast<std::uint64_t>(kMovieWidth) * kMovieHeight * 4U;
constexpr std::uint64_t kMovieSafetyTicks = 60'000'000U;
constexpr std::string_view kUnavailableError =
    "front-end presentation [presentation-unavailable]: retail front-end presentation is unavailable";

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

struct GeneratedMovieObservation final
{
    std::size_t advance_calls = 0U;
    std::size_t destruction_count = 0U;
};

class GeneratedOpeningMovie final : public OpeningMoviePlayback
{
public:
    explicit GeneratedOpeningMovie(
        std::shared_ptr<GeneratedMovieObservation> observation)
        : observation_(std::move(observation))
    {
        frame_.width = kMovieWidth;
        frame_.height = kMovieHeight;
        frame_.pixels.resize(
            static_cast<std::size_t>(kMovieLogicalBytes), std::byte{0});
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
        const bool first_frame = observation_->advance_calls == 1U;
        return OpeningMoviePlayerUpdate{
            .status = first_frame ? OpeningMoviePlayerStatus::Playing
                                  : OpeningMoviePlayerStatus::Completed,
            .frame_updated = first_frame,
            .current_frame = &frame_,
        };
    }

    [[nodiscard]] std::expected<std::uint64_t, OpeningMoviePlayerError>
    ReadAudioFrames(const std::span<std::int16_t>) override
    {
        return 0U;
    }

    [[nodiscard]] bool audio_finished() const noexcept override
    {
        return true;
    }

    [[nodiscard]] std::uint32_t width() const noexcept override
    {
        return kMovieWidth;
    }

    [[nodiscard]] std::uint32_t height() const noexcept override
    {
        return kMovieHeight;
    }

    [[nodiscard]] std::uint64_t safety_duration_ticks() const noexcept override
    {
        return kMovieSafetyTicks;
    }

private:
    std::shared_ptr<GeneratedMovieObservation> observation_;
    omega::media::Rgba8VideoFrame frame_;
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

[[nodiscard]] std::expected<OmegaApp, std::string> CreateRetailApp(
    std::unique_ptr<OpeningMoviePlayback> playback)
{
    auto config = omega::runtime::ParseConfigText("");
    if (!config)
        return std::unexpected("test config: " + config.error());
    return OmegaAppTestAccess::Create(std::move(*config), TestSettings(),
        std::move(playback),
        omega::runtime::FrontEndPresentationMode::RetailRequired);
}

[[nodiscard]] bool SameSimulationState(
    const omega::simulation::SimulationState& left,
    const omega::simulation::SimulationState& right) noexcept
{
    return left.completed_steps == right.completed_steps &&
           left.simulated_time == right.simulated_time &&
           left.alive_entities == right.alive_entities;
}

void CheckBoundaryWithoutMovie()
{
    auto app = CreateRetailApp(nullptr);
    Check(app.has_value(), "retail-required host starts without a movie");
    if (!app)
        return;

    const GpuHostSnapshot before_gpu = OmegaAppTestAccess::Gpu(*app);
    const auto before_simulation = OmegaAppTestAccess::Simulation(*app);
    const auto rejected = app->Run(1);
    const GpuHostSnapshot after_gpu = OmegaAppTestAccess::Gpu(*app);
    Check(!rejected && rejected.error() == kUnavailableError,
        "normal mode fails closed with a fixed identity-free error");
    Check(after_gpu.frame_submissions == before_gpu.frame_submissions &&
              after_gpu.blit_submissions == before_gpu.blit_submissions &&
              after_gpu.successful_blit_draws ==
                  before_gpu.successful_blit_draws,
        "normal mode submits no project-authored front-end draw list");
    Check(SameSimulationState(
              OmegaAppTestAccess::Simulation(*app), before_simulation),
        "normal mode advances no project-authored simulation");
}

void CheckBoundaryAfterMovie()
{
    auto observation = std::make_shared<GeneratedMovieObservation>();
    auto app = CreateRetailApp(
        std::make_unique<GeneratedOpeningMovie>(observation));
    Check(app.has_value(),
        "retail-required host retains generated opening playback");
    if (!app)
        return;

    const GpuHostSnapshot before_gpu = OmegaAppTestAccess::Gpu(*app);
    const auto before_simulation = OmegaAppTestAccess::Simulation(*app);
    const auto movie_frame = app->Run(1);
    const GpuHostSnapshot after_movie = OmegaAppTestAccess::Gpu(*app);
    Check(movie_frame && movie_frame->rendered_frames == 1 &&
              movie_frame->input_frames == 1U &&
              movie_frame->executed_simulation_steps == 0U,
        "the opening-movie seam renders before retail UI is available");
    Check(after_movie.frame_submissions == before_gpu.frame_submissions + 1U &&
              after_movie.blit_submissions == before_gpu.blit_submissions + 1U &&
              after_movie.successful_blit_draws ==
                  before_gpu.successful_blit_draws + 1U,
        "the first submitted frame contains only the opening movie draw");

    const auto rejected = app->Run(1);
    const GpuHostSnapshot after_rejection = OmegaAppTestAccess::Gpu(*app);
    Check(!rejected && rejected.error() == kUnavailableError,
        "movie completion fails closed before diagnostic front-end submission");
    Check(after_rejection.frame_submissions == after_movie.frame_submissions &&
              after_rejection.blit_submissions == after_movie.blit_submissions &&
              after_rejection.successful_blit_draws ==
                  after_movie.successful_blit_draws,
        "the completion frame submits no project-authored menu pixels");
    Check(observation->advance_calls == 2U &&
              observation->destruction_count == 1U &&
              !OmegaAppTestAccess::HasOpeningMoviePlayback(*app) &&
              !OmegaAppTestAccess::HasOpeningMovieTexture(*app) &&
              OmegaAppTestAccess::BootSequence(*app).phase ==
                  omega::app::BootSequencePhase::Completed,
        "movie ownership completes and releases before the retail boundary reports unavailable");
    Check(SameSimulationState(
              OmegaAppTestAccess::Simulation(*app), before_simulation),
        "movie and rejected transition frames advance no project-authored simulation");
}
} // namespace

int main()
{
    CheckBoundaryWithoutMovie();
    CheckBoundaryAfterMovie();

    if (failures != 0)
    {
        std::cerr << failures << " retail-presentation smoke check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "omega_app_retail_presentation_smoke: all checks passed\n";
    return EXIT_SUCCESS;
}
