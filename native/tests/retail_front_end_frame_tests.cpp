#include "omega/frontend_presentation/retail_front_end_frame.h"
#include "omega/frontend_presentation/retail_mission_ring.h"

#include "omega/frontend_presentation/retail_front_end_nav.h"

#include "omega/asset/frontend_ir.h"
#include "omega/asset/indexed_image_ir.h"
#include "omega/content/front_end_screen_bundle.h"
#include "omega/content/retail_front_end_presentation_capability.h"
#include "omega/frontend/compositor_math.h"
#include "omega/retail/fnt_v3_decoder.h"
#include "omega/retail/retail_string_table_decoder.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Phase 3: pure retail navigation model (compile-time coverage).
namespace nav_test
{
using omega::content::FrontEndScreenKey;
using omega::frontend::presentation::RetailFrontEndNavInput;
using omega::frontend::presentation::RetailFrontEndNavState;
using omega::frontend::presentation::StepRetailFrontEndNav;

// next advances the selection; previous retreats it; both clamp to [0, count).
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 0U},
                  RetailFrontEndNavInput{.next = true}, 3U, std::nullopt)
                  .selected == 1U);
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 2U},
                  RetailFrontEndNavInput{.next = true}, 3U, std::nullopt)
                  .selected == 2U);
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 1U},
                  RetailFrontEndNavInput{.previous = true}, 3U, std::nullopt)
                  .selected == 0U);
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 0U},
                  RetailFrontEndNavInput{.previous = true}, 3U, std::nullopt)
                  .selected == 0U);
// Simultaneous previous+next is neutral.
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 1U},
                  RetailFrontEndNavInput{.previous = true, .next = true}, 3U, std::nullopt)
                  .selected == 1U);
// Accept on the Title with a routed target switches screen and resets selection.
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 0U},
                  RetailFrontEndNavInput{.accept = true}, 3U,
                  FrontEndScreenKey::CreateAgent)
                  .screen == FrontEndScreenKey::CreateAgent);
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 0U},
                  RetailFrontEndNavInput{.accept = true}, 3U,
                  FrontEndScreenKey::CreateAgent)
                  .selected == 0U);
// Accept with no target (e.g. Options) stays put.
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 2U},
                  RetailFrontEndNavInput{.accept = true}, 3U, std::nullopt)
                  .screen == FrontEndScreenKey::Title);
// Back leaves a sub-screen for the Title; back on the Title stays.
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::CreateAgent, 0U},
                  RetailFrontEndNavInput{.back = true}, 1U, std::nullopt)
                  .screen == FrontEndScreenKey::Title);
static_assert(StepRetailFrontEndNav(RetailFrontEndNavState{FrontEndScreenKey::Title, 1U},
                  RetailFrontEndNavInput{.back = true}, 3U, std::nullopt)
                  .screen == FrontEndScreenKey::Title);
} // namespace nav_test

// Minimal test-only construction access, mirroring the pattern in
// frontend_presentation_tests.cpp. Each test executable defines its own copy;
// there is no cross-TU ODR concern because tests link as separate binaries.
namespace omega::content::detail
{
struct RetailFrontEndPresentationCapabilityTestAccess final
{
    [[nodiscard]] static RetailFrontEndPresentationCapability Make(
        const bool valid = true) noexcept
    {
        RetailFrontEndPresentationCapability capability(
            RetailFrontEndPresentationCapability::ConstructionKey{});
        if (!valid)
        {
            RetailFrontEndPresentationCapability consumed(std::move(capability));
            (void)consumed;
        }
        return capability;
    }
};

struct FrontEndScreenBundleTestAccess final
{
    [[nodiscard]] static FrontEndVisualScope MakeScope(
        asset::FrontendVisualDocumentIR document,
        FrontEndVisualScope::ResourceSet resources,
        FrontEndVisualScope::TextureMap textures)
    {
        return FrontEndVisualScope(
            std::move(document), std::move(resources), std::move(textures));
    }

    [[nodiscard]] static FrontEndScreenBundle MakeBundle(
        const FrontEndScreenKey key,
        asset::FrontendWidgetDocumentIR widget_document,
        std::string primary_scope,
        FrontEndScreenBundle::VisualScopeMap visual_scopes,
        RetailFrontEndPresentationCapability capability)
    {
        return FrontEndScreenBundle(key, std::move(widget_document),
            std::move(primary_scope), std::move(visual_scopes), {}, {}, {},
            std::move(capability));
    }

    [[nodiscard]] static FrontEndTextureBinding MakeTextureBinding(
        asset::IndexedImageIR image,
        const asset::IndexedImageEncoding sampling_encoding,
        const FrontEndTextureAlphaMode alpha_mode)
    {
        return FrontEndTextureBinding(
            std::move(image), sampling_encoding, alpha_mode);
    }

    [[nodiscard]] static FrontEndScreenBundle MakeBundleWithFonts(
        const FrontEndScreenKey key,
        asset::FrontendWidgetDocumentIR widget_document,
        std::string primary_scope,
        FrontEndScreenBundle::VisualScopeMap visual_scopes,
        FrontEndScreenBundle::FontMap fonts,
        FrontEndScreenBundle::TextureMap font_atlases,
        retail::RetailStringTableIR strings,
        RetailFrontEndPresentationCapability capability)
    {
        return FrontEndScreenBundle(key, std::move(widget_document),
            std::move(primary_scope), std::move(visual_scopes),
            std::move(fonts), std::move(font_atlases), std::move(strings),
            std::move(capability));
    }
};
} // namespace omega::content::detail

