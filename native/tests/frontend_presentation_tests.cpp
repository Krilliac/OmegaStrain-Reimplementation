#include "omega/frontend_presentation/retail_title_compositor.h"

#include "omega/content/front_end_screen_bundle.h"

#include <array>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

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
};
} // namespace omega::content::detail

namespace
{
using omega::asset::Float3IR;
using omega::content::FrontEndScreenBundle;
using omega::content::FrontEndScreenKey;
using omega::frontend::kIdentityAffineTransform12;
using omega::frontend::presentation::ComposeStaticRetailTitle;
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
            Float3IR{.x = -320.0F, .y = 224.0F, .z = 0.0F},
            Float3IR{.x = 320.0F, .y = 224.0F, .z = 0.0F},
            Float3IR{.x = 320.0F, .y = -224.0F, .z = 0.0F},
            Float3IR{.x = -320.0F, .y = -224.0F, .z = 0.0F},
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
    if (fixture.visual.texture_member)
    {
        textures.emplace(*fixture.visual.texture_member,
            omega::asset::IndexedImageIR{
                .width = 1U,
                .height = 1U,
                .indices = {0U},
                .palette = {{.red = 1U, .green = 2U, .blue = 3U, .alpha = 128U}},
            });
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

void TestOwnedCanonicalFrame()
{
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

    auto projection = MakeFixture();
    projection.visual.positions[0].z = 1.0F;
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(projection))),
        RetailTitleCompositionError::UnsupportedProjection,
        "nonzero source Z is not silently dropped by the 2D subset");

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

    auto overflow = MakeFixture();
    overflow.widget.binding->transform_values[0] =
        std::numeric_limits<float>::max();
    overflow.visual.transform_values[0] = std::numeric_limits<float>::max();
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(overflow))),
        RetailTitleCompositionError::ArithmeticOverflow,
        "projection overflow fails closed without allocation");

    auto incomplete = MakeFixture();
    incomplete.visual.triangles[1] = incomplete.visual.triangles[0];
    CheckError(ComposeStaticRetailTitle(MakeBundle(std::move(incomplete))),
        RetailTitleCompositionError::IncompleteCoverage,
        "incomplete cover never returns a partial frame");
}
} // namespace

int main()
{
    TestOwnedCanonicalFrame();
    TestCapabilityAndScreenRejections();
    TestUnprovenSemanticRejections();
    TestGeometryBoundsOverflowAndCoverage();

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    return 0;
}
