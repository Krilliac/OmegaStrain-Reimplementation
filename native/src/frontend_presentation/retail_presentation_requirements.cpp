#include "omega/frontend_presentation/retail_presentation_requirements.h"

#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend/compositor_math.h"
#include "omega/frontend_presentation/screen_space_triangle_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <span>
#include <utility>
#include <variant>

namespace omega::frontend::presentation
{
namespace
{
constexpr std::uint64_t kCanonicalPixelCount =
    static_cast<std::uint64_t>(kCanonicalRasterWidth) *
    static_cast<std::uint64_t>(kCanonicalRasterHeight);
constexpr std::uint64_t kCoverageScratchBytes =
    kRetailPresentationInspectionFixedCoverageScratchBytes;
static_assert(kCoverageScratchBytes ==
    kCanonicalPixelCount *
        (sizeof(std::uint8_t) + 2U * sizeof(std::uint32_t)));
constexpr ScreenSpaceClipRect kCanonicalClip{
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(kCanonicalRasterWidth),
    .bottom = static_cast<std::int32_t>(kCanonicalRasterHeight),
};

enum class TransformStatus : std::uint8_t
{
    Valid = 0U,
    NonFinite,
    Failed,
};

struct VisualRecord final
{
    const content::FrontEndVisualScope* scope = nullptr;
    const asset::FrontendVisualNodeIR* node = nullptr;
    AffineTransform12 world_transform = kIdentityAffineTransform12;
    TransformStatus transform_status = TransformStatus::Valid;
    std::uint32_t subtree_end = 0U;
};
static_assert(sizeof(VisualRecord) <=
    kRetailPresentationInspectionVisualRecordScratchBytes);

struct VisualPreflight final
{
    std::uint32_t node_count = 0U;
    std::uint64_t position_count = 0U;
    std::uint64_t animation_track_count = 0U;
    std::uint64_t lookup_entry_count = 0U;
};

struct CandidateCoverageContext final
{
    std::uint32_t* stamps = nullptr;
    std::uint32_t* covered_indices = nullptr;
    std::uint32_t ordinal = 0U;
    std::uint32_t covered_index_count = 0U;
    bool overlap = false;
};

[[nodiscard]] bool IsFinite(const asset::FrontendWidgetRectangleIR& value) noexcept
{
    return std::isfinite(value.left) && std::isfinite(value.top) &&
           std::isfinite(value.width) && std::isfinite(value.height);
}

[[nodiscard]] bool IsFinite(const asset::FrontendTextColorIR& value) noexcept
{
    return std::isfinite(value.red) && std::isfinite(value.green) &&
           std::isfinite(value.blue) && std::isfinite(value.alpha);
}

[[nodiscard]] bool IsFinite(const asset::FrontendUvIR& value) noexcept
{
    return std::isfinite(value.u) && std::isfinite(value.v);
}

[[nodiscard]] bool HasTextSemantics(
    const asset::FrontendWidgetIR& widget) noexcept
{
    return widget.kind == asset::FrontendWidgetKind::Text ||
           widget.kind == asset::FrontendWidgetKind::Button ||
           widget.text_reference.has_value() ||
           widget.font_reference.has_value() ||
           widget.text_color.has_value() ||
           widget.text_alignment.has_value();
}

[[nodiscard]] bool IsValidLimits(
    const RetailPresentationInspectionLimits& limits) noexcept
{
    return limits.maximum_widget_nodes != 0U &&
           limits.maximum_widget_nodes <=
               kRetailPresentationInspectionMaximumWidgetNodes &&
           limits.maximum_visual_nodes != 0U &&
           limits.maximum_visual_nodes <=
               kRetailPresentationInspectionMaximumVisualNodes &&
           limits.maximum_widget_depth != 0U &&
           limits.maximum_widget_depth <=
               kRetailPresentationInspectionMaximumWidgetDepth &&
           limits.maximum_visual_depth != 0U &&
           limits.maximum_visual_depth <=
               kRetailPresentationInspectionMaximumVisualDepth &&
           limits.maximum_candidates != 0U &&
           limits.maximum_candidates <=
               kRetailPresentationInspectionMaximumCandidates &&
           limits.maximum_positions != 0U &&
           limits.maximum_positions <=
               kRetailPresentationInspectionMaximumPositions &&
           limits.maximum_animation_tracks != 0U &&
           limits.maximum_animation_tracks <=
               kRetailPresentationInspectionMaximumAnimationTracks &&
           limits.maximum_lookup_entries != 0U &&
           limits.maximum_lookup_entries <=
               kRetailPresentationInspectionMaximumLookupEntries &&
           limits.maximum_identity_bytes != 0U &&
           limits.maximum_identity_bytes <=
               kRetailPresentationInspectionMaximumIdentityBytes &&
           limits.maximum_candidate_node_visits != 0U &&
           limits.maximum_candidate_node_visits <=
               kRetailPresentationInspectionMaximumCandidateNodeVisits &&
           limits.maximum_lookup_steps != 0U &&
           limits.maximum_lookup_steps <=
               kRetailPresentationInspectionMaximumLookupSteps &&
           limits.maximum_triangle_visits != 0U &&
           limits.maximum_triangle_visits <=
               kRetailPresentationInspectionMaximumTriangleVisits &&
           limits.maximum_covered_samples != 0U &&
           limits.maximum_covered_samples <=
               kRetailPresentationInspectionMaximumCoveredSamples &&
           limits.maximum_scratch_bytes != 0U &&
           limits.maximum_scratch_bytes <=
               kRetailPresentationInspectionMaximumScratchBytes &&
           limits.maximum_output_bytes != 0U &&
           limits.maximum_output_bytes <=
               kRetailPresentationInspectionMaximumOutputBytes;
}

[[nodiscard]] TransformStatus MapTransformStatus(
    const CompositorMathError error) noexcept
{
    return error == CompositorMathError::NonFiniteInput
        ? TransformStatus::NonFinite
        : TransformStatus::Failed;
}

[[nodiscard]] ScreenSpaceTriangleVisitControl RecordCoveredPixel(
    void* const opaque_context,
    const ScreenSpaceTriangleSample& sample) noexcept
{
    auto& context =
        *static_cast<CandidateCoverageContext*>(opaque_context);
    const auto index = static_cast<std::uint32_t>(
        static_cast<std::uint32_t>(sample.y) * kCanonicalRasterWidth +
        static_cast<std::uint32_t>(sample.x));
    if (context.stamps[index] == context.ordinal)
    {
        context.overlap = true;
        return ScreenSpaceTriangleVisitControl::Continue;
    }
    context.stamps[index] = context.ordinal;
    context.covered_indices[context.covered_index_count++] = index;
    return ScreenSpaceTriangleVisitControl::Continue;
}
} // namespace

// Named in this namespace only so RetailPresentationRequirementSet can keep
// mutation private. The friend declaration exposes no callable API.
struct RetailPresentationInspectionContext final
{
    const content::FrontEndScreenBundle& bundle;
    RetailPresentationInspectionLimits limits;
    RetailPresentationRequirements report;
    std::unique_ptr<VisualRecord[]> visual_records;
    std::unique_ptr<std::uint8_t[]> union_coverage;
    std::unique_ptr<std::uint32_t[]> candidate_stamps;
    std::unique_ptr<std::uint32_t[]> candidate_covered_indices;
    std::uint32_t visual_record_count = 0U;
    std::uint32_t visual_record_cursor = 0U;
    std::uint32_t widget_count = 0U;
    std::uint64_t identity_bytes = 0U;
    std::uint64_t lookup_entry_count = 0U;
    std::uint64_t lookup_steps = 0U;
    std::uint64_t candidate_node_visits = 0U;
    std::uint64_t triangle_visits = 0U;
    std::uint64_t covered_samples = 0U;
    std::uint32_t candidate_count = 0U;
    std::uint32_t candidate_widget_lanes = 0U;
    bool has_visual_candidate = false;
    bool has_nonvisual_candidate = false;
    bool any_indeterminate_candidate = false;

