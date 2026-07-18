#include "omega/runtime/input_tracker.h"

#include <cstdint>
#include <iostream>
#include <string_view>
#include <vector>

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

using omega::runtime::InputBinding;
using omega::runtime::InputBindingTable;
using omega::runtime::InputDevice;
using omega::runtime::InputEvent;
using omega::runtime::InputSnapshot;
using omega::runtime::InputTracker;

constexpr std::uint32_t kFire = 10U;
constexpr std::uint32_t kJump = 20U;
constexpr std::uint32_t kCrouch = 30U;

// Fire is reachable from a keyboard key AND a gamepad button (multi-binding OR semantics);
// the keyboard code 7 deliberately collides with a gamepad code to prove device separation.
[[nodiscard]] std::vector<InputBinding> StandardBindings()
{
    return {
        InputBinding{.device = InputDevice::Keyboard, .code = 7U, .action = kFire},
        InputBinding{.device = InputDevice::GamepadButton, .code = 7U, .action = kFire},
        InputBinding{.device = InputDevice::Keyboard, .code = 44U, .action = kJump},
        InputBinding{.device = InputDevice::MouseButton, .code = 1U, .action = kCrouch},
    };
}

[[nodiscard]] InputTracker MakeTracker(const std::size_t budget)
{
    auto table = InputBindingTable::FromBindings(StandardBindings());
    Check(table.has_value(), "standard binding table builds");
    auto tracker = InputTracker::Create(std::move(*table), budget);
    Check(tracker.has_value(), "standard tracker builds");
    return std::move(*tracker);
}

[[nodiscard]] bool Push(InputTracker& tracker, const InputDevice device,
    const std::uint16_t code, const bool pressed)
{
    return tracker
        .PushEvent(InputEvent{.device = device, .code = code, .pressed = pressed})
        .has_value();
}

void TableValidationChecks()
{
    auto table = InputBindingTable::FromBindings(StandardBindings());
    Check(table.has_value(), "valid binding table is accepted");
    if (table)
    {
        Check(table->bindings().size() == 4U, "table preserves every binding");
        Check(table->actions().size() == 3U, "table registers each action once");
        Check(table->actions()[0] == kFire && table->actions()[1] == kJump &&
                  table->actions()[2] == kCrouch,
            "registered actions are ascending and unique");
        Check(table->HasAction(kJump) && !table->HasAction(999U),
            "action lookup distinguishes registered from unknown identifiers");
    }

    Check(!InputBindingTable::FromBindings({}), "an empty binding set is rejected");

    const std::vector<InputBinding> duplicate = {
        InputBinding{.device = InputDevice::Keyboard, .code = 7U, .action = kFire},
        InputBinding{.device = InputDevice::Keyboard, .code = 7U, .action = kJump},
    };
    Check(!InputBindingTable::FromBindings(duplicate),
        "duplicate (device, code) bindings are rejected even for distinct actions");

    const std::vector<InputBinding> bad_device = {
        InputBinding{.device = static_cast<InputDevice>(7), .code = 1U, .action = kFire},
    };
    Check(!InputBindingTable::FromBindings(bad_device),
        "bindings naming an unknown device enumerator are rejected");

    std::vector<InputBinding> at_budget;
    for (std::size_t index = 0; index < InputBindingTable::kMaxBindings; ++index)
        at_budget.push_back(InputBinding{.device = InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(index),
            .action = static_cast<std::uint32_t>(index % InputBindingTable::kMaxActions)});
    Check(InputBindingTable::FromBindings(at_budget).has_value(),
        "tables exactly at both budgets are accepted");

    std::vector<InputBinding> over_bindings = at_budget;
    over_bindings.push_back(InputBinding{
        .device = InputDevice::MouseButton, .code = 9999U, .action = kFire});
    Check(!InputBindingTable::FromBindings(over_bindings),
        "binding counts beyond the budget are rejected");

    std::vector<InputBinding> over_actions;
    for (std::size_t index = 0; index <= InputBindingTable::kMaxActions; ++index)
        over_actions.push_back(InputBinding{.device = InputDevice::Keyboard,
            .code = static_cast<std::uint16_t>(index),
            .action = static_cast<std::uint32_t>(index)});
    Check(!InputBindingTable::FromBindings(over_actions),
        "action counts beyond the budget are rejected");
}

