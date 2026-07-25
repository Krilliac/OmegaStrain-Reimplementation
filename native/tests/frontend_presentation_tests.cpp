#include "omega/frontend_presentation/retail_title_compositor.h"
#include "omega/frontend_presentation/retail_mission_ring.h"
#include "omega/frontend_presentation/retail_root_visual_layer.h"

#include "omega/content/front_end_screen_bundle.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

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
    [[nodiscard]] static FrontEndTextureBinding MakeTexture(
        asset::IndexedImageIR image,
        const asset::IndexedImageEncoding sampling_encoding,
        const FrontEndTextureAlphaMode alpha_mode)
    {
        return FrontEndTextureBinding(
            std::move(image), sampling_encoding, alpha_mode);
    }

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
};
} // namespace omega::content::detail

namespace
{
using omega::asset::Float3IR;
using omega::content::FrontEndScreenBundle;
using omega::content::FrontEndScreenKey;
using omega::frontend::kIdentityAffineTransform12;
using omega::frontend::presentation::ComposeRetailRootVisualLayer;
using omega::frontend::presentation::ComposeStaticRetailTitle;
using omega::frontend::presentation::RetailRootVisualLayerCoverage;
using omega::frontend::presentation::RetailRootVisualLayerError;
using omega::frontend::presentation::RetailRootVisualLayerLimits;
using omega::frontend::presentation::RetailRootVisualLayerResult;
using omega::frontend::presentation::RetailTitleCompositionError;
using omega::frontend::presentation::RetailTitleCompositionResult;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

struct Fixture final
{
    FrontEndScreenKey key = FrontEndScreenKey::Title;
    bool valid_capability = true;
    bool add_extra_scope = false;
    bool omit_texture_binding = false;
    omega::asset::RawGsRgba8 texture_texel{
        .red = 255U,
        .green = 255U,
        .blue = 255U,
        .alpha = 128U,
    };
    omega::content::FrontEndTextureAlphaMode texture_alpha_mode =
        omega::content::FrontEndTextureAlphaMode::UsesPaletteAlpha;
    omega::asset::FrontendWidgetIR widget;
    omega::asset::FrontendVisualNodeIR visual;
};

[[nodiscard]] Fixture MakeFixture()
{
    Fixture fixture;
    fixture.widget = omega::asset::FrontendWidgetIR{
        .kind = omega::asset::FrontendWidgetKind::Container,
        .identifier = "CANVAS",
        .rectangle = {
            .left = -320.0F,
            .top = 224.0F,
            .width = 640.0F,
            .height = 448.0F,
        },
        .visible = true,
        .enabled = true,
        .binding = omega::asset::FrontendWidgetBindingIR{
            .transform_values = kIdentityAffineTransform12.column_vectors,
        },
    };
    fixture.visual = omega::asset::FrontendVisualNodeIR{
        .identifier = "CANVAS_root",
        .transform_values = kIdentityAffineTransform12.column_vectors,
        .positions = {
            Float3IR{.x = -320.0F, .y = 17.0F, .z = 224.0F},
            Float3IR{.x = 320.0F, .y = 17.0F, .z = 224.0F},
            Float3IR{.x = 320.0F, .y = 17.0F, .z = -224.0F},
            Float3IR{.x = -320.0F, .y = 17.0F, .z = -224.0F},
        },
        .uvs = {{.u = 0.0F, .v = 0.0F}},
        .colors = {{.red = 17U, .green = 34U, .blue = 51U, .alpha = 255U}},
        .triangles = {
            {
                .position_indices = {0U, 1U, 2U},
                .uv_indices = {0U, 0U, 0U},
                .color_indices = {0U, 0U, 0U},
            },
            {
                .position_indices = {0U, 2U, 3U},
                .uv_indices = {0U, 0U, 0U},
                .color_indices = {0U, 0U, 0U},
            },
        },
    };
    return fixture;
}

[[nodiscard]] FrontEndScreenBundle MakeBundle(Fixture fixture)
{
    using omega::content::FrontEndVisualScope;
    using omega::content::detail::FrontEndScreenBundleTestAccess;
    using omega::content::detail::RetailFrontEndPresentationCapabilityTestAccess;

    FrontEndVisualScope::TextureMap textures;
    if (fixture.visual.texture_member && !fixture.omit_texture_binding)
    {
        std::vector<omega::asset::RawGsRgba8> palette(16U);
        palette[0U] = fixture.texture_texel;
        textures.emplace(*fixture.visual.texture_member,
            FrontEndScreenBundleTestAccess::MakeTexture(
                omega::asset::IndexedImageIR{
                    .width = 1U,
                    .height = 1U,
                    .source_encoding = omega::asset::IndexedImageEncoding::Indexed4,
                    .indices = {0U},
                    .palette = std::move(palette),
                },
                omega::asset::IndexedImageEncoding::Indexed4,
                fixture.texture_alpha_mode));
    }

    omega::asset::FrontendVisualDocumentIR document{
        .root = std::move(fixture.visual),
    };
    auto primary = FrontEndScreenBundleTestAccess::MakeScope(
        std::move(document), FrontEndVisualScope::ResourceSet{"CANVAS_root"},
        std::move(textures));
    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(primary));
    if (fixture.add_extra_scope)
    {
        auto extra = FrontEndScreenBundleTestAccess::MakeScope(
            omega::asset::FrontendVisualDocumentIR{
                .root = omega::asset::FrontendVisualNodeIR{
                    .identifier = "UNUSED_root",
                    .transform_values = kIdentityAffineTransform12.column_vectors,
                },
            },
            FrontEndVisualScope::ResourceSet{"UNUSED_root"}, {});
        scopes.emplace("EXTRA", std::move(extra));
    }

