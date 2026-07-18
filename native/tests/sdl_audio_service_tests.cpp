#include "sdl_audio_service.h"
#include "sdl_platform_service.h"

#include <chrono>
#include <iostream>
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
    Check(created_audio.has_value(), "the dummy default playback stream opens and resumes");
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

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
    omega::app::AudioServiceSnapshot snapshot;
    do
    {
        snapshot = audio.Snapshot();
        if (snapshot.provided_frames != 0U || snapshot.callback_failures != 0U)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while (std::chrono::steady_clock::now() < deadline);

    Check(snapshot.callback_count != 0U, "the resumed device invokes the audio callback");
    Check(snapshot.provided_frames != 0U,
        "the callback supplies frame-aligned project-owned silence");
    Check(snapshot.callback_failures == 0U,
        "the callback reports no stream submission failure");

    if (failures == 0)
        std::cout << "omega_sdl_audio_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
