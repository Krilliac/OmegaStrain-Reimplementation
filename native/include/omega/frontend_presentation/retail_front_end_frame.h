#pragma once

#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include <cstdint>
#include <expected>

namespace omega::content
{
class FrontEndScreenBundle;
}

namespace omega::frontend::presentation
{
// Identity-free failure categories. Safe to surface to a host log; they never
// carry owner paths, member names, strings, or payloads.
enum class RetailFrontEndFrameError : std::uint8_t
{
    InvalidRetailCapability = 0U,
    UnsupportedRootWidget,
    MissingRootVisualBinding,
    MissingTextureBinding,
    InvalidGeometry,
    TransformFailure,
    ProjectionFailure,
    ArithmeticFailure,
    RasterizationFailure,
    LimitExceeded,
    AllocationFailure,
};

using RetailFrontEndFrameResult =
    std::expected<OwnedRgba8Frame, RetailFrontEndFrameError>;

// Identity-free code-path/value tally for a single compose. Carries only
// aggregate counts (never owner strings, member names, or payloads); safe to
// surface to a host log. Populated when a non-null pointer is passed to
// ComposeRetailFrontEndFrame; the compositor itself does no I/O, so the caller
// decides whether/when to emit it (e.g. under an OPENOMEGA_FRONTEND_TRACE gate).
struct RetailFrontEndFrameDiagnostics final
{
    std::uint32_t visual_nodes_visited = 0U;
    std::uint32_t ie_triangles_emitted = 0U;
    std::uint32_t triangles_skipped_out_of_range = 0U;
    std::uint32_t triangles_skipped_non_finite = 0U;
    std::uint32_t triangles_skipped_degenerate = 0U;
    std::uint32_t text_widgets_seen = 0U;
    std::uint32_t strings_resolved = 0U;
    std::uint32_t strings_missing = 0U;
    std::uint32_t fonts_resolved = 0U;
    std::uint32_t fonts_missing = 0U;
    std::uint32_t text_layouts_failed = 0U;
    std::uint32_t glyph_quads_emitted = 0U;
    std::uint32_t total_triangles = 0U;
    std::uint32_t frame_width = 0U;
    std::uint32_t frame_height = 0U;
};

// [any thread; stateless/reentrant] Composes one retail front-end screen into a
// single fully owned canonical 640x448 frame. The root GUI widget selects the
// screen's visual scope and resolves the root IE node; the entire IE visual
// subtree below it is then emitted in depth-first PREORDER (each node draws its
// own triangles under its accumulated world transform = parent_world * local,
// then recurses into its children), matching the retail render order recovered
// from the disassembly. The fixed IE->raster axis bridge is applied outermost so
// screen vertical comes from IE Y and depth from IE Z. Layering is painter's by
// submission order: each triangle's depth rank is its preorder submission ordinal
// and the CPU raster's GEQUAL keep makes a later node cover an earlier one -- the
// projected Z is deliberately not used as a depth key (real IE Z is unclipped).
//
// After the IE geometry, a GUI text pass (Phase 2) walks the widget tree and
// emits textured glyph quads for visible Text/Button widgets -- string resolved
// via the bundle string table, font/atlas via the bundle resolvers, glyphs laid
// out by LayoutRetailText -- appended last so they carry the highest submission
// ordinals and draw on top. Animation tracks are still held at frame-0; GUI
// action interaction is a later phase. The walk is FAIL-SOFT: a node
// whose declared texture member is not resolvable draws untextured (vertex colors
// only); an out-of-range vertex index, a non-finite transform/projection/colour,
// or a degenerate (zero-area, kernel-rejected) triangle drops only that triangle;
// a non-finite node world transform drops only that node's subtree. Only a wholly
// empty screen, an invalid capability/root widget, or a raster limit/allocation
// failure is a hard error.
//
// Pure value boundary: borrows the immutable live bundle only for the call and
// returns owned bytes; no filesystem/service/global/SDL/renderer/cache work.
[[nodiscard]] RetailFrontEndFrameResult ComposeRetailFrontEndFrame(
    const content::FrontEndScreenBundle& bundle,
    RetailFrontEndRasterLimits limits = {},
    RetailFrontEndFrameDiagnostics* diagnostics = nullptr) noexcept;
} // namespace omega::frontend::presentation