void TrackerCreationChecks()
{
    auto table_zero = InputBindingTable::FromBindings(StandardBindings());
    Check(table_zero &&
              !InputTracker::Create(std::move(*table_zero), 0U).has_value(),
        "a zero per-frame event budget is rejected");
    auto table_over = InputBindingTable::FromBindings(StandardBindings());
    Check(table_over &&
              !InputTracker::Create(std::move(*table_over),
                  InputTracker::kMaxEventsPerFrameLimit + 1U)
                   .has_value(),
        "a per-frame event budget beyond the tracker limit is rejected");
}

void EdgeMatrixChecks()
{
    InputTracker tracker = MakeTracker(16U);

    Check(Push(tracker, InputDevice::Keyboard, 44U, true), "a bound press is accepted");
    const InputSnapshot press = tracker.EndFrame();
    Check(press.frame_index() == 0U, "the first frame closes with index zero");
    Check(press.IsHeld(kJump) && press.WasPressed(kJump) && !press.WasReleased(kJump),
        "a press reports held plus a press edge and no release edge");
    Check(!press.IsHeld(kFire) && !press.WasPressed(kFire) && !press.WasReleased(kFire),
        "untouched actions stay fully inactive");
    Check(press.accepted_event_count() == 1U && press.rejected_event_count() == 0U,
        "the frame counts exactly the accepted event");

    const InputSnapshot held = tracker.EndFrame();
    Check(held.frame_index() == 1U && held.IsHeld(kJump) && !held.WasPressed(kJump) &&
              !held.WasReleased(kJump),
        "a control held across frames reports held without repeated edges");

    for (int frame = 0; frame < 8; ++frame)
    {
        const InputSnapshot still_held = tracker.EndFrame();
        if (!still_held.IsHeld(kJump) || still_held.WasPressed(kJump) ||
            still_held.WasReleased(kJump))
        {
            Check(false, "held state stays edge-free across many empty frames");
            break;
        }
    }

    Check(Push(tracker, InputDevice::Keyboard, 44U, false), "a bound release is accepted");
    const InputSnapshot released = tracker.EndFrame();
    Check(!released.IsHeld(kJump) && !released.WasPressed(kJump) &&
              released.WasReleased(kJump),
        "a release reports a release edge and clears held");

    const InputSnapshot idle = tracker.EndFrame();
    Check(!idle.IsHeld(kJump) && !idle.WasPressed(kJump) && !idle.WasReleased(kJump),
        "the frame after a release is fully inactive");

    Check(Push(tracker, InputDevice::MouseButton, 1U, true) &&
              Push(tracker, InputDevice::MouseButton, 1U, false),
        "press plus release inside one frame are both accepted");
    const InputSnapshot tap = tracker.EndFrame();
    Check(tap.WasPressed(kCrouch) && tap.WasReleased(kCrouch) && !tap.IsHeld(kCrouch),
        "a same-frame tap reports both edges and a final up state");

    Check(Push(tracker, InputDevice::MouseButton, 1U, true) &&
              Push(tracker, InputDevice::MouseButton, 1U, false) &&
              Push(tracker, InputDevice::MouseButton, 1U, true),
        "press, release, and re-press inside one frame are accepted");
    const InputSnapshot retap = tracker.EndFrame();
    Check(retap.WasPressed(kCrouch) && retap.WasReleased(kCrouch) && retap.IsHeld(kCrouch),
        "a same-frame re-press keeps both edges and a final held state");
    Check(Push(tracker, InputDevice::MouseButton, 1U, false),
        "cleanup release is accepted");
    const InputSnapshot cleanup = tracker.EndFrame();
    Check(cleanup.WasReleased(kCrouch) && !cleanup.IsHeld(kCrouch),
        "the carried held state releases on the following frame");

    Check(Push(tracker, InputDevice::Keyboard, 44U, false),
        "a release without a preceding press is accepted, not an error");
    const InputSnapshot spurious = tracker.EndFrame();
    Check(!spurious.IsHeld(kJump) && !spurious.WasPressed(kJump) &&
              !spurious.WasReleased(kJump),
        "a spurious release produces no edges and no held state");

    Check(Push(tracker, InputDevice::Keyboard, 44U, true), "press before repeat accepted");
    (void)tracker.EndFrame();
    Check(Push(tracker, InputDevice::Keyboard, 44U, true),
        "a duplicate press while already down is accepted");
    const InputSnapshot repeat = tracker.EndFrame();
    Check(repeat.IsHeld(kJump) && !repeat.WasPressed(kJump) && !repeat.WasReleased(kJump),
        "a duplicate press while down produces no second press edge");
}

