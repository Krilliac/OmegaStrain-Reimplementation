#include "boot_sequence.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

int main()
{
    using omega::app::BootSequenceConfig;
    using omega::app::BootSequenceInput;
    using omega::app::BootSequencePhase;
    using omega::app::BootSequenceReduction;
    using omega::app::BootSequenceState;

    int failures = 0;
    const auto Check = [&failures](const bool condition, const std::string_view message) {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
            ++failures;
        }
    };

    static_assert(noexcept(omega::app::InitialBootSequenceState({})));
    static_assert(noexcept(omega::app::ReduceBootSequence({}, {})));
    static_assert(std::is_trivially_copyable_v<BootSequenceState>);
    static_assert(omega::app::kBootSequenceTicksPerSecond == 1'000'000U);

    Check(omega::app::InitialBootSequenceState({}) == BootSequenceState{} &&
              omega::app::InitialBootSequenceState(BootSequenceConfig{
                  .duration_ticks = 10U, .source_available = false}) == BootSequenceState{} &&
              omega::app::InitialBootSequenceState(BootSequenceConfig{
                  .duration_ticks = 0U, .source_available = true}) == BootSequenceState{},
          "missing and empty sources preserve immediate front-end startup");

    const BootSequenceState playing = omega::app::InitialBootSequenceState(
        BootSequenceConfig{.duration_ticks = 10U, .source_available = true});
    Check(playing ==
                  BootSequenceState{
                      .phase = BootSequencePhase::Playing,
                      .position_ticks = 0U,
                      .duration_ticks = 10U,
                  } &&
              omega::app::IsBootSequenceActive(playing),
          "a nonempty available source starts at the exact zero position");

    const BootSequenceReduction advanced =
        omega::app::ReduceBootSequence(playing, BootSequenceInput{.elapsed_ticks = 4U});
    Check(advanced ==
              BootSequenceReduction{
                  .state =
                      BootSequenceState{
                          .phase = BootSequencePhase::Playing,
                          .position_ticks = 4U,
                          .duration_ticks = 10U,
                      },
              },
          "a partial elapsed interval advances without entering the front end");

    const BootSequenceReduction completed_exact =
        omega::app::ReduceBootSequence(advanced.state, BootSequenceInput{.elapsed_ticks = 6U});
    const BootSequenceReduction completed_overshoot = omega::app::ReduceBootSequence(
        advanced.state,
        BootSequenceInput{.elapsed_ticks = std::numeric_limits<std::uint64_t>::max()});
    const BootSequenceReduction expected_completed{
        .state =
            BootSequenceState{
                .phase = BootSequencePhase::Completed,
                .position_ticks = 10U,
                .duration_ticks = 10U,
            },
        .entered_front_end = true,
    };
    Check(completed_exact == expected_completed && completed_overshoot == expected_completed,
          "exact completion and arbitrary overshoot saturate to the declared "
          "duration");

    const BootSequenceReduction completed_by_source =
        omega::app::ReduceBootSequence(advanced.state, BootSequenceInput{.source_completed = true});
    Check(completed_by_source == expected_completed,
          "decoder completion enters the front end without waiting for the time limit");

    const BootSequenceReduction skipped = omega::app::ReduceBootSequence(
        advanced.state,
        BootSequenceInput{.elapsed_ticks = std::numeric_limits<std::uint64_t>::max(),
                          .primary_pressed = true});
    Check(skipped ==
              BootSequenceReduction{
                  .state =
                      BootSequenceState{
                          .phase = BootSequencePhase::Skipped,
                          .position_ticks = 4U,
                          .duration_ticks = 10U,
                      },
                  .primary_consumed = true,
                  .entered_front_end = true,
              },
          "primary skip has priority over elapsed time and is swallowed once");
    Check(
        omega::app::ReduceBootSequence(skipped.state, BootSequenceInput{.primary_pressed = true}) ==
            BootSequenceReduction{.state = skipped.state},
        "terminal state does not consume a later front-end primary edge");

    const BootSequenceReduction failed =
        omega::app::ReduceBootSequence(advanced.state, BootSequenceInput{.primary_pressed = true,
                                                                         .source_failed = true,
                                                                         .source_completed = true});
    Check(failed ==
              BootSequenceReduction{
                  .state =
                      BootSequenceState{
                          .phase = BootSequencePhase::Failed,
                          .position_ticks = 4U,
                          .duration_ticks = 10U,
                      },
                  .primary_consumed = true,
                  .entered_front_end = true,
              },
          "source failure fails open while swallowing an edge seen during the "
          "modal frame");

    const BootSequenceReduction skipped_at_source_end = omega::app::ReduceBootSequence(
        advanced.state, BootSequenceInput{.primary_pressed = true, .source_completed = true});
    Check(skipped_at_source_end.state.phase == BootSequencePhase::Skipped &&
              skipped_at_source_end.primary_consumed,
          "a primary edge at decoder completion remains consumed by the modal sequence");

    constexpr std::array terminal_phases{
        BootSequencePhase::Completed,
        BootSequencePhase::Skipped,
        BootSequencePhase::Failed,
    };
    bool terminal_states_are_stable = true;
    for (const BootSequencePhase phase : terminal_phases)
    {
        const BootSequenceState terminal{
            .phase = phase,
            .position_ticks = phase == BootSequencePhase::Completed ? 10U : 4U,
            .duration_ticks = 10U,
        };
        terminal_states_are_stable =
            terminal_states_are_stable &&
            omega::app::ReduceBootSequence(
                terminal, BootSequenceInput{
                              .elapsed_ticks = std::numeric_limits<std::uint64_t>::max(),
                              .primary_pressed = true,
                              .source_failed = true,
                          }) == BootSequenceReduction{.state = terminal};
    }
    Check(terminal_states_are_stable, "every terminal phase is stable under all later inputs");

    const std::array malformed_states{
        BootSequenceState{
            .phase = static_cast<BootSequencePhase>(0xffU),
            .position_ticks = 0U,
            .duration_ticks = 10U,
        },
        BootSequenceState{
            .phase = BootSequencePhase::Playing,
            .position_ticks = 10U,
            .duration_ticks = 10U,
        },
        BootSequenceState{
            .phase = BootSequencePhase::Completed,
            .position_ticks = 9U,
            .duration_ticks = 10U,
        },
        BootSequenceState{
            .phase = BootSequencePhase::Skipped,
            .position_ticks = 11U,
            .duration_ticks = 10U,
        },
    };
    bool malformed_fail_open = true;
    for (const BootSequenceState malformed : malformed_states)
    {
        const BootSequenceReduction first =
            omega::app::ReduceBootSequence(malformed, BootSequenceInput{.primary_pressed = true});
        const BootSequenceReduction second =
            omega::app::ReduceBootSequence(malformed, BootSequenceInput{.primary_pressed = true});
        malformed_fail_open = malformed_fail_open && first == second &&
                              first.state.phase == BootSequencePhase::Failed &&
                              !omega::app::IsBootSequenceActive(first.state) &&
                              !first.primary_consumed && first.entered_front_end;
    }
    Check(malformed_fail_open, "malformed states deterministically fail open to the front end");

    if (failures != 0)
    {
        std::cerr << failures << " boot sequence test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "boot sequence tests passed\n";
    return EXIT_SUCCESS;
}
