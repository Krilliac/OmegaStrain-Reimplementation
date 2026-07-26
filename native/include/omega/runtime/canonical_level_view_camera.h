#pragma once

#include "omega/asset/scene_ir.h"
#include "omega/runtime/canonical_level_scene.h"

#include <array>
#include <cstdint>
#include <expected>
#include <string>

namespace omega::runtime
{
// Diagnostic framing budgets, not retail camera, projection, or renderer limits.
struct CanonicalLevelViewCameraLimits
{
    std::uint64_t maximum_cells = 4096U;
    std::uint64_t maximum_positions = 1ULL << 20U;
    std::uint64_t maximum_triangle_indices = 6ULL << 20U;
};

// Project-owned diagnostic framing result. It publishes the two camera stages
// plus structural counts and the selected decoded axis order. It deliberately
// carries no decoded coordinate, extent, centre, or scale value, so an aggregate
// may be logged without republishing owner-derived geometry.
struct CanonicalLevelViewCamera
{
    asset::SceneCameraIR camera;
    // Decoded source axes assigned to the clip horizontal, vertical, and depth
    // stages, where 0 is X, 1 is Y, and 2 is Z. This is the established project
    // largest-extent selection order, not a retail axis, handedness, or
    // up-direction claim.
    std::array<std::uint8_t, 3> source_axes{0U, 1U, 2U};
    // Cells whose complete triangles contributed to the framed union.
    std::uint64_t framed_cells = 0U;
    // Positions inspected from those framed cells.
    std::uint64_t framed_positions = 0U;

    bool operator==(const CanonicalLevelViewCamera&) const = default;
};

// [any worker thread; reentrant] Frames an unmodified `CanonicalLevelScene` for
// the project clip volume by producing camera stages only. It never rewrites,
// fits, offsets, clamps, or concatenates a decoded coordinate, so the canonical
// per-cell geometry and `SourceCellOrdinal` identity that
// `BuildCanonicalLevelRenderPages` uploads remain verbatim.
//
// The framed union covers every position of every cell that has both positions
// and complete triangles, matching the renderable set the page adapter publishes.
// Axis assignment reuses the ADR 0005 rule: the two largest union extents become
// the clip horizontal and vertical stages, with equal extents broken in X, then
// Y, then Z order, and the remaining axis becomes clip depth.
//
// `world_to_view` selects those axes, centres the horizontal and vertical stages
// on the union centre, and rebases depth on the union minimum. `view_to_clip`
// applies one uniform horizontal/vertical scale that fits the larger of the two
// extents inside the project clip volume under the established ten-percent
// inset, and maps depth into the same inset range. A zero horizontal/vertical
// extent collapses to the clip centre and a zero depth extent uses the fixed
// project diagnostic depth, matching the accepted global-projection behaviour.
//
// A scene with no renderable cell is framed by the identity camera with zero
// counts rather than rejected. Source ordinals, complete triangle stride, and
// triangle references are validated to match the render-page adapter's
// renderable-cell boundary. Every cell transform must be the canonical identity
// placeholder; a non-identity transform is rejected instead of being assigned
// an unproven placement meaning. Non-finite coordinates, non-finite
// intermediates, and scales that are not representable as `float` are rejected
// with fixed path-free diagnostics before any camera value is published.
//
// This is a project diagnostic framing. It establishes no retail camera, view,
// projection, axis meaning, handedness, up direction, field of view, depth
// range, placement, visibility, or world-space claim.
[[nodiscard]] std::expected<CanonicalLevelViewCamera, std::string>
BuildCanonicalLevelDiagnosticViewCamera(
    const CanonicalLevelScene& canonical,
    const CanonicalLevelViewCameraLimits& limits = CanonicalLevelViewCameraLimits{});
} // namespace omega::runtime
