#include "omega/frontend_presentation/retail_mission_ring.h"

#include "omega/asset/frontend_ir.h"
#include "omega/frontend/compositor_math.h"

#include <cmath>
#include <cstdint>

namespace omega::frontend::presentation
{
namespace
{
// --- Reference-matched approximation parameters (Command Center DrawDump) ---
// The ring is a flat annulus in the world XZ plane, tilted back about X and
// viewed through a fixed pinhole camera, producing the foreshortened ellipse the
// retail mission-select screen shows. Values chosen to seat the ellipse in the
// centre of the canonical 640x448 frame, wider than tall. These are a project
// approximation of the retail 3D backdrop, not decoded geometry.
constexpr float kPi = 3.14159265358979323846F;
constexpr std::uint32_t kSegments = 96U;   // angular tessellation (affine-safe)
constexpr float kOuterRadius = 1.16F;
constexpr float kInnerRadius = 0.60F;
constexpr float kTiltRadians = 1.17F;      // ~67 deg lean-back
constexpr float kCameraDistance = 2.55F;   // push the ring in front of camera
constexpr float kCameraHeight = 0.42F;     // camera above the ring plane
constexpr float kFocalPixels = 340.0F;
constexpr float kScreenCenterX = 320.0F;
constexpr float kScreenCenterY = 246.0F;   // ring centre slightly below middle
constexpr float kUvRadialSpan = 1.0F;
constexpr float kUvAngularRepeat = 4.0F;   // wrap the texture 4x around the band

struct Vec2 final
{
    float x = 0.0F;
    float y = 0.0F;
};

// Perspective-project a world point (x, 0, z) on the flat ring, after the fixed
// tilt, into canonical raster space. Returns false if it lands at/behind the
// camera or is non-finite.
[[nodiscard]] bool ProjectRingPoint(const float x, const float z,
    Vec2& out) noexcept
{
    const float sin_t = std::sin(kTiltRadians);
    const float cos_t = std::cos(kTiltRadians);
    // Tilt the y=0 ring back about X: y' = -z sin(t), z' = z cos(t).
    const float world_y = -z * sin_t;
    const float world_z = z * cos_t;
    // Camera sits on -Z looking toward +Z, lifted above the plane.
    const float view_x = x;
    const float view_y = world_y - kCameraHeight;
    const float view_z = world_z + kCameraDistance;
    if (!std::isfinite(view_x) || !std::isfinite(view_y) ||
        !std::isfinite(view_z) || !(view_z > 0.03125F))
        return false;
    const float screen_x = kScreenCenterX + kFocalPixels * view_x / view_z;
    const float screen_y = kScreenCenterY - kFocalPixels * view_y / view_z;
    if (!std::isfinite(screen_x) || !std::isfinite(screen_y))
        return false;
    out = Vec2{.x = screen_x, .y = screen_y};
    return true;
}

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

[[nodiscard]] RetailFrontEndRasterVertex MakeVertex(const Vec2 position,
    const float u, const float v, const RgbaF color) noexcept
{
    return RetailFrontEndRasterVertex{
        .x = position.x,
        .y = position.y,
        // Overwritten by the compositor's submission-ordinal depth pass; the
        // ring is appended first, so it ends up under the shell.
        .depth_rank = 0.0F,
        .normalized_st = asset::FrontendUvIR{.u = u, .v = v},
        .modulation = color,
    };
}
} // namespace

void AppendRetailMissionRingTriangles(
    const content::FrontEndTextureBinding* const ring_texture,
    std::vector<RetailFrontEndRasterTriangle>& out)
{
    // White for the textured path (show the TDX at full brightness); a cool tint
    // for the untextured fallback so the band still reads as the ring surface.
    const RgbaF color = ring_texture != nullptr
        ? RgbaF{.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F}
        : RgbaF{.red = 0.22F, .green = 0.42F, .blue = 0.72F, .alpha = 1.0F};

    for (std::uint32_t segment = 0U; segment < kSegments; ++segment)
    {
        const float angle0 = (2.0F * kPi * static_cast<float>(segment)) /
                             static_cast<float>(kSegments);
        const float angle1 = (2.0F * kPi * static_cast<float>(segment + 1U)) /
                             static_cast<float>(kSegments);
        const float cos0 = std::cos(angle0);
        const float sin0 = std::sin(angle0);
        const float cos1 = std::cos(angle1);
        const float sin1 = std::sin(angle1);

        Vec2 inner0;
        Vec2 inner1;
        Vec2 outer0;
        Vec2 outer1;
        if (!ProjectRingPoint(kInnerRadius * cos0, kInnerRadius * sin0, inner0) ||
            !ProjectRingPoint(kInnerRadius * cos1, kInnerRadius * sin1, inner1) ||
            !ProjectRingPoint(kOuterRadius * cos0, kOuterRadius * sin0, outer0) ||
            !ProjectRingPoint(kOuterRadius * cos1, kOuterRadius * sin1, outer1))
            continue;

        const float u0 = (kUvAngularRepeat * static_cast<float>(segment)) /
                         static_cast<float>(kSegments);
        const float u1 = (kUvAngularRepeat * static_cast<float>(segment + 1U)) /
                         static_cast<float>(kSegments);

        const RetailFrontEndRasterTriangle triangle_a{
            .vertices = {MakeVertex(inner0, u0, 0.0F, color),
                MakeVertex(outer0, u0, kUvRadialSpan, color),
                MakeVertex(outer1, u1, kUvRadialSpan, color)},
            .texture = ring_texture,
        };
        const RetailFrontEndRasterTriangle triangle_b{
            .vertices = {MakeVertex(inner0, u0, 0.0F, color),
                MakeVertex(outer1, u1, kUvRadialSpan, color),
                MakeVertex(inner1, u1, 0.0F, color)},
            .texture = ring_texture,
        };
        if (IsFiniteNonDegenerate(triangle_a))
            out.push_back(triangle_a);
        if (IsFiniteNonDegenerate(triangle_b))
            out.push_back(triangle_b);
    }
}
} // namespace omega::frontend::presentation
