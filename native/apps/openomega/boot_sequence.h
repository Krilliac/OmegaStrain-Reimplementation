#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace omega::app
{
// Project-owned presentation timebase. It is independent of emulator frames,
// retail stream timestamps, the renderer, and wall-clock precision.
inline constexpr std::uint64_t kBootSequenceTicksPerSecond = 1'000'000U;

enum class BootSequencePhase : std::uint8_t
{
    Playing = 0U,
    Completed = 1U,
    Skipped = 2U,
    Failed = 3U,
};

enum class BootSequenceCompletionCause : std::uint8_t
{
    None = 0U,
    SourceCompleted = 1U,
    SafetyTimeout = 2U,
};

struct BootSequenceConfig
{
    std::uint64_t duration_ticks = 0U;
    bool source_available = false;

    friend constexpr bool operator==(const BootSequenceConfig&,
                                     const BootSequenceConfig&) noexcept = default;
};

struct BootSequenceState
{
    // The zero/default state deliberately preserves the existing no-content
    // startup behavior by entering the front end immediately.
    BootSequencePhase phase = BootSequencePhase::Completed;
    std::uint64_t position_ticks = 0U;
    std::uint64_t duration_ticks = 0U;

    friend constexpr bool operator==(const BootSequenceState&,
                                     const BootSequenceState&) noexcept = default;
};

struct BootSequenceInput
{
    std::uint64_t elapsed_ticks = 0U;
    bool primary_pressed = false;
    bool source_failed = false;
    bool source_completed = false;

    friend constexpr bool operator==(const BootSequenceInput&,
                                     const BootSequenceInput&) noexcept = default;
};

struct BootSequenceReduction
{
    BootSequenceState state{};
    // Distinguishes normal decoder/source completion from the independent
    // safety deadline on the exact transition frame. Non-completion terminal
    // paths and later stable reductions report None.
    BootSequenceCompletionCause completion_cause = BootSequenceCompletionCause::None;
    // A primary edge observed while a valid sequence was active is never
    // forwarded into the front end on the same frame.
    bool primary_consumed = false;
    bool entered_front_end = false;

    friend constexpr bool operator==(const BootSequenceReduction&,
                                     const BootSequenceReduction&) noexcept = default;
};

[[nodiscard]] constexpr bool IsValidBootSequencePhase(const BootSequencePhase phase) noexcept
{
    return phase == BootSequencePhase::Playing || phase == BootSequencePhase::Completed ||
           phase == BootSequencePhase::Skipped || phase == BootSequencePhase::Failed;
}

[[nodiscard]] constexpr bool IsValidBootSequenceState(const BootSequenceState state) noexcept
{
    if (!IsValidBootSequencePhase(state.phase))
        return false;
    if (state.position_ticks > state.duration_ticks)
        return false;
    if (state.phase == BootSequencePhase::Playing)
        return state.duration_ticks != 0U && state.position_ticks < state.duration_ticks;
    if (state.phase == BootSequencePhase::Completed)
        return state.duration_ticks == 0U || state.position_ticks == state.duration_ticks;
    return true;
}

[[nodiscard]] constexpr bool IsBootSequenceActive(const BootSequenceState state) noexcept
{
    return IsValidBootSequenceState(state) && state.phase == BootSequencePhase::Playing;
}

// [any thread; reentrant] Missing, empty, or unsupported content deliberately
// preserves the historical immediate-menu startup path.
[[nodiscard]] constexpr BootSequenceState InitialBootSequenceState(
    const BootSequenceConfig config) noexcept
{
    if (!config.source_available || config.duration_ticks == 0U)
        return {};
    return BootSequenceState{
        .phase = BootSequencePhase::Playing,
        .position_ticks = 0U,
        .duration_ticks = config.duration_ticks,
    };
}

// [any thread; reentrant] Pure modal reducer. Invalid state and decoder/source
// failure fail open to the front end. Decoder completion and natural timed
// completion clamp exactly to the declared duration. A primary edge skips an
// active sequence and is swallowed for that frame; terminal states never
// consume later menu input.
[[nodiscard]] constexpr BootSequenceReduction ReduceBootSequence(
    const BootSequenceState state, const BootSequenceInput input) noexcept
{
    const bool was_playing = IsBootSequenceActive(state);
    if (!IsValidBootSequenceState(state))
    {
        return BootSequenceReduction{
            .state =
                BootSequenceState{
                    .phase = BootSequencePhase::Failed,
                    .position_ticks = std::min(state.position_ticks, state.duration_ticks),
                    .duration_ticks = state.duration_ticks,
                },
            .primary_consumed = false,
            .entered_front_end = true,
        };
    }

    if (!was_playing)
        return BootSequenceReduction{.state = state};

    if (input.source_failed)
    {
        return BootSequenceReduction{
            .state =
                BootSequenceState{
                    .phase = BootSequencePhase::Failed,
                    .position_ticks = state.position_ticks,
                    .duration_ticks = state.duration_ticks,
                },
            .primary_consumed = input.primary_pressed,
            .entered_front_end = true,
        };
    }

    if (input.primary_pressed)
    {
        return BootSequenceReduction{
            .state =
                BootSequenceState{
                    .phase = BootSequencePhase::Skipped,
                    .position_ticks = state.position_ticks,
                    .duration_ticks = state.duration_ticks,
                },
            .primary_consumed = true,
            .entered_front_end = true,
        };
    }

    if (input.source_completed)
    {
        return BootSequenceReduction{
            .state =
                BootSequenceState{
                    .phase = BootSequencePhase::Completed,
                    .position_ticks = state.duration_ticks,
                    .duration_ticks = state.duration_ticks,
                },
            .completion_cause = BootSequenceCompletionCause::SourceCompleted,
            .entered_front_end = true,
        };
    }

    const std::uint64_t remaining = state.duration_ticks - state.position_ticks;
    const std::uint64_t advanced = input.elapsed_ticks >= remaining
                                       ? state.duration_ticks
                                       : state.position_ticks + input.elapsed_ticks;
    if (advanced == state.duration_ticks)
    {
        return BootSequenceReduction{
            .state =
                BootSequenceState{
                    .phase = BootSequencePhase::Completed,
                    .position_ticks = state.duration_ticks,
                    .duration_ticks = state.duration_ticks,
                },
            .completion_cause = BootSequenceCompletionCause::SafetyTimeout,
            .entered_front_end = true,
        };
    }

    return BootSequenceReduction{
        .state =
            BootSequenceState{
                .phase = BootSequencePhase::Playing,
                .position_ticks = advanced,
                .duration_ticks = state.duration_ticks,
            },
    };
}

static_assert(std::is_trivially_copyable_v<BootSequenceConfig>);
static_assert(std::is_standard_layout_v<BootSequenceConfig>);
static_assert(std::is_trivially_copyable_v<BootSequenceState>);
static_assert(std::is_standard_layout_v<BootSequenceState>);
static_assert(std::is_trivially_copyable_v<BootSequenceInput>);
static_assert(std::is_standard_layout_v<BootSequenceInput>);
static_assert(std::is_trivially_copyable_v<BootSequenceReduction>);
static_assert(std::is_standard_layout_v<BootSequenceReduction>);
static_assert(sizeof(BootSequencePhase) == 1U);
static_assert(sizeof(BootSequenceCompletionCause) == 1U);
} // namespace omega::app
