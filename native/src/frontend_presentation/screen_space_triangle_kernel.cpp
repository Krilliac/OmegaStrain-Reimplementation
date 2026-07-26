#include "omega/frontend_presentation/screen_space_triangle_kernel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

namespace omega::frontend::presentation
{
namespace
{
struct OwnedVertex final
{
    double x = 0.0;
    double y = 0.0;
    std::array<double, kScreenSpaceTriangleMaximumAffineChannels>
        affine_channels{};
    double s = 0.0;
    double t = 0.0;
    double q = 1.0;
};

struct OwnedTriangle final
{
    std::array<OwnedVertex, 3U> vertices;
    std::uint32_t affine_channel_count = 0U;
    double signed_twice_area = 0.0;
};

[[nodiscard]] bool IsValidLimits(
    const ScreenSpaceTriangleLimits& limits) noexcept
{
    return limits.maximum_affine_channels <=
               kScreenSpaceTriangleMaximumAffineChannels &&
           limits.maximum_clip_width != 0U &&
           limits.maximum_clip_width <= kScreenSpaceTriangleMaximumClipWidth &&
           limits.maximum_clip_height != 0U &&
           limits.maximum_clip_height <=
               kScreenSpaceTriangleMaximumClipHeight &&
           limits.maximum_covered_pixels != 0U &&
           limits.maximum_covered_pixels <=
               kScreenSpaceTriangleMaximumCoveredPixels &&
           std::isfinite(limits.maximum_coordinate_magnitude) &&
           limits.maximum_coordinate_magnitude > 0.0F &&
           limits.maximum_coordinate_magnitude <=
               kScreenSpaceTriangleMaximumCoordinateMagnitude;
}

[[nodiscard]] double Edge(const double ax, const double ay, const double bx,
    const double by, const double px, const double py) noexcept
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

[[nodiscard]] std::expected<OwnedTriangle, ScreenSpaceTriangleError>
SnapshotTriangle(
    const std::span<const ScreenSpaceTriangleVertex, 3U> vertices,
    const ScreenSpaceTriangleLimits& limits) noexcept
{
    const std::size_t channel_count = vertices.front().affine_channels.size();
    if (channel_count > limits.maximum_affine_channels)
        return std::unexpected(ScreenSpaceTriangleError::LimitExceeded);

    OwnedTriangle triangle;
    triangle.affine_channel_count = static_cast<std::uint32_t>(channel_count);
    const double coordinate_limit =
        static_cast<double>(limits.maximum_coordinate_magnitude);
    for (std::size_t vertex_index = 0U; vertex_index < vertices.size();
         ++vertex_index)
    {
        const auto& input = vertices[vertex_index];
        if (input.affine_channels.size() != channel_count)
        {
            return std::unexpected(
                ScreenSpaceTriangleError::InconsistentAffineChannelCount);
        }
        if (!std::isfinite(input.x) || !std::isfinite(input.y) ||
            !std::isfinite(input.s) || !std::isfinite(input.t) ||
            !std::isfinite(input.q))
        {
            return std::unexpected(ScreenSpaceTriangleError::NonFiniteInput);
        }
        if (std::abs(static_cast<double>(input.x)) > coordinate_limit ||
            std::abs(static_cast<double>(input.y)) > coordinate_limit)
        {
            return std::unexpected(ScreenSpaceTriangleError::LimitExceeded);
        }

        auto& output = triangle.vertices[vertex_index];
        output.x = static_cast<double>(input.x);
        output.y = static_cast<double>(input.y);
        output.s = static_cast<double>(input.s);
        output.t = static_cast<double>(input.t);
        output.q = static_cast<double>(input.q);
        for (std::size_t channel_index = 0U;
             channel_index < channel_count; ++channel_index)
        {
            const float channel = input.affine_channels[channel_index];
            if (!std::isfinite(channel))
            {
                return std::unexpected(
                    ScreenSpaceTriangleError::NonFiniteInput);
            }
            output.affine_channels[channel_index] =
                static_cast<double>(channel);
        }
    }

    const auto& first = triangle.vertices[0U];
    const auto& second = triangle.vertices[1U];
    const auto& third = triangle.vertices[2U];
    triangle.signed_twice_area = Edge(first.x, first.y, second.x, second.y,
        third.x, third.y);
    if (!std::isfinite(triangle.signed_twice_area))
        return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
    if (triangle.signed_twice_area == 0.0)
        return std::unexpected(ScreenSpaceTriangleError::DegenerateTriangle);
    return triangle;
}

[[nodiscard]] bool TryNarrowFinite(const double value, float& output) noexcept
{
    constexpr double maximum =
        static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(value) || value < -maximum || value > maximum)
        return false;
    output = static_cast<float>(value);
    return std::isfinite(output);
}

struct SampleWeights final
{
    double first = 0.0;
    double second = 0.0;
    double third = 0.0;
};

// Shared by the per-pixel sample build and by the coverage proof so both read
// exactly the same arithmetic. Splitting it out changes no expression, operand
// order, or rounding.
[[nodiscard]] SampleWeights ComputeWeights(const OwnedTriangle& triangle,
    const double sample_x, const double sample_y) noexcept
{
    const auto& first = triangle.vertices[0U];
    const auto& second = triangle.vertices[1U];
    const auto& third = triangle.vertices[2U];
    const double inverse_area = 1.0 / triangle.signed_twice_area;
    return SampleWeights{
        .first = Edge(second.x, second.y, third.x, third.y, sample_x,
                     sample_y) *
                 inverse_area,
        .second = Edge(third.x, third.y, first.x, first.y, sample_x,
                      sample_y) *
                  inverse_area,
        .third = Edge(first.x, first.y, second.x, second.y, sample_x,
                     sample_y) *
                 inverse_area,
    };
}

[[nodiscard]] std::expected<ScreenSpaceTriangleSample,
    ScreenSpaceTriangleError>
BuildSample(const OwnedTriangle& triangle, const std::int32_t x,
    const std::int32_t y) noexcept
{
    const double sample_x = static_cast<double>(x);
    const double sample_y = static_cast<double>(y);
    const auto& first = triangle.vertices[0U];
    const auto& second = triangle.vertices[1U];
    const auto& third = triangle.vertices[2U];
    const SampleWeights weights = ComputeWeights(triangle, sample_x, sample_y);
    const double first_weight = weights.first;
    const double second_weight = weights.second;
    const double third_weight = weights.third;
    if (!std::isfinite(first_weight) || !std::isfinite(second_weight) ||
        !std::isfinite(third_weight))
    {
        return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
    }

    const auto interpolate = [&](const double first_value,
                                 const double second_value,
                                 const double third_value) noexcept {
        return first_weight * first_value + second_weight * second_value +
               third_weight * third_value;
    };

    ScreenSpaceTriangleSample sample;
    sample.x = x;
    sample.y = y;
    sample.affine_channel_count = triangle.affine_channel_count;
    for (std::uint32_t channel_index = 0U;
         channel_index < triangle.affine_channel_count; ++channel_index)
    {
        const double value = interpolate(
            first.affine_channels[channel_index],
            second.affine_channels[channel_index],
            third.affine_channels[channel_index]);
        if (!TryNarrowFinite(value, sample.affine_channels[channel_index]))
            return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
    }

    const double interpolated_s = interpolate(first.s, second.s, third.s);
    const double interpolated_t = interpolate(first.t, second.t, third.t);
    const double interpolated_q = interpolate(first.q, second.q, third.q);
    if (!TryNarrowFinite(interpolated_s, sample.s) ||
        !TryNarrowFinite(interpolated_t, sample.t) ||
        !TryNarrowFinite(interpolated_q, sample.q))
    {
        return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
    }
    if (interpolated_q == 0.0)
    {
        return std::unexpected(
            ScreenSpaceTriangleError::PerspectiveDivideFailure);
    }
    if (!TryNarrowFinite(interpolated_s / interpolated_q, sample.s_over_q) ||
        !TryNarrowFinite(interpolated_t / interpolated_q, sample.t_over_q))
    {
        return std::unexpected(
            ScreenSpaceTriangleError::PerspectiveDivideFailure);
    }
    return sample;
}

struct ScanRows final
{
    std::int64_t begin = 0;
    std::int64_t end = 0;
};

struct ScanSpan final
{
    std::int64_t begin = 0;
    std::int64_t end = 0;
};

[[nodiscard]] ScanRows ResolveScanRows(const OwnedTriangle& triangle,
    const ScreenSpaceClipRect clip) noexcept
{
    const double minimum_y = std::min(
        {triangle.vertices[0U].y, triangle.vertices[1U].y,
            triangle.vertices[2U].y});
    const double maximum_y = std::max(
        {triangle.vertices[0U].y, triangle.vertices[1U].y,
            triangle.vertices[2U].y});
    const std::int64_t raw_y_begin =
        static_cast<std::int64_t>(std::ceil(minimum_y));
    const std::int64_t raw_y_end =
        static_cast<std::int64_t>(std::ceil(maximum_y));
    return ScanRows{
        .begin = std::max(raw_y_begin, static_cast<std::int64_t>(clip.top)),
        .end = std::min(raw_y_end, static_cast<std::int64_t>(clip.bottom)),
    };
}

[[nodiscard]] std::expected<ScanSpan, ScreenSpaceTriangleError>
ResolveScanSpan(const OwnedTriangle& triangle, const ScreenSpaceClipRect clip,
    const std::int64_t scan_y) noexcept
{
    const double sample_y = static_cast<double>(scan_y);
    std::array<double, 2U> intersections{};
    std::size_t intersection_count = 0U;
    for (std::size_t edge_index = 0U; edge_index < 3U; ++edge_index)
    {
        const auto& start = triangle.vertices[edge_index];
        const auto& end = triangle.vertices[(edge_index + 1U) % 3U];
        if (start.y == end.y)
            continue;
        const double edge_minimum_y = std::min(start.y, end.y);
        const double edge_maximum_y = std::max(start.y, end.y);
        if (sample_y < edge_minimum_y || sample_y >= edge_maximum_y)
            continue;
        if (intersection_count >= intersections.size())
            return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
        const double interpolation = (sample_y - start.y) / (end.y - start.y);
        const double intersection = start.x + interpolation * (end.x - start.x);
        if (!std::isfinite(intersection))
            return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
        intersections[intersection_count++] = intersection;
    }
    if (intersection_count != 2U)
        return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);

