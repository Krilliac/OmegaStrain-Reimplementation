#include "omega/frontend_presentation/screen_space_triangle_kernel.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
using omega::frontend::presentation::RasterizeScreenSpaceTriangle;
using omega::frontend::presentation::ScreenSpaceClipRect;
using omega::frontend::presentation::ScreenSpaceTriangleError;
using omega::frontend::presentation::ScreenSpaceTriangleLimits;
using omega::frontend::presentation::ScreenSpaceTriangleSample;
using omega::frontend::presentation::ScreenSpaceTriangleVertex;
using omega::frontend::presentation::ScreenSpaceTriangleVisitControl;

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

struct Collector final
{
    std::array<ScreenSpaceTriangleSample, 64U> samples{};
    std::size_t count = 0U;
    bool stop_after_first = false;
};

[[nodiscard]] ScreenSpaceTriangleVisitControl Collect(
    void* const context, const ScreenSpaceTriangleSample& sample) noexcept
{
    auto& collector = *static_cast<Collector*>(context);
    if (collector.count >= collector.samples.size())
        return ScreenSpaceTriangleVisitControl::Stop;
    collector.samples[collector.count++] = sample;
    if (collector.stop_after_first)
        return ScreenSpaceTriangleVisitControl::Stop;
    return ScreenSpaceTriangleVisitControl::Continue;
}

[[nodiscard]] ScreenSpaceTriangleVisitControl CountOnly(
    void* const context, const ScreenSpaceTriangleSample&) noexcept
{
    auto& count = *static_cast<std::uint64_t*>(context);
    ++count;
    return ScreenSpaceTriangleVisitControl::Continue;
}

[[nodiscard]] ScreenSpaceTriangleVisitControl InvalidVisitResult(
    void*, const ScreenSpaceTriangleSample&) noexcept
{
    return static_cast<ScreenSpaceTriangleVisitControl>(255U);
}

[[nodiscard]] std::array<ScreenSpaceTriangleVertex, 3U>
BasicTriangle() noexcept
{
    return {
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 0.0F},
        ScreenSpaceTriangleVertex{.x = 3.0F, .y = 0.0F},
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 3.0F},
    };
}

void TestCoverageWindingAndOrder()
{
    constexpr ScreenSpaceClipRect clip{.left = 0, .top = 0, .right = 4,
        .bottom = 4};
    const auto clockwise = BasicTriangle();
    const std::array<ScreenSpaceTriangleVertex, 3U> counter_clockwise{
        clockwise[0U], clockwise[2U], clockwise[1U]};
    Collector first;
    Collector second;
    const auto first_result = RasterizeScreenSpaceTriangle(clockwise, clip,
        Collect, &first);
    const auto second_result = RasterizeScreenSpaceTriangle(
        counter_clockwise, clip, Collect, &second);

    Check(first_result && first_result->covered_pixel_count == 6U,
        "ceil conversion covers the expected half-open triangle");
    Check(second_result && *second_result == *first_result,
        "opposite winding reports the same coverage count");
    Check(first.count == 6U && second.count == first.count,
        "both windings visit every covered sample");
    bool same_samples = first.count == second.count;
    for (std::size_t index = 0U; same_samples && index < first.count; ++index)
        same_samples = first.samples[index] == second.samples[index];
    Check(same_samples, "winding does not change samples or visit order");

    constexpr std::array<std::pair<std::int32_t, std::int32_t>, 6U> expected{
        std::pair{0, 0}, std::pair{1, 0}, std::pair{2, 0},
        std::pair{0, 1}, std::pair{1, 1}, std::pair{0, 2}};
    bool ordered = first.count == expected.size();
    for (std::size_t index = 0U; ordered && index < expected.size(); ++index)
    {
        ordered = first.samples[index].x == expected[index].first &&
                  first.samples[index].y == expected[index].second;
    }
    Check(ordered, "samples are deterministically row-major");

    Collector repeated;
    const auto repeated_result = RasterizeScreenSpaceTriangle(clockwise, clip,
        Collect, &repeated);
    bool identical = repeated_result == first_result &&
                     repeated.count == first.count;
    for (std::size_t index = 0U; identical && index < first.count; ++index)
        identical = repeated.samples[index] == first.samples[index];
    Check(identical, "repeated evaluation returns identical ordered values");
}

