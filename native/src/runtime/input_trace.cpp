#include "omega/runtime/input_trace.h"
#include "omega/debug/subsystem_entry_break.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <span>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr InputTraceError Error(const InputTraceErrorCode code) noexcept
{
    return InputTraceError{.code = code, .message = InputTraceErrorMessage(code)};
}

[[nodiscard]] bool IsValidConfig(const InputTraceConfig config) noexcept
{
    if (config.maximum_frames == 0U ||
        config.maximum_frames > kMaximumInputTraceFrames)
    {
        return false;
    }

    const auto final_offset = static_cast<std::uint64_t>(config.maximum_frames - 1U);
    return config.first_frame_index <=
           std::numeric_limits<std::uint64_t>::max() - final_offset;
}

[[nodiscard]] bool IsValidActionSchema(
    const std::span<const std::uint32_t> actions) noexcept
{
    if (actions.empty() || actions.size() > InputBindingTable::kMaxActions)
        return false;

    for (std::size_t index = 1U; index < actions.size(); ++index)
    {
        if (actions[index - 1U] >= actions[index])
            return false;
    }
    return true;
}

[[nodiscard]] constexpr std::uint64_t PackPointerPosition(
    const std::optional<PointerPositionQ16>& position) noexcept
{
    if (!position)
        return std::numeric_limits<std::uint64_t>::max();
    return (static_cast<std::uint64_t>(position->y) << 32U) |
           static_cast<std::uint64_t>(position->x);
}

[[nodiscard]] constexpr std::optional<PointerPositionQ16> UnpackPointerPosition(
    const std::uint64_t packed) noexcept
{
    if (packed == std::numeric_limits<std::uint64_t>::max())
        return std::nullopt;
    return PointerPositionQ16{
        .x = static_cast<std::uint32_t>(packed),
        .y = static_cast<std::uint32_t>(packed >> 32U),
    };
}
} // namespace

InputTrace::InputTrace(const InputTraceConfig config, const std::size_t action_count,
    const std::size_t frame_count, const ActionArray& actions,
    std::vector<FrameRecord>&& frames) noexcept
    : config_(config), action_count_(action_count), frame_count_(frame_count),
      actions_(actions), frames_(std::move(frames))
{
}

InputTrace::InputTrace(InputTrace&& other) noexcept
    : config_(std::exchange(other.config_, InputTraceConfig{})),
      action_count_(std::exchange(other.action_count_, 0U)),
      frame_count_(std::exchange(other.frame_count_, 0U)), actions_(other.actions_),
      frames_(std::move(other.frames_))
{
    other.NormalizeInert();
}

void InputTrace::NormalizeInert() noexcept
{
    config_ = {};
    action_count_ = 0U;
    frame_count_ = 0U;
    actions_.fill(0U);
    // A moved-from vector may retain storage. Clear its logical contents without making an
    // allocator-capacity or process-memory claim.
    frames_.clear();
}

std::uint64_t InputTrace::first_frame_index() const noexcept
{
    return config_.first_frame_index;
}

std::size_t InputTrace::maximum_frames() const noexcept
{
    return config_.maximum_frames;
}

std::size_t InputTrace::frame_count() const noexcept
{
    return frame_count_;
}

std::span<const std::uint32_t> InputTrace::actions() const noexcept
{
    return {actions_.data(), action_count_};
}

std::optional<InputTraceFrameState> InputTrace::FrameAt(
    const std::size_t frame_offset) const noexcept
{
    if (frame_offset >= frame_count_)
        return std::nullopt;

    const FrameRecord& frame = frames_[frame_offset];
    return InputTraceFrameState{
        .frame_index = config_.first_frame_index +
                       static_cast<std::uint64_t>(frame_offset),
        .accepted_event_count = frame.accepted_event_count,
        .rejected_event_count = frame.rejected_event_count,
    };
}

std::optional<PointerPositionQ16> InputTrace::PointerAt(
    const std::size_t frame_offset) const noexcept
{
    if (frame_offset >= frame_count_)
        return std::nullopt;
    return UnpackPointerPosition(frames_[frame_offset].packed_pointer_position);
}

std::optional<InputTraceActionState> InputTrace::ActionAt(
    const std::size_t frame_offset, const std::uint32_t action) const noexcept
{
    if (frame_offset >= frame_count_)
        return std::nullopt;

    std::size_t first = 0U;
    std::size_t last = action_count_;
    while (first < last)
    {
        const std::size_t middle = first + (last - first) / 2U;
        if (actions_[middle] < action)
            first = middle + 1U;
        else
            last = middle;
    }

    if (first == action_count_ || actions_[first] != action)
        return InputTraceActionState{};

    const std::uint64_t bit = std::uint64_t{1U} << first;
    const FrameRecord& frame = frames_[frame_offset];
    return InputTraceActionState{
        .held = (frame.held_mask & bit) != 0U,
        .pressed = (frame.pressed_mask & bit) != 0U,
        .released = (frame.released_mask & bit) != 0U,
    };
}

