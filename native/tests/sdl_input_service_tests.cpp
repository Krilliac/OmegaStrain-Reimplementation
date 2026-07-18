#include "sdl_input_service.h"
#include "sdl_platform_service.h"

#include "omega/runtime/input_tracker.h"
#include "omega/runtime/log_service.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <type_traits>
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

constexpr std::uint32_t kKeyboardAction = 10U;
constexpr std::uint32_t kMouseAction = 20U;
constexpr std::uint32_t kGamepadAction = 30U;
constexpr std::uint16_t kKeyboardCode = static_cast<std::uint16_t>(SDL_SCANCODE_A);
constexpr std::uint16_t kMouseCode = static_cast<std::uint16_t>(SDL_BUTTON_LEFT);
constexpr std::uint16_t kGamepadCode =
    static_cast<std::uint16_t>(SDL_GAMEPAD_BUTTON_SOUTH);

[[nodiscard]] SDL_JoystickID AttachVirtualGamepad(const char* name)
{
    SDL_VirtualJoystickDesc descriptor;
    SDL_INIT_INTERFACE(&descriptor);
    descriptor.type = static_cast<Uint16>(SDL_JOYSTICK_TYPE_GAMEPAD);
    descriptor.vendor_id = static_cast<Uint16>(0xF00DU);
    descriptor.product_id = static_cast<Uint16>(0x0001U);
    descriptor.naxes = static_cast<Uint16>(SDL_GAMEPAD_AXIS_COUNT);
    descriptor.nbuttons = static_cast<Uint16>(SDL_GAMEPAD_BUTTON_COUNT);
    descriptor.name = name;
    return SDL_AttachVirtualJoystick(&descriptor);
}

[[nodiscard]] bool PushEvent(SDL_Event event)
{
    return SDL_PushEvent(&event);
}

[[nodiscard]] bool PushKey(const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.key.scancode = SDL_SCANCODE_A;
    event.key.down = down;
    return PushEvent(event);
}

[[nodiscard]] bool PushMouseButton(const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    return PushEvent(event);
}

[[nodiscard]] bool PushGamepadButton(
    const SDL_JoystickID id, const bool down)
{
    SDL_Event event{};
    event.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    event.gbutton.which = id;
    event.gbutton.button = static_cast<Uint8>(SDL_GAMEPAD_BUTTON_SOUTH);
    event.gbutton.down = down;
    return PushEvent(event);
}

[[nodiscard]] bool SetVirtualButton(const SDL_JoystickID id, const bool down)
{
    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(id);
    if (gamepad == nullptr)
        return false;
    SDL_Joystick* joystick = SDL_GetGamepadJoystick(gamepad);
    return joystick != nullptr &&
           SDL_SetJoystickVirtualButton(joystick, SDL_GAMEPAD_BUTTON_SOUTH, down);
}

[[nodiscard]] bool HasLogMessage(const omega::runtime::RingLogSink& sink,
    const std::string_view fragment)
{
    const auto records = sink.Snapshot();
    return std::ranges::any_of(records,
        [fragment](const omega::runtime::LogRecord& record) {
            return record.message.find(fragment) != std::string::npos;
        });
}
} // namespace