    const double left = std::min(intersections[0U], intersections[1U]);
    const double right = std::max(intersections[0U], intersections[1U]);
    const std::int64_t raw_x_begin = static_cast<std::int64_t>(std::ceil(left));
    const std::int64_t raw_x_end = static_cast<std::int64_t>(std::ceil(right));
    return ScanSpan{
        .begin = std::max(raw_x_begin, static_cast<std::int64_t>(clip.left)),
        .end = std::min(raw_x_end, static_cast<std::int64_t>(clip.right)),
    };
}

// `visitor` is invoked once per covered sample, so it is taken by const
// reference rather than as a forwarding reference: forwarding it would move
// from the same callable on every pixel of the scan.
template <typename SampleVisitor>
[[nodiscard]] std::expected<std::uint64_t, ScreenSpaceTriangleError>
WalkCoverage(const OwnedTriangle& triangle, const ScreenSpaceClipRect clip,
    const std::uint64_t maximum_covered_pixels,
    const SampleVisitor& visitor) noexcept
{
    const ScanRows rows = ResolveScanRows(triangle, clip);
    std::uint64_t covered_pixel_count = 0U;
    for (std::int64_t scan_y = rows.begin; scan_y < rows.end; ++scan_y)
    {
        const auto span = ResolveScanSpan(triangle, clip, scan_y);
        if (!span)
            return std::unexpected(span.error());
        for (std::int64_t scan_x = span->begin; scan_x < span->end; ++scan_x)
        {
            if (covered_pixel_count >= maximum_covered_pixels)
            {
                return std::unexpected(
                    ScreenSpaceTriangleError::LimitExceeded);
            }
            const auto sample = BuildSample(triangle,
                static_cast<std::int32_t>(scan_x),
                static_cast<std::int32_t>(scan_y));
            if (!sample)
                return std::unexpected(sample.error());
            const auto visited = visitor(*sample);
            if (!visited)
                return std::unexpected(visited.error());
            ++covered_pixel_count;
        }
    }
    return covered_pixel_count;
}

