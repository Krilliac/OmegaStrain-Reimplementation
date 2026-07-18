#include "sdl_audio_service.h"

#include "sdl_platform_service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app
{
namespace
{
constexpr int kBytesPerFrame = static_cast<int>(sizeof(float)) * SdlAudioService::kChannelCount;
constexpr int kSilenceChunkBytes = 4'096;
constexpr int kMaximumBytesPerCallback = 1'048'576;
constexpr std::array<std::byte, static_cast<std::size_t>(kSilenceChunkBytes)> kSilence{};

static_assert(kSilenceChunkBytes % kBytesPerFrame == 0);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

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
        while (remaining > 0)
        {
            const int bounded = std::min(remaining, kSilenceChunkBytes);
            const int aligned =
                ((bounded + kBytesPerFrame - 1) / kBytesPerFrame) * kBytesPerFrame;
            if (!SDL_PutAudioStreamData(stream, kSilence.data(), aligned))
            {
                self.callback_failures.fetch_add(1U, std::memory_order_relaxed);
                return;
            }
            self.provided_frames.fetch_add(
                static_cast<std::uint64_t>(aligned / kBytesPerFrame),
                std::memory_order_relaxed);
            remaining -= aligned;
        }
    }

    bool subsystem_initialized = false;
    SDL_AudioStream* stream = nullptr;
    std::string driver;
    std::atomic<std::uint64_t> callback_count{0};
    std::atomic<std::uint64_t> provided_frames{0};
    std::atomic<std::uint64_t> callback_failures{0};
};

std::expected<SdlAudioService, std::string> SdlAudioService::Create(
    const SdlPlatformService& platform)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");

    auto impl = std::make_unique<Impl>();
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
    if (!SDL_ResumeAudioStreamDevice(impl->stream))
        return std::unexpected(SdlError("SDL_ResumeAudioStreamDevice"));
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
    return AudioServiceSnapshot{
        .callback_count = impl_->callback_count.load(std::memory_order_relaxed),
        .provided_frames = impl_->provided_frames.load(std::memory_order_relaxed),
        .callback_failures = impl_->callback_failures.load(std::memory_order_relaxed),
    };
}

std::string_view SdlAudioService::driver_name() const noexcept
{
    return impl_->driver;
}
} // namespace omega::app
