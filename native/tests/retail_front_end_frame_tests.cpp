#include "omega/frontend_presentation/retail_front_end_frame.h"

#include "omega/asset/frontend_ir.h"
#include "omega/asset/indexed_image_ir.h"
#include "omega/content/front_end_screen_bundle.h"
#include "omega/content/retail_front_end_presentation_capability.h"
#include "omega/frontend/compositor_math.h"
#include "omega/retail/fnt_v3_decoder.h"
#include "omega/retail/retail_string_table_decoder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
using omega::asset::FrontendUvIR;
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
[[nodiscard]] FrontEndScreenBundle MakeTextBundle(const bool provide_font)
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
    root_widget.children.push_back(std::move(label));

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

    if (failures != 0)
    {
        std::cerr << failures << " retail front-end frame test(s) failed\n";
        return 1;
    }
    std::cout << "retail front-end frame tests passed\n";
    return 0;
}
