#include "omega/frontend_presentation/retail_front_end_frame.h"

#include "omega/asset/frontend_ir.h"
#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend/compositor_math.h"
#include "omega/frontend_presentation/retail_frontend_timeline.h"
#include "omega/frontend_text/text_layout.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace omega::frontend::presentation
{
namespace
{
// Bounds recursion over the decoded visual tree so pathological/hostile nesting
// cannot exhaust the native stack. The value is a project-owned safety policy.
inline constexpr std::uint32_t kMaximumVisualTreeDepth = 64U;

// A decrement-only append budget avoids size+count overflow and checks the
// caller's triangle ceiling before every triangle-vector mutation. Once
// exhausted, all producers stop and the frame returns LimitExceeded without
// rasterizing the bounded prefix.
struct TriangleAppendBudget final
{
    std::uint32_t remaining = 0U;
    bool exceeded = false;

    [[nodiscard]] bool TryAppend(
        std::vector<RetailFrontEndRasterTriangle>& out,
        RetailFrontEndRasterTriangle&& triangle)
    {
        if (remaining == 0U)
        {
            exceeded = true;
            return false;
        }
        out.push_back(std::move(triangle));
        --remaining;
        return true;
    }
};

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

// Preflight before any scope lookup (whose exact-identifier DFS assumes a
// validated document). A depth overflow is a hard limit error, never a partial
// frame with the tail of the visual tree silently omitted.
[[nodiscard]] bool VisualTreeDepthIsBounded(
    const asset::FrontendVisualNodeIR& node,
    const std::uint32_t depth) noexcept
{
    if (depth >= kMaximumVisualTreeDepth)
        return false;
    for (const auto& child : node.children)
    {
        if (!VisualTreeDepthIsBounded(child, depth + 1U))
            return false;
    }
    return true;
}

struct VisualParentTransform final
{
    // Scope-root through the target's parent, excluding the target's own local
    // transform (AppendVisualNodeTriangles composes that exactly once).
    AffineTransform12 world = kIdentityAffineTransform12;
    bool valid = true;
};

// Finds the transform chain from one visual scope's root to the target's parent.
// This mirrors the presentation inspector's widget_transform *
// record.world_transform rule while leaving the target local for the renderer.
// A non-finite ancestor still returns the target with valid=false so its widget
// claim can suppress the subtree instead of letting IE containment draw it.
[[nodiscard]] std::optional<VisualParentTransform> FindVisualParentTransform(
    const asset::FrontendVisualNodeIR& node,
    const asset::FrontendVisualNodeIR& target,
    const AffineTransform12& parent_world, const bool parent_valid,
    const std::uint32_t depth) noexcept
{
    if (depth >= kMaximumVisualTreeDepth)
        return std::nullopt;
    if (&node == &target)
        return VisualParentTransform{parent_world, parent_valid};

    AffineTransform12 world = parent_world;
    bool valid = parent_valid;
    if (parent_valid)
    {
        const auto composed = ComposeAffineTransforms(parent_world,
            AffineTransform12{.column_vectors = node.transform_values});
        if (composed)
            world = *composed;
        else
            valid = false;
    }
    for (const auto& child : node.children)
    {
        auto match = FindVisualParentTransform(
            child, target, world, valid, depth + 1U);
        if (match)
            return match;
    }
    return std::nullopt;
}

// One GUI widget's ownership of one IE visual node. A widget binding selects a
// resource and carries that resource's project placement, so the resolved node
// uses the owning widget's accumulated parent * local transform.
struct VisualNodeClaim final
{
    // Stable for the duration of composition: the immutable bundle owns the
    // widget tree.
    const asset::FrontendWidgetIR* owner = nullptr;
    // Nearest ancestor widget with a resolved claim. This associates nested
    // claims with one concrete shared-resource instance.
    const asset::FrontendWidgetIR* parent_owner = nullptr;
    // The scope owns both the node and any texture member it samples.
    const content::FrontEndVisualScope* scope = nullptr;
    // Includes the IE axis bridge exactly once, the widget binding chain, and
    // the visual-scope ancestors above this resource.
    AffineTransform12 world{};
    // False when this widget or any ancestor is currently hidden. Hidden claims
    // are still consumed so the IE containment walk cannot draw them instead.
    bool drawn = false;
    bool emitted = false;
};

// Widget preorder is retained within each node's claim list. A node may have
// multiple claims when several widgets instance one shared resource.
using VisualNodeClaimMap =
    std::map<const asset::FrontendVisualNodeIR*, std::vector<VisualNodeClaim>>;

// Depth-first preorder over the IE visual tree: each node emits its own
// triangles under its accumulated world transform (parent_world * local), THEN
// recurses into its children. This DFS, its mixed claimed/unclaimed sibling
// ordering, and the later IE/text interleave are bounded experimental PROJECT
// policies; they do not claim a recovered retail submission order. A node whose
// declared texture member is not resolvable falls back to vertex colors.
//
// `claims` redirects children only. A claimed child is emitted in its authored
// IE sibling position, preserving this project's painter order, but under its
// owning widget's placement. Deferring it to a later widget pass would reorder
// it relative to adjacent unclaimed siblings.
//
// Animation (Phase 3b): a node carrying animation_tracks is evaluated at
// `animation_tick` via the retail timeline primitive; its interpolated positions
// (VERTEX tracks), UV offset (UVOFF tracks), and opacity (OPACITY tracks,
// multiplied into vertex-colour alpha) replace the frame-0 base values. Tick 0
// reproduces frame-0 exactly, and a node with no tracks is untouched at any tick.
// Timeline clone/evaluation failure is fail-soft: the node renders at frame-0.
//
// The walk is FAIL-SOFT and never aborts the screen: an out-of-range vertex
// index, a non-finite transform/projection/colour, or a degenerate (zero-area)
// triangle drops only that one triangle; a non-finite node world transform drops
// only that node's subtree. This is project-owned developer-preview behavior, not
// a recovered claim about retail submission or failure policy; a wholly empty
// result is still caught by the caller.
void AppendVisualNodeTriangles(const content::FrontEndVisualScope& scope,
    const asset::FrontendVisualNodeIR& node,
    const AffineTransform12& parent_world, const std::uint32_t depth,
    const asset::FrontendWidgetIR* owner_context,
    const std::uint32_t animation_tick, VisualNodeClaimMap& claims,
    TriangleAppendBudget& budget,
    RetailFrontEndFrameDiagnostics* const diag,
    std::vector<RetailFrontEndRasterTriangle>& out);

// Emits claims belonging to this concrete parent-widget instance only. Claims
// for another instance of the same shared parent remain pending until that
// parent's turn, preserving parentA,childA,parentB,childB widget order. A hidden
// contextual claim is consumed without drawing. The return value distinguishes
// "claimed but hidden/already emitted" from "not claimed in this instance", for
// which the caller must use inherited IE placement.
[[nodiscard]] bool AppendClaimedVisualNode(
    const asset::FrontendVisualNodeIR& node,
    std::vector<VisualNodeClaim>& node_claims, const std::uint32_t depth,
    const asset::FrontendWidgetIR* owner_context,
    const std::uint32_t animation_tick, VisualNodeClaimMap& claims,
    TriangleAppendBudget& budget,
    RetailFrontEndFrameDiagnostics* const diag,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (budget.exceeded)
        return true;
    bool has_contextual_claim = false;
    for (auto& claim : node_claims)
    {
        if (claim.parent_owner != owner_context)
            continue;
        has_contextual_claim = true;
        if (claim.emitted)
            continue;
        claim.emitted = true;
        if (!claim.drawn || claim.scope == nullptr)
            continue;
        AppendVisualNodeTriangles(*claim.scope, node, claim.world, depth,
            claim.owner, animation_tick, claims, budget, diag, out);
        if (budget.exceeded)
            return true;
    }
    return has_contextual_claim;
}

void AppendVisualNodeTriangles(const content::FrontEndVisualScope& scope,
    const asset::FrontendVisualNodeIR& node,
    const AffineTransform12& parent_world, const std::uint32_t depth,
    const asset::FrontendWidgetIR* const owner_context,
    const std::uint32_t animation_tick, VisualNodeClaimMap& claims,
    TriangleAppendBudget& budget,
    RetailFrontEndFrameDiagnostics* const diag,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (budget.exceeded || depth >= kMaximumVisualTreeDepth)
        return;

    const AffineTransform12 local_transform{
        .column_vectors = node.transform_values,
    };
    const auto world = ComposeAffineTransforms(parent_world, local_transform);
    if (!world)
        return;
    if (diag != nullptr)
        ++diag->visual_nodes_visited;

    // Animation: evaluate this node's tracks at the tick. On success the cloned
    // instance owns the interpolated positions and any resolved opacity / UV
    // offset; on failure (or with no tracks) the base frame-0 values are used.
    // The instance outlives the triangle loop so `effective_positions` stays valid.
    const std::vector<asset::Float3IR>* effective_positions = &node.positions;
    RgbaF opacity_modulation{1.0F, 1.0F, 1.0F, 1.0F};
    std::optional<asset::FrontendUvIR> uv_offset;
    std::optional<RetailFrontendVisualInstanceState> instance;
    if (!node.animation_tracks.empty())
    {
        auto cloned = CloneRetailFrontendVisualInstance(node);
        if (cloned)
        {
            const RetailFrontendTimelineInput timeline_input{
                .live_tick = animation_tick,
                .authored_timeline_tick = static_cast<float>(animation_tick),
            };
            const auto evaluation = EvaluateRetailFrontendTimeline(
                *cloned, node, timeline_input);
            if (evaluation)
            {
                instance = std::move(*cloned);
                if (instance->positions().size() == node.positions.size())
                    effective_positions = &instance->positions();
                if (instance->opacity())
                    opacity_modulation.alpha = *instance->opacity();
                if (instance->uv_offset_u() || instance->uv_offset_v())
                    uv_offset = asset::FrontendUvIR{
                        .u = instance->uv_offset_u().value_or(0.0F),
                        .v = instance->uv_offset_v().value_or(0.0F),
                    };
                if (diag != nullptr)
                    ++diag->animated_nodes;
            }
        }
    }

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
            if (position_index >= effective_positions->size() ||
                uv_index >= node.uvs.size() ||
                color_index >= node.colors.size())
            {
                vertices_valid = false;
                if (diag != nullptr)
                    ++diag->triangles_skipped_out_of_range;
                break;
            }

            const auto transformed = TransformPoint(
                *world, (*effective_positions)[position_index]);
            if (!transformed)
            {
                vertices_valid = false;
                if (diag != nullptr)
                    ++diag->triangles_skipped_non_finite;
                break;
            }
            const auto projected = ProjectInterfaceElementPoint(*transformed);
            if (!projected)
            {
                vertices_valid = false;
                if (diag != nullptr)
                    ++diag->triangles_skipped_non_finite;
                break;
            }
            // Depth is not taken from projected Z. The experimental compositor
            // uses a project-owned preorder painter policy because decoded IE
            // nodes can project outside [0,1]; treating that value as raster
            // depth would reject otherwise valid decoded data. The project
            // submission ordinal overwrites this placeholder after the walk.
            const auto modulation = ModulateVertexColor(
                node.colors[color_index], opacity_modulation);
            if (!modulation)
            {
                vertices_valid = false;
                if (diag != nullptr)
                    ++diag->triangles_skipped_non_finite;
                break;
            }

            // Animated UV scroll: apply the resolved offset with unit scale
            // ((uv+offset-0.5)*1+0.5 == uv+offset); fail-soft to the base UV.
            asset::FrontendUvIR normalized_st = node.uvs[uv_index];
            if (uv_offset)
            {
                const auto scrolled = TransformUv(normalized_st, *uv_offset,
                    asset::FrontendUvIR{.u = 1.0F, .v = 1.0F});
                if (scrolled)
                    normalized_st = *scrolled;
            }

            triangle.vertices[vertex_index] = RetailFrontEndRasterVertex{
                .x = projected->raster_position.x,
                .y = projected->raster_position.y,
                .depth_rank = 0.0F,
                .normalized_st = normalized_st,
                .modulation = *modulation,
            };
        }
        if (!vertices_valid)
            continue;
        if (!IsRasterizableTriangle(triangle))
        {
            if (diag != nullptr)
                ++diag->triangles_skipped_degenerate;
            continue;
        }
        if (!budget.TryAppend(out, std::move(triangle)))
            return;
        if (diag != nullptr)
            ++diag->ie_triangles_emitted;
    }

    for (const auto& child : node.children)
    {
        const auto claim_entry = claims.find(&child);
        if (claim_entry == claims.end())
        {
            AppendVisualNodeTriangles(scope, child, *world, depth + 1U,
                owner_context, animation_tick, claims, budget, diag, out);
            continue;
        }
        if (!AppendClaimedVisualNode(child, claim_entry->second, depth + 1U,
                owner_context, animation_tick, claims, budget, diag, out))
        {
            AppendVisualNodeTriangles(scope, child, *world, depth + 1U,
                owner_context, animation_tick, claims, budget, diag, out);
        }
        if (budget.exceeded)
            return;
    }
}

