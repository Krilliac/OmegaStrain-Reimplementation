#include "omega/frontend_presentation/retail_root_visual_layer.h"

#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend_presentation/screen_space_triangle_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <span>
#include <utility>
#include <vector>

namespace omega::frontend::presentation
{
namespace
{
constexpr ScreenSpaceClipRect kCanonicalClip{
    .left = 0,
    .top = 0,
    .right = static_cast<std::int32_t>(kCanonicalRasterWidth),
    .bottom = static_cast<std::int32_t>(kCanonicalRasterHeight),
};

struct CoverageContext final
{
    std::vector<std::uint8_t>* pixels = nullptr;
    bool overlap = false;
};

[[nodiscard]] bool HasTextSemantics(
    const asset::FrontendWidgetIR& widget) noexcept
{
    return widget.text_reference.has_value() ||
           widget.font_reference.has_value() ||
           widget.text_color.has_value() ||
           widget.text_alignment.has_value();
}

[[nodiscard]] bool IsValidLimits(
    const RetailRootVisualLayerLimits& limits) noexcept
{
    return limits.maximum_positions != 0U &&
           limits.maximum_positions <=
               kRetailRootVisualLayerMaximumPositions &&
           limits.maximum_uvs != 0U &&
           limits.maximum_uvs <= kRetailRootVisualLayerMaximumUvs &&
           limits.maximum_colors != 0U &&
           limits.maximum_colors <= kRetailRootVisualLayerMaximumColors &&
           limits.maximum_triangles != 0U &&
           limits.maximum_triangles <=
               kRetailRootVisualLayerMaximumTriangles &&
           limits.maximum_covered_samples != 0U &&
           limits.maximum_covered_samples <=
               kRetailRootVisualLayerMaximumCoveredSamples &&
           limits.maximum_intermediate_bytes != 0U &&
           limits.maximum_intermediate_bytes <=
               kRetailRootVisualLayerMaximumIntermediateBytes &&
           limits.maximum_output_bytes != 0U &&
           limits.maximum_output_bytes <=
               kRetailFrontEndRasterOutputBytes &&
           limits.maximum_raster_scratch_bytes != 0U &&
           limits.maximum_raster_scratch_bytes <=
               kRetailFrontEndRasterScratchBytes;
}

[[nodiscard]] RetailRootVisualLayerError MapMathError(
    const CompositorMathError error,
    const RetailRootVisualLayerError finite_result_error) noexcept
{
    switch (error)
    {
    case CompositorMathError::NonFiniteInput:
        return RetailRootVisualLayerError::NonFiniteInput;
    case CompositorMathError::NonFiniteResult:
        return finite_result_error;
    case CompositorMathError::ColorOutOfRange:
        return RetailRootVisualLayerError::InvalidGeometry;
    }
    return RetailRootVisualLayerError::ArithmeticFailure;
}

[[nodiscard]] RetailRootVisualLayerError MapKernelError(
    const ScreenSpaceTriangleError error) noexcept
{
    switch (error)
    {
    case ScreenSpaceTriangleError::LimitExceeded:
        return RetailRootVisualLayerError::LimitExceeded;
    case ScreenSpaceTriangleError::NonFiniteInput:
        return RetailRootVisualLayerError::NonFiniteInput;
    case ScreenSpaceTriangleError::DegenerateTriangle:
        return RetailRootVisualLayerError::DegenerateGeometry;
    case ScreenSpaceTriangleError::ArithmeticFailure:
    case ScreenSpaceTriangleError::PerspectiveDivideFailure:
        return RetailRootVisualLayerError::ArithmeticFailure;
    case ScreenSpaceTriangleError::InvalidLimits:
    case ScreenSpaceTriangleError::InvalidClip:
    case ScreenSpaceTriangleError::InconsistentAffineChannelCount:
    case ScreenSpaceTriangleError::MissingVisitor:
    case ScreenSpaceTriangleError::VisitorStopped:
    case ScreenSpaceTriangleError::InvalidVisitorResult:
        return RetailRootVisualLayerError::RasterizationFailure;
    }
    return RetailRootVisualLayerError::RasterizationFailure;
}

[[nodiscard]] RetailRootVisualLayerError MapRasterError(
    const RetailFrontEndRasterError error) noexcept
{
    switch (error)
    {
    case RetailFrontEndRasterError::InvalidLimits:
        return RetailRootVisualLayerError::InvalidLimits;
    case RetailFrontEndRasterError::LimitExceeded:
        return RetailRootVisualLayerError::LimitExceeded;
    case RetailFrontEndRasterError::NonFiniteInput:
        return RetailRootVisualLayerError::NonFiniteInput;
    case RetailFrontEndRasterError::ColorOutOfRange:
        return RetailRootVisualLayerError::InvalidGeometry;
    case RetailFrontEndRasterError::DegenerateTriangle:
        return RetailRootVisualLayerError::DegenerateGeometry;
    case RetailFrontEndRasterError::TextureSamplingFailure:
        return RetailRootVisualLayerError::TextureSamplingFailure;
    case RetailFrontEndRasterError::ArithmeticFailure:
        return RetailRootVisualLayerError::ArithmeticFailure;
    case RetailFrontEndRasterError::RasterizationFailure:
        return RetailRootVisualLayerError::RasterizationFailure;
    case RetailFrontEndRasterError::AllocationFailure:
        return RetailRootVisualLayerError::AllocationFailure;
    }
    return RetailRootVisualLayerError::RasterizationFailure;
}

[[nodiscard]] ScreenSpaceTriangleVisitControl MarkCoveredPixel(
    void* const opaque_context,
    const ScreenSpaceTriangleSample& sample) noexcept
{
    auto& context = *static_cast<CoverageContext*>(opaque_context);
    const std::size_t index =
        static_cast<std::size_t>(sample.y) *
            static_cast<std::size_t>(kCanonicalRasterWidth) +
        static_cast<std::size_t>(sample.x);
    if ((*context.pixels)[index] != 0U)
    {
        context.overlap = true;
        return ScreenSpaceTriangleVisitControl::Stop;
    }
    (*context.pixels)[index] = 1U;
    return ScreenSpaceTriangleVisitControl::Continue;
}

[[nodiscard]] std::expected<void, RetailRootVisualLayerError>
PreflightCoverage(
    const std::span<const RetailFrontEndRasterTriangle> triangles,
    const RetailRootVisualLayerLimits& limits) noexcept
{
    std::vector<std::uint8_t> coverage;
    try
    {
        coverage.assign(
            static_cast<std::size_t>(kRetailRootVisualLayerCoverageBytes), 0U);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailRootVisualLayerError::AllocationFailure);
    }

