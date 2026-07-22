#include "omega/runtime/model_pose_evaluation.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace omega::runtime
{
namespace
{
[[nodiscard]] asset::DecodeError Error(
    const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> item_index = std::nullopt)
{
    return asset::DecodeError{
        .code = code,
        .byte_offset = item_index,
        .message = std::move(message),
    };
}

[[nodiscard]] bool IsFiniteMatrix(const asset::Matrix4x4IR& matrix) noexcept
{
    for (const float value : matrix.row_major)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

// Mirrors the finite-checked composition used by omega::runtime::ComposeObjectToClip
// (scene_transform.cpp): standard row-major 4x4 product, rejecting a non-finite or
// out-of-float-range accumulator before it is ever narrowed back to float.
[[nodiscard]] asset::DecodeResult<asset::Matrix4x4IR> MultiplyFinite(
    const asset::Matrix4x4IR& left, const asset::Matrix4x4IR& right)
{
    asset::Matrix4x4IR result;
    constexpr double float_max = static_cast<double>(std::numeric_limits<float>::max());
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t column = 0; column < 4; ++column)
        {
            double value = 0.0;
            for (std::size_t inner = 0; inner < 4; ++inner)
            {
                value += static_cast<double>(left.row_major[(row * 4) + inner]) *
                         static_cast<double>(right.row_major[(inner * 4) + column]);
            }
            if (!std::isfinite(value) || value > float_max || value < -float_max)
            {
                return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                    "model pose composition produced a non-finite transform"));
            }
            result.row_major[(row * 4) + column] = static_cast<float>(value);
        }
    }
    return result;
}

[[nodiscard]] asset::DecodeResult<asset::WorldPoseIR> EvaluateLocalTransforms(
    const asset::SkeletonIR& skeleton, const std::vector<asset::Matrix4x4IR>& local_transforms,
    const asset::DecodeLimits& limits)
{
    const std::size_t joint_count = skeleton.joints.size();
    if (local_transforms.size() != joint_count)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "pose joint-transform count does not match the skeleton joint count"));
    }
    if (joint_count > asset::kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed evaluation ceiling"));
    }

    const auto item_count = static_cast<std::uint64_t>(joint_count) + 1U; // root
    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "pose evaluation exceeds the caller item budget"));
    }
    const auto output_bytes =
        static_cast<std::uint64_t>(joint_count) * sizeof(asset::Matrix4x4IR);
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "pose evaluation exceeds the caller output-byte budget"));
    }

    asset::WorldPoseIR world_pose;
    world_pose.joint_world_transforms.reserve(joint_count);
    for (std::size_t index = 0; index < joint_count; ++index)
    {
        const asset::JointIR& joint = skeleton.joints[index];
        const asset::Matrix4x4IR& local = local_transforms[index];
        if (!IsFiniteMatrix(local))
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "pose local transform is not finite", index));
        }
        if (!joint.parent_index.has_value())
        {
            world_pose.joint_world_transforms.push_back(local);
            continue;
        }

        const std::uint32_t parent_index = *joint.parent_index;
        if (parent_index >= index)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "joint parent_index does not strictly precede its own index", index));
        }
        auto composed = MultiplyFinite(world_pose.joint_world_transforms[parent_index], local);
        if (!composed)
            return std::unexpected(composed.error());
        world_pose.joint_world_transforms.push_back(*composed);
    }
    return world_pose;
}
} // namespace

asset::DecodeResult<asset::WorldPoseIR> EvaluateBindPose(
    const asset::SkeletonIR& skeleton, const asset::DecodeLimits& limits)
{
    std::vector<asset::Matrix4x4IR> local_transforms;
    local_transforms.reserve(skeleton.joints.size());
    for (const asset::JointIR& joint : skeleton.joints)
        local_transforms.push_back(joint.local_bind_transform);
    return EvaluateLocalTransforms(skeleton, local_transforms, limits);
}

asset::DecodeResult<asset::WorldPoseIR> EvaluatePose(
    const asset::SkeletonIR& skeleton, const asset::PoseIR& pose,
    const asset::DecodeLimits& limits)
{
    return EvaluateLocalTransforms(skeleton, pose.joint_local_transforms, limits);
}
} // namespace omega::runtime
