#pragma once

#include "omega/asset/geometry_ir.h"
#include "omega/asset/render_mesh_ir.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace omega::asset
{
// Fixed per-vertex skin-influence ceiling. This is a project-owned authoring bound chosen for
// SkinInfluenceIR's fixed-size storage; no tracked SKM/SKA/SKAS evidence establishes how many (if
// any) bone influences a retail vertex carries. See docs/adr/0007-project-owned-model-pose-ir.md.
inline constexpr std::size_t kMaximumSkinInfluencesPerVertex = 4;

// Project-owned hard ceiling on joints per SkeletonIR. Chosen as a generous authoring bound, not a
// retail bone-count observation.
inline constexpr std::uint32_t kMaximumSkeletonJoints = 256;

// Project-owned hard ceiling on keyframes per ClipIR. No independently established retail clip
// grammar exists yet (see ClipIR below); this bound only protects future synthetic fixtures.
inline constexpr std::uint32_t kMaximumClipKeyframes = 4096;

// One project-owned joint in a flattened skeleton hierarchy. parent_index, when present, must
// reference an earlier joint in the same SkeletonIR::joints (strictly less than this joint's own
// index). That ordering is a project authoring convention chosen to make acyclicity a mechanical
// per-index check; it is not a claim about retail bone order. local_bind_transform is this joint's
// project-owned joint-to-parent transform at bind time.
struct JointIR
{
    std::string name;
    std::optional<std::uint32_t> parent_index;
    Matrix4x4IR local_bind_transform = kIdentityMatrix4x4IR;

    bool operator==(const JointIR&) const = default;
};

// Fully owned, project-authored joint hierarchy. No retail bone count, name, order, or parent
// relationship is claimed; SkeletonIR exists so synthetic fixtures and any future evidence-backed
// importer target one canonical shape.
struct SkeletonIR
{
    std::vector<JointIR> joints;

    bool operator==(const SkeletonIR&) const = default;
};

// One vertex's bounded skin-influence set. Slots at or beyond used_influences are unexamined
// padding. No retail weight encoding, normalization rule, zero-influence fallback, inverse-bind
// policy, or influence count is asserted. A renderer must not consume this table until a separate
// project-owned skinning contract defines those policies; a future retail bridge will translate
// into that contract.
struct SkinInfluenceIR
{
    std::array<std::uint32_t, kMaximumSkinInfluencesPerVertex> joint_indices{};
    std::array<float, kMaximumSkinInfluencesPerVertex> weights{};
    std::uint8_t used_influences = 0;

    bool operator==(const SkinInfluenceIR&) const = default;
};

// Fully owned project-authored model: renderer-neutral geometry plus an optional owned skeleton
// and an optional parallel per-vertex skin-influence table. skin_influences, when present, has
// exactly one entry per mesh.positions entry. No retail vertex/bone binding is claimed; ModelIR is
// the safe target shape for a future evidence-backed SKM/SKA bridge, not a decoded retail model.
struct ModelIR
{
    RenderMeshIR mesh;
    std::optional<SkeletonIR> skeleton;
    std::optional<std::vector<SkinInfluenceIR>> skin_influences;

    bool operator==(const ModelIR&) const = default;
};

// One evaluated local pose: one joint-to-parent transform per skeleton joint, in the same source
// order as SkeletonIR::joints. It carries no timing, blending, or retail-clip meaning by itself.
struct PoseIR
{
    std::vector<Matrix4x4IR> joint_local_transforms;

    bool operator==(const PoseIR&) const = default;
};

// One evaluated skeleton-global pose: one accumulated joint-to-model transform per skeleton joint,
// in the same source order as SkeletonIR::joints. Scene placement remains a separate
// SceneMeshInstanceIR::local_to_world transform; this value therefore does not claim world-space
// placement. It also is not a complete skinning palette because inverse-bind policy is deliberately
// outside this slice. See omega/runtime/model_pose_evaluation.h.
struct GlobalPoseIR
{
    std::vector<Matrix4x4IR> joint_global_transforms;

    bool operator==(const GlobalPoseIR&) const = default;
};

// One project-owned pose keyframe at an opaque, caller-defined sample coordinate. No retail time
// unit, tick rate, or interpolation rule is established.
struct ClipKeyframeIR
{
    float sample_time = 0.0F;
    PoseIR pose;

    bool operator==(const ClipKeyframeIR&) const = default;
};

// Fully owned, source-ordered sequence of pose keyframes. ValidateClipAgainstSkeleton enforces the
// fixed keyframe ceiling, finite sample coordinates, pose cardinality, and aggregate caller
// budgets. ClipIR remains a target shape only: this slice decodes no independently established
// retail clip grammar into it and implements no time-based sampling or blending.
struct ClipIR
{
    std::string name;
    std::vector<ClipKeyframeIR> keyframes;

    bool operator==(const ClipIR&) const = default;
};
} // namespace omega::asset