std::expected<InputTraceRecorder, InputTraceError> InputTraceRecorder::Create(
    const InputTraceConfig config,
    const std::span<const std::uint32_t> action_schema)
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_runtime");
    if (!IsValidConfig(config))
        return std::unexpected(Error(InputTraceErrorCode::InvalidConfiguration));
    if (!IsValidActionSchema(action_schema))
        return std::unexpected(Error(InputTraceErrorCode::InvalidActionSchema));

    try
    {
        InputTraceRecorder recorder(config, action_schema);
        return std::expected<InputTraceRecorder, InputTraceError>{std::move(recorder)};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(InputTraceErrorCode::AllocationFailed));
    }
}

InputTraceRecorder::InputTraceRecorder(const InputTraceConfig config,
    const std::span<const std::uint32_t> action_schema)
    : config_(config), action_count_(action_schema.size()),
      frames_(config.maximum_frames)
{
    for (std::size_t index = 0U; index < action_count_; ++index)
        actions_[index] = action_schema[index];
}

InputTraceRecorder::InputTraceRecorder(InputTraceRecorder&& other) noexcept
    : config_(std::exchange(other.config_, InputTraceConfig{})),
      action_count_(std::exchange(other.action_count_, 0U)),
      frame_count_(std::exchange(other.frame_count_, 0U)), actions_(other.actions_),
      frames_(std::move(other.frames_))
{
    other.NormalizeInert();
}

void InputTraceRecorder::NormalizeInert() noexcept
{
    config_ = {};
    action_count_ = 0U;
    frame_count_ = 0U;
    actions_.fill(0U);
    frames_.clear();
}

std::expected<void, InputTraceError> InputTraceRecorder::Append(
    const InputSnapshot& snapshot) noexcept
{
    if (config_.maximum_frames == 0U)
        return std::unexpected(Error(InputTraceErrorCode::InvalidRecorderState));
    if (frame_count_ >= config_.maximum_frames)
        return std::unexpected(Error(InputTraceErrorCode::CapacityExceeded));

    const std::uint64_t expected_frame =
        config_.first_frame_index + static_cast<std::uint64_t>(frame_count_);
    if (snapshot.frame_index() != expected_frame)
        return std::unexpected(Error(InputTraceErrorCode::FrameDiscontinuity));

    const std::span<const std::uint32_t> snapshot_actions = snapshot.actions();
    if (snapshot_actions.size() != action_count_)
        return std::unexpected(Error(InputTraceErrorCode::ActionSchemaMismatch));
    for (std::size_t index = 0U; index < action_count_; ++index)
    {
        if (snapshot_actions[index] != actions_[index])
            return std::unexpected(Error(InputTraceErrorCode::ActionSchemaMismatch));
    }

    InputTrace::FrameRecord record{
        .packed_pointer_position = PackPointerPosition(snapshot.pointer_position()),
        .accepted_event_count = snapshot.accepted_event_count(),
        .rejected_event_count = snapshot.rejected_event_count(),
    };
    for (std::size_t index = 0U; index < action_count_; ++index)
    {
        const std::uint32_t action = actions_[index];
        const std::uint64_t bit = std::uint64_t{1U} << index;
        if (snapshot.IsHeld(action))
            record.held_mask |= bit;
        if (snapshot.WasPressed(action))
            record.pressed_mask |= bit;
        if (snapshot.WasReleased(action))
            record.released_mask |= bit;
    }

    frames_[frame_count_] = record;
    ++frame_count_;
    return {};
}

std::expected<InputTrace, InputTraceError> InputTraceRecorder::Finish() && noexcept
{
    if (config_.maximum_frames == 0U)
        return std::unexpected(Error(InputTraceErrorCode::InvalidRecorderState));

    InputTrace trace(
        config_, action_count_, frame_count_, actions_, std::move(frames_));
    NormalizeInert();
    return std::expected<InputTrace, InputTraceError>{std::move(trace)};
}

std::uint64_t InputTraceRecorder::first_frame_index() const noexcept
{
    return config_.first_frame_index;
}

std::size_t InputTraceRecorder::maximum_frames() const noexcept
{
    return config_.maximum_frames;
}

std::size_t InputTraceRecorder::frame_count() const noexcept
{
    return frame_count_;
}

std::span<const std::uint32_t> InputTraceRecorder::actions() const noexcept
{
    return {actions_.data(), action_count_};
}
} // namespace omega::runtime
