#pragma once

#include "omega/runtime/render_draw_list.h"
#include "omega/simulation/simulation_world.h"

#include <algorithm>
#include <cstdint>

namespace omega::app
{
// [any thread; reentrant] Maps one synthetic project-owned Position3 copy to a
// bounded diagnostic marker destination. Positive X moves right, positive Z
// moves up, and Y is ignored. This is presentation policy only: it establishes
// no retail coordinate, camera, world-placement, physics, or collision contract.
// Every Position3 value is accepted, and the function performs no allocation,
// I/O, GPU work, mutation, or reference retention.
[[nodiscard]] constexpr runtime::RenderTargetRectQ16
PlanProjectDiagnosticActorMarkerDestination(
    const simulation::Position3 position) noexcept
{
    constexpr std::int64_t extent =
        static_cast<std::int64_t>(runtime::kNormalizedRenderExtent);
    constexpr std::int64_t center = extent / 2;
    constexpr std::int64_t unit = extent / 64;
    constexpr std::int64_t half_extent = unit;
    constexpr std::int64_t maximum_coordinate =
        (center - half_extent) / unit;

    static_assert(extent == 65'536);
    static_assert(unit == 1'024);
    static_assert(maximum_coordinate == 31);

    // Clamp before multiplication and before reversing Z. In particular, do
    // not negate an unbounded Z because INT64_MIN cannot be represented after
    // negation. These bounded intermediates keep every edge in [0, 65'536].
    const std::int64_t x =
        std::clamp(position.x, -maximum_coordinate, maximum_coordinate);
    const std::int64_t z =
        std::clamp(position.z, -maximum_coordinate, maximum_coordinate);
    const std::int64_t center_x = center + x * unit;
    const std::int64_t center_y = center - z * unit;

    return runtime::RenderTargetRectQ16{
        .left = static_cast<std::uint32_t>(center_x - half_extent),
        .top = static_cast<std::uint32_t>(center_y - half_extent),
        .right = static_cast<std::uint32_t>(center_x + half_extent),
        .bottom = static_cast<std::uint32_t>(center_y + half_extent),
    };
}
} // namespace omega::app
