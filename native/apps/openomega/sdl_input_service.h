#pragma once

#include <expected>
#include <memory>
#include <string>

namespace omega::runtime
{
class InputTracker;
class LogService;
}

namespace omega::app
{
class SdlPlatformService;

struct InputPumpResult
{
    bool quit_requested = false;
    // True only when this pump consumed a fresh physical keyboard or mouse
    // button press. Releases, key-repeat events, pointer motion, and gamepad
    // events never set this app-layer modal-input signal.
    bool keyboard_or_mouse_pressed = false;
};

// Non-hot-reloadable main-thread SDL event owner with opt-in primary-gamepad
// support. This leaf borrows the process-global platform lifetime, which must
// outlive it.
class SdlInputService final
{
public:
    // [main thread, startup] Keyboard/mouse-only construction is the default and
    // does not initialize SDL's gamepad subsystem. When explicitly enabled, the
    // service initializes that subsystem and selects the first openable attached
    // gamepad. Having no attached or openable gamepad is not a startup failure.
    [[nodiscard]] static std::expected<SdlInputService, std::string> Create(
        const SdlPlatformService& platform, bool gamepad_enabled = false);

    // [main thread, before the platform service is destroyed]
    ~SdlInputService();
    SdlInputService(SdlInputService&&) noexcept;
    SdlInputService& operator=(SdlInputService&&) noexcept = delete;
    SdlInputService(const SdlInputService&) = delete;
    SdlInputService& operator=(const SdlInputService&) = delete;

    // [main thread] Owns the SDL event queue, translating neutral controls into the borrowed
    // tracker. OmegaApp owns frame closure and quit-action policy.
    [[nodiscard]] InputPumpResult PumpEvents(
        runtime::InputTracker& input, runtime::LogService& log);

private:
    struct Impl;
    explicit SdlInputService(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};
} // namespace omega::app
