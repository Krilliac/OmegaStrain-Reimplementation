#pragma once

#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include <cstdint>
#include <span>
#include <string_view>
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
//   3. Every ring quad maps its texture over the FULL 0..1 ST range: the six
//      vertices of each marker and band draw carry exactly (0,0) (0,1) (1,0)
//      (1,1) (1,0) (0,1) with Q=1, and vertex RGBA is a uniform (127,127,127,
//      127) under TFX=Modulate, which is GS unity. So no sub-atlas rectangle
//      and no colour tint exist to reconstruct; a marker's drawn form is its
//      texture's own alpha over its measured quad.
//
// TEXTURE IDENTITY IS NOW PROVEN, and this is the one point on which this
// header contradicts MISSION-RING.md, which lists it under UNPROVEN because
// that pass deliberately did not decode texel payloads. It was decoded
// afterwards, and by byte identity rather than by inference:
//   * The shipped CMDCENTR.HOG members store their GS upload rectangles
//     verbatim (frontend_tdx_decoder.cpp: one block, palette payload at block
//     +0x120, texel payload at +0x520). Those exact byte strings appear in the
//     capture's GIF IMAGE payloads.
//   * Walking the capture's GIF stream while tracking BITBLTBUF gives, for
//     every upload, the destination base pointer it was written to. Replaying
//     uploads and draws in one pass therefore resolves each draw's TEX0.TBP0
//     and CBP to the member whose own bytes most recently occupied that slot --
//     which is what the GS itself samples, and it is decided by byte equality,
//     not by matching dimensions or guessing from names.
//   * Under that resolution the ring's opaque passes are, without exception:
//     markers 3-18 sample BUTTON.TDX (32x32 PSMT4); marker 1 samples
//     BUTTON_HIGHLIGHT.TDX (32x32 PSMT8); the selected marker's two quads
//     sample IPCA_LOGO.TDX then MULTI_ICON.TDX (64x64 PSMT8); markers 19, 20
//     and 21 sample UNLOCKED_DARK.TDX, ONLINEDARK_GREY.TDX and
//     PERSONNELDARK.TDX. Reproduce with the capture and CMDCENTR.HOG; the
//     member names below are the archive members those bytes came from.
// This module never picks a texture itself: it names the member each marker
// samples and the caller resolves those names in the screen's own visual scope.
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
//   * WHICH texture a marker samples when a DIFFERENT marker is selected. Only
//     one selection state was ever captured, so the roles below are the roles
//     observed in that state, applied to whichever index the caller selects.
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

// The two highlight quads are NOT concentric. In capture ...003922 frame 0 the
// halo quad (draw 24) spans (336.688, 338.625)-(389.000, 385.938), centred on
// the marker, and the icon quad (draw 25) spans (339.812, 339.500)-(392.125,
// 386.812) -- the same size, displaced by exactly this much. Only marker 2 was
// ever observed selected, so this displacement is measured once and applied to
// whichever marker the caller selects.
inline constexpr float kRetailMissionRingSelectedIconOffsetX = 3.125F;
inline constexpr float kRetailMissionRingSelectedIconOffsetY = 0.875F;

// The CMDCENTR.HOG members the ring's opaque passes sample, proven by byte
// identity between each member's stored upload rectangle and the capture's GIF
// IMAGE payloads (see the note above). Normalized as the content layer
// normalizes an interface-element texture reference: uppercase stem plus ".TDX".
inline constexpr std::string_view kRetailMissionRingPlainDotMember =
    "BUTTON.TDX";
inline constexpr std::string_view kRetailMissionRingHighlightedDotMember =
    "BUTTON_HIGHLIGHT.TDX";
inline constexpr std::string_view kRetailMissionRingSelectedHaloMember =
    "IPCA_LOGO.TDX";
inline constexpr std::string_view kRetailMissionRingSelectedIconMember =
    "MULTI_ICON.TDX";
inline constexpr std::string_view kRetailMissionRingUnlockedIconMember =
    "UNLOCKED_DARK.TDX";
inline constexpr std::string_view kRetailMissionRingOnlineIconMember =
    "ONLINEDARK_GREY.TDX";
inline constexpr std::string_view kRetailMissionRingPersonnelIconMember =
    "PERSONNELDARK.TDX";

