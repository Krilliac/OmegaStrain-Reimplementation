#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/content/front_end_screen_bundle.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace omega::content
{
// Bounded, pure summary of a decoded front-end widget document, built so an
// owner can report what a real disc produces WITHOUT sending back anything that
// is not already public in this repository.
//
// Privacy rule, and the reason this type exists at all: a widget identifier is
// free-form text authored into retail data. This survey therefore never copies
// an identifier out of the document. It only answers, for each identifier this
// repository ALREADY contains and already routes on, "is it here, and where in
// order". Every other visible button is reduced to an anonymous count. Nothing
// derived from an identifier's characters -- no digest, no length, no prefix --
// leaves this boundary, so a report cannot be used to recover a name or act as
// a fingerprint of proprietary content.
//
// What this measures is the project's own routing reachability. It assigns no
// retail menu structure, control layout, ordering rule, or screen semantic.

// The identifiers this build already knows and acts on. Kept exactly in step
// with OmegaApp::RetailScreenSelectableButtons, which maps these same two
// strings to their destination screens; matching is exact and case-sensitive
// there, so it is exact and case-sensitive here and the survey reports what the
// app would really route rather than a looser approximation.
//
// NOTE: these two literals are currently duplicated here and in
// omega_app.cpp. Folding that routing table onto this array is a follow-up;
// until it lands the two lists must be edited together.
inline constexpr std::array<std::string_view, 2U> kKnownFrontEndButtonIdentifiers{
    "newagent",
    "loadagent",
};

// Fixed traversal ceilings. Owner input reaches this walk already decoded, but
// a survey must not be the thing that turns a deep or wide document into a
// stack overflow or an unbounded run, so both are capped and the cap is
// reported rather than hidden.
inline constexpr std::uint32_t kFrontEndSurveyMaximumDepth = 64U;
inline constexpr std::uint32_t kFrontEndSurveyMaximumNodes = 65'536U;

// One allowlisted identifier's observation. `identifier` always borrows from
// kKnownFrontEndButtonIdentifiers -- never from the surveyed document -- so this
// value holds no owner-authored text even when the document does.
struct FrontEndKnownButtonObservation final
{
    std::string_view identifier;
    bool present = false;
    // Position among visible buttons in document preorder, valid only when
    // `present`. First occurrence wins; a repeat raises the duplicate count.
    std::uint32_t ordinal = 0U;
};

// Aggregate-only result. Every field is a count, a flag, or an allowlisted
// identifier this repository already published.
struct FrontEndButtonSurvey final
{
    std::uint32_t visible_button_count = 0U;
    // Visible buttons carrying a non-empty identifier that is not allowlisted.
    // Counted, never named: this is the number of routes still unmapped.
    std::uint32_t unknown_identifier_count = 0U;
    // Visible buttons carrying no identifier at all.
    std::uint32_t empty_identifier_count = 0U;
    // Extra occurrences of an already-seen allowlisted identifier.
    std::uint32_t duplicate_known_identifier_count = 0U;
    // Nodes visited, so a caller can see the walk's size and tell a genuinely
    // small screen from one cut short.
    std::uint32_t nodes_visited = 0U;
    // True when a ceiling stopped the walk, making every count above a lower
    // bound rather than a total. Never silently absorbed.
    bool truncated = false;
    std::array<FrontEndKnownButtonObservation, kKnownFrontEndButtonIdentifiers.size()>
        known{};
};

namespace detail
{
// Depth-first PREORDER (self, then children), which is the order
// OmegaApp::RetailScreenSelectableButtons uses to index the selection, so an
// ordinal reported here is the selection index the app would use.
inline void SurveyFrontEndWidgetSubtree(const asset::FrontendWidgetIR& widget,
    const std::uint32_t depth, FrontEndButtonSurvey& survey) noexcept
{
    if (survey.truncated)
        return;
    if (depth >= kFrontEndSurveyMaximumDepth ||
        survey.nodes_visited >= kFrontEndSurveyMaximumNodes)
    {
        survey.truncated = true;
        return;
    }
    ++survey.nodes_visited;

    if (widget.visible && widget.kind == asset::FrontendWidgetKind::Button)
    {
        const std::uint32_t ordinal = survey.visible_button_count;
        ++survey.visible_button_count;

        if (widget.identifier.empty())
        {
            ++survey.empty_identifier_count;
        }
        else
        {
            bool matched = false;
            for (std::size_t index = 0U;
                 index < kKnownFrontEndButtonIdentifiers.size(); ++index)
            {
                // string_view on the left so template deduction is direct and
                // the std::string converts on the non-deduced side.
                if (kKnownFrontEndButtonIdentifiers[index] != widget.identifier)
                    continue;
                matched = true;
                if (survey.known[index].present)
                    ++survey.duplicate_known_identifier_count;
                else
                {
                    survey.known[index].present = true;
                    survey.known[index].ordinal = ordinal;
                }
                break;
            }
            if (!matched)
                ++survey.unknown_identifier_count;
        }
    }

    for (const auto& child : widget.children)
    {
        SurveyFrontEndWidgetSubtree(child, depth + 1U, survey);
        if (survey.truncated)
            return;
    }
}
} // namespace detail

// [any thread; reentrant] Surveys one decoded widget tree. Borrows the tree for
// the call, retains nothing, allocates nothing, and performs no I/O. The
// returned value is safe to print verbatim: it contains no path, no payload
// byte, no digest, and no owner-authored string.
[[nodiscard]] inline FrontEndButtonSurvey SurveyFrontEndVisibleButtons(
    const asset::FrontendWidgetIR& root) noexcept
{
    FrontEndButtonSurvey survey;
    for (std::size_t index = 0U; index < kKnownFrontEndButtonIdentifiers.size();
         ++index)
    {
        survey.known[index].identifier = kKnownFrontEndButtonIdentifiers[index];
    }
    detail::SurveyFrontEndWidgetSubtree(root, 0U, survey);
    return survey;
}

// Stable wire name for the survey's own key spelling. Kept separate from the
// enumerator names so a future rename of either cannot silently change a
// report's schema.
[[nodiscard]] constexpr std::string_view FrontEndScreenSurveyKeyName(
    const FrontEndScreenKey key) noexcept
{
    switch (key)
    {
    case FrontEndScreenKey::Title:
        return "title";
    case FrontEndScreenKey::CreateAgent:
        return "create_agent";
    case FrontEndScreenKey::LoadAgent:
        return "load_agent";
    case FrontEndScreenKey::CommandCenter:
        return "command_center";
    case FrontEndScreenKey::Equipment:
        return "equipment";
    }
    return "unknown";
}

// Every declared screen key, in declaration order. The survey command iterates
// exactly this list: it never probes a name, member, or key that this
// repository has not already declared, so the command cannot become a search
// for undiscovered retail content.
inline constexpr std::array<FrontEndScreenKey, 5U> kAllFrontEndScreenKeys{
    FrontEndScreenKey::Title,
    FrontEndScreenKey::CreateAgent,
    FrontEndScreenKey::LoadAgent,
    FrontEndScreenKey::CommandCenter,
    FrontEndScreenKey::Equipment,
};
} // namespace omega::content
