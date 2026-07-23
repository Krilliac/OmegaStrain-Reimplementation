#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include "omega/content/front_end_screen_bundle.h"
#include "omega/frontend_presentation/retail_frontend_texture_sampler.h"
#include "omega/frontend_presentation/screen_space_triangle_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <new>
#include <utility>
#include <vector>

namespace omega::frontend::presentation
{
namespace
{
inline constexpr std::size_t kDepthChannel = 0U;
inline constexpr std::size_t kRedChannel = 1U;
inline constexpr std::size_t kGreenChannel = 2U;
inline constexpr std::size_t kBlueChannel = 3U;
inline constexpr std::size_t kAlphaChannel = 4U;
inline constexpr std::size_t kRasterAffineChannelCount = 5U;

struct RasterScratch final
{
    std::vector<RgbaF> colors;
    std::vector<float> depths;
};

struct TriangleVisitorContext final
{
    RasterScratch* scratch = nullptr;
    const content::FrontEndTextureBinding* texture = nullptr;
    std::uint64_t covered_before_triangle = 0U;
    std::uint64_t maximum_covered_samples = 0U;
    std::uint64_t visited_samples = 0U;
    RetailFrontEndRasterError error =
        RetailFrontEndRasterError::RasterizationFailure;
    bool failed = false;
};

[[nodiscard]] bool IsFinite(const RgbaF& color) noexcept
{
    return std::isfinite(color.red) && std::isfinite(color.green) &&
           std::isfinite(color.blue) && std::isfinite(color.alpha);
}

[[nodiscard]] bool IsNormalized(const RgbaF& color) noexcept
{
    return color.red >= 0.0F && color.red <= 1.0F &&
           color.green >= 0.0F && color.green <= 1.0F &&
           color.blue >= 0.0F && color.blue <= 1.0F &&
           color.alpha >= 0.0F && color.alpha <= 1.0F;
}

[[nodiscard]] bool IsValidLimits(const RetailFrontEndRasterLimits& limits) noexcept
{
    return limits.maximum_triangles != 0U &&
           limits.maximum_triangles <= kRetailFrontEndRasterMaximumTriangles &&
           limits.maximum_covered_samples != 0U &&
           limits.maximum_covered_samples <=
               kRetailFrontEndRasterMaximumCoveredSamples &&
           limits.maximum_output_bytes != 0U &&
           limits.maximum_output_bytes <= kRetailFrontEndRasterOutputBytes &&
           limits.maximum_scratch_bytes != 0U &&
           limits.maximum_scratch_bytes <= kRetailFrontEndRasterScratchBytes;
}

[[nodiscard]] RetailFrontEndRasterError MapTextureError(
    const RetailFrontEndTextureSamplingError) noexcept
{
    return RetailFrontEndRasterError::TextureSamplingFailure;
}

[[nodiscard]] RetailFrontEndRasterError MapTriangleError(
    const ScreenSpaceTriangleError error) noexcept
{
    switch (error)
    {
    case ScreenSpaceTriangleError::LimitExceeded:
        return RetailFrontEndRasterError::LimitExceeded;
    case ScreenSpaceTriangleError::NonFiniteInput:
        return RetailFrontEndRasterError::NonFiniteInput;
    case ScreenSpaceTriangleError::DegenerateTriangle:
        return RetailFrontEndRasterError::DegenerateTriangle;
    case ScreenSpaceTriangleError::ArithmeticFailure:
    case ScreenSpaceTriangleError::PerspectiveDivideFailure:
        return RetailFrontEndRasterError::ArithmeticFailure;
    case ScreenSpaceTriangleError::InvalidLimits:
    case ScreenSpaceTriangleError::InvalidClip:
    case ScreenSpaceTriangleError::InconsistentAffineChannelCount:
    case ScreenSpaceTriangleError::MissingVisitor:
    case ScreenSpaceTriangleError::VisitorStopped:
    case ScreenSpaceTriangleError::InvalidVisitorResult:
        return RetailFrontEndRasterError::RasterizationFailure;
    }
    return RetailFrontEndRasterError::RasterizationFailure;
}

[[nodiscard]] std::expected<void, RetailFrontEndRasterError> ValidateInputs(
    const std::span<const RetailFrontEndRasterTriangle> triangles,
    const RgbaF clear_color,
    const RetailFrontEndRasterLimits& limits) noexcept
{
    if (!IsValidLimits(limits))
        return std::unexpected(RetailFrontEndRasterError::InvalidLimits);
    if (triangles.size() > limits.maximum_triangles)
        return std::unexpected(RetailFrontEndRasterError::LimitExceeded);
    if (limits.maximum_output_bytes < kRetailFrontEndRasterOutputBytes ||
        limits.maximum_scratch_bytes < kRetailFrontEndRasterScratchBytes)
    {
        return std::unexpected(RetailFrontEndRasterError::LimitExceeded);
    }
    if (!IsFinite(clear_color))
        return std::unexpected(RetailFrontEndRasterError::NonFiniteInput);
    if (!IsNormalized(clear_color))
        return std::unexpected(RetailFrontEndRasterError::ColorOutOfRange);

    for (const auto& triangle : triangles)
    {
        for (const auto& vertex : triangle.vertices)
        {
            if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
                !std::isfinite(vertex.depth_rank) ||
                !std::isfinite(vertex.normalized_st.u) ||
                !std::isfinite(vertex.normalized_st.v) ||
                !IsFinite(vertex.modulation))
            {
                return std::unexpected(
                    RetailFrontEndRasterError::NonFiniteInput);
            }
            if (!IsNormalized(vertex.modulation))
            {
                return std::unexpected(
                    RetailFrontEndRasterError::ColorOutOfRange);
            }
        }
        if (triangle.texture != nullptr)
        {
            const auto validated =
                LookupRetailFrontEndTexel(*triangle.texture, 0U, 0U);
            if (!validated)
                return std::unexpected(MapTextureError(validated.error()));
        }
    }
    return {};
}

[[nodiscard]] std::size_t PixelIndex(
    const std::int32_t x, const std::int32_t y) noexcept
{
    return static_cast<std::size_t>(y) *
               static_cast<std::size_t>(kCanonicalRasterWidth) +
           static_cast<std::size_t>(x);
}

[[nodiscard]] float ClampUnit(const float value) noexcept
{
    return std::clamp(value, 0.0F, 1.0F);
}

[[nodiscard]] RgbaF ClampUnit(const RgbaF color) noexcept
{
    return RgbaF{
        .red = ClampUnit(color.red),
        .green = ClampUnit(color.green),
        .blue = ClampUnit(color.blue),
        .alpha = ClampUnit(color.alpha),
    };
}

[[nodiscard]] std::expected<RgbaF, RetailFrontEndRasterError> ResolveSource(
    const ScreenSpaceTriangleSample& sample,
    const content::FrontEndTextureBinding* const texture) noexcept
{
    const auto channels = sample.AffineChannels();
    if (channels.size() != kRasterAffineChannelCount)
        return std::unexpected(RetailFrontEndRasterError::RasterizationFailure);

    RgbaF source = ClampUnit(RgbaF{
        .red = channels[kRedChannel],
        .green = channels[kGreenChannel],
        .blue = channels[kBlueChannel],
        .alpha = channels[kAlphaChannel],
    });
    if (texture == nullptr)
        return source;

    const auto texture_sample = SampleRetailFrontEndTextureBilinearRepeat(
        *texture, {.u = sample.s_over_q, .v = sample.t_over_q});
    if (!texture_sample)
        return std::unexpected(MapTextureError(texture_sample.error()));

    source.red *= texture_sample->modulation.red;
    source.green *= texture_sample->modulation.green;
    source.blue *= texture_sample->modulation.blue;
    switch (texture_sample->alpha_contribution)
    {
    case RetailFrontEndTextureAlphaContribution::Identity:
        break;
    case RetailFrontEndTextureAlphaContribution::Palette:
        source.alpha *= texture_sample->modulation.alpha;
        break;
    default:
        return std::unexpected(RetailFrontEndRasterError::TextureSamplingFailure);
    }
    if (!IsFinite(source))
        return std::unexpected(RetailFrontEndRasterError::ArithmeticFailure);
    return ClampUnit(source);
}

[[nodiscard]] ScreenSpaceTriangleVisitControl VisitTriangleSample(
    void* const opaque_context,
    const ScreenSpaceTriangleSample& sample) noexcept
{
    auto& context = *static_cast<TriangleVisitorContext*>(opaque_context);
    if (context.covered_before_triangle + context.visited_samples >=
        context.maximum_covered_samples)
    {
        context.error = RetailFrontEndRasterError::LimitExceeded;
        context.failed = true;
        return ScreenSpaceTriangleVisitControl::Stop;
    }

    const auto source = ResolveSource(sample, context.texture);
    if (!source)
    {
        context.error = source.error();
        context.failed = true;
        return ScreenSpaceTriangleVisitControl::Stop;
    }
    ++context.visited_samples;

    // The established TEST ordering rejects zero alpha before depth compare or
    // either framebuffer write.
    if (!(source->alpha > 0.0F))
        return ScreenSpaceTriangleVisitControl::Continue;

    const auto channels = sample.AffineChannels();
    const float depth = channels[kDepthChannel];
    const std::size_t pixel_index = PixelIndex(sample.x, sample.y);
    if (depth < context.scratch->depths[pixel_index])
        return ScreenSpaceTriangleVisitControl::Continue;

    const auto& destination = context.scratch->colors[pixel_index];
    const auto blended = BlendSourceOverRgb(
        *source,
        RgbF{destination.red, destination.green, destination.blue});
    if (!blended)
    {
        context.error = RetailFrontEndRasterError::ArithmeticFailure;
        context.failed = true;
        return ScreenSpaceTriangleVisitControl::Stop;
    }

    context.scratch->colors[pixel_index] = RgbaF{
        .red = blended->red,
        .green = blended->green,
        .blue = blended->blue,
        .alpha = source->alpha,
    };
    context.scratch->depths[pixel_index] = depth;
    return ScreenSpaceTriangleVisitControl::Continue;
}

[[nodiscard]] std::uint8_t ToHostByte(const float value) noexcept
{
    const double scaled = static_cast<double>(ClampUnit(value)) * 255.0;
    return static_cast<std::uint8_t>(std::floor(scaled + 0.5));
}

[[nodiscard]] std::expected<RasterScratch, RetailFrontEndRasterError>
AllocateScratch(const RgbaF clear_color) noexcept
{
    constexpr std::size_t pixel_count =
        static_cast<std::size_t>(kCanonicalRasterWidth) *
        static_cast<std::size_t>(kCanonicalRasterHeight);
    try
    {
        RasterScratch scratch;
        scratch.colors.assign(pixel_count, clear_color);
        scratch.depths.assign(pixel_count,
            -std::numeric_limits<float>::infinity());
        return scratch;
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailFrontEndRasterError::AllocationFailure);
    }
}

