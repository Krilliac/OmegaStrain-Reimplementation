#include "sdl_audio_service.h"

#include "sdl_platform_service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace omega::app
{
namespace
{
constexpr int kBytesPerFrame = static_cast<int>(sizeof(float)) * SdlAudioService::kChannelCount;
constexpr int kCallbackChunkBytes = 4'096;
constexpr int kMaximumBytesPerCallback = 1'048'576;
constexpr std::size_t kCallbackChunkSamples =
    static_cast<std::size_t>(kCallbackChunkBytes / static_cast<int>(sizeof(float)));
constexpr std::size_t kOpeningMovieRingSamples =
    static_cast<std::size_t>(SdlAudioService::kOpeningMovieQueueCapacityFrames) *
    SdlAudioService::kChannelCount;

static_assert(kCallbackChunkBytes % kBytesPerFrame == 0);
static_assert((SdlAudioService::kOpeningMovieQueueCapacityFrames &
                  (SdlAudioService::kOpeningMovieQueueCapacityFrames - 1U)) == 0U);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);

[[nodiscard]] std::string SdlError(const std::string_view operation)
{
    const char* detail = SDL_GetError();
    return std::string(operation) + ": " +
           (detail != nullptr && detail[0] != '\0' ? detail : "unknown SDL error");
}
} // namespace

struct SdlAudioService::Impl
{
    ~Impl()
    {
        if (stream != nullptr)
            SDL_DestroyAudioStream(stream);
        if (subsystem_initialized)
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }

    static void SDLCALL ProvidePlayback(void* userdata, SDL_AudioStream* stream,
        const int additional_amount, const int total_amount) noexcept
    {
        (void)total_amount;
        auto& self = *static_cast<Impl*>(userdata);
        self.callback_count.fetch_add(1U, std::memory_order_relaxed);
        if (additional_amount <= 0)
            return;
        if (additional_amount > kMaximumBytesPerCallback)
        {
            self.callback_failures.fetch_add(1U, std::memory_order_relaxed);
            return;
        }

        int remaining = additional_amount;
        std::array<float, kCallbackChunkSamples> samples{};
        while (remaining > 0)
        {
            const int bounded = std::min(remaining, kCallbackChunkBytes);
            const int aligned =
                ((bounded + kBytesPerFrame - 1) / kBytesPerFrame) * kBytesPerFrame;
            const std::uint64_t requested_frames =
                static_cast<std::uint64_t>(aligned / kBytesPerFrame);
            std::fill(samples.begin(), samples.end(), 0.0F);

            std::uint64_t read_sequence =
                self.opening_movie_read_sequence.load(std::memory_order_relaxed);
            const std::uint64_t write_sequence =
                self.opening_movie_write_sequence.load(std::memory_order_acquire);
            const std::uint64_t available_frames = write_sequence >= read_sequence
                ? std::min(write_sequence - read_sequence,
                      SdlAudioService::kOpeningMovieQueueCapacityFrames)
                : 0U;
            const std::uint64_t consumed_frames =
                std::min(requested_frames, available_frames);
            for (std::uint64_t frame = 0; frame < consumed_frames; ++frame)
            {
                const std::uint64_t ring_frame =
                    (read_sequence + frame) &
                    (SdlAudioService::kOpeningMovieQueueCapacityFrames - 1U);
                const std::size_t ring_sample =
                    static_cast<std::size_t>(ring_frame) * SdlAudioService::kChannelCount;
                const std::size_t output_sample =
                    static_cast<std::size_t>(frame) * SdlAudioService::kChannelCount;
                samples[output_sample] = self.opening_movie_samples[ring_sample];
                samples[output_sample + 1U] = self.opening_movie_samples[ring_sample + 1U];
            }

            if (!SDL_PutAudioStreamData(stream, samples.data(), aligned))
            {
                self.callback_failures.fetch_add(1U, std::memory_order_relaxed);
                return;
            }
            read_sequence += consumed_frames;
            self.opening_movie_read_sequence.store(read_sequence, std::memory_order_release);
            self.provided_frames.fetch_add(
                requested_frames, std::memory_order_relaxed);
            self.opening_movie_frames_consumed.fetch_add(
                consumed_frames, std::memory_order_relaxed);
            if (self.opening_movie_session_running.load(std::memory_order_acquire))
            {
                self.opening_movie_timeline_frames.fetch_add(
                    requested_frames, std::memory_order_relaxed);
            }
            const bool producer_finished =
                self.opening_movie_producer_finished.load(std::memory_order_acquire);
            if (consumed_frames < requested_frames && !producer_finished)
            {
                self.opening_movie_underrun_frames.fetch_add(
                    requested_frames - consumed_frames, std::memory_order_relaxed);
            }
            remaining -= aligned;
        }
    }

    [[nodiscard]] std::uint64_t ReadSequence() const noexcept
    {
        return opening_movie_read_sequence.load(std::memory_order_acquire);
    }

    void RecordControlFailure() noexcept
    {
        opening_movie_control_failures.fetch_add(1U, std::memory_order_relaxed);
    }

