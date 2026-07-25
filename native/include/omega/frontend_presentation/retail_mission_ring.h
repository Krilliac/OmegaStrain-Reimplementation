#pragma once

#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include <cstdint>
#include <span>
#include <vector>

namespace omega::content
{
class FrontEndTextureBinding;
}

namespace omega::frontend::presentation
{
// MEASURED reconstruction of the retail Command Center mission-select ring.
// Every position, extent and count below is transcribed from
// analysis/formats/MISSION-RING.md, which derives them from three owner-private
// PCSX2 GS captures of the `command-center-mission-select` cohort via
// tools/gs_dump_inspect.py. The captures are in a 640x448 framebuffer, which is
// exactly this project's canonical front-end raster, so the coordinates are
// used verbatim with no rescaling.
//
// What the captures proved, and what this module therefore models:
//   1. The ring BAND is not geometry. It is a six-vertex, screen-aligned,
//      axis-aligned textured quad over a 256x256 PSMT8 texture, re-drawn 40
//      times per frame at a modal extent of (170.812, 97.438)-(489.688,
//      387.500). The visible ellipse, tick marks and continental lettering are
//      texture art; no ellipse exists in the vertex stream. The earlier
//      project-guessed tilted-annulus model (kOuterRadius / kTiltRadians /
//      kCameraDistance ...) is DISPROVEN and has been removed.
//   2. The 22 markers are NOT evenly spaced on an ellipse. Normalised radii
//      span 0.748..1.190 and consecutive angular gaps run 10.29 to 47.59 deg
//      against a uniform 17.14; a projective fit from a uniformly spaced circle
//      leaves 38.2 px RMS. They are fixed, map-like positions, so they are a
//      fixed table here, listed in retail draw order (which the captures show
//      is monotone in ring angle).
//
// SCOPE -- the prohibition this header used to carry, that the module "must not
// be extended with a project-chosen item layout dressed up as the ring's item
// positions", was correct when written and is now SATISFIED, not waived: the
// item positions emitted here are MEASURED from the captures documented in
// analysis/formats/MISSION-RING.md, not chosen by this project. Its spirit is
// preserved unchanged -- anything still unmeasured stays out. Per that document
// the following remain UNPROVEN and must NOT be invented here:
//   * whether 22 equals the mission count (the honest range of selectable
//     entries is 18-22), so this module exposes marker indices, never missions;
//   * what drives the ordering (stored table, depth sort, or re-sorted view);
//   * the ROTATION STEP -- no capture spans an L1/R1 press, so rotation is
//     unobservable. No rotation is implemented. The selected index picks among
//     FIXED positions and never rotates the ring to bring a selection to a
//     focal point: the captures show the highlighted marker is not at the
//     ring's lowest point (marker 3 is lower than the highlighted marker 2);
//   * whether the layout is geographic -- the positions are an opaque fixed
//     table;
//   * what the animated marker 0 represents;
//   * texture identity per marker, and the ST coordinates of any quad.
// Menu label placement is still none of this module's business: it comes from
// the decoded GUI widget rectangle (asset::FrontendWidgetIR::rectangle) laid
// out by LayoutRetailText in the frame compositor's text pass.

// MISSION-RING.md, "The ring band is a screen-aligned textured quad, not
// geometry": modal band quad (170.812, 97.438) - (489.688, 387.500).
inline constexpr float kRetailMissionRingBandMinimumX = 170.812F;
inline constexpr float kRetailMissionRingBandMinimumY = 97.438F;
inline constexpr float kRetailMissionRingBandMaximumX = 489.688F;
inline constexpr float kRetailMissionRingBandMaximumY = 387.500F;

// MISSION-RING.md, "22 markers, at fixed screen positions".
inline constexpr std::uint32_t kRetailMissionRingMarkerCount = 22U;

// MISSION-RING.md, "The highlighted marker": marker 2, centred at
// (362.844, 362.281), is the selected one in every capture. It is the default
// selection here because it is the only selection state ever observed.
inline constexpr std::uint32_t kRetailMissionRingCapturedSelectedIndex = 2U;

// MISSION-RING.md, "The highlighted marker": the selected marker's drawn extent
// is "the pair of 52.31 x 47.31 quads spanning (336.688, 338.625) -
// (389.000, 385.938)" -- 52.312 x 47.312 px centred on the marker centre.
inline constexpr float kRetailMissionRingHighlightWidth = 52.312F;
inline constexpr float kRetailMissionRingHighlightHeight = 47.312F;
inline constexpr std::uint32_t kRetailMissionRingHighlightQuadCount = 2U;

// One measured marker cluster. Centre and extent are the centre and size of the
// cluster's smallest quad from the MISSION-RING.md marker table, in 640x448
// framebuffer pixels. No mission identity, ordering rule or ring angle is
// attached, because none is proven.
struct RetailMissionRingMarker final
{
    float center_x = 0.0F;
    float center_y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
    // MISSION-RING.md: "Every marker emits exactly one opaque pass (vertex
    // alpha 127 on all six vertices)" -- except marker 0, which "carries no
    // opaque pass at all (alpha 0 on every vertex)" and is the only element
    // whose position changes between frames. False only for marker 0.
    bool emits_opaque_pass = true;

    bool operator==(const RetailMissionRingMarker&) const = default;
};

// The measured marker table, in retail draw order. Static storage; the span is
// valid for the program's lifetime and allocates nothing.
[[nodiscard]] std::span<const RetailMissionRingMarker>
RetailMissionRingMarkers() noexcept;

// Emits the Command Center mission ring as canonical-raster triangles: the band
// quad first (lowest submission ordinal), then the markers in measured draw
// order.
//
// `selected_marker_index` selects among the fixed measured positions. The
// selected marker renders the measured highlight form -- two 52.312 x 47.312
// quads at the opaque vertex alpha, with its small dot suppressed (the captures
// show that dot at alpha 0) -- and every other opaque marker renders its
// measured dot. Fails soft: an index outside [0, kRetailMissionRingMarkerCount)
// highlights nothing and reads nothing out of bounds; it never throws and never
// asserts.
//
// `band_texture` is the texture the band quad samples. It may be null, and then
// NO band quad is emitted: the captures show the ellipse, the tick marks and
// the lettering are texture art with no ellipse in the vertex stream, so an
// untextured band is not a degraded ring, it is a flat box over the screen.
// Which texture retail binds is not measured (MISSION-RING.md, "Texture
// identity"), so this module never picks one. Markers are always untextured for
// the same reason -- no marker atlas is invented, and each marker therefore
// renders its full measured quad footprint rather than whatever smaller mark
// the retail texture paints inside it.
//
// Appends to `out`. The captures put the ring at draws 16-155 of each 168-draw
// frame -- after the backdrop furniture, before the text runs at 156-167 -- so
// the caller should append it AFTER the screen's interface-element geometry and
// BEFORE the text pass. A degenerate or non-finite candidate triangle is
// skipped, not emitted, so this can never fail the screen raster.
void AppendRetailMissionRingTriangles(
    const content::FrontEndTextureBinding* band_texture,
    std::vector<RetailFrontEndRasterTriangle>& out,
    std::uint32_t selected_marker_index =
        kRetailMissionRingCapturedSelectedIndex);
} // namespace omega::frontend::presentation
