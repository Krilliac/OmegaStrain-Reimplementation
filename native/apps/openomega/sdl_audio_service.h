#pragma once

#include <cstdint>
#include <expected>
#include <memory>
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

    // [main thread, startup] Opens and resumes the system-default playback stream. The platform
    // service must outlive the returned audio service.
    [[nodiscard]] static std::expected<SdlAudioService, std::string> Create(
        const SdlPlatformService& platform);

    // [main thread; synchronizes with and stops the SDL audio callback]
    ~SdlAudioService();
    SdlAudioService(SdlAudioService&&) noexcept;
    SdlAudioService& operator=(SdlAudioService&&) noexcept = delete;
    SdlAudioService(const SdlAudioService&) = delete;
    SdlAudioService& operator=(const SdlAudioService&) = delete;

    // [game thread; atomic snapshot]
    [[nodiscard]] AudioServiceSnapshot Snapshot() const noexcept;
    // [main/game thread; immutable after Create()]
    [[nodiscard]] std::string_view driver_name() const noexcept;

private:
    struct Impl;
    explicit SdlAudioService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
