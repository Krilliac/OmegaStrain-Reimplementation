#include "omega/runtime/model_pose_evaluation.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::runtime
{
namespace
{
[[nodiscard]] asset::ModelIrError Error(
    const asset::DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> item_index = std::nullopt)
{
    return asset::ModelIrError{
        .code = code,
        .item_index = item_index,
        .message = std::move(message),
    };
}

[[nodiscard]] bool Add(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        return false;
    result = left + right;
    return true;
}

[[nodiscard]] bool Multiply(
    const std::uint64_t left, const std::uint64_t right, std::uint64_t& result) noexcept
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return false;
    result = left * right;
    return true;
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
[[nodiscard]] asset::ModelIrResult<asset::Matrix4x4IR> MultiplyFinite(
    const asset::Matrix4x4IR& left, const asset::Matrix4x4IR& right,
    const std::size_t joint_index)
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
                    "model pose composition produced a non-finite transform", joint_index));
            }
            result.row_major[(row * 4) + column] = static_cast<float>(value);
        }
    }
    return result;
}

template <typename LocalTransformAt>
[[nodiscard]] asset::ModelIrResult<asset::GlobalPoseIR> EvaluateLocalTransforms(
    const asset::SkeletonIR& skeleton, const std::size_t local_transform_count,
    LocalTransformAt&& local_transform_at, const asset::DecodeLimits& limits)
{
    const std::size_t joint_count = skeleton.joints.size();
    if (local_transform_count != joint_count)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "pose joint-transform count does not match the skeleton joint count"));
    }
    if (joint_count > asset::kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed evaluation ceiling"));
    }

    std::uint64_t item_count = 0;
    if (!Add(1, joint_count, item_count))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "pose evaluation item count overflows"));
    }
    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "pose evaluation exceeds the caller item budget"));
    }

    std::uint64_t transform_bytes = 0;
    std::uint64_t output_bytes = sizeof(asset::GlobalPoseIR);
    if (!Multiply(joint_count, sizeof(asset::Matrix4x4IR), transform_bytes) ||
        !Add(output_bytes, transform_bytes, output_bytes))
    {
        return std::unexpected(
            Error(asset::DecodeErrorCode::Overflow, "pose evaluation output size overflows"));
    }
    if (output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "pose evaluation exceeds the caller output-byte budget"));
    }

    asset::GlobalPoseIR global_pose;
    global_pose.joint_global_transforms.reserve(joint_count);
    for (std::size_t index = 0; index < joint_count; ++index)
    {
        const asset::JointIR& joint = skeleton.joints[index];
        const asset::Matrix4x4IR& local = local_transform_at(index);
        if (!IsFiniteMatrix(local))
        {
            return std::unexpected(Error(
                asset::DecodeErrorCode::Malformed, "pose local transform is not finite", index));
        }
        if (!joint.parent_index.has_value())
        {
            global_pose.joint_global_transforms.push_back(local);
            continue;
        }

        const std::uint32_t parent_index = *joint.parent_index;
        if (parent_index >= index)
        {
            return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
                "joint parent_index does not strictly precede its own index", index));
        }
        auto composed = MultiplyFinite(
            global_pose.joint_global_transforms[parent_index], local, index);
        if (!composed)
            return std::unexpected(composed.error());
        global_pose.joint_global_transforms.push_back(*composed);
    }
    return global_pose;
}
} // namespace

asset::ModelIrResult<asset::GlobalPoseIR> EvaluateBindPose(
    const asset::SkeletonIR& skeleton, const asset::DecodeLimits& limits)
{
    return EvaluateLocalTransforms(skeleton, skeleton.joints.size(),
        [&skeleton](const std::size_t index) -> const asset::Matrix4x4IR& {
            return skeleton.joints[index].local_bind_transform;
        },
        limits);
}

asset::ModelIrResult<asset::GlobalPoseIR> EvaluatePose(
    const asset::SkeletonIR& skeleton, const asset::PoseIR& pose,
    const asset::DecodeLimits& limits)
{
    return EvaluateLocalTransforms(skeleton, pose.joint_local_transforms.size(),
        [&pose](const std::size_t index) -> const asset::Matrix4x4IR& {
            return pose.joint_local_transforms[index];
        },
        limits);
}
} // namespace omega::runtime
