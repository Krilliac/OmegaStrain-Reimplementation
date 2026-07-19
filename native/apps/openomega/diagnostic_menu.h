#pragma once

#include "omega/runtime/debug_image.h"

#include <cstdint>
#include <type_traits>

namespace omega::app
{
// Project-owned logical action used only by the synthetic native diagnostic host. It is not a
// retail input identifier or control-layout claim.
inline constexpr std::uint32_t kDiagnosticMenuToggleAction = 6U;
inline constexpr std::uint32_t kDiagnosticMenuImageWidth = 128U;
inline constexpr std::uint32_t kDiagnosticMenuImageHeight = 72U;

// Small owned app-layer value. It has no service, platform, renderer, or retail-data lifetime.
struct DiagnosticMenuState
{
    bool visible = false;

    friend constexpr bool operator==(
        const DiagnosticMenuState&, const DiagnosticMenuState&) noexcept = default;
};

// [any thread; reentrant] toggle_pressed is an already-routed logical press edge. A false edge
// preserves the value; a true edge performs exactly one visibility transition.
[[nodiscard]] constexpr DiagnosticMenuState UpdateDiagnosticMenu(
    DiagnosticMenuState state, const bool toggle_pressed) noexcept
{
    if (toggle_pressed)
        state.visible = !state.visible;
    return state;
}

// [any thread; reentrant] Returns a fully owned, project-generated opaque RGBA8 diagnostic card.
// It performs no file I/O and consumes no platform object, decoded asset, or retail input.
[[nodiscard]] runtime::DebugImage BuildProjectDiagnosticMenuImage();

static_assert(std::is_trivially_copyable_v<DiagnosticMenuState>);
static_assert(std::is_standard_layout_v<DiagnosticMenuState>);
} // namespace omega::app
