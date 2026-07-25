#include "omega/frontend_presentation/retail_mission_ring.h"

#include "omega/asset/frontend_ir.h"
#include "omega/frontend/compositor_math.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>

namespace omega::frontend::presentation
{
namespace
{
// --- Measured constants (analysis/formats/MISSION-RING.md) ------------------
// All coordinates are framebuffer pixels in the captures' 640x448 display area
// (DISP0.DISPLAY: DW=2559, MAGH=3, DH=447, MAGV=0), which is byte-for-byte this
// project's canonical front-end raster, so nothing is rescaled.

// MISSION-RING.md, "Per-marker draw structure": every opaque pass is emitted at
// vertex alpha 127 of 128. Carried through literally rather than rounded to 1.0
// so the number stays traceable to the capture.
constexpr float kOpaqueVertexAlpha = 127.0F / 128.0F;

using Role = RetailMissionRingTextureRole;

// MISSION-RING.md, "22 markers, at fixed screen positions" -- the marker table
// in draw order. Centre is that document's "centre" column verbatim.
//
// The EXTENT here is the marker's opaque pass, which is what it draws. That
// document's "marker W x H" column is the smallest quad in each cluster, and
// for 21 of the 22 markers the smallest quad IS the opaque one, so the two
// agree. Marker 19 is the exception: its cluster's smallest quad (37.06 x
// 34.62, capture ...003922 frame 0 draw 145) is emitted at alpha 0, and its
// opaque pass is draw 146 at 43.69 x 40.75 on the same centre. Taking the
// document's column there drew marker 19 about 15% too small, so the opaque
// extent is used and the divergence is recorded rather than smoothed over.
//
// The role is the CMDCENTR.HOG member that opaque pass samples, resolved by
// byte identity between the member's stored GS upload rectangle and the
// capture's IMAGE payloads (see the header). Marker 0 emits no opaque pass and
// therefore samples nothing.
constexpr std::array<RetailMissionRingMarker, kRetailMissionRingMarkerCount>
    kMarkers = {{
        // Marker 0 is the animated element: it is the only marker whose
        // position changes between frames and it carries no opaque pass, so it
        // contributes no drawn form here. Its measured position is kept in the
        // table because it is measured; its MEANING is unproven.
        {.center_x = 511.812F, .center_y = 180.438F, .width = 43.00F,
            .height = 37.50F, .emits_opaque_pass = false,
            .opaque_role = Role::None},
        // Marker 1 is the only plain dot whose opaque pass is the HIGHLIGHTED
        // button art rather than the plain one (capture draw 19 samples
        // BUTTON_HIGHLIGHT.TDX where draws 36..141 sample BUTTON.TDX). The
        // reference screenshot agrees: that dot reads brighter and bluer than
        // its neighbours. Why it, and not the selected marker 2, is unproven.
        {.center_x = 437.844F, .center_y = 361.000F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true,
            .opaque_role = Role::HighlightedDot},
        {.center_x = 362.844F, .center_y = 362.281F, .width = 29.81F,
            .height = 26.94F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 297.688F, .center_y = 374.406F, .width = 29.62F,
            .height = 26.81F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 229.000F, .center_y = 372.094F, .width = 29.62F,
            .height = 26.81F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 202.031F, .center_y = 330.719F, .width = 25.94F,
            .height = 24.19F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 141.781F, .center_y = 325.062F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 123.719F, .center_y = 279.938F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 168.562F, .center_y = 247.844F, .width = 28.38F,
            .height = 23.81F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 99.125F, .center_y = 218.625F, .width = 27.75F,
            .height = 25.88F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 160.750F, .center_y = 189.469F, .width = 27.75F,
            .height = 25.94F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 176.250F, .center_y = 136.875F, .width = 29.25F,
            .height = 26.50F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 253.094F, .center_y = 156.469F, .width = 26.31F,
            .height = 24.56F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 258.562F, .center_y = 101.625F, .width = 27.75F,
            .height = 25.88F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 315.344F, .center_y = 111.344F, .width = 29.81F,
            .height = 26.94F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 371.344F, .center_y = 105.844F, .width = 29.81F,
            .height = 26.94F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 433.594F, .center_y = 85.594F, .width = 27.81F,
            .height = 25.94F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 425.344F, .center_y = 142.062F, .width = 25.56F,
            .height = 23.88F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        {.center_x = 479.219F, .center_y = 134.688F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true,
            .opaque_role = Role::PlainDot},
        // Markers 19, 20 and 21 are the three distinctly shaped icons on the
        // right of the ring. Their opaque passes sample menu-entry art, not the
        // dot art; 19's opaque extent is its own, not its cluster minimum.
        {.center_x = 515.031F, .center_y = 164.375F, .width = 43.69F,
            .height = 40.75F, .emits_opaque_pass = true,
            .opaque_role = Role::UnlockedIcon},
        {.center_x = 513.156F, .center_y = 269.031F, .width = 54.06F,
            .height = 45.56F, .emits_opaque_pass = true,
            .opaque_role = Role::OnlineIcon},
        {.center_x = 469.375F, .center_y = 310.344F, .width = 52.75F,
            .height = 45.56F, .emits_opaque_pass = true,
            .opaque_role = Role::PersonnelIcon},
    }};

// MEASURED modulation. Every ring vertex in the capture carries RGBA
// (127, 127, 127, 127) under TFX=Modulate, and GS modulate treats 128 as unity
// (Cv = Cs * Ct >> 7), so 127/128 is white to within one part in 128. The
// marker's colour is therefore entirely its texture's; nothing is tinted.
constexpr RgbaF kOpaqueModulation{
    .red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = kOpaqueVertexAlpha};

// --- Untextured stand-in (NOT measured) -------------------------------------
// Used only when a binding fails to resolve. The retail dot art paints a small
// soft mark inside a mostly-transparent 32x32 cell, so a flat quad over the
// measured footprint is several times too much ink; a fan that fades to nothing
// at the measured rim is the closest honest degradation. The rim count and the
// linear falloff are PROJECT choices and assert nothing about the retail art.
constexpr std::uint32_t kStandInRimVertices = 16U;

// Two triangles per quad, and the emitted quad budget: one band quad, plus one
// quad for each opaque marker, plus the extra highlight quad. The stand-in fan
// emits more triangles than a quad, so this is a reserve hint, not a bound.
constexpr std::size_t kTrianglesPerQuad = 2U;
constexpr std::size_t kMaximumQuads =
    1U + kRetailMissionRingMarkerCount + kRetailMissionRingHighlightQuadCount;

[[nodiscard]] bool IsFiniteNonDegenerate(
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

[[nodiscard]] RetailFrontEndRasterVertex MakeVertex(const float x,
    const float y, const float u, const float v, const RgbaF color) noexcept
{
    return RetailFrontEndRasterVertex{
        .x = x,
        .y = y,
        // Overwritten by the compositor's submission-ordinal depth pass, which
        // ranks by append order; the caller places the ring after the screen's
        // interface-element geometry and before the text pass.
        .depth_rank = 0.0F,
        .normalized_st = asset::FrontendUvIR{.u = u, .v = v},
        .modulation = color,
    };
}

// Emits one screen-aligned, axis-aligned quad as two triangles, matching the
// six-vertex triangle-strip quad the captures show for both the band and every
// marker. The full 0..1 ST mapping is MEASURED, not assumed: every ring draw's
// six vertices carry exactly (0,0) (0,1) (1,0) (1,1) (1,0) (0,1) with Q=1.
void AppendQuad(const float min_x, const float min_y, const float max_x,
    const float max_y, const RgbaF color,
    const content::FrontEndTextureBinding* const texture,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    const RetailFrontEndRasterTriangle triangle_a{
        .vertices = {MakeVertex(min_x, min_y, 0.0F, 0.0F, color),
            MakeVertex(max_x, min_y, 1.0F, 0.0F, color),
            MakeVertex(max_x, max_y, 1.0F, 1.0F, color)},
        .texture = texture,
    };
    const RetailFrontEndRasterTriangle triangle_b{
        .vertices = {MakeVertex(min_x, min_y, 0.0F, 0.0F, color),
            MakeVertex(max_x, max_y, 1.0F, 1.0F, color),
            MakeVertex(min_x, max_y, 0.0F, 1.0F, color)},
        .texture = texture,
    };
    if (IsFiniteNonDegenerate(triangle_a))
        out.push_back(triangle_a);
    if (IsFiniteNonDegenerate(triangle_b))
        out.push_back(triangle_b);
}

// PROJECT APPEARANCE STAND-IN. A fan inscribed in the measured extent: opaque
// at the centre, alpha 0 on the rim, untextured. Only reached when a binding is
// missing.
void AppendStandInDot(const float center_x, const float center_y,
    const float width, const float height, const RgbaF color,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    const RgbaF rim_color{.red = color.red, .green = color.green,
        .blue = color.blue, .alpha = 0.0F};

    const auto center =
        MakeVertex(center_x, center_y, 0.5F, 0.5F, color);
    const auto rim = [&](const std::uint32_t step) {
        const float angle = (2.0F * std::numbers::pi_v<float> *
                                static_cast<float>(step % kStandInRimVertices)) /
            static_cast<float>(kStandInRimVertices);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        return MakeVertex(center_x + (half_width * cosine),
            center_y + (half_height * sine), 0.5F + (0.5F * cosine),
            0.5F + (0.5F * sine), rim_color);
    };
    for (std::uint32_t step = 0U; step < kStandInRimVertices; ++step)
    {
        const RetailFrontEndRasterTriangle wedge{
            .vertices = {center, rim(step), rim(step + 1U)},
            .texture = nullptr,
        };
        if (IsFiniteNonDegenerate(wedge))
            out.push_back(wedge);
    }
}

// One marker pass: the measured quad over its resolved texture, or the labelled
// stand-in when that texture is missing.
void AppendMarkerPass(const float center_x, const float center_y,
    const float width, const float height,
    const content::FrontEndTextureBinding* const texture,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    if (texture == nullptr)
    {
        AppendStandInDot(
            center_x, center_y, width, height, kOpaqueModulation, out);
        return;
    }
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    AppendQuad(center_x - half_width, center_y - half_height,
        center_x + half_width, center_y + half_height, kOpaqueModulation,
        texture, out);
}
} // namespace

const content::FrontEndTextureBinding* RetailMissionRingTextures::For(
    const RetailMissionRingTextureRole role) const noexcept
{
    switch (role)
    {
    case RetailMissionRingTextureRole::PlainDot:
        return plain_dot;
    case RetailMissionRingTextureRole::HighlightedDot:
        return highlighted_dot;
    case RetailMissionRingTextureRole::SelectedHalo:
        return selected_halo;
    case RetailMissionRingTextureRole::SelectedIcon:
        return selected_icon;
    case RetailMissionRingTextureRole::UnlockedIcon:
        return unlocked_icon;
    case RetailMissionRingTextureRole::OnlineIcon:
        return online_icon;
    case RetailMissionRingTextureRole::PersonnelIcon:
        return personnel_icon;
    case RetailMissionRingTextureRole::None:
        break;
    }
    return nullptr;
}

std::span<const RetailMissionRingMarker> RetailMissionRingMarkers() noexcept
{
    return std::span<const RetailMissionRingMarker>(kMarkers);
}

void AppendRetailMissionRingTriangles(
    const RetailMissionRingTextures& textures,
    std::vector<RetailFrontEndRasterTriangle>& out,
    const std::uint32_t selected_marker_index)
{
    out.reserve(out.size() + (kMaximumQuads * kTrianglesPerQuad));

    // The band. MISSION-RING.md, "The ring band is a screen-aligned textured
    // quad, not geometry": one axis-aligned quad over a 256x256 PSMT8 texture at
    // the modal extent (170.812, 97.438) - (489.688, 387.500). Retail re-draws
    // it 40 times per frame as part of a blended texture stack; the per-draw
    // blend weights are not measured, so it is emitted once here rather than
    // stacking 40 unmeasured copies.
    //
    // The band exists ONLY as texture art: the same document establishes that
    // "the elliptical track, the tick marks and the AMERICA lettering visible on
    // screen are texture art" and that no ellipse is in the vertex stream. A
    // band with no texture is therefore not a degraded ring, it is a flat opaque
    // box over the screen -- so with no texture the band is not emitted at all.
    //
    // WHICH texture belongs here is now decoded, and the answer is why this
    // module still does not choose one. Of the 40 band-extent draws exactly one
    // is opaque (capture ...003922 frame 0 draw 28), and by the byte-identity
    // resolution described in the header it samples TORONTO1.TDX -- a 256x256
    // photographic level tile, one of eighteen in CMDCENTR.HOG, drawn behind
    // the ring for the SELECTED mission. Binding it would therefore require a
    // marker-index-to-level mapping, and MISSION-RING.md leaves both the
    // marker-to-mission correspondence and the ordering rule unproven. The
    // caller supplies a band texture or supplies nothing.
    if (textures.band != nullptr)
    {
        AppendQuad(kRetailMissionRingBandMinimumX,
            kRetailMissionRingBandMinimumY, kRetailMissionRingBandMaximumX,
            kRetailMissionRingBandMaximumY, kOpaqueModulation, textures.band,
            out);
    }

    // The markers, in measured draw order. Fail-soft selection: an index
    // outside the table simply matches no marker, so nothing is highlighted and
    // nothing is read out of bounds.
    for (std::uint32_t index = 0U; index < kRetailMissionRingMarkerCount;
         ++index)
    {
        const RetailMissionRingMarker& marker = kMarkers[index];
        if (index == selected_marker_index)
        {
            // MISSION-RING.md, "The highlighted marker": the selected marker is
            // the only one whose opaque pass is not its small dot -- it emits
            // TWO 52.31 x 47.31 quads at the opaque alpha and leaves both of
            // its dot draws at alpha 0. An alpha-0 draw contributes no sample,
            // so the dot is simply not emitted. The pair is a halo quad on the
            // marker centre and an icon quad displaced from it; both extents
            // and the displacement are measured.
            AppendMarkerPass(marker.center_x, marker.center_y,
                kRetailMissionRingHighlightWidth,
                kRetailMissionRingHighlightHeight,
                textures.For(RetailMissionRingTextureRole::SelectedHalo), out);
            AppendMarkerPass(
                marker.center_x + kRetailMissionRingSelectedIconOffsetX,
                marker.center_y + kRetailMissionRingSelectedIconOffsetY,
                kRetailMissionRingHighlightWidth,
                kRetailMissionRingHighlightHeight,
                textures.For(RetailMissionRingTextureRole::SelectedIcon), out);
            continue;
        }
        if (!marker.emits_opaque_pass)
            continue;
        AppendMarkerPass(marker.center_x, marker.center_y, marker.width,
            marker.height, textures.For(marker.opaque_role), out);
    }
}
} // namespace omega::frontend::presentation
