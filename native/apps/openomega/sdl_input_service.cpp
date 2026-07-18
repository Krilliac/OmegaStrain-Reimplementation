#include "sdl_input_service.h"

#include "sdl_platform_service.h"

#include "omega/runtime/input_tracker.h"
#include "omega/runtime/log_service.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace omega::app
{
namespace
{
[[nodiscard]] std::string SdlError(const std::string_view operation)
{
    const char* detail = SDL_GetError();
    return std::string(operation) + ": " +
           (detail != nullptr && detail[0] != '\0' ? detail : "unknown SDL error");
}

[[nodiscard]] std::optional<runtime::InputEvent> TranslateInputEvent(
    const SDL_Event& event, const SDL_JoystickID primary_gamepad_id) noexcept
{
    switch (event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        if (event.key.repeat)
            return std::nullopt;
        return runtime::InputEvent{
            .device = runtime::InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(event.key.scancode),
            .pressed = event.type == SDL_EVENT_KEY_DOWN,
        };
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return runtime::InputEvent{
            .device = runtime::InputDevice::MouseButton,
            .code = event.button.button,
            .pressed = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN,
        };
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        if (primary_gamepad_id == 0 || event.gbutton.which != primary_gamepad_id)
            return std::nullopt;
        return runtime::InputEvent{
            .device = runtime::InputDevice::GamepadButton,
            .code = event.gbutton.button,
            .pressed = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN,
        };
    default:
        return std::nullopt;
    }
}
} // namespace

struct SdlInputService::Impl
{
    ~Impl()
    {
        if (gamepad != nullptr)
            SDL_CloseGamepad(gamepad);
        if (subsystem_initialized)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }

    void OpenFirstAvailable(runtime::LogService* log)
    {
        if (gamepad != nullptr)
            return;

        int count = 0;
        SDL_JoystickID* gamepad_ids = SDL_GetGamepads(&count);
        if (gamepad_ids == nullptr)
        {
            if (log != nullptr)
                log->Warning("input", SdlError("SDL_GetGamepads"));
            return;
        }

        for (int index = 0; index < count; ++index)
        {
            SDL_Gamepad* opened = SDL_OpenGamepad(gamepad_ids[index]);
            if (opened == nullptr)
            {
                if (log != nullptr)
                {
                    log->Warning("input",
                        SdlError("SDL_OpenGamepad(" +
                                 std::to_string(gamepad_ids[index]) + ")"));
                }
                continue;
            }

            gamepad = opened;
            gamepad_id = gamepad_ids[index];
            if (log != nullptr)
            {
                log->Info("input",
                    "opened primary SDL gamepad " + std::to_string(gamepad_id));
            }
            break;
        }

        SDL_free(gamepad_ids);
    }

    bool subsystem_initialized = false;
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID gamepad_id = 0;
};

std::expected<SdlInputService, std::string> SdlInputService::Create(
    const SdlPlatformService& platform)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");

    auto impl = std::make_unique<Impl>();
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        return std::unexpected(SdlError("SDL_InitSubSystem(gamepad)"));
    impl->subsystem_initialized = true;

    // A missing or temporarily unopenable controller does not prevent keyboard/mouse startup.
    // PumpEvents retries deterministic enumeration when SDL reports a device arrival.
    impl->OpenFirstAvailable(nullptr);
    return SdlInputService(std::move(impl));
}

SdlInputService::SdlInputService(std::unique_ptr<Impl> impl) noexcept
    : impl_(std::move(impl))
{
}

SdlInputService::~SdlInputService() = default;
SdlInputService::SdlInputService(SdlInputService&&) noexcept = default;

InputPumpResult SdlInputService::PumpEvents(
    runtime::InputTracker& input, runtime::LogService& log)
{
    InputPumpResult result;
    SDL_Event event{};
    while (SDL_PollEvent(&event))
    {
        if (event.type == SDL_EVENT_QUIT)
        {
            result.quit_requested = true;
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            input.ResetAllControls();
        }
        else if (event.type == SDL_EVENT_GAMEPAD_ADDED && impl_->gamepad == nullptr)
        {
            impl_->OpenFirstAvailable(&log);
        }
        else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && impl_->gamepad != nullptr &&
                 event.gdevice.which == impl_->gamepad_id)
        {
            const SDL_JoystickID removed_id = impl_->gamepad_id;
            SDL_CloseGamepad(impl_->gamepad);
            impl_->gamepad = nullptr;
            impl_->gamepad_id = 0;
            input.ResetDevice(runtime::InputDevice::GamepadButton);
            log.Info("input",
                "closed removed primary SDL gamepad " + std::to_string(removed_id));
            impl_->OpenFirstAvailable(&log);
        }

        if (const auto translated = TranslateInputEvent(event, impl_->gamepad_id))
        {
            const auto accepted = input.PushEvent(*translated);
            (void)accepted;
        }
    }
    return result;
}
} // namespace omega::app
