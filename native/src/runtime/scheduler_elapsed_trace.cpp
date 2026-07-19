#include "omega/runtime/scheduler_elapsed_trace.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <utility>

namespace omega::runtime
{
static_assert(std::chrono::nanoseconds::min().count() ==
              std::numeric_limits<std::int64_t>::min());
static_assert(std::chrono::nanoseconds::max().count() ==
              std::numeric_limits<std::int64_t>::max());

namespace
{
[[nodiscard]] constexpr SchedulerElapsedTraceError Error(
    const SchedulerElapsedTraceErrorCode code) noexcept
{
    return SchedulerElapsedTraceError{
        .code = code,
        .message = SchedulerElapsedTraceErrorMessage(code),
    };
}

[[nodiscard]] bool IsValidConfig(const SchedulerElapsedTraceConfig config) noexcept
{
    if (config.maximum_frames == 0U ||
        config.maximum_frames > kMaximumSchedulerElapsedTraceFrames)
    {
        return false;
    }

    const auto final_offset = static_cast<std::uint64_t>(config.maximum_frames - 1U);
    return config.first_frame_index <=
           std::numeric_limits<std::uint64_t>::max() - final_offset;
}
} // namespace

SchedulerElapsedTrace::SchedulerElapsedTrace(const SchedulerElapsedTraceConfig config,
    const std::size_t frame_count, std::vector<FrameRecord>&& frames) noexcept
    : config_(config), frame_count_(frame_count), frames_(std::move(frames))
{
}

SchedulerElapsedTrace::SchedulerElapsedTrace(SchedulerElapsedTrace&& other) noexcept
    : config_(std::exchange(other.config_, SchedulerElapsedTraceConfig{})),
      frame_count_(std::exchange(other.frame_count_, 0U)),
      frames_(std::move(other.frames_))
{
    other.NormalizeInert();
}

void SchedulerElapsedTrace::NormalizeInert() noexcept
{
    config_ = {};
    frame_count_ = 0U;
    // A moved-from vector may retain storage. Clear its logical contents without making an
    // allocator-capacity or process-memory claim.
    frames_.clear();
}

std::uint64_t SchedulerElapsedTrace::first_frame_index() const noexcept
{
    return config_.first_frame_index;
}

std::size_t SchedulerElapsedTrace::maximum_frames() const noexcept
{
    return config_.maximum_frames;
}

std::size_t SchedulerElapsedTrace::frame_count() const noexcept
{
    return frame_count_;
}

std::optional<SchedulerElapsedFrameState> SchedulerElapsedTrace::FrameAt(
    const std::size_t frame_offset) const noexcept
{
    if (frame_offset >= frame_count_)
        return std::nullopt;

    return SchedulerElapsedFrameState{
        .frame_index = config_.first_frame_index +
                       static_cast<std::uint64_t>(frame_offset),
        .elapsed = std::chrono::nanoseconds{frames_[frame_offset]},
    };
}

std::expected<SchedulerElapsedTraceRecorder, SchedulerElapsedTraceError>
SchedulerElapsedTraceRecorder::Create(const SchedulerElapsedTraceConfig config)
{
    if (!IsValidConfig(config))
    {
        return std::unexpected(
            Error(SchedulerElapsedTraceErrorCode::InvalidConfiguration));
    }

    try
    {
        SchedulerElapsedTraceRecorder recorder(config);
        return std::expected<SchedulerElapsedTraceRecorder,
            SchedulerElapsedTraceError>{std::move(recorder)};
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(Error(SchedulerElapsedTraceErrorCode::AllocationFailed));
    }
}

SchedulerElapsedTraceRecorder::SchedulerElapsedTraceRecorder(
    const SchedulerElapsedTraceConfig config)
    : config_(config), frames_(config.maximum_frames)
{
}

SchedulerElapsedTraceRecorder::SchedulerElapsedTraceRecorder(
    SchedulerElapsedTraceRecorder&& other) noexcept
    : config_(std::exchange(other.config_, SchedulerElapsedTraceConfig{})),
      frame_count_(std::exchange(other.frame_count_, 0U)),
      frames_(std::move(other.frames_))
{
    other.NormalizeInert();
}

void SchedulerElapsedTraceRecorder::NormalizeInert() noexcept
{
    config_ = {};
    frame_count_ = 0U;
    frames_.clear();
}

std::expected<void, SchedulerElapsedTraceError> SchedulerElapsedTraceRecorder::Append(
    const std::uint64_t frame_index, const std::chrono::nanoseconds elapsed) noexcept
{
    if (config_.maximum_frames == 0U)
    {
        return std::unexpected(
            Error(SchedulerElapsedTraceErrorCode::InvalidRecorderState));
    }
    if (frame_count_ >= config_.maximum_frames)
        return std::unexpected(Error(SchedulerElapsedTraceErrorCode::CapacityExceeded));

    const std::uint64_t expected_frame =
        config_.first_frame_index + static_cast<std::uint64_t>(frame_count_);
    if (frame_index != expected_frame)
    {
        return std::unexpected(
            Error(SchedulerElapsedTraceErrorCode::FrameDiscontinuity));
    }

    frames_[frame_count_] = elapsed.count();
    ++frame_count_;
    return {};
}

std::expected<SchedulerElapsedTrace, SchedulerElapsedTraceError>
SchedulerElapsedTraceRecorder::Finish() && noexcept
{
    if (config_.maximum_frames == 0U)
    {
        return std::unexpected(
            Error(SchedulerElapsedTraceErrorCode::InvalidRecorderState));
    }

    SchedulerElapsedTrace trace(config_, frame_count_, std::move(frames_));
    NormalizeInert();
    return std::expected<SchedulerElapsedTrace,
        SchedulerElapsedTraceError>{std::move(trace)};
}

std::uint64_t SchedulerElapsedTraceRecorder::first_frame_index() const noexcept
{
    return config_.first_frame_index;
}

std::size_t SchedulerElapsedTraceRecorder::maximum_frames() const noexcept
{
    return config_.maximum_frames;
}

std::size_t SchedulerElapsedTraceRecorder::frame_count() const noexcept
{
    return frame_count_;
}
} // namespace omega::runtime
