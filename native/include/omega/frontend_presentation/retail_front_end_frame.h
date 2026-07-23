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
// This is the Phase-1 still-frame slice: animation tracks are held at frame-0
// (base positions/uvs/colors); text/font layout and GUI action interaction are
// intentionally NOT applied here (later phases). The walk is FAIL-SOFT: a node
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
    RetailFrontEndRasterLimits limits = {}) noexcept;
} // namespace omega::frontend::presentation