namespace
{
using omega::asset::Float3IR;
using omega::asset::FrontendColorRgba8IR;
using omega::asset::FrontendTriangleIR;
using omega::asset::FrontendVisualNodeIR;
using omega::asset::FrontendWidgetBindingIR;
using omega::asset::FrontendWidgetIR;
using omega::asset::FrontendWidgetKind;
using omega::content::FrontEndScreenBundle;
using omega::content::FrontEndScreenKey;
using omega::content::FrontEndVisualScope;
using omega::content::detail::FrontEndScreenBundleTestAccess;
using omega::content::detail::RetailFrontEndPresentationCapabilityTestAccess;
using omega::frontend::kIdentityAffineTransform12;
using omega::frontend::presentation::ComposeRetailFrontEndFrame;
using omega::frontend::presentation::RetailFrontEndFrameError;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

// A four-corner quad in interface-element space (x horizontal, y vertical,
// z the depth axis) with two triangles sharing one uv and one color. This matches
// the real retail convention: ComposeRetailFrontEndFrame applies the axis bridge
// so screen vertical comes from Y and depth from Z. Depth Z is a constant here.
[[nodiscard]] FrontendVisualNodeIR MakeQuad(std::string identifier,
    const float half_width, const float half_height,
    const FrontendColorRgba8IR color)
{
    return FrontendVisualNodeIR{
        .identifier = std::move(identifier),
        .transform_values = kIdentityAffineTransform12.column_vectors,
        .positions = {
            Float3IR{.x = -half_width, .y = half_height, .z = 17.0F},
            Float3IR{.x = half_width, .y = half_height, .z = 17.0F},
            Float3IR{.x = half_width, .y = -half_height, .z = 17.0F},
            Float3IR{.x = -half_width, .y = -half_height, .z = 17.0F},
        },
        .uvs = {{.u = 0.0F, .v = 0.0F}},
        .colors = {color},
        .triangles = {
            FrontendTriangleIR{
                .position_indices = {0U, 1U, 2U},
                .uv_indices = {0U, 0U, 0U},
                .color_indices = {0U, 0U, 0U},
            },
            FrontendTriangleIR{
                .position_indices = {0U, 2U, 3U},
                .uv_indices = {0U, 0U, 0U},
                .color_indices = {0U, 0U, 0U},
            },
        },
    };
}

[[nodiscard]] FrontendWidgetIR MakeWidget(std::string identifier)
{
    return FrontendWidgetIR{
        .kind = FrontendWidgetKind::Container,
        .identifier = std::move(identifier),
        .rectangle = {.left = -320.0F, .top = 224.0F, .width = 640.0F, .height = 448.0F},
        .visible = true,
        .enabled = true,
        .binding = FrontendWidgetBindingIR{
            .transform_values = kIdentityAffineTransform12.column_vectors,
        },
    };
}

// Root full-screen quad (color A) plus one immediate child quad centered on the
// canvas (color B). The root widget resolves parentless -> "ROOT_root"; the
// child widget resolves by exact identifier -> "LOGO".
[[nodiscard]] FrontEndScreenBundle MakeRootPlusChildBundle(
    const FrontendColorRgba8IR root_color,
    const FrontendColorRgba8IR child_color,
    const bool valid_capability = true,
    const bool add_degenerate_triangle = false)
{
    FrontendVisualNodeIR root_visual =
        MakeQuad("ROOT_root", 320.0F, 224.0F, root_color);
    if (add_degenerate_triangle)
    {
        // A zero-area triangle (all three vertices reference position 0). Real
        // retail IE meshes carry such helper/padding triangles; the compositor
        // must skip them fail-soft, not reject the whole screen.
        root_visual.triangles.push_back(FrontendTriangleIR{
            .position_indices = {0U, 0U, 0U},
            .uv_indices = {0U, 0U, 0U},
            .color_indices = {0U, 0U, 0U},
        });
    }
    root_visual.children.push_back(
        MakeQuad("LOGO", 80.0F, 56.0F, child_color));

    omega::asset::FrontendVisualDocumentIR document{.root = std::move(root_visual)};
    auto scope = FrontEndScreenBundleTestAccess::MakeScope(
        std::move(document),
        FrontEndVisualScope::ResourceSet{"ROOT_root", "LOGO"},
        FrontEndVisualScope::TextureMap{});
    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(scope));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    root_widget.children.push_back(MakeWidget("LOGO"));

    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(valid_capability));
}

// Root quad plus a centred child quad whose OPACITY track fades it from fully
// opaque at tick 0 to fully transparent at tick 20 (Phase 3b animation). At tick 0
// the opaque child covers the root in its region; at tick 20 it is transparent and
// the root shows through, so the two composed frames differ. Nodes without tracks
// are tick-invariant (verified separately with MakeRootPlusChildBundle).
[[nodiscard]] FrontEndScreenBundle MakeAnimatedOpacityBundle()
{
    FrontendVisualNodeIR root_visual = MakeQuad("ROOT_root", 320.0F, 224.0F,
        FrontendColorRgba8IR{.red = 16U, .green = 16U, .blue = 16U, .alpha = 255U});
    FrontendVisualNodeIR child = MakeQuad("LOGO", 120.0F, 90.0F,
        FrontendColorRgba8IR{.red = 240U, .green = 48U, .blue = 48U, .alpha = 255U});
    child.animation_tracks.push_back(omega::asset::FrontendScalarAnimationTrackIR{
        .target = omega::asset::FrontendScalarAnimationTarget::Opacity,
        .keys = {
            omega::asset::FrontendScalarAnimationKeyIR{
                .timeline_tick = 0.0F, .value = 1.0F},
            omega::asset::FrontendScalarAnimationKeyIR{
                .timeline_tick = 20.0F, .value = 0.0F},
        },
    });
    root_visual.children.push_back(std::move(child));

    omega::asset::FrontendVisualDocumentIR document{.root = std::move(root_visual)};
    auto scope = FrontEndScreenBundleTestAccess::MakeScope(std::move(document),
        FrontEndVisualScope::ResourceSet{"ROOT_root", "LOGO"},
        FrontEndVisualScope::TextureMap{});
    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(scope));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    root_widget.children.push_back(MakeWidget("LOGO"));

    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}

