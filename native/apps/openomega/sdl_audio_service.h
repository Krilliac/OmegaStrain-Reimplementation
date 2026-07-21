#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace omega::app
{
class SdlPlatformService;

struct AudioServiceSnapshot
{
    std::uint64_t callback_count = 0;
    std::uint64_t provided_frames = 0;
    std::uint64_t callback_failures = 0;
    std::uint64_t opening_movie_queued_frames = 0;
    std::uint64_t opening_movie_frames_consumed = 0;
    std::uint64_t opening_movie_timeline_frames = 0;
    std::uint64_t opening_movie_underrun_frames = 0;
    std::uint64_t opening_movie_queue_rejections = 0;
    std::uint64_t opening_movie_discard_count = 0;
    std::uint64_t opening_movie_control_failures = 0;
    std::uint64_t opening_movie_session_generation = 0;
    bool opening_movie_active = false;
    bool opening_movie_session_running = false;
    bool opening_movie_device_resumed = false;
};

// Non-hot-reloadable playback-device owner. The game thread observes atomics only; SDL invokes the
// provider on its audio thread, where project code performs no file access, logging, explicit
// locking, or dynamic allocation.
class SdlAudioService final
{
public:
    // Synthetic native mix format, not an assertion about the retail engine or stored audio.
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannelCount = 2;
    // An 85.3 ms source-frame ceiling at the fixed native mix rate. The game-thread producer
    // refills below this ceiling; the audio callback never allocates, locks, or grows it.
    static constexpr std::uint64_t kOpeningMovieQueueCapacityFrames = 4'096U;

    // [main thread, startup] Opens the system-default playback stream and deliberately leaves its
    // device paused. The first accepted movie span resumes it; the platform service must outlive
    // the returned audio service.
    [[nodiscard]] static std::expected<SdlAudioService, std::string> Create(
        const SdlPlatformService& platform);

    // [main thread; synchronizes with and stops the SDL audio callback]
    ~SdlAudioService();
    SdlAudioService(SdlAudioService&&) noexcept;
    SdlAudioService& operator=(SdlAudioService&&) noexcept = delete;
    SdlAudioService(const SdlAudioService&) = delete;
    SdlAudioService& operator=(const SdlAudioService&) = delete;

    // [game thread; independent atomic counter/state observations]
    [[nodiscard]] AudioServiceSnapshot Snapshot() const noexcept;
    // [creating main/game thread; single producer] Converts and enqueues one exact frame-aligned
    // host-endian signed-16 stereo span. The call is all-or-nothing and never allocates. Setting
    // end_of_stream on the final non-empty span suppresses underrun accounting after it drains.
    [[nodiscard]] bool QueueOpeningMoviePcm16(
        std::span<const std::int16_t> interleaved_samples,
        bool end_of_stream = false) noexcept;
    // [creating main/game thread] Number of source frames that an exact enqueue can currently fit.
    [[nodiscard]] std::uint64_t OpeningMovieAvailableFrames() const noexcept;
    // [creating main/game thread] Pauses the device, excludes the callback through SDL's stream
    // lock, clears SDL's conversion backlog, zeroes the fixed PCM ring, and resets session state.
    // Used on skip, completion, or presentation failure. No path/media data is retained or reported.
    [[nodiscard]] bool DiscardOpeningMovieAudio() noexcept;
    // [main/game thread; immutable after Create()]
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    struct Impl;
    explicit SdlAudioService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