    std::uint64_t total_covered = 0U;
    for (const auto& triangle : triangles)
    {
        std::array<ScreenSpaceTriangleVertex, 3U> vertices{};
        for (std::size_t index = 0U; index < vertices.size(); ++index)
        {
            vertices[index] = ScreenSpaceTriangleVertex{
                .x = triangle.vertices[index].x,
                .y = triangle.vertices[index].y,
                .s = 0.0F,
                .t = 0.0F,
                .q = 1.0F,
            };
        }

        const std::uint64_t remaining =
            limits.maximum_covered_samples - total_covered;
        const std::uint64_t triangle_budget = std::min(
            std::max(remaining, std::uint64_t{1U}),
            kScreenSpaceTriangleMaximumCoveredPixels);
        CoverageContext context{.pixels = &coverage};
        const auto covered = RasterizeScreenSpaceTriangle(
            vertices, kCanonicalClip, MarkCoveredPixel, &context,
            ScreenSpaceTriangleLimits{
                .maximum_affine_channels = 0U,
                .maximum_clip_width = kCanonicalRasterWidth,
                .maximum_clip_height = kCanonicalRasterHeight,
                .maximum_covered_pixels = triangle_budget,
            });
        if (!covered)
        {
            if (context.overlap &&
                covered.error() == ScreenSpaceTriangleError::VisitorStopped)
            {
                return std::unexpected(
                    RetailRootVisualLayerError::OverlappingCoverage);
            }
            return std::unexpected(MapKernelError(covered.error()));
        }
        if (covered->covered_pixel_count > remaining)
            return std::unexpected(RetailRootVisualLayerError::LimitExceeded);
        total_covered += covered->covered_pixel_count;
    }