// Which of those members a marker's single opaque pass samples. One role per
// member; the selected marker's two quads use SelectedHalo then SelectedIcon
// regardless of the role its table entry carries, because a selected marker
// does not draw its unselected form at all.
enum class RetailMissionRingTextureRole : std::uint8_t
{
    None,
    PlainDot,
    HighlightedDot,
    SelectedHalo,
    SelectedIcon,
    UnlockedIcon,
    OnlineIcon,
    PersonnelIcon,
};

// The bindings the ring samples, resolved by the caller from the Command Center
// screen bundle. Any of them may be null: a pass whose binding is missing falls
// back to the labelled untextured stand-in described on
// AppendRetailMissionRingTriangles, and the band is simply not emitted.
struct RetailMissionRingTextures final
{
    const content::FrontEndTextureBinding* band = nullptr;
    const content::FrontEndTextureBinding* plain_dot = nullptr;
    const content::FrontEndTextureBinding* highlighted_dot = nullptr;
    const content::FrontEndTextureBinding* selected_halo = nullptr;
    const content::FrontEndTextureBinding* selected_icon = nullptr;
    const content::FrontEndTextureBinding* unlocked_icon = nullptr;
    const content::FrontEndTextureBinding* online_icon = nullptr;
    const content::FrontEndTextureBinding* personnel_icon = nullptr;

    // The binding for a role, or null when that role has none.
    [[nodiscard]] const content::FrontEndTextureBinding* For(
        RetailMissionRingTextureRole role) const noexcept;
};

// One measured marker cluster. Centre and extent are in 640x448 framebuffer
// pixels. No mission identity, ordering rule or ring angle is attached, because
// none is proven.
struct RetailMissionRingMarker final
{
    float center_x = 0.0F;
    float center_y = 0.0F;
    // The extent of the marker's ONE opaque pass -- what it actually draws.
    // This equals the MISSION-RING.md marker table's "marker W x H" column for
    // every marker except 19, where they differ; see the table in the .cpp.
    float width = 0.0F;
    float height = 0.0F;
    // MISSION-RING.md: "Every marker emits exactly one opaque pass (vertex
    // alpha 127 on all six vertices)" -- except marker 0, which "carries no
    // opaque pass at all (alpha 0 on every vertex)" and is the only element
    // whose position changes between frames. False only for marker 0.
    bool emits_opaque_pass = true;
    // The member that opaque pass samples. None exactly when it emits none.
    RetailMissionRingTextureRole opaque_role =
        RetailMissionRingTextureRole::None;

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
// quads at the opaque vertex alpha sampling the halo then the icon member, with
// its small dot suppressed (the captures show that dot at alpha 0) -- and every
// other opaque marker renders its measured quad sampling its own member. Fails
// soft: an index outside [0, kRetailMissionRingMarkerCount) highlights nothing
// and reads nothing out of bounds; it never throws and never asserts.
//
// `textures.band` is the texture the band quad samples. It may be null, and
// then NO band quad is emitted: the captures show the ellipse, the tick marks
// and the lettering are texture art with no ellipse in the vertex stream, so an
// untextured band is not a degraded ring, it is a flat box over the screen.
//
// A MARKER whose binding is null does not fall back to a flat quad, because a
// flat quad is visibly wrong: the retail texture paints a small soft mark
// inside a much larger mostly-transparent footprint, so filling that footprint
// draws a marker several times the retail ink. It instead emits a PROJECT
// APPEARANCE STAND-IN: a triangle fan inscribed in the marker's measured
// extent, opaque at the centre and alpha 0 at the rim. Its footprint is
// measured; its radial falloff is a project choice that asserts nothing about
// the retail art, and it exists only so a failed texture resolution degrades
// into something recognisable rather than into a solid block.
//
// Appends to `out`. The captures put the ring at draws 16-155 of each 168-draw
// frame -- after the backdrop furniture, before the text runs at 156-167 -- so
// the caller should append it AFTER the screen's interface-element geometry and
// BEFORE the text pass. A degenerate or non-finite candidate triangle is
// skipped, not emitted, so this can never fail the screen raster.
void AppendRetailMissionRingTriangles(const RetailMissionRingTextures& textures,
    std::vector<RetailFrontEndRasterTriangle>& out,
    std::uint32_t selected_marker_index =
        kRetailMissionRingCapturedSelectedIndex);
} // namespace omega::frontend::presentation
