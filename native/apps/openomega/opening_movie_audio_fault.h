#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace omega::app
{
// Counter-only view of the audio service's opening-movie fault surface. Keeping
// classification independent from SDL makes the frame-boundary policy directly
// testable and prevents unsigned delta arithmetic from wrapping on stale samples.
struct OpeningMovieAudioFaultCounters
{
    std::uint64_t callback_failures = 0U;
    std::uint64_t opening_movie_control_failures = 0U;
    std::uint64_t opening_movie_underrun_frames = 0U;
    std::uint64_t opening_movie_queue_rejections = 0U;

    friend constexpr bool operator==(const OpeningMovieAudioFaultCounters&,
        const OpeningMovieAudioFaultCounters&) noexcept = default;
};

enum class OpeningMovieAudioFault : std::uint8_t
{
    None = 0U,
    Callback = 1U,
    Control = 2U,
    Underrun = 3U,
    QueueRejection = 4U,
};

enum class OpeningMovieAudioFaultDisposition : std::uint8_t
{
    Ignore = 0U,
    FailOpen = 1U,
    Fatal = 2U,
};

[[nodiscard]] constexpr OpeningMovieAudioFault ClassifyOpeningMovieAudioFault(
    const OpeningMovieAudioFaultCounters baseline,
    const OpeningMovieAudioFaultCounters observed) noexcept
{
    if (observed.callback_failures > baseline.callback_failures)
        return OpeningMovieAudioFault::Callback;
    if (observed.opening_movie_control_failures >
        baseline.opening_movie_control_failures)
    {
        return OpeningMovieAudioFault::Control;
    }
    if (observed.opening_movie_underrun_frames >
        baseline.opening_movie_underrun_frames)
    {
        return OpeningMovieAudioFault::Underrun;
    }
    if (observed.opening_movie_queue_rejections >
        baseline.opening_movie_queue_rejections)
    {
        return OpeningMovieAudioFault::QueueRejection;
    }
    return OpeningMovieAudioFault::None;
}

// Callback/control counters can describe general device failures. They fail
// open only while a frame still belongs to the modal opening-movie window.
// Underrun/rejection counters are movie-scoped and are stale once that window
// has closed, so they cannot make an unrelated front-end frame fatal.
[[nodiscard]] constexpr OpeningMovieAudioFaultDisposition
DisposeOpeningMovieAudioFault(const OpeningMovieAudioFault fault,
    const bool movie_window_open) noexcept
{
    switch (fault)
    {
    case OpeningMovieAudioFault::None:
        return OpeningMovieAudioFaultDisposition::Ignore;
    case OpeningMovieAudioFault::Callback:
    case OpeningMovieAudioFault::Control:
        return movie_window_open
            ? OpeningMovieAudioFaultDisposition::FailOpen
            : OpeningMovieAudioFaultDisposition::Fatal;
    case OpeningMovieAudioFault::Underrun:
    case OpeningMovieAudioFault::QueueRejection:
        return movie_window_open
            ? OpeningMovieAudioFaultDisposition::FailOpen
            : OpeningMovieAudioFaultDisposition::Ignore;
    }
    return OpeningMovieAudioFaultDisposition::Fatal;
}

[[nodiscard]] constexpr std::string_view OpeningMovieAudioFaultMessage(
    const OpeningMovieAudioFault fault) noexcept
{
    switch (fault)
    {
    case OpeningMovieAudioFault::Callback:
        return "opening movie audio playback callback failed";
    case OpeningMovieAudioFault::Control:
        return "opening movie audio control operation failed";
    case OpeningMovieAudioFault::Underrun:
        return "opening movie audio ring underran";
    case OpeningMovieAudioFault::QueueRejection:
        return "opening movie audio queue rejected bounded PCM";
    case OpeningMovieAudioFault::None:
        return "opening movie audio reported no fault";
    }
    return "opening movie audio fault classification failed";
}

[[nodiscard]] constexpr std::string_view GeneralAudioFaultMessage(
    const OpeningMovieAudioFault fault) noexcept
{
    switch (fault)
    {
    case OpeningMovieAudioFault::Callback:
        return "audio playback callback operation failed";
    case OpeningMovieAudioFault::Control:
        return "audio playback control operation failed";
    case OpeningMovieAudioFault::None:
    case OpeningMovieAudioFault::Underrun:
    case OpeningMovieAudioFault::QueueRejection:
        break;
    }
    return "audio playback operation failed";
}

static_assert(std::is_trivially_copyable_v<OpeningMovieAudioFaultCounters>);
static_assert(std::is_standard_layout_v<OpeningMovieAudioFaultCounters>);
static_assert(sizeof(OpeningMovieAudioFault) == 1U);
static_assert(sizeof(OpeningMovieAudioFaultDisposition) == 1U);
} // namespace omega::app