// Records widget claims in bounded depth-first preorder. `world` is this
// widget's already-composed world transform. A widget without a binding passes
// it through unchanged. A non-finite binding transform marks that whole subtree
// not drawn while still collecting its claims, so its art cannot leak back in
// through IE containment. Current IR visibility is propagated through ancestors
// in the same way. `false` reports a depth-limit hit so the caller can fail
// closed instead of leaving deeper nodes accidentally unclaimed.
[[nodiscard]] bool CollectVisualNodeClaims(
    const content::FrontEndScreenBundle& bundle,
    const asset::FrontendWidgetIR& widget, const std::uint32_t depth,
    const bool parentless, const AffineTransform12& world,
    const bool ancestors_drawn, const bool transform_valid,
    const asset::FrontendWidgetIR* const parent_claim_owner,
    VisualNodeClaimMap& claims)
{
    if (depth >= kMaximumVisualTreeDepth)
        return false;
    const bool drawn = ancestors_drawn && transform_valid && widget.visible;

    bool claimed = false;
    if (widget.binding)
    {
        const auto* const scope =
            bundle.FindVisualScope(widget.binding->scope_reference);
        const auto* const node = bundle.ResolveVisualBinding(widget, parentless);
        if (scope != nullptr && node != nullptr)
        {
            const auto visual_parent = FindVisualParentTransform(
                scope->document().root, *node, kIdentityAffineTransform12,
                true, 0U);
            AffineTransform12 claim_world = world;
            bool claim_transform_valid =
                transform_valid && visual_parent && visual_parent->valid;
            if (claim_transform_valid)
            {
                const auto composed =
                    ComposeAffineTransforms(world, visual_parent->world);
                if (composed)
                    claim_world = *composed;
                else
                    claim_transform_valid = false;
            }
            claims[node].push_back(VisualNodeClaim{
                .owner = &widget,
                .parent_owner = parent_claim_owner,
                .scope = scope,
                .world = claim_world,
                .drawn = drawn && claim_transform_valid,
                .emitted = false,
            });
            claimed = true;
        }
    }

    bool complete = true;
    const auto* const child_parent_claim_owner =
        claimed ? &widget : parent_claim_owner;
    for (const auto& child : widget.children)
    {
        AffineTransform12 child_world = world;
        bool child_transform_valid = transform_valid;
        if (child.binding)
        {
            if (transform_valid)
            {
                const auto composed = ComposeAffineTransforms(world,
                    AffineTransform12{
                        .column_vectors = child.binding->transform_values});
                if (composed)
                    child_world = *composed;
                else
                    child_transform_valid = false;
            }
        }
        complete = CollectVisualNodeClaims(bundle, child, depth + 1U, false,
                       child_world, drawn, child_transform_valid,
                       child_parent_claim_owner, claims) &&
            complete;
    }
    return complete;
}

