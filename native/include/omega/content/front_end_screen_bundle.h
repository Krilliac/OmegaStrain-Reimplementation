#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/asset/indexed_image_ir.h"
#include "omega/content/retail_front_end_presentation_capability.h"
#include "omega/retail/fnt_v3_decoder.h"
#include "omega/retail/retail_string_table_decoder.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace omega::content
{
class GameDataService;
namespace detail
{
struct FrontEndScreenBundleTestAccess;

[[nodiscard]] constexpr bool FrontEndAsciiCaseEqual(
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
}

enum class FrontEndScreenKey
{
    Title,
    CreateAgent,
    LoadAgent,
};

// Proven GS TCC behavior retained at the content/presentation boundary. This
// is deliberately not a bool: a missing/default value cannot be mistaken for
// an observed texture that ignores or consumes palette alpha.
enum class FrontEndTextureAlphaMode : std::uint8_t
{
    IgnoresTextureAlpha,
    UsesPaletteAlpha,
};

// Fully owned renderer-neutral pixels plus the two proven sampling facts that
// survive the retail TDX adapter. Addressing, filtering, upload rectangles,
// GS base pointers, header flags, and palette-transfer state do not cross this
// boundary. Construction is restricted to the content service and generated
// tests so every live value has explicit, encoding-consistent metadata. All
// accessors are [any thread; immutable] while the owning bundle remains live
// and is not moved concurrently.
class FrontEndTextureBinding final
{
public:
    FrontEndTextureBinding() = delete;
    FrontEndTextureBinding(const FrontEndTextureBinding&) = delete;
    FrontEndTextureBinding& operator=(const FrontEndTextureBinding&) = delete;
    FrontEndTextureBinding(FrontEndTextureBinding&&) noexcept = default;
    FrontEndTextureBinding& operator=(FrontEndTextureBinding&&) noexcept = default;
    ~FrontEndTextureBinding() = default;

    [[nodiscard]] const asset::IndexedImageIR& image() const noexcept { return image_; }
    [[nodiscard]] asset::IndexedImageEncoding sampling_encoding() const noexcept
    {
        return sampling_encoding_;
    }
    [[nodiscard]] FrontEndTextureAlphaMode alpha_mode() const noexcept
    {
        return alpha_mode_;
    }

    [[nodiscard]] bool operator==(const FrontEndTextureBinding& other) const noexcept
    {
        return image_ == other.image_ && sampling_encoding_ == other.sampling_encoding_ &&
               alpha_mode_ == other.alpha_mode_;
    }

private:
    FrontEndTextureBinding(asset::IndexedImageIR image,
        const asset::IndexedImageEncoding sampling_encoding,
        const FrontEndTextureAlphaMode alpha_mode) noexcept
        : image_(std::move(image)), sampling_encoding_(sampling_encoding),
          alpha_mode_(alpha_mode)
    {
        // A mismatch can only be introduced by content-service code or the
        // friend test seam, never by owner input after the TDX decoder passed.
        if (image_.source_encoding != sampling_encoding_)
            std::terminate();
    }

    asset::IndexedImageIR image_;
    asset::IndexedImageEncoding sampling_encoding_;
    FrontEndTextureAlphaMode alpha_mode_;

    friend class GameDataService;
    friend struct detail::FrontEndScreenBundleTestAccess;
};

// One case-insensitive retail visual scope, keyed by its normalized scope name
// in FrontEndScreenBundle. The decoded document remains fully owned. Only
// resource identifiers proven reachable from the paired GUI are exposed by
// FindResource; lookup is exact and case-sensitive, matching the retail DFS.
class FrontEndVisualScope final
{
public:
    using TextureMap = std::map<std::string, FrontEndTextureBinding, std::less<>>;
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

    // [any thread; immutable] Resolves one normalized archive member only
    // within this exact owning scope. Member matching folds ASCII case like
    // the frozen archive index; no same-named texture in another scope can
    // satisfy this lookup.
    [[nodiscard]] const FrontEndTextureBinding* FindTexture(
        const std::string_view member) const noexcept
    {
        for (const auto& [normalized_member, texture] : textures_)
        {
            if (detail::FrontEndAsciiCaseEqual(normalized_member, member))
                return &texture;
        }
        return nullptr;
    }

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
    friend struct detail::FrontEndScreenBundleTestAccess;
};

// Borrowed result of a scope-qualified lookup. Both the normalized owning
// scope and texture value remain tied to the immutable live bundle; neither
// may outlive, race a move of, or outlive destruction of that bundle.
class ResolvedFrontEndTextureBinding final
{
public:
    ResolvedFrontEndTextureBinding() = delete;
    ResolvedFrontEndTextureBinding(const ResolvedFrontEndTextureBinding&) noexcept = default;
    ResolvedFrontEndTextureBinding& operator=(
        const ResolvedFrontEndTextureBinding&) noexcept = default;

    [[nodiscard]] std::string_view owning_scope() const noexcept
    {
        return *owning_scope_;
    }
    [[nodiscard]] const FrontEndVisualScope& scope() const noexcept { return *scope_; }
    [[nodiscard]] const FrontEndTextureBinding& texture() const noexcept { return *texture_; }

private:
    ResolvedFrontEndTextureBinding(const std::string& owning_scope,
        const FrontEndVisualScope& scope,
        const FrontEndTextureBinding& texture) noexcept
        : owning_scope_(&owning_scope), scope_(&scope), texture_(&texture)
    {
    }

    const std::string* owning_scope_;
    const FrontEndVisualScope* scope_;
    const FrontEndTextureBinding* texture_;

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
        const auto match = FindVisualScopeEntry(authored_scope);
        return match == visual_scopes_.end() ? nullptr : &match->second;
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

    // [any thread; immutable] Resolves one member inside an explicitly selected
    // authored scope and returns the normalized scope with the texture. Empty
    // selects the primary scope. This never performs a global same-name lookup.
    [[nodiscard]] std::optional<ResolvedFrontEndTextureBinding> ResolveTextureBinding(
        const std::string_view authored_scope,
        const std::string_view texture_member) const noexcept
    {
        const auto match = FindVisualScopeEntry(authored_scope);
        if (match == visual_scopes_.end())
            return std::nullopt;
        const auto* texture = match->second.FindTexture(texture_member);
        if (texture == nullptr)
            return std::nullopt;
        return ResolvedFrontEndTextureBinding(match->first, match->second, *texture);
    }

    // [any thread; immutable] Resolves the widget's proven visual resource and
    // then its declared texture in that same owning scope. An untextured
    // resource, missing metadata, or cross-scope same-name candidate returns no
    // value.
    [[nodiscard]] std::optional<ResolvedFrontEndTextureBinding>
    ResolveVisualTextureBinding(
        const asset::FrontendWidgetIR& widget, const bool parentless) const noexcept
    {
        if (!widget.binding)
            return std::nullopt;
        const auto match = FindVisualScopeEntry(widget.binding->scope_reference);
        if (match == visual_scopes_.end())
            return std::nullopt;
        const std::string_view resource = widget.binding->resource_reference.empty()
            ? std::string_view(widget.identifier)
            : std::string_view(widget.binding->resource_reference);
        const auto* visual = parentless ? match->second.FindRootResource(resource)
                                        : match->second.FindResource(resource);
        if (visual == nullptr || !visual->texture_member)
            return std::nullopt;
        const auto* texture = match->second.FindTexture(*visual->texture_member);
        if (texture == nullptr)
            return std::nullopt;
        return ResolvedFrontEndTextureBinding(match->first, match->second, *texture);
    }
    [[nodiscard]] const TextureMap& screen_textures() const noexcept
    {
        return visual_scopes_.find(primary_scope_)->second.textures();
    }
    [[nodiscard]] const FontMap& fonts() const noexcept { return fonts_; }

    // Applies the retail font-manager lookup rule to an authored GUI value.
    // Empty or absent references select the registered default. Named fonts
    // accept their authored basename or full .FNT member name and fold ASCII
    // case only; a font not retained by this bundle fails closed.
    [[nodiscard]] const retail::FntV3IR* ResolveFont(
        const std::string_view authored_reference) const noexcept
    {
        constexpr std::string_view default_member = "DEFAULT.FNT";
        constexpr std::string_view suffix = ".FNT";
        const std::string_view reference = authored_reference.empty()
            ? default_member
            : authored_reference;
        for (const auto& [normalized_member, font] : fonts_)
        {
            const std::string_view member(normalized_member);
            if (detail::FrontEndAsciiCaseEqual(member, reference))
                return &font;
            if (!authored_reference.empty() &&
                authored_reference.find('.') == std::string_view::npos &&
                member.size() == authored_reference.size() + suffix.size() &&
                detail::FrontEndAsciiCaseEqual(member.substr(0U, authored_reference.size()),
                    authored_reference) &&
                detail::FrontEndAsciiCaseEqual(
                    member.substr(authored_reference.size()), suffix))
            {
                return &font;
            }
        }
        return nullptr;
    }

    [[nodiscard]] const retail::FntV3IR* ResolveFontReference(
        const std::optional<std::string>& authored_reference) const noexcept
    {
        return ResolveFont(authored_reference
                ? std::string_view(*authored_reference)
                : std::string_view{});
    }

    [[nodiscard]] const TextureMap& font_atlases() const noexcept
    {
        return font_atlases_;
    }
    // [any thread; immutable] Resolves only within the retained font-archive
    // atlas domain; visual-scope texture maps are never searched.
    [[nodiscard]] const FrontEndTextureBinding* ResolveFontAtlas(
        const retail::FntV3IR& font) const noexcept
    {
        for (const auto& [normalized_member, atlas] : font_atlases_)
        {
            if (detail::FrontEndAsciiCaseEqual(normalized_member, font.atlas_reference))
                return &atlas;
        }
        return nullptr;
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
    [[nodiscard]] VisualScopeMap::const_iterator FindVisualScopeEntry(
        const std::string_view authored_scope) const noexcept
    {
        if (authored_scope.empty())
            return visual_scopes_.find(primary_scope_);
        for (auto match = visual_scopes_.cbegin(); match != visual_scopes_.cend(); ++match)
        {
            if (detail::FrontEndAsciiCaseEqual(match->first, authored_scope))
                return match;
        }
        return visual_scopes_.end();
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
    friend struct detail::FrontEndScreenBundleTestAccess;
};
} // namespace omega::content
