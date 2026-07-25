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

// ---------------------------------------------------------------------------------------------
// Project-owned linear blend skinning.
//
// The retail skinning model is UNDECODED. The SKA/SKL/SKM containers are passive descriptors in
// this project with no assigned semantics: no retail weight encoding, normalization rule,
// influence count, inverse-bind convention, or blend operator has been independently established.
// Everything below is therefore a PROJECT implementation of textbook linear blend skinning,
// chosen because it is the simplest model that can drive a skinned draw, and it must not be read
// as a statement about how the retail game skinned its characters.
//
// Deliberately NOT modelled here:
//   * no dual-quaternion (or any other non-linear) skinning;
//   * no normal, tangent or bitangent skinning -- positions only, so a caller that needs shading
//     basis vectors must derive them itself;
//   * no scale-compensation, no bone-length preservation and no candy-wrapper mitigation, so the
//     usual linear-blend volume loss at twisted joints is present and accepted;
//   * no per-vertex influence limit beyond the one the signature states -- SkinVertexPosition
//     consumes at most asset::kMaximumSkinInfluencesPerVertex slots from one SkinInfluenceIR and
//     has no notion of a larger influence set spread across several records;
//   * no perspective row: a skinning matrix's fourth row is never applied and never divided by,
//     because the palette is treated as affine;
//   * no GPU palette upload, no draw submission and no clip/animation sampling.
// ---------------------------------------------------------------------------------------------

// Failure modes of ComposeSkinningMatrices. Kept separate from asset::ModelIrError because these
// helpers are runtime-path math: they are noexcept and allocate nothing, so they cannot carry a
// std::string diagnostic.
enum class SkinningError : std::uint8_t
{
    CountMismatch = 0U,
    NonFiniteInput,
    NonFiniteResult,
};

// PROJECT value: the smallest magnitude of summed influence weight SkinVertexPosition is willing
// to divide by. It is a numerical guard chosen for this implementation, not a retail threshold.
// Any total below it (including exactly zero, and including weights that cancel out) fails soft to
// the unskinned position rather than producing a collapsed vertex.
inline constexpr float kMinimumSkinTotalWeight = 1.0e-6F;

// [any thread; reentrant] Writes one skinning matrix per joint as the classic
// joint_global_transforms[i] * inverse_bind_transforms[i] product -- the transform that takes a
// vertex from bind/model space into posed model space for joint i. The caller owns every buffer;
// this function allocates nothing and retains no references.
//
// joint_global_transforms is a posed hierarchy as produced by EvaluateBindPose/EvaluatePose
// (asset::GlobalPoseIR::joint_global_transforms converts to the span implicitly).
// inverse_bind_transforms is the caller's own inverse bind pose, in the same joint order; this
// module neither derives nor validates it as an inverse, because inverse-bind policy is owned by
// whoever authored the model. All three spans must have the same length, otherwise
// SkinningError::CountMismatch is returned and nothing is written.
//
// A non-finite input matrix yields SkinningError::NonFiniteInput and a product that cannot be
// represented as finite floats yields SkinningError::NonFiniteResult. In both cases the entries
// before the offending joint have already been written, so a caller that reuses the output buffer
// must treat a failed call's output as indeterminate rather than partially valid.
[[nodiscard]] std::expected<void, SkinningError> ComposeSkinningMatrices(
    std::span<const asset::Matrix4x4IR> joint_global_transforms,
    std::span<const asset::Matrix4x4IR> inverse_bind_transforms,
    std::span<asset::Matrix4x4IR> skinning_matrices) noexcept;

// [any thread; reentrant] Applies one vertex's bounded influence set to bind_position and returns
// the skinned position, as the weighted affine combination
//   sum(weight[k] * skinning_matrices[joint_indices[k]] * bind_position) / sum(weight[k])
// over the first influences.used_influences slots (clamped to
// asset::kMaximumSkinInfluencesPerVertex). Pure, noexcept, allocation-free; skinning_matrices is
// the palette ComposeSkinningMatrices filled and stays owned by the caller.
//
// Documented weight policy -- weights are NOT required to sum to 1. Dividing by the accumulated
// total normalises them defensively, so (2, 2) behaves exactly like (0.5, 0.5) and a mesh authored
// with unnormalised weights is not silently scaled toward or away from the model origin. Negative
// weights are not rejected: they stay in both sums and therefore extrapolate, which is the
// consistent reading of the same affine combination.
//
// Fail-soft paths, all of which return bind_position UNCHANGED (never a zero vector -- a collapsed
// vertex is the classic skinning bug and is visually catastrophic):
//   * total weight magnitude below kMinimumSkinTotalWeight, which covers zero used influences,
//     an all-zero weight set, and weights that cancel;
//   * a non-finite bind_position, weight, referenced skinning matrix, or accumulated result;
//   * an accumulated result outside the representable float range.
// A joint index at or beyond skinning_matrices.size() is skipped fail-soft -- it is never read,
// and it contributes to neither sum, so the surviving influences renormalise among themselves.
[[nodiscard]] asset::Float3IR SkinVertexPosition(
    const asset::Float3IR& bind_position,
    std::span<const asset::Matrix4x4IR> skinning_matrices,
    const asset::SkinInfluenceIR& influences) noexcept;
} // namespace omega::runtime
