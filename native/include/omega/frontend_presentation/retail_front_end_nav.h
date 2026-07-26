#pragma once

#include "omega/content/front_end_screen_bundle.h"

#include <cstdint>
#include <optional>

namespace omega::frontend::presentation
{
// Bounded pure navigation model for the retail front end. It owns only the
// current screen and a selection index; it carries no bundle, renderer, or
// persistence lifetime and performs no I/O. OmegaApp drives it each frame from
// resolved input edges and recomposes the retail draw list when the returned
// state differs.
//
// This is deliberately separate from the project-owned ReduceFrontEnd machine
// (front_end.h): the retail path switches decoded screen BUNDLES rather than
// running the capability-gated profile/character flow, so it must not fire the
// project's persistence commands (e.g. an Accept on the Title's third row must
// never RequestQuit here).
struct RetailFrontEndNavState final
{
    content::FrontEndScreenKey screen = content::FrontEndScreenKey::Title;
    std::uint32_t selected = 0U;

    friend constexpr bool operator==(const RetailFrontEndNavState&,
        const RetailFrontEndNavState&) noexcept = default;
};

// Resolved menu edges for one frame. previous/next move the selection;
// accept follows the selected button; back returns to the Title screen.
struct RetailFrontEndNavInput final
{
    bool previous = false;
    bool next = false;
    bool accept = false;
    bool back = false;

    friend constexpr bool operator==(const RetailFrontEndNavInput&,
        const RetailFrontEndNavInput&) noexcept = default;
};

// [any thread; reentrant] Pure accept-target admission policy.
//
// `button_target` is the screen the currently-selected button declares, or
// nullopt when it declares none. `target_screen_is_presentable` reports whether
// the caller can actually compose that screen right now (for OmegaApp: whether
// its bundle resolves). The target survives only when an Accept edge is present
// AND the screen is presentable; otherwise Accept becomes inert.
//
// This exists because StepRetailFrontEndNav commits a screen switch
// unconditionally once it is handed a target. Without this admission step a
// caller that discovers, only after the switch, that it cannot compose the new
// screen leaves navigation and pixels disagreeing: the player sees the previous
// screen's last composed frame while the selection lives on a screen that never
// draws, and Accept is inert off the Title, so only a Back edge recovers.
// Refusing the transition keeps the player on a screen that still composes and
// still responds.
//
// Fail-closed refusal is project-owned policy for an unpresentable screen. It
// asserts no retail transition rule, no retail screen availability, and no
// retail behaviour for a screen this build cannot yet compose.
//
// `accept_requested` is taken so a caller can pass its live Accept edge and let
// this function decide; callers that probe availability lazily should probe only
// when that edge is set. No allocation or I/O occurs.
[[nodiscard]] constexpr std::optional<content::FrontEndScreenKey>
ResolveRetailFrontEndAcceptTarget(
    const std::optional<content::FrontEndScreenKey> button_target,
    const bool accept_requested, const bool target_screen_is_presentable) noexcept
{
    if (!accept_requested || !button_target || !target_screen_is_presentable)
        return std::nullopt;
    return button_target;
}

// [any thread; reentrant] Pure retail front-end navigation step.
//
// Priority: back, then accept, then navigation (matching the reducer's
// cancel>primary>navigation order). `button_count` is the number of selectable
// buttons on the CURRENT screen; the selection is always clamped into
// [0, button_count) (or 0 when the screen has none). `accept_target` is the
// screen the currently-selected Title button routes to, or nullopt when it has
// no target (e.g. Options) -- the caller derives it from the bundle's buttons.
// Accept only switches screens from the Title; back only leaves a non-Title
// screen. Simultaneous previous+next is neutral. No allocation or I/O occurs.
[[nodiscard]] constexpr RetailFrontEndNavState StepRetailFrontEndNav(
    RetailFrontEndNavState state, const RetailFrontEndNavInput input,
    const std::uint32_t button_count,
    const std::optional<content::FrontEndScreenKey> accept_target) noexcept
{
    // Clamp an incoming stale selection before acting on it.
    if (button_count == 0U)
        state.selected = 0U;
    else if (state.selected >= button_count)
        state.selected = button_count - 1U;

    if (input.back)
    {
        if (state.screen != content::FrontEndScreenKey::Title)
            return RetailFrontEndNavState{
                .screen = content::FrontEndScreenKey::Title, .selected = 0U};
        return state;
    }

    if (input.accept)
    {
        if (state.screen == content::FrontEndScreenKey::Title && accept_target)
            return RetailFrontEndNavState{.screen = *accept_target, .selected = 0U};
        return state;
    }

    if (input.previous == input.next)
        return state;
    if (button_count == 0U)
        return state;

    if (input.previous && state.selected > 0U)
        --state.selected;
    else if (input.next && state.selected + 1U < button_count)
        ++state.selected;
    return state;
}
} // namespace omega::frontend::presentation
