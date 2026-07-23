#include "omega/frontend_presentation/retail_presentation_requirements.h"
#include "omega/frontend_presentation/retail_title_compositor.h"

#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend/compositor_math.h"

#include <array>
#include <concepts>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
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
            RetailFrontEndPresentationCapability consumed(
                std::move(capability));
            (void)consumed;
        }
        return capability;
    }
};

struct FrontEndScreenBundleTestAccess final
{
    [[nodiscard]] static FrontEndVisualScope MakeScope(
        asset::FrontendVisualDocumentIR document,
        FrontEndVisualScope::ResourceSet resources)
    {
        return FrontEndVisualScope(
            std::move(document), std::move(resources), {});
    }

    [[nodiscard]] static FrontEndScreenBundle MakeBundle(
        const FrontEndScreenKey key,
        asset::FrontendWidgetDocumentIR widget_document,
        std::string primary_scope,
        FrontEndScreenBundle::VisualScopeMap visual_scopes,
        const bool valid_capability = true)
    {
        return FrontEndScreenBundle(key, std::move(widget_document),
            std::move(primary_scope), std::move(visual_scopes), {}, {}, {},
            RetailFrontEndPresentationCapabilityTestAccess::Make(
                valid_capability));
    }
};
} // namespace omega::content::detail

