#include "opening_movie_audio_fault.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace
{
using omega::app::ClassifyOpeningMovieAudioFault;
using omega::app::DisposeOpeningMovieAudioFault;
using omega::app::OpeningMovieAudioFault;
using omega::app::OpeningMovieAudioFaultCounters;
using omega::app::OpeningMovieAudioFaultDisposition;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}
} // namespace

int main()
{
    constexpr OpeningMovieAudioFaultCounters baseline{
        .callback_failures = 7U,
        .opening_movie_control_failures = 5U,
        .opening_movie_underrun_frames = 3U,
        .opening_movie_queue_rejections = 2U,
    };

    Check(ClassifyOpeningMovieAudioFault(baseline, baseline) ==
              OpeningMovieAudioFault::None,
        "unchanged counters are quiet");

    struct CategoryCase
    {
        OpeningMovieAudioFault expected;
        OpeningMovieAudioFaultCounters observed;
    };
    auto callback = baseline;
    ++callback.callback_failures;
    auto control = baseline;
    ++control.opening_movie_control_failures;
    auto underrun = baseline;
    ++underrun.opening_movie_underrun_frames;
    auto rejection = baseline;
    ++rejection.opening_movie_queue_rejections;
    const std::array category_cases{
        CategoryCase{OpeningMovieAudioFault::Callback, callback},
        CategoryCase{OpeningMovieAudioFault::Control, control},
        CategoryCase{OpeningMovieAudioFault::Underrun, underrun},
        CategoryCase{OpeningMovieAudioFault::QueueRejection, rejection},
    };
    for (const CategoryCase& category : category_cases)
    {
        Check(ClassifyOpeningMovieAudioFault(baseline, category.observed) ==
                  category.expected,
            "each counter maps to its categorical fault");
    }

    auto concurrent = rejection;
    ++concurrent.callback_failures;
    ++concurrent.opening_movie_control_failures;
    ++concurrent.opening_movie_underrun_frames;
    Check(ClassifyOpeningMovieAudioFault(baseline, concurrent) ==
              OpeningMovieAudioFault::Callback,
        "callback failure has stable escalation priority");

    auto stale = baseline;
    ++stale.callback_failures;
    Check(ClassifyOpeningMovieAudioFault(stale, baseline) ==
              OpeningMovieAudioFault::None,
        "a stale newer baseline cannot underflow into a fault");
    constexpr OpeningMovieAudioFaultCounters saturated{
        .callback_failures = std::numeric_limits<std::uint64_t>::max(),
        .opening_movie_control_failures =
            std::numeric_limits<std::uint64_t>::max(),
        .opening_movie_underrun_frames =
            std::numeric_limits<std::uint64_t>::max(),
        .opening_movie_queue_rejections =
            std::numeric_limits<std::uint64_t>::max(),
    };
    Check(ClassifyOpeningMovieAudioFault(saturated, saturated) ==
              OpeningMovieAudioFault::None,
        "equal saturated counters remain quiet");

    Check(DisposeOpeningMovieAudioFault(OpeningMovieAudioFault::Callback, true) ==
              OpeningMovieAudioFaultDisposition::FailOpen &&
          DisposeOpeningMovieAudioFault(OpeningMovieAudioFault::Control, true) ==
              OpeningMovieAudioFaultDisposition::FailOpen &&
          DisposeOpeningMovieAudioFault(OpeningMovieAudioFault::Underrun, true) ==
              OpeningMovieAudioFaultDisposition::FailOpen &&
          DisposeOpeningMovieAudioFault(
              OpeningMovieAudioFault::QueueRejection, true) ==
              OpeningMovieAudioFaultDisposition::FailOpen,
        "every movie-window audio category fails open");
    Check(DisposeOpeningMovieAudioFault(OpeningMovieAudioFault::Callback, false) ==
              OpeningMovieAudioFaultDisposition::Fatal &&
          DisposeOpeningMovieAudioFault(OpeningMovieAudioFault::Control, false) ==
              OpeningMovieAudioFaultDisposition::Fatal,
        "general callback and control faults remain fatal");
    Check(DisposeOpeningMovieAudioFault(OpeningMovieAudioFault::Underrun, false) ==
              OpeningMovieAudioFaultDisposition::Ignore &&
          DisposeOpeningMovieAudioFault(
              OpeningMovieAudioFault::QueueRejection, false) ==
              OpeningMovieAudioFaultDisposition::Ignore,
        "stale movie-scoped counters cannot poison the front end");

    for (const OpeningMovieAudioFault fault :
        {OpeningMovieAudioFault::None, OpeningMovieAudioFault::Callback,
            OpeningMovieAudioFault::Control, OpeningMovieAudioFault::Underrun,
            OpeningMovieAudioFault::QueueRejection})
    {
        Check(!omega::app::OpeningMovieAudioFaultMessage(fault).empty(),
            "every fault has categorical diagnostic text");
    }
    Check(omega::app::GeneralAudioFaultMessage(
              OpeningMovieAudioFault::Callback).find("movie") ==
              std::string_view::npos,
        "general audio diagnostics do not misattribute the opening movie");

    if (failures == 0)
        std::cout << "opening movie audio fault tests passed\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