    return FrontEndScreenBundleTestAccess::MakeBundle(fixture.key,
        omega::asset::FrontendWidgetDocumentIR{.root = std::move(fixture.widget)},
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make(
            fixture.valid_capability));
}

void CheckError(const RetailTitleCompositionResult& result,
    const RetailTitleCompositionError error, const std::string_view message)
{
    Check(!result && result.error() == error, message);
}

void CheckLayerError(const RetailRootVisualLayerResult& result,
    const RetailRootVisualLayerError error, const std::string_view message)
{
    Check(!result && result.error() == error, message);
}

[[nodiscard]] std::array<std::uint8_t, 4U> LayerPixel(
    const omega::frontend::presentation::RetailRootVisualLayer& layer,
    const std::uint32_t x, const std::uint32_t y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * layer.frame.width + x) * 4U;
    return {
        layer.frame.pixels[offset],
        layer.frame.pixels[offset + 1U],
        layer.frame.pixels[offset + 2U],
        layer.frame.pixels[offset + 3U],
    };
}

[[nodiscard]] omega::content::FrontEndTextureBinding MakeLayerTexture(
    const omega::asset::RawGsRgba8 texel,
    const omega::content::FrontEndTextureAlphaMode alpha_mode)
{
    std::vector<omega::asset::RawGsRgba8> palette(16U);
    palette[0U] = texel;
    return omega::content::detail::FrontEndScreenBundleTestAccess::MakeTexture(
        omega::asset::IndexedImageIR{
            .width = 1U,
            .height = 1U,
            .source_encoding = omega::asset::IndexedImageEncoding::Indexed4,
            .indices = {0U},
            .palette = std::move(palette),
        },
        omega::asset::IndexedImageEncoding::Indexed4, alpha_mode);
}

[[nodiscard]] FrontEndScreenBundle MakeCrossScopeLayerBundle()
{
    using omega::content::FrontEndTextureAlphaMode;
    using omega::content::FrontEndVisualScope;
    using omega::content::detail::FrontEndScreenBundleTestAccess;
    using omega::content::detail::RetailFrontEndPresentationCapabilityTestAccess;

    auto fixture = MakeFixture();
    fixture.widget.binding->scope_reference = "other";
    fixture.visual.texture_member = "BOUND.TDX";
    fixture.visual.colors[0U] = {
        .red = 255U,
        .green = 255U,
        .blue = 255U,
        .alpha = 255U,
    };

    auto primary_visual = fixture.visual;
    FrontEndVisualScope::TextureMap primary_textures;
    primary_textures.emplace("BOUND.TDX",
        MakeLayerTexture(
            {.red = 255U, .green = 0U, .blue = 0U, .alpha = 128U},
            FrontEndTextureAlphaMode::UsesPaletteAlpha));
    auto primary = FrontEndScreenBundleTestAccess::MakeScope(
        omega::asset::FrontendVisualDocumentIR{
            .root = std::move(primary_visual),
        },
        FrontEndVisualScope::ResourceSet{"CANVAS_root"},
        std::move(primary_textures));

    FrontEndVisualScope::TextureMap other_textures;
    other_textures.emplace("bound.tdx",
        MakeLayerTexture(
            {.red = 0U, .green = 255U, .blue = 0U, .alpha = 128U},
            FrontEndTextureAlphaMode::UsesPaletteAlpha));
    auto other = FrontEndScreenBundleTestAccess::MakeScope(
        omega::asset::FrontendVisualDocumentIR{
            .root = std::move(fixture.visual),
        },
        FrontEndVisualScope::ResourceSet{"CANVAS_root"},
        std::move(other_textures));

    FrontEndScreenBundle::VisualScopeMap scopes;
    scopes.emplace("TITLE", std::move(primary));
    scopes.emplace("OTHER", std::move(other));
    return FrontEndScreenBundleTestAccess::MakeBundle(FrontEndScreenKey::Title,
        omega::asset::FrontendWidgetDocumentIR{
            .root = std::move(fixture.widget),
        },
        "TITLE", std::move(scopes),
        RetailFrontEndPresentationCapabilityTestAccess::Make());
}

void TestOwnedCanonicalFrame()
{
    static_assert(!std::is_default_constructible_v<omega::content::FrontEndTextureBinding>);
    static_assert(!std::is_copy_constructible_v<omega::content::FrontEndTextureBinding>);
    static_assert(std::is_move_constructible_v<omega::content::FrontEndTextureBinding>);
    static_assert(!std::is_default_constructible_v<
        omega::content::ResolvedFrontEndTextureBinding>);
    static_assert(std::same_as<decltype(ComposeStaticRetailTitle(
                                   std::declval<const FrontEndScreenBundle&>())),
        RetailTitleCompositionResult>);
    static_assert(noexcept(ComposeStaticRetailTitle(
        std::declval<const FrontEndScreenBundle&>())));
    static_assert(omega::frontend::presentation::kRetailTitleFrameByteCount ==
                  static_cast<std::uint64_t>(640U) * 448U * 4U);

    RetailTitleCompositionResult result = [] {
        auto bundle = MakeBundle(MakeFixture());
        return ComposeStaticRetailTitle(bundle);
    }();
    Check(result.has_value(), "canonical cover composes successfully");
    if (!result)
        return;

    Check(result->width == 640U && result->height == 448U,
        "successful frame has canonical dimensions");
    Check(result->pixels.size() ==
              omega::frontend::presentation::kRetailTitleFrameByteCount,
        "successful frame owns exactly one canonical RGBA8 raster");
    bool pixels_match = (result->pixels.size() % 4U) == 0U;
    for (std::size_t offset = 0U; pixels_match && offset < result->pixels.size();
         offset += 4U)
    {
        pixels_match = result->pixels[offset] == 17U &&
                       result->pixels[offset + 1U] == 34U &&
                       result->pixels[offset + 2U] == 51U &&
                       result->pixels[offset + 3U] == 255U;
    }
    Check(pixels_match,
        "owned frame survives bundle destruction with deterministic complete pixels");
}