void MultiBindingChecks()
{
    InputTracker tracker = MakeTracker(16U);

    Check(Push(tracker, InputDevice::Keyboard, 7U, true) &&
              Push(tracker, InputDevice::GamepadButton, 7U, true),
        "both controls bound to one action are accepted");
    const InputSnapshot both = tracker.EndFrame();
    Check(both.IsHeld(kFire) && both.WasPressed(kFire) && !both.WasReleased(kFire),
        "two same-frame presses of one action report a single press edge");

    Check(Push(tracker, InputDevice::Keyboard, 7U, false),
        "releasing one of two down controls is accepted");
    const InputSnapshot partial = tracker.EndFrame();
    Check(partial.IsHeld(kFire) && !partial.WasPressed(kFire) && !partial.WasReleased(kFire),
        "an action stays held with no release edge while another control is down");

    Check(Push(tracker, InputDevice::Keyboard, 7U, true),
        "re-pressing while the sibling control is down is accepted");
    const InputSnapshot repress = tracker.EndFrame();
    Check(repress.IsHeld(kFire) && !repress.WasPressed(kFire),
        "a press while the action is already held produces no press edge");

    Check(Push(tracker, InputDevice::Keyboard, 7U, false) &&
              Push(tracker, InputDevice::GamepadButton, 7U, false),
        "releasing the remaining controls is accepted");
    const InputSnapshot empty = tracker.EndFrame();
    Check(!empty.IsHeld(kFire) && empty.WasReleased(kFire),
        "the release edge fires only when the last down control releases");
}

void BudgetAndRejectionChecks()
{
    InputTracker tracker = MakeTracker(3U);
    Check(tracker.max_events_per_frame() == 3U, "the configured budget is reported");

    Check(Push(tracker, InputDevice::Keyboard, 500U, true) &&
              Push(tracker, InputDevice::Keyboard, 501U, true) &&
              Push(tracker, InputDevice::Keyboard, 502U, true),
        "unbound events are accepted and debit the frame budget");
    Check(!Push(tracker, InputDevice::Keyboard, 44U, true),
        "the event over budget is rejected via the expected channel");
    const InputSnapshot over = tracker.EndFrame();
    Check(over.accepted_event_count() == 3U && over.rejected_event_count() == 1U,
        "the snapshot counts accepted and rejected events for its frame");
    Check(!over.IsHeld(kJump) && !over.WasPressed(kJump),
        "a rejected press never mutates action state");
    Check(tracker.total_rejected_event_count() == 1U,
        "the tracker accumulates rejections across the run");

    Check(Push(tracker, InputDevice::Keyboard, 44U, true),
        "the per-frame budget resets after EndFrame");
    Check(!tracker
               .PushEvent(InputEvent{.device = static_cast<InputDevice>(200),
                   .code = 1U,
                   .pressed = true})
               .has_value(),
        "an event naming an unknown device enumerator is rejected");
    const InputSnapshot next = tracker.EndFrame();
    Check(next.IsHeld(kJump) && next.WasPressed(kJump),
        "accepted events still land in the frame that rejected a malformed one");
    Check(next.accepted_event_count() == 1U && next.rejected_event_count() == 1U &&
              tracker.total_rejected_event_count() == 2U,
        "malformed-device rejections are counted, never silently dropped");
}

void ResetAllControlsChecks()
{
    InputTracker tracker = MakeTracker(3U);
    Check(Push(tracker, InputDevice::Keyboard, 7U, true) &&
              Push(tracker, InputDevice::GamepadButton, 7U, true) &&
              Push(tracker, InputDevice::Keyboard, 44U, true),
        "controls for two actions are held before a host-state reset");
    const InputSnapshot held = tracker.EndFrame();
    Check(held.IsHeld(kFire) && held.IsHeld(kJump),
        "the pre-reset snapshot captures both held actions");

    Check(Push(tracker, InputDevice::Keyboard, 500U, true) &&
              Push(tracker, InputDevice::Keyboard, 501U, true) &&
              Push(tracker, InputDevice::Keyboard, 502U, true),
        "unbound events exhaust the reset frame's event budget");
    Check(!Push(tracker, InputDevice::MouseButton, 1U, true),
        "the reset frame is demonstrably over its event budget");
    tracker.ResetAllControls();
    const InputSnapshot reset = tracker.EndFrame();
    Check(!reset.IsHeld(kFire) && reset.WasReleased(kFire) &&
              !reset.IsHeld(kJump) && reset.WasReleased(kJump),
        "one atomic reset releases every action that had a down control");
    Check(!reset.IsHeld(kCrouch) && !reset.WasReleased(kCrouch),
        "the reset does not invent an edge for an action that was already up");
    Check(reset.accepted_event_count() == 3U && reset.rejected_event_count() == 1U &&
              tracker.total_rejected_event_count() == 1U,
        "the reset bypasses the event budget without changing event counters");

    tracker.ResetAllControls();
    const InputSnapshot repeated = tracker.EndFrame();
    Check(!repeated.WasReleased(kFire) && !repeated.WasReleased(kJump) &&
              repeated.accepted_event_count() == 0U && repeated.rejected_event_count() == 0U,
        "a repeated reset is edge-free and does not consume the next frame budget");

    Check(Push(tracker, InputDevice::Keyboard, 7U, false) &&
              Push(tracker, InputDevice::GamepadButton, 7U, false) &&
              Push(tracker, InputDevice::Keyboard, 44U, false),
        "late physical releases after the reset are accepted as level no-ops");
    const InputSnapshot late_releases = tracker.EndFrame();
    Check(!late_releases.WasReleased(kFire) && !late_releases.WasReleased(kJump),
        "late host releases cannot duplicate reset-generated release edges");

    InputTracker same_frame = MakeTracker(1U);
    Check(Push(same_frame, InputDevice::MouseButton, 1U, true),
        "a press is accepted before a same-frame reset");
    same_frame.ResetAllControls();
    const InputSnapshot tap = same_frame.EndFrame();
    Check(!tap.IsHeld(kCrouch) && tap.WasPressed(kCrouch) && tap.WasReleased(kCrouch) &&
              tap.accepted_event_count() == 1U,
        "a same-frame reset preserves the press edge and adds one release edge");
}