int main()
{
    using omega::app::SdlInputService;
    using omega::runtime::InputBinding;
    using omega::runtime::InputBindingTable;
    using omega::runtime::InputDevice;
    using omega::runtime::InputTracker;
    using omega::runtime::LogService;
    using omega::runtime::LogServiceConfig;
    using omega::runtime::RingLogSink;

    static_assert(std::is_move_constructible_v<SdlInputService>);
    static_assert(!std::is_move_assignable_v<SdlInputService>);
    static_assert(!std::is_copy_constructible_v<SdlInputService>);

    Check(SDL_SetHintWithPriority(
              SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1", SDL_HINT_OVERRIDE),
        "background gamepad events are enabled for the headless test");
    Check(SDL_SetHintWithPriority(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT,
              "0xF00D/0x0001", SDL_HINT_OVERRIDE),
        "the test excludes non-fixture gamepads");

    auto platform = omega::app::SdlPlatformService::Create();
    Check(platform.has_value(), "the SDL process-global runtime initializes");
    if (!platform)
    {
        std::cerr << platform.error() << '\n';
        return 1;
    }

    Check(SDL_InitSubSystem(SDL_INIT_GAMEPAD),
        "the test fixture holds a gamepad-subsystem reference");

    {
        auto no_controller = SdlInputService::Create(*platform);
        Check(no_controller.has_value(), "no controller is not an input startup failure");
    }

    const SDL_JoystickID first_attached = AttachVirtualGamepad("Omega virtual gamepad A");
    const SDL_JoystickID second_attached = AttachVirtualGamepad("Omega virtual gamepad B");
    Check(first_attached != 0 && second_attached != 0 && first_attached != second_attached,
        "two distinct virtual gamepads attach");
    SDL_UpdateJoysticks();

    int gamepad_count = 0;
    SDL_JoystickID* gamepad_ids = SDL_GetGamepads(&gamepad_count);
    Check(gamepad_ids != nullptr && gamepad_count == 2,
        "the fixture exposes exactly two gamepads");
    if (gamepad_ids == nullptr || gamepad_count != 2)
    {
        SDL_free(gamepad_ids);
        if (first_attached != 0)
            (void)SDL_DetachVirtualJoystick(first_attached);
        if (second_attached != 0)
            (void)SDL_DetachVirtualJoystick(second_attached);
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        return 1;
    }
    const SDL_JoystickID primary_id = gamepad_ids[0];
    const SDL_JoystickID secondary_id = gamepad_ids[1];
    SDL_free(gamepad_ids);
    SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

    auto ring = RingLogSink::Create(32U);
    Check(ring.has_value(), "the input test log ring initializes");
    if (!ring)
        return 1;
    LogServiceConfig log_config;
    log_config.sinks.push_back(ring->get());
    auto created_log = LogService::Create(std::move(log_config));
    Check(created_log.has_value(), "the input test log service initializes");
    if (!created_log)
        return 1;
    LogService log = std::move(*created_log);

    constexpr std::array bindings = {
        InputBinding{.device = InputDevice::Keyboard,
            .code = kKeyboardCode,
            .action = kKeyboardAction},
        InputBinding{.device = InputDevice::MouseButton,
            .code = kMouseCode,
            .action = kMouseAction},
        InputBinding{.device = InputDevice::GamepadButton,
            .code = kGamepadCode,
            .action = kGamepadAction},
    };
    auto table = InputBindingTable::FromBindings(bindings);
    Check(table.has_value(), "the input test binding table initializes");
    if (!table)
        return 1;
    auto created_tracker = InputTracker::Create(std::move(*table), 64U);
    Check(created_tracker.has_value(), "the input test tracker initializes");
    if (!created_tracker)
        return 1;
    InputTracker tracker = std::move(*created_tracker);

    {
        auto created_input = SdlInputService::Create(*platform);
        Check(created_input.has_value(), "the SDL input service initializes");
        if (!created_input)
        {
            std::cerr << created_input.error() << '\n';
            return 1;
        }
        SdlInputService input = std::move(*created_input);

        Check(SDL_GetGamepadFromID(primary_id) != nullptr,
            "Create selects the first enumerated preattached gamepad");
        Check(SDL_GetGamepadFromID(secondary_id) == nullptr,
            "Create owns only one primary gamepad");

        Check(SetVirtualButton(primary_id, true), "the primary virtual button presses");
        SDL_UpdateJoysticks();
        (void)input.PumpEvents(tracker, log);
        const auto primary_press = tracker.EndFrame();
        Check(primary_press.IsHeld(kGamepadAction) &&
                  primary_press.WasPressed(kGamepadAction) &&
                  !primary_press.WasReleased(kGamepadAction),
            "primary gamepad button-down reaches the tracker");

        Check(SetVirtualButton(primary_id, false), "the primary virtual button releases");
        SDL_UpdateJoysticks();
        (void)input.PumpEvents(tracker, log);
        const auto primary_release = tracker.EndFrame();
        Check(!primary_release.IsHeld(kGamepadAction) &&
                  primary_release.WasReleased(kGamepadAction),
            "primary gamepad button-up reaches the tracker");

        Check(PushGamepadButton(secondary_id, true),
            "a synthetic secondary button event enters SDL's queue");
        (void)input.PumpEvents(tracker, log);
        const auto ignored_secondary = tracker.EndFrame();
        Check(!ignored_secondary.IsHeld(kGamepadAction) &&
                  !ignored_secondary.WasPressed(kGamepadAction) &&
                  ignored_secondary.accepted_event_count() == 0U,
            "button events from the unopened secondary gamepad are ignored");

        Check(SetVirtualButton(primary_id, true) && PushKey(true) && PushMouseButton(true),
            "primary, keyboard, and mouse controls press before detach");
        SDL_UpdateJoysticks();
        (void)input.PumpEvents(tracker, log);
        const auto three_devices_held = tracker.EndFrame();
        Check(three_devices_held.IsHeld(kGamepadAction) &&
                  three_devices_held.IsHeld(kKeyboardAction) &&
                  three_devices_held.IsHeld(kMouseAction),
            "all three device classes are held before primary detach");

        Check(SDL_DetachVirtualJoystick(primary_id), "the primary virtual gamepad detaches");
        SDL_UpdateJoysticks();
        (void)input.PumpEvents(tracker, log);
        const auto detached = tracker.EndFrame();
        Check(!detached.IsHeld(kGamepadAction) && detached.WasReleased(kGamepadAction),
            "primary detach reconciles the gamepad control exactly once");
        Check(detached.IsHeld(kKeyboardAction) && !detached.WasReleased(kKeyboardAction) &&
                  detached.IsHeld(kMouseAction) && !detached.WasReleased(kMouseAction),
            "primary detach preserves keyboard and mouse controls");
        Check(SDL_GetGamepadFromID(secondary_id) != nullptr,
            "primary removal promotes the next enumerated gamepad");

        (void)input.PumpEvents(tracker, log);
        const auto post_detach = tracker.EndFrame();
        Check(!post_detach.WasReleased(kGamepadAction),
            "the frame after detach has no duplicate gamepad release");

        Check(PushGamepadButton(primary_id, false) && PushGamepadButton(primary_id, true),
            "stale events for the removed primary enter SDL's queue");
        (void)input.PumpEvents(tracker, log);
        const auto stale_primary = tracker.EndFrame();
        Check(!stale_primary.IsHeld(kGamepadAction) &&
                  !stale_primary.WasPressed(kGamepadAction) &&
                  !stale_primary.WasReleased(kGamepadAction) &&
                  stale_primary.accepted_event_count() == 0U,
            "stale old-ID events are ignored and cannot duplicate the detach release");
        Check(stale_primary.IsHeld(kKeyboardAction) && stale_primary.IsHeld(kMouseAction),
            "stale gamepad events preserve other device classes");

        Check(SetVirtualButton(secondary_id, true),
            "the promoted secondary virtual button presses");
        SDL_UpdateJoysticks();
        (void)input.PumpEvents(tracker, log);
        const auto promoted_press = tracker.EndFrame();
        Check(promoted_press.IsHeld(kGamepadAction) &&
                  promoted_press.WasPressed(kGamepadAction),
            "the promoted secondary becomes the accepted primary");

        SDL_Event focus_lost{};
        focus_lost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        Check(PushEvent(focus_lost), "focus loss enters SDL's queue");
        (void)input.PumpEvents(tracker, log);
        const auto focus_reset = tracker.EndFrame();
        Check(!focus_reset.IsHeld(kKeyboardAction) &&
                  focus_reset.WasReleased(kKeyboardAction) &&
                  !focus_reset.IsHeld(kMouseAction) && focus_reset.WasReleased(kMouseAction) &&
                  !focus_reset.IsHeld(kGamepadAction) &&
                  focus_reset.WasReleased(kGamepadAction),
            "focus loss atomically resets every control class");

        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        Check(PushEvent(quit), "quit enters SDL's queue");
        Check(input.PumpEvents(tracker, log).quit_requested,
            "PumpEvents reports a queued quit request");
        (void)tracker.EndFrame();

        Check(HasLogMessage(**ring, "closed removed primary SDL gamepad"),
            "primary removal is logged through the borrowed log service");
        Check(HasLogMessage(**ring, "opened primary SDL gamepad"),
            "event-driven secondary promotion is logged");
    }

    Check(SDL_GetGamepadFromID(secondary_id) == nullptr,
        "the input-service destructor closes its gamepad before platform teardown");
    Check(SDL_DetachVirtualJoystick(secondary_id),
        "the remaining virtual gamepad detaches after service teardown");
    SDL_UpdateJoysticks();
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    SDL_ResetHint(SDL_HINT_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT);
    SDL_ResetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS);

    if (failures == 0)
        std::cout << "omega_sdl_input_tests: all checks passed\n";
    return failures == 0 ? 0 : 1;
}