void TestSharedEdgesAndClipping()
{
    constexpr ScreenSpaceClipRect square_clip{.left = 0, .top = 0,
        .right = 2, .bottom = 2};
    const std::array<ScreenSpaceTriangleVertex, 3U> upper_right{
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 0.0F},
        ScreenSpaceTriangleVertex{.x = 2.0F, .y = 0.0F},
        ScreenSpaceTriangleVertex{.x = 2.0F, .y = 2.0F},
    };
    const std::array<ScreenSpaceTriangleVertex, 3U> lower_left{
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 0.0F},
        ScreenSpaceTriangleVertex{.x = 2.0F, .y = 2.0F},
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 2.0F},
    };
    Collector first;
    Collector second;
    const auto first_result = RasterizeScreenSpaceTriangle(upper_right,
        square_clip, Collect, &first);
    const auto second_result = RasterizeScreenSpaceTriangle(lower_left,
        square_clip, Collect, &second);
    Check(first_result && first_result->covered_pixel_count == 3U &&
              second_result && second_result->covered_pixel_count == 1U,
        "two half-open triangles partition their shared edge");

    std::array<std::uint32_t, 4U> visits{};
    for (std::size_t index = 0U; index < first.count; ++index)
    {
        const auto& sample = first.samples[index];
        ++visits[static_cast<std::size_t>(sample.y * 2 + sample.x)];
    }
    for (std::size_t index = 0U; index < second.count; ++index)
    {
        const auto& sample = second.samples[index];
        ++visits[static_cast<std::size_t>(sample.y * 2 + sample.x)];
    }
    Check(visits == std::array<std::uint32_t, 4U>{1U, 1U, 1U, 1U},
        "a shared diagonal has neither duplicate nor missing samples");

    const std::array<ScreenSpaceTriangleVertex, 3U> oversized{
        ScreenSpaceTriangleVertex{.x = -2.0F, .y = -2.0F},
        ScreenSpaceTriangleVertex{.x = 4.0F, .y = -2.0F},
        ScreenSpaceTriangleVertex{.x = -2.0F, .y = 4.0F},
    };
    Collector clipped;
    const auto clipped_result = RasterizeScreenSpaceTriangle(oversized,
        square_clip, Collect, &clipped);
    Check(clipped_result && clipped_result->covered_pixel_count == 3U,
        "coverage is clipped to the caller's half-open bounds");
    bool inside = true;
    for (std::size_t index = 0U; inside && index < clipped.count; ++index)
    {
        const auto& sample = clipped.samples[index];
        inside = sample.x >= square_clip.left && sample.x < square_clip.right &&
                 sample.y >= square_clip.top && sample.y < square_clip.bottom;
    }
    Check(inside, "no callback escapes the clip rectangle");

    std::uint64_t empty_visits = 0U;
    constexpr ScreenSpaceClipRect empty_clip{.left = 10, .top = 10,
        .right = 12, .bottom = 12};
    const auto empty_result = RasterizeScreenSpaceTriangle(upper_right,
        empty_clip, CountOnly, &empty_visits);
    Check(empty_result && empty_result->covered_pixel_count == 0U &&
              empty_visits == 0U,
        "fully clipped geometry succeeds without callbacks");
}