// ---- Widget-binding placement fixtures ------------------------------------

// A pure translation in the twelve-coefficient column-vector form: identity
// basis, translation in C3. Used for both widget bindings and node-local
// transforms, which take the same form (FrontendWidgetBindingIR documents both,
// and both compose parent * local).
//
// Everything below stays in interface-element space (x horizontal, y vertical,
// z depth) because the compositor applies kInterfaceElementAxisBridge exactly
// once, at the top of the chain. A point that ends at interface-element
// (x, y, z) therefore rasterizes at (320 + x, 223 - y).
[[nodiscard]] std::array<float, 12> TranslationTransform(
    const float translate_x, const float translate_y, const float translate_z)
{
    return std::array<float, 12>{
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
        translate_x, translate_y, translate_z,
    };
}

struct PlacedChildOptions final
{
    // Authors the child widget vis=0, which must cull the ART it owns as well as
    // its text -- and must not hand the node back to its IE parent's walk.
    bool visible_child = true;
    // Moves the child's art into a SECOND visual scope that the child widget
    // names, the way a screen instances a shared shell resource that is not in
    // its own interface-element document at all.
    bool second_scope = false;
    // Interposes a vis=0 container widget between the root and the child, so the
    // child is authored visible but has a hidden ancestor.
    bool hidden_ancestor = false;
};

// Root full-screen quad plus one child quad whose widget binding translates it
// away from the frame centre. Resolving only the root binding would leave the
// child at its authored interface-element origin (the frame centre, where
// MakeQuad puts it); its own binding places it at
// x [480, 560], y [343, 403].
[[nodiscard]] FrontEndScreenBundle MakePlacedChildBundle(
    const FrontendColorRgba8IR root_color,
    const FrontendColorRgba8IR child_color,
    const PlacedChildOptions options = {})
{
    FrontendVisualNodeIR root_visual =
        MakeQuad("ROOT_root", 320.0F, 224.0F, root_color);
    FrontendVisualNodeIR child_visual =
        MakeQuad("LOGO", 40.0F, 30.0F, child_color);

    FrontEndScreenBundle::VisualScopeMap scopes;
    if (options.second_scope)
    {
        FrontendVisualNodeIR shell_root =
            FrontendVisualNodeIR{.identifier = "SHELL_root",
                .transform_values = kIdentityAffineTransform12.column_vectors};
        shell_root.children.push_back(std::move(child_visual));
        scopes.emplace("SHELL",
            FrontEndScreenBundleTestAccess::MakeScope(
                omega::asset::FrontendVisualDocumentIR{
                    .root = std::move(shell_root)},
                FrontEndVisualScope::ResourceSet{"SHELL_root", "LOGO"},
                FrontEndVisualScope::TextureMap{}));
    }
    else
    {
        root_visual.children.push_back(std::move(child_visual));
    }
    scopes.emplace("TITLE",
        FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = std::move(root_visual)},
            FrontEndVisualScope::ResourceSet{"ROOT_root", "LOGO"},
            FrontEndVisualScope::TextureMap{}));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    FrontendWidgetIR child_widget = MakeWidget("LOGO");
    child_widget.visible = options.visible_child;
    child_widget.binding = FrontendWidgetBindingIR{
        .scope_reference =
            options.second_scope ? std::string("SHELL") : std::string(),
        .transform_values = TranslationTransform(200.0F, -150.0F, 0.0F),
    };
    if (options.hidden_ancestor)
    {
        FrontendWidgetIR hidden_group = MakeWidget("HIDDEN_GROUP");
        hidden_group.visible = false;
        // The group binds nothing of its own; it exists only to hide its
        // subtree, exactly as the authored lane containers do.
        hidden_group.binding.reset();
        hidden_group.children.push_back(std::move(child_widget));
        root_widget.children.push_back(std::move(hidden_group));
    }
    else
    {
        root_widget.children.push_back(std::move(child_widget));
    }

    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}

// Root quad, then a `buttons_grp`-shaped nesting: a container widget translated
// +80 in x owning an empty interface-element container node, and inside it a
// child widget translated a further +20 owning the art. Hierarchical composition
// is parent * local (FrontendWidgetBindingIR), so the art lands at +100 --
// x [412, 428] -- not at the child's own +20 (x [332, 348]) and not at the
// parent's +80 alone (x [392, 408]).
[[nodiscard]] FrontEndScreenBundle MakeNestedTranslationBundle(
    const FrontendColorRgba8IR root_color,
    const FrontendColorRgba8IR child_color)
{
    FrontendVisualNodeIR root_visual =
        MakeQuad("ROOT_root", 320.0F, 224.0F, root_color);
    FrontendVisualNodeIR group_visual{.identifier = "GROUP",
        .transform_values = kIdentityAffineTransform12.column_vectors};
    group_visual.children.push_back(MakeQuad("LOGO", 8.0F, 40.0F, child_color));
    root_visual.children.push_back(std::move(group_visual));

    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE",
        FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = std::move(root_visual)},
            FrontEndVisualScope::ResourceSet{"ROOT_root", "GROUP", "LOGO"},
            FrontEndVisualScope::TextureMap{}));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    FrontendWidgetIR group_widget = MakeWidget("GROUP");
    group_widget.binding = FrontendWidgetBindingIR{
        .transform_values = TranslationTransform(80.0F, 0.0F, 0.0F)};
    FrontendWidgetIR child_widget = MakeWidget("LOGO");
    child_widget.binding = FrontendWidgetBindingIR{
        .transform_values = TranslationTransform(20.0F, 0.0F, 0.0F)};
    group_widget.children.push_back(std::move(child_widget));
    root_widget.children.push_back(std::move(group_widget));

    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}

