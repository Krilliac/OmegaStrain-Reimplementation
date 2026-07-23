#pragma once

#include "omega/asset/frontend_ir.h"
#include "omega/frontend/compositor_math.h"
#include "omega/frontend_presentation/retail_title_compositor.h"

#include <array>
#include <cstdint>
#include <expected>
#include <span>

namespace omega::content
{
class FrontEndTextureBinding;
}

namespace omega::frontend::presentation
{
// Project safety ceilings, not retail-format limits. Callers may tighten but
// cannot raise them.
inline constexpr std::uint32_t kRetailFrontEndRasterMaximumTriangles = 65'536U;
inline constexpr std::uint64_t kRetailFrontEndRasterMaximumCoveredSamples =
    16'777'216ULL;
inline constexpr std::uint64_t kRetailFrontEndRasterOutputBytes =
    static_cast<std::uint64_t>(kCanonicalRasterWidth) *
    static_cast<std::uint64_t>(kCanonicalRasterHeight) * 4ULL;
inline constexpr std::uint64_t kRetailFrontEndRasterScratchBytes =
    static_cast<std::uint64_t>(kCanonicalRasterWidth) *
    static_cast<std::uint64_t>(kCanonicalRasterHeight) *
    (sizeof(RgbaF) + sizeof(float));

struct RetailFrontEndRasterLimits final
{
    std::uint32_t maximum_triangles =
        kRetailFrontEndRasterMaximumTriangles;
    std::uint64_t maximum_covered_samples =
        kRetailFrontEndRasterMaximumCoveredSamples;
    std::uint64_t maximum_output_bytes = kRetailFrontEndRasterOutputBytes;
    std::uint64_t maximum_scratch_bytes = kRetailFrontEndRasterScratchBytes;

    bool operator==(const RetailFrontEndRasterLimits&) const = default;
};

// Already-projected canonical-raster input. Depth is an explicit finite rank;
// larger values are nearer. ST and straight RGBA modulation are normalized
// per vertex. No projection, node traversal, animation, or asset lookup occurs
// at this boundary.
struct RetailFrontEndRasterVertex final
{
    float x = 0.0F;
    float y = 0.0F;
    float depth_rank = 0.0F;
    asset::FrontendUvIR normalized_st;
    RgbaF modulation;

    bool operator==(const RetailFrontEndRasterVertex&) const = default;
};

struct RetailFrontEndRasterTriangle final
{
    std::array<RetailFrontEndRasterVertex, 3U> vertices;

    // Null selects the untextured identity path. A non-null immutable binding
    // is borrowed only for the synchronous call and is never retained.
    const content::FrontEndTextureBinding* texture = nullptr;

    bool operator==(const RetailFrontEndRasterTriangle&) const = default;
};

// Identity-free failures only. They carry no path, member name, hash, payload,
// exception text, or other owner-corpus identifier.
enum class RetailFrontEndRasterError : std::uint8_t
{
    InvalidLimits = 0U,
    LimitExceeded,
    NonFiniteInput,
    ColorOutOfRange,
    DegenerateTriangle,
    TextureSamplingFailure,
    ArithmeticFailure,
    RasterizationFailure,
    AllocationFailure,
};

using RetailFrontEndRasterResult =
    std::expected<OwnedRgba8Frame, RetailFrontEndRasterError>;

// [any thread; stateless/reentrant] Renders ordered, already-projected
// triangles into one fully owned canonical 640x448 frame. Coverage comes from
// the bounded screen-space triangle kernel. Textured samples use the retained
// bilinear-repeat sampler; untextured samples use identity modulation.
//
// Alpha is tested strictly greater than zero before depth or color writes.
// Depth compares GEQUAL and writes on pass, so a later equal-depth sample wins.
// RGB uses straight source-over; framebuffer alpha becomes the current
// post-modulation source alpha rather than an accumulated Porter-Duff alpha.
//
// Float color remains in bounded scratch until the complete render succeeds.
// The final host-transfer bridge clamps each channel to [0,1] and rounds to the
// nearest RGBA8 value with half ties upward. This is an explicit native bridge,
// not a claim about exact GS intermediate quantization.
//
// The input span and optional texture bindings are borrowed only for this call.
// The function performs no filesystem, SDL, service, global, or retained-cache
// work. Any failure discards all scratch and returns no partial frame.
[[nodiscard]] RetailFrontEndRasterResult RasterizeRetailFrontEndTriangles(
    std::span<const RetailFrontEndRasterTriangle> triangles,
    RgbaF clear_color,
    RetailFrontEndRasterLimits limits = {}) noexcept;
} // namespace omega::frontend::presentation

static_assert(omega::frontend::presentation::kRetailFrontEndRasterOutputBytes ==
              1'146'880ULL);
static_assert(omega::frontend::presentation::kRetailFrontEndRasterScratchBytes ==
              5'734'400ULL);