void TestInterpolationAndPerspectiveDivide()
{
    const std::array<float, 1U> first_channel{0.0F};
    const std::array<float, 1U> second_channel{2.0F};
    const std::array<float, 1U> third_channel{4.0F};
    const std::array<ScreenSpaceTriangleVertex, 3U> vertices{
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 0.0F,
            .affine_channels = first_channel, .s = 0.0F, .t = 0.0F,
            .q = 1.0F},
        ScreenSpaceTriangleVertex{.x = 2.0F, .y = 0.0F,
            .affine_channels = second_channel, .s = 4.0F, .t = 0.0F,
            .q = 2.0F},
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 2.0F,
            .affine_channels = third_channel, .s = 0.0F, .t = 2.0F,
            .q = 1.0F},
    };
    Collector collector;
    const auto result = RasterizeScreenSpaceTriangle(vertices,
        {.left = 0, .top = 0, .right = 2, .bottom = 2}, Collect, &collector);
    Check(result && collector.count == 3U,
        "interpolation fixture covers three integer samples");
    if (collector.count == 3U)
    {
        const auto& horizontal_midpoint = collector.samples[1U];
        Check(horizontal_midpoint.AffineChannels().size() == 1U &&
                  Near(horizontal_midpoint.AffineChannels()[0U], 1.0F),
            "affine channels use the screen-space plane");
        Check(Near(horizontal_midpoint.s, 2.0F) &&
                  Near(horizontal_midpoint.t, 0.0F) &&
                  Near(horizontal_midpoint.q, 1.5F),
            "S T and Q are plane-interpolated independently");
        Check(Near(horizontal_midpoint.s_over_q, 4.0F / 3.0F) &&
                  Near(horizontal_midpoint.t_over_q, 0.0F),
            "perspective coordinates divide interpolated S and T by Q");
    }

    auto invalid_q = BasicTriangle();
    for (auto& vertex : invalid_q)
        vertex.q = 0.0F;
    std::uint64_t callback_count = 0U;
    const auto q_result = RasterizeScreenSpaceTriangle(invalid_q,
        {.left = 0, .top = 0, .right = 4, .bottom = 4}, CountOnly,
        &callback_count);
    Check(!q_result &&
              q_result.error() ==
                  ScreenSpaceTriangleError::PerspectiveDivideFailure &&
              callback_count == 0U,
        "invalid Q fails before any callback is published");

    auto later_invalid_q = BasicTriangle();
    later_invalid_q[0U].q = 1.0F;
    later_invalid_q[1U].q = -2.0F;
    later_invalid_q[2U].q = 1.0F;
    callback_count = 0U;
    const auto later_q_result = RasterizeScreenSpaceTriangle(later_invalid_q,
        {.left = 0, .top = 0, .right = 4, .bottom = 4}, CountOnly,
        &callback_count);
    Check(!later_q_result &&
              later_q_result.error() ==
                  ScreenSpaceTriangleError::PerspectiveDivideFailure &&
              callback_count == 0U,
        "a later invalid Q is found before an earlier valid sample is emitted");

    auto nonfinite_q = BasicTriangle();
    nonfinite_q[0U].q = std::numeric_limits<float>::quiet_NaN();
    const auto nonfinite_result = RasterizeScreenSpaceTriangle(nonfinite_q,
        {.left = 0, .top = 0, .right = 4, .bottom = 4}, CountOnly,
        &callback_count);
    Check(!nonfinite_result &&
              nonfinite_result.error() ==
                  ScreenSpaceTriangleError::NonFiniteInput,
        "non-finite vertex Q is rejected as malformed input");
}

void TestDegeneratesInputsAndVisitors()
{
    const std::array<ScreenSpaceTriangleVertex, 3U> degenerate{
        ScreenSpaceTriangleVertex{.x = 0.0F, .y = 0.0F},
        ScreenSpaceTriangleVertex{.x = 1.0F, .y = 1.0F},
        ScreenSpaceTriangleVertex{.x = 2.0F, .y = 2.0F},
    };
    std::uint64_t count = 0U;
    const auto degenerate_result = RasterizeScreenSpaceTriangle(degenerate,
        {.left = 0, .top = 0, .right = 3, .bottom = 3}, CountOnly, &count);
    Check(!degenerate_result &&
              degenerate_result.error() ==
                  ScreenSpaceTriangleError::DegenerateTriangle &&
              count == 0U,
        "zero-area triangles are rejected deterministically");

    const auto basic = BasicTriangle();
    const auto missing_visitor = RasterizeScreenSpaceTriangle(basic,
        {.left = 0, .top = 0, .right = 4, .bottom = 4}, nullptr, nullptr);
    Check(!missing_visitor &&
              missing_visitor.error() ==
                  ScreenSpaceTriangleError::MissingVisitor,
        "a missing visitor is a typed failure");

    Collector stopping;
    stopping.stop_after_first = true;
    const auto stopped = RasterizeScreenSpaceTriangle(basic,
        {.left = 0, .top = 0, .right = 4, .bottom = 4}, Collect, &stopping);
    Check(!stopped &&
              stopped.error() == ScreenSpaceTriangleError::VisitorStopped &&
              stopping.count == 1U,
        "caller-requested early termination is explicit");

    const auto invalid_visitor_result = RasterizeScreenSpaceTriangle(basic,
        {.left = 0, .top = 0, .right = 4, .bottom = 4},
        InvalidVisitResult, nullptr);
    Check(!invalid_visitor_result &&
              invalid_visitor_result.error() ==
                  ScreenSpaceTriangleError::InvalidVisitorResult,
        "an invalid visitor control value fails closed");
}