// Emits claims unreachable from the primary IE root (for example, a shared
// resource in another scope) in widget preorder. Claims reachable through the IE
// walk have already been consumed in their authored IE sibling positions.
void AppendWidgetVisualTriangles(const content::FrontEndScreenBundle& bundle,
    const asset::FrontendWidgetIR& widget, const std::uint32_t depth,
    const bool parentless, VisualNodeClaimMap& claims,
    const std::uint32_t animation_tick,
    TriangleAppendBudget& budget,
    RetailFrontEndFrameDiagnostics* const diag,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (budget.exceeded || depth >= kMaximumVisualTreeDepth)
        return;

    if (widget.binding)
    {
        if (const auto* const node =
                bundle.ResolveVisualBinding(widget, parentless);
            node != nullptr)
        {
            const auto claim_entry = claims.find(node);
            if (claim_entry != claims.end())
            {
                for (auto& claim : claim_entry->second)
                {
                    if (claim.owner != &widget || claim.emitted)
                        continue;
                    claim.emitted = true;
                    if (!claim.drawn || claim.scope == nullptr)
                        continue;
                    AppendVisualNodeTriangles(*claim.scope, *node, claim.world,
                        0U, claim.owner, animation_tick, claims, budget, diag,
                        out);
                    if (budget.exceeded)
                        return;
                }
            }
        }
    }

    for (const auto& child : widget.children)
    {
        AppendWidgetVisualTriangles(bundle, child, depth + 1U, false, claims,
            animation_tick, budget, diag, out);
        if (budget.exceeded)
            return;
    }
}

