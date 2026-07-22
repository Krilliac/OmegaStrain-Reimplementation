#pragma once

#include "omega/frontend/compositor_math.h"

#include <cstdint>
#include <expected>
#include <vector>

namespace omega::content
{
class FrontEndScreenBundle;
}

namespace omega::frontend::presentation
{
inline constexpr std::uint64_t kRetailTitleFrameByteCount =
    static_cast<std::uint64_t>(kCanonicalRasterWidth) *
    static_cast<std::uint64_t>(kCanonicalRasterHeight) * 4U;

// Fully owned host-transfer frame. Every successful frame is the complete
// canonical 640x448 image; no source view, bundle reference, or allocator
// owned by the content service survives the call.
struct OwnedRgba8Frame final
{
    std::uint32_t width = kCanonicalRasterWidth;
    std::uint32_t height = kCanonicalRasterHeight;
    std::vector<std::uint8_t> pixels;

    bool operator==(const OwnedRgba8Frame&) const = default;
};

// Deliberately identity-free failure categories. They are safe to surface to
// a host log and never carry owner paths, member names, strings, or payloads.
enum class RetailTitleCompositionError : std::uint8_t
{
    InvalidRetailCapability = 0U,
    UnsupportedScreen,
    UnsupportedWidgetSemantics,
    UnsupportedActionSemantics,
    UnsupportedTextEncoding,
    UnsupportedVisualHierarchy,
    UnsupportedAnimation,
    UnsupportedTextureSampling,
    UnsupportedTransform,
    UnsupportedProjection,
    UnsupportedRasterization,
    UnsupportedOutputAlpha,
    InvalidGeometry,
    GeometryOutOfBounds,
    ArithmeticOverflow,
    IncompleteCoverage,
    AllocationFailure,
};

using RetailTitleCompositionResult =
    std::expected<OwnedRgba8Frame, RetailTitleCompositionError>;

// [any thread; stateless/reentrant] Borrows one immutable live retail bundle
// for the duration of the call and returns only owned bytes. The caller must
// not concurrently move or destroy the bundle. This initial fail-closed slice
// accepts only an untextured, animation-free, single-resource, constant-color,
// fully opaque canonical cover. Its output is invariant to the still-unproven
// retail sampling/addressing, interpolation, draw-order, blend-alpha, text,
// font, animation, and action semantics. Any screen requiring one of those
// semantics is rejected in full; a partial frame is never returned.
//
// Pure value boundary: no filesystem/service access, globals, SDL, renderer,
// worker, or retained cache. It is therefore hot-reload-safe as a leaf call.
[[nodiscard]] RetailTitleCompositionResult ComposeStaticRetailTitle(
    const content::FrontEndScreenBundle& bundle) noexcept;
} // namespace omega::frontend::presentation

static_assert(omega::frontend::presentation::kRetailTitleFrameByteCount == 1'146'880U);
