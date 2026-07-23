#pragma once

#include "omega/frontend_presentation/retail_frontend_cpu_raster.h"

#include <cstdint>
#include <expected>

namespace omega::content
{
class FrontEndScreenBundle;
}

namespace omega::frontend::presentation
{
// Project safety ceilings, not retail-format claims. Callers may tighten but
// cannot raise them.
inline constexpr std::uint32_t kRetailRootVisualLayerMaximumPositions = 65'536U;
inline constexpr std::uint32_t kRetailRootVisualLayerMaximumUvs = 65'536U;
inline constexpr std::uint32_t kRetailRootVisualLayerMaximumColors = 65'536U;
inline constexpr std::uint32_t kRetailRootVisualLayerMaximumTriangles =
    kRetailFrontEndRasterMaximumTriangles;
inline constexpr std::uint64_t kRetailRootVisualLayerMaximumCoveredSamples =
    kRetailFrontEndRasterMaximumCoveredSamples;
inline constexpr std::uint64_t kRetailRootVisualLayerCoverageBytes =
    static_cast<std::uint64_t>(kCanonicalRasterWidth) *
    static_cast<std::uint64_t>(kCanonicalRasterHeight);
inline constexpr std::uint64_t kRetailRootVisualLayerMaximumIntermediateBytes =
    kRetailRootVisualLayerCoverageBytes +
    static_cast<std::uint64_t>(kRetailRootVisualLayerMaximumTriangles) *
        sizeof(RetailFrontEndRasterTriangle);

struct RetailRootVisualLayerLimits final
{
    std::uint32_t maximum_positions =
        kRetailRootVisualLayerMaximumPositions;
    std::uint32_t maximum_uvs = kRetailRootVisualLayerMaximumUvs;
    std::uint32_t maximum_colors = kRetailRootVisualLayerMaximumColors;
    std::uint32_t maximum_triangles =
        kRetailRootVisualLayerMaximumTriangles;
    std::uint64_t maximum_covered_samples =
        kRetailRootVisualLayerMaximumCoveredSamples;
    std::uint64_t maximum_intermediate_bytes =
        kRetailRootVisualLayerMaximumIntermediateBytes;
    std::uint64_t maximum_output_bytes =
        kRetailFrontEndRasterOutputBytes;
    std::uint64_t maximum_raster_scratch_bytes =
        kRetailFrontEndRasterScratchBytes;

    bool operator==(const RetailRootVisualLayerLimits&) const = default;
};

// This value is intentionally incapable of authorizing retail presentation.
// It says exactly what was rendered: only the resolved parentless root visual
// node's own triangles. Widget descendants and visual descendants are omitted
// even when present in the borrowed bundle.
enum class RetailRootVisualLayerCoverage : std::uint8_t
{
    RootVisualOwnGeometryOnly = 0U,
};

struct RetailRootVisualLayer final
{
    OwnedRgba8Frame frame;
    RetailRootVisualLayerCoverage coverage =
        RetailRootVisualLayerCoverage::RootVisualOwnGeometryOnly;

    bool operator==(const RetailRootVisualLayer&) const = default;
};

// Identity-free failure categories. No path, member, source string, payload,
// hash, or exception text crosses this boundary.
enum class RetailRootVisualLayerError : std::uint8_t
{
    InvalidRetailCapability = 0U,
    InvalidLimits,
    LimitExceeded,
    UnsupportedRootWidget,
    UnsupportedRootText,
    UnsupportedRootAction,
    MissingRootVisualBinding,
    UnsupportedRootVisualHierarchy,
    UnsupportedRootAnimation,
    MissingTextureBinding,
    InvalidGeometry,
    NonFiniteInput,
    TransformFailure,
    ProjectionFailure,
    DegenerateGeometry,
    OverlappingCoverage,
    IncompleteCoverage,
    TextureSamplingFailure,
    RasterizationFailure,
    TransparentOutput,
    ArithmeticFailure,
    AllocationFailure,
};

using RetailRootVisualLayerResult =
    std::expected<RetailRootVisualLayer, RetailRootVisualLayerError>;

// [any thread; stateless/reentrant] Borrows a live immutable Title,
// CreateAgent, or LoadAgent bundle for this synchronous call. The caller must
// not concurrently move or destroy it. A visible Container root with a
// binding is resolved as a parentless visual resource; only that visual node's
// own static geometry is transformed, projected, and rasterized.
//
// Child traversal, child/self lane ordering, text, actions, and animation are
// deliberately not assigned. To avoid guessing triangle order, exact
// canonical-raster coverage is preflighted with the established screen-space
// kernel: any covered-pixel overlap is rejected, every pixel must be covered,
// and the final owned frame must be fully opaque. Optional textures resolve
// only in the visual's exact owning scope. No input view or capability escapes.
[[nodiscard]] RetailRootVisualLayerResult ComposeRetailRootVisualLayer(
    const content::FrontEndScreenBundle& bundle,
    RetailRootVisualLayerLimits limits = {}) noexcept;
} // namespace omega::frontend::presentation

static_assert(
    omega::frontend::presentation::kRetailRootVisualLayerCoverageBytes ==
    286'720ULL);