// Root quad with two overlapping children in authored interface-element order:
// CLAIMED (a widget owns it, identity placement, x [220, 340]) then UNCLAIMED
// (no widget names it, x [300, 420]). The authored order must survive the mix,
// so the emission order is ROOT, CLAIMED, UNCLAIMED and the later UNCLAIMED wins
// the overlap at x [300, 340].
[[nodiscard]] FrontEndScreenBundle MakeMixedSiblingBundle(
    const FrontendColorRgba8IR root_color,
    const FrontendColorRgba8IR claimed_color,
    const FrontendColorRgba8IR unclaimed_color)
{
    FrontendVisualNodeIR root_visual =
        MakeQuad("ROOT_root", 320.0F, 224.0F, root_color);
    FrontendVisualNodeIR claimed = MakeQuad("CLAIMED", 60.0F, 40.0F,
        claimed_color);
    claimed.transform_values = TranslationTransform(-40.0F, 0.0F, 0.0F);
    FrontendVisualNodeIR unclaimed = MakeQuad("UNCLAIMED", 60.0F, 40.0F,
        unclaimed_color);
    unclaimed.transform_values = TranslationTransform(40.0F, 0.0F, 0.0F);
    root_visual.children.push_back(std::move(claimed));
    root_visual.children.push_back(std::move(unclaimed));

    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE",
        FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = std::move(root_visual)},
            // UNCLAIMED is deliberately absent: an unbound node is not a
            // resource any widget can name, so nothing claims it.
            FrontEndVisualScope::ResourceSet{"ROOT_root", "CLAIMED"},
            FrontEndVisualScope::TextureMap{}));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    root_widget.children.push_back(MakeWidget("CLAIMED"));

    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}

// Two widgets naming the SAME resource in a shared scope, each at its own
// placement: one instance at x [140, 200] and one at x [440, 500]. Both bindings
// resolve to one node, so the compositor must instance it per claiming widget
// rather than draw it once.
[[nodiscard]] FrontEndScreenBundle MakeSharedResourceBundle(
    const FrontendColorRgba8IR root_color,
    const FrontendColorRgba8IR panel_color)
{
    FrontendVisualNodeIR shell_root{.identifier = "SHELL_root",
        .transform_values = kIdentityAffineTransform12.column_vectors};
    shell_root.children.push_back(MakeQuad("PANEL", 30.0F, 20.0F, panel_color));

    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("SHELL",
        FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = std::move(shell_root)},
            FrontEndVisualScope::ResourceSet{"PANEL"},
            FrontEndVisualScope::TextureMap{}));
    scopes.emplace("TITLE",
        FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = MakeQuad("ROOT_root", 320.0F, 224.0F, root_color)},
            FrontEndVisualScope::ResourceSet{"ROOT_root"},
            FrontEndVisualScope::TextureMap{}));

    const auto make_instance = [](std::string identifier,
                                   const float translate_x) {
        FrontendWidgetIR instance = MakeWidget(std::move(identifier));
        instance.binding = FrontendWidgetBindingIR{
            .scope_reference = "SHELL",
            .resource_reference = "PANEL",
            .transform_values = TranslationTransform(translate_x, 0.0F, 0.0F),
        };
        return instance;
    };
    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    root_widget.children.push_back(make_instance("INSTANCE_LEFT", -150.0F));
    root_widget.children.push_back(make_instance("INSTANCE_RIGHT", 150.0F));

    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}

// A Command Center screen whose one child widget is authored VISIBLE and owns
// art. Naming it after a group the composed screen state does not draw
// (kCommandCenterRuntimeHiddenGroups in the compositor) must cull that art;
// any other identifier must draw it.
[[nodiscard]] FrontEndScreenBundle MakeRuntimeGatedBundle(
    const std::string& group_identifier, const FrontendColorRgba8IR root_color,
    const FrontendColorRgba8IR group_color)
{
    FrontendVisualNodeIR root_visual =
        MakeQuad("ROOT_root", 320.0F, 224.0F, root_color);
    root_visual.children.push_back(
        MakeQuad(group_identifier, 40.0F, 30.0F, group_color));

    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("CMDCENTR",
        FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = std::move(root_visual)},
            FrontEndVisualScope::ResourceSet{"ROOT_root", group_identifier},
            FrontEndVisualScope::TextureMap{}));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    FrontendWidgetIR group_widget = MakeWidget(group_identifier);
    group_widget.binding = FrontendWidgetBindingIR{
        .transform_values = TranslationTransform(200.0F, -150.0F, 0.0F)};
    root_widget.children.push_back(std::move(group_widget));

    return FrontEndScreenBundleTestAccess::MakeBundle(
        FrontEndScreenKey::CommandCenter,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "CMDCENTR", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}

[[nodiscard]] std::array<std::uint8_t, 4U> Pixel(
    const omega::frontend::presentation::OwnedRgba8Frame& frame,
    const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * frame.width + x) * 4U;
    return {frame.pixels[offset], frame.pixels[offset + 1U],
        frame.pixels[offset + 2U], frame.pixels[offset + 3U]};
}

// ---- Phase 2 GUI text-pass fixtures --------------------------------------
using omega::asset::IndexedImageEncoding;
using omega::asset::IndexedImageIR;
using omega::asset::RawGsRgba8;
using omega::content::FrontEndTextureAlphaMode;
using omega::content::FrontEndTextureBinding;
using omega::frontend::presentation::RetailFrontEndFrameDiagnostics;
using omega::retail::FntV3GlyphIR;
using omega::retail::FntV3IR;
using omega::retail::RetailStringEntryIR;
using omega::retail::RetailStringTableIR;

// One glyph per printable ASCII code (index = codepoint - 0x21), each a nonzero
// atlas UV box. Mirrors the synthetic font in frontend_text_layout_tests.
[[nodiscard]] FntV3IR MakeFont()
{
    FntV3IR font;
    font.atlas_reference = "SYNTHFNT.TDX";
    font.raw_byte_16 = 7U;
    font.raw_byte_17 = 2U;
    font.space_advance = 3;
    font.glyphs.resize(0x7EU - 0x21U + 1U);
    for (auto& glyph : font.glyphs)
    {
        glyph = FntV3GlyphIR{
            .u_left = 0.10F, .u_right = 0.20F, .v_top = 0.10F, .v_bottom = 0.30F};
    }
    return font;
}

