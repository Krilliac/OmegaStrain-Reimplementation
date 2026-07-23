#include "omega/frontend_presentation/retail_front_end_frame.h"

#include "omega/asset/frontend_ir.h"
#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend/compositor_math.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <vector>

namespace omega::frontend::presentation
{
namespace
{
// Bounds recursion over the retail visual tree so pathological/hostile nesting
// cannot exhaust the native stack. Real retail front-end trees are shallow.
inline constexpr std::uint32_t kMaximumVisualTreeDepth = 64U;

[[nodiscard]] RetailFrontEndFrameError MapRasterError(
    const RetailFrontEndRasterError error) noexcept
{
    switch (error)
    {
    case RetailFrontEndRasterError::InvalidLimits:
    case RetailFrontEndRasterError::LimitExceeded:
        return RetailFrontEndFrameError::LimitExceeded;
    case RetailFrontEndRasterError::NonFiniteInput:
    case RetailFrontEndRasterError::ColorOutOfRange:
    case RetailFrontEndRasterError::DegenerateTriangle:
        return RetailFrontEndFrameError::InvalidGeometry;
    case RetailFrontEndRasterError::TextureSamplingFailure:
    case RetailFrontEndRasterError::RasterizationFailure:
        return RetailFrontEndFrameError::RasterizationFailure;
    case RetailFrontEndRasterError::ArithmeticFailure:
        return RetailFrontEndFrameError::ArithmeticFailure;
    case RetailFrontEndRasterError::AllocationFailure:
        return RetailFrontEndFrameError::AllocationFailure;
    }
    return RetailFrontEndFrameError::RasterizationFailure;
}

// Replicates the screen-space triangle kernel's degeneracy rejection exactly
// (screen_space_triangle_kernel.cpp: same double-precision Edge formula, reject
// when the signed twice area is non-finite or exactly zero). Real retail UI
// meshes carry collinear / zero-area helper triangles that the kernel refuses,
// so the compositor drops them here (fail-soft) instead of failing the screen.
// Negative (opposite-winding) area is valid and kept, matching the kernel.
[[nodiscard]] bool IsRasterizableTriangle(
    const RetailFrontEndRasterTriangle& triangle) noexcept
{
    for (const auto& vertex : triangle.vertices)
    {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y))
            return false;
    }
    const double ax = static_cast<double>(triangle.vertices[0U].x);
    const double ay = static_cast<double>(triangle.vertices[0U].y);
    const double bx = static_cast<double>(triangle.vertices[1U].x);
    const double by = static_cast<double>(triangle.vertices[1U].y);
    const double cx = static_cast<double>(triangle.vertices[2U].x);
    const double cy = static_cast<double>(triangle.vertices[2U].y);
    const double signed_twice_area =
        (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    return std::isfinite(signed_twice_area) && signed_twice_area != 0.0;
}

// Depth-first preorder over the retail IE visual tree: each node emits its own
// triangles under its accumulated world transform (parent_world * local), THEN
// recurses into its children, matching the retail render order recovered from
// the disassembly. Animation tracks are intentionally held at frame-0 (the
// node's base positions/uvs/colors). A node whose declared texture member is
// not resolvable falls back to the untextured vertex-color path.
//
// The walk is FAIL-SOFT and never aborts the screen: an out-of-range vertex
// index, a non-finite transform/projection/colour, or a degenerate (zero-area)
// triangle drops only that one triangle; a non-finite node world transform drops
// only that node's subtree. This matches the retail engine, which submits a fixed
// stream of primitives and skips ones the GS would reject rather than failing the
// frame; a wholly empty result is still caught by the caller.
void AppendVisualNodeTriangles(const content::FrontEndVisualScope& scope,
    const asset::FrontendVisualNodeIR& node,
    const AffineTransform12& parent_world, const std::uint32_t depth,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (depth >= kMaximumVisualTreeDepth)
        return;

    const AffineTransform12 local_transform{
        .column_vectors = node.transform_values,
    };
    const auto world = ComposeAffineTransforms(parent_world, local_transform);
    if (!world)
        return;

    // Fail-soft texture resolution within the node's owning scope. A missing or
    // absent texture member draws with vertex colors only.
    const content::FrontEndTextureBinding* texture = nullptr;
    if (node.texture_member)
        texture = scope.FindTexture(*node.texture_member);

    for (const auto& source_triangle : node.triangles)
    {
        RetailFrontEndRasterTriangle triangle{.texture = texture};
        bool vertices_valid = true;
        for (std::size_t vertex_index = 0U;
             vertex_index < triangle.vertices.size(); ++vertex_index)
        {
            const std::uint16_t position_index =
                source_triangle.position_indices[vertex_index];
            const std::uint16_t uv_index =
                source_triangle.uv_indices[vertex_index];
            const std::uint16_t color_index =
                source_triangle.color_indices[vertex_index];
            if (position_index >= node.positions.size() ||
                uv_index >= node.uvs.size() ||
                color_index >= node.colors.size())
            {
                vertices_valid = false;
                break;
            }

            const auto transformed =
                TransformPoint(*world, node.positions[position_index]);
            if (!transformed)
            {
                vertices_valid = false;
                break;
            }
            const auto projected = ProjectInterfaceElementPoint(*transformed);
            if (!projected)
            {
                vertices_valid = false;
                break;
            }
            // Depth is NOT taken from the projected Z. The retail front end draws
            // in depth-first preorder with a monotonic submission sequence and no
            // post-hoc z-sort (recovered from the disassembly), so layering is
            // painter's-by-submission-order. The projected depth_rank is only a
            // normalized rank the projection does not clip, and real IE nodes
            // legitimately fall outside [0,1]; using it as the raster depth key
            // would both reject valid screens and reorder nodes against the retail
            // submission order. depth_rank is assigned per-triangle after the full
            // preorder walk (see ComposeRetailFrontEndFrame) as its submission
            // ordinal; here it is a placeholder overwritten there.
            const auto modulation = ModulateVertexColor(
                node.colors[color_index], RgbaF{1.0F, 1.0F, 1.0F, 1.0F});
            if (!modulation)
            {
                vertices_valid = false;
                break;
            }

            triangle.vertices[vertex_index] = RetailFrontEndRasterVertex{
                .x = projected->raster_position.x,
                .y = projected->raster_position.y,
                .depth_rank = 0.0F,
                .normalized_st = node.uvs[uv_index],
                .modulation = *modulation,
            };
        }
        if (!vertices_valid)
            continue;
        if (!IsRasterizableTriangle(triangle))
            continue;
        out.push_back(triangle);
    }

    for (const auto& child : node.children)
        AppendVisualNodeTriangles(scope, child, *world, depth + 1U, out);
}
} // namespace