// ---- Phase 2: GUI text pass ----------------------------------------------

[[nodiscard]] frontend::HorizontalTextAlignment MapHorizontalAlignment(
    const std::optional<asset::FrontendTextAlignment>& alignment) noexcept
{
    if (!alignment)
        return frontend::HorizontalTextAlignment::Left;
    switch (*alignment)
    {
    case asset::FrontendTextAlignment::Right:
        return frontend::HorizontalTextAlignment::Right;
    case asset::FrontendTextAlignment::Center:
        return frontend::HorizontalTextAlignment::Center;
    case asset::FrontendTextAlignment::Left:
        break;
    }
    return frontend::HorizontalTextAlignment::Left;
}

// Emits one atlas-textured quad (two triangles) for a laid-out glyph. The glyph
// rectangle is in canonical GUI space (Y up); GuiToCanonicalRaster bridges each
// corner to the 640x448 raster (x+320, 224-y). The glyph's atlas UV rectangle
// (already normalized) maps corner-for-corner. Fail-soft: a corner that will not
// bridge to a finite raster point drops the whole glyph.
[[nodiscard]] bool AppendGlyphQuad(const frontend::TextGlyphQuad& glyph,
    const content::FrontEndTextureBinding* const atlas, const RgbaF& color,
    TriangleAppendBudget& budget,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (budget.exceeded)
        return false;
    struct Corner
    {
        float gui_x;
        float gui_y;
        float u;
        float v;
    };
    const std::array<Corner, 4U> corners{
        Corner{glyph.left, glyph.top, glyph.uv.u_left, glyph.uv.v_top},
        Corner{glyph.right, glyph.top, glyph.uv.u_right, glyph.uv.v_top},
        Corner{glyph.right, glyph.bottom, glyph.uv.u_right, glyph.uv.v_bottom},
        Corner{glyph.left, glyph.bottom, glyph.uv.u_left, glyph.uv.v_bottom},
    };
    std::array<RetailFrontEndRasterVertex, 4U> vertices;
    for (std::size_t index = 0U; index < corners.size(); ++index)
    {
        const auto raster = GuiToCanonicalRaster(
            Point2F{corners[index].gui_x, corners[index].gui_y});
        if (!raster)
            return true;
        vertices[index] = RetailFrontEndRasterVertex{
            .x = raster->x,
            .y = raster->y,
            .depth_rank = 0.0F,
            .normalized_st = asset::FrontendUvIR{.u = corners[index].u,
                .v = corners[index].v},
            .modulation = color,
        };
    }
    // Two triangles: (0,1,2) and (0,2,3). Degenerate glyph boxes are dropped by
    // the shared rasterizability test, same as IE geometry.
    RetailFrontEndRasterTriangle first{
        .vertices = {vertices[0U], vertices[1U], vertices[2U]},
        .texture = atlas,
    };
    RetailFrontEndRasterTriangle second{
        .vertices = {vertices[0U], vertices[2U], vertices[3U]},
        .texture = atlas,
    };
    if (IsRasterizableTriangle(first) &&
        !budget.TryAppend(out, std::move(first)))
    {
        return false;
    }
    if (IsRasterizableTriangle(second) &&
        !budget.TryAppend(out, std::move(second)))
    {
        return false;
    }
    return true;
}