// A 256x256 opaque white indexed atlas so any glyph UV samples a visible texel.
[[nodiscard]] FrontEndTextureBinding MakeFontAtlas()
{
    IndexedImageIR image;
    image.width = 256U;
    image.height = 256U;
    image.source_encoding = IndexedImageEncoding::Indexed8;
    image.indices.assign(
        static_cast<std::size_t>(image.width) * image.height, 1U);
    // Indexed8 requires a full 256-entry palette (sampler contract). GS channels
    // are on a 0..128 scale (128 == 1.0); index 1 (the only one used) is opaque
    // white, index 0 is transparent, the rest are unused padding.
    image.palette.assign(256U,
        RawGsRgba8{.red = 128U, .green = 128U, .blue = 128U, .alpha = 128U});
    image.palette[0U] = RawGsRgba8{.red = 0U, .green = 0U, .blue = 0U,
        .alpha = 0U};
    return FrontEndScreenBundleTestAccess::MakeTextureBinding(std::move(image),
        IndexedImageEncoding::Indexed8,
        FrontEndTextureAlphaMode::UsesPaletteAlpha);
}

// A valid IE root (so composition succeeds) plus a Text widget referencing a
// string key. When provide_font is false the font/atlas are absent so the text
// pass must be fail-soft.
[[nodiscard]] FrontEndScreenBundle MakeTextBundle(const bool provide_font,
    const bool wrap_in_invisible_group = false)
{
    FrontendVisualNodeIR root_visual = MakeQuad("ROOT_root", 320.0F, 224.0F,
        FrontendColorRgba8IR{.red = 20U, .green = 40U, .blue = 60U,
            .alpha = 255U});
    omega::asset::FrontendVisualDocumentIR document{
        .root = std::move(root_visual)};
    auto scope = FrontEndScreenBundleTestAccess::MakeScope(std::move(document),
        FrontEndVisualScope::ResourceSet{"ROOT_root"},
        FrontEndVisualScope::TextureMap{});
    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(scope));

    FrontendWidgetIR root_widget = MakeWidget("ROOT");
    FrontendWidgetIR label = MakeWidget("LABEL");
    label.kind = FrontendWidgetKind::Text;
    label.rectangle = {
        .left = -60.0F, .top = 40.0F, .width = 120.0F, .height = 40.0F};
    // "$key" localization reference; the table stores the '$'-stripped key
    // pre-lowercased (as the decoder does), and Find lowercases the query.
    label.text_reference = "$MENU_KEY";
    if (wrap_in_invisible_group)
    {
        // Mirror the retail hub's authored-hidden lane groups: a vis=0 container
        // holding visible labels. The text pass must cull the whole subtree.
        FrontendWidgetIR hidden_group = MakeWidget("HIDDEN_GROUP");
        hidden_group.visible = false;
        hidden_group.children.push_back(std::move(label));
        root_widget.children.push_back(std::move(hidden_group));
    }
    else
    {
        root_widget.children.push_back(std::move(label));
    }

    FrontEndScreenBundle::FontMap fonts;
    FrontEndScreenBundle::TextureMap atlases;
    if (provide_font)
    {
        fonts.emplace("DEFAULT.FNT", MakeFont());
        atlases.emplace("SYNTHFNT.TDX", MakeFontAtlas());
    }
    RetailStringTableIR strings;
    strings.entries.push_back(
        RetailStringEntryIR{.key = "menu_key", .value = "AB"});

    return FrontEndScreenBundleTestAccess::MakeBundleWithFonts(
        FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(root_widget)},
        "TITLE", std::move(scopes), std::move(fonts), std::move(atlases),
        std::move(strings),
        RetailFrontEndPresentationCapabilityTestAccess::Make(true));
}
} // namespace

