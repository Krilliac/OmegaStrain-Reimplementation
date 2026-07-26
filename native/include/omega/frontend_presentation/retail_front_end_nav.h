#pragma once

#include "omega/content/front_end_screen_bundle.h"

#include <cstdint>
#include <optional>
#include <string_view>

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

// How far an attempt to present a candidate screen progressed. A decoded bundle
// is not itself a presentable screen; navigation may advance only after the
// composed pixels have reached a resident texture and live draw list.
enum class RetailFrontEndPresentOutcome : std::uint8_t
{
    NotAttempted = 0U,
    BundleUnavailable = 1U,
    ComposeFailed = 2U,
    PublishFailed = 3U,
    Published = 4U,
};

// [any thread; reentrant] True only for an explicit Title-screen Accept that can
// route. Loading is confined to that input edge so a failed decode is retried on
// the next Accept rather than every rendered frame.
[[nodiscard]] constexpr bool ShouldProbeRetailAcceptTarget(
    const RetailFrontEndNavState state, const RetailFrontEndNavInput input,
    const std::optional<content::FrontEndScreenKey> button_target) noexcept
{
    return state.screen == content::FrontEndScreenKey::Title && input.accept &&
           !input.back && button_target.has_value();
}

struct RetailFrontEndNavCommit final
{
    RetailFrontEndNavState nav{};
    bool commit = false;
    bool warn = false;
};

// [any thread; reentrant] Keeps navigation and the composed-navigation marker
// paired with the pixels on screen. Only a published candidate may commit;
// failed explicit Accept attempts additionally request one fixed warning.
[[nodiscard]] constexpr RetailFrontEndNavCommit ResolveRetailFrontEndNavCommit(
    const RetailFrontEndNavState current, const RetailFrontEndNavState candidate,
    const RetailFrontEndPresentOutcome outcome,
    const bool explicit_attempt) noexcept
{
    if (outcome == RetailFrontEndPresentOutcome::Published)
        return RetailFrontEndNavCommit{.nav = candidate, .commit = true};
    return RetailFrontEndNavCommit{
        .nav = current, .commit = false, .warn = explicit_attempt};
}

// [any thread; reentrant] Exact spellings supported by the bounded three-screen
// developer preview. Unknown spellings are deliberately not guessed.
[[nodiscard]] constexpr std::optional<content::FrontEndScreenKey>
ResolveRetailStartScreenOverride(const std::string_view requested) noexcept
{
    if (requested == "createagent")
        return content::FrontEndScreenKey::CreateAgent;
    if (requested == "loadagent")
        return content::FrontEndScreenKey::LoadAgent;
    return std::nullopt;
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

// [any thread; reentrant] Builds a local candidate. A destination is admitted
// only after its bundle loaded; publication is still required before the caller
// may adopt the result.
[[nodiscard]] constexpr RetailFrontEndNavState PlanRetailFrontEndNavCandidate(
    const RetailFrontEndNavState state, const RetailFrontEndNavInput input,
    const std::uint32_t button_count,
    const std::optional<content::FrontEndScreenKey> button_target,
    const bool accept_target_is_admitted) noexcept
{
    return StepRetailFrontEndNav(state, input, button_count,
        accept_target_is_admitted ? button_target
                                  : std::optional<content::FrontEndScreenKey>{});
}
} // namespace omega::frontend::presentation
