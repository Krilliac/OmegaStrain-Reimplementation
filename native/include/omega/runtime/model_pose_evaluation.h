#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/model_ir.h"

namespace omega::runtime
{
// [any thread; reentrant] Composes each joint's world-space transform from SkeletonIR's own
// local_bind_transform values, following each joint's parent-before-child chain
// (world[i] = world[parent[i]] * local_bind_transform[i], world[i] = local_bind_transform[i] for a
// root joint). This evaluates the skeleton's authored bind pose only; it decodes and blends no
// retail clip, and it re-validates the parent chain and every input/composed transform for
// finiteness regardless of any prior validation the caller performed.
[[nodiscard]] asset::DecodeResult<asset::WorldPoseIR> EvaluateBindPose(
    const asset::SkeletonIR& skeleton, const asset::DecodeLimits& limits = {});

// [any thread; reentrant] Composes each joint's world-space transform from an arbitrary
// caller-supplied local PoseIR against the given SkeletonIR's parent chain, ignoring the
// skeleton's own local_bind_transform values. pose.joint_local_transforms must have exactly one
// entry per skeleton joint, in the same source order. This is the generic project-owned pose
// evaluator: no independently established retail clip/keyframe grammar exists yet, so this
// function performs no clip time-sampling, interpolation, or blending; it only composes a pose the
// caller has already selected.
[[nodiscard]] asset::DecodeResult<asset::WorldPoseIR> EvaluatePose(
    const asset::SkeletonIR& skeleton, const asset::PoseIR& pose,
    const asset::DecodeLimits& limits = {});
} // namespace omega::runtime
