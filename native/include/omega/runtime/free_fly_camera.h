#pragma once

#include "omega/asset/geometry_ir.h"

#include <optional>
#include <string_view>

namespace omega::runtime
{
// A minimal input-driven free-fly camera for navigating the diagnostic 3D level
// view. Pure value math (no allocation, no state, no platform types): position +
// yaw/pitch produce a world_to_view matrix, and a fixed-field-of-view perspective
// produces a view_to_clip matrix. Both are row-major, column-vector transforms
// consistent with runtime::ComposeObjectToClip and left-handed (view space +Z is
// forward, +Y is up, +X is right), with a Direct3D-style clip depth range [0, 1].
struct FreeFlyPose
{
    asset::Float3IR position{};
    float yaw = 0.0F;   // radians about world +Y (0 => facing world +Z)
    float pitch = 0.0F; // radians about the camera right axis (clamped)

    bool operator==(const FreeFlyPose&) const = default;
};

// Per-frame movement/look intent, already resolved from held input. Translation
// is in camera-local units (forward/right/up along the current basis); the look
// deltas are radians added to yaw/pitch.
struct FreeFlyInput
{
    float forward = 0.0F;  // +1 = toward the view direction, -1 = back
    float strafe = 0.0F;   // +1 = camera right, -1 = left
    float vertical = 0.0F; // +1 = world up, -1 = world down
    float yaw_delta = 0.0F;
    float pitch_delta = 0.0F;
};

// [any thread; reentrant] Camera-basis unit vectors for a pose (left-handed).
[[nodiscard]] asset::Float3IR FreeFlyForward(const FreeFlyPose& pose) noexcept;
[[nodiscard]] asset::Float3IR FreeFlyRight(const FreeFlyPose& pose) noexcept;

// [any thread; reentrant] Advances a pose by one input step. Translation moves
// along the (forward, right, world-up) basis scaled by move_speed; pitch is
// clamped to just under +/- 90 degrees so the view never flips. Non-finite
// inputs are ignored (the pose passes through unchanged for that component).
[[nodiscard]] FreeFlyPose AdvanceFreeFly(
    FreeFlyPose pose, const FreeFlyInput& input, float move_speed) noexcept;

// [any thread; reentrant] world_to_view for the pose (a look transform).
[[nodiscard]] asset::Matrix4x4IR FreeFlyViewMatrix(const FreeFlyPose& pose) noexcept;

// [any thread; reentrant] view_to_clip perspective (left-handed, clip depth
// [0,1]). aspect = width/height. Returns identity-safe finite values; callers
// pass sane, positive fov/aspect/near<far.
[[nodiscard]] asset::Matrix4x4IR PerspectiveProjection(
    float vertical_field_of_view_radians, float aspect_ratio, float near_plane,
    float far_plane) noexcept;

// [any thread; reentrant] Parses a headless camera-pose override of the form
// "x,y,z,yaw,pitch" (yaw/pitch in radians). Returns nullopt on any malformed or
// non-finite field. Used by the OPENOMEGA_CAMERA_POSE capture hook.
[[nodiscard]] std::optional<FreeFlyPose> ParseFreeFlyPose(
    std::string_view spec) noexcept;
} // namespace omega::runtime
