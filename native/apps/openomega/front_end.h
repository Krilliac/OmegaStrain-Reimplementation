#pragma once

#include "omega/profiles/profile_catalog.h"
#include "omega/runtime/content_startup.h"
#include "omega/runtime/texture_storage_topology_debug_image.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <type_traits>

namespace omega::app
{
// Project-owned logical actions used by the synthetic native front-end. They
// are not retail input identifiers or control-layout claims. Actions 2 and 3
// remain shared with diagnostic locomotion; action 6 remains the sole
// primary/confirm edge.
inline constexpr std::uint32_t kFrontEndPrimaryAction = 6U;
inline constexpr std::uint32_t kFrontEndPreviousAction = 2U;
inline constexpr std::uint32_t kFrontEndNextAction = 3U;

inline constexpr std::size_t kFrontEndMaximumProfiles = 1'024U;
inline constexpr std::size_t kFrontEndVisibleProfiles = 3U;
inline constexpr std::size_t kFrontEndLabelCells = 24U;
inline constexpr std::size_t kFrontEndMainRowCount = 4U;
inline constexpr std::uint32_t kFrontEndImageWidth = 128U;
inline constexpr std::uint32_t kFrontEndImageHeight = 72U;

enum class FrontEndMode : std::uint8_t
{
    Main = 0U,
    Profiles = 1U,
    DiagnosticPlay = 2U,
    Controls = 3U,
    AssetTopology = 4U,
};

enum class FrontEndMainRow : std::uint8_t
{
    StartDiagnostic = 0U,
    Profiles = 1U,
    Controls = 2U,
    AssetTopology = 3U,
};

// Fixed project-font cells. Unused cells remain NUL-filled; length is
// authoritative and never exceeds the array extent. Projection uppercases ASCII
// a-z, preserves supported project-font ASCII, maps each other Unicode scalar
// to one '?', and never truncates within a scalar.
struct FrontEndLabel
{
    std::array<char, kFrontEndLabelCells> cells{};
    std::uint8_t length = 0U;
    bool truncated = false;

    friend constexpr bool operator==(const FrontEndLabel &, const FrontEndLabel &) noexcept = default;
};

// Immutable bounded snapshot copied from the ID-sorted native catalog before
// SDL startup. It contains no borrowed string, catalog, database, or renderer
// lifetime.
struct FrontEndStartupModel
{
    std::uint16_t total_profiles = 0U;
    std::uint8_t visible_profiles = 0U;
    std::array<FrontEndLabel, kFrontEndVisibleProfiles> profiles{};

    friend constexpr bool operator==(const FrontEndStartupModel &, const FrontEndStartupModel &) noexcept = default;
};

enum class FrontEndModelError : std::uint8_t
{
    TooManyProfiles = 0U,
    UnsortedProfiles = 1U,
};

[[nodiscard]] constexpr std::string_view FrontEndModelErrorMessage(const FrontEndModelError error) noexcept
{
    switch (error)
    {
    case FrontEndModelError::TooManyProfiles:
        return "front-end profile snapshot exceeds the fixed profile limit";
    case FrontEndModelError::UnsortedProfiles:
        return "front-end profile snapshot is not strictly ID-sorted";
    }
    return "front-end profile snapshot exceeds the fixed profile limit";
}

// [persistence/game thread, before SDL] Copies at most 1024 strictly ascending
// ProfileSummary records into a fixed front-end snapshot. It performs no
// allocation, I/O, catalog access, or mutation and never echoes input data
// through its fixed error taxonomy.
[[nodiscard]] std::expected<FrontEndStartupModel, FrontEndModelError> MakeFrontEndStartupModel(
    std::span<const profiles::ProfileSummary> summaries) noexcept;

// Small owned app-layer value. It has no service, platform, renderer, database,
// or retail-data lifetime. The default remains the safe DiagnosticPlay state
// used by legacy nonmodal replay.
struct FrontEndState
{
    FrontEndMode mode = FrontEndMode::DiagnosticPlay;
    FrontEndMainRow selected_main_row = FrontEndMainRow::StartDiagnostic;

    friend constexpr bool operator==(const FrontEndState &, const FrontEndState &) noexcept = default;
};

struct FrontEndInputEdges
{
    bool primary_pressed = false;
    bool previous_pressed = false;
    bool next_pressed = false;

    friend constexpr bool operator==(const FrontEndInputEdges &, const FrontEndInputEdges &) noexcept = default;
};

struct FrontEndView
{
    FrontEndMode mode = FrontEndMode::Main;
    FrontEndMainRow selected_main_row = FrontEndMainRow::StartDiagnostic;
    runtime::ContentStartupStage content_stage = runtime::ContentStartupStage::NoContent;
    FrontEndStartupModel profiles{};

