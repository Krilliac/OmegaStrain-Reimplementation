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
#include <cstdio>
#include <cstdlib>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
// the disassembly. A node whose declared texture member is not resolvable falls
// back to the untextured vertex-color path.
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
// only that node's subtree. This matches the retail engine, which submits a fixed
// stream of primitives and skips ones the GS would reject rather than failing the
// frame; a wholly empty result is still caught by the caller.
void AppendVisualNodeTriangles(const content::FrontEndVisualScope& scope,
    const asset::FrontendVisualNodeIR& node,
    const AffineTransform12& parent_world, const std::uint32_t depth,
    const std::uint32_t animation_tick,
    RetailFrontEndFrameDiagnostics* const diag,
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
        if (diag != nullptr)
            ++diag->ie_triangles_emitted;
        out.push_back(triangle);
    }

    for (const auto& child : node.children)
        AppendVisualNodeTriangles(
            scope, child, *world, depth + 1U, animation_tick, diag, out);
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
void AppendGlyphQuad(const frontend::TextGlyphQuad& glyph,
    const content::FrontEndTextureBinding* const atlas, const RgbaF& color,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
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
            return;
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
    const RetailFrontEndRasterTriangle first{
        .vertices = {vertices[0U], vertices[1U], vertices[2U]},
        .texture = atlas,
    };
    const RetailFrontEndRasterTriangle second{
        .vertices = {vertices[0U], vertices[2U], vertices[3U]},
        .texture = atlas,
    };
    if (IsRasterizableTriangle(first))
        out.push_back(first);
    if (IsRasterizableTriangle(second))
        out.push_back(second);
}

// Depth-first over the GUI widget tree. Each visible Text/Button widget carrying
// a text reference resolves its string (bundle string table, falling back to the
// reference text itself), its font and font atlas (bundle resolvers), lays the
// glyphs out with LayoutRetailText inside the widget rectangle, and appends
// atlas-textured glyph quads. Text is emitted AFTER all IE geometry so the
// submission-ordinal depth pass makes it draw on top. Fail-soft: a missing
// string/font/atlas or a failed layout skips that widget, never the screen.
// Phase-3 selection affordance: the focused menu label is drawn in this colour
// instead of its authored colour so the player can see the current selection.
// Project-owned (the retail selected-button visual is not yet reverse-engineered).
inline constexpr RgbaF kRetailSelectionHighlightColor{1.0F, 0.85F, 0.20F, 1.0F};

void AppendGuiTextTriangles(const content::FrontEndScreenBundle& bundle,
    const asset::FrontendWidgetIR& widget, const std::uint32_t depth,
    const std::string_view selected_identifier,
    const std::string_view force_visible_group,
    RetailFrontEndFrameDiagnostics* const diag,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (depth >= kMaximumVisualTreeDepth)
        return;

    // A widget marked not-visible hides its ENTIRE subtree. The retail Command
    // Center hub authors its mutually-exclusive lanes -- each sub-option submenu
    // (personnel/mission/zeus/media_files groups) and the online vs offline nav
    // bars (onlinebutton_grp with Messages, offlinebutton_grp without) -- as vis=0
    // container groups it toggles by runtime state. Recursing into an invisible
    // container drew every lane at once (overlapping submenu labels + doubled nav
    // hints). Culling the subtree matches the retail default. force_visible_group
    // re-enables exactly ONE authored-hidden group for a screen's grounded default
    // state: the hub shows its offline nav bar (offlinebutton_grp), matching the
    // retail command-center-mission-select DrawDump frame (Previous/Select icons,
    // single-player offline mode, no Messages lane).
    if (!widget.visible && widget.identifier != force_visible_group)
        return;

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    static const bool kTreeDump =
        std::getenv("OPENOMEGA_FRONTEND_TREE_DUMP") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (kTreeDump)
    {
        std::fprintf(stderr, "TREEDUMP d=%u kind=%d vis=%d id=%s nchild=%zu\n",
            depth, static_cast<int>(widget.kind), widget.visible ? 1 : 0,
            widget.identifier.c_str(), widget.children.size());
    }

    const bool is_text_widget =
        widget.kind == asset::FrontendWidgetKind::Text ||
        widget.kind == asset::FrontendWidgetKind::Button;
    if (widget.visible && is_text_widget && widget.text_reference)
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

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        static const bool kTextDump =
            std::getenv("OPENOMEGA_FRONTEND_TEXT_DUMP") != nullptr;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (kTextDump)
        {
            std::fprintf(stderr,
                "TEXTDUMP id=%s vis=%d en=%d rect=(%.1f,%.1f,%.1f,%.1f) text=[%.*s]\n",
                widget.identifier.c_str(), widget.visible ? 1 : 0,
                widget.enabled ? 1 : 0, static_cast<double>(widget.rectangle.left),
                static_cast<double>(widget.rectangle.top),
                static_cast<double>(widget.rectangle.width),
                static_cast<double>(widget.rectangle.height),
                static_cast<int>(text.size()), text.data());
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

            const frontend::TextLayoutOptions options{
                .rectangle = {.left = widget.rectangle.left,
                    .top = widget.rectangle.top,
                    .width = widget.rectangle.width,
                    .height = widget.rectangle.height},
                .atlas_extent = {.width =
                                     static_cast<float>(atlas->image().width),
                    .height = static_cast<float>(atlas->image().height)},
                // Single-line menu labels: one line fills the widget box. The FNT
                // exposes no proven line-height, so the box height is the step.
                .line_origin_step = widget.rectangle.height,
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
            };

            const auto layout = frontend::LayoutRetailText(
                *font, std::u32string_view(codepoints), options);
            if (layout)
            {
                for (const auto& glyph : layout->glyphs)
                {
                    AppendGlyphQuad(glyph, atlas, color, out);
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
        AppendGuiTextTriangles(bundle, child, depth + 1U, selected_identifier,
            force_visible_group, diag, out);
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
        AppendVisualNodeTriangles(*scope, *root_visual, initial_world, 0U,
            animation_tick, diagnostics, triangles);
        // Phase 2 GUI text pass. Glyph quads are appended AFTER all IE geometry
        // so the submission-ordinal depth pass below ranks them highest and they
        // draw on top of the screen art. Text is a decoration over a valid IE
        // screen; it never gates composition. Invisible (vis=0) widget subtrees
        // are culled here; the hub re-enables its offline nav group for the
        // grounded default state (see AppendGuiTextTriangles).
        const std::string_view force_visible_group =
            bundle.key() == content::FrontEndScreenKey::CommandCenter
                ? std::string_view("offlinebutton_grp")
                : std::string_view();
        AppendGuiTextTriangles(bundle, bundle.widget_document().root, 0U,
            selected_widget_identifier, force_visible_group, diagnostics,
            triangles);
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
