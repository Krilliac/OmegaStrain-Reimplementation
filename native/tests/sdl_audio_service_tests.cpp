#include "sdl_audio_service.h"
#include "sdl_platform_service.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <thread>
#include <utility>

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

template <typename Predicate>
omega::app::AudioServiceSnapshot WaitForSnapshot(
    omega::app::SdlAudioService& audio, Predicate predicate)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    omega::app::AudioServiceSnapshot snapshot;
    do
    {
        snapshot = audio.Snapshot();
        if (predicate(snapshot))
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < deadline);
    return snapshot;
}
} // namespace

int main()
{
    auto platform = omega::app::SdlPlatformService::Create();
    Check(platform.has_value(), "the SDL process-global runtime initializes");
    if (!platform)
    {
        std::cerr << platform.error() << '\n';
        return 1;
    }

    auto created_audio = omega::app::SdlAudioService::Create(*platform);
    Check(created_audio.has_value(), "the dummy default playback stream opens paused");
    if (!created_audio)
    {
        std::cerr << created_audio.error() << '\n';
        return 1;
    }

    omega::app::SdlAudioService audio = std::move(*created_audio);
    Check(audio.driver_name() == "dummy", "CTest selects SDL's deterministic dummy driver");
    Check(omega::app::SdlAudioService::kSampleRate == 48'000 &&
              omega::app::SdlAudioService::kChannelCount == 2,
        "the synthetic native mix format is explicit");

    std::this_thread::sleep_for(std::chrono::milliseconds{20});
    omega::app::AudioServiceSnapshot snapshot = audio.Snapshot();
    Check(snapshot.callback_count == 0U && snapshot.provided_frames == 0U,
        "the device does not generate silence before the first movie frame");
    Check(snapshot.opening_movie_queued_frames == 0U &&
              snapshot.opening_movie_frames_consumed == 0U &&
              snapshot.opening_movie_timeline_frames == 0U &&
              snapshot.opening_movie_underrun_frames == 0U &&
              !snapshot.opening_movie_active &&
              !snapshot.opening_movie_session_running &&
              !snapshot.opening_movie_device_resumed,
        "opening-movie playback begins paused and inactive");

    constexpr std::size_t kMovieSamples = static_cast<std::size_t>(
        omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames) *
        omega::app::SdlAudioService::kChannelCount;
    std::array<std::int16_t, kMovieSamples> movie_samples{};
    for (std::size_t index = 0; index < movie_samples.size(); ++index)
        movie_samples[index] = (index % 2U) == 0U ? std::int16_t{16'384} : std::int16_t{-16'384};

    const std::array<std::int16_t, 1> unaligned_sample{1};
    const auto short_samples = std::span<const std::int16_t>(movie_samples).first(512U);
    Check(!audio.QueueOpeningMoviePcm16(unaligned_sample),
        "movie queue rejects a sample span that is not stereo-frame aligned");

    constexpr std::uint64_t kFinalPublishIterations = 16U;
    std::uint64_t expected_consumed_frames = 0U;
    std::uint64_t previous_generation = 0U;
    for (std::uint64_t iteration = 0U; iteration < kFinalPublishIterations; ++iteration)
    {
        Check(audio.QueueOpeningMoviePcm16(movie_samples, true),
            "a full final span publishes and resumes one playback generation");
        snapshot = WaitForSnapshot(audio, [expected_consumed_frames](const auto& current)
        {
            return current.opening_movie_frames_consumed >=
                    expected_consumed_frames +
                        omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames &&
                !current.opening_movie_active;
        });
        expected_consumed_frames +=
            omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames;
        Check(snapshot.opening_movie_frames_consumed == expected_consumed_frames &&
                  snapshot.opening_movie_queued_frames == 0U &&
                  !snapshot.opening_movie_active,
            "a final span drains exactly once without a stuck active state");
        Check(snapshot.opening_movie_session_running &&
                  snapshot.opening_movie_device_resumed &&
                  snapshot.opening_movie_session_generation > previous_generation,
            "the device timeline remains live after PCM end until synchronized discard");
        previous_generation = snapshot.opening_movie_session_generation;
        Check(audio.DiscardOpeningMovieAudio(),
            "synchronized discard pauses, clears, zeroes, and resets one generation");
        snapshot = audio.Snapshot();
        Check(snapshot.opening_movie_queued_frames == 0U &&
                  !snapshot.opening_movie_active &&
                  !snapshot.opening_movie_session_running &&
                  !snapshot.opening_movie_device_resumed &&
                  audio.OpeningMovieAvailableFrames() ==
                      omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames,
            "discard leaves a paused empty ring ready for exact reuse");
    }

    Check(snapshot.opening_movie_underrun_frames == 0U,
        "final spans and post-EOS timeline silence never count as underrun");
    Check(snapshot.opening_movie_queue_rejections == 1U,
        "invalid queue rejection remains observable across playback generations");

    constexpr std::uint64_t kRunningFinalRefillIterations = 16U;
    for (std::uint64_t iteration = 0U; iteration < kRunningFinalRefillIterations; ++iteration)
    {
        const omega::app::AudioServiceSnapshot before_running_refill = audio.Snapshot();
        Check(audio.QueueOpeningMoviePcm16(movie_samples),
            "a full non-final span starts a running publication generation");
        snapshot = WaitForSnapshot(audio, [](const auto& current)
        {
            return current.opening_movie_queued_frames >=
                    omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames / 2U &&
                current.opening_movie_queued_frames <
                    omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames;
        });
        Check(snapshot.opening_movie_queued_frames >=
                  omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames / 2U &&
                  snapshot.opening_movie_queued_frames <
                      omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames,
            "the callback exposes bounded refill space while the non-final span remains active");
        Check(audio.QueueOpeningMoviePcm16(short_samples, true),
            "final extent and EOS publish together while the callback is active");
        snapshot = WaitForSnapshot(audio, [before_running_refill](const auto& current)
        {
            return current.opening_movie_frames_consumed >=
                    before_running_refill.opening_movie_frames_consumed +
                        omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames + 256U &&
                !current.opening_movie_active;
        });
        Check(snapshot.opening_movie_frames_consumed ==
                  before_running_refill.opening_movie_frames_consumed +
                      omega::app::SdlAudioService::kOpeningMovieQueueCapacityFrames + 256U &&
                  snapshot.opening_movie_underrun_frames ==
                      before_running_refill.opening_movie_underrun_frames &&
                  !snapshot.opening_movie_active,
            "running non-final to final publication consumes exact PCM without a silent EOS gap");
        Check(audio.DiscardOpeningMovieAudio(),
            "running final-refill generation is synchronized and contained");
    }

    const std::uint64_t underrun_before = snapshot.opening_movie_underrun_frames;
    Check(audio.QueueOpeningMoviePcm16(short_samples),
        "a short non-final span starts a bounded active generation");
    snapshot = WaitForSnapshot(audio, [underrun_before](const auto& current)
    {
        return current.opening_movie_underrun_frames > underrun_before;
    });
    Check(snapshot.opening_movie_active && snapshot.opening_movie_session_running &&
              snapshot.opening_movie_underrun_frames > underrun_before,
        "a pre-EOS underrun is explicit and keeps the source generation active");
    Check(audio.DiscardOpeningMovieAudio(),
        "discard contains a terminal pre-EOS underrun");

    Check(audio.QueueOpeningMoviePcm16(short_samples),
        "a non-final generation can begin after underrun containment");
    Check(audio.DiscardOpeningMovieAudio(),
        "immediate synchronized discard excludes the callback before ring reuse");
    const std::uint64_t consumed_before_restart =
        audio.Snapshot().opening_movie_frames_consumed;
    Check(audio.QueueOpeningMoviePcm16(short_samples, true),
        "the ring can restart immediately with a final span after discard");
    snapshot = WaitForSnapshot(audio, [consumed_before_restart](const auto& current)
    {
        return current.opening_movie_frames_consumed >= consumed_before_restart + 256U &&
            !current.opening_movie_active;
    });
    Check(snapshot.opening_movie_frames_consumed == consumed_before_restart + 256U,
        "restart consumes the exact new span without stale-frame overlap");
    Check(audio.DiscardOpeningMovieAudio(), "restart generation is contained");

    const std::uint64_t discard_before = audio.Snapshot().opening_movie_discard_count;
    Check(audio.DiscardOpeningMovieAudio() && audio.DiscardOpeningMovieAudio(),
        "repeated discard is idempotent and remains successful");
    snapshot = audio.Snapshot();
    Check(snapshot.opening_movie_discard_count == discard_before + 2U &&
              snapshot.opening_movie_control_failures == 0U,
        "repeated containment attempts remain observable and healthy");

    bool wrong_thread_queue = true;
    bool wrong_thread_discard = true;
    std::uint64_t wrong_thread_available = 1U;
    const std::uint64_t rejection_before = snapshot.opening_movie_queue_rejections;
    const std::uint64_t control_before = snapshot.opening_movie_control_failures;
    std::thread wrong_thread([&]()
    {
        wrong_thread_queue = audio.QueueOpeningMoviePcm16(short_samples, true);
        wrong_thread_available = audio.OpeningMovieAvailableFrames();
        wrong_thread_discard = audio.DiscardOpeningMovieAudio();
    });
    wrong_thread.join();
    snapshot = audio.Snapshot();
    Check(!wrong_thread_queue && wrong_thread_available == 0U && !wrong_thread_discard,
        "producer and containment controls reject the wrong thread");
    Check(snapshot.opening_movie_queue_rejections == rejection_before + 1U &&
              snapshot.opening_movie_control_failures == control_before + 1U,
        "wrong-thread control rejection is visible without touching the ring");
    Check(audio.DiscardOpeningMovieAudio(),
        "the creator thread can still contain after a rejected wrong-thread control");

    omega::app::SdlAudioService moved = std::move(audio);
    const omega::app::AudioServiceSnapshot moved_from_snapshot = audio.Snapshot();
    Check(moved_from_snapshot.callback_count == 0U &&
              moved_from_snapshot.opening_movie_queued_frames == 0U &&
              !moved_from_snapshot.opening_movie_session_running &&
              audio.driver_name().empty() && audio.OpeningMovieAvailableFrames() == 0U &&
              !audio.QueueOpeningMoviePcm16(short_samples, true) &&
              !audio.DiscardOpeningMovieAudio(),
        "moved-from audio observers and controls remain inert");
    snapshot = moved.Snapshot();
    Check(snapshot.callback_failures == 0U,
        "queue, timeline, drain, discard, and move preserve callback health");

    if (failures == 0)
        std::cout << "omega_sdl_audio_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