    friend constexpr bool operator==(const FrontEndView &, const FrontEndView &) noexcept = default;
};

// [any thread; reentrant] Explicit startup state for the project-owned static
// front-end.
[[nodiscard]] constexpr FrontEndState InitialFrontEndState() noexcept
{
    return FrontEndState{
        .mode = FrontEndMode::Main,
        .selected_main_row = FrontEndMainRow::StartDiagnostic,
    };
}

[[nodiscard]] constexpr bool IsValidFrontEndState(const FrontEndState state) noexcept
{
    const bool valid_mode = state.mode == FrontEndMode::Main || state.mode == FrontEndMode::Profiles ||
                            state.mode == FrontEndMode::DiagnosticPlay || state.mode == FrontEndMode::Controls ||
                            state.mode == FrontEndMode::AssetTopology;
    const bool valid_row = state.selected_main_row == FrontEndMainRow::StartDiagnostic ||
                           state.selected_main_row == FrontEndMainRow::Profiles ||
                           state.selected_main_row == FrontEndMainRow::Controls ||
                           state.selected_main_row == FrontEndMainRow::AssetTopology;
    return valid_mode && valid_row;
}

// [any thread; reentrant] Consumes already-routed logical press edges. Invalid
// input state fails closed to the initial front-end before considering any
// edge. Primary has priority over navigation; each project screen returns to
// its own main row; simultaneous navigation edges are neutral; navigation is
// press-edge-only and clamps at both bounds.
[[nodiscard]] constexpr FrontEndState UpdateFrontEnd(FrontEndState state, const FrontEndInputEdges input) noexcept
{
    if (!IsValidFrontEndState(state))
        return InitialFrontEndState();

    if (input.primary_pressed)
    {
        switch (state.mode)
        {
        case FrontEndMode::Profiles:
            return FrontEndState{
                .mode = FrontEndMode::Main,
                .selected_main_row = FrontEndMainRow::Profiles,
            };
        case FrontEndMode::DiagnosticPlay:
            return InitialFrontEndState();
        case FrontEndMode::Controls:
            return FrontEndState{
                .mode = FrontEndMode::Main,
                .selected_main_row = FrontEndMainRow::Controls,
            };
        case FrontEndMode::AssetTopology:
            return FrontEndState{
                .mode = FrontEndMode::Main,
                .selected_main_row = FrontEndMainRow::AssetTopology,
            };
        case FrontEndMode::Main:
            break;
        }

        switch (state.selected_main_row)
        {
        case FrontEndMainRow::StartDiagnostic:
            state.mode = FrontEndMode::DiagnosticPlay;
            break;
        case FrontEndMainRow::Profiles:
            state.mode = FrontEndMode::Profiles;
            break;
        case FrontEndMainRow::Controls:
            state.mode = FrontEndMode::Controls;
            break;
        case FrontEndMainRow::AssetTopology:
            state.mode = FrontEndMode::AssetTopology;
            break;
        }
        return state;
    }

    if (state.mode != FrontEndMode::Main || input.previous_pressed == input.next_pressed)
        return state;

    const std::uint8_t row = static_cast<std::uint8_t>(state.selected_main_row);
    if (input.previous_pressed && row > 0U)
    {
        state.selected_main_row = static_cast<FrontEndMainRow>(row - 1U);
    }
    else if (input.next_pressed && row + 1U < kFrontEndMainRowCount)
    {
        state.selected_main_row = static_cast<FrontEndMainRow>(row + 1U);
    }
    return state;
}

// [any thread; reentrant] Simulation is enabled only by a fully valid
// DiagnosticPlay state.
[[nodiscard]] constexpr bool FrontEndAllowsSimulation(const FrontEndState state) noexcept
{
    return IsValidFrontEndState(state) && state.mode == FrontEndMode::DiagnosticPlay;
}

// [any thread; reentrant] Normalizes invalid state to InitialFrontEndState and
// copies only fixed values. The returned view has no borrowed lifetime and
// performs no allocation or I/O.
[[nodiscard]] constexpr FrontEndView BuildFrontEndView(const FrontEndState state,
                                                       const runtime::ContentStartupStage content_stage,
                                                       const FrontEndStartupModel &profiles) noexcept
{
    const FrontEndState normalized = IsValidFrontEndState(state) ? state : InitialFrontEndState();
    const runtime::ContentStartupStage normalized_stage =
        content_stage == runtime::ContentStartupStage::DataMounted ||
                content_stage == runtime::ContentStartupStage::LevelContent
            ? content_stage
            : runtime::ContentStartupStage::NoContent;
    return FrontEndView{
        .mode = normalized.mode,
        .selected_main_row = normalized.selected_main_row,
        .content_stage = normalized_stage,
        .profiles = profiles,
    };
}

// [any thread; reentrant] Fully owned project-generated opaque RGBA8 cards.
// These builders perform no I/O and consume no platform object, decoded retail
// asset, or emulator data.
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndMainImage(runtime::ContentStartupStage content_stage,
                                                                std::uint16_t profile_count);
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndProfilesImage(const FrontEndStartupModel &profiles);
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndDiagnosticPlayImage();
[[nodiscard]] runtime::DebugImage BuildProjectFrontEndControlsImage();

// Builds the exact project-owned E-0066 three-block topology fixture and
// returns its fully owned metadata-only diagnostic image.
[[nodiscard]] std::expected<runtime::DebugImage, runtime::TextureStorageTopologyDebugImageError>
BuildProjectFrontEndAssetTopologyImage();

static_assert(std::is_trivially_copyable_v<FrontEndLabel>);
static_assert(std::is_standard_layout_v<FrontEndLabel>);
static_assert(std::is_trivially_copyable_v<FrontEndStartupModel>);
static_assert(std::is_standard_layout_v<FrontEndStartupModel>);
static_assert(std::is_trivially_copyable_v<FrontEndState>);
static_assert(std::is_standard_layout_v<FrontEndState>);
static_assert(std::is_trivially_copyable_v<FrontEndInputEdges>);
static_assert(std::is_standard_layout_v<FrontEndInputEdges>);
static_assert(std::is_trivially_copyable_v<FrontEndView>);
static_assert(std::is_standard_layout_v<FrontEndView>);
static_assert(sizeof(FrontEndMode) == 1U);
static_assert(sizeof(FrontEndMainRow) == 1U);
static_assert(sizeof(FrontEndModelError) == 1U);
} // namespace omega::app
