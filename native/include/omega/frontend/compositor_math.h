#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/asset/indexed_image_ir.h"

#include <array>
#include <cstdint>
#include <expected>

namespace omega::frontend
{
inline constexpr std::uint32_t kCanonicalRasterWidth = 640U;
inline constexpr std::uint32_t kCanonicalRasterHeight = 448U;

struct Point2F
{
    float x = 0.0F;
    float y = 0.0F;

    bool operator==(const Point2F&) const = default;
};

struct RgbF
{
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;

    bool operator==(const RgbF&) const = default;
};

struct RgbaF
{
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 0.0F;

    bool operator==(const RgbaF&) const = default;
};

// The twelve retail coefficients are four column vectors with implied final
// components 0, 0, 0, and 1. No row-major reinterpretation is permitted.
struct AffineTransform12
{
    std::array<float, 12> column_vectors{};

    bool operator==(const AffineTransform12&) const = default;
};

inline constexpr AffineTransform12 kIdentityAffineTransform12{
    .column_vectors = {
        1.0F, 0.0F, 0.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 0.0F, 0.0F,
    },
};

// Fixed retail interface-element bridge. Applying it to (x, y, 0) produces
// (x, 0, y + 1).
inline constexpr AffineTransform12 kInterfaceElementAxisBridge{
    .column_vectors = {
        1.0F, 0.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
        0.0F, 1.0F, 0.0F,
        0.0F, 0.0F, 1.0F,
    },
};

enum class CompositorMathError : std::uint8_t
{
    NonFiniteInput = 0U,
    NonFiniteResult,
    ColorOutOfRange,
};

// [any thread; reentrant] Maps retail GUI coordinates onto the canonical
// 640x448 raster without clipping or applying a host viewport transform.
[[nodiscard]] std::expected<Point2F, CompositorMathError> GuiToCanonicalRaster(
    Point2F gui_position) noexcept;

// [any thread; reentrant] Applies one validated column-vector affine transform
// to an owned point value. No state or input reference is retained.
[[nodiscard]] std::expected<asset::Float3IR, CompositorMathError> TransformPoint(
    const AffineTransform12& transform, const asset::Float3IR& point) noexcept;

// [any thread; reentrant] Returns parent * local so a point is transformed by
// local first and parent second. No state or input reference is retained.
[[nodiscard]] std::expected<AffineTransform12, CompositorMathError> ComposeAffineTransforms(
    const AffineTransform12& parent, const AffineTransform12& local) noexcept;

// [any thread; reentrant] Applies (uv + offset - 0.5) * scale + 0.5 exactly.
// Sampling and address-mode policy deliberately remain outside this module.
[[nodiscard]] std::expected<asset::FrontendUvIR, CompositorMathError> TransformUv(
    const asset::FrontendUvIR& uv,
    const asset::FrontendUvIR& offset,
    const asset::FrontendUvIR& scale) noexcept;

// [any thread; reentrant] Converts the decoded vertex bytes to normalized
// channels and multiplies them component-wise by the effective node color.
[[nodiscard]] std::expected<RgbaF, CompositorMathError> ModulateVertexColor(
    const asset::FrontendColorRgba8IR& vertex_color,
    const RgbaF& effective_node_color) noexcept;

// [any thread; reentrant] Converts a raw GS alpha coefficient to [0, 1]. Raw
// 0x80 is fully opaque and larger values clamp to the same result.
[[nodiscard]] float NormalizeGsAlpha(std::uint8_t raw_alpha) noexcept;

// [any thread; reentrant] Converts raw GS alpha to conventional RGBA8 using
// the proven rounded 0..128 mapping and saturation above 0x80.
[[nodiscard]] std::uint8_t GsAlphaToRgba8(std::uint8_t raw_alpha) noexcept;

// [any thread; reentrant] Normalizes straight RGB bytes and the GS alpha
// coefficient. The returned color remains straight, not premultiplied.
[[nodiscard]] RgbaF NormalizeGsColor(const asset::RawGsRgba8& color) noexcept;

// [any thread; reentrant] Evaluates the proven ALPHA_1 RGB equation
// (Cs - Cd) * As + Cd. It intentionally makes no output-alpha inference.
[[nodiscard]] std::expected<RgbF, CompositorMathError> BlendSourceOverRgb(
    const RgbaF& source, const RgbF& destination) noexcept;
} // namespace omega::frontend

static_assert(sizeof(omega::frontend::Point2F) == 8U);
static_assert(sizeof(omega::frontend::RgbF) == 12U);
static_assert(sizeof(omega::frontend::RgbaF) == 16U);
static_assert(sizeof(omega::frontend::AffineTransform12) == 48U);
