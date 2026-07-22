#include "omega/frontend/compositor_math.h"

#include "omega/debug/subsystem_entry_break.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace omega::frontend
{
namespace
{
[[nodiscard]] bool IsFinite(const Point2F& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y);
}

[[nodiscard]] bool IsFinite(const asset::Float3IR& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool IsFinite(const asset::FrontendUvIR& value) noexcept
{
    return std::isfinite(value.u) && std::isfinite(value.v);
}

[[nodiscard]] bool IsFinite(const RgbF& value) noexcept
{
    return std::isfinite(value.red) && std::isfinite(value.green) &&
           std::isfinite(value.blue);
}

[[nodiscard]] bool IsFinite(const RgbaF& value) noexcept
{
    return std::isfinite(value.red) && std::isfinite(value.green) &&
           std::isfinite(value.blue) && std::isfinite(value.alpha);
}

[[nodiscard]] bool IsFinite(const AffineTransform12& value) noexcept
{
    return std::ranges::all_of(value.column_vectors, [](const float coefficient) {
        return std::isfinite(coefficient);
    });
}

[[nodiscard]] bool IsNormalized(const RgbF& value) noexcept
{
    return value.red >= 0.0F && value.red <= 1.0F && value.green >= 0.0F &&
           value.green <= 1.0F && value.blue >= 0.0F && value.blue <= 1.0F;
}

[[nodiscard]] bool IsNormalized(const RgbaF& value) noexcept
{
    return IsNormalized(RgbF{value.red, value.green, value.blue}) &&
           value.alpha >= 0.0F && value.alpha <= 1.0F;
}

[[nodiscard]] bool TryNarrowFinite(const double value, float& result) noexcept
{
    constexpr double maximum = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(value) || value < -maximum || value > maximum)
        return false;
    result = static_cast<float>(value);
    return std::isfinite(result);
}

[[nodiscard]] double Coefficient(
    const AffineTransform12& transform, const std::size_t column, const std::size_t row) noexcept
{
    return static_cast<double>(transform.column_vectors[column * 3U + row]);
}

[[nodiscard]] double Translation(
    const AffineTransform12& transform, const std::size_t row) noexcept
{
    return Coefficient(transform, 3U, row);
}
} // namespace

std::expected<Point2F, CompositorMathError> GuiToCanonicalRaster(
    const Point2F gui_position) noexcept
{
    OMEGA_DEBUG_BREAK_SUBSYSTEM_ENTRY("omega_frontend");
    if (!IsFinite(gui_position))
        return std::unexpected(CompositorMathError::NonFiniteInput);

    Point2F result;
    if (!TryNarrowFinite(static_cast<double>(gui_position.x) + 320.0, result.x) ||
        !TryNarrowFinite(224.0 - static_cast<double>(gui_position.y), result.y))
    {
        return std::unexpected(CompositorMathError::NonFiniteResult);
    }
    return result;
}

std::expected<asset::Float3IR, CompositorMathError> TransformPoint(
    const AffineTransform12& transform, const asset::Float3IR& point) noexcept
{
    if (!IsFinite(transform) || !IsFinite(point))
        return std::unexpected(CompositorMathError::NonFiniteInput);

    const std::array<double, 3> input{
        static_cast<double>(point.x),
        static_cast<double>(point.y),
        static_cast<double>(point.z),
    };
    asset::Float3IR result;
    std::array<float*, 3> output{&result.x, &result.y, &result.z};
    for (std::size_t row = 0U; row < output.size(); ++row)
    {
        double value = Translation(transform, row);
        for (std::size_t column = 0U; column < input.size(); ++column)
            value += Coefficient(transform, column, row) * input[column];
        if (!TryNarrowFinite(value, *output[row]))
            return std::unexpected(CompositorMathError::NonFiniteResult);
    }
    return result;
}

