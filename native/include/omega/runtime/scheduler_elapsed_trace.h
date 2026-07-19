#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace omega::runtime
{
// Synthetic in-process diagnostic policy, not a retail limit or timing claim.
inline constexpr std::size_t kMaximumSchedulerElapsedTraceFrames = 65'536U;

struct SchedulerElapsedTraceConfig
{
    std::size_t maximum_frames = 0U;
    std::uint64_t first_frame_index = 0U;
};

enum class SchedulerElapsedTraceErrorCode
{
    InvalidConfiguration,
    AllocationFailed,
    InvalidRecorderState,
    CapacityExceeded,
    FrameDiscontinuity,
};

[[nodiscard]] constexpr std::string_view SchedulerElapsedTraceErrorCodeName(
    const SchedulerElapsedTraceErrorCode code) noexcept
{
    switch (code)
    {
    case SchedulerElapsedTraceErrorCode::InvalidConfiguration:
        return "invalid-configuration";
    case SchedulerElapsedTraceErrorCode::AllocationFailed:
        return "allocation-failed";
    case SchedulerElapsedTraceErrorCode::InvalidRecorderState:
        return "invalid-recorder-state";
    case SchedulerElapsedTraceErrorCode::CapacityExceeded:
        return "capacity-exceeded";
    case SchedulerElapsedTraceErrorCode::FrameDiscontinuity:
        return "frame-discontinuity";
    }
    return "unknown";
}

[[nodiscard]] constexpr std::string_view SchedulerElapsedTraceErrorMessage(
    const SchedulerElapsedTraceErrorCode code) noexcept
{
    switch (code)
    {
    case SchedulerElapsedTraceErrorCode::InvalidConfiguration:
        return "scheduler elapsed trace configuration is invalid";
    case SchedulerElapsedTraceErrorCode::AllocationFailed:
        return "scheduler elapsed trace allocation failed";
    case SchedulerElapsedTraceErrorCode::InvalidRecorderState:
        return "scheduler elapsed trace recorder is not open";
    case SchedulerElapsedTraceErrorCode::CapacityExceeded:
        return "scheduler elapsed trace frame capacity is exceeded";
    case SchedulerElapsedTraceErrorCode::FrameDiscontinuity:
        return "scheduler elapsed trace frame index is not contiguous";
    }
    return "scheduler elapsed trace error is unknown";
}

struct SchedulerElapsedTraceError
{
    SchedulerElapsedTraceErrorCode code =
        SchedulerElapsedTraceErrorCode::InvalidConfiguration;
    // Fixed category text only. It contains no frame, clock, path, or owner data.
    std::string_view message = SchedulerElapsedTraceErrorMessage(code);
};

// Small owned query value. It remains valid after the source trace moves or is destroyed.
struct SchedulerElapsedFrameState
{
    std::uint64_t frame_index = 0U;
    std::chrono::nanoseconds elapsed{0};

    friend constexpr bool operator==(const SchedulerElapsedFrameState&,
        const SchedulerElapsedFrameState&) noexcept = default;
};

static_assert(std::is_trivially_copyable_v<SchedulerElapsedFrameState>);
static_assert(std::is_standard_layout_v<SchedulerElapsedFrameState>);

class SchedulerElapsedTraceRecorder;

// Immutable, move-only, in-process scheduler-input trace. After ownership publication, const
// reads are [any thread; reentrant] provided no reader races a move or destruction. This value
// is non-hot-reloadable and contains no clock, input, quit/run-control, or simulation state.
class SchedulerElapsedTrace final
{
public:
    SchedulerElapsedTrace(SchedulerElapsedTrace&& other) noexcept;
    SchedulerElapsedTrace& operator=(SchedulerElapsedTrace&&) = delete;
    SchedulerElapsedTrace(const SchedulerElapsedTrace&) = delete;
    SchedulerElapsedTrace& operator=(const SchedulerElapsedTrace&) = delete;
    ~SchedulerElapsedTrace() = default;

    // [any thread; reentrant after publication] Moved-from traces report zero metadata.
    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] std::size_t maximum_frames() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;

    // [any thread; reentrant after publication] Returns an owned active-frame value. An offset
    // at or beyond frame_count() returns std::nullopt.
    [[nodiscard]] std::optional<SchedulerElapsedFrameState> FrameAt(
        std::size_t frame_offset) const noexcept;

private:
    friend class SchedulerElapsedTraceRecorder;

    using FrameRecord = std::int64_t;

    static_assert(sizeof(FrameRecord) == 8U);
    static_assert(std::is_trivially_copyable_v<FrameRecord>);
    static_assert(std::is_standard_layout_v<FrameRecord>);

    SchedulerElapsedTrace(SchedulerElapsedTraceConfig config, std::size_t frame_count,
        std::vector<FrameRecord>&& frames) noexcept;
    void NormalizeInert() noexcept;

    SchedulerElapsedTraceConfig config_{};
    std::size_t frame_count_ = 0U;
    std::vector<FrameRecord> frames_;
};

// Exclusive game-thread owner for bounded scheduler-elapsed capture. It records caller-supplied
// values without measuring, clamping, or interpreting them. It is not a service, performs no
// injection or playback, and captures no input, quit/run-control, or simulation state.
class SchedulerElapsedTraceRecorder final
{
public:
    // [any thread; reentrant] Validates the complete contiguous range and allocates all bounded
    // frame backing before returning an unpublished owner.
    [[nodiscard]] static std::expected<SchedulerElapsedTraceRecorder,
        SchedulerElapsedTraceError>
    Create(SchedulerElapsedTraceConfig config);

    // [game thread; no concurrent use] Transfers the complete recorder and leaves the source
    // inert.
    SchedulerElapsedTraceRecorder(SchedulerElapsedTraceRecorder&& other) noexcept;
    SchedulerElapsedTraceRecorder& operator=(SchedulerElapsedTraceRecorder&&) = delete;
    SchedulerElapsedTraceRecorder(const SchedulerElapsedTraceRecorder&) = delete;
    SchedulerElapsedTraceRecorder& operator=(const SchedulerElapsedTraceRecorder&) = delete;
    ~SchedulerElapsedTraceRecorder() = default;

    // [game thread] Records one exact contiguous elapsed value without allocation. Every signed
    // nanosecond value is data, including negative, zero, and representation-limit values.
    // Failure changes neither the recorder nor caller-owned values.
    [[nodiscard]] std::expected<void, SchedulerElapsedTraceError> Append(
        std::uint64_t frame_index, std::chrono::nanoseconds elapsed) noexcept;

    // [game thread] Move-finalizes an immutable trace without allocating. An open zero-frame
    // recorder succeeds. A moved-from or already-finished recorder fails closed.
    [[nodiscard]] std::expected<SchedulerElapsedTrace, SchedulerElapsedTraceError>
    Finish() && noexcept;

    // [game thread] A moved-from or finished recorder reports zero inert metadata.
    [[nodiscard]] std::uint64_t first_frame_index() const noexcept;
    [[nodiscard]] std::size_t maximum_frames() const noexcept;
    [[nodiscard]] std::size_t frame_count() const noexcept;

private:
    explicit SchedulerElapsedTraceRecorder(SchedulerElapsedTraceConfig config);
    void NormalizeInert() noexcept;

    SchedulerElapsedTraceConfig config_{};
    std::size_t frame_count_ = 0U;
    std::vector<SchedulerElapsedTrace::FrameRecord> frames_;
};
} // namespace omega::runtime
