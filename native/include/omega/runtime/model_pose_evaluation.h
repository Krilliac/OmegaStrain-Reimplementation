#pragma once

#include "omega/asset/model_ir.h"
#include "omega/asset/model_ir_result.h"

#include <cstdint>
#include <expected>
#include <span>

namespace omega::runtime
{
// [any thread; reentrant] Composes each joint's skeleton-global/model-space transform from
// SkeletonIR's own local_bind_transform values, following each joint's parent-before-child chain
// (global[i] = global[parent[i]] * local_bind_transform[i], global[i] = local_bind_transform[i] for a
// root joint). This evaluates the skeleton's authored bind pose only; it decodes and blends no
// retail clip, and it re-validates the parent chain and every input/composed transform for
// finiteness regardless of any prior validation the caller performed.
[[nodiscard]] asset::ModelIrResult<asset::GlobalPoseIR> EvaluateBindPose(
    const asset::SkeletonIR& skeleton, const asset::DecodeLimits& limits = {});

// [any thread; reentrant] Composes each joint's skeleton-global/model-space transform from an
// arbitrary caller-supplied local PoseIR against the given SkeletonIR's parent chain, ignoring the
// skeleton's own local_bind_transform values. pose.joint_local_transforms must have exactly one
// entry per skeleton joint, in the same source order. This is the generic project-owned pose
// evaluator: no independently established retail clip/keyframe grammar exists yet, so this
// function performs no clip time-sampling, interpolation, or blending; it only composes a pose the
// caller has already selected.
[[nodiscard]] asset::ModelIrResult<asset::GlobalPoseIR> EvaluatePose(
    const asset::SkeletonIR& skeleton, const asset::PoseIR& pose,
    const asset::DecodeLimits& limits = {});

// Project-owned linear-blend skinning utilities. No retail skinning grammar, weight policy,
// inverse-bind convention, or blend operator is asserted by these APIs. They consume only caller-
// owned canonical values and perform no decoding, animation sampling, allocation, or GPU work.
enum class SkinningError : std::uint8_t
{
    CountMismatch = 0U,
    CapacityExceeded,
    NonFiniteInput,
    NonFiniteResult,
};

// Project numerical guard, not a retail threshold.
inline constexpr float kMinimumSkinTotalWeight = 1.0e-6F;

// [any thread; reentrant] Computes joint_global[i] * inverse_bind[i] into output. All spans must
// have equal length and no more than asset::kMaximumSkeletonJoints entries. Products are staged in
// fixed local storage before output is modified, so failure leaves output unchanged and input/output
// span aliasing is deterministic. The function is noexcept and allocation-free.
[[nodiscard]] std::expected<void, SkinningError> ComposeSkinningMatrices(
    std::span<const asset::Matrix4x4IR> joint_global_transforms,
    std::span<const asset::Matrix4x4IR> inverse_bind_transforms,
    std::span<asset::Matrix4x4IR> skinning_matrices) noexcept;

// [any thread; reentrant] Applies the first used_influences slots, clamped to the fixed canonical
// storage ceiling, as a normalized affine weighted blend. Out-of-range joint indices are skipped
// and surviving weights renormalize. Negative finite weights extrapolate by project policy.
//
// A non-finite input or result, or an effective total weight whose magnitude is below
// kMinimumSkinTotalWeight, fails soft to bind_position unchanged. The fourth matrix row is ignored:
// palettes are treated as affine and positions are transformed with implicit w = 1.
[[nodiscard]] asset::Float3IR SkinVertexPosition(
    const asset::Float3IR& bind_position,
    std::span<const asset::Matrix4x4IR> skinning_matrices,
    const asset::SkinInfluenceIR& influences) noexcept;
} // namespace omega::runtime
