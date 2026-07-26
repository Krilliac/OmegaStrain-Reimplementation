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
// MEASURED reconstruction of the retail Command Center ring.
// Every position, extent and count below is transcribed from
// analysis/formats/MISSION-RING.md, which derives them from five owner-private
// PCSX2 GS captures spanning the Command Center mission-select and Personnel
// screens via tools/gs_dump_inspect.py. The captures are in a 640x448
// framebuffer, which is exactly this project's canonical front-end raster, so
// the coordinates are used verbatim with no rescaling.
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
// Texture identity for the listed draws was decoded afterwards, by byte
// identity rather than by inference:
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
//   * Under that resolution the common opaque passes are: markers 3-18 sample
//     BUTTON.TDX (32x32 PSMT4), marker 1 samples BUTTON_HIGHLIGHT.TDX
//     (32x32 PSMT8), and markers 19 and 20 sample UNLOCKED_DARK.TDX and
//     ONLINEDARK_GREY.TDX.
//   * On the observed mission-select form, marker 2's two opaque quads sample
//     IPCA_LOGO.TDX then MULTI_ICON.TDX (64x64 PSMT8), while marker 21's
//     opaque draw samples PERSONNELDARK.TDX.
//   * On the observed Personnel form, marker 2 has no opaque pass and marker 21
//     swaps to a different 53.500x46.188 opaque draw. That draw's archive
//     member identity is still unresolved, so this API assigns it no member
//     constant and never substitutes either mission-select binding for it.
// Reproduce the resolved subset with the capture and CMDCENTR.HOG; the member
// names below are the archive members those bytes came from.
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
//   * the selection increment -- no capture spans an L1/R1 press. No rotation
//     is implemented because the two observed forms keep all static markers at
//     identical fixed positions;
//   * whether the layout is geographic -- the positions are an opaque fixed
//     table;
//   * what the animated marker 0 represents;
//   * any selected-marker form other than mission-select marker 2 and Personnel
//     marker 21. The API exposes only those two observed forms.
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

// The only two observed forms. They differ only in four draw alpha values:
// mission select makes marker 2's two large quads opaque and marker 21's dark
// quad opaque; Personnel suppresses marker 2 and swaps marker 21 to its other
// large quad. Arbitrary marker selection remains unproven and is not accepted
// by AppendRetailMissionRingTriangles.
enum class RetailMissionRingObservedForm : std::uint8_t
{
    MissionSelect,
    Personnel,
};

inline constexpr std::uint32_t kRetailMissionRingMissionSelectMarkerIndex = 2U;
inline constexpr std::uint32_t kRetailMissionRingPersonnelMarkerIndex = 21U;

// Compatibility name used by the current frame compositor. Its type is an
// observed form, not an integer marker index, so it cannot re-enable the former
// arbitrary-selection API.
inline constexpr RetailMissionRingObservedForm
    kRetailMissionRingDefaultObservedForm =
        RetailMissionRingObservedForm::MissionSelect;

inline constexpr RetailMissionRingObservedForm
    kRetailMissionRingCapturedSelectedIndex =
        kRetailMissionRingDefaultObservedForm;

// MISSION-RING.md, "The highlighted marker": the selected marker's drawn extent
// is "the pair of 52.31 x 47.31 quads spanning (336.688, 338.625) -
// (389.000, 385.938)" -- 52.312 x 47.312 px centred on the marker centre.
inline constexpr float kRetailMissionRingHighlightWidth = 52.312F;
inline constexpr float kRetailMissionRingHighlightHeight = 47.312F;
inline constexpr std::uint32_t kRetailMissionRingHighlightQuadCount = 2U;

// The two highlight quads are NOT concentric. In capture ...003922 frame 0 the
// halo quad (draw 24) spans (336.688, 338.625)-(389.000, 385.938), centred on
// the marker, and the icon quad (draw 25) spans (339.812, 339.500)-(392.125,
// 386.812) -- the same size, displaced by exactly this much. These constants
// belong only to mission-select marker 2.
inline constexpr float kRetailMissionRingSelectedIconOffsetX = 3.125F;
inline constexpr float kRetailMissionRingSelectedIconOffsetY = 0.875F;

// Personnel marker 21's observed opaque draw 153. Its centre is the marker
// table's centre. The associated archive member is unresolved, so these
// geometry constants carry no texture claim.
inline constexpr float kRetailMissionRingPersonnelSelectedWidth = 53.500F;
inline constexpr float kRetailMissionRingPersonnelSelectedHeight = 46.188F;

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

// Which of those members a measured opaque pass samples. SelectedHalo and
// SelectedIcon are exclusive to mission-select marker 2.
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
    // Optional binding for Personnel marker 21's observed draw 153. No archive
    // member identity is claimed. When absent, that pass is omitted rather
    // than guessed or replaced with mission-select art.
    const content::FrontEndTextureBinding* personnel_selected_observed_draw =
        nullptr;

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
    // The marker table's measured baseline extent. For an emitted baseline
    // pass this is its quad extent; for markers 0 and 2 it retains the measured
    // cluster's small extent even though that pass is transparent. This equals
    // MISSION-RING.md's "marker W x H" column except at marker 19; see the .cpp.
    // State-specific marker 2 and Personnel marker 21 geometry uses the
    // constants above instead.
    float width = 0.0F;
    float height = 0.0F;
    // Whether the marker has the baseline opaque pass described above.
    // Markers 0 and 2 do not: marker 0 is always transparent in the observed
    // frames, while marker 2 uses only its mission-select two-quad form.
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
// `observed_form` selects one of exactly two captured forms:
//   * MissionSelect emits marker 2's measured two-quad IPCA_LOGO/MULTI_ICON
//     form, suppresses its small dot, and emits marker 21's PERSONNELDARK draw.
//   * Personnel suppresses marker 2 and the dark marker-21 draw. It emits
//     marker 21's measured 53.500x46.188 draw only when
//     `personnel_selected_observed_draw` is supplied; otherwise that unresolved
//     pass is omitted fail-closed.
// No arbitrary marker-index highlight is supported. An invalid enum value emits
// only passes common to both observed forms. It never throws or asserts.
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
    RetailMissionRingObservedForm observed_form =
        kRetailMissionRingDefaultObservedForm);
} // namespace omega::frontend::presentation