// Depth-first over the GUI widget tree. Each visible Text/Button widget carrying
// a text reference resolves its string (bundle string table, falling back to the
// reference text itself), its font and font atlas (bundle resolvers), lays the
// glyphs out with LayoutRetailText inside the widget rectangle, and appends
// atlas-textured glyph quads. Appending text after IE geometry is the same
// experimental PROJECT painter policy as the IE DFS; it is not a recovered
// retail interleave. Fail-soft: a missing string/font/atlas or a failed layout
// skips that widget, never the screen.
// Phase-3 selection affordance: the focused menu label is drawn in this colour
// instead of its authored colour so the player can see the current selection.
// Project-owned (the retail selected-button visual is not yet reverse-engineered).
inline constexpr RgbaF kRetailSelectionHighlightColor{1.0F, 0.85F, 0.20F, 1.0F};

// Constant inter-glyph tracking, in canonical GUI units, that the retail text
// pass adds on top of each glyph's atlas cell width.
//
// Recovered by MEASUREMENT against the retail reference captures, not from the
// disassembly and not decoded from any shipped file, so the exact integer is a
// PROJECT value fitted to that measurement. Method: for a label whose widget
// rectangle is known from its .GUI document, the horizontal screen mapping is
// pinned by two left-aligned labels whose first glyph lands on their box's left
// edge; each neighbouring glyph pair then yields (measured advance - atlas cell
// width). Over 35 such pairs spanning CALLOUT, DEFAULT and LARGEFNT on two
// captured screens the difference is 2.00 canonical units (sigma 0.48, standard
// error 0.08) and shows no dependence on glyph width or font size, so it is a
// constant and not a scale. The fit is corroborated by a centered label: with
// this constant the predicted origin of the centered on-line label falls within
// 0.1 units of its captured ink.
//
// Without it every string is roughly a sixth too narrow and reads as a
// different, cramped typeface even though the glyphs are the retail glyphs.
inline constexpr float kRetailGlyphTracking = 2.0F;

