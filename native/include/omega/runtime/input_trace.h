#pragma once

#include "omega/runtime/input_tracker.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace omega::runtime
{
// Synthetic in-process diagnostic policy, not a retail limit or timing claim.
inline constexpr std::size_t kMaximumInputTraceFrames = 65'536U;
static_assert(InputBindingTable::kMaxActions == 64U);

struct InputTraceConfig
{
    std::size_t maximum_frames = 0U;
    std::uint64_t first_frame_index = 0U;
};

enum class InputTraceErrorCode
{
    InvalidConfiguration,
    InvalidActionSchema,
    AllocationFailed,
    InvalidRecorderState,
    CapacityExceeded,
    FrameDiscontinuity,
    ActionSchemaMismatch,
};

[[nodiscard]] constexpr std::string_view InputTraceErrorCodeName(
    const InputTraceErrorCode code) noexcept
{
    switch (code)
    {
    case InputTraceErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case InputTraceErrorCode::InvalidActionSchema:
        return "invalid-action-schema";
    case InputTraceErrorCode::AllocationFailed:
        return "allocation-failed";
    case InputTraceErrorCode::InvalidRecorderState:
        return "invalid-recorder-state";
    case InputTraceErrorCode::CapacityExceeded:
        return "capacity-exceeded";
    case InputTraceErrorCode::FrameDiscontinuity:
        return "frame-discontinuity";
    case InputTraceErrorCode::ActionSchemaMismatch:
        return "action-schema-mismatch";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view InputTraceErrorMessage(
    const InputTraceErrorCode code) noexcept
{
    switch (code)
    {
    case InputTraceErrorCode::InvalidConfiguration:
        return "input trace configuration is invalid";
    case InputTraceErrorCode::InvalidActionSchema:
        return "input trace action schema is invalid";
    case InputTraceErrorCode::AllocationFailed:
        return "input trace allocation failed";
    case InputTraceErrorCode::InvalidRecorderState:
        return "input trace recorder is not open";
    case InputTraceErrorCode::CapacityExceeded:
        return "input trace frame capacity is exceeded";
    case InputTraceErrorCode::FrameDiscontinuity:
        return "input trace frame index is not contiguous";
    case InputTraceErrorCode::ActionSchemaMismatch:
        return "input trace action schema does not match";
    }
    return "input trace error is unknown";
}

struct InputTraceError
{
    InputTraceErrorCode code = InputTraceErrorCode::InvalidConfiguration;
    // Fixed category text only. It contains no action, frame, device, path, or owner data.
    std::string_view message = InputTraceErrorMessage(code);
};

// Small owned query values. They remain valid after the source trace moves or is destroyed.
struct InputTraceFrameState
{
    std::uint64_t frame_index = 0U;
    std::uint32_t accepted_event_count = 0U;
    std::uint32_t rejected_event_count = 0U;

    friend constexpr bool operator==(
        const InputTraceFrameState&, const InputTraceFrameState&) noexcept = default;
};

struct InputTraceActionState
{
    bool held = false;
    bool pressed = false;
    bool released = false;

    friend constexpr bool operator==(
        const InputTraceActionState&, const InputTraceActionState&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<InputTraceFrameState>);
static_assert(std::is_standard_layout_v<InputTraceFrameState>);
static_assert(std::is_trivially_copyable_v<InputTraceActionState>);
static_assert(std::is_standard_layout_v<InputTraceActionState>);

class InputTraceRecorder;

// Immutable, move-only, in-process logical-input trace. After ownership publication, const reads
// are [any thread; reentrant] provided no reader races a move or destruction. This value is
// non-hot-reloadable. It contains no scheduler timing, quit/run-control, or simulation state.
class InputTrace final
{
public:
    InputTrace(InputTrace&& other) noexcept;
    InputTrace& operator=(InputTrace&&) = delete;
    InputTrace(const InputTrace&) = delete;
    InputTrace& operator=(const InputTrace&) = delete;
    ~InputTrace() = default;

    // [any thread; reentrant after publication] Moved-from traces report zero for all metadata.
    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] std::size_t maximum_frames() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;

    // [any thread; reentrant after publication] One fixed ascending logical-action schema. This is
    // the trace API's only borrowed value and is invalidated only by trace move or destruction.
    [[nodiscard]] std::span<const std::uint32_t> actions() const noexcept;

    // [any thread; reentrant after publication] Returns owned metadata for an active frame offset.
    // An offset at or beyond frame_count() returns std::nullopt.
    [[nodiscard]] std::optional<InputTraceFrameState> FrameAt(
        std::size_t frame_offset) const noexcept;

    // [any thread; reentrant after publication] An invalid frame returns std::nullopt. An unknown
    // action on a valid frame returns an engaged all-false value, matching InputSnapshot queries.
    [[nodiscard]] std::optional<InputTraceActionState> ActionAt(
        std::size_t frame_offset, std::uint32_t action) const noexcept;

private:
    friend class InputTraceRecorder;

    struct FrameRecord
    {
        std::uint64_t held_mask = 0U;
        std::uint64_t pressed_mask = 0U;
        std::uint64_t released_mask = 0U;
        std::uint32_t accepted_event_count = 0U;
        std::uint32_t rejected_event_count = 0U;
    };

    static_assert(sizeof(FrameRecord) == 32U);
    static_assert(std::is_trivially_copyable_v<FrameRecord>);
    static_assert(std::is_standard_layout_v<FrameRecord>);

    using ActionArray = std::array<std::uint32_t, InputBindingTable::kMaxActions>;

    InputTrace(InputTraceConfig config, std::size_t action_count, std::size_t frame_count,
        const ActionArray& actions, std::vector<FrameRecord>&& frames) noexcept;
    void NormalizeInert() noexcept;

    InputTraceConfig config_{};
    std::size_t action_count_ = 0U;
    std::size_t frame_count_ = 0U;
    ActionArray actions_{};
    std::vector<FrameRecord> frames_;
};

// Exclusive game-thread owner for bounded post-binding logical input capture. It observes const
// snapshots without retaining or mutating them. It is not a service, performs no input injection,
// and captures no scheduler timing, quit/run-control, or simulation state.
class InputTraceRecorder final
{
public:
    // [any thread; reentrant] Validates and copies one fixed ascending nonempty action schema, then
    // allocates the complete bounded frame backing before returning an unpublished owner.
    [[nodiscard]] static std::expected<InputTraceRecorder, InputTraceError> Create(
        InputTraceConfig config, std::span<const std::uint32_t> action_schema);

    // [game thread; no concurrent use] Transfers the complete recorder and leaves the source inert.
    InputTraceRecorder(InputTraceRecorder&& other) noexcept;
    InputTraceRecorder& operator=(InputTraceRecorder&&) = delete;
    InputTraceRecorder(const InputTraceRecorder&) = delete;
    InputTraceRecorder& operator=(const InputTraceRecorder&) = delete;
    ~InputTraceRecorder() = default;

    // [game thread] Records one exact contiguous logical snapshot without allocation. Failure
    // changes neither the recorder nor the caller-owned snapshot.
    [[nodiscard]] std::expected<void, InputTraceError> Append(
        const InputSnapshot& snapshot) noexcept;

    // [game thread] Move-finalizes an immutable trace without allocating. An open zero-frame
    // recorder succeeds. A moved-from or already-finished recorder fails closed.
    [[nodiscard]] std::expected<InputTrace, InputTraceError> Finish() && noexcept;

    // [game thread] A moved-from or finished recorder reports zero/empty inert metadata. The
    // borrowed actions() view is invalidated by recorder move, successful Finish, or destruction.
    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] std::size_t maximum_frames() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;
    [[nodiscard]] std::span<const std::uint32_t> actions() const noexcept;

private:
    InputTraceRecorder(InputTraceConfig config,
        std::span<const std::uint32_t> action_schema);
    void NormalizeInert() noexcept;

    InputTraceConfig config_{};
    std::size_t action_count_ = 0U;
    std::size_t frame_count_ = 0U;
    InputTrace::ActionArray actions_{};
    std::vector<InputTrace::FrameRecord> frames_;
};
} // namespace omega::runtime
