#pragma once

#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <type_traits>

namespace omega::app
{
// Project-owned logical action used only by the synthetic native diagnostic host. It is not a
// retail input identifier or control-layout claim.
inline constexpr std::uint32_t kDiagnosticMenuToggleAction = 6U;
inline constexpr std::uint32_t kDiagnosticMenuPrimaryAction =
    kDiagnosticMenuToggleAction;
// These menu edges intentionally reuse the existing project-owned forward/backward diagnostic
// action numbers. Keeping the numeric relationship here avoids an app-core dependency on the
// SDL-host replay header and establishes no retail input or control-layout meaning.
inline constexpr std::uint32_t kDiagnosticMenuPreviousAction = 2U;
inline constexpr std::uint32_t kDiagnosticMenuNextAction = 3U;
inline constexpr std::uint32_t kDiagnosticMenuRowCount = 3U;
inline constexpr std::uint32_t kDiagnosticMenuImageWidth = 128U;
inline constexpr std::uint32_t kDiagnosticMenuImageHeight = 72U;

enum class DiagnosticMenuMode : std::uint8_t
{
    MainMenu = 0U,
    DiagnosticPlay = 1U,
    Controls = 2U,
};

enum class DiagnosticMenuRow : std::uint8_t
{
    StartDiagnosticPlay = 0U,
    ShowControls = 1U,
    ReservedProjectTwo = 2U,
};

// Small owned app-layer value. It has no service, platform, renderer, or retail-data lifetime.
struct DiagnosticMenuState
{
    DiagnosticMenuMode mode = DiagnosticMenuMode::DiagnosticPlay;
    DiagnosticMenuRow selected_row = DiagnosticMenuRow::StartDiagnosticPlay;

    friend constexpr bool operator==(
        const DiagnosticMenuState&, const DiagnosticMenuState&) noexcept = default;
};

struct DiagnosticMenuInputEdges
{
    bool primary_pressed = false;
    bool previous_pressed = false;
    bool next_pressed = false;

    friend constexpr bool operator==(
        const DiagnosticMenuInputEdges&, const DiagnosticMenuInputEdges&) noexcept = default;
};

// [any thread; reentrant] Explicit startup state for the project-owned main menu. The default
// state remains the safe diagnostic-play value used by inert or default-constructed owners.
[[nodiscard]] constexpr DiagnosticMenuState InitialDiagnosticMenuState() noexcept
{
    return DiagnosticMenuState{
        .mode = DiagnosticMenuMode::MainMenu,
        .selected_row = DiagnosticMenuRow::StartDiagnosticPlay,
    };
}

// [any thread; reentrant] Simulation is enabled only by a fully valid diagnostic-play state.
// Invalid enum values fail closed even when the mode byte resembles DiagnosticPlay.
[[nodiscard]] constexpr bool DiagnosticMenuAllowsSimulation(
    const DiagnosticMenuState state) noexcept
{
    if (state.mode != DiagnosticMenuMode::DiagnosticPlay)
        return false;

    switch (state.selected_row)
    {
    case DiagnosticMenuRow::StartDiagnosticPlay:
    case DiagnosticMenuRow::ShowControls:
    case DiagnosticMenuRow::ReservedProjectTwo:
        return true;
    }
    return false;
}

// [any thread; reentrant] Consumes already-routed logical press edges. Invalid input state fails
// closed to the initial main menu before considering any edge. Primary has priority over
// navigation; the controls screen returns to its main-menu row on primary; simultaneous
// previous/next edges are neutral; main-menu navigation clamps at both bounds.
[[nodiscard]] constexpr DiagnosticMenuState UpdateDiagnosticMenu(
    DiagnosticMenuState state, const DiagnosticMenuInputEdges input) noexcept
{
    const bool valid_mode = state.mode == DiagnosticMenuMode::MainMenu ||
                            state.mode == DiagnosticMenuMode::DiagnosticPlay ||
                            state.mode == DiagnosticMenuMode::Controls;
    const bool valid_row = state.selected_row == DiagnosticMenuRow::StartDiagnosticPlay ||
                           state.selected_row == DiagnosticMenuRow::ShowControls ||
                           state.selected_row == DiagnosticMenuRow::ReservedProjectTwo;
    if (!valid_mode || !valid_row)
        return InitialDiagnosticMenuState();

    if (input.primary_pressed)
    {
        if (state.mode == DiagnosticMenuMode::DiagnosticPlay)
            return InitialDiagnosticMenuState();
        if (state.mode == DiagnosticMenuMode::Controls)
        {
            return DiagnosticMenuState{
                .mode = DiagnosticMenuMode::MainMenu,
                .selected_row = DiagnosticMenuRow::ShowControls,
            };
        }
        if (state.selected_row == DiagnosticMenuRow::StartDiagnosticPlay)
            state.mode = DiagnosticMenuMode::DiagnosticPlay;
        else if (state.selected_row == DiagnosticMenuRow::ShowControls)
            state.mode = DiagnosticMenuMode::Controls;
        return state;
    }

    if (state.mode != DiagnosticMenuMode::MainMenu ||
        input.previous_pressed == input.next_pressed)
    {
        return state;
    }

    if (input.previous_pressed)
    {
        if (state.selected_row == DiagnosticMenuRow::ReservedProjectTwo)
            state.selected_row = DiagnosticMenuRow::ShowControls;
        else if (state.selected_row == DiagnosticMenuRow::ShowControls)
            state.selected_row = DiagnosticMenuRow::StartDiagnosticPlay;
    }
    else
    {
        if (state.selected_row == DiagnosticMenuRow::StartDiagnosticPlay)
            state.selected_row = DiagnosticMenuRow::ShowControls;
        else if (state.selected_row == DiagnosticMenuRow::ShowControls)
            state.selected_row = DiagnosticMenuRow::ReservedProjectTwo;
    }
    return state;
}

// [any thread; reentrant] Returns a fully owned, project-generated opaque RGBA8 diagnostic card.
// It performs no file I/O and consumes no platform object, decoded asset, or retail input.
[[nodiscard]] runtime::DebugImage BuildProjectDiagnosticMenuImage();

// [any thread; reentrant] Returns a fully owned, project-generated opaque RGBA8 controls card.
// It performs no file I/O and consumes no platform object, decoded asset, or retail input.
[[nodiscard]] runtime::DebugImage BuildProjectDiagnosticControlsImage();

static_assert(std::is_trivially_copyable_v<DiagnosticMenuState>);
static_assert(std::is_standard_layout_v<DiagnosticMenuState>);
static_assert(std::is_trivially_copyable_v<DiagnosticMenuInputEdges>);
static_assert(std::is_standard_layout_v<DiagnosticMenuInputEdges>);
static_assert(std::is_trivially_copyable_v<DiagnosticMenuMode>);
static_assert(std::is_standard_layout_v<DiagnosticMenuMode>);
static_assert(std::is_trivially_copyable_v<DiagnosticMenuRow>);
static_assert(std::is_standard_layout_v<DiagnosticMenuRow>);
} // namespace omega::app