namespace
{
using omega::asset::Float3IR;
using omega::content::FrontEndScreenBundle;
using omega::content::FrontEndScreenKey;
using omega::frontend::kIdentityAffineTransform12;
using omega::frontend::presentation::InspectRetailPresentationRequirements;
using omega::frontend::presentation::RetailLayerCandidateKind;
using omega::frontend::presentation::RetailProjectedGeometryCoverage;
using omega::frontend::presentation::RetailPresentationInspectionError;
using omega::frontend::presentation::RetailPresentationInspectionLimits;
using omega::frontend::presentation::RetailPresentationInspectionResult;
using omega::frontend::presentation::RetailPresentationRequirement;
using omega::frontend::presentation::RetailPresentationRequirements;
using omega::frontend::presentation::
    kRetailPresentationInspectionFixedCoverageScratchBytes;
using omega::frontend::presentation::
    kRetailPresentationInspectionVisualRecordScratchBytes;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void CheckError(const RetailPresentationInspectionResult& result,
    const RetailPresentationInspectionError error,
    const std::string_view message)
{
    Check(!result && result.error() == error, message);
}

[[nodiscard]] omega::asset::FrontendWidgetIR MakeWidget(
    std::string identifier, const bool visible = true)
{
    return omega::asset::FrontendWidgetIR{
        .kind = omega::asset::FrontendWidgetKind::Container,
        .identifier = std::move(identifier),
        .rectangle = {
            .left = -320.0F,
            .top = 224.0F,
            .width = 640.0F,
            .height = 448.0F,
        },
        .visible = visible,
        .enabled = true,
    };
}

[[nodiscard]] omega::asset::FrontendVisualNodeIR MakeQuad(
    std::string identifier, const float left = -320.0F,
    const float right = 320.0F)
{
    return omega::asset::FrontendVisualNodeIR{
        .identifier = std::move(identifier),
        .transform_values = kIdentityAffineTransform12.column_vectors,
        .positions = {
            Float3IR{.x = left, .y = 17.0F, .z = 224.0F},
            Float3IR{.x = right, .y = 17.0F, .z = 224.0F},
            Float3IR{.x = right, .y = 17.0F, .z = -224.0F},
            Float3IR{.x = left, .y = 17.0F, .z = -224.0F},
        },
        .uvs = {{.u = 0.0F, .v = 0.0F}},
        .colors = {{
            .red = 255U,
            .green = 255U,
            .blue = 255U,
            .alpha = 255U,
        }},
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
}

[[nodiscard]] FrontEndScreenBundle MakeBundle(
    omega::asset::FrontendWidgetIR widget,
    omega::asset::FrontendVisualNodeIR visual,
    std::set<std::string, std::less<>> resources,
    const FrontEndScreenKey key = FrontEndScreenKey::Title,
    std::string scope_name = "SCREEN", const bool valid_capability = true)
{
    using omega::content::detail::FrontEndScreenBundleTestAccess;

    auto scope = FrontEndScreenBundleTestAccess::MakeScope(
        omega::asset::FrontendVisualDocumentIR{
            .root = std::move(visual),
        },
        std::move(resources));
    FrontEndScreenBundle::VisualScopeMap scopes;
    const std::string primary_scope = scope_name;
    scopes.emplace(std::move(scope_name), std::move(scope));
    return FrontEndScreenBundleTestAccess::MakeBundle(key,
        omega::asset::FrontendWidgetDocumentIR{
            .root = std::move(widget),
        },
        primary_scope, std::move(scopes), valid_capability);
}

[[nodiscard]] FrontEndScreenBundle MakeBaseline(
    const FrontEndScreenKey key = FrontEndScreenKey::Title,
    std::string widget_identifier = "CANVAS",
    std::string scope_name = "SCREEN")
{
    const std::string resource = widget_identifier + "_root";
    auto widget = MakeWidget(std::move(widget_identifier));
    widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    return MakeBundle(std::move(widget), MakeQuad(resource),
        std::set<std::string, std::less<>>{resource}, key,
        std::move(scope_name));
}

[[nodiscard]] FrontEndScreenBundle MakeTwoLaneBundle(
    const bool duplicate_binding)
{
    auto widget = MakeWidget("ROOT");
    auto first = MakeWidget("FIRST");
    first.binding = omega::asset::FrontendWidgetBindingIR{
        .resource_reference = duplicate_binding ? "FULL" : "LEFT",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    auto second = MakeWidget("SECOND");
    second.binding = omega::asset::FrontendWidgetBindingIR{
        .resource_reference = duplicate_binding ? "FULL" : "RIGHT",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    widget.children.push_back(std::move(first));
    widget.children.push_back(std::move(second));

    omega::asset::FrontendVisualNodeIR visual{
        .identifier = "WRAPPER",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    std::set<std::string, std::less<>> resources;
    if (duplicate_binding)
    {
        visual.children.push_back(MakeQuad("FULL"));
        resources.emplace("FULL");
    }
    else
    {
        visual.children.push_back(MakeQuad("LEFT", -320.0F, 0.0F));
        visual.children.push_back(MakeQuad("RIGHT", 0.0F, 320.0F));
        resources.emplace("LEFT");
        resources.emplace("RIGHT");
    }
    return MakeBundle(
        std::move(widget), std::move(visual), std::move(resources));
}

[[nodiscard]] FrontEndScreenBundle MakeAnimationBundle()
{
    auto widget = MakeWidget("CANVAS");
    widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
        .actions = {{
            .identifier = "UNEXPOSED",
            .mode = omega::asset::FrontendTimelineActionMode::Immediate,
        }},
    };
    auto text = MakeWidget("TEXT");
    text.kind = omega::asset::FrontendWidgetKind::Text;
    text.text_reference = "UNEXPOSED";
    text.font_reference = "UNEXPOSED";
    text.text_color = omega::asset::FrontendTextColorIR{
        .red = 1.0F,
        .green = 1.0F,
        .blue = 1.0F,
        .alpha = 1.0F,
    };
    text.text_alignment = omega::asset::FrontendTextAlignment::Center;
    widget.children.push_back(std::move(text));

    auto visual = MakeQuad("CANVAS_root");
    omega::asset::FrontendVertexAnimationTrackIR vertex;
    for (const auto& position : visual.positions)
    {
        vertex.position_subtracks.push_back({
            .keys = {{
                .timeline_tick = 0.0F,
                .position = position,
            }},
        });
    }
    visual.animation_tracks.emplace_back(std::move(vertex));
    visual.animation_tracks.emplace_back(
        omega::asset::FrontendScalarAnimationTrackIR{
            .target =
                omega::asset::FrontendScalarAnimationTarget::Opacity,
            .keys = {{.timeline_tick = 0.0F, .value = 1.0F}},
        });
    visual.animation_tracks.emplace_back(
        omega::asset::FrontendScalarAnimationTrackIR{
            .target =
                omega::asset::FrontendScalarAnimationTarget::UvOffsetU,
            .keys = {{.timeline_tick = 0.0F, .value = 0.0F}},
        });
    visual.animation_tracks.emplace_back(
        omega::asset::FrontendScalarAnimationTrackIR{
            .target =
                omega::asset::FrontendScalarAnimationTarget::UvOffsetV,
            .keys = {{.timeline_tick = 0.0F, .value = 0.0F}},
        });
    return MakeBundle(std::move(widget), std::move(visual),
        std::set<std::string, std::less<>>{"CANVAS_root"});
}

void TestContractScreensOwnershipAndPrivacy()
{
    static_assert(std::same_as<decltype(InspectRetailPresentationRequirements(
                                   std::declval<const FrontEndScreenBundle&>(),
                                   std::declval<
                                       RetailPresentationInspectionLimits>())),
        RetailPresentationInspectionResult>);
    static_assert(noexcept(InspectRetailPresentationRequirements(
        std::declval<const FrontEndScreenBundle&>(),
        std::declval<RetailPresentationInspectionLimits>())));
    static_assert(!std::is_convertible_v<RetailPresentationRequirements,
        omega::content::RetailFrontEndPresentationCapability>);
    static_assert(!std::is_convertible_v<RetailPresentationRequirements,
        omega::frontend::presentation::OwnedRgba8Frame>);
    static_assert(std::is_trivially_copyable_v<
        omega::frontend::presentation::RetailLayerCandidateCensus>);

    std::optional<RetailPresentationRequirements> first;
    for (const auto key : {FrontEndScreenKey::Title,
             FrontEndScreenKey::CreateAgent, FrontEndScreenKey::LoadAgent})
    {
        const auto inspected =
            InspectRetailPresentationRequirements(MakeBaseline(key));
        Check(inspected.has_value(),
            "Title, CreateAgent, and LoadAgent are structurally inspectable");
        if (!inspected)
            continue;
        Check(inspected->candidate_census.total() == 1U &&
                inspected->candidate_census.Count(
                    RetailLayerCandidateKind::VisualGeometry) == 1U &&
                inspected->projected_geometry_coverage.Count(
                    RetailProjectedGeometryCoverage::AllCanonicalSamples) ==
                    1U,
            "a root quad is one all-canonical-samples geometry candidate");
        Check(inspected->projected_geometry_union_evaluated &&
                inspected->
                    projected_geometry_union_covers_canonical_raster,
            "a lone static candidate covers the project raster geometrically");
        Check(!inspected->blockers.Contains(
                  RetailPresentationRequirement::VisualWidgetLaneMerge) &&
                !inspected->blockers.Contains(
                    RetailPresentationRequirement::CandidateOverlap) &&
                !inspected->blockers.Contains(
                    RetailPresentationRequirement::
                        IncompleteCandidateUnion),
            "the structural baseline needs no invented merge or coverage claim");
        if (!first)
            first = *inspected;
        else
            Check(*first == *inspected,
                "screen role is not leaked into the categorical report");
    }

    const auto owned = [] {
        auto bundle = MakeBaseline();
        return InspectRetailPresentationRequirements(bundle);
    }();
    Check(owned && first && *owned == *first,
        "the report owns all output after bundle destruction");

    const auto renamed_a = InspectRetailPresentationRequirements(
        MakeBaseline(FrontEndScreenKey::Title, "PRIVATE_A", "SCOPE_A"));
    const auto renamed_b = InspectRetailPresentationRequirements(
        MakeBaseline(FrontEndScreenKey::Title, "PRIVATE_B", "SCOPE_B"));
    Check(renamed_a && renamed_b && *renamed_a == *renamed_b,
        "renaming every retained identity leaves no identity in the output");

    auto invalid_widget = MakeWidget("CANVAS");
    invalid_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    CheckError(InspectRetailPresentationRequirements(MakeBundle(
                   std::move(invalid_widget), MakeQuad("CANVAS_root"),
                   {"CANVAS_root"}, FrontEndScreenKey::Title, "SCREEN",
                   false)),
        RetailPresentationInspectionError::InvalidRetailCapability,
        "a consumed presentation capability fails before traversal");
}

void TestDisjointOverlapAndDuplicateBindings()
{
    const auto disjoint =
        InspectRetailPresentationRequirements(MakeTwoLaneBundle(false));
    Check(disjoint && disjoint->candidate_census.total() == 2U &&
            disjoint->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 2U &&
            disjoint->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::
                    PartialCanonicalSamples) == 2U,
        "disjoint visual bindings remain two aggregate partial candidates");
    Check(disjoint && disjoint->projected_geometry_union_evaluated &&
            disjoint->
                projected_geometry_union_covers_canonical_raster &&
            disjoint->blockers.Contains(
                RetailPresentationRequirement::CandidateOrderingUnresolved) &&
            disjoint->blockers.Contains(
                RetailPresentationRequirement::VisualWidgetLaneMerge) &&
            !disjoint->blockers.Contains(
                RetailPresentationRequirement::CandidateOverlap),
        "disjoint candidates prove union only, never their merge order");

    const auto duplicate =
        InspectRetailPresentationRequirements(MakeTwoLaneBundle(true));
    Check(duplicate && duplicate->candidate_census.total() == 2U &&
            duplicate->blockers.Contains(
                RetailPresentationRequirement::CandidateOverlap) &&
            duplicate->blockers.Contains(
                RetailPresentationRequirement::CandidateOrderingUnresolved) &&
            duplicate->blockers.Contains(
                RetailPresentationRequirement::VisualWidgetLaneMerge),
        "duplicate bindings are retained as overlapping candidates");
    Check(duplicate && duplicate->projected_geometry_union_evaluated &&
            duplicate->
                projected_geometry_union_covers_canonical_raster,
        "overlap does not erase independently proven complete union coverage");
}

void TestNestedTransformsAndParentVisibility()
{
    auto widget = MakeWidget("CANVAS");
    widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    widget.binding->transform_values[9U] = 5.0F;

    omega::asset::FrontendVisualNodeIR visual{
        .identifier = "CANVAS_root",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    visual.transform_values[9U] = 5.0F;
    auto nested = MakeQuad("NESTED", -160.0F, 160.0F);
    nested.transform_values[0U] = 2.0F;
    nested.transform_values[9U] = -10.0F;
    visual.children.push_back(std::move(nested));
    const auto transformed = InspectRetailPresentationRequirements(
        MakeBundle(std::move(widget), std::move(visual),
            {"CANVAS_root"}));
    Check(transformed && transformed->candidate_census.total() == 1U &&
            transformed->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            transformed->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::AllCanonicalSamples) == 1U &&
            transformed->
                projected_geometry_union_covers_canonical_raster,
        "established binding and nested visual parent*local transforms compose");

    auto hidden_root = MakeWidget("ROOT", false);
    auto visible_child = MakeWidget("CHILD");
    visible_child.binding = omega::asset::FrontendWidgetBindingIR{
        .resource_reference = "FULL",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    hidden_root.children.push_back(std::move(visible_child));
    omega::asset::FrontendVisualNodeIR wrapper{
        .identifier = "WRAPPER",
        .transform_values = kIdentityAffineTransform12.column_vectors,
        .children = {MakeQuad("FULL")},
    };
    const auto ambiguous = InspectRetailPresentationRequirements(
        MakeBundle(std::move(hidden_root), std::move(wrapper), {"FULL"}));
    Check(ambiguous && ambiguous->candidate_census.total() == 1U &&
            ambiguous->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            ambiguous->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::Indeterminate) == 1U &&
            ambiguous->blockers.Contains(
                RetailPresentationRequirement::ParentVisibility) &&
            !ambiguous->projected_geometry_union_evaluated,
        "a visible child beneath a hidden parent remains explicitly ambiguous");
}

void TestTextActionAndEveryAnimationFamily()
{
    const auto inspected =
        InspectRetailPresentationRequirements(MakeAnimationBundle());
    Check(inspected &&
            inspected->blockers.Contains(
                RetailPresentationRequirement::TextEncoding) &&
            inspected->blockers.Contains(
                RetailPresentationRequirement::TextLayout) &&
            inspected->blockers.Contains(
                RetailPresentationRequirement::TextInterleave) &&
            inspected->blockers.Contains(
                RetailPresentationRequirement::ActionLifecycle),
        "text and action requirements remain categorical");
    Check(inspected &&
            inspected->blockers.Contains(
                RetailPresentationRequirement::AnimationTickSelection) &&
            inspected->blockers.Contains(RetailPresentationRequirement::
                    AnimationOpacityApplication) &&
            inspected->blockers.Contains(RetailPresentationRequirement::
                    AnimationUvOffsetApplication),
        "vertex, opacity, U-offset, and V-offset families remain unresolved");
    Check(inspected && inspected->candidate_census.total() == 2U &&
            inspected->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            inspected->candidate_census.Count(
                RetailLayerCandidateKind::Text) == 1U &&
            inspected->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::Indeterminate) == 1U &&
            !inspected->projected_geometry_union_evaluated,
        "dynamic geometry and text never fabricate static layer coverage");
}

void TestUnresolvedVisibilityDepthBindingAndSameLaneOrder()
{
    auto empty_widget = MakeWidget("EMPTY");
    omega::asset::FrontendVisualNodeIR empty_visual{
        .identifier = "EMPTY_VISUAL",
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    const auto empty = InspectRetailPresentationRequirements(
        MakeBundle(std::move(empty_widget), std::move(empty_visual), {}));
    Check(empty && empty->candidate_census.total() == 0U &&
            empty->projected_geometry_union_evaluated &&
            !empty->
                projected_geometry_union_covers_canonical_raster &&
            empty->blockers.Contains(
                RetailPresentationRequirement::IncompleteCandidateUnion),
        "an empty visible root produces an explicit empty census");

    auto hidden_widget = MakeWidget("CANVAS", false);
    hidden_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    const auto hidden = InspectRetailPresentationRequirements(
        MakeBundle(std::move(hidden_widget), MakeQuad("CANVAS_root"),
            {"CANVAS_root"}));
    Check(hidden && hidden->candidate_census.total() == 0U &&
            hidden->projected_geometry_union_evaluated &&
            !hidden->
                projected_geometry_union_covers_canonical_raster,
        "an authored hidden root is not promoted to a visible candidate");

    auto texture_widget = MakeWidget("CANVAS");
    texture_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    auto missing_texture = MakeQuad("CANVAS_root");
    missing_texture.texture_member = "UNRESOLVED.TDX";
    const auto textured = InspectRetailPresentationRequirements(
        MakeBundle(std::move(texture_widget), std::move(missing_texture),
            {"CANVAS_root"}));
    Check(textured &&
            textured->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            textured->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::Indeterminate) == 1U &&
            textured->blockers.Contains(
                RetailPresentationRequirement::MissingTextureBinding) &&
            textured->blockers.Contains(
                RetailPresentationRequirement::TextureVisibility) &&
            !textured->projected_geometry_union_evaluated,
        "full geometry with unresolved texture visibility stays indeterminate");

    auto opacity_widget = MakeWidget("CANVAS");
    opacity_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    auto zero_opacity = MakeQuad("CANVAS_root");
    zero_opacity.animation_tracks.emplace_back(
        omega::asset::FrontendScalarAnimationTrackIR{
            .target =
                omega::asset::FrontendScalarAnimationTarget::Opacity,
            .keys = {{.timeline_tick = 0.0F, .value = 0.0F}},
        });
    const auto opacity = InspectRetailPresentationRequirements(
        MakeBundle(std::move(opacity_widget), std::move(zero_opacity),
            {"CANVAS_root"}));
    Check(opacity &&
            opacity->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            opacity->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::Indeterminate) == 1U &&
            opacity->blockers.Contains(RetailPresentationRequirement::
                    AnimationOpacityApplication),
        "zero authored opacity never becomes visible geometric coverage");

    auto depth_widget = MakeWidget("CANVAS");
    depth_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    auto depth_visual = MakeQuad("CANVAS_root");
    for (auto& position : depth_visual.positions)
        position.y = -2.0F;
    const auto depth = InspectRetailPresentationRequirements(
        MakeBundle(std::move(depth_widget), std::move(depth_visual),
            {"CANVAS_root"}));
    Check(depth &&
            depth->blockers.Contains(
                RetailPresentationRequirement::DepthPolicy) &&
            !depth->blockers.Contains(
                RetailPresentationRequirement::ProjectionFailure) &&
            depth->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            depth->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::Indeterminate) == 1U,
        "finite out-of-range depth is unresolved policy, not projection failure");

