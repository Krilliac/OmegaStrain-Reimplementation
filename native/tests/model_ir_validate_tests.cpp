#include "omega/asset/model_ir_validate.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace
{
using omega::asset::DecodeErrorCode;
using omega::asset::DecodeLimits;
using omega::asset::ClipIR;
using omega::asset::ClipKeyframeIR;
using omega::asset::JointIR;
using omega::asset::kIdentityMatrix4x4IR;
using omega::asset::kMaximumClipKeyframes;
using omega::asset::kMaximumSkeletonJoints;
using omega::asset::kMaximumSkinInfluencesPerVertex;
using omega::asset::Matrix4x4IR;
using omega::asset::ModelIR;
using omega::asset::PoseIR;
using omega::asset::RenderMeshIR;
using omega::asset::SkeletonIR;
using omega::asset::SkinInfluenceIR;
using omega::asset::ValidateModelIR;
using omega::asset::ValidateClipAgainstSkeleton;
using omega::asset::ValidatePoseAgainstSkeleton;
using omega::asset::ValidateSkeletonIR;

int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

template <typename Result>
void CheckError(
    const Result& result, const DecodeErrorCode code, const std::string_view message)
{
    Check(!result && result.error().code == code, message);
}

[[nodiscard]] Matrix4x4IR NonFiniteMatrix()
{
    Matrix4x4IR matrix = kIdentityMatrix4x4IR;
    matrix.row_major[0] = std::numeric_limits<float>::quiet_NaN();
    return matrix;
}

[[nodiscard]] SkeletonIR MakeChainSkeleton(const std::size_t joint_count)
{
    SkeletonIR skeleton;
    skeleton.joints.reserve(joint_count);
    for (std::size_t index = 0; index < joint_count; ++index)
    {
        JointIR joint;
        joint.name = "joint";
        if (index > 0)
            joint.parent_index = static_cast<std::uint32_t>(index - 1);
        skeleton.joints.push_back(joint);
    }
    return skeleton;
}

[[nodiscard]] RenderMeshIR MakeTriangleMesh()
{
    RenderMeshIR mesh;
    mesh.positions = {
        {.x = 0.0F, .y = 0.0F, .z = 0.0F},
        {.x = 1.0F, .y = 0.0F, .z = 0.0F},
        {.x = 0.0F, .y = 1.0F, .z = 0.0F},
    };
    mesh.triangle_indices = {0, 1, 2};
    return mesh;
}
} // namespace