    void AddBlocker(const RetailPresentationRequirement blocker) noexcept
    {
        report.blockers.Add(blocker);
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    DebitIdentityBytes(const std::size_t byte_count) noexcept
    {
        const auto bytes = static_cast<std::uint64_t>(byte_count);
        if (bytes > limits.maximum_identity_bytes - identity_bytes)
        {
            return std::unexpected(
                RetailPresentationInspectionError::
                    IdentityByteLimitExceeded);
        }
        identity_bytes += bytes;
        return {};
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    DebitLookupEntries(const std::size_t entry_count,
        VisualPreflight& preflight) const noexcept
    {
        const auto entries = static_cast<std::uint64_t>(entry_count);
        if (entries >
            limits.maximum_lookup_entries -
                preflight.lookup_entry_count)
        {
            return std::unexpected(
                RetailPresentationInspectionError::
                    LookupEntryLimitExceeded);
        }
        preflight.lookup_entry_count += entries;
        return {};
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    DebitLookupSteps(const std::uint64_t step_count) noexcept
    {
        if (step_count > limits.maximum_lookup_steps - lookup_steps)
        {
            return std::unexpected(
                RetailPresentationInspectionError::
                    LookupStepLimitExceeded);
        }
        lookup_steps += step_count;
        return {};
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    PreflightVisualNode(const asset::FrontendVisualNodeIR& node,
        const std::uint32_t depth, VisualPreflight& preflight) noexcept
    {
        if (depth > limits.maximum_visual_depth)
        {
            return std::unexpected(
                RetailPresentationInspectionError::VisualDepthLimitExceeded);
        }
        if (preflight.node_count == limits.maximum_visual_nodes)
        {
            return std::unexpected(
                RetailPresentationInspectionError::VisualNodeLimitExceeded);
        }
        ++preflight.node_count;

        auto debit_identity =
            DebitIdentityBytes(node.identifier.size());
        if (!debit_identity)
            return debit_identity;
        if (node.texture_member)
        {
            debit_identity =
                DebitIdentityBytes(node.texture_member->size());
            if (!debit_identity)
                return debit_identity;
        }

        const auto position_count =
            static_cast<std::uint64_t>(node.positions.size());
        if (position_count >
            limits.maximum_positions - preflight.position_count)
        {
            return std::unexpected(
                RetailPresentationInspectionError::PositionLimitExceeded);
        }
        preflight.position_count += position_count;

        const auto track_count =
            static_cast<std::uint64_t>(node.animation_tracks.size());
        if (track_count >
            limits.maximum_animation_tracks -
                preflight.animation_track_count)
        {
            return std::unexpected(
                RetailPresentationInspectionError::
                    AnimationTrackLimitExceeded);
        }
        preflight.animation_track_count += track_count;

        for (const auto& child : node.children)
        {
            const auto result =
                PreflightVisualNode(child, depth + 1U, preflight);
            if (!result)
                return result;
        }
        return {};
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    PreflightVisuals(VisualPreflight& preflight) noexcept
    {
        for (const auto& [scope_name, scope] : bundle.visual_scopes())
        {
            auto debit_entries =
                DebitLookupEntries(1U, preflight);
            if (!debit_entries)
                return debit_entries;
            debit_entries =
                DebitLookupEntries(scope.resources().size(), preflight);
            if (!debit_entries)
                return debit_entries;
            debit_entries =
                DebitLookupEntries(scope.textures().size(), preflight);
            if (!debit_entries)
                return debit_entries;

            auto debit_identity = DebitIdentityBytes(scope_name.size());
            if (!debit_identity)
                return debit_identity;
            for (const auto& resource : scope.resources())
            {
                debit_identity = DebitIdentityBytes(resource.size());
                if (!debit_identity)
                    return debit_identity;
            }
            for (const auto& [texture_name, unused_texture] :
                scope.textures())
            {
                (void)unused_texture;
                debit_identity =
                    DebitIdentityBytes(texture_name.size());
                if (!debit_identity)
                    return debit_identity;
            }

            const auto result =
                PreflightVisualNode(scope.document().root, 1U, preflight);
            if (!result)
                return result;
        }
        return {};
    }

    void BuildVisualRecords(const content::FrontEndVisualScope& scope,
        const asset::FrontendVisualNodeIR& node,
        const AffineTransform12& parent_transform,
        const TransformStatus parent_status) noexcept
    {
        const std::uint32_t own_index = visual_record_cursor++;
        auto& record = visual_records[own_index];
        record.scope = &scope;
        record.node = &node;
        record.transform_status = parent_status;
        if (parent_status == TransformStatus::Valid)
        {
            const auto composed = ComposeAffineTransforms(parent_transform,
                AffineTransform12{
                    .column_vectors = node.transform_values,
                });
            if (composed)
                record.world_transform = *composed;
            else
                record.transform_status = MapTransformStatus(composed.error());
        }

        for (const auto& child : node.children)
        {
            BuildVisualRecords(scope, child, record.world_transform,
                record.transform_status);
        }
        record.subtree_end = visual_record_cursor;
    }

    void BuildAllVisualRecords() noexcept
    {
        for (const auto& [unused_scope_name, scope] : bundle.visual_scopes())
        {
            (void)unused_scope_name;
            BuildVisualRecords(scope, scope.document().root,
                kIdentityAffineTransform12, TransformStatus::Valid);
        }
    }

    [[nodiscard]] const VisualRecord* FindVisualRecord(
        const content::FrontEndVisualScope& scope,
        const asset::FrontendVisualNodeIR& node) const noexcept
    {
        for (std::uint32_t index = 0U; index < visual_record_count; ++index)
        {
            const auto& record = visual_records[index];
            if (record.scope == &scope && record.node == &node)
                return &record;
        }
        return nullptr;
    }

    [[nodiscard]] std::expected<std::uint32_t,
        RetailPresentationInspectionError>
    ReserveCandidate() noexcept
    {
        if (candidate_count == limits.maximum_candidates)
        {
            return std::unexpected(
                RetailPresentationInspectionError::CandidateLimitExceeded);
        }
        return candidate_count++;
    }

    void RecordCandidate(const RetailLayerCandidateKind kind,
        const RetailProjectedGeometryCoverage coverage) noexcept
    {
        report.candidate_census.Add(kind);
        if (kind == RetailLayerCandidateKind::VisualGeometry)
        {
            has_visual_candidate = true;
            report.projected_geometry_coverage.Add(coverage);
        }
        else
            has_nonvisual_candidate = true;
        if (coverage == RetailProjectedGeometryCoverage::Indeterminate)
            any_indeterminate_candidate = true;
    }

    [[nodiscard]] bool InspectAnimations(
        const asset::FrontendVisualNodeIR& node) noexcept
    {
        const bool has_animation = !node.animation_tracks.empty();
        if (!node.animation_tracks.empty())
        {
            AddBlocker(
                RetailPresentationRequirement::AnimationTickSelection);
        }
        for (const auto& track : node.animation_tracks)
        {
            if (track.valueless_by_exception())
            {
                AddBlocker(RetailPresentationRequirement::InvalidGeometry);
                continue;
            }
            if (const auto* const vertex =
                    std::get_if<asset::FrontendVertexAnimationTrackIR>(
                        &track))
            {
                if (vertex->position_subtracks.size() != node.positions.size())
                {
                    AddBlocker(
                        RetailPresentationRequirement::InvalidGeometry);
                }
                continue;
            }

            const auto* const scalar =
                std::get_if<asset::FrontendScalarAnimationTrackIR>(&track);
            if (scalar == nullptr)
            {
                AddBlocker(RetailPresentationRequirement::InvalidGeometry);
                continue;
            }
            switch (scalar->target)
            {
            case asset::FrontendScalarAnimationTarget::Opacity:
                AddBlocker(RetailPresentationRequirement::
                        AnimationOpacityApplication);
                break;
            case asset::FrontendScalarAnimationTarget::UvOffsetU:
            case asset::FrontendScalarAnimationTarget::UvOffsetV:
                AddBlocker(RetailPresentationRequirement::
                        AnimationUvOffsetApplication);
                break;
            default:
                AddBlocker(RetailPresentationRequirement::InvalidGeometry);
                break;
            }
        }
        return has_animation;
    }

    [[nodiscard]] bool ValidateAndProjectTriangle(
        const asset::FrontendVisualNodeIR& node,
        const asset::FrontendTriangleIR& triangle,
        const AffineTransform12& transform,
        std::array<ScreenSpaceTriangleVertex, 3U>& projected,
        bool& unresolved_visibility) noexcept
    {
        for (std::size_t vertex = 0U; vertex < projected.size(); ++vertex)
        {
            const auto position_index = triangle.position_indices[vertex];
            const auto uv_index = triangle.uv_indices[vertex];
            const auto color_index = triangle.color_indices[vertex];
            if (position_index >= node.positions.size() ||
                uv_index >= node.uvs.size() ||
                color_index >= node.colors.size())
            {
                AddBlocker(RetailPresentationRequirement::InvalidGeometry);
                return false;
            }
            if (!IsFinite(node.uvs[uv_index]))
            {
                AddBlocker(RetailPresentationRequirement::NonFiniteMath);
                return false;
            }
            if (node.colors[color_index].alpha != 255U)
            {
                AddBlocker(
                    RetailPresentationRequirement::VertexOpacityPolicy);
                unresolved_visibility = true;
            }

            const auto transformed =
                TransformPoint(transform, node.positions[position_index]);
            if (!transformed)
            {
                AddBlocker(transformed.error() ==
                        CompositorMathError::NonFiniteInput
                    ? RetailPresentationRequirement::NonFiniteMath
                    : RetailPresentationRequirement::TransformFailure);
                return false;
            }
            const auto projection =
                ProjectInterfaceElementPoint(*transformed);
            if (!projection)
            {
                AddBlocker(projection.error() ==
                        CompositorMathError::NonFiniteInput
                    ? RetailPresentationRequirement::NonFiniteMath
                    : RetailPresentationRequirement::ProjectionFailure);
                return false;
            }
            if (projection->depth_rank < 0.0F ||
                projection->depth_rank > 1.0F)
            {
                AddBlocker(RetailPresentationRequirement::DepthPolicy);
                unresolved_visibility = true;
            }
            projected[vertex] = ScreenSpaceTriangleVertex{
                .x = projection->raster_position.x,
                .y = projection->raster_position.y,
                .s = 0.0F,
                .t = 0.0F,
                .q = 1.0F,
            };
        }
        return true;
    }

    [[nodiscard]] std::expected<RetailProjectedGeometryCoverage,
        RetailPresentationInspectionError>
    InspectStaticGeometry(const asset::FrontendVisualNodeIR& node,
        const AffineTransform12& transform,
        const std::uint32_t candidate_ordinal,
        bool unresolved_visibility) noexcept
    {
        const auto triangle_count =
            static_cast<std::uint64_t>(node.triangles.size());
        if (triangle_count >
            limits.maximum_triangle_visits - triangle_visits)
        {
            return std::unexpected(RetailPresentationInspectionError::
                    TriangleVisitLimitExceeded);
        }
        triangle_visits += triangle_count;

        CandidateCoverageContext coverage{
            .stamps = candidate_stamps.get(),
            .covered_indices = candidate_covered_indices.get(),
            .ordinal = candidate_ordinal,
        };
        for (const auto& triangle : node.triangles)
        {
            std::array<ScreenSpaceTriangleVertex, 3U> projected{};
            if (!ValidateAndProjectTriangle(
                    node, triangle, transform, projected,
                    unresolved_visibility))
            {
                return RetailProjectedGeometryCoverage::Indeterminate;
            }

            const std::uint64_t remaining =
                limits.maximum_covered_samples - covered_samples;
            const auto triangle_budget = std::min(
                std::max(remaining, std::uint64_t{1U}),
                kScreenSpaceTriangleMaximumCoveredPixels);
            const auto rasterized = RasterizeScreenSpaceTriangle(
                projected, kCanonicalClip, RecordCoveredPixel, &coverage,
                ScreenSpaceTriangleLimits{
                    .maximum_affine_channels = 0U,
                    .maximum_clip_width = kCanonicalRasterWidth,
                    .maximum_clip_height = kCanonicalRasterHeight,
                    .maximum_covered_pixels = triangle_budget,
                });
            if (!rasterized)
            {
                switch (rasterized.error())
                {
                case ScreenSpaceTriangleError::LimitExceeded:
                    return std::unexpected(
                        RetailPresentationInspectionError::
                            CoveredSampleLimitExceeded);
                case ScreenSpaceTriangleError::NonFiniteInput:
                    AddBlocker(
                        RetailPresentationRequirement::NonFiniteMath);
                    return RetailProjectedGeometryCoverage::Indeterminate;
                case ScreenSpaceTriangleError::DegenerateTriangle:
                    AddBlocker(
                        RetailPresentationRequirement::InvalidGeometry);
                    return RetailProjectedGeometryCoverage::Indeterminate;
                case ScreenSpaceTriangleError::ArithmeticFailure:
                case ScreenSpaceTriangleError::PerspectiveDivideFailure:
                    AddBlocker(
                        RetailPresentationRequirement::ProjectionFailure);
                    return RetailProjectedGeometryCoverage::Indeterminate;
                case ScreenSpaceTriangleError::InvalidLimits:
                case ScreenSpaceTriangleError::InvalidClip:
                case ScreenSpaceTriangleError::
                    InconsistentAffineChannelCount:
                case ScreenSpaceTriangleError::MissingVisitor:
                case ScreenSpaceTriangleError::VisitorStopped:
                case ScreenSpaceTriangleError::InvalidVisitorResult:
                    AddBlocker(
                        RetailPresentationRequirement::InvalidGeometry);
                    return RetailProjectedGeometryCoverage::Indeterminate;
                }
                AddBlocker(
                    RetailPresentationRequirement::InvalidGeometry);
                return RetailProjectedGeometryCoverage::Indeterminate;
            }
            if (rasterized->covered_pixel_count > remaining)
            {
                return std::unexpected(RetailPresentationInspectionError::
                        CoveredSampleLimitExceeded);
            }
            covered_samples += rasterized->covered_pixel_count;
        }

        if (coverage.overlap)
        {
            AddBlocker(
                RetailPresentationRequirement::CandidateOverlap);
        }
        if (!unresolved_visibility)
        {
            for (std::uint32_t index = 0U;
                 index < coverage.covered_index_count; ++index)
            {
                const auto pixel = coverage.covered_indices[index];
                if (union_coverage[pixel] != 0U)
                {
                    AddBlocker(
                        RetailPresentationRequirement::CandidateOverlap);
                }
                union_coverage[pixel] = 1U;
            }
        }

        if (unresolved_visibility)
            return RetailProjectedGeometryCoverage::Indeterminate;
        if (coverage.covered_index_count == 0U)
        {
            return RetailProjectedGeometryCoverage::NoCanonicalSamples;
        }
        if (coverage.covered_index_count == kCanonicalPixelCount)
        {
            return RetailProjectedGeometryCoverage::AllCanonicalSamples;
        }
        return RetailProjectedGeometryCoverage::PartialCanonicalSamples;
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    InspectVisualSubtree(const VisualRecord& root_record,
        const AffineTransform12& widget_transform,
        const TransformStatus widget_transform_status,
        const bool visibility_ambiguous) noexcept
    {
        const auto first_index = static_cast<std::uint32_t>(
            &root_record - visual_records.get());
        for (std::uint32_t index = first_index;
             index < root_record.subtree_end; ++index)
        {
            if (candidate_node_visits ==
                limits.maximum_candidate_node_visits)
            {
                return std::unexpected(
                    RetailPresentationInspectionError::
                        CandidateNodeVisitLimitExceeded);
            }
            ++candidate_node_visits;
            const auto& record = visual_records[index];
            const auto& node = *record.node;
            const bool animated = InspectAnimations(node);
            bool unresolved_visibility =
                visibility_ambiguous || animated;
            if (node.texture_member)
            {
                AddBlocker(
                    RetailPresentationRequirement::TextureVisibility);
                unresolved_visibility = true;
                const auto debited =
                    DebitLookupSteps(record.scope->textures().size());
                if (!debited)
                    return debited;
                if (record.scope->FindTexture(*node.texture_member) == nullptr)
                {
                    AddBlocker(RetailPresentationRequirement::
                            MissingTextureBinding);
                }
            }
            if (node.triangles.empty())
                continue;

            const auto candidate = ReserveCandidate();
            if (!candidate)
                return std::unexpected(candidate.error());

            if (widget_transform_status != TransformStatus::Valid ||
                record.transform_status != TransformStatus::Valid)
            {
                if (widget_transform_status == TransformStatus::NonFinite ||
                    record.transform_status == TransformStatus::NonFinite)
                {
                    AddBlocker(
                        RetailPresentationRequirement::NonFiniteMath);
                }
                if (widget_transform_status == TransformStatus::Failed ||
                    record.transform_status == TransformStatus::Failed)
                {
                    AddBlocker(
                        RetailPresentationRequirement::TransformFailure);
                }
                RecordCandidate(RetailLayerCandidateKind::VisualGeometry,
                    RetailProjectedGeometryCoverage::Indeterminate);
                continue;
            }

            const auto transform = ComposeAffineTransforms(
                widget_transform, record.world_transform);
            if (!transform)
            {
                AddBlocker(transform.error() ==
                        CompositorMathError::NonFiniteInput
                    ? RetailPresentationRequirement::NonFiniteMath
                    : RetailPresentationRequirement::TransformFailure);
                RecordCandidate(RetailLayerCandidateKind::VisualGeometry,
                    RetailProjectedGeometryCoverage::Indeterminate);
                continue;
            }
            const auto inspected =
                InspectStaticGeometry(node, *transform, *candidate,
                    unresolved_visibility);
            if (!inspected)
                return std::unexpected(inspected.error());
            RecordCandidate(
                RetailLayerCandidateKind::VisualGeometry, *inspected);
        }
        return {};
    }

    [[nodiscard]] std::expected<void, RetailPresentationInspectionError>
    InspectWidget(const asset::FrontendWidgetIR& widget,
        const std::uint32_t depth,
        const AffineTransform12& parent_transform,
        const TransformStatus parent_transform_status,
        const bool ancestor_hidden, const bool parentless) noexcept
    {
        if (depth > limits.maximum_widget_depth)
        {
            return std::unexpected(
                RetailPresentationInspectionError::WidgetDepthLimitExceeded);
        }
        if (widget_count == limits.maximum_widget_nodes)
        {
            return std::unexpected(
                RetailPresentationInspectionError::WidgetNodeLimitExceeded);
        }
        ++widget_count;

        auto debit_identity = DebitIdentityBytes(widget.identifier.size());
        if (!debit_identity)
            return debit_identity;
        if (widget.binding)
        {
            debit_identity =
                DebitIdentityBytes(widget.binding->scope_reference.size());
            if (!debit_identity)
                return debit_identity;
            debit_identity = DebitIdentityBytes(
                widget.binding->resource_reference.size());
            if (!debit_identity)
                return debit_identity;
        }

        if (!IsFinite(widget.rectangle) ||
            (widget.text_color && !IsFinite(*widget.text_color)))
        {
            AddBlocker(RetailPresentationRequirement::NonFiniteMath);
        }

        AffineTransform12 widget_transform = parent_transform;
        TransformStatus widget_transform_status = parent_transform_status;
        if (widget.binding &&
            parent_transform_status == TransformStatus::Valid)
        {
            const auto composed = ComposeAffineTransforms(parent_transform,
                AffineTransform12{
                    .column_vectors = widget.binding->transform_values,
                });
            if (composed)
                widget_transform = *composed;
            else
                widget_transform_status =
                    MapTransformStatus(composed.error());
        }

        const bool visibility_ambiguous =
            widget.visible && ancestor_hidden;
        if (visibility_ambiguous)
        {
            AddBlocker(
                RetailPresentationRequirement::ParentVisibility);
        }

        const std::uint32_t own_candidate_start = candidate_count;
        if (HasTextSemantics(widget))
        {
            AddBlocker(RetailPresentationRequirement::TextEncoding);
            AddBlocker(RetailPresentationRequirement::TextLayout);
            AddBlocker(RetailPresentationRequirement::TextInterleave);
            if (widget.visible)
            {
                const auto candidate = ReserveCandidate();
                if (!candidate)
                    return std::unexpected(candidate.error());
                RecordCandidate(RetailLayerCandidateKind::Text,
                    RetailProjectedGeometryCoverage::Indeterminate);
            }
        }
        if (widget.binding && !widget.binding->actions.empty())
        {
            AddBlocker(
                RetailPresentationRequirement::ActionLifecycle);
        }
        if (widget.kind == asset::FrontendWidgetKind::CharacterDisplay)
        {
            AddBlocker(
                RetailPresentationRequirement::UnsupportedWidgetKind);
            if (widget.visible)
            {
                const auto candidate = ReserveCandidate();
                if (!candidate)
                    return std::unexpected(candidate.error());
                RecordCandidate(RetailLayerCandidateKind::UnresolvedWidget,
                    RetailProjectedGeometryCoverage::Indeterminate);
            }
        }

        if (widget.visible && widget.binding)
        {
            auto debit_lookup =
                DebitLookupSteps(lookup_entry_count);
            if (!debit_lookup)
                return debit_lookup;
            const auto* const scope =
                bundle.FindVisualScope(widget.binding->scope_reference);

            debit_lookup = DebitLookupSteps(lookup_entry_count);
            if (!debit_lookup)
                return debit_lookup;
            debit_lookup = DebitLookupSteps(visual_record_count);
            if (!debit_lookup)
                return debit_lookup;
            const auto* const visual =
                bundle.ResolveVisualBinding(widget, parentless);
            if (scope == nullptr || visual == nullptr)
            {
                AddBlocker(
                    RetailPresentationRequirement::MissingVisualBinding);
                const auto candidate = ReserveCandidate();
                if (!candidate)
                    return std::unexpected(candidate.error());
                RecordCandidate(RetailLayerCandidateKind::UnresolvedBinding,
                    RetailProjectedGeometryCoverage::Indeterminate);
            }
            else
            {
                debit_lookup = DebitLookupSteps(visual_record_count);
                if (!debit_lookup)
                    return debit_lookup;
                const auto* const record =
                    FindVisualRecord(*scope, *visual);
                if (record == nullptr)
                {
                    AddBlocker(
                        RetailPresentationRequirement::MissingVisualBinding);
                    const auto candidate = ReserveCandidate();
                    if (!candidate)
                        return std::unexpected(candidate.error());
                    RecordCandidate(
                        RetailLayerCandidateKind::UnresolvedBinding,
                        RetailProjectedGeometryCoverage::Indeterminate);
                }
                else
                {
                    const auto inspected = InspectVisualSubtree(*record,
                        widget_transform, widget_transform_status,
                        visibility_ambiguous);
                    if (!inspected)
                        return inspected;
                }
            }
        }

        if (candidate_count != own_candidate_start)
            ++candidate_widget_lanes;

        const bool child_ancestor_hidden =
            ancestor_hidden || !widget.visible;
        for (const auto& child : widget.children)
        {
            const auto inspected = InspectWidget(child, depth + 1U,
                widget_transform, widget_transform_status,
                child_ancestor_hidden, false);
            if (!inspected)
                return inspected;
        }
        return {};
    }

    void Finalize() noexcept
    {
        if (candidate_count > 1U)
        {
            AddBlocker(
                RetailPresentationRequirement::CandidateOrderingUnresolved);
        }
        if (candidate_widget_lanes > 1U ||
            (has_visual_candidate && has_nonvisual_candidate))
        {
            AddBlocker(
                RetailPresentationRequirement::VisualWidgetLaneMerge);
        }
        if (any_indeterminate_candidate)
        {
            report.projected_geometry_union_evaluated = false;
            report.projected_geometry_union_covers_canonical_raster = false;
            return;
        }

        report.projected_geometry_union_evaluated = true;
        report.projected_geometry_union_covers_canonical_raster =
            std::all_of(
                union_coverage.get(),
                union_coverage.get() +
                    static_cast<std::ptrdiff_t>(kCanonicalPixelCount),
                [](const std::uint8_t covered) { return covered == 1U; });
        if (!report.projected_geometry_union_covers_canonical_raster)
        {
            AddBlocker(
                RetailPresentationRequirement::IncompleteCandidateUnion);
        }
    }
};

RetailPresentationInspectionResult InspectRetailPresentationRequirements(
    const content::FrontEndScreenBundle& bundle,
    const RetailPresentationInspectionLimits limits) noexcept
{
    if (!bundle.presentation_capability().valid())
    {
        return std::unexpected(
            RetailPresentationInspectionError::InvalidRetailCapability);
    }
    if (!IsValidLimits(limits))
    {
        return std::unexpected(
            RetailPresentationInspectionError::InvalidLimits);
    }
    if (limits.maximum_output_bytes <
        sizeof(RetailPresentationRequirements))
    {
        return std::unexpected(
            RetailPresentationInspectionError::OutputLimitExceeded);
    }

    RetailPresentationInspectionContext context{
        .bundle = bundle,
        .limits = limits,
    };
    VisualPreflight preflight;
    const auto preflighted = context.PreflightVisuals(preflight);
    if (!preflighted)
        return std::unexpected(preflighted.error());
    context.visual_record_count = preflight.node_count;
    context.lookup_entry_count = preflight.lookup_entry_count;

    const std::uint64_t visual_record_bytes =
        static_cast<std::uint64_t>(context.visual_record_count) *
        kRetailPresentationInspectionVisualRecordScratchBytes;
    if (visual_record_bytes >
            std::numeric_limits<std::uint64_t>::max() -
                kCoverageScratchBytes ||
        visual_record_bytes + kCoverageScratchBytes >
            limits.maximum_scratch_bytes)
    {
        return std::unexpected(
            RetailPresentationInspectionError::ScratchLimitExceeded);
    }

    try
    {
        context.visual_records =
            std::make_unique_for_overwrite<VisualRecord[]>(
                context.visual_record_count);
        context.union_coverage =
            std::make_unique_for_overwrite<std::uint8_t[]>(
                static_cast<std::size_t>(kCanonicalPixelCount));
        context.candidate_stamps =
            std::make_unique_for_overwrite<std::uint32_t[]>(
                static_cast<std::size_t>(kCanonicalPixelCount));
        context.candidate_covered_indices =
            std::make_unique_for_overwrite<std::uint32_t[]>(
                static_cast<std::size_t>(kCanonicalPixelCount));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(
            RetailPresentationInspectionError::AllocationFailure);
    }

    std::fill_n(context.union_coverage.get(),
        static_cast<std::size_t>(kCanonicalPixelCount), std::uint8_t{0U});
    std::fill_n(context.candidate_stamps.get(),
        static_cast<std::size_t>(kCanonicalPixelCount),
        std::numeric_limits<std::uint32_t>::max());
    context.BuildAllVisualRecords();

    const auto inspected = context.InspectWidget(
        bundle.widget_document().root, 1U, kIdentityAffineTransform12,
        TransformStatus::Valid, false, true);
    if (!inspected)
        return std::unexpected(inspected.error());

    context.Finalize();
    return std::move(context.report);
}

static_assert(kCoverageScratchBytes == 2'580'480ULL);
} // namespace omega::frontend::presentation