[[nodiscard]] std::expected<OwnedRgba8Frame, RetailFrontEndRasterError>
BuildOwnedFrame(const RasterScratch& scratch) noexcept
{
    OwnedRgba8Frame frame;
    try
    {
        frame.pixels.resize(
            static_cast<std::size_t>(kRetailFrontEndRasterOutputBytes));
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(RetailFrontEndRasterError::AllocationFailure);
    }

    for (std::size_t pixel_index = 0U;
         pixel_index < scratch.colors.size(); ++pixel_index)
    {
        const std::size_t byte_offset = pixel_index * 4U;
        const RgbaF color = ClampUnit(scratch.colors[pixel_index]);
        frame.pixels[byte_offset] = ToHostByte(color.red);
        frame.pixels[byte_offset + 1U] = ToHostByte(color.green);
        frame.pixels[byte_offset + 2U] = ToHostByte(color.blue);
        frame.pixels[byte_offset + 3U] = ToHostByte(color.alpha);
    }
    return frame;
}
} // namespace

RetailFrontEndRasterResult RasterizeRetailFrontEndTriangles(
    const std::span<const RetailFrontEndRasterTriangle> triangles,
    const RgbaF clear_color,
    const RetailFrontEndRasterLimits limits) noexcept
{
    const auto valid = ValidateInputs(triangles, clear_color, limits);
    if (!valid)
        return std::unexpected(valid.error());

    auto scratch = AllocateScratch(clear_color);
    if (!scratch)
        return std::unexpected(scratch.error());

    constexpr ScreenSpaceClipRect clip{
        .left = 0,
        .top = 0,
        .right = static_cast<std::int32_t>(kCanonicalRasterWidth),
        .bottom = static_cast<std::int32_t>(kCanonicalRasterHeight),
    };
    std::uint64_t covered_samples = 0U;
    for (const auto& triangle : triangles)
    {
        std::array<std::array<float, kRasterAffineChannelCount>, 3U> channels{};
        std::array<ScreenSpaceTriangleVertex, 3U> kernel_vertices{};
        for (std::size_t vertex_index = 0U;
             vertex_index < triangle.vertices.size(); ++vertex_index)
        {
            const auto& input = triangle.vertices[vertex_index];
            channels[vertex_index] = {
                input.depth_rank,
                input.modulation.red,
                input.modulation.green,
                input.modulation.blue,
                input.modulation.alpha,
            };
            kernel_vertices[vertex_index] = ScreenSpaceTriangleVertex{
                .x = input.x,
                .y = input.y,
                .affine_channels = channels[vertex_index],
                .s = input.normalized_st.u,
                .t = input.normalized_st.v,
                .q = 1.0F,
            };
        }

        const std::uint64_t remaining =
            limits.maximum_covered_samples - covered_samples;
        const std::uint64_t kernel_budget = std::min(
            std::max(remaining, std::uint64_t{1U}),
            kScreenSpaceTriangleMaximumCoveredPixels);
        TriangleVisitorContext context{
            .scratch = &*scratch,
            .texture = triangle.texture,
            .covered_before_triangle = covered_samples,
            .maximum_covered_samples = limits.maximum_covered_samples,
        };
        const auto rendered = RasterizeScreenSpaceTriangle(
            kernel_vertices, clip, VisitTriangleSample, &context,
            ScreenSpaceTriangleLimits{
                .maximum_affine_channels =
                    static_cast<std::uint32_t>(kRasterAffineChannelCount),
                .maximum_clip_width = kCanonicalRasterWidth,
                .maximum_clip_height = kCanonicalRasterHeight,
                .maximum_covered_pixels = kernel_budget,
            });
        if (!rendered)
        {
            if (context.failed)
                return std::unexpected(context.error);
            return std::unexpected(MapTriangleError(rendered.error()));
        }
        if (context.failed ||
            context.visited_samples != rendered->covered_pixel_count)
        {
            return std::unexpected(
                RetailFrontEndRasterError::RasterizationFailure);
        }
        covered_samples += rendered->covered_pixel_count;
    }

    return BuildOwnedFrame(*scratch);
}
} // namespace omega::frontend::presentation