void TestEveryLimit()
{
    const auto basic = BasicTriangle();
    constexpr ScreenSpaceClipRect clip{.left = 0, .top = 0, .right = 4,
        .bottom = 4};
    std::uint64_t count = 0U;

    auto invalid = ScreenSpaceTriangleLimits{};
    invalid.maximum_affine_channels =
        omega::frontend::presentation::
            kScreenSpaceTriangleMaximumAffineChannels +
        1U;
    auto result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        invalid);
    Check(!result && result.error() == ScreenSpaceTriangleError::InvalidLimits,
        "affine-channel hard ceiling cannot be raised");

    invalid = {};
    invalid.maximum_clip_width = 0U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        invalid);
    Check(!result && result.error() == ScreenSpaceTriangleError::InvalidLimits,
        "zero clip-width limit is invalid");

    invalid = {};
    invalid.maximum_clip_height = 0U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        invalid);
    Check(!result && result.error() == ScreenSpaceTriangleError::InvalidLimits,
        "zero clip-height limit is invalid");

    invalid = {};
    invalid.maximum_covered_pixels = 0U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        invalid);
    Check(!result && result.error() == ScreenSpaceTriangleError::InvalidLimits,
        "zero pixel limit is invalid");

    invalid = {};
    invalid.maximum_coordinate_magnitude = 0.0F;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        invalid);
    Check(!result && result.error() == ScreenSpaceTriangleError::InvalidLimits,
        "zero coordinate limit is invalid");

    auto tightened = ScreenSpaceTriangleLimits{};
    tightened.maximum_clip_width = 3U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        tightened);
    Check(!result && result.error() == ScreenSpaceTriangleError::LimitExceeded,
        "caller clip-width limit is enforced");

    tightened = {};
    tightened.maximum_clip_height = 3U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        tightened);
    Check(!result && result.error() == ScreenSpaceTriangleError::LimitExceeded,
        "caller clip-height limit is enforced");

    tightened = {};
    tightened.maximum_covered_pixels = 5U;
    count = 0U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        tightened);
    Check(!result && result.error() == ScreenSpaceTriangleError::LimitExceeded &&
              count == 0U,
        "pixel budget fails during preflight without callback output");

    tightened = {};
    tightened.maximum_coordinate_magnitude = 2.0F;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        tightened);
    Check(!result && result.error() == ScreenSpaceTriangleError::LimitExceeded,
        "caller coordinate-magnitude limit is enforced");

    tightened = {};
    tightened.maximum_affine_channels = 0U;
    count = 0U;
    result = RasterizeScreenSpaceTriangle(basic, clip, CountOnly, &count,
        tightened);
    Check(result && result->covered_pixel_count == 6U && count == 6U,
        "zero affine-channel allowance accepts unattributed geometry");

    const std::array<float, 1U> one_channel{1.0F};
    auto attributed = BasicTriangle();
    for (auto& vertex : attributed)
        vertex.affine_channels = one_channel;
    tightened = {};
    tightened.maximum_affine_channels = 0U;
    result = RasterizeScreenSpaceTriangle(attributed, clip, CountOnly, &count,
        tightened);
    Check(!result && result.error() == ScreenSpaceTriangleError::LimitExceeded,
        "caller affine-channel limit is enforced");

    const std::array<float, 2U> two_channels{1.0F, 2.0F};
    attributed[2U].affine_channels = two_channels;
    result = RasterizeScreenSpaceTriangle(attributed, clip, CountOnly, &count);
    Check(!result &&
              result.error() ==
                  ScreenSpaceTriangleError::InconsistentAffineChannelCount,
        "mismatched per-vertex channel counts are rejected");

    result = RasterizeScreenSpaceTriangle(basic,
        {.left = 0, .top = 0, .right = 0, .bottom = 4}, CountOnly, &count);
    Check(!result && result.error() == ScreenSpaceTriangleError::InvalidClip,
        "empty clip bounds are rejected");
}
} // namespace

int main()
{
    static_assert(std::is_trivially_copyable_v<ScreenSpaceTriangleSample>);
    static_assert(noexcept(RasterizeScreenSpaceTriangle(
        std::declval<std::span<const ScreenSpaceTriangleVertex, 3U>>(),
        std::declval<ScreenSpaceClipRect>(), CountOnly, nullptr,
        std::declval<ScreenSpaceTriangleLimits>())));

    TestCoverageWindingAndOrder();
    TestSharedEdgesAndClipping();
    TestInterpolationAndPerspectiveDivide();
    TestDegeneratesInputsAndVisitors();
    TestEveryLimit();

    if (failures != 0)
    {
        std::cerr << failures << " screen-space triangle kernel test(s) failed\n";
        return 1;
    }
    return 0;
}
