#pragma once

#include "omega/frontend/compositor_math.h"

#include <cstdint>
#include <expected>

namespace omega::content
{
class FrontEndTextureBinding;
}

namespace omega::frontend::presentation
{
// The retained frontend TDX family is already bounded to this extent by its
// decoder. Rechecking the hard ceiling here keeps generated or future callers
// from turning a sampling operation into unbounded indexing work.
inline constexpr std::uint32_t kRetailFrontEndTextureMaximumDimension = 512U;
inline constexpr std::uint64_t kRetailFrontEndTextureMaximumPixels =
    static_cast<std::uint64_t>(kRetailFrontEndTextureMaximumDimension) *
    static_cast<std::uint64_t>(kRetailFrontEndTextureMaximumDimension);

// Texture alpha is a multiplier only when TCC consumes palette alpha. Identity
// makes the TCC-disabled case explicit: callers retain vertex/node alpha rather
// than replacing it with an invented texture value.
enum class RetailFrontEndTextureAlphaContribution : std::uint8_t
{
    Identity = 0U,
    Palette,
};

struct RetailFrontEndTextureSample final
{
    // Straight normalized texture RGB plus the explicit alpha multiplier.
    // This value is not premultiplied and performs no vertex/node modulation.
    RgbaF modulation;
    RetailFrontEndTextureAlphaContribution alpha_contribution =
        RetailFrontEndTextureAlphaContribution::Identity;

    bool operator==(const RetailFrontEndTextureSample&) const = default;
};

// Identity-free failures only. No owner path, member name, payload byte, or
// serialized field escapes this presentation boundary.
enum class RetailFrontEndTextureSamplingError : std::uint8_t
{
    EmptyImage = 0U,
    DimensionLimitExceeded,
    UnsupportedEncoding,
    InvalidIndexStorage,
    InvalidPaletteStorage,
    PaletteIndexOutOfRange,
    TexelCoordinateOutOfRange,
    UnsupportedAlphaMode,
    NonFiniteCoordinate,
};

using RetailFrontEndTextureSamplingResult =
    std::expected<RetailFrontEndTextureSample,
        RetailFrontEndTextureSamplingError>;

// [any thread; stateless/reentrant] Looks up one already-expanded indexed-4 or
// indexed-8 texel. The immutable binding is borrowed only for this call. The
// caller must not move or destroy its owning bundle concurrently.
[[nodiscard]] RetailFrontEndTextureSamplingResult LookupRetailFrontEndTexel(
    const content::FrontEndTextureBinding& binding,
    std::uint32_t x,
    std::uint32_t y) noexcept;

// [any thread; stateless/reentrant] Applies the evidence-closed frontend
// sampling rule: normalized ST to texel coordinates, a half-texel subtraction,
// repeat addressing, and four-tap bilinear interpolation. No projection,
// rasterization, depth, blending, FST, draw ordering, or output-alpha policy is
// implied. No input view or binding reference survives the call.
[[nodiscard]] RetailFrontEndTextureSamplingResult
SampleRetailFrontEndTextureBilinearRepeat(
    const content::FrontEndTextureBinding& binding,
    const asset::FrontendUvIR& normalized_st) noexcept;
} // namespace omega::frontend::presentation

static_assert(omega::frontend::presentation::kRetailFrontEndTextureMaximumPixels ==
              262'144U);