void TestCapabilityAndScreenRejections()
{
    auto invalid_capability = MakeFixture();
    invalid_capability.valid_capability = false;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(invalid_capability))),
        RetailTitleCompositionError::InvalidRetailCapability,
        "moved capability fails closed");

    auto wrong_screen = MakeFixture();
    wrong_screen.key = FrontEndScreenKey::CreateAgent;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(wrong_screen))),
        RetailTitleCompositionError::UnsupportedScreen,
        "non-Title bundle fails closed");
}

void TestUnprovenSemanticRejections()
{
    auto invisible = MakeFixture();
    invisible.widget.visible = false;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(invisible))),
        RetailTitleCompositionError::UnsupportedWidgetSemantics,
        "unsupported visibility case fails closed");

    auto text = MakeFixture();
    text.widget.text_reference = "$TITLE";
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(text))),
        RetailTitleCompositionError::UnsupportedTextEncoding,
        "retail text bytes are not transcoded by guess");

    auto action = MakeFixture();
    action.widget.binding->actions.push_back({
        .identifier = "confirm",
        .mode = omega::asset::FrontendTimelineActionMode::Immediate,
    });
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(action))),
        RetailTitleCompositionError::UnsupportedActionSemantics,
        "retail action semantics fail closed");

    auto texture = MakeFixture();
    texture.visual.texture_member = "BOUND.TDX";
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(texture))),
        RetailTitleCompositionError::UnsupportedTextureSampling,
        "sampling and address mode are not guessed");

    auto missing_texture = MakeFixture();
    missing_texture.visual.texture_member = "BOUND.TDX";
    missing_texture.omit_texture_binding = true;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(missing_texture))),
        RetailTitleCompositionError::MissingTextureBinding,
        "a declared texture without explicit sampling and alpha metadata fails closed");

    auto animation = MakeFixture();
    animation.visual.animation_tracks.emplace_back(
        omega::asset::FrontendScalarAnimationTrackIR{});
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(animation))),
        RetailTitleCompositionError::UnsupportedAnimation,
        "timeline evaluation is not guessed");

    auto hierarchy = MakeFixture();
    hierarchy.visual.children.push_back(omega::asset::FrontendVisualNodeIR{
        .identifier = "CHILD",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    });
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(hierarchy))),
        RetailTitleCompositionError::UnsupportedVisualHierarchy,
        "unproven traversal and lane order fail closed");

    auto extra_scope = MakeFixture();
    extra_scope.add_extra_scope = true;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(extra_scope))),
        RetailTitleCompositionError::UnsupportedVisualHierarchy,
        "multiple scopes cannot produce a partial frame");

    auto transform = MakeFixture();
    transform.widget.binding->transform_values[9] = 1.0F;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(transform))),
        RetailTitleCompositionError::UnsupportedTransform,
        "nontrivial retail transform composition fails closed");

    auto interpolation = MakeFixture();
    interpolation.visual.uvs[0].u = 0.25F;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(interpolation))),
        RetailTitleCompositionError::UnsupportedRasterization,
        "unneeded but noncanonical raster attributes fail closed");

    auto varying_depth = MakeFixture();
    varying_depth.visual.positions[0].y = 99.0F;
    const auto varying_depth_result =
        ComposeStaticRetailTitle(MakeBundle(std::move(varying_depth)));
    Check(varying_depth_result.has_value(),
        "nonzero and varying source Y supplies depth without changing raster XY");

    auto out_of_range_depth = MakeFixture();
    out_of_range_depth.visual.positions[0].y = -2.0F;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(out_of_range_depth))),
        RetailTitleCompositionError::UnsupportedProjection,
        "the static subset fails closed when projected depth leaves the normalized interval");

    auto output_alpha = MakeFixture();
    output_alpha.visual.colors[0].alpha = 128U;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(output_alpha))),
        RetailTitleCompositionError::UnsupportedOutputAlpha,
        "output alpha is not inferred");
}

void TestGeometryBoundsOverflowAndCoverage()
{
    auto invalid_index = MakeFixture();
    invalid_index.visual.triangles[1].position_indices[2] = 4U;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(invalid_index))),
        RetailTitleCompositionError::InvalidGeometry,
        "out-of-range triangle index fails closed");

    auto out_of_bounds = MakeFixture();
    out_of_bounds.visual.positions[0].x = -321.0F;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(out_of_bounds))),
        RetailTitleCompositionError::GeometryOutOfBounds,
        "geometry outside the canonical raster fails closed");

    auto x_overflow = MakeFixture();
    x_overflow.visual.positions[0].x = std::numeric_limits<float>::max();
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(x_overflow))),
        RetailTitleCompositionError::ArithmeticOverflow,
        "screen-X projection overflow fails closed without allocation");

    auto z_overflow = MakeFixture();
    z_overflow.visual.positions[0].z = -std::numeric_limits<float>::max();
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(z_overflow))),
        RetailTitleCompositionError::ArithmeticOverflow,
        "screen-Z projection overflow fails closed without allocation");

    auto incomplete = MakeFixture();
    incomplete.visual.triangles[1] = incomplete.visual.triangles[0];
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(incomplete))),
        RetailTitleCompositionError::IncompleteCoverage,
        "incomplete cover never returns a partial frame");
}