    auto unresolved_widget = MakeWidget("MISSING");
    unresolved_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    const auto unresolved = InspectRetailPresentationRequirements(
        MakeBundle(std::move(unresolved_widget), MakeQuad("CANVAS_root"),
            {"CANVAS_root"}));
    Check(unresolved &&
            unresolved->candidate_census.Count(
                RetailLayerCandidateKind::UnresolvedBinding) == 1U &&
            unresolved->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 0U &&
            unresolved->projected_geometry_coverage.total() == 0U,
        "an unresolved binding is never promoted to geometry");

    const auto make_same_lane = [](const bool reverse) {
        auto widget = MakeWidget("CANVAS");
        widget.binding = omega::asset::FrontendWidgetBindingIR{
            .transform_values = kIdentityAffineTransform12.column_vectors,
        };
        omega::asset::FrontendVisualNodeIR visual{
            .identifier = "CANVAS_root",
            .transform_values = kIdentityAffineTransform12.column_vectors,
        };
        auto left = MakeQuad("LEFT", -320.0F, 0.0F);
        auto right = MakeQuad("RIGHT", 0.0F, 320.0F);
        if (reverse)
        {
            visual.children.push_back(std::move(right));
            visual.children.push_back(std::move(left));
        }
        else
        {
            visual.children.push_back(std::move(left));
            visual.children.push_back(std::move(right));
        }
        return MakeBundle(
            std::move(widget), std::move(visual), {"CANVAS_root"});
    };
    const auto forward =
        InspectRetailPresentationRequirements(make_same_lane(false));
    const auto reversed =
        InspectRetailPresentationRequirements(make_same_lane(true));
    Check(forward && reversed && *forward == *reversed &&
            forward->blockers.Contains(
                RetailPresentationRequirement::CandidateOrderingUnresolved) &&
            !forward->blockers.Contains(
                RetailPresentationRequirement::VisualWidgetLaneMerge),
        "same-lane visual reordering has an identical aggregate census");
}

