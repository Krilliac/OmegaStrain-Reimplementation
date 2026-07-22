#pragma once

#include "omega/asset/model_ir.h"
#include "omega/asset/model_ir_result.h"

namespace omega::asset
{
// [any thread; reentrant] Validates a project-owned SkeletonIR: a joint count within
// kMaximumSkeletonJoints and caller item/output/string budgets, a finite local_bind_transform per
// joint, and a strictly-precedes parent_index per joint (which makes the hierarchy acyclic by
// construction). Assigns no retail bone semantics; only proves internal self-consistency.
[[nodiscard]] ModelIrResult<void> ValidateSkeletonIR(
    const SkeletonIR& skeleton, const DecodeLimits& limits = {});

// [any thread; reentrant] Validates a project-owned ModelIR: a whole-triangle, in-range,
// finite-position mesh; when present, a self-consistent skeleton (via ValidateSkeletonIR); when
// present, a skin_influences table sized exactly to mesh.positions with in-range joint indices,
// finite non-negative used weights, canonical-zero unused slots, and used_influences within
// kMaximumSkinInfluencesPerVertex; and the caller's aggregate DecodeLimits item/output/string
// budgets across every owned component.
// skin_influences requires an owned skeleton to validate its joint indices against. Count/size
// limits are checked before full mesh traversal. Assigns no retail vertex/bone semantics.
[[nodiscard]] ModelIrResult<void> ValidateModelIR(
    const ModelIR& model, const DecodeLimits& limits = {});

// [any thread; reentrant] Validates a project-owned PoseIR against a specific SkeletonIR: an exact
// joint-count match and a finite joint_local_transforms entry per joint, within caller item/output
// budgets. Does not itself validate the skeleton's own hierarchy; call ValidateSkeletonIR first.
[[nodiscard]] ModelIrResult<void> ValidatePoseAgainstSkeleton(
    const SkeletonIR& skeleton, const PoseIR& pose, const DecodeLimits& limits = {});

// [any thread; reentrant] Validates a project-owned ClipIR against a specific SkeletonIR. Enforces
// kMaximumClipKeyframes, a caller-bounded name, finite sample coordinates, one finite local
// transform per skeleton joint in every keyframe, and checked aggregate item/output budgets across
// the whole clip. It does not validate the SkeletonIR parent hierarchy, joint names, or bind
// transforms; callers must validate that separately with ValidateSkeletonIR (or validate the owning
// model with ValidateModelIR). It does not establish ordering, interpolation, blending, or retail
// time units.
[[nodiscard]] ModelIrResult<void> ValidateClipAgainstSkeleton(
    const SkeletonIR& skeleton, const ClipIR& clip, const DecodeLimits& limits = {});
} // namespace omega::asset
