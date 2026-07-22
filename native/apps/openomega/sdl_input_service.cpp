#include "sdl_input_service.h"

#include "sdl_platform_service.h"

#include "omega/runtime/input_tracker.h"
#include "omega/runtime/log_service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
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

[[nodiscard]] std::optional<runtime::PointerPositionQ16> TranslatePointerPosition(
    const SDL_WindowID window_id, const float x, const float y) noexcept
{
    if (!std::isfinite(x) || !std::isfinite(y))
        return std::nullopt;

    SDL_Window* window = SDL_GetWindowFromID(window_id);
    if (window == nullptr)
        return std::nullopt;

    int logical_width = 0;
    int logical_height = 0;
    if (!SDL_GetWindowSize(window, &logical_width, &logical_height) ||
        logical_width <= 0 || logical_height <= 0)
    {
        return std::nullopt;
    }

    const auto normalize_axis = [](const float coordinate,
                                    const int logical_extent) noexcept {
        const double clamped = std::clamp(
            static_cast<double>(coordinate), 0.0,
            static_cast<double>(logical_extent));
        return static_cast<std::uint32_t>(std::llround(
            clamped * static_cast<double>(runtime::kNormalizedInputExtent) /
            static_cast<double>(logical_extent)));
    };
    return runtime::PointerPositionQ16{
        .x = normalize_axis(x, logical_width),
        .y = normalize_axis(y, logical_height),
    };
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
        // SDL_GetGamepads requires SDL_free on its result. The log calls below build strings
        // (std::to_string/operator+) that can throw std::bad_alloc; without an RAII owner, an
        // exception there would skip the unconditional SDL_free that used to sit at the end of
        // this function and leak the array.
        const std::unique_ptr<SDL_JoystickID[], decltype(&SDL_free)> gamepad_ids_owner(
            gamepad_ids, &SDL_free);

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
    }

    bool subsystem_initialized = false;
    bool gamepad_enabled = false;
    SDL_Gamepad* gamepad = nullptr;
    SDL_JoystickID gamepad_id = 0;
};

std::expected<SdlInputService, std::string> SdlInputService::Create(
    const SdlPlatformService& platform, const bool gamepad_enabled)
{
    if (!platform.ready())
        return std::unexpected("SDL platform service is not ready");

    auto impl = std::make_unique<Impl>();
    impl->gamepad_enabled = gamepad_enabled;
    if (!gamepad_enabled)
        return SdlInputService(std::move(impl));

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
        if ((event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) ||
            event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            result.keyboard_or_mouse_pressed = true;
        }

        if (event.type == SDL_EVENT_QUIT ||
            event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
        {
            result.quit_requested = true;
        }
        else if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST)
        {
            input.ResetAllControls();
            input.ClearPointerPosition();
        }
        else if (impl_->gamepad_enabled &&
                 event.type == SDL_EVENT_GAMEPAD_ADDED &&
                 impl_->gamepad == nullptr)
        {
            impl_->OpenFirstAvailable(&log);
        }
        else if (impl_->gamepad_enabled &&
                 event.type == SDL_EVENT_GAMEPAD_REMOVED && impl_->gamepad != nullptr &&
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

        std::optional<runtime::PointerPositionQ16> pointer_position;
        if (event.type == SDL_EVENT_MOUSE_MOTION)
        {
            pointer_position = TranslatePointerPosition(
                event.motion.windowID, event.motion.x, event.motion.y);
        }
        else if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            pointer_position = TranslatePointerPosition(
                event.button.windowID, event.button.x, event.button.y);
        }
        if (pointer_position)
        {
            const auto sampled = input.SetPointerPosition(*pointer_position);
            (void)sampled;
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