void TestRootVisualLayerContractAndScreenCoverage()
{
    using omega::frontend::presentation::RetailRootVisualLayer;
    using omega::frontend::presentation::kRetailRootVisualLayerCoverageBytes;
    using omega::frontend::presentation::
        kRetailRootVisualLayerMaximumIntermediateBytes;

    static_assert(std::same_as<decltype(ComposeRetailRootVisualLayer(
                                   std::declval<const FrontEndScreenBundle&>(),
                                   std::declval<RetailRootVisualLayerLimits>())),
        RetailRootVisualLayerResult>);
    static_assert(noexcept(ComposeRetailRootVisualLayer(
        std::declval<const FrontEndScreenBundle&>(),
        std::declval<RetailRootVisualLayerLimits>())));
    static_assert(std::is_enum_v<RetailRootVisualLayerError>);
    static_assert(sizeof(RetailRootVisualLayerError) == 1U);
    static_assert(!std::is_convertible_v<RetailRootVisualLayer,
        omega::content::RetailFrontEndPresentationCapability>);
    static_assert(kRetailRootVisualLayerCoverageBytes == 640ULL * 448ULL);
    static_assert(kRetailRootVisualLayerMaximumIntermediateBytes >
                  kRetailRootVisualLayerCoverageBytes);

    for (const auto key : {FrontEndScreenKey::Title,
             FrontEndScreenKey::CreateAgent, FrontEndScreenKey::LoadAgent})
    {
        auto fixture = MakeFixture();
        fixture.key = key;
        const auto layer =
            ComposeRetailRootVisualLayer(MakeBundle(std::move(fixture)));
        Check(layer && layer->coverage ==
                           RetailRootVisualLayerCoverage::
                               RootVisualOwnGeometryOnly,
            "Title, CreateAgent, and LoadAgent each admit the bounded root layer");
        Check(layer && layer->frame.width == 640U &&
                layer->frame.height == 448U &&
                layer->frame.pixels.size() ==
                    omega::frontend::presentation::
                        kRetailFrontEndRasterOutputBytes,
            "root-layer success owns exactly one canonical RGBA8 frame");
        if (layer)
        {
            Check(LayerPixel(*layer, 0U, 0U) ==
                      std::array<std::uint8_t, 4U>{
                          17U, 34U, 51U, 255U} &&
                    LayerPixel(*layer, 639U, 447U) ==
                        LayerPixel(*layer, 0U, 0U),
                "untextured root geometry covers the complete frame");
        }
    }

    const auto owned_after_bundle = []() -> RetailRootVisualLayerResult {
        auto bundle = MakeBundle(MakeFixture());
        return ComposeRetailRootVisualLayer(bundle);
    }();
    const auto repeated =
        ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()));
    Check(owned_after_bundle && repeated &&
            owned_after_bundle == repeated,
        "root-layer output is owned and repeated calls are byte-identical");
}

void TestRootVisualLayerTexturesAndExactScope()
{
    auto textured = MakeFixture();
    textured.visual.texture_member = "BOUND.TDX";
    textured.texture_texel = {
        .red = 128U,
        .green = 64U,
        .blue = 255U,
        .alpha = 128U,
    };
    const auto textured_layer =
        ComposeRetailRootVisualLayer(MakeBundle(std::move(textured)));
    Check(textured_layer &&
            LayerPixel(*textured_layer, 0U, 0U) ==
                std::array<std::uint8_t, 4U>{9U, 9U, 51U, 255U},
        "same-scope texture sampling uses established modulation and palette alpha");

    auto ignores_alpha = MakeFixture();
    ignores_alpha.visual.texture_member = "BOUND.TDX";
    ignores_alpha.texture_texel = {
        .red = 255U,
        .green = 255U,
        .blue = 255U,
        .alpha = 0U,
    };
    ignores_alpha.texture_alpha_mode =
        omega::content::FrontEndTextureAlphaMode::IgnoresTextureAlpha;
    Check(ComposeRetailRootVisualLayer(
              MakeBundle(std::move(ignores_alpha))).has_value(),
        "TCC-disabled texture alpha preserves fully opaque vertex alpha");

    auto uses_transparent_alpha = MakeFixture();
    uses_transparent_alpha.visual.texture_member = "BOUND.TDX";
    uses_transparent_alpha.texture_texel.alpha = 0U;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(uses_transparent_alpha))),
        RetailRootVisualLayerError::TransparentOutput,
        "TCC-enabled transparent palette output cannot masquerade as a complete layer");

    auto missing_texture = MakeFixture();
    missing_texture.visual.texture_member = "BOUND.TDX";
    missing_texture.omit_texture_binding = true;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(missing_texture))),
        RetailRootVisualLayerError::MissingTextureBinding,
        "a declared texture must resolve in the selected visual scope");

    const auto cross_scope =
        ComposeRetailRootVisualLayer(MakeCrossScopeLayerBundle());
    Check(cross_scope &&
            LayerPixel(*cross_scope, 17U, 31U) ==
                std::array<std::uint8_t, 4U>{0U, 255U, 0U, 255U},
        "an authored non-primary scope binds only its own exact texture cohort");
}

