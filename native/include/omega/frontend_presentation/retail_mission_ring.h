#pragma once

#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include <vector>

namespace omega::content
{
class FrontEndTextureBinding;
}

namespace omega::frontend::presentation
{
// Project-authored APPROXIMATION of the retail Command Center 3D mission-ring
// backdrop (Gap B Slice A). The retail ring is generated procedurally in-engine
// -- there is no ring model file on the disc -- so this reconstructs a static,
// tilted elliptical ring band, reference-matched to the DrawDump mission-select
// frame (analysis/DrawDump/GS/...003840.png), and emits it as textured triangles
// for the existing front-end CPU raster.
//
// Two deliberate approximations vs retail, both fidelity follow-ups:
//   1. Fixed camera / tilt / geometry parameters chosen to seat the ellipse in
//      the frame (no runtime mission state; no L1/R1 rotation -- that is Slice B).
//   2. AFFINE texture interpolation. The vertices ARE perspective-projected for
//      POSITION (so the ellipse foreshortens correctly), but the texture is
//      interpolated affinely over a finely tessellated band; perspective-correct
//      S/T/Q sampling is a later fidelity slice. Per-mission node tiles from
//      ZEUS.HOG are Slice C.
//
// SCOPE -- what this producer deliberately does NOT model: it emits the ring
// SURFACE BAND and nothing else. It does not position, order, anchor or scale
// the hub's mission items or their text labels, and it exposes no per-item slot
// or anchor geometry. Menu label placement comes entirely from the decoded GUI
// widget rectangle (asset::FrontendWidgetIR::rectangle), laid out by
// LayoutRetailText and emitted as glyph quads in the frame compositor's text
// pass; no value produced here participates in it. What drives per-mission
// item placement in the retail runtime is undecoded -- this module asserts
// nothing about it, and must not be extended with a project-chosen item layout
// dressed up as the ring's item positions.
//
// ring_texture may be null: the ring then renders untextured (a cool vertex
// colour) and still reads as the ring surface. Appends to `out`; the caller
// composites it FIRST (lowest submission ordinal) so the 2D shell draws over it.
// Never throws; a degenerate or non-finite candidate triangle is skipped, not
// emitted, so it can never fail the screen raster.
void AppendRetailMissionRingTriangles(
    const content::FrontEndTextureBinding* ring_texture,
    std::vector<RetailFrontEndRasterTriangle>& out);
} // namespace omega::frontend::presentation