// ---------------------------------------------------------------------------
// Coverage proof
//
// The exact preflight below (a full BuildSample walk with a no-op visitor)
// exists only to guarantee that a kernel failure never exposes a partial
// callback prefix. Paying it means rasterizing every covered pixel twice.
//
// Every quantity BuildSample can fail on is affine in x along one scanline
// (the three barycentric weights, each affine channel, S, T and Q), except
// S/Q and T/Q, which are linear-fractional in x and therefore extremal at the
// span endpoints whenever Q keeps one sign and never approaches zero. So two
// probe samples per scanline - the first and last covered pixel of the span -
// bound every interior pixel of that span. When the probes clear the margins
// below, no covered sample can fail, the exact preflight is redundant, and one
// walk suffices. Anything unproven (including a tripped pixel budget or a
// scan-geometry failure) falls back to the exact two-walk path, which keeps
// the original error identity and ordering.
//
// Margins. Interpolation is a three-term sum, so the computed value differs
// from the exact affine value by at most ~3 * DBL_EPSILON * sum|weight*value|,
// bounded here by 9 * DBL_EPSILON * kProofWeightLimit * FLT_MAX ~ 1e25. Half
// the float range (~1.7e38) of headroom and a 1e-6 relative floor under |Q|
// swallow that by more than ten orders of magnitude.
// ---------------------------------------------------------------------------
inline constexpr double kProofWeightLimit = 16.0;
inline constexpr double kProofNarrowLimit =
    static_cast<double>(std::numeric_limits<float>::max()) * 0.5;
