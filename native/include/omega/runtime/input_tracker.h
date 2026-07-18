#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <vector>

namespace omega::runtime
{
// Neutral physical-device classes. Codes are opaque host-layer scancodes/button ordinals; the
// runtime never interprets them. SDL (or any other host) translates its events into this model
// before they cross into runtime code.
enum class InputDevice : std::uint8_t
{
    Keyboard = 0,
    MouseButton = 1,
    GamepadButton = 2,
};

// One physical control -> logical action association. A (device, code) pair may appear at most
// once per table; one action may be reached from many physical controls.
struct InputBinding
{
    InputDevice device = InputDevice::Keyboard;
    std::uint16_t code = 0;
    std::uint32_t action = 0;
};

// One neutral level-state event from the host: pressed=true means the control went (or is) down,
// pressed=false means it went (or is) up. Duplicate level reports are tolerated deterministically.
struct InputEvent
{
    InputDevice device = InputDevice::Keyboard;
    std::uint16_t code = 0;
    bool pressed = false;
};

// Validated, immutable, bounded registry of physical-control-to-action bindings.
class InputBindingTable
{
public:
    // Synthetic-shell budgets, not retail claims: generous for a shooter control scheme while
    // keeping every per-frame scan trivially bounded.
    static constexpr std::size_t kMaxBindings = 256U;
    static constexpr std::size_t kMaxActions = 64U;

    // [any thread; reentrant] Builds a validated table from host-supplied bindings. Rejects an
    // empty set, budget overruns, invalid device enumerators, and duplicate (device, code) pairs
    // (even when they name the same action). Multiple bindings per action are allowed.
    [[nodiscard]] static std::expected<InputBindingTable, std::string> FromBindings(
        std::span<const InputBinding> bindings);

    // [any thread; reentrant] Bindings sorted by (device, code); stable for identical input.
    [[nodiscard]] std::span<const InputBinding> bindings() const noexcept;

    // [any thread; reentrant] Registered action identifiers, ascending and unique.
    [[nodiscard]] std::span<const std::uint32_t> actions() const noexcept;

    // [any thread; reentrant] True when the action identifier is registered.
    [[nodiscard]] bool HasAction(std::uint32_t action) const noexcept;

private:
    InputBindingTable() = default;

    std::vector<InputBinding> bindings_;
    std::vector<std::uint32_t> actions_;
};

// Owned immutable per-frame action state. The tracker hands this to the game thread by value per
// the architecture doc's immutable-packet rule; a snapshot never observes later frames.
class InputSnapshot
{
public:
    // A default snapshot represents "no frame produced yet": no actions, every query false.
    InputSnapshot() = default;

    // [any thread; reentrant] Index of the frame this snapshot closed (first EndFrame is 0).
    [[nodiscard]] std::uint64_t frame_index() const noexcept;

    // [any thread; reentrant] Registered action identifiers, ascending and unique.
    [[nodiscard]] std::span<const std::uint32_t> actions() const noexcept;

    // [any thread; reentrant] True when any bound control was down at EndFrame. Unregistered
    // action identifiers report false.
    [[nodiscard]] bool IsHeld(std::uint32_t action) const noexcept;

    // [any thread; reentrant] True when the action gained its first down control this frame.
    // Unregistered action identifiers report false.
    [[nodiscard]] bool WasPressed(std::uint32_t action) const noexcept;

    // [any thread; reentrant] True when the action lost its last down control this frame.
    // Unregistered action identifiers report false.
    [[nodiscard]] bool WasReleased(std::uint32_t action) const noexcept;

    // [any thread; reentrant] Events accepted into this frame, including unbound-control events.
    [[nodiscard]] std::uint32_t accepted_event_count() const noexcept;

    // [any thread; reentrant] Events rejected during this frame (budget overruns and malformed
    // device values). Rejections never mutate action state.
    [[nodiscard]] std::uint32_t rejected_event_count() const noexcept;

private:
    friend class InputTracker;

    struct ActionRow
    {
        std::uint32_t action = 0;
        bool held = false;
        bool pressed = false;
        bool released = false;
    };

    std::uint64_t frame_index_ = 0;
    std::vector<std::uint32_t> actions_;
    std::vector<ActionRow> rows_;
    std::uint32_t accepted_event_count_ = 0;
    std::uint32_t rejected_event_count_ = 0;
};

// Platform-neutral edge-detection core for the future InputService. Deterministic: an identical
// event/EndFrame sequence always produces identical snapshots (replay-friendly for M4).
class InputTracker
{
public:
    // Synthetic-shell ceiling on the configurable per-frame event budget, not a retail claim.
    static constexpr std::size_t kMaxEventsPerFrameLimit = 4096U;

    // [any thread; reentrant] Takes ownership of the validated table. The per-frame event budget
    // must be in [1, kMaxEventsPerFrameLimit].
    [[nodiscard]] static std::expected<InputTracker, std::string> Create(
        InputBindingTable table, std::size_t max_events_per_frame);

    // [main thread] Consumes one host event. Failure means the event was rejected and counted
    // (frame budget exhausted or invalid device enumerator) and no state changed. An event whose
    // (device, code) matches no binding is accepted, debits the budget, and affects no action.
    // Redundant level reports (press while down, release while up) are accepted no-ops.
    [[nodiscard]] std::expected<void, std::string> PushEvent(InputEvent event);

    // [main thread] Closes the frame and returns the owned immutable snapshot for the game
    // thread. Held reflects whether any bound control is down at this call; pressed/released
    // edges are sticky within the frame, so a press+release inside one frame reports both edges.
    // Resets per-frame edges and event counters; held control state carries forward.
    [[nodiscard]] InputSnapshot EndFrame();

    // [main thread] The validated table this tracker was built from.
    [[nodiscard]] const InputBindingTable& bindings() const noexcept;

    // [main thread] Configured per-frame event budget.
    [[nodiscard]] std::size_t max_events_per_frame() const noexcept;

    // [main thread] Events rejected since construction, across all frames.
    [[nodiscard]] std::uint64_t total_rejected_event_count() const noexcept;

private:
    InputTracker(InputBindingTable table, std::size_t max_events_per_frame);

    struct ActionState
    {
        std::uint32_t action = 0;
        std::uint16_t down_count = 0;
        bool pressed_edge = false;
        bool released_edge = false;
    };

    InputBindingTable table_;
    std::size_t max_events_per_frame_ = 0;
    std::vector<std::uint8_t> control_down_;
    std::vector<std::size_t> control_action_index_;
    std::vector<ActionState> action_states_;
    std::uint64_t next_frame_index_ = 0;
    std::uint32_t frame_accepted_events_ = 0;
    std::uint32_t frame_rejected_events_ = 0;
    std::uint64_t total_rejected_events_ = 0;
};
} // namespace omega::runtime