    [[nodiscard]] bool ContainOpeningMovieAudio(const bool count_discard) noexcept
    {
        bool contained = true;
        const bool paused = SDL_PauseAudioStreamDevice(stream);
        if (!paused)
        {
            RecordControlFailure();
            contained = false;
        }
        else
        {
            opening_movie_device_resumed.store(false, std::memory_order_release);
        }

        const bool locked = SDL_LockAudioStream(stream);
        if (!locked)
        {
            RecordControlFailure();
            contained = false;
        }

        const bool cleared = SDL_ClearAudioStream(stream);
        if (!cleared)
        {
            RecordControlFailure();
            contained = false;
        }

        bool unlocked = false;
        if (locked)
        {
            std::fill(opening_movie_samples.begin(), opening_movie_samples.end(), 0.0F);
            opening_movie_write_sequence.store(0U, std::memory_order_release);
            opening_movie_read_sequence.store(0U, std::memory_order_release);
            opening_movie_producer_finished.store(true, std::memory_order_release);
            opening_movie_session_running.store(false, std::memory_order_release);
            unlocked = SDL_UnlockAudioStream(stream);
            if (!unlocked)
            {
                RecordControlFailure();
                contained = false;
            }
        }

        if (count_discard)
            opening_movie_discard_count.fetch_add(1U, std::memory_order_relaxed);
        containment_ready = contained && locked && cleared && unlocked;
        return containment_ready;
    }

    bool subsystem_initialized = false;
    SDL_AudioStream* stream = nullptr;
    std::string driver;
    std::thread::id creator_thread;
    bool containment_ready = true;
    std::array<float, kOpeningMovieRingSamples> opening_movie_samples{};
    std::atomic<std::uint64_t> opening_movie_write_sequence{0U};
    std::atomic<std::uint64_t> opening_movie_read_sequence{0U};
    std::atomic<bool> opening_movie_producer_finished{true};
    std::atomic<bool> opening_movie_session_running{false};
    std::atomic<bool> opening_movie_device_resumed{false};
    std::atomic<std::uint64_t> callback_count{0};
    std::atomic<std::uint64_t> provided_frames{0};
    std::atomic<std::uint64_t> callback_failures{0};
    std::atomic<std::uint64_t> opening_movie_frames_consumed{0U};
    std::atomic<std::uint64_t> opening_movie_timeline_frames{0U};
    std::atomic<std::uint64_t> opening_movie_underrun_frames{0U};
    std::atomic<std::uint64_t> opening_movie_queue_rejections{0U};
    std::atomic<std::uint64_t> opening_movie_discard_count{0U};
    std::atomic<std::uint64_t> opening_movie_control_failures{0U};
    std::atomic<std::uint64_t> opening_movie_session_generation{0U};
};

std::expected<SdlAudioService, std::string> SdlAudioService::Create(
    const SdlPlatformService& platform)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");

    auto impl = std::make_unique<Impl>();
    impl->creator_thread = std::this_thread::get_id();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
        return std::unexpected(SdlError("SDL_InitSubSystem(audio)"));
    impl->subsystem_initialized = true;

    const char* driver = SDL_GetCurrentAudioDriver();
    impl->driver = driver != nullptr ? driver : "unknown";

    const SDL_AudioSpec source_format{
        .format = SDL_AUDIO_F32,
        .channels = kChannelCount,
        .freq = kSampleRate,
    };
    impl->stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &source_format,
        &Impl::ProvidePlayback, impl.get());
    if (impl->stream == nullptr)
        return std::unexpected(SdlError("SDL_OpenAudioDeviceStream"));
    return SdlAudioService(std::move(impl));
}

SdlAudioService::SdlAudioService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SdlAudioService::~SdlAudioService() = default;
SdlAudioService::SdlAudioService(SdlAudioService&&) noexcept = default;

AudioServiceSnapshot SdlAudioService::Snapshot() const noexcept
{
    if (!impl_)
        return {};
    const std::uint64_t write_sequence =
        impl_->opening_movie_write_sequence.load(std::memory_order_acquire);
    const std::uint64_t read_sequence = impl_->ReadSequence();
    const std::uint64_t queued_frames = write_sequence >= read_sequence
        ? std::min(write_sequence - read_sequence, kOpeningMovieQueueCapacityFrames)
        : 0U;
    const bool producer_finished =
        impl_->opening_movie_producer_finished.load(std::memory_order_acquire);
    return AudioServiceSnapshot{
        .callback_count = impl_->callback_count.load(std::memory_order_relaxed),
        .provided_frames = impl_->provided_frames.load(std::memory_order_relaxed),
        .callback_failures = impl_->callback_failures.load(std::memory_order_relaxed),
        .opening_movie_queued_frames = queued_frames,
        .opening_movie_frames_consumed =
            impl_->opening_movie_frames_consumed.load(std::memory_order_relaxed),
        .opening_movie_timeline_frames =
            impl_->opening_movie_timeline_frames.load(std::memory_order_relaxed),
        .opening_movie_underrun_frames =
            impl_->opening_movie_underrun_frames.load(std::memory_order_relaxed),
        .opening_movie_queue_rejections =
            impl_->opening_movie_queue_rejections.load(std::memory_order_relaxed),
        .opening_movie_discard_count =
            impl_->opening_movie_discard_count.load(std::memory_order_relaxed),
        .opening_movie_control_failures =
            impl_->opening_movie_control_failures.load(std::memory_order_relaxed),
        .opening_movie_session_generation =
            impl_->opening_movie_session_generation.load(std::memory_order_relaxed),
        .opening_movie_active = queued_frames != 0U || !producer_finished,
        .opening_movie_session_running =
            impl_->opening_movie_session_running.load(std::memory_order_acquire),
        .opening_movie_device_resumed =
            impl_->opening_movie_device_resumed.load(std::memory_order_acquire),
    };
}