void SnapshotValueChecks()
{
    InputTracker tracker = MakeTracker(16U);
    Check(Push(tracker, InputDevice::Keyboard, 7U, true), "press for snapshot capture");
    const InputSnapshot captured = tracker.EndFrame();
    Check(Push(tracker, InputDevice::Keyboard, 7U, false), "release after capture");
    (void)tracker.EndFrame();
    Check(captured.IsHeld(kFire) && captured.WasPressed(kFire) &&
              !captured.WasReleased(kFire),
        "an earlier snapshot is immune to later frames");
    Check(captured.actions().size() == 3U && captured.actions()[0] == kFire,
        "a snapshot lists the registered actions ascending");
    Check(!captured.IsHeld(999U) && !captured.WasPressed(999U) &&
              !captured.WasReleased(999U),
        "unregistered action queries report false, not an error");

    const InputSnapshot blank;
    Check(blank.actions().empty() && !blank.IsHeld(kFire) &&
              blank.accepted_event_count() == 0U && blank.rejected_event_count() == 0U,
        "a default snapshot is empty and fully inactive");
}

void DeterminismChecks()
{
    const std::vector<InputEvent> script = {
        InputEvent{.device = InputDevice::Keyboard, .code = 7U, .pressed = true},
        InputEvent{.device = InputDevice::MouseButton, .code = 1U, .pressed = true},
        InputEvent{.device = InputDevice::Keyboard, .code = 7U, .pressed = false},
        InputEvent{.device = InputDevice::GamepadButton, .code = 7U, .pressed = true},
        InputEvent{.device = InputDevice::Keyboard, .code = 999U, .pressed = true},
        InputEvent{.device = InputDevice::MouseButton, .code = 1U, .pressed = false},
    };

    InputTracker first = MakeTracker(4U);
    InputTracker second = MakeTracker(4U);
    bool identical = true;
    for (std::size_t offset = 0; offset < script.size(); offset += 2U)
    {
        for (std::size_t index = offset; index < offset + 2U; ++index)
        {
            const bool a = first.PushEvent(script[index]).has_value();
            const bool b = second.PushEvent(script[index]).has_value();
            identical = identical && a == b;
        }
        const InputSnapshot left = first.EndFrame();
        const InputSnapshot right = second.EndFrame();
        identical = identical && left.frame_index() == right.frame_index() &&
                    left.accepted_event_count() == right.accepted_event_count() &&
                    left.rejected_event_count() == right.rejected_event_count();
        for (const std::uint32_t action : left.actions())
            identical = identical && left.IsHeld(action) == right.IsHeld(action) &&
                        left.WasPressed(action) == right.WasPressed(action) &&
                        left.WasReleased(action) == right.WasReleased(action);
    }
    Check(identical, "identical event sequences replay into identical snapshots");
}
} // namespace

int InputTrackerFailureCount()
{
    TableValidationChecks();
    TrackerCreationChecks();
    EdgeMatrixChecks();
    MultiBindingChecks();
    BudgetAndRejectionChecks();
    ResetAllControlsChecks();
    SnapshotValueChecks();
    DeterminismChecks();
    return failures;
}