    if (total_covered != kRetailRootVisualLayerCoverageBytes ||
        std::ranges::any_of(
            coverage, [](const std::uint8_t value) { return value != 1U; }))
    {
        return std::unexpected(
            RetailRootVisualLayerError::IncompleteCoverage);
    }
    return {};
}

[[nodiscard]] bool IsFullyOpaque(const OwnedRgba8Frame& frame) noexcept
{
    if (frame.width != kCanonicalRasterWidth ||
        frame.height != kCanonicalRasterHeight ||
        frame.pixels.size() != kRetailFrontEndRasterOutputBytes)
    {
        return false;
    }
    for (std::size_t offset = 3U; offset < frame.pixels.size(); offset += 4U)
    {
        if (frame.pixels[offset] != 255U)
            return false;
    }
    return true;
}
} // namespace

RetailRootVisualLayerResult ComposeRetailRootVisualLayer(
    const content::FrontEndScreenBundle& bundle,
    const RetailRootVisualLayerLimits limits) noexcept
{
    if (!bundle.presentation_capability().valid())
    {
        return std::unexpected(
            RetailRootVisualLayerError::InvalidRetailCapability);
    }
    if (!IsValidLimits(limits))
        return std::unexpected(RetailRootVisualLayerError::InvalidLimits);
    if (limits.maximum_output_bytes < kRetailFrontEndRasterOutputBytes ||
        limits.maximum_raster_scratch_bytes <
            kRetailFrontEndRasterScratchBytes ||
        limits.maximum_intermediate_bytes <
            kRetailRootVisualLayerCoverageBytes)
    {
        return std::unexpected(RetailRootVisualLayerError::LimitExceeded);
    }

    const auto& widget = bundle.widget_document().root;
    if (widget.kind != asset::FrontendWidgetKind::Container ||
        !widget.visible)
    {
        return std::unexpected(
            RetailRootVisualLayerError::UnsupportedRootWidget);
    }
    if (!widget.binding)
    {
        return std::unexpected(
            RetailRootVisualLayerError::MissingRootVisualBinding);
    }
    if (HasTextSemantics(widget))
        return std::unexpected(RetailRootVisualLayerError::UnsupportedRootText);
    if (!widget.binding->actions.empty())
    {
        return std::unexpected(
            RetailRootVisualLayerError::UnsupportedRootAction);
    }

    const auto* const selected_scope =
        bundle.FindVisualScope(widget.binding->scope_reference);
    if (selected_scope == nullptr)
    {
        return std::unexpected(
            RetailRootVisualLayerError::MissingRootVisualBinding);
    }
    const auto* const visual = bundle.ResolveVisualBinding(widget, true);
    if (visual == nullptr)
    {
        return std::unexpected(
            RetailRootVisualLayerError::MissingRootVisualBinding);
    }
    if (visual != &selected_scope->document().root)
    {
        return std::unexpected(
            RetailRootVisualLayerError::UnsupportedRootVisualHierarchy);
    }
    if (!visual->animation_tracks.empty())
    {
        return std::unexpected(
            RetailRootVisualLayerError::UnsupportedRootAnimation);
    }

    if (visual->positions.size() > limits.maximum_positions ||
        visual->uvs.size() > limits.maximum_uvs ||
        visual->colors.size() > limits.maximum_colors ||
        visual->triangles.size() > limits.maximum_triangles)
    {
        return std::unexpected(RetailRootVisualLayerError::LimitExceeded);
    }
    const std::uint64_t triangle_bytes =
        static_cast<std::uint64_t>(visual->triangles.size()) *
        sizeof(RetailFrontEndRasterTriangle);
    if (triangle_bytes >
            std::numeric_limits<std::uint64_t>::max() -
                kRetailRootVisualLayerCoverageBytes ||
        triangle_bytes + kRetailRootVisualLayerCoverageBytes >
            limits.maximum_intermediate_bytes)
    {
        return std::unexpected(RetailRootVisualLayerError::LimitExceeded);
    }

    const AffineTransform12 binding_transform{
        .column_vectors = widget.binding->transform_values,
    };
    const AffineTransform12 visual_transform{
        .column_vectors = visual->transform_values,
    };
    const auto composed =
        ComposeAffineTransforms(binding_transform, visual_transform);
    if (!composed)
    {
        return std::unexpected(MapMathError(
            composed.error(), RetailRootVisualLayerError::TransformFailure));
    }

    const content::FrontEndTextureBinding* texture = nullptr;
    if (visual->texture_member)
    {
        const auto resolved =
            bundle.ResolveVisualTextureBinding(widget, true);
        if (!resolved)
        {
            return std::unexpected(
                RetailRootVisualLayerError::MissingTextureBinding);
        }
        if (&resolved->scope() != selected_scope)
        {
            return std::unexpected(
                RetailRootVisualLayerError::MissingTextureBinding);
        }
        texture = &resolved->texture();
    }

    std::vector<RetailFrontEndRasterTriangle> triangles;
    try
    {
        triangles.reserve(visual->triangles.size());
        for (const auto& source_triangle : visual->triangles)
        {
            RetailFrontEndRasterTriangle triangle{.texture = texture};
            for (std::size_t vertex_index = 0U;
                 vertex_index < triangle.vertices.size(); ++vertex_index)
            {
                const std::uint16_t position_index =
                    source_triangle.position_indices[vertex_index];
                const std::uint16_t uv_index =
                    source_triangle.uv_indices[vertex_index];
                const std::uint16_t color_index =
                    source_triangle.color_indices[vertex_index];
                if (position_index >= visual->positions.size() ||
                    uv_index >= visual->uvs.size() ||
                    color_index >= visual->colors.size())
                {
                    return std::unexpected(
                        RetailRootVisualLayerError::InvalidGeometry);
                }

                const auto transformed =
                    TransformPoint(*composed, visual->positions[position_index]);
                if (!transformed)
                {
                    return std::unexpected(MapMathError(transformed.error(),
                        RetailRootVisualLayerError::TransformFailure));
                }
                const auto projected =
                    ProjectInterfaceElementPoint(*transformed);
                if (!projected)
                {
                    return std::unexpected(MapMathError(projected.error(),
                        RetailRootVisualLayerError::ProjectionFailure));
                }
                if (projected->depth_rank < 0.0F ||
                    projected->depth_rank > 1.0F)
                {
                    return std::unexpected(
                        RetailRootVisualLayerError::ProjectionFailure);
                }
                const auto modulation = ModulateVertexColor(
                    visual->colors[color_index],
                    RgbaF{1.0F, 1.0F, 1.0F, 1.0F});
                if (!modulation)
                {
                    return std::unexpected(MapMathError(modulation.error(),
                        RetailRootVisualLayerError::ArithmeticFailure));
                }

                triangle.vertices[vertex_index] = RetailFrontEndRasterVertex{
                    .x = projected->raster_position.x,
                    .y = projected->raster_position.y,
                    .depth_rank = projected->depth_rank,
                    .normalized_st = visual->uvs[uv_index],
                    .modulation = *modulation,
                };
            }
            triangles.push_back(triangle);
        }
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailRootVisualLayerError::AllocationFailure);
    }

    const auto coverage = PreflightCoverage(triangles, limits);
    if (!coverage)
        return std::unexpected(coverage.error());

    const auto rasterized = RasterizeRetailFrontEndTriangles(
        triangles, RgbaF{},
        RetailFrontEndRasterLimits{
            .maximum_triangles = limits.maximum_triangles,
            .maximum_covered_samples = limits.maximum_covered_samples,
            .maximum_output_bytes = limits.maximum_output_bytes,
            .maximum_scratch_bytes = limits.maximum_raster_scratch_bytes,
        });
    if (!rasterized)
        return std::unexpected(MapRasterError(rasterized.error()));
    if (!IsFullyOpaque(*rasterized))
    {
        return std::unexpected(
            RetailRootVisualLayerError::TransparentOutput);
    }

    return RetailRootVisualLayer{
        .frame = std::move(*rasterized),
        .coverage =
            RetailRootVisualLayerCoverage::RootVisualOwnGeometryOnly,
    };
}
} // namespace omega::frontend::presentation
