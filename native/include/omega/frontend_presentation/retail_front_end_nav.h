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

// How far an attempt to put a candidate screen on screen actually got.
//
// A loaded bundle is NOT a presentable screen. Decoding a bundle and composing
// it into a frame are separate steps that fail for separate reasons, and
// uploading that frame is a third. Only `Published` means a candidate reached
// the display, so only `Published` may move navigation.
enum class RetailFrontEndPresentOutcome : std::uint8_t
{
    NotAttempted = 0U,
    // The candidate screen's decoded bundle could not be obtained.
    BundleUnavailable = 1U,
    // The bundle was in hand but the compositor produced no frame.
    ComposeFailed = 2U,
    // A frame existed but did not reach a resident texture and draw list.
    PublishFailed = 3U,
    // Pixels and draw list are live. This is the only committing outcome.
    Published = 4U,
};

// [any thread; reentrant] Whether the caller should spend a bundle load on this
// frame's Accept.
//
// Loading is the expensive, failure-prone step, so it happens only for an
// explicit player Accept that could actually route somewhere: Back outranks
// Accept in StepRetailFrontEndNav, and Accept only switches screens from the
// Title, so an Accept accompanied by Back, an Accept away from the Title, and an
// Accept on a button that declares no target must all cost nothing.
//
// Because probing is confined to an Accept edge, a screen that fails to load is
// retried on the player's NEXT Accept rather than every frame or never again.
// No allocation or I/O occurs.
[[nodiscard]] constexpr bool ShouldProbeRetailAcceptTarget(
    const RetailFrontEndNavState state, const RetailFrontEndNavInput input,
    const std::optional<content::FrontEndScreenKey> button_target) noexcept
{
    return state.screen == content::FrontEndScreenKey::Title && input.accept &&
           !input.back && button_target.has_value();
}

// What the caller must do with a candidate once it has tried to present it.
struct RetailFrontEndNavCommit final
{
    // The navigation state to keep. The candidate only on success, otherwise the
    // caller's existing state, untouched.
    RetailFrontEndNavState nav{};
    // True only when the candidate published. The caller's composed-navigation
    // marker and animation state may advance if and only if this is true.
    bool commit = false;
    // True when an explicit player Accept was attempted and did not publish.
    // Exactly one fixed warning belongs to such an attempt.
    bool warn = false;
};

// [any thread; reentrant] The single commit rule for retail navigation.
//
// THE INVARIANT: navigation and the composed-navigation marker advance if and
// only if the candidate actually published. Anything short of `Published` keeps
// the caller's current navigation, composed marker, animation state, texture and
// draw list exactly as they were, so what the player sees and what navigation
// believes can never disagree, and the next frame retries from a coherent state.
//
// Warning is tied to `explicit_attempt` rather than to failure alone: a screen
// that fails to compose while animating would otherwise emit a line per rendered
// frame, whereas a player pressing Accept deserves one line per press.
[[nodiscard]] constexpr RetailFrontEndNavCommit ResolveRetailFrontEndNavCommit(
    const RetailFrontEndNavState current, const RetailFrontEndNavState candidate,
    const RetailFrontEndPresentOutcome outcome,
    const bool explicit_attempt) noexcept
{
    if (outcome == RetailFrontEndPresentOutcome::Published)
        return RetailFrontEndNavCommit{.nav = candidate, .commit = true, .warn = false};
    return RetailFrontEndNavCommit{
        .nav = current, .commit = false, .warn = explicit_attempt};
}

// [any thread; reentrant] Maps a debug start-screen override spelling to a
// declared screen key. An unrecognized spelling yields no key, and the caller
// must then compose the Title rather than leaving navigation on a screen it
// never resolved. Matching is exact; this recognizes only spellings this
// repository already publishes and never guesses a screen name.
[[nodiscard]] constexpr std::optional<content::FrontEndScreenKey>
ResolveRetailStartScreenOverride(const std::string_view requested) noexcept
{
    if (requested == "createagent")
        return content::FrontEndScreenKey::CreateAgent;
    if (requested == "loadagent")
        return content::FrontEndScreenKey::LoadAgent;
    if (requested == "commandcenter")
        return content::FrontEndScreenKey::CommandCenter;
    if (requested == "equipment")
        return content::FrontEndScreenKey::Equipment;
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

// [any thread; reentrant] Builds the CANDIDATE navigation for this frame.
//
// The result is deliberately not the caller's live state: it is what navigation
// would become if presenting it succeeds. StepRetailFrontEndNav switches screens
// unconditionally once handed a target, so the target is admitted here only when
// the caller already holds the destination's decoded bundle -- and even then the
// caller must still present the candidate, and adopt it only through
// ResolveRetailFrontEndNavCommit.
//
// `accept_target_is_admitted` means "the destination bundle is loaded", nothing
// more. It is not a claim that the destination composes, uploads, or renders.
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