void TestRootVisualLayerTransformsAndDescendantOmission()
{
    auto transformed = MakeFixture();
    transformed.visual.positions[0U].x = -155.0F;
    transformed.visual.positions[1U].x = 165.0F;
    transformed.visual.positions[2U].x = 165.0F;
    transformed.visual.positions[3U].x = -155.0F;
    transformed.visual.transform_values[0U] = 2.0F;
    transformed.widget.binding->transform_values[9U] = -10.0F;
    Check(ComposeRetailRootVisualLayer(
              MakeBundle(std::move(transformed))).has_value(),
        "binding times visual transform is applied before projection");

    const auto baseline =
        ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()));
    auto descendants = MakeFixture();
    descendants.widget.children.push_back(omega::asset::FrontendWidgetIR{
        .kind = omega::asset::FrontendWidgetKind::Text,
        .identifier = "OMITTED_WIDGET",
        .visible = true,
        .text_reference = "UNINTERPRETED",
    });
    descendants.visual.children.push_back(
        omega::asset::FrontendVisualNodeIR{
            .identifier = "OMITTED_VISUAL",
            .transform_values = {
                std::numeric_limits<float>::quiet_NaN(),
            },
            .animation_tracks = {
                omega::asset::FrontendScalarAnimationTrackIR{},
            },
        });
    const auto with_descendants =
        ComposeRetailRootVisualLayer(MakeBundle(std::move(descendants)));
    Check(baseline && with_descendants && baseline == with_descendants,
        "widget and visual descendants are explicitly omitted from the root-only layer");
}

void TestRootVisualLayerSemanticAndGeometryFailures()
{
    auto invalid_capability = MakeFixture();
    invalid_capability.valid_capability = false;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(invalid_capability))),
        RetailRootVisualLayerError::InvalidRetailCapability,
        "a consumed retail capability fails before bundle traversal");

    auto invisible = MakeFixture();
    invisible.widget.visible = false;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(invisible))),
        RetailRootVisualLayerError::UnsupportedRootWidget,
        "an invisible root cannot produce a research layer");

    auto wrong_kind = MakeFixture();
    wrong_kind.widget.kind = omega::asset::FrontendWidgetKind::Button;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(wrong_kind))),
        RetailRootVisualLayerError::UnsupportedRootWidget,
        "only a Container root is admitted");

    auto missing_binding = MakeFixture();
    missing_binding.widget.binding.reset();
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(missing_binding))),
        RetailRootVisualLayerError::MissingRootVisualBinding,
        "the root must carry an explicit visual binding");

    auto root_text = MakeFixture();
    root_text.widget.text_reference = "UNINTERPRETED";
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(root_text))),
        RetailRootVisualLayerError::UnsupportedRootText,
        "root text encoding is never guessed");

    auto root_action = MakeFixture();
    root_action.widget.binding->actions.push_back({
        .identifier = "UNINTERPRETED",
        .mode = omega::asset::FrontendTimelineActionMode::Immediate,
    });
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(root_action))),
        RetailRootVisualLayerError::UnsupportedRootAction,
        "root action lifecycle is never guessed");

    auto missing_visual = MakeFixture();
    missing_visual.widget.identifier = "MISSING";
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(missing_visual))),
        RetailRootVisualLayerError::MissingRootVisualBinding,
        "a missing parentless root visual is categorical");

    auto nested_root = MakeFixture();
    auto nested_match = std::move(nested_root.visual);
    nested_root.visual = omega::asset::FrontendVisualNodeIR{
        .identifier = "WRAPPER",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    nested_root.visual.children.push_back(std::move(nested_match));
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(nested_root))),
        RetailRootVisualLayerError::UnsupportedRootVisualHierarchy,
        "a nested parentless-name match is rejected rather than omitting ancestor transforms");

    auto animation = MakeFixture();
    animation.visual.animation_tracks.emplace_back(
        omega::asset::FrontendScalarAnimationTrackIR{});
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(animation))),
        RetailRootVisualLayerError::UnsupportedRootAnimation,
        "root animation remains outside the static layer");

    auto invalid_position_index = MakeFixture();
    invalid_position_index.visual.triangles[0U].position_indices[0U] = 4U;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(invalid_position_index))),
        RetailRootVisualLayerError::InvalidGeometry,
        "an out-of-range position index fails closed");

    auto invalid_uv_index = MakeFixture();
    invalid_uv_index.visual.triangles[0U].uv_indices[0U] = 1U;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(invalid_uv_index))),
        RetailRootVisualLayerError::InvalidGeometry,
        "an out-of-range UV index fails closed");

    auto invalid_color_index = MakeFixture();
    invalid_color_index.visual.triangles[0U].color_indices[0U] = 1U;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(invalid_color_index))),
        RetailRootVisualLayerError::InvalidGeometry,
        "an out-of-range color index fails closed");

    auto nonfinite_transform = MakeFixture();
    nonfinite_transform.widget.binding->transform_values[0U] =
        std::numeric_limits<float>::quiet_NaN();
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(nonfinite_transform))),
        RetailRootVisualLayerError::NonFiniteInput,
        "a nonfinite authored transform is categorical");

    auto nonfinite_position = MakeFixture();
    nonfinite_position.visual.positions[0U].x =
        std::numeric_limits<float>::quiet_NaN();
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(nonfinite_position))),
        RetailRootVisualLayerError::NonFiniteInput,
        "a nonfinite referenced position is categorical");

    auto projection_overflow = MakeFixture();
    projection_overflow.visual.positions[0U].x =
        std::numeric_limits<float>::max();
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(projection_overflow))),
        RetailRootVisualLayerError::ProjectionFailure,
        "projection overflow fails before coverage allocation");

    auto depth_out_of_range = MakeFixture();
    depth_out_of_range.visual.positions[0U].y = -2.0F;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(depth_out_of_range))),
        RetailRootVisualLayerError::ProjectionFailure,
        "projected depth outside the established normalized interval is rejected");

    auto nonfinite_uv = MakeFixture();
    nonfinite_uv.visual.uvs[0U].u =
        std::numeric_limits<float>::quiet_NaN();
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(nonfinite_uv))),
        RetailRootVisualLayerError::NonFiniteInput,
        "nonfinite texture coordinates fail before sampling");

    auto degenerate = MakeFixture();
    degenerate.visual.triangles[1U].position_indices = {0U, 0U, 0U};
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(degenerate))),
        RetailRootVisualLayerError::DegenerateGeometry,
        "degenerate root geometry remains distinct from incomplete coverage");
}