void TestInvalidGeometryAndNonFiniteMath()
{
    for (std::size_t lane = 0U; lane < 3U; ++lane)
    {
        auto widget = MakeWidget("CANVAS");
        widget.binding = omega::asset::FrontendWidgetBindingIR{
            .transform_values = kIdentityAffineTransform12.column_vectors,
        };
        auto visual = MakeQuad("CANVAS_root");
        if (lane == 0U)
            visual.triangles[0U].position_indices[0U] = 4U;
        else if (lane == 1U)
            visual.triangles[0U].uv_indices[0U] = 1U;
        else
            visual.triangles[0U].color_indices[0U] = 1U;
        const auto inspected = InspectRetailPresentationRequirements(
            MakeBundle(
                std::move(widget), std::move(visual), {"CANVAS_root"}));
        Check(inspected && inspected->candidate_census.total() == 1U &&
                inspected->blockers.Contains(
                    RetailPresentationRequirement::InvalidGeometry) &&
                inspected->candidate_census.Count(
                    RetailLayerCandidateKind::VisualGeometry) == 1U &&
                inspected->projected_geometry_coverage.Count(
                    RetailProjectedGeometryCoverage::Indeterminate) == 1U &&
                !inspected->projected_geometry_union_evaluated,
            "every triangle index lane fails categorically without a frame");
    }

    auto transform_widget = MakeWidget("CANVAS");
    transform_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    transform_widget.binding->transform_values[0U] =
        std::numeric_limits<float>::quiet_NaN();
    const auto bad_transform = InspectRetailPresentationRequirements(
        MakeBundle(std::move(transform_widget), MakeQuad("CANVAS_root"),
            {"CANVAS_root"}));
    Check(bad_transform &&
            bad_transform->candidate_census.total() == 1U &&
            bad_transform->blockers.Contains(
                RetailPresentationRequirement::NonFiniteMath) &&
            bad_transform->candidate_census.Count(
                RetailLayerCandidateKind::VisualGeometry) == 1U &&
            bad_transform->projected_geometry_coverage.Count(
                RetailProjectedGeometryCoverage::Indeterminate) == 1U,
        "nonfinite widget math is explicit and never projected");

    auto position_widget = MakeWidget("CANVAS");
    position_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    auto nonfinite_position = MakeQuad("CANVAS_root");
    nonfinite_position.positions[0U].x =
        std::numeric_limits<float>::quiet_NaN();
    const auto bad_position = InspectRetailPresentationRequirements(
        MakeBundle(std::move(position_widget),
            std::move(nonfinite_position), {"CANVAS_root"}));
    Check(bad_position &&
            bad_position->blockers.Contains(
                RetailPresentationRequirement::NonFiniteMath),
        "nonfinite referenced geometry is categorized");
}

