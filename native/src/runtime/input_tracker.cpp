#include "omega/runtime/input_tracker.h"

#include <algorithm>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] bool IsKnownDevice(const InputDevice device) noexcept
{
    return device == InputDevice::Keyboard || device == InputDevice::MouseButton ||
           device == InputDevice::GamepadButton;
}

[[nodiscard]] bool ControlLess(const InputBinding& left, const InputBinding& right) noexcept
{
    if (left.device != right.device)
        return static_cast<std::uint8_t>(left.device) < static_cast<std::uint8_t>(right.device);
    return left.code < right.code;
}

// Returns the index of the binding whose (device, code) matches, or the binding count.
[[nodiscard]] std::size_t FindControl(const std::span<const InputBinding> sorted_bindings,
    const InputDevice device, const std::uint16_t code) noexcept
{
    const InputBinding probe{.device = device, .code = code, .action = 0};
    const auto position =
        std::lower_bound(sorted_bindings.begin(), sorted_bindings.end(), probe, ControlLess);
    if (position == sorted_bindings.end() || position->device != device ||
        position->code != code)
        return sorted_bindings.size();
    return static_cast<std::size_t>(position - sorted_bindings.begin());
}
} // namespace

std::expected<InputBindingTable, std::string> InputBindingTable::FromBindings(
    const std::span<const InputBinding> bindings)
{
    if (bindings.empty())
        return std::unexpected("a binding table requires at least one binding");
    if (bindings.size() > kMaxBindings)
        return std::unexpected("binding count exceeds the table budget");

    InputBindingTable table;
    table.bindings_.assign(bindings.begin(), bindings.end());
    for (const InputBinding& binding : table.bindings_)
    {
        if (!IsKnownDevice(binding.device))
            return std::unexpected("binding names an unknown input device");
    }

    std::sort(table.bindings_.begin(), table.bindings_.end(), ControlLess);
    for (std::size_t index = 1; index < table.bindings_.size(); ++index)
    {
        const InputBinding& previous = table.bindings_[index - 1];
        const InputBinding& current = table.bindings_[index];
        if (previous.device == current.device && previous.code == current.code)
            return std::unexpected("duplicate (device, code) binding");
    }

    table.actions_.reserve(table.bindings_.size());
    for (const InputBinding& binding : table.bindings_)
        table.actions_.push_back(binding.action);
    std::sort(table.actions_.begin(), table.actions_.end());
    table.actions_.erase(
        std::unique(table.actions_.begin(), table.actions_.end()), table.actions_.end());
    if (table.actions_.size() > kMaxActions)
        return std::unexpected("action count exceeds the table budget");
    return table;
}

std::span<const InputBinding> InputBindingTable::bindings() const noexcept
{
    return bindings_;
}

std::span<const std::uint32_t> InputBindingTable::actions() const noexcept
{
    return actions_;
}

bool InputBindingTable::HasAction(const std::uint32_t action) const noexcept
{
    return std::binary_search(actions_.begin(), actions_.end(), action);
}

std::uint64_t InputSnapshot::frame_index() const noexcept
{
    return frame_index_;
}

std::span<const std::uint32_t> InputSnapshot::actions() const noexcept
{
    return actions_;
}

bool InputSnapshot::IsHeld(const std::uint32_t action) const noexcept
{
    for (const ActionRow& row : rows_)
    {
        if (row.action == action)
            return row.held;
    }
    return false;
}

bool InputSnapshot::WasPressed(const std::uint32_t action) const noexcept
{
    for (const ActionRow& row : rows_)
    {
        if (row.action == action)
            return row.pressed;
    }
    return false;
}

bool InputSnapshot::WasReleased(const std::uint32_t action) const noexcept
{
    for (const ActionRow& row : rows_)
    {
        if (row.action == action)
            return row.released;
    }
    return false;
}

std::uint32_t InputSnapshot::accepted_event_count() const noexcept
{
    return accepted_event_count_;
}

std::uint32_t InputSnapshot::rejected_event_count() const noexcept
{
    return rejected_event_count_;
}

