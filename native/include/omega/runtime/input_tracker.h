#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace omega::runtime
{
class RunCaptureReplaySession;

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

// One normalized absolute pointer sample. Zero names the left/top edge and the inclusive extent
// names the right/bottom edge, independent of the host window's pixel dimensions or DPI scale.
struct PointerPositionQ16
{
    std::uint32_t x = 0U;
    std::uint32_t y = 0U;

    friend constexpr bool operator==(
        const PointerPositionQ16&, const PointerPositionQ16&) noexcept = default;
};

inline constexpr std::uint32_t kNormalizedInputExtent = 65'536U;

static_assert(std::is_trivially_copyable_v<PointerPositionQ16>);
static_assert(std::is_standard_layout_v<PointerPositionQ16>);

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

// Owned immutable per-frame action state. A tracker produces this by value, and replay can
// reconstruct the same logical state; a snapshot never observes later frames.
class InputSnapshot
{
public:
    // A default snapshot represents "no frame produced yet": no actions, every query false.
    InputSnapshot() = default;

    // [any thread; reentrant] Logical frame index. A fresh tracker's first EndFrame is zero;
    // replay preserves the captured origin.
    [[nodiscard]] std::uint64_t frame_index() const noexcept;

    // [any thread; reentrant] Registered action identifiers, ascending and unique.
    [[nodiscard]] std::span<const std::uint32_t> actions() const noexcept;

    // [any thread; reentrant] Latest normalized pointer position, or unavailable when the host has
    // not supplied one (or explicitly cleared it). The owned value persists across frames.
    [[nodiscard]] const std::optional<PointerPositionQ16>& pointer_position() const noexcept;

    // [any thread; reentrant] True when any bound control was down at EndFrame. Unregistered
    // action identifiers report false.
    [[nodiscard]] bool IsHeld(std::uint32_t action) const noexcept;

    // [any thread; reentrant] True when the action gained its first down control this frame.
    // Unregistered action identifiers report false.
    [[nodiscard]] bool WasPressed(std::uint32_t action) const noexcept;

    // [any thread; reentrant] True when the action lost its last down control this frame.
    // Unregistered action identifiers report false.
    [[nodiscard]] bool WasReleased(std::uint32_t action) const noexcept;

    // [any thread; reentrant] Digital events accepted into this frame, including unbound controls.
    [[nodiscard]] std::uint32_t accepted_event_count() const noexcept;

    // [any thread; reentrant] Digital events rejected during this frame (budget overruns and
    // malformed device values). Rejections never mutate action or pointer state.
    [[nodiscard]] std::uint32_t rejected_event_count() const noexcept;

private:
    friend class RunCaptureReplaySession;
    friend class InputTracker;

    struct ActionRow
    {
        std::uint32_t action = 0;
        bool held = false;
        bool pressed = false;
        bool released = false;
    };

    InputSnapshot(std::uint64_t frame_index,
        std::span<const std::uint32_t> actions,
        std::span<const ActionRow> rows,
        std::optional<PointerPositionQ16> pointer_position,
        std::uint32_t accepted_event_count,
        std::uint32_t rejected_event_count);

    std::uint64_t frame_index_ = 0;
    std::vector<std::uint32_t> actions_;
    std::vector<ActionRow> rows_;
    std::optional<PointerPositionQ16> pointer_position_;
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

    // [main thread] Replaces the latest normalized absolute pointer sample. Either coordinate
    // above kNormalizedInputExtent rejects the complete sample without changing pointer state or
    // digital accepted/rejected event counters. This API is independent of the digital budget.
    [[nodiscard]] std::expected<void, std::string> SetPointerPosition(
        PointerPositionQ16 position);

    // [main thread] Makes the pointer unavailable without changing digital event counters.
    void ClearPointerPosition() noexcept;

    // [main thread] Reconciles only controls in one physical-device class to up. This is the
    // disconnect path for a host-owned device and preserves held controls from every other class.
    // Invalid device enumerators are ignored. Like ResetAllControls, this bypasses event budgets,
    // preserves earlier press edges, and emits a release only when an action loses its final
    // down control.
    void ResetDevice(InputDevice device) noexcept;

    // [main thread] Atomically reconciles every tracked control to the up state after focus
    // loss, device reset, or another host event that invalidates level state. This operation
    // bypasses the per-frame event budget and does not change accepted/rejected event counts.
    // Each action that had at least one down control receives one sticky release edge, even
    // when several of its controls were down; actions already up receive no edge. Any press
    // edge produced earlier in the frame remains sticky, and repeated resets are no-ops. Pointer
    // availability is independent and must be cleared explicitly with ClearPointerPosition().
    void ResetAllControls() noexcept;

    // [main/game thread] Exact index the next successful EndFrame() will assign.
    [[nodiscard]] std::uint64_t next_frame_index() const noexcept;

    // [main thread] Closes the frame and returns the owned immutable snapshot for the game
    // thread. Held reflects whether any bound control is down at this call; pressed/released
    // edges are sticky within the frame, so a press+release inside one frame reports both edges.
    // Resets per-frame edges and event counters; held controls and pointer position carry forward.
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
    std::optional<PointerPositionQ16> pointer_position_;
    std::uint64_t next_frame_index_ = 0;
    std::uint32_t frame_accepted_events_ = 0;
    std::uint32_t frame_rejected_events_ = 0;
    std::uint64_t total_rejected_events_ = 0;
};
} // namespace omega::runtime
