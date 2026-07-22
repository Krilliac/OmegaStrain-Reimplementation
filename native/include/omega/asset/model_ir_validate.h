#pragma once

#include "omega/asset/decode.h"
#include "omega/asset/model_ir.h"

namespace omega::asset
{
// [any thread; reentrant] Validates a project-owned SkeletonIR: a joint count within
// kMaximumSkeletonJoints and the caller's item budget, a finite local_bind_transform per joint,
// and a strictly-precedes parent_index per joint (which makes the hierarchy acyclic by
// construction). Assigns no retail bone semantics; only proves internal self-consistency.
[[nodiscard]] DecodeResult<void> ValidateSkeletonIR(
    const SkeletonIR& skeleton, const DecodeLimits& limits = {});

// [any thread; reentrant] Validates a project-owned ModelIR: a whole-triangle, in-range,
// finite-position mesh; when present, a self-consistent skeleton (via ValidateSkeletonIR); when
// present, a skin_influences table sized exactly to mesh.positions with in-range joint indices,
// finite non-negative weights, and used_influences within kMaximumSkinInfluencesPerVertex; and the
// caller's DecodeLimits item budget across every counted component. skin_influences requires an
// owned skeleton to validate its joint indices against. Assigns no retail vertex/bone semantics.
[[nodiscard]] DecodeResult<void> ValidateModelIR(const ModelIR& model, const DecodeLimits& limits = {});

// [any thread; reentrant] Validates a project-owned PoseIR against a specific SkeletonIR: an exact
// joint-count match and a finite joint_local_transforms entry per joint, within the caller's item
// budget. Does not itself validate the skeleton's own hierarchy; call ValidateSkeletonIR first.
[[nodiscard]] DecodeResult<void> ValidatePoseAgainstSkeleton(
    const SkeletonIR& skeleton, const PoseIR& pose, const DecodeLimits& limits = {});
} // namespace omega::asset