void TestRootVisualLayerOrderIndependenceCoverageAndLimits()
{
    auto overlap = MakeFixture();
    overlap.visual.triangles.push_back(overlap.visual.triangles[0U]);
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(overlap))),
        RetailRootVisualLayerError::OverlappingCoverage,
        "any covered-pixel overlap rejects unproven triangle ordering");

    auto incomplete = MakeFixture();
    incomplete.visual.triangles.pop_back();
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(incomplete))),
        RetailRootVisualLayerError::IncompleteCoverage,
        "geometric coverage must include every canonical pixel");

    auto transparent = MakeFixture();
    transparent.visual.colors[0U].alpha = 254U;
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(transparent))),
        RetailRootVisualLayerError::TransparentOutput,
        "complete geometric coverage still requires final alpha 255 everywhere");

    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_positions = 3U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten position count");
    auto uv_limited = MakeFixture();
    uv_limited.visual.uvs.push_back({.u = 1.0F, .v = 1.0F});
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(uv_limited)),
                        RetailRootVisualLayerLimits{
                            .maximum_uvs = 1U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten UV count");
    auto color_limited = MakeFixture();
    color_limited.visual.colors.push_back(
        {.red = 0U, .green = 0U, .blue = 0U, .alpha = 255U});
    CheckLayerError(ComposeRetailRootVisualLayer(
                        MakeBundle(std::move(color_limited)),
                        RetailRootVisualLayerLimits{
                            .maximum_colors = 1U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten color count");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_triangles = 1U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten triangle count");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_covered_samples =
                                omega::frontend::presentation::
                                    kRetailRootVisualLayerCoverageBytes -
                                1U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten aggregate coverage work");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_intermediate_bytes =
                                omega::frontend::presentation::
                                    kRetailRootVisualLayerCoverageBytes,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten logical intermediate storage");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_output_bytes =
                                omega::frontend::presentation::
                                    kRetailFrontEndRasterOutputBytes -
                                1U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten output storage");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_raster_scratch_bytes =
                                omega::frontend::presentation::
                                    kRetailFrontEndRasterScratchBytes -
                                1U,
                        }),
        RetailRootVisualLayerError::LimitExceeded,
        "the caller can tighten raster scratch storage");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_positions = 0U,
                        }),
        RetailRootVisualLayerError::InvalidLimits,
        "zero never means an unbounded limit");
    CheckLayerError(ComposeRetailRootVisualLayer(MakeBundle(MakeFixture()),
                        RetailRootVisualLayerLimits{
                            .maximum_triangles =
                                omega::frontend::presentation::
                                    kRetailRootVisualLayerMaximumTriangles +
                                1U,
                        }),
        RetailRootVisualLayerError::InvalidLimits,
        "callers cannot raise immutable hard ceilings");
}

// --- Command Center mission ring -------------------------------------------
// Pins the MEASURED geometry in analysis/formats/MISSION-RING.md. Every number
// below is transcribed from that document independently of the producer, so a
// drift in either one fails the test. Synthetic fixtures only; no capture data,
// no owner-corpus bytes.

struct DocumentedMarker final
{
    float center_x = 0.0F;
    float center_y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

// MISSION-RING.md, "22 markers, at fixed screen positions": the "centre" and
// "marker W x H" columns, in draw order.
constexpr std::array<DocumentedMarker, 22U> kDocumentedMarkers = {{
    {511.812F, 180.438F, 43.00F, 37.50F},
    {437.844F, 361.000F, 29.81F, 27.00F},
    {362.844F, 362.281F, 29.81F, 26.94F},
    {297.688F, 374.406F, 29.62F, 26.81F},
    {229.000F, 372.094F, 29.62F, 26.81F},
    {202.031F, 330.719F, 25.94F, 24.19F},
    {141.781F, 325.062F, 29.81F, 27.00F},
    {123.719F, 279.938F, 29.81F, 27.00F},
    {168.562F, 247.844F, 28.38F, 23.81F},
    {99.125F, 218.625F, 27.75F, 25.88F},
    {160.750F, 189.469F, 27.75F, 25.94F},
    {176.250F, 136.875F, 29.25F, 26.50F},
    {253.094F, 156.469F, 26.31F, 24.56F},
    {258.562F, 101.625F, 27.75F, 25.88F},
    {315.344F, 111.344F, 29.81F, 26.94F},
    {371.344F, 105.844F, 29.81F, 26.94F},
    {433.594F, 85.594F, 27.81F, 25.94F},
    {425.344F, 142.062F, 25.56F, 23.88F},
    {479.219F, 134.688F, 29.81F, 27.00F},
    {515.031F, 164.375F, 37.06F, 34.62F},
    {513.156F, 269.031F, 54.06F, 45.56F},
    {469.375F, 310.344F, 52.75F, 45.56F},
}};

struct QuadBounds final
{
    float min_x = 0.0F;
    float min_y = 0.0F;
    float max_x = 0.0F;
    float max_y = 0.0F;