void AppendGuiTextTriangles(const content::FrontEndScreenBundle& bundle,
    const asset::FrontendWidgetIR& widget, const std::uint32_t depth,
    const std::string_view selected_identifier,
    TriangleAppendBudget& budget,
    RetailFrontEndFrameDiagnostics* const diag,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (budget.exceeded || depth >= kMaximumVisualTreeDepth)
        return;
    // Current IR visibility gates the whole subtree. This also makes a runtime-
    // hidden ancestor suppress descendant text consistently with visual claims.
    if (!widget.visible)
        return;

    const bool is_text_widget =
        widget.kind == asset::FrontendWidgetKind::Text ||
        widget.kind == asset::FrontendWidgetKind::Button;
    if (is_text_widget && widget.text_reference)
    {
        if (diag != nullptr)
            ++diag->text_widgets_seen;

        std::string_view text;
        // Retail localization convention (matches game_data_service's validation):
        // a widget text reference is a "$name" key; the string-table lookup strips
        // the leading '$'. RetailStringTableIR::Find lowercases the query to match
        // the decoder's already-lowercased entry keys.
        std::string_view lookup_key = *widget.text_reference;
        if (!lookup_key.empty() && lookup_key.front() == '$')
            lookup_key.remove_prefix(1U);
        if (const auto* const entry = bundle.strings().Find(lookup_key))
        {
            text = entry->value;
            if (diag != nullptr)
                ++diag->strings_resolved;
        }
        else
        {
            // A "$name" reference with no entry in the static string table is
            // runtime-filled dynamic content (mission names, live agent/rank
            // stats, chat) that a static screen render does not have. Render
            // NOTHING for it -- drawing the raw key here overlaps the screen's
            // real static labels (e.g. the Command Center hub's 38 dynamic
            // placeholders piling onto MODIFY AGENT / MEDALS / OMEGA STRAIN).
            // text stays empty -> no glyphs emitted below; children still recurse.
            if (diag != nullptr)
                ++diag->strings_missing;
        }

        const retail::FntV3IR* const font =
            bundle.ResolveFontReference(widget.font_reference);
        const content::FrontEndTextureBinding* const atlas =
            font != nullptr ? bundle.ResolveFontAtlas(*font) : nullptr;
        if (font != nullptr && atlas != nullptr)
        {
            if (diag != nullptr)
                ++diag->fonts_resolved;

            // Retail front-end strings are ASCII; each byte is one codepoint.
            std::u32string codepoints;
            try
            {
                codepoints.reserve(text.size());
                for (const unsigned char byte : text)
                    codepoints.push_back(static_cast<char32_t>(byte));
            }
            catch (const std::bad_alloc&)
            {
                return;
            }

            const bool is_selected = !selected_identifier.empty() &&
                widget.identifier == selected_identifier;
            const RgbaF color = is_selected
                ? kRetailSelectionHighlightColor
                : (widget.text_color
                          ? RgbaF{widget.text_color->red, widget.text_color->green,
                                widget.text_color->blue, widget.text_color->alpha}
                          : RgbaF{1.0F, 1.0F, 1.0F, 1.0F});

            // The font's own glyph row height is the line step, so the vertical
            // centering below centers the GLYPH CELL in the authored widget box
            // -- the placement recovered from the retail captures. Falling back
            // to the box height (the previous step) only happens for a font
            // whose glyph records disagree on their row height, which no
            // shipped retail font does.
            const float line_step =
                font->LineCellHeight(
                        static_cast<float>(atlas->image().height))
                    .value_or(widget.rectangle.height);

            const frontend::TextLayoutOptions options{
                .rectangle = {.left = widget.rectangle.left,
                    .top = widget.rectangle.top,
                    .width = widget.rectangle.width,
                    .height = widget.rectangle.height},
                .atlas_extent = {.width =
                                     static_cast<float>(atlas->image().width),
                    .height = static_cast<float>(atlas->image().height)},
                .line_origin_step = line_step,
                .horizontal_alignment =
                    MapHorizontalAlignment(widget.text_alignment),
                .vertical_alignment = frontend::VerticalTextAlignment::Center,
                .wrap_mode = frontend::TextWrapMode::ExplicitNewlinesOnly,
                .ellipsis_mode = frontend::TextEllipsisMode::Disabled,
                .pair_adjustments = {},
                .limits = {.maximum_codepoints =
                               frontend::kMaximumTextLayoutCodepoints,
                    .maximum_lines = frontend::kMaximumTextLayoutLines,
                    .maximum_glyphs = frontend::kMaximumTextLayoutGlyphs,
                    .maximum_pair_adjustments =
                        frontend::kMaximumTextLayoutPairAdjustments,
                    .maximum_output_bytes =
                        frontend::kMaximumTextLayoutOutputBytes},
                .glyph_tracking = kRetailGlyphTracking,
                .vertical_origin = frontend::GlyphVerticalOrigin::LineOrigin,
            };

            const auto layout = frontend::LayoutRetailText(
                *font, std::u32string_view(codepoints), options);
            if (layout)
            {
                for (const auto& glyph : layout->glyphs)
                {
                    if (!AppendGlyphQuad(glyph, atlas, color, budget, out))
                        return;
                    if (diag != nullptr)
                        ++diag->glyph_quads_emitted;
                }
            }
            else if (diag != nullptr)
            {
                ++diag->text_layouts_failed;
            }
        }
        else if (diag != nullptr)
        {
            ++diag->fonts_missing;
        }
    }

    for (const auto& child : widget.children)
    {
        AppendGuiTextTriangles(
            bundle, child, depth + 1U, selected_identifier, budget, diag, out);
        if (budget.exceeded)
            return;
    }
}
} // namespace

