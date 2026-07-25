#include "omega/frontend_presentation/retail_mission_ring.h"

#include "omega/asset/frontend_ir.h"
#include "omega/frontend/compositor_math.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

// MISSION-RING.md, "22 markers, at fixed screen positions" -- the marker table,
// verbatim, in draw order. Centre and W x H are the "centre" and "marker W x H"
// columns (the centre and size of each cluster's smallest quad).
constexpr std::array<RetailMissionRingMarker, kRetailMissionRingMarkerCount>
    kMarkers = {{
        // Marker 0 is the animated element: it is the only marker whose
        // position changes between frames and it carries no opaque pass, so it
        // contributes no drawn form here. Its measured position is kept in the
        // table because it is measured; its MEANING is unproven.
        {.center_x = 511.812F, .center_y = 180.438F, .width = 43.00F,
            .height = 37.50F, .emits_opaque_pass = false},
        {.center_x = 437.844F, .center_y = 361.000F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true},
        {.center_x = 362.844F, .center_y = 362.281F, .width = 29.81F,
            .height = 26.94F, .emits_opaque_pass = true},
        {.center_x = 297.688F, .center_y = 374.406F, .width = 29.62F,
            .height = 26.81F, .emits_opaque_pass = true},
        {.center_x = 229.000F, .center_y = 372.094F, .width = 29.62F,
            .height = 26.81F, .emits_opaque_pass = true},
        {.center_x = 202.031F, .center_y = 330.719F, .width = 25.94F,
            .height = 24.19F, .emits_opaque_pass = true},
        {.center_x = 141.781F, .center_y = 325.062F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true},
        {.center_x = 123.719F, .center_y = 279.938F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true},
        {.center_x = 168.562F, .center_y = 247.844F, .width = 28.38F,
            .height = 23.81F, .emits_opaque_pass = true},
        {.center_x = 99.125F, .center_y = 218.625F, .width = 27.75F,
            .height = 25.88F, .emits_opaque_pass = true},
        {.center_x = 160.750F, .center_y = 189.469F, .width = 27.75F,
            .height = 25.94F, .emits_opaque_pass = true},
        {.center_x = 176.250F, .center_y = 136.875F, .width = 29.25F,
            .height = 26.50F, .emits_opaque_pass = true},
        {.center_x = 253.094F, .center_y = 156.469F, .width = 26.31F,
            .height = 24.56F, .emits_opaque_pass = true},
        {.center_x = 258.562F, .center_y = 101.625F, .width = 27.75F,
            .height = 25.88F, .emits_opaque_pass = true},
        {.center_x = 315.344F, .center_y = 111.344F, .width = 29.81F,
            .height = 26.94F, .emits_opaque_pass = true},
        {.center_x = 371.344F, .center_y = 105.844F, .width = 29.81F,
            .height = 26.94F, .emits_opaque_pass = true},
        {.center_x = 433.594F, .center_y = 85.594F, .width = 27.81F,
            .height = 25.94F, .emits_opaque_pass = true},
        {.center_x = 425.344F, .center_y = 142.062F, .width = 25.56F,
            .height = 23.88F, .emits_opaque_pass = true},
        {.center_x = 479.219F, .center_y = 134.688F, .width = 29.81F,
            .height = 27.00F, .emits_opaque_pass = true},
        // Markers 19, 20 and 21 use larger quads and no 32x32 dot texture:
        // MISSION-RING.md records them as the three distinctly shaped icons on
        // the right of the ring. Their own measured extent IS their drawn form.
        {.center_x = 515.031F, .center_y = 164.375F, .width = 37.06F,
            .height = 34.62F, .emits_opaque_pass = true},
        {.center_x = 513.156F, .center_y = 269.031F, .width = 54.06F,
            .height = 45.56F, .emits_opaque_pass = true},
        {.center_x = 469.375F, .center_y = 310.344F, .width = 52.75F,
            .height = 45.56F, .emits_opaque_pass = true},
    }};

// --- Rendering-only choices (NOT measured) ----------------------------------
// MISSION-RING.md deliberately did not decode texel payloads and states that
// per-marker texture identity is unproven, so markers are drawn untextured with
// a flat modulation. These two colours are presentation, not reconstruction:
// they assert nothing about the retail art and no measurement depends on them.
constexpr RgbaF kMarkerColor{
    .red = 0.86F, .green = 0.92F, .blue = 1.00F, .alpha = kOpaqueVertexAlpha};
constexpr RgbaF kHighlightColor{
    .red = 1.00F, .green = 0.97F, .blue = 0.78F, .alpha = kOpaqueVertexAlpha};

// Two triangles per quad, and the emitted quad budget: one band quad, plus one
// quad for each opaque marker, plus the extra highlight quad.
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
// marker. The full 0..1 ST mapping is the minimal choice: MISSION-RING.md
// measures each quad's screen extent but not its ST values, and explicitly does
// not decode texel payloads, so no sub-atlas rectangle is invented.
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

void AppendCenteredQuad(const float center_x, const float center_y,
    const float width, const float height, const RgbaF color,
    const content::FrontEndTextureBinding* const texture,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    const float half_width = width * 0.5F;
    const float half_height = height * 0.5F;
    AppendQuad(center_x - half_width, center_y - half_height,
        center_x + half_width, center_y + half_height, color, texture, out);
}
} // namespace

std::span<const RetailMissionRingMarker> RetailMissionRingMarkers() noexcept
{
    return std::span<const RetailMissionRingMarker>(kMarkers);
}

void AppendRetailMissionRingTriangles(
    const content::FrontEndTextureBinding* const band_texture,
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
    // Which texture retail binds here is NOT measured (MISSION-RING.md,
    // "Texture identity"), so the module never picks one; the caller supplies it
    // or supplies nothing.
    if (band_texture != nullptr)
    {
        constexpr RgbaF band_color{.red = 1.0F, .green = 1.0F, .blue = 1.0F,
            .alpha = kOpaqueVertexAlpha};
        AppendQuad(kRetailMissionRingBandMinimumX,
            kRetailMissionRingBandMinimumY, kRetailMissionRingBandMaximumX,
            kRetailMissionRingBandMaximumY, band_color, band_texture, out);
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
            // TWO 52.31 x 47.31 quads at the opaque alpha (a halo plus a
            // distinct icon texture) and leaves both of its dot draws at alpha
            // 0. An alpha-0 draw contributes no sample, so the dot is simply
            // not emitted.
            for (std::uint32_t quad = 0U;
                 quad < kRetailMissionRingHighlightQuadCount; ++quad)
            {
                AppendCenteredQuad(marker.center_x, marker.center_y,
                    kRetailMissionRingHighlightWidth,
                    kRetailMissionRingHighlightHeight, kHighlightColor, nullptr,
                    out);
            }
            continue;
        }
        if (!marker.emits_opaque_pass)
            continue;
        AppendCenteredQuad(marker.center_x, marker.center_y, marker.width,
            marker.height, kMarkerColor, nullptr, out);
    }
}
} // namespace omega::frontend::presentation
