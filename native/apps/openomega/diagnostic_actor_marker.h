#pragma once

#include "omega/gameplay/diagnostic_proximity_trigger.h"
#include "omega/gameplay/diagnostic_target_fire.h"
#include "omega/runtime/diagnostic_actor_scene.h"
#include "omega/runtime/input_tracker.h"
#include "omega/runtime/render_draw_list.h"
#include "omega/simulation/simulation_world.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

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

// [any thread; reentrant] Projects the one fixed, synthetic diagnostic objective volume through
// the same 1,024-Q16-per-unit X/Z presentation policy as the actor marker. The armed rectangle is
// visible only until the launch-local objective latches complete. This assigns no retail trigger,
// mission, camera, coordinate-scale, or persistence semantics.
[[nodiscard]] constexpr std::optional<runtime::RenderTargetRectQ16>
PlanProjectDiagnosticObjectiveMarkerDestination(
    const gameplay::DiagnosticProximityTriggerState state) noexcept
{
    if (state.objective_complete)
        return std::nullopt;

    constexpr std::int64_t center =
        static_cast<std::int64_t>(runtime::kNormalizedRenderExtent) / 2;
    constexpr std::int64_t unit =
        static_cast<std::int64_t>(runtime::kNormalizedRenderExtent) / 64;
    constexpr gameplay::DiagnosticProximityVolumeXZ volume =
        gameplay::kProjectDiagnosticObjectiveVolume;
    static_assert(unit == 1'024);
    static_assert(volume.min_x <= volume.max_x);
    static_assert(volume.min_z <= volume.max_z);

    return runtime::RenderTargetRectQ16{
        .left = static_cast<std::uint32_t>(center + volume.min_x * unit),
        .top = static_cast<std::uint32_t>(center - volume.max_z * unit),
        .right = static_cast<std::uint32_t>(center + volume.max_x * unit),
        .bottom = static_cast<std::uint32_t>(center - volume.min_z * unit),
    };
}

// [any thread; reentrant] Projects the fixed project-owned diagnostic aim target directly into
// the normalized presentation plane. It becomes visible only after the proximity objective has
// completed and remains visible until the launch-local target latches complete. This assigns no
// retail target, hitbox, weapon, camera, or mission semantics.
[[nodiscard]] constexpr std::optional<runtime::RenderTargetRectQ16>
PlanProjectDiagnosticTargetMarkerDestination(
    const gameplay::DiagnosticProximityTriggerState proximity_state,
    const gameplay::DiagnosticTargetFireState target_fire_state) noexcept
{
    if (!proximity_state.objective_complete || target_fire_state.target_complete)
        return std::nullopt;

    constexpr gameplay::DiagnosticAimTargetQ16 target =
        gameplay::kProjectDiagnosticAimTarget;
    static_assert(gameplay::kDiagnosticAimExtent ==
                  runtime::kNormalizedRenderExtent);
    static_assert(target.left < target.right);
    static_assert(target.top < target.bottom);
    static_assert(target.right <= gameplay::kDiagnosticAimExtent);
    static_assert(target.bottom <= gameplay::kDiagnosticAimExtent);
    return runtime::RenderTargetRectQ16{
        .left = target.left,
        .top = target.top,
        .right = target.right,
        .bottom = target.bottom,
    };
}

namespace detail
{
inline constexpr std::uint32_t kProjectDiagnosticCueCenter = 32'768U;
inline constexpr std::uint32_t kProjectDiagnosticCueMinimum = 4'096U;
inline constexpr std::uint32_t kProjectDiagnosticCueMaximum = 61'440U;

[[nodiscard]] constexpr std::uint32_t ClampProjectDiagnosticCueAxis(
    const std::uint32_t value) noexcept
{
    if (value < kProjectDiagnosticCueMinimum)
        return kProjectDiagnosticCueMinimum;
    if (value > kProjectDiagnosticCueMaximum)
        return kProjectDiagnosticCueMaximum;
    return value;
}

[[nodiscard]] constexpr std::array<std::uint32_t, 2U>
ResolveProjectDiagnosticCueCenter(
    const std::optional<runtime::PointerPositionQ16>& pointer) noexcept
{
    const std::uint32_t x = pointer ? pointer->x : kProjectDiagnosticCueCenter;
    const std::uint32_t y = pointer ? pointer->y : kProjectDiagnosticCueCenter;
    return {
        ClampProjectDiagnosticCueAxis(x),
        ClampProjectDiagnosticCueAxis(y),
    };
}
} // namespace detail

// [any thread; reentrant] Plans two synthetic native diagnostic presentation bars around the
// available absolute pointer position, or around target center when the pointer is unavailable.
// This project-only policy establishes no retail crosshair, aim, weapon, sensitivity, or camera
// semantics. Horizontal then vertical order is stable, and clamping keeps every edge in target.
[[nodiscard]] constexpr std::array<runtime::RenderTargetRectQ16, 2U>
PlanProjectDiagnosticTargetCueRectangles(
    const std::optional<runtime::PointerPositionQ16>& pointer) noexcept
{
    constexpr std::uint32_t long_half_extent = 4'096U;
    constexpr std::uint32_t short_half_extent = 256U;
    const auto center = detail::ResolveProjectDiagnosticCueCenter(pointer);
    return {
        runtime::RenderTargetRectQ16{
            .left = center[0U] - long_half_extent,
            .top = center[1U] - short_half_extent,
            .right = center[0U] + long_half_extent,
            .bottom = center[1U] + short_half_extent,
        },
        runtime::RenderTargetRectQ16{
            .left = center[0U] - short_half_extent,
            .top = center[1U] - long_half_extent,
            .right = center[0U] + short_half_extent,
            .bottom = center[1U] + long_half_extent,
        },
    };
}

// [any thread; reentrant] Plans one synthetic native diagnostic presentation square around the
// same resolved pointer center. This project-only policy establishes no retail crosshair, aim,
// weapon, sensitivity, or camera semantics.
[[nodiscard]] constexpr runtime::RenderTargetRectQ16
PlanProjectDiagnosticFireCueRectangle(
    const std::optional<runtime::PointerPositionQ16>& pointer) noexcept
{
    constexpr std::uint32_t half_extent = 768U;
    const auto center = detail::ResolveProjectDiagnosticCueCenter(pointer);
    return runtime::RenderTargetRectQ16{
        .left = center[0U] - half_extent,
        .top = center[1U] - half_extent,
        .right = center[0U] + half_extent,
        .bottom = center[1U] + half_extent,
    };
}

// [any thread; reentrant] Maps the same bounded synthetic X/Z presentation policy to an owned
// project matrix for the indexed diagnostic actor. Positive X moves right, positive Z moves up,
// and Y is ignored. This is not a retail world, camera, axis, or transform convention.
[[nodiscard]] constexpr asset::Matrix4x4IR PlanProjectDiagnosticActorMeshTransform(
    const simulation::Position3 position) noexcept
{
    constexpr std::int64_t maximum_coordinate = 31;
    constexpr float coordinate_scale = 1.0F / 32.0F;
    const std::int64_t x =
        std::clamp(position.x, -maximum_coordinate, maximum_coordinate);
    const std::int64_t z =
        std::clamp(position.z, -maximum_coordinate, maximum_coordinate);

    asset::Matrix4x4IR transform = asset::kIdentityMatrix4x4IR;
    transform.row_major[3U] = static_cast<float>(x) * coordinate_scale;
    transform.row_major[7U] = static_cast<float>(z) * coordinate_scale;
    return transform;
}
} // namespace omega::app