inline constexpr double kProofQMargin = 1.0e-6;
// Caps the perspective residual: the absolute rounding error carried into S/Q
// is at most ~1e-13 * max|S| / (kProofQMargin * kProofWeightLimit * max|Q|).
inline constexpr double kProofScaleRatio = 1.0e30;

struct ProofScale final
{
    double q_floor = 0.0;
    bool usable = false;
};

[[nodiscard]] bool IsProofBounded(const double value, const double limit) noexcept
{
    return std::isfinite(value) && std::abs(value) <= limit;
}

[[nodiscard]] ProofScale ResolveProofScale(const OwnedTriangle& triangle) noexcept
{
    double maximum_absolute_s = 0.0;
    double maximum_absolute_t = 0.0;
    double maximum_absolute_q = 0.0;
    for (const auto& vertex : triangle.vertices)
    {
        maximum_absolute_s = std::max(maximum_absolute_s, std::abs(vertex.s));
        maximum_absolute_t = std::max(maximum_absolute_t, std::abs(vertex.t));
        maximum_absolute_q = std::max(maximum_absolute_q, std::abs(vertex.q));
    }

    ProofScale scale;
    if (!(maximum_absolute_q > 0.0))
        return scale;
    if (maximum_absolute_s > kProofScaleRatio * maximum_absolute_q ||
        maximum_absolute_t > kProofScaleRatio * maximum_absolute_q)
    {
        return scale;
    }
    scale.q_floor = kProofQMargin * kProofWeightLimit * maximum_absolute_q;
    scale.usable = scale.q_floor > 0.0;
    return scale;
}

// Mirrors BuildSample's arithmetic and additionally insists on the margins
// that make the whole span safe. `interpolated_q` is published so the caller
// can confirm Q keeps one sign across the span.
[[nodiscard]] bool IsProvenSample(const OwnedTriangle& triangle,
    const ProofScale& scale, const std::int64_t x, const std::int64_t y,
    double& interpolated_q) noexcept
{
    const auto& first = triangle.vertices[0U];
    const auto& second = triangle.vertices[1U];
    const auto& third = triangle.vertices[2U];
    const SampleWeights weights = ComputeWeights(triangle,
        static_cast<double>(x), static_cast<double>(y));
    if (!IsProofBounded(weights.first, kProofWeightLimit) ||
        !IsProofBounded(weights.second, kProofWeightLimit) ||
        !IsProofBounded(weights.third, kProofWeightLimit))
    {
        return false;
    }

    const auto interpolate = [&](const double first_value,
                                 const double second_value,
                                 const double third_value) noexcept {
        return weights.first * first_value + weights.second * second_value +
               weights.third * third_value;
    };
    for (std::uint32_t channel_index = 0U;
         channel_index < triangle.affine_channel_count; ++channel_index)
    {
        const double channel = interpolate(
            first.affine_channels[channel_index],
            second.affine_channels[channel_index],
            third.affine_channels[channel_index]);
        if (!IsProofBounded(channel, kProofNarrowLimit))
            return false;
    }

    const double sample_s = interpolate(first.s, second.s, third.s);
    const double sample_t = interpolate(first.t, second.t, third.t);
    const double sample_q = interpolate(first.q, second.q, third.q);
    if (!IsProofBounded(sample_s, kProofNarrowLimit) ||
        !IsProofBounded(sample_t, kProofNarrowLimit) ||
        !IsProofBounded(sample_q, kProofNarrowLimit))
    {
        return false;
    }
    if (!(std::abs(sample_q) >= scale.q_floor))
        return false;
    if (!IsProofBounded(sample_s / sample_q, kProofNarrowLimit) ||
        !IsProofBounded(sample_t / sample_q, kProofNarrowLimit))
    {
        return false;
    }
    interpolated_q = sample_q;
    return true;
}