RetailFrontEndFrameResult ComposeRetailFrontEndFrame(
    const content::FrontEndScreenBundle& bundle,
    const RetailFrontEndRasterLimits limits,
    RetailFrontEndFrameDiagnostics* const diagnostics,
    const std::string_view selected_widget_identifier,
    const std::uint32_t animation_tick) noexcept
{
    if (!bundle.presentation_capability().valid())
        return std::unexpected(RetailFrontEndFrameError::InvalidRetailCapability);
    if (limits.maximum_triangles == 0U ||
        limits.maximum_triangles > kRetailFrontEndRasterMaximumTriangles)
    {
        return std::unexpected(RetailFrontEndFrameError::LimitExceeded);
    }

    const auto& root_widget = bundle.widget_document().root;
    if (root_widget.kind != asset::FrontendWidgetKind::Container ||
        !root_widget.visible || !root_widget.binding)
    {
        return std::unexpected(RetailFrontEndFrameError::UnsupportedRootWidget);
    }

    // Validate every immutable scope before ResolveVisualBinding performs its
    // exact-identifier DFS. A too-deep scope is rejected as one frame-level
    // limit error; no partially walked visual tree can reach publication.
    for (const auto& [unused_scope_name, visual_scope] : bundle.visual_scopes())
    {
        (void)unused_scope_name;
        if (!VisualTreeDepthIsBounded(visual_scope.document().root, 0U))
            return std::unexpected(RetailFrontEndFrameError::LimitExceeded);
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
    // the natural convention. It is applied OUTERMOST and exactly once here.
    // ComposeAffineTransforms(parent, local) = parent*local, so placing the
    // bridge at the root keeps it outermost through both widget and IE transform
    // chains. Applying it twice, or not at all, collapses flat menu geometry and
    // the raster kernel rejects the resulting degenerate triangles.
    const AffineTransform12 binding_transform{
        .column_vectors = root_widget.binding->transform_values,
    };
    const auto bridged_initial_world =
        ComposeAffineTransforms(kInterfaceElementAxisBridge, binding_transform);
    if (!bridged_initial_world)
        return std::unexpected(RetailFrontEndFrameError::TransformFailure);
    const AffineTransform12 initial_world = *bridged_initial_world;
    std::vector<RetailFrontEndRasterTriangle> triangles;
    TriangleAppendBudget triangle_budget{
        .remaining = limits.maximum_triangles,
        .exceeded = false,
    };
    try
    {
        VisualNodeClaimMap claims;
        if (!CollectVisualNodeClaims(bundle, root_widget, 0U, true,
                initial_world, true, true, nullptr, claims))
        {
            return std::unexpected(RetailFrontEndFrameError::LimitExceeded);
        }
        AppendWidgetVisualTriangles(bundle, root_widget, 0U, true, claims,
            animation_tick, triangle_budget, diagnostics, triangles);
        if (triangle_budget.exceeded)
            return std::unexpected(RetailFrontEndFrameError::LimitExceeded);
        // Phase 2 GUI text pass. The experimental PROJECT policy appends glyph
        // quads after IE geometry so submission ordinals draw them on top.
        // Public evidence does not establish the retail IE/text interleave.
        AppendGuiTextTriangles(bundle, bundle.widget_document().root, 0U,
            selected_widget_identifier, triangle_budget, diagnostics,
            triangles);
        if (triangle_budget.exceeded)
            return std::unexpected(RetailFrontEndFrameError::LimitExceeded);
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailFrontEndFrameError::AllocationFailure);
    }
    if (triangles.empty())
        return std::unexpected(RetailFrontEndFrameError::MissingRootVisualBinding);

    // Assign each triangle its submission ordinal as depth_rank, normalized into
    // (0, 1). A later triangle gets a strictly larger rank and the CPU raster's
    // GEQUAL (larger-is-nearer) keep makes it cover earlier ones. This is the
    // experimental project-owned painter policy, not a recovered retail order.
    const float ordinal_scale = static_cast<float>(triangles.size() + 1U);
    for (std::size_t triangle_index = 0U; triangle_index < triangles.size();
         ++triangle_index)
    {
        const float rank =
            static_cast<float>(triangle_index + 1U) / ordinal_scale;
        for (auto& vertex : triangles[triangle_index].vertices)
            vertex.depth_rank = rank;
    }

    if (diagnostics != nullptr)
        diagnostics->total_triangles =
            static_cast<std::uint32_t>(triangles.size());

    auto rasterized = RasterizeRetailFrontEndTriangles(
        triangles, RgbaF{0.0F, 0.0F, 0.0F, 1.0F}, limits);
    if (!rasterized)
        return std::unexpected(MapRasterError(rasterized.error()));
    if (diagnostics != nullptr)
    {
        diagnostics->frame_width = rasterized->width;
        diagnostics->frame_height = rasterized->height;
    }
    return std::move(*rasterized);
}
} // namespace omega::frontend::presentation
