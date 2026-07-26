#include "omega/runtime/model_pose_evaluation.h"

#include <algorithm>
#include <array>
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

[[nodiscard]] bool IsFiniteFloat3(const asset::Float3IR& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] bool NarrowToFloat(const double value, float& narrowed) noexcept
{
    constexpr double float_max = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(value) || value > float_max || value < -float_max)
        return false;
    narrowed = static_cast<float>(value);
    return true;
}

// Mirrors the finite-checked composition used by omega::runtime::ComposeObjectToClip
// (scene_transform.cpp): standard row-major 4x4 product, rejecting a non-finite or
// out-of-float-range accumulator before it is ever narrowed back to float.
[[nodiscard]] std::optional<asset::Matrix4x4IR> MultiplyChecked(
    const asset::Matrix4x4IR& left, const asset::Matrix4x4IR& right) noexcept
{
    asset::Matrix4x4IR result;
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
            if (!NarrowToFloat(value, result.row_major[(row * 4) + column]))
                return std::nullopt;
        }
    }
    return result;
}

[[nodiscard]] asset::ModelIrResult<asset::Matrix4x4IR> MultiplyFinite(
    const asset::Matrix4x4IR& left, const asset::Matrix4x4IR& right,
    const std::size_t joint_index)
{
    const std::optional<asset::Matrix4x4IR> product = MultiplyChecked(left, right);
    if (!product)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "model pose composition produced a non-finite transform", joint_index));
    }
    return *product;
}

[[nodiscard]] double TransformPointComponent(const asset::Matrix4x4IR& matrix,
    const std::size_t row, const asset::Float3IR& position) noexcept
{
    return static_cast<double>(matrix.row_major[row * 4U]) *
               static_cast<double>(position.x) +
           static_cast<double>(matrix.row_major[row * 4U + 1U]) *
               static_cast<double>(position.y) +
           static_cast<double>(matrix.row_major[row * 4U + 2U]) *
               static_cast<double>(position.z) +
           static_cast<double>(matrix.row_major[row * 4U + 3U]);
}

template <typename LocalTransformAt>
[[nodiscard]] asset::ModelIrResult<asset::GlobalPoseIR> EvaluateLocalTransforms(
    const asset::SkeletonIR& skeleton, const std::size_t local_transform_count,
    LocalTransformAt&& local_transform_at, const asset::DecodeLimits& limits)
{
    const std::size_t joint_count = skeleton.joints.size();
    if (joint_count > asset::kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed evaluation ceiling"));
    }
    if (local_transform_count != joint_count)
    {
        return std::unexpected(Error(asset::DecodeErrorCode::Malformed,
            "pose joint-transform count does not match the skeleton joint count"));
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

std::expected<void, SkinningError> ComposeSkinningMatrices(
    const std::span<const asset::Matrix4x4IR> joint_global_transforms,
    const std::span<const asset::Matrix4x4IR> inverse_bind_transforms,
    const std::span<asset::Matrix4x4IR> skinning_matrices) noexcept
{
    if (joint_global_transforms.size() != inverse_bind_transforms.size() ||
        joint_global_transforms.size() != skinning_matrices.size())
    {
        return std::unexpected(SkinningError::CountMismatch);
    }
    if (joint_global_transforms.size() > asset::kMaximumSkeletonJoints)
        return std::unexpected(SkinningError::CapacityExceeded);

    std::array<asset::Matrix4x4IR, asset::kMaximumSkeletonJoints> staged{};
    for (std::size_t index = 0; index < joint_global_transforms.size(); ++index)
    {
        if (!IsFiniteMatrix(joint_global_transforms[index]) ||
            !IsFiniteMatrix(inverse_bind_transforms[index]))
        {
            return std::unexpected(SkinningError::NonFiniteInput);
        }
        const std::optional<asset::Matrix4x4IR> product =
            MultiplyChecked(joint_global_transforms[index], inverse_bind_transforms[index]);
        if (!product)
            return std::unexpected(SkinningError::NonFiniteResult);
        staged[index] = *product;
    }

    std::copy_n(staged.begin(), joint_global_transforms.size(), skinning_matrices.begin());
    return {};
}

asset::Float3IR SkinVertexPosition(const asset::Float3IR& bind_position,
    const std::span<const asset::Matrix4x4IR> skinning_matrices,
    const asset::SkinInfluenceIR& influences) noexcept
{
    if (!IsFiniteFloat3(bind_position))
        return bind_position;
    if (skinning_matrices.size() > asset::kMaximumSkeletonJoints ||
        static_cast<std::size_t>(influences.used_influences) >
            asset::kMaximumSkinInfluencesPerVertex)
    {
        return bind_position;
    }

    const std::size_t influence_count = static_cast<std::size_t>(influences.used_influences);
    for (std::size_t slot = 0U; slot < asset::kMaximumSkinInfluencesPerVertex; ++slot)
    {
        const float weight = influences.weights[slot];
        if (slot < influence_count)
        {
            if (!std::isfinite(weight) || weight < 0.0F ||
                static_cast<std::size_t>(influences.joint_indices[slot]) >=
                    skinning_matrices.size())
            {
                return bind_position;
            }
            continue;
        }
        if (influences.joint_indices[slot] != 0U || !std::isfinite(weight) ||
            weight != 0.0F || std::signbit(weight))
        {
            return bind_position;
        }
    }

    double accumulated_x = 0.0;
    double accumulated_y = 0.0;
    double accumulated_z = 0.0;
    double total_weight = 0.0;
    for (std::size_t slot = 0; slot < influence_count; ++slot)
    {
        const float weight = influences.weights[slot];
        const std::size_t joint_index =
            static_cast<std::size_t>(influences.joint_indices[slot]);
        const asset::Matrix4x4IR& matrix = skinning_matrices[joint_index];
        if (!IsFiniteMatrix(matrix))
            return bind_position;

        const double scaled_weight = static_cast<double>(weight);
        accumulated_x += scaled_weight * TransformPointComponent(matrix, 0U, bind_position);
        accumulated_y += scaled_weight * TransformPointComponent(matrix, 1U, bind_position);
        accumulated_z += scaled_weight * TransformPointComponent(matrix, 2U, bind_position);
        total_weight += scaled_weight;
    }

    if (!std::isfinite(total_weight) ||
        std::fabs(total_weight) < static_cast<double>(kMinimumSkinTotalWeight))
    {
        return bind_position;
    }

    asset::Float3IR skinned;
    if (!NarrowToFloat(accumulated_x / total_weight, skinned.x) ||
        !NarrowToFloat(accumulated_y / total_weight, skinned.y) ||
        !NarrowToFloat(accumulated_z / total_weight, skinned.z))
    {
        return bind_position;
    }
    return skinned;
}
} // namespace omega::runtime