std::expected<AffineTransform12, CompositorMathError> ComposeAffineTransforms(
    const AffineTransform12& parent, const AffineTransform12& local) noexcept
{
    if (!IsFinite(parent) || !IsFinite(local))
        return std::unexpected(CompositorMathError::NonFiniteInput);

    AffineTransform12 result;
    for (std::size_t column = 0U; column < 3U; ++column)
    {
        for (std::size_t row = 0U; row < 3U; ++row)
        {
            double value = 0.0;
            for (std::size_t inner = 0U; inner < 3U; ++inner)
                value += Coefficient(parent, inner, row) * Coefficient(local, column, inner);
            if (!TryNarrowFinite(value, result.column_vectors[column * 3U + row]))
                return std::unexpected(CompositorMathError::NonFiniteResult);
        }
    }

    for (std::size_t row = 0U; row < 3U; ++row)
    {
        double value = Translation(parent, row);
        for (std::size_t inner = 0U; inner < 3U; ++inner)
            value += Coefficient(parent, inner, row) * Translation(local, inner);
        if (!TryNarrowFinite(value, result.column_vectors[9U + row]))
            return std::unexpected(CompositorMathError::NonFiniteResult);
    }
    return result;
}

std::expected<asset::FrontendUvIR, CompositorMathError> TransformUv(
    const asset::FrontendUvIR& uv,
    const asset::FrontendUvIR& offset,
    const asset::FrontendUvIR& scale) noexcept
{
    if (!IsFinite(uv) || !IsFinite(offset) || !IsFinite(scale))
        return std::unexpected(CompositorMathError::NonFiniteInput);

    asset::FrontendUvIR result;
    const double transformed_u =
        (static_cast<double>(uv.u) + static_cast<double>(offset.u) - 0.5) *
            static_cast<double>(scale.u) +
        0.5;
    const double transformed_v =
        (static_cast<double>(uv.v) + static_cast<double>(offset.v) - 0.5) *
            static_cast<double>(scale.v) +
        0.5;
    if (!TryNarrowFinite(transformed_u, result.u) || !TryNarrowFinite(transformed_v, result.v))
        return std::unexpected(CompositorMathError::NonFiniteResult);
    return result;
}

std::expected<RgbaF, CompositorMathError> ModulateVertexColor(
    const asset::FrontendColorRgba8IR& vertex_color,
    const RgbaF& effective_node_color) noexcept
{
    if (!IsFinite(effective_node_color))
        return std::unexpected(CompositorMathError::NonFiniteInput);
    if (!IsNormalized(effective_node_color))
        return std::unexpected(CompositorMathError::ColorOutOfRange);

    constexpr float byte_scale = 1.0F / 255.0F;
    return RgbaF{
        static_cast<float>(vertex_color.red) * byte_scale * effective_node_color.red,
        static_cast<float>(vertex_color.green) * byte_scale * effective_node_color.green,
        static_cast<float>(vertex_color.blue) * byte_scale * effective_node_color.blue,
        static_cast<float>(vertex_color.alpha) * byte_scale * effective_node_color.alpha,
    };
}

float NormalizeGsAlpha(const std::uint8_t raw_alpha) noexcept
{
    const std::uint8_t clamped = std::min(raw_alpha, std::uint8_t{128U});
    return static_cast<float>(clamped) / 128.0F;
}

std::uint8_t GsAlphaToRgba8(const std::uint8_t raw_alpha) noexcept
{
    if (raw_alpha >= 128U)
        return 255U;
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(raw_alpha) * 255U + 64U) / 128U);
}

RgbaF NormalizeGsColor(const asset::RawGsRgba8& color) noexcept
{
    constexpr float byte_scale = 1.0F / 255.0F;
    return RgbaF{
        static_cast<float>(color.red) * byte_scale,
        static_cast<float>(color.green) * byte_scale,
        static_cast<float>(color.blue) * byte_scale,
        NormalizeGsAlpha(color.alpha),
    };
}

std::expected<RgbF, CompositorMathError> BlendSourceOverRgb(
    const RgbaF& source, const RgbF& destination) noexcept
{
    if (!IsFinite(source) || !IsFinite(destination))
        return std::unexpected(CompositorMathError::NonFiniteInput);
    if (!IsNormalized(source) || !IsNormalized(destination))
        return std::unexpected(CompositorMathError::ColorOutOfRange);

    const auto blend_channel = [alpha = static_cast<double>(source.alpha)](
                                   const float source_channel,
                                   const float destination_channel) noexcept {
        return static_cast<float>(
            (static_cast<double>(source_channel) - static_cast<double>(destination_channel)) *
                alpha +
            static_cast<double>(destination_channel));
    };
    return RgbF{
        blend_channel(source.red, destination.red),
        blend_channel(source.green, destination.green),
        blend_channel(source.blue, destination.blue),
    };
}
} // namespace omega::frontend
