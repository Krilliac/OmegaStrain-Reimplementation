#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/content/front_end_screen_bundle.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace omega::content
{
// Bounded, aggregate-only summary of a decoded front-end widget tree. Widget
// identifiers are owner-authored retail text, so this boundary never copies an
// identifier from the document. It reports only identifiers already published
// and routed by this project; every other value is reduced to a count. No
// digest, length, prefix, or other character-derived value is retained.
//
// This measures project routing reachability only. It does not assign retail
// menu structure, control layout, ordering rules, activation behavior, or
// screen semantics.
// Give each allowlisted spelling named inline storage. Direct string-literal
// initializers may use distinct literal objects across inline definitions, so
// their data() pointer identity is not a portable borrowing proof.
inline constexpr char kKnownFrontEndNewAgentIdentifier[] = "newagent";
inline constexpr char kKnownFrontEndLoadAgentIdentifier[] = "loadagent";
inline constexpr std::array<std::string_view, 2U> kKnownFrontEndButtonIdentifiers{
    std::string_view{
        kKnownFrontEndNewAgentIdentifier, sizeof(kKnownFrontEndNewAgentIdentifier) - 1U},
    std::string_view{
        kKnownFrontEndLoadAgentIdentifier, sizeof(kKnownFrontEndLoadAgentIdentifier) - 1U},
};

inline constexpr std::uint32_t kFrontEndSurveyMaximumDepth = 64U;
inline constexpr std::uint32_t kFrontEndSurveyMaximumNodes = 65'536U;

struct FrontEndKnownButtonObservation final
{
    // Always borrows the matching repository-owned allowlist entry above,
    // never the surveyed document.
    std::string_view identifier;
    bool present = false;
    // Position among visible buttons in the app's preorder selection walk.
    // Valid only when present; the first occurrence wins.
    std::uint32_t ordinal = 0U;
};

struct FrontEndButtonSurvey final
{
    constexpr FrontEndButtonSurvey() noexcept
    {
        for (std::size_t index = 0U; index < kKnownFrontEndButtonIdentifiers.size(); ++index)
        {
            known[index].identifier = kKnownFrontEndButtonIdentifiers[index];
        }
    }

    std::uint32_t visible_button_count = 0U;
    std::uint32_t unknown_identifier_count = 0U;
    std::uint32_t empty_identifier_count = 0U;
    std::uint32_t duplicate_known_identifier_count = 0U;
    std::uint32_t nodes_visited = 0U;
    // When true, every count and ordinal is only a lower-bound observation.
    bool truncated = false;
    std::array<FrontEndKnownButtonObservation, kKnownFrontEndButtonIdentifiers.size()> known{};
};

namespace detail
{
inline void SurveyFrontEndWidgetSubtree(const asset::FrontendWidgetIR& widget,
                                        const std::uint32_t depth,
                                        FrontEndButtonSurvey& survey) noexcept
{
    if (survey.truncated)
        return;
    if (depth >= kFrontEndSurveyMaximumDepth || survey.nodes_visited >= kFrontEndSurveyMaximumNodes)
    {
        survey.truncated = true;
        return;
    }
    ++survey.nodes_visited;

    // Match OmegaApp::RetailScreenSelectableButtons: a hidden parent hides its
    // whole subtree, so no descendant acquires a selection ordinal.
    if (!widget.visible)
        return;

    if (widget.kind == asset::FrontendWidgetKind::Button)
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
            for (std::size_t index = 0U; index < kKnownFrontEndButtonIdentifiers.size(); ++index)
            {
                if (kKnownFrontEndButtonIdentifiers[index] != widget.identifier)
                    continue;

                matched = true;
                if (survey.known[index].present)
                {
                    ++survey.duplicate_known_identifier_count;
                }
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

// [any thread; reentrant] Borrows the tree for the call, retains nothing,
// allocates nothing, and performs no I/O.
[[nodiscard]] inline FrontEndButtonSurvey SurveyFrontEndVisibleButtons(
    const asset::FrontendWidgetIR& root) noexcept
{
    FrontEndButtonSurvey survey;
    detail::SurveyFrontEndWidgetSubtree(root, 0U, survey);
    return survey;
}

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
    }
    return "unknown";
}

// The owner command iterates exactly the screen roles already declared by the
// current owned API. It does not discover or probe undeclared retail names.
inline constexpr std::array<FrontEndScreenKey, 3U> kAllFrontEndScreenKeys{
    FrontEndScreenKey::Title,
    FrontEndScreenKey::CreateAgent,
    FrontEndScreenKey::LoadAgent,
};
} // namespace omega::content