    [[nodiscard]] float width() const noexcept { return max_x - min_x; }
    [[nodiscard]] float height() const noexcept { return max_y - min_y; }
    [[nodiscard]] float center_x() const noexcept
    {
        return (min_x + max_x) * 0.5F;
    }
    [[nodiscard]] float center_y() const noexcept
    {
        return (min_y + max_y) * 0.5F;
    }
};

[[nodiscard]] bool Near(
    const float lhs, const float rhs, const float tolerance = 0.002F) noexcept
{
    return std::fabs(lhs - rhs) <= tolerance;
}

// Every emitted quad is two triangles over one axis-aligned rectangle, so a
// pair of consecutive triangles with an identical bounding box is one quad.
[[nodiscard]] std::vector<QuadBounds> CollectQuads(
    const std::vector<
        omega::frontend::presentation::RetailFrontEndRasterTriangle>& triangles)
{
    std::vector<QuadBounds> quads;
    for (std::size_t index = 0U; index + 1U < triangles.size(); index += 2U)
    {
        QuadBounds bounds{
            .min_x = std::numeric_limits<float>::max(),
            .min_y = std::numeric_limits<float>::max(),
            .max_x = std::numeric_limits<float>::lowest(),
            .max_y = std::numeric_limits<float>::lowest(),
        };
        for (std::size_t offset = 0U; offset < 2U; ++offset)
        {
            for (const auto& vertex : triangles[index + offset].vertices)
            {
                bounds.min_x = std::min(bounds.min_x, vertex.x);
                bounds.min_y = std::min(bounds.min_y, vertex.y);
                bounds.max_x = std::max(bounds.max_x, vertex.x);
                bounds.max_y = std::max(bounds.max_y, vertex.y);
            }
        }
        quads.push_back(bounds);
    }
    return quads;
}

void TestMeasuredMissionRingGeometry()
{
    namespace presentation = omega::frontend::presentation;
    using presentation::AppendRetailMissionRingTriangles;
    using presentation::RetailFrontEndRasterTriangle;

    // 1. The table itself matches the document, entry for entry.
    const auto markers = presentation::RetailMissionRingMarkers();
    Check(presentation::kRetailMissionRingMarkerCount == 22U,
        "MISSION-RING.md measures 22 markers");
    Check(markers.size() == kDocumentedMarkers.size(),
        "the exposed marker table has one entry per measured marker");
    if (markers.size() == kDocumentedMarkers.size())
    {
        bool table_matches = true;
        bool opaque_flags_match = true;
        for (std::size_t index = 0U; index < markers.size(); ++index)
        {
            const auto& measured = kDocumentedMarkers[index];
            const auto& marker = markers[index];
            if (!Near(marker.center_x, measured.center_x) ||
                !Near(marker.center_y, measured.center_y) ||
                !Near(marker.width, measured.width) ||
                !Near(marker.height, measured.height))
            {
                table_matches = false;
            }
            // MISSION-RING.md: only marker 0 "carries no opaque pass at all".
            if (marker.emits_opaque_pass != (index != 0U))
                opaque_flags_match = false;
        }
        Check(table_matches,
            "every marker centre and extent matches the measured table");
        Check(opaque_flags_match,
            "only the animated marker 0 lacks an opaque pass");
    }

    // 2. The markers, composed with the one selection the captures show. With
    // no band texture the band quad is not emitted, so everything here is a
    // marker.
    std::vector<RetailFrontEndRasterTriangle> ring;
    AppendRetailMissionRingTriangles(
        nullptr, ring, presentation::kRetailMissionRingCapturedSelectedIndex);
    Check(!ring.empty(), "the mission ring emits triangles");
    Check((ring.size() % 2U) == 0U, "the ring is emitted as whole quads");

    const auto quads = CollectQuads(ring);
    // 2 highlight quads + 20 opaque dots (22 markers less the animated marker 0
    // and less the highlighted one).
    Check(quads.size() == 22U,
        "a two-quad highlight plus one dot per other opaque marker");

    bool all_finite_non_degenerate = true;
    bool markers_untextured = true;
    for (const auto& triangle : ring)
    {
        if (triangle.texture != nullptr)
            markers_untextured = false;
        const double ax = static_cast<double>(triangle.vertices[0U].x);
        const double ay = static_cast<double>(triangle.vertices[0U].y);
        const double bx = static_cast<double>(triangle.vertices[1U].x);
        const double by = static_cast<double>(triangle.vertices[1U].y);
        const double cx = static_cast<double>(triangle.vertices[2U].x);
        const double cy = static_cast<double>(triangle.vertices[2U].y);
        const double area2 = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
        if (!std::isfinite(area2) || area2 == 0.0)
            all_finite_non_degenerate = false;
        for (const auto& vertex : triangle.vertices)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y))
                all_finite_non_degenerate = false;
        }
    }
    Check(all_finite_non_degenerate,
        "every ring triangle is finite and non-degenerate");
    Check(markers_untextured,
        "marker quads carry no texture: per-marker texture identity is "
        "unproven");

    // 3. Given a band texture, the band is the FIRST quad, at the measured
    // modal extent, and it is the only textured quad. The band exists only as
    // texture art, so with no texture it is absent entirely.
    {
        const auto band_texture = MakeLayerTexture(
            omega::asset::RawGsRgba8{
                .red = 200U, .green = 210U, .blue = 255U, .alpha = 128U},
            omega::content::FrontEndTextureAlphaMode::UsesPaletteAlpha);
        std::vector<RetailFrontEndRasterTriangle> textured;
        AppendRetailMissionRingTriangles(&band_texture, textured,
            presentation::kRetailMissionRingCapturedSelectedIndex);
        Check(textured.size() == ring.size() + 2U,
            "a band texture adds exactly the two band triangles");
        std::size_t textured_count = 0U;
        for (const auto& triangle : textured)
        {
            if (triangle.texture != nullptr)
                ++textured_count;
        }
        Check(textured_count == 2U,
            "exactly the two band triangles sample the band texture");

        const auto textured_quads = CollectQuads(textured);
        if (!textured_quads.empty())
        {
            const auto& band = textured_quads.front();
            Check(Near(band.min_x, 170.812F) && Near(band.min_y, 97.438F) &&
                      Near(band.max_x, 489.688F) && Near(band.max_y, 387.500F),
                "the band quad is the measured modal extent "
                "(170.812, 97.438)-(489.688, 387.500)");
            Check(Near(band.width(), 318.876F) && Near(band.height(), 290.062F),
                "the band quad is 318.876 x 290.062");
        }
    }

    // 4. Exactly one marker renders the highlight form; the rest render dots.
    for (std::uint32_t selected = 1U;
         selected < presentation::kRetailMissionRingMarkerCount; ++selected)
    {
        std::vector<RetailFrontEndRasterTriangle> selection;
        AppendRetailMissionRingTriangles(nullptr, selection, selected);
        std::size_t highlight_quads = 0U;
        std::size_t highlight_at_selected = 0U;
        std::size_t dot_quads = 0U;
        for (const auto& quad : CollectQuads(selection))
        {
            // MISSION-RING.md, "The highlighted marker": the pair of
            // 52.31 x 47.31 quads spanning (336.688, 338.625)-(389.000,
            // 385.938) -- 52.312 x 47.312 centred on the marker. The two
            // largest icon markers (54.06 x 45.56 and 52.75 x 45.56) are close
            // in size but never equal, so the discriminator stays exact.
            if (Near(quad.width(),
                    presentation::kRetailMissionRingHighlightWidth, 0.01F) &&
                Near(quad.height(),
                    presentation::kRetailMissionRingHighlightHeight, 0.01F))
            {
                ++highlight_quads;
                if (Near(quad.center_x(),
                        kDocumentedMarkers[selected].center_x) &&
                    Near(quad.center_y(),
                        kDocumentedMarkers[selected].center_y))
                {
                    ++highlight_at_selected;
                }
                continue;
            }
            ++dot_quads;
        }
        Check(highlight_quads ==
                presentation::kRetailMissionRingHighlightQuadCount,
            "exactly one marker renders the two-quad highlight form");
        Check(highlight_at_selected ==
                presentation::kRetailMissionRingHighlightQuadCount,
            "the highlight sits on the selected marker's measured centre");
        // 22 markers, less marker 0 (no opaque pass), less the highlighted one.
        Check(dot_quads == 20U, "every other opaque marker renders its dot");
    }

    // The highlighted marker's own dot is suppressed (retail leaves both of its
    // dot draws at alpha 0), so no quad at marker 2's dot extent survives when
    // marker 2 is selected.
    {
        bool dot_suppressed = true;
        for (const auto& quad : quads)
        {
            if (Near(quad.center_x(), 362.844F) &&
                Near(quad.center_y(), 362.281F) && Near(quad.width(), 29.81F))
            {
                dot_suppressed = false;
            }
        }
        Check(dot_suppressed,
            "the highlighted marker's small dot is not emitted");
    }

    // 5. Selection is fail-soft, and it never rotates the ring: the marker
    // positions are identical for every selection.
    {
        std::vector<RetailFrontEndRasterTriangle> out_of_range;
        AppendRetailMissionRingTriangles(nullptr, out_of_range,
            presentation::kRetailMissionRingMarkerCount);
        // Nothing highlighted, nothing out of bounds: 21 opaque dots.
        Check(CollectQuads(out_of_range).size() == 21U,
            "an out-of-range selection highlights nothing and stays in bounds");

        std::vector<RetailFrontEndRasterTriangle> huge;
        AppendRetailMissionRingTriangles(
            nullptr, huge, std::numeric_limits<std::uint32_t>::max());
        Check(CollectQuads(huge).size() == 21U,
            "a wildly out-of-range selection also fails soft");

        // The ring does not rotate: marker 5's dot is at its measured position
        // whichever marker is selected. (MISSION-RING.md: the rotation step is
        // unobservable, and the highlighted marker is not at the ring's lowest
        // point, so no selection is brought to a focal position.)
        std::vector<RetailFrontEndRasterTriangle> other_selection;
        AppendRetailMissionRingTriangles(nullptr, other_selection, 12U);
        bool marker_five_fixed = false;
        for (const auto& quad : CollectQuads(other_selection))
        {
            if (Near(quad.center_x(), kDocumentedMarkers[5U].center_x) &&
                Near(quad.center_y(), kDocumentedMarkers[5U].center_y) &&
                Near(quad.width(), kDocumentedMarkers[5U].width))
            {
                marker_five_fixed = true;
            }
        }
        Check(marker_five_fixed,
            "changing the selection does not move any marker");
    }

    // 6. Appending is additive and touches nothing already in the buffer.
    {
        std::vector<RetailFrontEndRasterTriangle> seeded;
        seeded.push_back(RetailFrontEndRasterTriangle{});
        AppendRetailMissionRingTriangles(nullptr, seeded,
            presentation::kRetailMissionRingCapturedSelectedIndex);
        Check(seeded.size() == ring.size() + 1U &&
                  seeded.front() == RetailFrontEndRasterTriangle{},
            "the producer appends and preserves prior triangles");
    }
}
} // namespace

int main()
{
    TestOwnedCanonicalFrame();
    TestCapabilityAndScreenRejections();
    TestUnprovenSemanticRejections();
    TestGeometryBoundsOverflowAndCoverage();
    TestRootVisualLayerContractAndScreenCoverage();
    TestRootVisualLayerTexturesAndExactScope();
    TestRootVisualLayerTransformsAndDescendantOmission();
    TestRootVisualLayerSemanticAndGeometryFailures();
    TestRootVisualLayerOrderIndependenceCoverageAndLimits();
    TestMeasuredMissionRingGeometry();

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    return 0;
}
