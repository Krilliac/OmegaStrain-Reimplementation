#include "omega/frontend/compositor_math.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <limits>
#include <string_view>
#include <type_traits>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] bool Near(const float left, const float right) noexcept
{
    return std::abs(left - right) <= 0.00001F;
}

[[nodiscard]] bool Near(
    const omega::asset::Float3IR& left, const omega::asset::Float3IR& right) noexcept
{
    return Near(left.x, right.x) && Near(left.y, right.y) && Near(left.z, right.z);
}

[[nodiscard]] bool Near(
    const omega::frontend::RgbF& left, const omega::frontend::RgbF& right) noexcept
{
    return Near(left.red, right.red) && Near(left.green, right.green) &&
           Near(left.blue, right.blue);
}

[[nodiscard]] bool Near(
    const omega::frontend::RgbaF& left, const omega::frontend::RgbaF& right) noexcept
{
    return Near(left.red, right.red) && Near(left.green, right.green) &&
           Near(left.blue, right.blue) && Near(left.alpha, right.alpha);
}
} // namespace

int main()
{
    using omega::frontend::AffineTransform12;
    using omega::frontend::CompositorMathError;
    using omega::frontend::Point2F;
    using omega::frontend::RgbF;
    using omega::frontend::RgbaF;
    using PointResult = std::expected<Point2F, CompositorMathError>;
    using TransformResult = std::expected<AffineTransform12, CompositorMathError>;

    static_assert(std::is_same_v<decltype(omega::frontend::GuiToCanonicalRaster({})), PointResult>);
    static_assert(noexcept(omega::frontend::GuiToCanonicalRaster({})));
    static_assert(noexcept(omega::frontend::TransformPoint({}, {})));
    static_assert(noexcept(omega::frontend::ComposeAffineTransforms({}, {})));
    static_assert(noexcept(omega::frontend::TransformUv({}, {}, {})));
    static_assert(noexcept(omega::frontend::ModulateVertexColor({}, {})));
    static_assert(noexcept(omega::frontend::NormalizeGsAlpha(0U)));
    static_assert(noexcept(omega::frontend::GsAlphaToRgba8(0U)));
    static_assert(noexcept(omega::frontend::NormalizeGsColor({})));
    static_assert(noexcept(omega::frontend::BlendSourceOverRgb({}, {})));
    static_assert(omega::frontend::kCanonicalRasterWidth == 640U);
    static_assert(omega::frontend::kCanonicalRasterHeight == 448U);

    const PointResult raster_origin = omega::frontend::GuiToCanonicalRaster({-320.0F, 224.0F});
    const PointResult raster_center = omega::frontend::GuiToCanonicalRaster({0.0F, 0.0F});
    const PointResult raster_extent = omega::frontend::GuiToCanonicalRaster({320.0F, -224.0F});
    Check(raster_origin && *raster_origin == Point2F{0.0F, 0.0F},
          "GUI top-left maps to the canonical raster origin");
    Check(raster_center && *raster_center == Point2F{320.0F, 224.0F},
          "GUI center maps to the canonical raster center");
    Check(raster_extent && *raster_extent == Point2F{640.0F, 448.0F},
          "GUI right-bottom boundary maps without an invented clamp");

    const PointResult rejected_gui = omega::frontend::GuiToCanonicalRaster(
        {std::numeric_limits<float>::quiet_NaN(), 0.0F});
    Check(!rejected_gui && rejected_gui.error() == CompositorMathError::NonFiniteInput,
          "GUI mapping rejects non-finite input");

    const auto bridged = omega::frontend::TransformPoint(
        omega::frontend::kInterfaceElementAxisBridge, {7.0F, -3.0F, 0.0F});
    Check(bridged && Near(*bridged, {7.0F, 0.0F, -2.0F}),
          "the fixed IE bridge maps (x,y,0) to (x,0,y+1)");
    const auto rejected_point = omega::frontend::TransformPoint(
        omega::frontend::kIdentityAffineTransform12,
        {std::numeric_limits<float>::infinity(), 0.0F, 0.0F});
    Check(!rejected_point && rejected_point.error() == CompositorMathError::NonFiniteInput,
          "point transformation rejects non-finite input");

    constexpr AffineTransform12 parent{
        .column_vectors = {
            2.0F, 0.0F, 0.0F,
            0.0F, 3.0F, 0.0F,
            0.0F, 0.0F, 4.0F,
            10.0F, 20.0F, 30.0F,
        },
    };
    constexpr AffineTransform12 local{
        .column_vectors = {
            1.0F, 0.0F, 0.0F,
            0.0F, 1.0F, 0.0F,
            0.0F, 0.0F, 1.0F,
            1.0F, 2.0F, 3.0F,
        },
    };
    const TransformResult composed = omega::frontend::ComposeAffineTransforms(parent, local);
    Check(composed && composed->column_vectors ==
                          std::array<float, 12>{
                              2.0F, 0.0F, 0.0F,
                              0.0F, 3.0F, 0.0F,
                              0.0F, 0.0F, 4.0F,
                              12.0F, 26.0F, 42.0F,
                          },
          "parent * local preserves the proven column-vector composition order");
    if (composed)
    {
        const auto local_transformed =
            omega::frontend::TransformPoint(local, {5.0F, 6.0F, 7.0F});
        std::expected<omega::asset::Float3IR, CompositorMathError> separately_transformed =
            std::unexpected(CompositorMathError::NonFiniteInput);
        if (local_transformed)
            separately_transformed = omega::frontend::TransformPoint(parent, *local_transformed);
        const auto composed_transform =
            omega::frontend::TransformPoint(*composed, {5.0F, 6.0F, 7.0F});
        Check(separately_transformed && composed_transform &&
                  Near(*separately_transformed, *composed_transform),
              "composed transform matches local-then-parent point evaluation");
    }

    AffineTransform12 nonfinite_transform = omega::frontend::kIdentityAffineTransform12;
    nonfinite_transform.column_vectors[4] = std::numeric_limits<float>::infinity();
    const TransformResult rejected_transform =
        omega::frontend::ComposeAffineTransforms(parent, nonfinite_transform);
    Check(!rejected_transform &&
              rejected_transform.error() == CompositorMathError::NonFiniteInput,
          "affine composition rejects non-finite coefficients");

    AffineTransform12 overflow_parent = omega::frontend::kIdentityAffineTransform12;
    overflow_parent.column_vectors[0] = std::numeric_limits<float>::max();
    AffineTransform12 overflow_local = omega::frontend::kIdentityAffineTransform12;
    overflow_local.column_vectors[0] = 2.0F;
    const TransformResult rejected_overflow =
        omega::frontend::ComposeAffineTransforms(overflow_parent, overflow_local);
    Check(!rejected_overflow &&
              rejected_overflow.error() == CompositorMathError::NonFiniteResult,
          "affine composition rejects a result outside finite float range");
    const auto rejected_point_overflow =
        omega::frontend::TransformPoint(overflow_parent, {2.0F, 0.0F, 0.0F});
    Check(!rejected_point_overflow &&
              rejected_point_overflow.error() == CompositorMathError::NonFiniteResult,
          "point transformation rejects a result outside finite float range");

    const auto transformed_uv = omega::frontend::TransformUv(
        {.u = 0.25F, .v = 0.75F},
        {.u = 0.10F, .v = -0.20F},
        {.u = 2.0F, .v = 0.5F});
    Check(transformed_uv && Near(transformed_uv->u, 0.20F) && Near(transformed_uv->v, 0.525F),
          "UV transform applies offset about the 0.5 pivot");
    const auto out_of_range_uv = omega::frontend::TransformUv(
        {.u = 2.0F, .v = -1.0F}, {}, {.u = 1.0F, .v = 1.0F});
    Check(out_of_range_uv && Near(out_of_range_uv->u, 2.0F) && Near(out_of_range_uv->v, -1.0F),
          "UV transform does not invent clamp or repeat sampling policy");
    const auto rejected_uv = omega::frontend::TransformUv(
        {.u = 0.0F, .v = 0.0F},
        {.u = std::numeric_limits<float>::infinity(), .v = 0.0F},
        {.u = 1.0F, .v = 1.0F});
    Check(!rejected_uv && rejected_uv.error() == CompositorMathError::NonFiniteInput,
          "UV transform rejects non-finite values");
    const auto rejected_uv_overflow = omega::frontend::TransformUv(
        {.u = std::numeric_limits<float>::max(), .v = 0.0F},
        {},
        {.u = 2.0F, .v = 1.0F});
    Check(!rejected_uv_overflow &&
              rejected_uv_overflow.error() == CompositorMathError::NonFiniteResult,
          "UV transform rejects a result outside finite float range");

    const auto modulated = omega::frontend::ModulateVertexColor(
        {.red = 255U, .green = 128U, .blue = 64U, .alpha = 32U},
        {.red = 0.5F, .green = 0.25F, .blue = 1.0F, .alpha = 0.75F});
    Check(modulated && Near(*modulated,
                            RgbaF{0.5F, (128.0F / 255.0F) * 0.25F, 64.0F / 255.0F,
                                  (32.0F / 255.0F) * 0.75F}),
          "vertex RGBA bytes normalize and multiply the effective node color");
    const auto rejected_color = omega::frontend::ModulateVertexColor(
        {}, {.red = 1.01F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F});
    Check(!rejected_color && rejected_color.error() == CompositorMathError::ColorOutOfRange,
          "vertex modulation rejects non-normalized effective color");
    const auto rejected_nonfinite_color = omega::frontend::ModulateVertexColor(
        {}, {.red = 0.0F,
             .green = std::numeric_limits<float>::quiet_NaN(),
             .blue = 0.0F,
             .alpha = 1.0F});
    Check(!rejected_nonfinite_color &&
              rejected_nonfinite_color.error() == CompositorMathError::NonFiniteInput,
          "vertex modulation rejects non-finite effective color");

    Check(Near(omega::frontend::NormalizeGsAlpha(0U), 0.0F) &&
              Near(omega::frontend::NormalizeGsAlpha(64U), 0.5F) &&
              Near(omega::frontend::NormalizeGsAlpha(128U), 1.0F) &&
              Near(omega::frontend::NormalizeGsAlpha(255U), 1.0F),
          "raw GS alpha maps 0x80 to opaque and clamps larger coefficients");
    Check(omega::frontend::GsAlphaToRgba8(0U) == 0U &&
              omega::frontend::GsAlphaToRgba8(1U) == 2U &&
              omega::frontend::GsAlphaToRgba8(64U) == 128U &&
              omega::frontend::GsAlphaToRgba8(127U) == 253U &&
              omega::frontend::GsAlphaToRgba8(128U) == 255U &&
              omega::frontend::GsAlphaToRgba8(255U) == 255U,
          "GS alpha RGBA8 bridge rounds and saturates at 0x80");
    Check(Near(omega::frontend::NormalizeGsColor({255U, 128U, 0U, 64U}),
               RgbaF{1.0F, 128.0F / 255.0F, 0.0F, 0.5F}),
          "GS color normalization preserves straight RGB and coefficient alpha");

    const RgbF destination{0.2F, 0.4F, 0.8F};
    const auto transparent = omega::frontend::BlendSourceOverRgb(
        {.red = 1.0F, .green = 0.25F, .blue = 0.0F, .alpha = 0.0F}, destination);
    const auto opaque = omega::frontend::BlendSourceOverRgb(
        {.red = 1.0F, .green = 0.25F, .blue = 0.0F, .alpha = 1.0F}, destination);
    const auto quarter = omega::frontend::BlendSourceOverRgb(
        {.red = 1.0F, .green = 0.25F, .blue = 0.0F, .alpha = 0.25F}, destination);
    Check(transparent && Near(*transparent, destination),
          "ALPHA_1 source-over retains destination RGB at zero source alpha");
    Check(opaque && Near(*opaque, {1.0F, 0.25F, 0.0F}),
          "ALPHA_1 source-over selects source RGB at full source alpha");
    Check(quarter && Near(*quarter, {0.4F, 0.3625F, 0.6F}),
          "ALPHA_1 source-over evaluates (Cs-Cd)*As+Cd per channel");
    const auto rejected_blend = omega::frontend::BlendSourceOverRgb(
        {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = -0.1F}, destination);
    Check(!rejected_blend && rejected_blend.error() == CompositorMathError::ColorOutOfRange,
          "source-over rejects colors outside the normalized contract");
    const auto rejected_nonfinite_blend = omega::frontend::BlendSourceOverRgb(
        {.red = std::numeric_limits<float>::quiet_NaN(),
         .green = 0.0F,
         .blue = 0.0F,
         .alpha = 1.0F},
        destination);
    Check(!rejected_nonfinite_blend &&
              rejected_nonfinite_blend.error() == CompositorMathError::NonFiniteInput,
          "source-over rejects non-finite color channels");

    if (failures != 0)
    {
        std::cerr << failures << " frontend compositor math test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "frontend compositor math tests passed\n";
    return EXIT_SUCCESS;
}