int main()
{
    const FrontendColorRgba8IR root_color{
        .red = 20U, .green = 40U, .blue = 60U, .alpha = 255U};
    const FrontendColorRgba8IR child_color{
        .red = 220U, .green = 60U, .blue = 55U, .alpha = 255U};

    // Invalid capability fails closed.
    Check(!ComposeRetailFrontEndFrame(
              MakeRootPlusChildBundle(root_color, child_color, false)) &&
              ComposeRetailFrontEndFrame(
                  MakeRootPlusChildBundle(root_color, child_color, false))
                      .error() == RetailFrontEndFrameError::InvalidRetailCapability,
        "invalid retail capability is rejected");

    const auto composed =
        ComposeRetailFrontEndFrame(MakeRootPlusChildBundle(root_color, child_color));
    Check(composed.has_value(), "root plus child composition succeeds");
    if (!composed)
    {
        std::cerr << failures << " retail front-end frame test(s) failed\n";
        return 1;
    }

    Check(composed->width == 640U && composed->height == 448U,
        "composed frame has canonical dimensions");
    Check(composed->pixels.size() ==
              static_cast<std::size_t>(640U) * 448U * 4U,
        "composed frame has canonical byte count");

    // Center pixel is covered by the child quad (screen ~ x[240,400] y[167,279]
    // after the axis bridge maps IE Y onto screen vertical).
    const auto center = Pixel(*composed, 320U, 224U);
    Check(center[0U] > 150U && center[1U] < 110U && center[2U] < 110U,
        "center pixel shows the child quad color (child drew over the root)");

    // Corner pixel is outside the child quad -> shows the root background.
    const auto corner = Pixel(*composed, 16U, 16U);
    Check(corner[0U] < 90U && corner[2U] > 30U,
        "corner pixel shows the root background color");

    // A degenerate (zero-area) triangle alongside valid geometry is skipped
    // fail-soft: composition still succeeds and the valid quads still render.
    const auto with_degenerate = ComposeRetailFrontEndFrame(
        MakeRootPlusChildBundle(root_color, child_color, true, true));
    Check(with_degenerate.has_value(),
        "a degenerate triangle is skipped rather than failing the screen");
    if (with_degenerate)
    {
        const auto degenerate_center = Pixel(*with_degenerate, 320U, 224U);
        Check(degenerate_center[0U] > 150U && degenerate_center[1U] < 110U &&
                  degenerate_center[2U] < 110U,
            "valid geometry still renders when a degenerate triangle is present");
    }

    // Phase 2: the GUI text pass emits atlas-textured glyph quads for a Text
    // widget on top of the IE geometry.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto text_composed =
            ComposeRetailFrontEndFrame(MakeTextBundle(true), {}, &diagnostics);
        Check(text_composed.has_value(), "text-bearing bundle composes");
        Check(diagnostics.text_widgets_seen == 1U,
            "the one Text widget is visited");
        Check(diagnostics.strings_resolved == 1U,
            "the widget string key resolves in the string table");
        Check(diagnostics.fonts_resolved == 1U,
            "the default font and its atlas resolve");
        Check(diagnostics.glyph_quads_emitted == 2U,
            "two glyph quads are laid out for the two-character label");
        Check(diagnostics.total_triangles > diagnostics.ie_triangles_emitted,
            "glyph triangles are appended after the IE geometry");
    }

    // Phase 2 fail-soft: a Text widget whose font is absent skips its glyphs but
    // never fails the screen.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto text_composed =
            ComposeRetailFrontEndFrame(MakeTextBundle(false), {}, &diagnostics);
        Check(text_composed.has_value(),
            "a text widget with no resolvable font still composes the screen");
        Check(diagnostics.text_widgets_seen == 1U,
            "the Text widget is still visited without a font");
        Check(diagnostics.fonts_missing == 1U,
            "the missing font is counted");
        Check(diagnostics.glyph_quads_emitted == 0U,
            "no glyph quads are emitted without a font");
    }

    // A not-visible widget culls its entire subtree: a visible label inside a
    // vis=0 container draws nothing (the retail hub authors its mutually-exclusive
    // lanes as vis=0 groups; the pre-cull compositor recursed into them and drew
    // every lane at once). The same label at top level (above) DID emit 2 glyphs.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto culled =
            ComposeRetailFrontEndFrame(MakeTextBundle(true, true), {}, &diagnostics);
        Check(culled.has_value(),
            "a bundle with an invisible label group still composes");
        Check(diagnostics.text_widgets_seen == 0U,
            "the label inside the invisible group is never visited");
        Check(diagnostics.glyph_quads_emitted == 0U,
            "no glyph quads are emitted from an invisible group's subtree");
    }

    // Phase 3: naming the selected widget recolors its label, so the composed
    // frame differs from the unselected compose; an unmatched identifier does not.
    {
        const auto unselected =
            ComposeRetailFrontEndFrame(MakeTextBundle(true), {}, nullptr, {});
        const auto selected = ComposeRetailFrontEndFrame(
            MakeTextBundle(true), {}, nullptr, "LABEL");
        Check(unselected.has_value() && selected.has_value(),
            "selection-highlight composes with and without a selected widget");
        if (unselected && selected)
        {
            Check(unselected->pixels != selected->pixels,
                "highlighting the selected label changes the composed pixels");
        }
        const auto unmatched = ComposeRetailFrontEndFrame(
            MakeTextBundle(true), {}, nullptr, "NOT_A_WIDGET");
        Check(unmatched.has_value() && unselected.has_value() &&
                  unmatched->pixels == unselected->pixels,
            "an unmatched selection identifier leaves the frame unchanged");
    }

    // Phase 3b: a node carrying an OPACITY animation track composes differently at
    // different ticks (the child fades out), while a screen with no tracks is
    // tick-invariant. Diagnostics report the animated node.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto tick0 = ComposeRetailFrontEndFrame(
            MakeAnimatedOpacityBundle(), {}, &diagnostics, {}, 0U);
        const auto tick20 = ComposeRetailFrontEndFrame(
            MakeAnimatedOpacityBundle(), {}, nullptr, {}, 20U);
        Check(tick0.has_value() && tick20.has_value(),
            "an animated screen composes at both ticks");
        Check(diagnostics.animated_nodes == 1U,
            "the compositor evaluates the one node carrying animation tracks");
        if (tick0 && tick20)
        {
            Check(tick0->pixels != tick20->pixels,
                "advancing the animation tick changes the composed pixels");
        }

        // Regression guard: a screen with no animation tracks renders identically at
        // any tick, so tick 0 (the default) reproduces the static frame exactly.
        const auto static_tick0 = ComposeRetailFrontEndFrame(
            MakeRootPlusChildBundle(
                FrontendColorRgba8IR{.red = 16U, .green = 16U, .blue = 16U,
                    .alpha = 255U},
                FrontendColorRgba8IR{.red = 240U, .green = 48U, .blue = 48U,
                    .alpha = 255U}),
            {}, nullptr, {}, 0U);
        const auto static_tick20 = ComposeRetailFrontEndFrame(
            MakeRootPlusChildBundle(
                FrontendColorRgba8IR{.red = 16U, .green = 16U, .blue = 16U,
                    .alpha = 255U},
                FrontendColorRgba8IR{.red = 240U, .green = 48U, .blue = 48U,
                    .alpha = 255U}),
            {}, nullptr, {}, 20U);
        Check(static_tick0.has_value() && static_tick20.has_value() &&
                  static_tick0->pixels == static_tick20->pixels,
            "a screen with no animation tracks is tick-invariant");
    }

    {
        // Gap B Slice A: the Command Center mission-ring producer emits a
        // non-empty band of finite, non-degenerate triangles; a null texture
        // yields the untextured fallback; and the perspective projection seats a
        // wider-than-tall ellipse over the frame centre.
        std::vector<omega::frontend::presentation::RetailFrontEndRasterTriangle>
            ring;
        omega::frontend::presentation::AppendRetailMissionRingTriangles(
            omega::frontend::presentation::RetailMissionRingTextures{}, ring);
        Check(!ring.empty(), "mission ring emits triangles");
        bool finite_non_degenerate = true;
        bool all_untextured = true;
        float min_x = 1.0e9F;
        float max_x = -1.0e9F;
        float min_y = 1.0e9F;
        float max_y = -1.0e9F;
        for (const auto& triangle : ring)
        {
            if (triangle.texture != nullptr)
                all_untextured = false;
            const double ax = static_cast<double>(triangle.vertices[0U].x);
            const double ay = static_cast<double>(triangle.vertices[0U].y);
            const double bx = static_cast<double>(triangle.vertices[1U].x);
            const double by = static_cast<double>(triangle.vertices[1U].y);
            const double cx = static_cast<double>(triangle.vertices[2U].x);
            const double cy = static_cast<double>(triangle.vertices[2U].y);
            const double area2 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
            if (!std::isfinite(area2) || area2 == 0.0)
                finite_non_degenerate = false;
            for (const auto& vertex : triangle.vertices)
            {
                if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y))
                    finite_non_degenerate = false;
                min_x = std::min(min_x, vertex.x);
                max_x = std::max(max_x, vertex.x);
                min_y = std::min(min_y, vertex.y);
                max_y = std::max(max_y, vertex.y);
            }
        }
        Check(finite_non_degenerate,
            "mission ring triangles are finite and non-degenerate");
        Check(all_untextured,
            "a null ring texture yields untextured triangles");
        Check((max_x - min_x) > (max_y - min_y),
            "mission ring projects to a wider-than-tall ellipse");
        Check(min_x < 320.0F && max_x > 320.0F && min_y < 246.0F &&
                  max_y > 246.0F,
            "mission ring spans the frame centre");
    }

    // ---- Widget-owned interface-element placement -------------------------
    //
    // A CHILD widget's own binding transform places the interface-element art it
    // owns. Resolving only the root binding left every child's art at its
    // authored interface-element origin (here: the frame centre) instead of at
    // the widget's placement.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto placed = ComposeRetailFrontEndFrame(
            MakePlacedChildBundle(root_color, child_color), {}, &diagnostics);
        Check(placed.has_value(), "a placed child widget composes");
        if (placed)
        {
            const auto at_placement = Pixel(*placed, 520U, 373U);
            Check(at_placement[0U] > 150U && at_placement[1U] < 110U &&
                      at_placement[2U] < 110U,
                "child art draws where its OWN binding transform places it");
            const auto at_centre = Pixel(*placed, 320U, 224U);
            Check(at_centre[0U] < 90U && at_centre[2U] > 30U,
                "child art no longer draws at its authored IE-local origin");
            const auto just_outside = Pixel(*placed, 470U, 373U);
            Check(just_outside[0U] < 90U && just_outside[2U] > 30U,
                "the placed child quad ends at its transformed left edge");
        }
        // Exactly one draw per claim: the root quad's two triangles plus the
        // child quad's two. The child node is reachable from its interface-
        // element parent as well as from its own widget, and draws once.
        Check(diagnostics.ie_triangles_emitted == 4U,
            "each claimed visual node is emitted exactly once");
    }

    // Hierarchical composition is parent * local, as FrontendWidgetBindingIR
    // documents: a widget translated +80 holding a child translated +20 puts the
    // child's art at +100. Treating a binding as an absolute placement would put
    // it at +20, and dropping the child's own binding would put it at +80; both
    // land where the falsification pixels below require the root background.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto nested = ComposeRetailFrontEndFrame(
            MakeNestedTranslationBundle(root_color, child_color), {},
            &diagnostics);
        Check(nested.has_value(), "a nested widget translation composes");
        if (nested)
        {
            const auto at_sum = Pixel(*nested, 420U, 223U);
            Check(at_sum[0U] > 150U && at_sum[1U] < 110U && at_sum[2U] < 110U,
                "nested bindings compose: child art lands at parent + child");
            const auto at_child_only = Pixel(*nested, 340U, 223U);
            Check(at_child_only[0U] < 90U && at_child_only[2U] > 30U,
                "the child's own translation alone does not place its art");
            const auto at_parent_only = Pixel(*nested, 400U, 223U);
            Check(at_parent_only[0U] < 90U && at_parent_only[2U] > 30U,
                "the parent's translation alone does not place its art");
        }
        // The empty container node contributes no triangles, so this is the root
        // quad plus the child quad -- and the axis bridge, applied once at the
        // top of the chain, is not re-applied by either binding (a second
        // exchange would flatten the quad and the kernel would drop it).
        Check(diagnostics.ie_triangles_emitted == 4U,
            "a nested claim chain emits the root and the placed child once");
    }

    // Authored interface-element painter order survives a mix of claimed and
    // unclaimed siblings. ROOT's children are [CLAIMED, UNCLAIMED]; emission is
    // ROOT, CLAIMED, UNCLAIMED, so the later UNCLAIMED covers CLAIMED where they
    // overlap. Deferring claimed siblings to a widget-tree pass after the walk
    // would emit ROOT, UNCLAIMED, CLAIMED and silently invert the overlap.
    {
        const FrontendColorRgba8IR unclaimed_color{
            .red = 40U, .green = 200U, .blue = 70U, .alpha = 255U};
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto mixed = ComposeRetailFrontEndFrame(
            MakeMixedSiblingBundle(root_color, child_color, unclaimed_color),
            {}, &diagnostics);
        Check(mixed.has_value(), "mixed claimed/unclaimed siblings compose");
        if (mixed)
        {
            const auto overlap = Pixel(*mixed, 320U, 223U);
            Check(overlap[1U] > 150U && overlap[0U] < 110U,
                "the later authored sibling stays on top of the claimed one");
            const auto claimed_only = Pixel(*mixed, 260U, 223U);
            Check(claimed_only[0U] > 150U && claimed_only[1U] < 110U,
                "the claimed sibling still draws where nothing covers it");
            const auto unclaimed_only = Pixel(*mixed, 380U, 223U);
            Check(unclaimed_only[1U] > 150U && unclaimed_only[0U] < 110U,
                "the unclaimed sibling draws under its inherited placement");
        }
        Check(diagnostics.ie_triangles_emitted == 6U,
            "root, claimed and unclaimed siblings each emit their quad once");
    }

    // A widget naming a resource in ANOTHER visual scope instances that scope's
    // art at the widget's own placement. A walk of the screen's own interface-
    // element document alone never reaches such a resource.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto instanced = ComposeRetailFrontEndFrame(
            MakePlacedChildBundle(root_color, child_color,
                PlacedChildOptions{.second_scope = true}),
            {}, &diagnostics);
        Check(instanced.has_value(), "a cross-scope instanced child composes");
        if (instanced)
        {
            const auto at_placement = Pixel(*instanced, 520U, 373U);
            Check(at_placement[0U] > 150U && at_placement[1U] < 110U &&
                      at_placement[2U] < 110U,
                "art from another visual scope draws at the widget placement");
        }
        Check(diagnostics.ie_triangles_emitted == 4U,
            "cross-scope instancing emits the root and the instanced quad once");
    }

    // Two widgets naming the SAME shared resource each instance it at their own
    // placement, so one node yields two draws rather than one.
    {
        const FrontendColorRgba8IR panel_color{
            .red = 40U, .green = 200U, .blue = 70U, .alpha = 255U};
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto shared = ComposeRetailFrontEndFrame(
            MakeSharedResourceBundle(root_color, panel_color), {},
            &diagnostics);
        Check(shared.has_value(), "a shared instanced resource composes");
        if (shared)
        {
            const auto left = Pixel(*shared, 170U, 223U);
            const auto right = Pixel(*shared, 470U, 223U);
            Check(left[1U] > 150U && left[0U] < 110U,
                "the first widget instances the shared resource at its place");
            Check(right[1U] > 150U && right[0U] < 110U,
                "the second widget instances the same resource at its place");
            const auto between = Pixel(*shared, 320U, 223U);
            Check(between[1U] < 110U && between[2U] > 30U,
                "the shared resource does not draw between the two instances");
        }
        Check(diagnostics.ie_triangles_emitted == 6U,
            "one shared node claimed twice emits two instances");
    }

    // A not-visible widget culls the ART it owns as well as its text, and the
    // culled node is NOT handed back to its interface-element parent's walk.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto culled = ComposeRetailFrontEndFrame(
            MakePlacedChildBundle(root_color, child_color,
                PlacedChildOptions{.visible_child = false}),
            {}, &diagnostics);
        Check(culled.has_value(), "an invisible child widget still composes");
        if (culled)
        {
            const auto at_placement = Pixel(*culled, 520U, 373U);
            Check(at_placement[0U] < 90U && at_placement[2U] > 30U,
                "an invisible widget draws no art at its placement");
            const auto at_centre = Pixel(*culled, 320U, 224U);
            Check(at_centre[0U] < 90U && at_centre[2U] > 30U,
                "an invisible widget's art is not drawn by the root walk either");
        }
        Check(diagnostics.ie_triangles_emitted == 2U,
            "only the root quad is emitted when the child widget is invisible");
    }

    // One hidden ancestor culls the whole widget subtree below it, so a visible
    // widget inside a vis=0 group draws neither at its own placement nor at the
    // origin its interface-element parent would inherit to it.
    {
        RetailFrontEndFrameDiagnostics diagnostics;
        const auto culled = ComposeRetailFrontEndFrame(
            MakePlacedChildBundle(root_color, child_color,
                PlacedChildOptions{.hidden_ancestor = true}),
            {}, &diagnostics);
        Check(culled.has_value(),
            "a visible widget under a hidden group still composes");
        if (culled)
        {
            const auto at_placement = Pixel(*culled, 520U, 373U);
            Check(at_placement[0U] < 90U && at_placement[2U] > 30U,
                "a hidden ancestor culls its subtree's art at its placement");
            const auto at_centre = Pixel(*culled, 320U, 224U);
            Check(at_centre[0U] < 90U && at_centre[2U] > 30U,
                "a hidden ancestor's subtree art is not drawn by the root walk");
        }
        Check(diagnostics.ie_triangles_emitted == 2U,
            "only the root quad is emitted under a hidden ancestor");
    }

    // The runtime-hidden gate reaches the art too, not only the text: a Command
    // Center group the composed state does not draw contributes no interface-
    // element geometry. The control names the identical widget something else
    // and it draws, so this is the gate and not the fixture.
    {
        RetailFrontEndFrameDiagnostics gated_diagnostics;
        const auto gated = ComposeRetailFrontEndFrame(
            MakeRuntimeGatedBundle("bonusmissions", root_color, child_color), {},
            &gated_diagnostics);
        Check(gated.has_value(), "a runtime-gated hub screen composes");
        Check(gated_diagnostics.ie_triangles_emitted == 2U,
            "a runtime-hidden group contributes no interface-element art");

        RetailFrontEndFrameDiagnostics drawn_diagnostics;
        const auto drawn = ComposeRetailFrontEndFrame(
            MakeRuntimeGatedBundle("mission_grp", root_color, child_color), {},
            &drawn_diagnostics);
        Check(drawn.has_value(), "the ungated control screen composes");
        Check(drawn_diagnostics.ie_triangles_emitted == 4U,
            "the same widget draws its art when it is not runtime-hidden");
    }

    if (failures != 0)
    {
        std::cerr << failures << " retail front-end frame test(s) failed\n";
        return 1;
    }
    std::cout << "retail front-end frame tests passed\n";
    return 0;
}