InputTracker::InputTracker(InputBindingTable table, const std::size_t max_events_per_frame)
    : table_(std::move(table)),
      max_events_per_frame_(max_events_per_frame)
{
    const std::span<const InputBinding> sorted = table_.bindings();
    const std::span<const std::uint32_t> actions = table_.actions();
    control_down_.assign(sorted.size(), 0U);
    control_action_index_.reserve(sorted.size());
    for (const InputBinding& binding : sorted)
    {
        const auto position =
            std::lower_bound(actions.begin(), actions.end(), binding.action);
        control_action_index_.push_back(
            static_cast<std::size_t>(position - actions.begin()));
    }
    action_states_.reserve(actions.size());
    for (const std::uint32_t action : actions)
        action_states_.push_back(ActionState{.action = action});
}

std::expected<InputTracker, std::string> InputTracker::Create(
    InputBindingTable table, const std::size_t max_events_per_frame)
{
    if (max_events_per_frame == 0U)
        return std::unexpected("the per-frame event budget must be at least one");
    if (max_events_per_frame > kMaxEventsPerFrameLimit)
        return std::unexpected("the per-frame event budget exceeds the tracker limit");
    return InputTracker(std::move(table), max_events_per_frame);
}

std::expected<void, std::string> InputTracker::PushEvent(const InputEvent event)
{
    if (!IsKnownDevice(event.device))
    {
        ++frame_rejected_events_;
        ++total_rejected_events_;
        return std::unexpected("event names an unknown input device");
    }
    if (frame_accepted_events_ >= max_events_per_frame_)
    {
        ++frame_rejected_events_;
        ++total_rejected_events_;
        return std::unexpected("per-frame event budget exhausted");
    }
    ++frame_accepted_events_;

    const std::span<const InputBinding> sorted = table_.bindings();
    const std::size_t control = FindControl(sorted, event.device, event.code);
    if (control == sorted.size())
        return {}; // Unbound control: accepted, affects no action.

    const bool was_down = control_down_[control] != 0U;
    ActionState& state = action_states_[control_action_index_[control]];
    if (event.pressed && !was_down)
    {
        control_down_[control] = 1U;
        if (state.down_count == 0U)
            state.pressed_edge = true;
        ++state.down_count;
    }
    else if (!event.pressed && was_down)
    {
        control_down_[control] = 0U;
        --state.down_count;
        if (state.down_count == 0U)
            state.released_edge = true;
    }
    return {};
}

void InputTracker::ResetAllControls() noexcept
{
    std::fill(control_down_.begin(), control_down_.end(), std::uint8_t{0});
    for (ActionState& state : action_states_)
    {
        if (state.down_count == 0U)
            continue;
        state.down_count = 0U;
        state.released_edge = true;
    }
}

InputSnapshot InputTracker::EndFrame()
{
    InputSnapshot snapshot;
    snapshot.frame_index_ = next_frame_index_;
    snapshot.accepted_event_count_ = frame_accepted_events_;
    snapshot.rejected_event_count_ = frame_rejected_events_;
    snapshot.actions_.reserve(action_states_.size());
    snapshot.rows_.reserve(action_states_.size());
    for (ActionState& state : action_states_)
    {
        snapshot.actions_.push_back(state.action);
        snapshot.rows_.push_back(InputSnapshot::ActionRow{
            .action = state.action,
            .held = state.down_count > 0U,
            .pressed = state.pressed_edge,
            .released = state.released_edge,
        });
        state.pressed_edge = false;
        state.released_edge = false;
    }
    ++next_frame_index_;
    frame_accepted_events_ = 0U;
    frame_rejected_events_ = 0U;
    return snapshot;
}

const InputBindingTable& InputTracker::bindings() const noexcept
{
    return table_;
}

std::size_t InputTracker::max_events_per_frame() const noexcept
{
    return max_events_per_frame_;
}

std::uint64_t InputTracker::total_rejected_event_count() const noexcept
{
    return total_rejected_events_;
}
} // namespace omega::runtime