RetailFrontEndFrameResult ComposeRetailFrontEndFrame(
    const content::FrontEndScreenBundle& bundle,
    const RetailFrontEndRasterLimits limits) noexcept
{
    if (!bundle.presentation_capability().valid())
        return std::unexpected(RetailFrontEndFrameError::InvalidRetailCapability);

    const auto& root_widget = bundle.widget_document().root;
    if (root_widget.kind != asset::FrontendWidgetKind::Container ||
        !root_widget.visible || !root_widget.binding)
    {
        return std::unexpected(RetailFrontEndFrameError::UnsupportedRootWidget);
    }

    // The root GUI widget selects the screen's visual scope and, parentless,
    // resolves the root IE node. The whole IE subtree below it is the screen.
    const auto* const scope =
        bundle.FindVisualScope(root_widget.binding->scope_reference);
    const auto* const root_visual = bundle.ResolveVisualBinding(root_widget, true);
    if (scope == nullptr || root_visual == nullptr)
        return std::unexpected(RetailFrontEndFrameError::MissingRootVisualBinding);

    // The retail IE positions carry screen-horizontal in X, screen-VERTICAL in Y,
    // and depth in Z. ProjectInterfaceElementPoint expects an already-bridged point
    // (screen-vertical in Z, depth in Y): project(x,y,z) = (320+x, 224-z, ...).
    // kInterfaceElementAxisBridge performs exactly that IE->raster axis swap
    // ((x,y,z)->(x,z,y+1)), so project o bridge = (320+x, 223-y, depth-from-z) --
    // the natural convention. It must be applied OUTERMOST (after every node's
    // world transform), so it is the top-of-chain parent: composing it with the
    // root binding transform keeps it outermost through the whole preorder walk
    // (ComposeAffineTransforms(parent, local) = parent*local, parent applied last).
    // Without it, flat menu geometry (constant Z) collapses to horizontal lines and
    // the kernel rejects ~every triangle as degenerate.
    const AffineTransform12 binding_transform{
        .column_vectors = root_widget.binding->transform_values,
    };
    const auto bridged_initial_world =
        ComposeAffineTransforms(kInterfaceElementAxisBridge, binding_transform);
    if (!bridged_initial_world)
        return std::unexpected(RetailFrontEndFrameError::TransformFailure);
    const AffineTransform12 initial_world = *bridged_initial_world;
    std::vector<RetailFrontEndRasterTriangle> triangles;
    try
    {
        AppendVisualNodeTriangles(
            *scope, *root_visual, initial_world, 0U, triangles);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailFrontEndFrameError::AllocationFailure);
    }
    if (triangles.empty())
        return std::unexpected(RetailFrontEndFrameError::MissingRootVisualBinding);

    // Assign each triangle its submission ordinal as depth_rank, normalized into
    // (0, 1). Triangles were appended in the retail depth-first preorder submission
    // order, so a later triangle gets a strictly larger rank and the CPU raster's
    // GEQUAL (larger-is-nearer) keep makes it cover earlier ones -- painter's order
    // by submission, matching the retail engine's no-z-sort front-end.
    const float ordinal_scale = static_cast<float>(triangles.size() + 1U);
    for (std::size_t triangle_index = 0U; triangle_index < triangles.size();
         ++triangle_index)
    {
        const float rank =
            static_cast<float>(triangle_index + 1U) / ordinal_scale;
        for (auto& vertex : triangles[triangle_index].vertices)
            vertex.depth_rank = rank;
    }

    auto rasterized = RasterizeRetailFrontEndTriangles(
        triangles, RgbaF{0.0F, 0.0F, 0.0F, 1.0F}, limits);
    if (!rasterized)
        return std::unexpected(MapRasterError(rasterized.error()));
    return std::move(*rasterized);
}
} // namespace omega::frontend::presentation