int ModelIrValidateFailureCount()
{
    // SkeletonIR: valid empty and chain skeletons are accepted.
    Check(ValidateSkeletonIR(SkeletonIR{}).has_value(), "empty skeleton is accepted");
    const auto chain = MakeChainSkeleton(3);
    Check(ValidateSkeletonIR(chain).has_value(), "strictly-precedes joint chain is accepted");

    // Self-referencing and forward-referencing parent indices are rejected.
    {
        SkeletonIR self_ref = MakeChainSkeleton(2);
        self_ref.joints[1].parent_index = 1;
        CheckError(ValidateSkeletonIR(self_ref), DecodeErrorCode::Malformed,
            "skeleton rejects a self-referencing parent index");
    }
    {
        SkeletonIR forward_ref = MakeChainSkeleton(2);
        forward_ref.joints[0].parent_index = 1;
        CheckError(ValidateSkeletonIR(forward_ref), DecodeErrorCode::Malformed,
            "skeleton rejects a forward-referencing parent index");
    }
    {
        SkeletonIR non_finite = MakeChainSkeleton(1);
        non_finite.joints[0].local_bind_transform = NonFiniteMatrix();
        CheckError(ValidateSkeletonIR(non_finite), DecodeErrorCode::Malformed,
            "skeleton rejects a non-finite local_bind_transform");
    }
    {
        const auto oversized = MakeChainSkeleton(kMaximumSkeletonJoints + 1);
        CheckError(ValidateSkeletonIR(oversized), DecodeErrorCode::LimitExceeded,
            "skeleton rejects joint counts above the fixed ceiling");
    }
    {
        DecodeLimits limits;
        limits.maximum_items = 1 + chain.joints.size();
        Check(ValidateSkeletonIR(chain, limits).has_value(),
            "skeleton succeeds at the exact root-plus-joints item budget");
        limits.maximum_items = chain.joints.size();
        CheckError(ValidateSkeletonIR(chain, limits), DecodeErrorCode::LimitExceeded,
            "skeleton rejects one item below the root-plus-joints item budget");
        limits.maximum_items = 1 + chain.joints.size();
        limits.maximum_output_bytes =
            sizeof(SkeletonIR) + chain.joints.size() * sizeof(JointIR) +
            chain.joints.size() * std::string_view{"joint"}.size();
        Check(ValidateSkeletonIR(chain, limits).has_value(),
            "skeleton succeeds at the exact logical output-byte budget");
        --limits.maximum_output_bytes;
        CheckError(ValidateSkeletonIR(chain, limits), DecodeErrorCode::LimitExceeded,
            "skeleton rejects one byte below the logical output-byte budget");
    }
    {
        DecodeLimits limits;
        limits.maximum_string_bytes = 4;
        const auto result = ValidateSkeletonIR(chain, limits);
        CheckError(result, DecodeErrorCode::LimitExceeded,
            "skeleton rejects a joint name above the caller string budget");
        Check(!result || !result.error().item_index.has_value() ||
                  *result.error().item_index == 0,
            "skeleton string failures use a semantic item index, not a byte offset");
    }

    // ModelIR: mesh-only structural checks.
    {
        ModelIR mesh_only;
        mesh_only.mesh = MakeTriangleMesh();
        Check(ValidateModelIR(mesh_only).has_value(), "mesh-only model is accepted");
    }
    Check(ValidateModelIR(ModelIR{}).has_value(), "default-constructed empty model is accepted");
    {
        ModelIR bad_stride;
        bad_stride.mesh = MakeTriangleMesh();
        bad_stride.mesh.triangle_indices.pop_back();
        CheckError(ValidateModelIR(bad_stride), DecodeErrorCode::Malformed,
            "model rejects a triangle_indices count that is not a multiple of three");
    }
    {
        ModelIR bad_reference;
        bad_reference.mesh = MakeTriangleMesh();
        bad_reference.mesh.triangle_indices[0] = 99;
        CheckError(ValidateModelIR(bad_reference), DecodeErrorCode::InvalidReference,
            "model rejects an out-of-bounds triangle index");
    }
    {
        ModelIR non_finite_position;
        non_finite_position.mesh = MakeTriangleMesh();
        non_finite_position.mesh.positions[0].x = std::numeric_limits<float>::infinity();
        CheckError(ValidateModelIR(non_finite_position), DecodeErrorCode::Malformed,
            "model rejects a non-finite mesh position");
    }
    {
        ModelIR over_budget;
        over_budget.mesh = MakeTriangleMesh();
        over_budget.mesh.positions[0].x = std::numeric_limits<float>::quiet_NaN();
        DecodeLimits limits;
        limits.maximum_items = 6; // model root + 3 positions + 3 indices requires 7
        CheckError(ValidateModelIR(over_budget, limits), DecodeErrorCode::LimitExceeded,
            "model preflights aggregate items before traversing malformed positions");
    }
    {
        ModelIR mesh_only;
        mesh_only.mesh = MakeTriangleMesh();
        const auto exact_output = sizeof(ModelIR) +
                                  mesh_only.mesh.positions.size() * sizeof(omega::asset::Float3IR) +
                                  mesh_only.mesh.triangle_indices.size() * sizeof(std::uint32_t);
        DecodeLimits limits;
        limits.maximum_items = 7;
        limits.maximum_output_bytes = exact_output;
        Check(ValidateModelIR(mesh_only, limits).has_value(),
            "model succeeds at exact aggregate item and output budgets");
        --limits.maximum_output_bytes;
        CheckError(ValidateModelIR(mesh_only, limits), DecodeErrorCode::LimitExceeded,
            "model rejects one byte below its aggregate output budget");
    }

    // ModelIR: skeleton and skin_influences interaction.
    {
        ModelIR with_skeleton;
        with_skeleton.mesh = MakeTriangleMesh();
        with_skeleton.skeleton = MakeChainSkeleton(2);
        Check(ValidateModelIR(with_skeleton).has_value(),
            "model with a valid skeleton and no skin influences is accepted");
    }
    {
        ModelIR bad_skeleton;
        bad_skeleton.mesh = MakeTriangleMesh();
        bad_skeleton.skeleton = MakeChainSkeleton(2);
        bad_skeleton.skeleton->joints[1].parent_index = 1;
        CheckError(ValidateModelIR(bad_skeleton), DecodeErrorCode::Malformed,
            "model propagates its skeleton's own validation failure");
    }
    {
        ModelIR skin_without_skeleton;
        skin_without_skeleton.mesh = MakeTriangleMesh();
        skin_without_skeleton.skin_influences =
            std::vector<SkinInfluenceIR>(skin_without_skeleton.mesh.positions.size());
        CheckError(ValidateModelIR(skin_without_skeleton), DecodeErrorCode::Malformed,
            "model rejects skin_influences without an owned skeleton");
    }
    {
        ModelIR mismatched_size;
        mismatched_size.mesh = MakeTriangleMesh();
        mismatched_size.skeleton = MakeChainSkeleton(1);
        mismatched_size.skin_influences = std::vector<SkinInfluenceIR>(1);
        CheckError(ValidateModelIR(mismatched_size), DecodeErrorCode::Malformed,
            "model rejects a skin_influences size that does not match position count");
    }
    {
        ModelIR full;
        full.mesh = MakeTriangleMesh();
        full.skeleton = MakeChainSkeleton(2);
        full.skin_influences = std::vector<SkinInfluenceIR>(full.mesh.positions.size());
        for (auto& influence : *full.skin_influences)
        {
            influence.used_influences = std::uint8_t{1};
            influence.joint_indices[0] = 1;
            influence.weights[0] = 1.0F;
        }
        Check(ValidateModelIR(full).has_value(),
            "model with matching skeleton and skin_influences is accepted");

        auto out_of_range = full;
        (*out_of_range.skin_influences)[0].joint_indices[0] = 5;
        CheckError(ValidateModelIR(out_of_range), DecodeErrorCode::InvalidReference,
            "model rejects a skin influence joint index outside the skeleton's range");

        auto bad_weight = full;
        (*bad_weight.skin_influences)[0].weights[0] = -1.0F;
        CheckError(ValidateModelIR(bad_weight), DecodeErrorCode::Malformed,
            "model rejects a negative skin influence weight");

        auto non_finite_weight = full;
        (*non_finite_weight.skin_influences)[0].weights[0] =
            std::numeric_limits<float>::quiet_NaN();
        CheckError(ValidateModelIR(non_finite_weight), DecodeErrorCode::Malformed,
            "model rejects a non-finite skin influence weight");

        auto too_many_influences = full;
        (*too_many_influences.skin_influences)[0].used_influences =
            static_cast<std::uint8_t>(kMaximumSkinInfluencesPerVertex + 1);
        CheckError(ValidateModelIR(too_many_influences), DecodeErrorCode::Malformed,
            "model rejects used_influences above the fixed per-vertex ceiling");
    }

    // PoseIR validation against a skeleton.
    {
        PoseIR undersized;
        undersized.joint_local_transforms = {kIdentityMatrix4x4IR, kIdentityMatrix4x4IR};
        CheckError(ValidatePoseAgainstSkeleton(chain, undersized), DecodeErrorCode::Malformed,
            "pose rejects a joint count that does not match a differently sized skeleton");
    }
    {
        PoseIR matching;
        matching.joint_local_transforms.assign(chain.joints.size(), kIdentityMatrix4x4IR);
        Check(ValidatePoseAgainstSkeleton(chain, matching).has_value(),
            "pose with a matching finite transform per joint is accepted");

        auto non_finite = matching;
        non_finite.joint_local_transforms[0] = NonFiniteMatrix();
        CheckError(ValidatePoseAgainstSkeleton(chain, non_finite), DecodeErrorCode::Malformed,
            "pose rejects a non-finite joint_local_transform");

        DecodeLimits limits;
        limits.maximum_items = 1 + matching.joint_local_transforms.size();
        Check(ValidatePoseAgainstSkeleton(chain, matching, limits).has_value(),
            "pose succeeds at the exact root-plus-transforms item budget");
        limits.maximum_items = matching.joint_local_transforms.size();
        CheckError(ValidatePoseAgainstSkeleton(chain, matching, limits),
            DecodeErrorCode::LimitExceeded,
            "pose rejects one item below the root-plus-transforms item budget");
        limits.maximum_items = 1 + matching.joint_local_transforms.size();
        limits.maximum_output_bytes =
            sizeof(PoseIR) + matching.joint_local_transforms.size() * sizeof(Matrix4x4IR);
        Check(ValidatePoseAgainstSkeleton(chain, matching, limits).has_value(),
            "pose succeeds at the exact logical output-byte budget");
        --limits.maximum_output_bytes;
        CheckError(ValidatePoseAgainstSkeleton(chain, matching, limits),
            DecodeErrorCode::LimitExceeded,
            "pose rejects one byte below the logical output-byte budget");
    }

    // ClipIR validation freezes the fixed ceiling, skeleton cardinality, finite values, and
    // aggregate budgets without assigning retail timing or interpolation semantics.
    {
        ClipIR clip;
        clip.name = "idle";
        ClipKeyframeIR first;
        first.sample_time = 0.0F;
        first.pose.joint_local_transforms.assign(chain.joints.size(), kIdentityMatrix4x4IR);
        ClipKeyframeIR second = first;
        second.sample_time = 1.0F;
        clip.keyframes = {first, second};

        Check(ValidateClipAgainstSkeleton(chain, clip).has_value(),
            "clip with finite cardinality-matched keyframes is accepted");

        constexpr std::size_t keyframe_count = 2;
        const auto exact_items = 1 + keyframe_count + keyframe_count * chain.joints.size();
        const auto exact_output = sizeof(ClipIR) + clip.name.size() +
                                  keyframe_count * sizeof(ClipKeyframeIR) +
                                  keyframe_count * chain.joints.size() * sizeof(Matrix4x4IR);
        DecodeLimits limits;
        limits.maximum_items = exact_items;
        limits.maximum_output_bytes = exact_output;
        Check(ValidateClipAgainstSkeleton(chain, clip, limits).has_value(),
            "clip succeeds at exact aggregate item and output budgets");
        --limits.maximum_items;
        CheckError(ValidateClipAgainstSkeleton(chain, clip, limits),
            DecodeErrorCode::LimitExceeded,
            "clip rejects one item below its aggregate item budget");
        ++limits.maximum_items;
        --limits.maximum_output_bytes;
        CheckError(ValidateClipAgainstSkeleton(chain, clip, limits),
            DecodeErrorCode::LimitExceeded,
            "clip rejects one byte below its aggregate output budget");

        auto wrong_cardinality = clip;
        wrong_cardinality.keyframes[1].pose.joint_local_transforms.pop_back();
        CheckError(ValidateClipAgainstSkeleton(chain, wrong_cardinality),
            DecodeErrorCode::Malformed,
            "clip rejects a keyframe whose pose cardinality differs from the skeleton");

        auto non_finite_time = clip;
        non_finite_time.keyframes[0].sample_time = std::numeric_limits<float>::quiet_NaN();
        const auto time_result = ValidateClipAgainstSkeleton(chain, non_finite_time);
        CheckError(time_result, DecodeErrorCode::Malformed,
            "clip rejects a non-finite sample coordinate");
        Check(!time_result || (time_result.error().item_index.has_value() &&
                                  *time_result.error().item_index == 0),
            "clip sample failure identifies its keyframe item");

        auto non_finite_pose = clip;
        non_finite_pose.keyframes[1].pose.joint_local_transforms[0] = NonFiniteMatrix();
        CheckError(ValidateClipAgainstSkeleton(chain, non_finite_pose),
            DecodeErrorCode::Malformed,
            "clip rejects a non-finite keyframe pose transform");
    }
    {
        ClipIR oversized;
        oversized.keyframes.resize(kMaximumClipKeyframes + 1U);
        CheckError(ValidateClipAgainstSkeleton(SkeletonIR{}, oversized),
            DecodeErrorCode::LimitExceeded,
            "clip rejects keyframe counts above the fixed project ceiling");
    }
    {
        ClipIR long_name;
        long_name.name = "12345";
        DecodeLimits limits;
        limits.maximum_string_bytes = 4;
        CheckError(ValidateClipAgainstSkeleton(SkeletonIR{}, long_name, limits),
            DecodeErrorCode::LimitExceeded,
            "clip rejects a name above the caller string-byte budget");
    }

    return failures;
}
