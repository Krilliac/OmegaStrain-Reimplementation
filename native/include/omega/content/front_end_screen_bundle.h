#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/asset/indexed_image_ir.h"
#include "omega/content/retail_front_end_presentation_capability.h"
#include "omega/retail/fnt_v3_decoder.h"
#include "omega/retail/retail_string_table_decoder.h"

#include <cstddef>
#include <exception>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace omega::content
{
class GameDataService;

enum class FrontEndScreenKey
{
    Title,
    CreateAgent,
    LoadAgent,
};

// One case-insensitive retail visual scope, keyed by its normalized scope name
// in FrontEndScreenBundle. The decoded document remains fully owned. Only
// resource identifiers proven reachable from the paired GUI are exposed by
// FindResource; lookup is exact and case-sensitive, matching the retail DFS.
class FrontEndVisualScope final
{
public:
    using TextureMap = std::map<std::string, asset::IndexedImageIR, std::less<>>;
    using ResourceSet = std::set<std::string, std::less<>>;

    FrontEndVisualScope() = delete;
    FrontEndVisualScope(const FrontEndVisualScope&) = delete;
    FrontEndVisualScope& operator=(const FrontEndVisualScope&) = delete;
    FrontEndVisualScope(FrontEndVisualScope&&) noexcept = default;
    FrontEndVisualScope& operator=(FrontEndVisualScope&&) noexcept = default;
    ~FrontEndVisualScope() = default;

    [[nodiscard]] const asset::FrontendVisualDocumentIR& document() const noexcept
    {
        return document_;
    }
    [[nodiscard]] const ResourceSet& resources() const noexcept { return resources_; }
    [[nodiscard]] const TextureMap& textures() const noexcept { return textures_; }

    // Returns only GUI-bound resources. An identifier with different ASCII
    // case does not match, and an unbound node in the same document is hidden.
    [[nodiscard]] const asset::FrontendVisualNodeIR* FindResource(
        std::string_view exact_identifier) const noexcept
    {
        if (resources_.find(exact_identifier) == resources_.end())
            return nullptr;
        return FindExact(document_.root, exact_identifier);
    }

private:
    FrontEndVisualScope(asset::FrontendVisualDocumentIR document,
        ResourceSet resources, TextureMap textures) noexcept
        : document_(std::move(document)), resources_(std::move(resources)),
          textures_(std::move(textures))
    {
    }

    [[nodiscard]] static const asset::FrontendVisualNodeIR* FindExact(
        const asset::FrontendVisualNodeIR& node,
        const std::string_view exact_identifier) noexcept
    {
        if (node.identifier == exact_identifier)
            return &node;
        for (const auto& child : node.children)
        {
            if (const auto* match = FindExact(child, exact_identifier); match != nullptr)
                return match;
        }
        return nullptr;
    }

    [[nodiscard]] const asset::FrontendVisualNodeIR* FindRootResource(
        const std::string_view identifier_without_suffix) const noexcept
    {
        constexpr std::string_view suffix = "_root";
        for (const auto& resource : resources_)
        {
            if (resource.size() >= suffix.size() &&
                resource.size() - suffix.size() == identifier_without_suffix.size() &&
                std::string_view(resource).starts_with(identifier_without_suffix) &&
                std::string_view(resource).ends_with(suffix))
            {
                return FindExact(document_.root, resource);
            }
        }
        return nullptr;
    }

    asset::FrontendVisualDocumentIR document_;
    ResourceSet resources_;
    TextureMap textures_;

    friend class GameDataService;
    friend class FrontEndScreenBundle;
};

// Fully owned canonical presentation data for one retail front-end screen. The
// game-data service is the only constructor: ordinary runtime code can consume
// or move this value, but cannot fabricate retail presentation evidence.
class FrontEndScreenBundle final
{
public:
    using TextureMap = FrontEndVisualScope::TextureMap;
    using FontMap = std::map<std::string, retail::FntV3IR, std::less<>>;
    using VisualScopeMap = std::map<std::string, FrontEndVisualScope, std::less<>>;

    FrontEndScreenBundle() = delete;
    FrontEndScreenBundle(const FrontEndScreenBundle&) = delete;
    FrontEndScreenBundle& operator=(const FrontEndScreenBundle&) = delete;
    // A moved-from value is destructible/assignable only; content accessors
    // retain their live-bundle precondition.
    FrontEndScreenBundle(FrontEndScreenBundle&&) noexcept = default;
    FrontEndScreenBundle& operator=(FrontEndScreenBundle&&) noexcept = default;

    [[nodiscard]] FrontEndScreenKey key() const noexcept { return key_; }
    [[nodiscard]] const asset::FrontendWidgetDocumentIR& widget_document() const noexcept
    {
        return widget_document_;
    }
    [[nodiscard]] const asset::FrontendVisualDocumentIR& visual_document() const noexcept
    {
        return visual_scopes_.find(primary_scope_)->second.document();
    }
    [[nodiscard]] const std::string& primary_scope() const noexcept { return primary_scope_; }
    [[nodiscard]] const VisualScopeMap& visual_scopes() const noexcept { return visual_scopes_; }

    // Scope strings come directly from authored GUI bindings. Empty selects the
    // primary screen scope; nonempty matching folds ASCII case only. Consumers
    // never need to reproduce archive-path normalization or cache key rules.
    [[nodiscard]] const FrontEndVisualScope* FindVisualScope(
        const std::string_view authored_scope) const noexcept
    {
        if (authored_scope.empty())
            return &visual_scopes_.find(primary_scope_)->second;
        for (const auto& [normalized_scope, scope] : visual_scopes_)
        {
            if (AsciiCaseEqual(normalized_scope, authored_scope))
                return &scope;
        }
        return nullptr;
    }

    // Applies the complete validated decorator lookup rule: empty scope uses
    // the primary scope, empty resource uses the wrapped widget identifier,
    // and a parentless widget appends `_root` before exact DFS lookup.
    [[nodiscard]] const asset::FrontendVisualNodeIR* ResolveVisualBinding(
        const asset::FrontendWidgetIR& widget, const bool parentless) const noexcept
    {
        if (!widget.binding)
            return nullptr;
        const auto* scope = FindVisualScope(widget.binding->scope_reference);
        if (scope == nullptr)
            return nullptr;
        const std::string_view resource = widget.binding->resource_reference.empty()
            ? std::string_view(widget.identifier)
            : std::string_view(widget.binding->resource_reference);
        return parentless ? scope->FindRootResource(resource)
                          : scope->FindResource(resource);
    }
    [[nodiscard]] const TextureMap& screen_textures() const noexcept
    {
        return visual_scopes_.find(primary_scope_)->second.textures();
    }
    [[nodiscard]] const FontMap& fonts() const noexcept { return fonts_; }
    [[nodiscard]] const TextureMap& font_atlases() const noexcept
    {
        return font_atlases_;
    }
    [[nodiscard]] const retail::RetailStringTableIR& strings() const noexcept
    {
        return strings_;
    }
    [[nodiscard]] const RetailFrontEndPresentationCapability&
    presentation_capability() const noexcept
    {
        return presentation_capability_;
    }

private:
    [[nodiscard]] static bool AsciiCaseEqual(
        const std::string_view left, const std::string_view right) noexcept
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            const auto fold = [](const unsigned char value) noexcept {
                return value >= 'a' && value <= 'z'
                    ? static_cast<unsigned char>(value - ('a' - 'A'))
                    : value;
            };
            if (fold(static_cast<unsigned char>(left[index])) !=
                fold(static_cast<unsigned char>(right[index])))
            {
                return false;
            }
        }
        return true;
    }

    FrontEndScreenBundle(FrontEndScreenKey key,
        asset::FrontendWidgetDocumentIR widget_document,
        std::string primary_scope, VisualScopeMap visual_scopes,
        FontMap fonts, TextureMap font_atlases,
        retail::RetailStringTableIR strings,
        RetailFrontEndPresentationCapability presentation_capability) noexcept
        : key_(key),
          widget_document_(std::move(widget_document)),
          primary_scope_(std::move(primary_scope)),
          visual_scopes_(std::move(visual_scopes)),
          fonts_(std::move(fonts)),
          font_atlases_(std::move(font_atlases)),
          strings_(std::move(strings)),
          presentation_capability_(std::move(presentation_capability))
    {
        // Private-construction invariant for the noexcept primary-scope views.
        // A violation is an internal programming error, never owner-data input.
        if (primary_scope_.empty() || visual_scopes_.count(primary_scope_) != 1U)
        {
            std::terminate();
        }
    }

    FrontEndScreenKey key_ = FrontEndScreenKey::Title;
    asset::FrontendWidgetDocumentIR widget_document_;
    std::string primary_scope_;
    VisualScopeMap visual_scopes_;
    FontMap fonts_;
    TextureMap font_atlases_;
    retail::RetailStringTableIR strings_;
    RetailFrontEndPresentationCapability presentation_capability_;

    friend class GameDataService;
};
} // namespace omega::content