void TestIncompleteUnionAndEveryLimitClass()
{
    auto partial_widget = MakeWidget("CANVAS");
    partial_widget.binding = omega::asset::FrontendWidgetBindingIR{
        .transform_values = kIdentityAffineTransform12.column_vectors,
    };
    const auto partial = InspectRetailPresentationRequirements(
        MakeBundle(std::move(partial_widget),
            MakeQuad("CANVAS_root", -320.0F, 0.0F), {"CANVAS_root"}));
    Check(partial && partial->projected_geometry_union_evaluated &&
            !partial->
                projected_geometry_union_covers_canonical_raster &&
            partial->blockers.Contains(
                RetailPresentationRequirement::IncompleteCandidateUnion),
        "a proven partial static union is distinct from an indeterminate union");

    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_widget_nodes = 0U,
                   }),
        RetailPresentationInspectionError::InvalidLimits,
        "zero is never an unbounded limit");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_widget_nodes =
                           omega::frontend::presentation::
                               kRetailPresentationInspectionMaximumWidgetNodes +
                           1U,
                   }),
        RetailPresentationInspectionError::InvalidLimits,
        "callers cannot raise fixed ceilings");
    CheckError(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
                   RetailPresentationInspectionLimits{
                       .maximum_widget_nodes = 2U,
                   }),
        RetailPresentationInspectionError::WidgetNodeLimitExceeded,
        "widget node limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
              RetailPresentationInspectionLimits{
                  .maximum_widget_nodes = 3U,
              }).has_value(),
        "the exact widget node budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
                   RetailPresentationInspectionLimits{
                       .maximum_visual_nodes = 2U,
                   }),
        RetailPresentationInspectionError::VisualNodeLimitExceeded,
        "visual node limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
              RetailPresentationInspectionLimits{
                  .maximum_visual_nodes = 3U,
              }).has_value(),
        "the exact visual node budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
                   RetailPresentationInspectionLimits{
                       .maximum_widget_depth = 1U,
                   }),
        RetailPresentationInspectionError::WidgetDepthLimitExceeded,
        "widget depth limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
              RetailPresentationInspectionLimits{
                  .maximum_widget_depth = 2U,
              }).has_value(),
        "the exact widget depth budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
                   RetailPresentationInspectionLimits{
                       .maximum_visual_depth = 1U,
                   }),
        RetailPresentationInspectionError::VisualDepthLimitExceeded,
        "visual depth limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
              RetailPresentationInspectionLimits{
                  .maximum_visual_depth = 2U,
              }).has_value(),
        "the exact visual depth budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
                   RetailPresentationInspectionLimits{
                       .maximum_candidates = 1U,
                   }),
        RetailPresentationInspectionError::CandidateLimitExceeded,
        "candidate limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeTwoLaneBundle(false),
              RetailPresentationInspectionLimits{
                  .maximum_candidates = 2U,
              }).has_value(),
        "the exact candidate budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_positions = 3U,
                   }),
        RetailPresentationInspectionError::PositionLimitExceeded,
        "position limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_positions = 4U,
              }).has_value(),
        "the exact position budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeAnimationBundle(),
                   RetailPresentationInspectionLimits{
                       .maximum_animation_tracks = 3U,
                   }),
        RetailPresentationInspectionError::AnimationTrackLimitExceeded,
        "animation track limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeAnimationBundle(),
              RetailPresentationInspectionLimits{
                  .maximum_animation_tracks = 4U,
              }).has_value(),
        "the exact animation-track budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_lookup_entries = 1U,
                   }),
        RetailPresentationInspectionError::LookupEntryLimitExceeded,
        "lookup entry limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_lookup_entries = 2U,
              }).has_value(),
        "the exact lookup-entry budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_identity_bytes = 33U,
                   }),
        RetailPresentationInspectionError::IdentityByteLimitExceeded,
        "identity comparison byte limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_identity_bytes = 34U,
              }).has_value(),
        "the exact identity-byte budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeTwoLaneBundle(true),
                   RetailPresentationInspectionLimits{
                       .maximum_candidate_node_visits = 1U,
                   }),
        RetailPresentationInspectionError::CandidateNodeVisitLimitExceeded,
        "candidate node visit limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeTwoLaneBundle(true),
              RetailPresentationInspectionLimits{
                  .maximum_candidate_node_visits = 2U,
              }).has_value(),
        "the exact candidate-node visit budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_lookup_steps = 5U,
                   }),
        RetailPresentationInspectionError::LookupStepLimitExceeded,
        "combined lookup step limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_lookup_steps = 6U,
              }).has_value(),
        "the exact combined lookup-step budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_triangle_visits = 1U,
                   }),
        RetailPresentationInspectionError::TriangleVisitLimitExceeded,
        "triangle visit limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_triangle_visits = 2U,
              }).has_value(),
        "the exact triangle-visit budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_covered_samples = 286'719U,
                   }),
        RetailPresentationInspectionError::CoveredSampleLimitExceeded,
        "covered sample limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_covered_samples = 286'720U,
              }).has_value(),
        "the exact covered-sample budget is admitted");
    constexpr std::uint64_t kBaselineScratchBytes =
        kRetailPresentationInspectionFixedCoverageScratchBytes +
        kRetailPresentationInspectionVisualRecordScratchBytes;
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_scratch_bytes =
                           kBaselineScratchBytes - 1U,
                   }),
        RetailPresentationInspectionError::ScratchLimitExceeded,
        "scratch allocation limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_scratch_bytes = kBaselineScratchBytes,
              }).has_value(),
        "the exact architecture-independent scratch budget is admitted");
    CheckError(InspectRetailPresentationRequirements(MakeBaseline(),
                   RetailPresentationInspectionLimits{
                       .maximum_output_bytes = 1U,
                   }),
        RetailPresentationInspectionError::OutputLimitExceeded,
        "owned output limit is terminal");
    Check(InspectRetailPresentationRequirements(MakeBaseline(),
              RetailPresentationInspectionLimits{
                  .maximum_output_bytes =
                      sizeof(RetailPresentationRequirements),
              }).has_value(),
        "the exact fixed aggregate output size is admitted");

    const std::array<RetailPresentationInspectionLimits, 15U>
        above_hard_ceiling{{
            {.maximum_widget_nodes =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumWidgetNodes +
                    1U},
            {.maximum_visual_nodes =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumVisualNodes +
                    1U},
            {.maximum_widget_depth =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumWidgetDepth +
                    1U},
            {.maximum_visual_depth =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumVisualDepth +
                    1U},
            {.maximum_candidates =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumCandidates +
                    1U},
            {.maximum_positions =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumPositions +
                    1U},
            {.maximum_animation_tracks =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumAnimationTracks +
                    1U},
            {.maximum_lookup_entries =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumLookupEntries +
                    1U},
            {.maximum_identity_bytes =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumIdentityBytes +
                    1U},
            {.maximum_candidate_node_visits =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumCandidateNodeVisits +
                    1U},
            {.maximum_lookup_steps =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumLookupSteps +
                    1U},
            {.maximum_triangle_visits =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumTriangleVisits +
                    1U},
            {.maximum_covered_samples =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumCoveredSamples +
                    1U},
            {.maximum_scratch_bytes =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumScratchBytes +
                    1U},
            {.maximum_output_bytes =
                    omega::frontend::presentation::
                        kRetailPresentationInspectionMaximumOutputBytes +
                    1U},
        }};
    for (const auto& limits : above_hard_ceiling)
    {
        CheckError(InspectRetailPresentationRequirements(
                       MakeBaseline(), limits),
            RetailPresentationInspectionError::InvalidLimits,
            "every caller limit rejects hard-ceiling plus one");
    }
}
} // namespace

int main()
{
    TestContractScreensOwnershipAndPrivacy();
    TestDisjointOverlapAndDuplicateBindings();
    TestNestedTransformsAndParentVisibility();
    TestTextActionAndEveryAnimationFamily();
    TestUnresolvedVisibilityDepthBindingAndSameLaneOrder();
    TestInvalidGeometryAndNonFiniteMath();
    TestIncompleteUnionAndEveryLimitClass();

    if (failures != 0)
    {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    return 0;
}