// Returns the covered pixel count only when every covered sample is proven to
// build without failure and the whole budget fits. std::nullopt means "not
// proven", never "malformed": the caller then runs the exact preflight.
[[nodiscard]] std::optional<std::uint64_t> ProveCoverage(
    const OwnedTriangle& triangle, const ScreenSpaceClipRect clip,
    const std::uint64_t maximum_covered_pixels) noexcept
{
    const ProofScale scale = ResolveProofScale(triangle);
    if (!scale.usable)
        return std::nullopt;

    const ScanRows rows = ResolveScanRows(triangle, clip);
    std::uint64_t covered_pixel_count = 0U;
    for (std::int64_t scan_y = rows.begin; scan_y < rows.end; ++scan_y)
    {
        const auto span = ResolveScanSpan(triangle, clip, scan_y);
        if (!span)
            return std::nullopt;
        if (span->end <= span->begin)
            continue;
        const std::uint64_t span_width =
            static_cast<std::uint64_t>(span->end - span->begin);
        if (span_width > maximum_covered_pixels - covered_pixel_count)
            return std::nullopt;

        double first_q = 0.0;
        double last_q = 0.0;
        if (!IsProvenSample(triangle, scale, span->begin, scan_y, first_q) ||
            !IsProvenSample(triangle, scale, span->end - 1, scan_y, last_q))
        {
            return std::nullopt;
        }
        if ((first_q > 0.0) != (last_q > 0.0))
            return std::nullopt;
        covered_pixel_count += span_width;
    }
    return covered_pixel_count;
}
} // namespace

ScreenSpaceTriangleResult RasterizeScreenSpaceTriangle(
    const std::span<const ScreenSpaceTriangleVertex, 3U> vertices,
    const ScreenSpaceClipRect clip,
    const ScreenSpaceTriangleVisitor visitor,
    void* const visitor_context,
    const ScreenSpaceTriangleLimits limits) noexcept
{
    if (!IsValidLimits(limits))
        return std::unexpected(ScreenSpaceTriangleError::InvalidLimits);
    if (visitor == nullptr)
        return std::unexpected(ScreenSpaceTriangleError::MissingVisitor);

    const std::int64_t clip_width = static_cast<std::int64_t>(clip.right) -
                                    static_cast<std::int64_t>(clip.left);
    const std::int64_t clip_height = static_cast<std::int64_t>(clip.bottom) -
                                     static_cast<std::int64_t>(clip.top);
    if (clip_width <= 0 || clip_height <= 0)
        return std::unexpected(ScreenSpaceTriangleError::InvalidClip);
    if (clip_width > static_cast<std::int64_t>(limits.maximum_clip_width) ||
        clip_height > static_cast<std::int64_t>(limits.maximum_clip_height))
    {
        return std::unexpected(ScreenSpaceTriangleError::LimitExceeded);
    }

    const auto triangle = SnapshotTriangle(vertices, limits);
    if (!triangle)
        return std::unexpected(triangle.error());

    // Two probe samples per scanline usually prove that no covered sample can
    // fail; only then is the exact per-pixel preflight skipped, so the
    // all-or-nothing failure contract is unchanged either way.
    std::uint64_t proven_pixel_count = 0U;
    if (const auto proven = ProveCoverage(*triangle, clip,
            limits.maximum_covered_pixels);
        proven)
    {
        proven_pixel_count = *proven;
    }
    else
    {
        const auto preflight = WalkCoverage(*triangle, clip,
            limits.maximum_covered_pixels,
            [](const ScreenSpaceTriangleSample&) noexcept
                -> std::expected<void, ScreenSpaceTriangleError> {
                return {};
            });
        if (!preflight)
            return std::unexpected(preflight.error());
        proven_pixel_count = *preflight;
    }

    const auto emitted = WalkCoverage(*triangle, clip,
        limits.maximum_covered_pixels,
        [&](const ScreenSpaceTriangleSample& sample) noexcept
            -> std::expected<void, ScreenSpaceTriangleError> {
            switch (visitor(visitor_context, sample))
            {
            case ScreenSpaceTriangleVisitControl::Continue:
                return {};
            case ScreenSpaceTriangleVisitControl::Stop:
                return std::unexpected(
                    ScreenSpaceTriangleError::VisitorStopped);
            default:
                return std::unexpected(
                    ScreenSpaceTriangleError::InvalidVisitorResult);
            }
        });
    if (!emitted)
        return std::unexpected(emitted.error());
    if (*emitted != proven_pixel_count)
        return std::unexpected(ScreenSpaceTriangleError::ArithmeticFailure);
    return ScreenSpaceTriangleSummary{.covered_pixel_count = *emitted};
}
} // namespace omega::frontend::presentation