bool SdlAudioService::QueueOpeningMoviePcm16(
    const std::span<const std::int16_t> interleaved_samples,
    const bool end_of_stream) noexcept
{
    if (!impl_ || std::this_thread::get_id() != impl_->creator_thread ||
        !impl_->containment_ready ||
        interleaved_samples.empty() ||
        interleaved_samples.size() % static_cast<std::size_t>(kChannelCount) != 0U)
    {
        if (impl_)
            impl_->opening_movie_queue_rejections.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    const std::uint64_t frame_count = interleaved_samples.size() / kChannelCount;
    const std::uint64_t write_sequence =
        impl_->opening_movie_write_sequence.load(std::memory_order_relaxed);
    const std::uint64_t read_sequence = impl_->ReadSequence();
    if (write_sequence < read_sequence ||
        write_sequence - read_sequence > kOpeningMovieQueueCapacityFrames ||
        frame_count > kOpeningMovieQueueCapacityFrames - (write_sequence - read_sequence))
    {
        impl_->opening_movie_queue_rejections.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }

    for (std::uint64_t frame = 0; frame < frame_count; ++frame)
    {
        const std::uint64_t ring_frame =
            (write_sequence + frame) & (kOpeningMovieQueueCapacityFrames - 1U);
        const std::size_t ring_sample =
            static_cast<std::size_t>(ring_frame) * kChannelCount;
        const std::size_t input_sample = static_cast<std::size_t>(frame) * kChannelCount;
        impl_->opening_movie_samples[ring_sample] =
            static_cast<float>(interleaved_samples[input_sample]) / 32'768.0F;
        impl_->opening_movie_samples[ring_sample + 1U] =
            static_cast<float>(interleaved_samples[input_sample + 1U]) / 32'768.0F;
    }
    const bool starting_session =
        !impl_->opening_movie_device_resumed.load(std::memory_order_acquire);
    if (!SDL_LockAudioStream(impl_->stream))
    {
        impl_->RecordControlFailure();
        impl_->opening_movie_queue_rejections.fetch_add(1U, std::memory_order_relaxed);
        (void)impl_->ContainOpeningMovieAudio(false);
        return false;
    }

    impl_->opening_movie_producer_finished.store(end_of_stream, std::memory_order_release);
    if (starting_session)
    {
        impl_->opening_movie_session_generation.fetch_add(1U, std::memory_order_relaxed);
        impl_->opening_movie_session_running.store(true, std::memory_order_release);
    }
    impl_->opening_movie_write_sequence.store(
        write_sequence + frame_count, std::memory_order_release);
    if (!SDL_UnlockAudioStream(impl_->stream))
    {
        impl_->RecordControlFailure();
        impl_->opening_movie_queue_rejections.fetch_add(1U, std::memory_order_relaxed);
        impl_->containment_ready = false;
        return false;
    }

    if (starting_session)
    {
        if (!SDL_ResumeAudioStreamDevice(impl_->stream))
        {
            impl_->RecordControlFailure();
            impl_->opening_movie_queue_rejections.fetch_add(1U, std::memory_order_relaxed);
            (void)impl_->ContainOpeningMovieAudio(false);
            return false;
        }
        impl_->opening_movie_device_resumed.store(true, std::memory_order_release);
    }
    return true;
}

std::uint64_t SdlAudioService::OpeningMovieAvailableFrames() const noexcept
{
    if (!impl_ || std::this_thread::get_id() != impl_->creator_thread ||
        !impl_->containment_ready)
        return 0U;
    const std::uint64_t write_sequence =
        impl_->opening_movie_write_sequence.load(std::memory_order_acquire);
    const std::uint64_t read_sequence = impl_->ReadSequence();
    if (write_sequence < read_sequence ||
        write_sequence - read_sequence > kOpeningMovieQueueCapacityFrames)
    {
        return 0U;
    }
    return kOpeningMovieQueueCapacityFrames - (write_sequence - read_sequence);
}

bool SdlAudioService::DiscardOpeningMovieAudio() noexcept
{
    if (!impl_ || std::this_thread::get_id() != impl_->creator_thread)
    {
        if (impl_)
            impl_->opening_movie_control_failures.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    return impl_->ContainOpeningMovieAudio(true);
}

std::string_view SdlAudioService::driver_name() const noexcept
{
    return impl_ ? std::string_view{impl_->driver} : std::string_view{};
}
} // namespace omega::app
