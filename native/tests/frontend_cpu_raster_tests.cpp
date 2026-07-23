#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend_presentation/screen_space_triangle_kernel.h"

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace omega::content::detail
{
struct FrontEndScreenBundleTestAccess final
{
    [[nodiscard]] static FrontEndTextureBinding MakeTexture(
        asset::IndexedImageIR image,
        const asset::IndexedImageEncoding sampling_encoding,
        const FrontEndTextureAlphaMode alpha_mode)
    {
        return FrontEndTextureBinding(
            std::move(image), sampling_encoding, alpha_mode);
    }
};
} // namespace omega::content::detail

namespace
{
namespace presentation = omega::frontend::presentation;
using omega::asset::IndexedImageEncoding;
using omega::asset::IndexedImageIR;
using omega::asset::RawGsRgba8;
using omega::content::FrontEndTextureAlphaMode;
using omega::content::FrontEndTextureBinding;
using omega::content::detail::FrontEndScreenBundleTestAccess;
using omega::frontend::RgbaF;
using presentation::RetailFrontEndRasterError;
using presentation::RetailFrontEndRasterLimits;
using presentation::RetailFrontEndRasterResult;
using presentation::RetailFrontEndRasterTriangle;
using presentation::RetailFrontEndRasterVertex;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

void CheckError(const RetailFrontEndRasterResult& result,
    const RetailFrontEndRasterError error,
    const std::string_view message)
{
    Check(!result && result.error() == error, message);
}

[[nodiscard]] FrontEndTextureBinding MakeTexture(
    const FrontEndTextureAlphaMode alpha_mode,
    const RawGsRgba8 texel,
    std::vector<std::uint8_t> indices = {0U})
{
    std::vector<RawGsRgba8> palette(16U);
    palette[0U] = texel;
    return FrontEndScreenBundleTestAccess::MakeTexture(
        IndexedImageIR{
            .width = 1U,
            .height = 1U,
            .source_encoding = IndexedImageEncoding::Indexed4,
            .indices = std::move(indices),
            .palette = std::move(palette),
        },
        IndexedImageEncoding::Indexed4, alpha_mode);
}

[[nodiscard]] RetailFrontEndRasterVertex MakeVertex(const float x,
    const float y,
    const float depth,
    const RgbaF color,
    const float s = 0.5F,
    const float t = 0.5F) noexcept
{
    return RetailFrontEndRasterVertex{
        .x = x,
        .y = y,
        .depth_rank = depth,
        .normalized_st = {.u = s, .v = t},
        .modulation = color,
    };
}

[[nodiscard]] RetailFrontEndRasterTriangle OnePixelTriangle(const float x,
    const float y,
    const float depth,
    const RgbaF color,
    const FrontEndTextureBinding* const texture = nullptr) noexcept
{
    return RetailFrontEndRasterTriangle{
        .vertices = {
            MakeVertex(x, y, depth, color),
            MakeVertex(x + 1.0F, y, depth, color),
            MakeVertex(x, y + 1.0F, depth, color),
        },
        .texture = texture,
    };
}

[[nodiscard]] std::array<std::uint8_t, 4U> Pixel(
    const presentation::OwnedRgba8Frame& frame,
    const std::uint32_t x,
    const std::uint32_t y)
{
    const std::size_t offset =
        (static_cast<std::size_t>(y) * frame.width + x) * 4U;
    return {
        frame.pixels[offset],
        frame.pixels[offset + 1U],
        frame.pixels[offset + 2U],
        frame.pixels[offset + 3U],
    };
}

void TestPublicContractAndClearBridge()
{
    static_assert(std::same_as<decltype(
                                   presentation::RasterizeRetailFrontEndTriangles(
                                       std::declval<std::span<const
                                           RetailFrontEndRasterTriangle>>(),
                                       std::declval<RgbaF>(),
                                       std::declval<RetailFrontEndRasterLimits>())),
        RetailFrontEndRasterResult>);
    static_assert(noexcept(presentation::RasterizeRetailFrontEndTriangles(
        std::declval<std::span<const RetailFrontEndRasterTriangle>>(),
        std::declval<RgbaF>(), std::declval<RetailFrontEndRasterLimits>())));
    static_assert(std::is_enum_v<RetailFrontEndRasterError>);
    static_assert(sizeof(RetailFrontEndRasterError) == 1U);
    static_assert(std::is_trivially_copyable_v<RetailFrontEndRasterVertex>);
    static_assert(std::is_trivially_copyable_v<RetailFrontEndRasterTriangle>);

    const auto frame = presentation::RasterizeRetailFrontEndTriangles(
        {}, RgbaF{0.5F, 0.25F, 1.0F, 0.0F});
    Check(frame && frame->width == 640U && frame->height == 448U &&
            frame->pixels.size() ==
                presentation::kRetailFrontEndRasterOutputBytes,
        "an empty draw produces one complete canonical owned frame");
    if (!frame)
        return;
    Check(Pixel(*frame, 0U, 0U) ==
              std::array<std::uint8_t, 4U>{128U, 64U, 255U, 0U} &&
            Pixel(*frame, 639U, 447U) == Pixel(*frame, 0U, 0U),
        "the final host bridge clamps normalized clear color and rounds halves upward");
}

void TestUntexturedAndTextureAlphaModes()
{
    constexpr RgbaF black{0.0F, 0.0F, 0.0F, 0.0F};
    const auto untextured = OnePixelTriangle(
        2.0F, 3.0F, 1.0F, RgbaF{1.0F, 0.5F, 0.0F, 0.5F});
    const auto untextured_frame =
        presentation::RasterizeRetailFrontEndTriangles(
            std::span<const RetailFrontEndRasterTriangle>(&untextured, 1U),
            black);
    Check(untextured_frame &&
            Pixel(*untextured_frame, 2U, 3U) ==
                std::array<std::uint8_t, 4U>{128U, 64U, 0U, 128U},
        "the untextured path uses identity modulation and straight source-over");

    const auto palette_alpha = MakeTexture(
        FrontEndTextureAlphaMode::UsesPaletteAlpha,
        RawGsRgba8{128U, 64U, 255U, 64U});
    const auto identity_alpha = MakeTexture(
        FrontEndTextureAlphaMode::IgnoresTextureAlpha,
        RawGsRgba8{128U, 64U, 255U, 64U});
    const RgbaF vertex_modulation{0.5F, 1.0F, 0.25F, 0.5F};
    const auto uses = OnePixelTriangle(
        4.0F, 5.0F, 1.0F, vertex_modulation, &palette_alpha);
    const auto ignores = OnePixelTriangle(
        4.0F, 5.0F, 1.0F, vertex_modulation, &identity_alpha);
    const auto uses_frame = presentation::RasterizeRetailFrontEndTriangles(
        std::span<const RetailFrontEndRasterTriangle>(&uses, 1U), black);
    const auto ignores_frame = presentation::RasterizeRetailFrontEndTriangles(
        std::span<const RetailFrontEndRasterTriangle>(&ignores, 1U), black);
    Check(uses_frame && Pixel(*uses_frame, 4U, 5U) ==
                            std::array<std::uint8_t, 4U>{16U, 16U, 16U, 64U},
        "TCC palette mode multiplies bilinear texture RGB and palette alpha");
    Check(ignores_frame &&
            Pixel(*ignores_frame, 4U, 5U) ==
                std::array<std::uint8_t, 4U>{32U, 32U, 32U, 128U},
        "TCC identity mode modulates RGB while preserving vertex alpha");
}

void TestAlphaDepthAndOrderedBlending()
{
    constexpr RgbaF black{0.0F, 0.0F, 0.0F, 0.0F};
    const std::array depth_order{
        OnePixelTriangle(10.0F, 10.0F, 10.0F,
            RgbaF{1.0F, 0.0F, 0.0F, 1.0F}),
        OnePixelTriangle(10.0F, 10.0F, 9.0F,
            RgbaF{0.0F, 1.0F, 0.0F, 1.0F}),
        OnePixelTriangle(10.0F, 10.0F, 11.0F,
            RgbaF{0.0F, 0.0F, 1.0F, 1.0F}),
        OnePixelTriangle(10.0F, 10.0F, 11.0F,
            RgbaF{1.0F, 1.0F, 0.0F, 1.0F}),
    };
    const auto depth_frame = presentation::RasterizeRetailFrontEndTriangles(
        depth_order, black);
    Check(depth_frame && Pixel(*depth_frame, 10U, 10U) ==
                             std::array<std::uint8_t, 4U>{255U, 255U, 0U, 255U},
        "far depth loses, nearer depth wins, and later equal depth wins");

    const std::array alpha_then_far{
        OnePixelTriangle(11.0F, 11.0F, 100.0F,
            RgbaF{0.0F, 1.0F, 0.0F, 0.0F}),
        OnePixelTriangle(11.0F, 11.0F, -100.0F,
            RgbaF{1.0F, 0.0F, 0.0F, 1.0F}),
    };
    const auto alpha_frame = presentation::RasterizeRetailFrontEndTriangles(
        alpha_then_far, black);
    Check(alpha_frame && Pixel(*alpha_frame, 11U, 11U) ==
                             std::array<std::uint8_t, 4U>{255U, 0U, 0U, 255U},
        "strict zero-alpha rejection writes neither depth nor color");

    const std::array blended{
        OnePixelTriangle(12.0F, 12.0F, 5.0F,
            RgbaF{1.0F, 0.0F, 0.0F, 0.5F}),
        OnePixelTriangle(12.0F, 12.0F, 5.0F,
            RgbaF{0.0F, 1.0F, 0.0F, 0.25F}),
    };
    const auto blend_frame = presentation::RasterizeRetailFrontEndTriangles(
        blended, RgbaF{0.0F, 0.0F, 1.0F, 0.75F});
    Check(blend_frame && Pixel(*blend_frame, 12U, 12U) ==
                             std::array<std::uint8_t, 4U>{96U, 64U, 96U, 64U},
        "ordered source-over retains float RGB while output alpha becomes the latest source alpha");
}

void TestClippingSharedEdgesAndOrder()
{
    constexpr RgbaF clear{0.0F, 0.0F, 0.0F, 1.0F};
    constexpr RgbaF red{1.0F, 0.0F, 0.0F, 1.0F};
    constexpr RgbaF green{0.0F, 1.0F, 0.0F, 1.0F};
    const RetailFrontEndRasterTriangle upper_right{
        .vertices = {
            MakeVertex(0.0F, 0.0F, 1.0F, red),
            MakeVertex(2.0F, 0.0F, 1.0F, red),
            MakeVertex(2.0F, 2.0F, 1.0F, red),
        },
    };
    const RetailFrontEndRasterTriangle lower_left{
        .vertices = {
            MakeVertex(0.0F, 0.0F, 1.0F, green),
            MakeVertex(2.0F, 2.0F, 1.0F, green),
            MakeVertex(0.0F, 2.0F, 1.0F, green),
        },
    };
    const std::array square{upper_right, lower_left};
    const auto square_frame =
        presentation::RasterizeRetailFrontEndTriangles(square, clear);
    Check(square_frame &&
            Pixel(*square_frame, 0U, 0U) ==
                std::array<std::uint8_t, 4U>{255U, 0U, 0U, 255U} &&
            Pixel(*square_frame, 1U, 0U) == Pixel(*square_frame, 0U, 0U) &&
            Pixel(*square_frame, 1U, 1U) == Pixel(*square_frame, 0U, 0U) &&
            Pixel(*square_frame, 0U, 1U) ==
                std::array<std::uint8_t, 4U>{0U, 255U, 0U, 255U},
        "half-open shared edges cover every square sample exactly once");

    const auto clipped = OnePixelTriangle(
        -1.0F, -1.0F, 1.0F, RgbaF{1.0F, 1.0F, 1.0F, 1.0F});
    const auto clipped_frame = presentation::RasterizeRetailFrontEndTriangles(
        std::span<const RetailFrontEndRasterTriangle>(&clipped, 1U), clear);
    Check(clipped_frame && Pixel(*clipped_frame, 0U, 0U) ==
                               std::array<std::uint8_t, 4U>{0U, 0U, 0U, 255U},
        "samples outside the canonical half-open clip cannot alter the frame");

    const std::array equal_depth_order{
        OnePixelTriangle(3.0F, 3.0F, 1.0F, red),
        OnePixelTriangle(3.0F, 3.0F, 1.0F, green),
    };
    const auto ordered = presentation::RasterizeRetailFrontEndTriangles(
        equal_depth_order, clear);
    Check(ordered && Pixel(*ordered, 3U, 3U) ==
                         std::array<std::uint8_t, 4U>{0U, 255U, 0U, 255U},
        "triangle span order is retained through equal-depth rendering");
}

void TestMalformedFiniteAndLimits()
{
    constexpr RgbaF clear{0.0F, 0.0F, 0.0F, 0.0F};
    const auto ordinary = OnePixelTriangle(
        0.0F, 0.0F, 0.0F, RgbaF{1.0F, 1.0F, 1.0F, 1.0F});
    const auto ordinary_span =
        std::span<const RetailFrontEndRasterTriangle>(&ordinary, 1U);

    CheckError(presentation::RasterizeRetailFrontEndTriangles(ordinary_span,
                   clear, RetailFrontEndRasterLimits{.maximum_triangles = 0U}),
        RetailFrontEndRasterError::InvalidLimits,
        "a zero limit is invalid rather than unbounded");
    CheckError(presentation::RasterizeRetailFrontEndTriangles(ordinary_span,
                   clear,
                   RetailFrontEndRasterLimits{
                       .maximum_output_bytes =
                           presentation::kRetailFrontEndRasterOutputBytes - 1U}),
        RetailFrontEndRasterError::LimitExceeded,
        "the caller can tighten the fixed output-byte budget");
    CheckError(presentation::RasterizeRetailFrontEndTriangles(ordinary_span,
                   clear,
                   RetailFrontEndRasterLimits{
                       .maximum_scratch_bytes =
                           presentation::kRetailFrontEndRasterScratchBytes - 1U}),
        RetailFrontEndRasterError::LimitExceeded,
        "the caller can tighten the fixed scratch-byte budget");

    const std::array two_triangles{ordinary, ordinary};
    CheckError(presentation::RasterizeRetailFrontEndTriangles(two_triangles,
                   clear,
                   RetailFrontEndRasterLimits{.maximum_triangles = 1U}),
        RetailFrontEndRasterError::LimitExceeded,
        "triangle count is checked before rendering");
    CheckError(presentation::RasterizeRetailFrontEndTriangles(two_triangles,
                   clear,
                   RetailFrontEndRasterLimits{.maximum_covered_samples = 1U}),
        RetailFrontEndRasterError::LimitExceeded,
        "aggregate covered samples are bounded across ordered triangles");

    auto nonfinite = ordinary;
    nonfinite.vertices[0U].depth_rank =
        std::numeric_limits<float>::infinity();
    CheckError(presentation::RasterizeRetailFrontEndTriangles(
                   std::span<const RetailFrontEndRasterTriangle>(&nonfinite, 1U),
                   clear),
        RetailFrontEndRasterError::NonFiniteInput,
        "nonfinite geometry attributes fail before allocation");
    CheckError(presentation::RasterizeRetailFrontEndTriangles(ordinary_span,
                   RgbaF{0.0F, 0.0F,
                       std::numeric_limits<float>::quiet_NaN(), 0.0F}),
        RetailFrontEndRasterError::NonFiniteInput,
        "a nonfinite clear channel is categorical");

    auto bad_color = ordinary;
    bad_color.vertices[2U].modulation.red = 1.01F;
    CheckError(presentation::RasterizeRetailFrontEndTriangles(
                   std::span<const RetailFrontEndRasterTriangle>(&bad_color, 1U),
                   clear),
        RetailFrontEndRasterError::ColorOutOfRange,
        "vertex modulation must be normalized");
    CheckError(presentation::RasterizeRetailFrontEndTriangles(
                   ordinary_span, RgbaF{-0.01F, 0.0F, 0.0F, 0.0F}),
        RetailFrontEndRasterError::ColorOutOfRange,
        "clear modulation must be normalized");

    auto degenerate = ordinary;
    degenerate.vertices[2U] = degenerate.vertices[1U];
    CheckError(presentation::RasterizeRetailFrontEndTriangles(
                   std::span<const RetailFrontEndRasterTriangle>(&degenerate, 1U),
                   clear),
        RetailFrontEndRasterError::DegenerateTriangle,
        "degenerate projected geometry is distinct from nonfinite input");

    auto excessive_coordinate = ordinary;
    excessive_coordinate.vertices[0U].x =
        presentation::kScreenSpaceTriangleMaximumCoordinateMagnitude + 1.0F;
    CheckError(presentation::RasterizeRetailFrontEndTriangles(
                   std::span<const RetailFrontEndRasterTriangle>(
                       &excessive_coordinate, 1U),
                   clear),
        RetailFrontEndRasterError::LimitExceeded,
        "the underlying coordinate ceiling remains unraiseable");

    const auto malformed_texture = MakeTexture(
        FrontEndTextureAlphaMode::UsesPaletteAlpha,
        RawGsRgba8{255U, 255U, 255U, 128U}, {});
    const auto malformed_textured = OnePixelTriangle(0.0F, 0.0F, 0.0F,
        RgbaF{1.0F, 1.0F, 1.0F, 1.0F}, &malformed_texture);
    CheckError(presentation::RasterizeRetailFrontEndTriangles(
                   std::span<const RetailFrontEndRasterTriangle>(
                       &malformed_textured, 1U),
                   clear),
        RetailFrontEndRasterError::TextureSamplingFailure,
        "malformed borrowed texture storage returns one identity-free category");
}

void TestDeterminismOwnershipAndPrivacy()
{
    const auto owned_after_inputs = []() -> RetailFrontEndRasterResult {
        auto texture = MakeTexture(FrontEndTextureAlphaMode::UsesPaletteAlpha,
            RawGsRgba8{17U, 31U, 63U, 96U});
        const auto triangle = OnePixelTriangle(7.0F, 9.0F, 2.0F,
            RgbaF{0.75F, 0.5F, 0.25F, 0.75F}, &texture);
        return presentation::RasterizeRetailFrontEndTriangles(
            std::span<const RetailFrontEndRasterTriangle>(&triangle, 1U),
            RgbaF{0.1F, 0.2F, 0.3F, 0.4F});
    }();
    Check(owned_after_inputs &&
            owned_after_inputs->pixels.size() ==
                presentation::kRetailFrontEndRasterOutputBytes,
        "the returned frame survives destruction of every borrowed input");
    if (!owned_after_inputs)
        return;

    for (std::uint32_t iteration = 0U; iteration < 8U; ++iteration)
    {
        const auto repeated = []() -> RetailFrontEndRasterResult {
            auto texture = MakeTexture(
                FrontEndTextureAlphaMode::UsesPaletteAlpha,
                RawGsRgba8{17U, 31U, 63U, 96U});
            const auto triangle = OnePixelTriangle(7.0F, 9.0F, 2.0F,
                RgbaF{0.75F, 0.5F, 0.25F, 0.75F}, &texture);
            return presentation::RasterizeRetailFrontEndTriangles(
                std::span<const RetailFrontEndRasterTriangle>(&triangle, 1U),
                RgbaF{0.1F, 0.2F, 0.3F, 0.4F});
        }();
        Check(repeated == owned_after_inputs,
            "repeated generated renders are byte-identical");
    }

    const auto first_bad = MakeTexture(
        FrontEndTextureAlphaMode::UsesPaletteAlpha, RawGsRgba8{}, {});
    const auto second_bad = MakeTexture(
        FrontEndTextureAlphaMode::UsesPaletteAlpha,
        RawGsRgba8{255U, 17U, 31U, 128U}, {0U, 0U});
    const auto first_triangle = OnePixelTriangle(0.0F, 0.0F, 0.0F,
        RgbaF{1.0F, 1.0F, 1.0F, 1.0F}, &first_bad);
    const auto second_triangle = OnePixelTriangle(0.0F, 0.0F, 0.0F,
        RgbaF{1.0F, 1.0F, 1.0F, 1.0F}, &second_bad);
    const auto first_error = presentation::RasterizeRetailFrontEndTriangles(
        std::span<const RetailFrontEndRasterTriangle>(&first_triangle, 1U),
        RgbaF{});
    const auto second_error = presentation::RasterizeRetailFrontEndTriangles(
        std::span<const RetailFrontEndRasterTriangle>(&second_triangle, 1U),
        RgbaF{});
    Check(!first_error && !second_error &&
            first_error.error() == second_error.error() &&
            first_error.error() ==
                RetailFrontEndRasterError::TextureSamplingFailure,
        "different malformed payload identities collapse to the same safe error");
}
} // namespace

int main()
{
    TestPublicContractAndClearBridge();
    TestUntexturedAndTextureAlphaModes();
    TestAlphaDepthAndOrderedBlending();
    TestClippingSharedEdgesAndOrder();
    TestMalformedFiniteAndLimits();
    TestDeterminismOwnershipAndPrivacy();

    if (failures != 0)
    {
        std::cerr << failures << " frontend CPU raster test(s) failed\n";
        return 1;
    }
    std::cout << "frontend CPU raster tests passed\n";
    return 0;
}
