#include "omega/asset/model_ir_validate.h"

#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <utility>

namespace omega::asset
{
namespace
{
[[nodiscard]] DecodeError Error(
    const DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> item_index = std::nullopt)
{
    return DecodeError{
        .code = code,
        .byte_offset = item_index,
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

[[nodiscard]] bool IsFiniteMatrix(const Matrix4x4IR& matrix) noexcept
{
    for (const float value : matrix.row_major)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}
} // namespace

DecodeResult<void> ValidateSkeletonIR(const SkeletonIR& skeleton, const DecodeLimits& limits)
{
    if (skeleton.joints.size() > kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed project ceiling"));
    }

    std::uint64_t item_count = 0;
    if (!Add(1, skeleton.joints.size(), item_count))
        return std::unexpected(Error(DecodeErrorCode::Overflow, "skeleton item count overflows"));
    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "skeleton item count exceeds the caller item budget"));
    }

    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const JointIR& joint = skeleton.joints[index];
        if (!IsFiniteMatrix(joint.local_bind_transform))
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "joint local_bind_transform is not finite", index));
        }
        if (joint.parent_index.has_value() && *joint.parent_index >= index)
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "joint parent_index does not strictly precede its own index", index));
        }
    }
    return {};
}

DecodeResult<void> ValidateModelIR(const ModelIR& model, const DecodeLimits& limits)
{
    if (model.mesh.triangle_indices.size() % 3 != 0)
    {
        return std::unexpected(Error(DecodeErrorCode::Malformed,
            "model mesh triangle_indices is not a whole number of triangles"));
    }
    for (const Float3IR& position : model.mesh.positions)
    {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "model mesh position is not finite"));
        }
    }
    for (const std::uint32_t index : model.mesh.triangle_indices)
    {
        if (index >= model.mesh.positions.size())
        {
            return std::unexpected(Error(DecodeErrorCode::InvalidReference,
                "model mesh triangle index is out of bounds"));
        }
    }

    std::uint64_t item_count = 1; // model root
    if (!Add(item_count, model.mesh.positions.size(), item_count) ||
        !Add(item_count, model.mesh.triangle_indices.size(), item_count))
    {
        return std::unexpected(Error(DecodeErrorCode::Overflow, "model item count overflows"));
    }

    if (model.skeleton.has_value())
    {
        const auto skeleton_result = ValidateSkeletonIR(*model.skeleton, limits);
        if (!skeleton_result)
            return std::unexpected(skeleton_result.error());
        if (!Add(item_count, model.skeleton->joints.size(), item_count))
            return std::unexpected(Error(DecodeErrorCode::Overflow, "model item count overflows"));
    }

    if (model.skin_influences.has_value())
    {
        if (!model.skeleton.has_value())
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "model skin_influences is present without an owned skeleton"));
        }
        const auto& influences = *model.skin_influences;
        if (influences.size() != model.mesh.positions.size())
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "model skin_influences size does not match mesh position count"));
        }
        const std::size_t joint_count = model.skeleton->joints.size();
        for (std::size_t vertex_index = 0; vertex_index < influences.size(); ++vertex_index)
        {
            const SkinInfluenceIR& influence = influences[vertex_index];
            if (static_cast<std::size_t>(influence.used_influences) >
                kMaximumSkinInfluencesPerVertex)
            {
                return std::unexpected(Error(DecodeErrorCode::Malformed,
                    "skin influence used_influences exceeds the fixed per-vertex ceiling",
                    vertex_index));
            }
            for (std::uint8_t slot = 0; slot < influence.used_influences; ++slot)
            {
                if (influence.joint_indices[slot] >= joint_count)
                {
                    return std::unexpected(Error(DecodeErrorCode::InvalidReference,
                        "skin influence joint index is outside the skeleton's range",
                        vertex_index));
                }
                if (!std::isfinite(influence.weights[slot]) || influence.weights[slot] < 0.0F)
                {
                    return std::unexpected(Error(DecodeErrorCode::Malformed,
                        "skin influence weight is not a finite non-negative value",
                        vertex_index));
                }
            }
        }
        if (!Add(item_count, influences.size(), item_count))
            return std::unexpected(Error(DecodeErrorCode::Overflow, "model item count overflows"));
    }

    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "model item count exceeds the caller item budget"));
    }
    return {};
}

DecodeResult<void> ValidatePoseAgainstSkeleton(
    const SkeletonIR& skeleton, const PoseIR& pose, const DecodeLimits& limits)
{
    if (pose.joint_local_transforms.size() != skeleton.joints.size())
    {
        return std::unexpected(Error(DecodeErrorCode::Malformed,
            "pose joint_local_transforms count does not match the skeleton joint count"));
    }

    std::uint64_t item_count = 0;
    if (!Add(1, pose.joint_local_transforms.size(), item_count))
        return std::unexpected(Error(DecodeErrorCode::Overflow, "pose item count overflows"));
    if (item_count > limits.maximum_items)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "pose item count exceeds the caller item budget"));
    }

    for (std::size_t index = 0; index < pose.joint_local_transforms.size(); ++index)
    {
        if (!IsFiniteMatrix(pose.joint_local_transforms[index]))
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "pose joint_local_transform is not finite", index));
        }
    }
    return {};
}
} // namespace omega::asset
