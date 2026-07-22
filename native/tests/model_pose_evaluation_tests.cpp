#include "omega/runtime/model_pose_evaluation.h"

#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace
{
using omega::asset::DecodeErrorCode;
using omega::asset::DecodeLimits;
using omega::asset::JointIR;
using omega::asset::kIdentityMatrix4x4IR;
using omega::asset::kMaximumSkeletonJoints;
using omega::asset::Matrix4x4IR;
using omega::asset::PoseIR;
using omega::asset::SkeletonIR;
using omega::runtime::EvaluateBindPose;
using omega::runtime::EvaluatePose;

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

[[nodiscard]] Matrix4x4IR Translation(const float x, const float y, const float z)
{
    Matrix4x4IR matrix = kIdentityMatrix4x4IR;
    matrix.row_major[3] = x;
    matrix.row_major[7] = y;
    matrix.row_major[11] = z;
    return matrix;
}

[[nodiscard]] bool IsTranslation(
    const Matrix4x4IR& matrix, const float x, const float y, const float z)
{
    return matrix == Translation(x, y, z);
}

// root -> child -> grandchild chain: local translations (1,0,0), (0,2,0), (0,0,3).
[[nodiscard]] SkeletonIR MakeTranslationChain()
{
    SkeletonIR skeleton;
    JointIR root;
    root.local_bind_transform = Translation(1.0F, 0.0F, 0.0F);
    skeleton.joints.push_back(root);

    JointIR child;
    child.parent_index = 0;
    child.local_bind_transform = Translation(0.0F, 2.0F, 0.0F);
    skeleton.joints.push_back(child);

    JointIR grandchild;
    grandchild.parent_index = 1;
    grandchild.local_bind_transform = Translation(0.0F, 0.0F, 3.0F);
    skeleton.joints.push_back(grandchild);

    return skeleton;
}
} // namespace

int ModelPoseEvaluationFailureCount()
{
    // Empty skeleton composes to an empty world pose.
    {
        const auto empty = EvaluateBindPose(SkeletonIR{});
        Check(empty.has_value() && empty->joint_world_transforms.empty(),
            "bind pose of an empty skeleton is an empty world pose");
    }

    // A single root joint's world transform equals its own local bind transform.
    {
        SkeletonIR single;
        single.joints.push_back(JointIR{.local_bind_transform = Translation(5.0F, 0.0F, 0.0F)});
        const auto pose = EvaluateBindPose(single);
        Check(pose.has_value() && pose->joint_world_transforms.size() == 1 &&
                  IsTranslation(pose->joint_world_transforms[0], 5.0F, 0.0F, 0.0F),
            "a root joint's bind-pose world transform equals its own local transform");
    }

    // A three-joint translation chain composes additively along the parent chain.
    {
        const auto chain = MakeTranslationChain();
        const auto pose = EvaluateBindPose(chain);
        Check(pose.has_value() && pose->joint_world_transforms.size() == 3, "chain bind pose has 3 joints");
        if (pose)
        {
            Check(IsTranslation(pose->joint_world_transforms[0], 1.0F, 0.0F, 0.0F),
                "chain root world transform is its own local translation");
            Check(IsTranslation(pose->joint_world_transforms[1], 1.0F, 2.0F, 0.0F),
                "chain child world transform accumulates parent and local translation");
            Check(IsTranslation(pose->joint_world_transforms[2], 1.0F, 2.0F, 3.0F),
                "chain grandchild world transform accumulates the full parent chain");
        }

        // Determinism: repeated evaluation of the same input yields an equal result.
        const auto repeat = EvaluateBindPose(chain);
        Check(pose.has_value() && repeat.has_value() && *pose == *repeat,
            "bind pose evaluation is deterministic across repeated calls");
    }

    // EvaluatePose composes a caller-supplied local pose instead of the skeleton's own bind
    // transforms, distinguishing the generic evaluator from EvaluateBindPose.
    {
        const auto chain = MakeTranslationChain();
        PoseIR generic_pose;
        generic_pose.joint_local_transforms = {
            Translation(0.0F, 0.0F, 0.0F),
            Translation(10.0F, 0.0F, 0.0F),
            Translation(0.0F, 0.0F, 0.0F),
        };
        const auto world = EvaluatePose(chain, generic_pose);
        Check(world.has_value() && world->joint_world_transforms.size() == 3 &&
                  IsTranslation(world->joint_world_transforms[1], 10.0F, 0.0F, 0.0F),
            "the generic evaluator composes the supplied local pose, not the skeleton's bind pose");
    }

    // Joint-count mismatch is rejected.
    {
        const auto chain = MakeTranslationChain();
        PoseIR undersized;
        undersized.joint_local_transforms = {kIdentityMatrix4x4IR, kIdentityMatrix4x4IR};
        CheckError(EvaluatePose(chain, undersized), DecodeErrorCode::Malformed,
            "pose evaluation rejects a joint-transform count mismatch");
    }

    // A non-finite local transform is rejected even without prior validation.
    {
        SkeletonIR single;
        single.joints.push_back(JointIR{});
        PoseIR non_finite;
        Matrix4x4IR nan_matrix = kIdentityMatrix4x4IR;
        nan_matrix.row_major[0] = std::numeric_limits<float>::quiet_NaN();
        non_finite.joint_local_transforms = {nan_matrix};
        CheckError(EvaluatePose(single, non_finite), DecodeErrorCode::Malformed,
            "pose evaluation rejects a non-finite local transform");
    }

    // A parent_index that does not strictly precede its own index is rejected even without
    // prior structural validation (defense in depth, not reliance on ValidateSkeletonIR).
    {
        SkeletonIR self_ref;
        self_ref.joints.push_back(JointIR{.parent_index = 0});
        PoseIR pose;
        pose.joint_local_transforms = {kIdentityMatrix4x4IR};
        CheckError(EvaluatePose(self_ref, pose), DecodeErrorCode::Malformed,
            "pose evaluation rejects a self-referencing parent index");
    }

    // Joint counts above the fixed ceiling are rejected.
    {
        SkeletonIR oversized;
        oversized.joints.assign(kMaximumSkeletonJoints + 1U, JointIR{});
        CheckError(EvaluateBindPose(oversized), DecodeErrorCode::LimitExceeded,
            "bind pose evaluation rejects joint counts above the fixed ceiling");
    }

    // Caller item-budget boundaries.
    {
        const auto chain = MakeTranslationChain();
        DecodeLimits limits;
        limits.maximum_items = 1 + chain.joints.size();
        Check(EvaluateBindPose(chain, limits).has_value(),
            "bind pose succeeds at the exact root-plus-joints item budget");
        limits.maximum_items = chain.joints.size();
        CheckError(EvaluateBindPose(chain, limits), DecodeErrorCode::LimitExceeded,
            "bind pose rejects one item below the root-plus-joints item budget");
    }

    // Caller output-byte-budget boundaries.
    {
        const auto chain = MakeTranslationChain();
        DecodeLimits limits;
        limits.maximum_output_bytes = chain.joints.size() * sizeof(Matrix4x4IR);
        Check(EvaluateBindPose(chain, limits).has_value(),
            "bind pose succeeds at the exact output-byte budget");
        limits.maximum_output_bytes = (chain.joints.size() * sizeof(Matrix4x4IR)) - 1;
        CheckError(EvaluateBindPose(chain, limits), DecodeErrorCode::LimitExceeded,
            "bind pose rejects one byte below the output-byte budget");
    }

    return failures;
}
