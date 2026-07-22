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
struct Usage
{
    std::uint64_t items = 0;
    std::uint64_t output_bytes = 0;
};

[[nodiscard]] ModelIrError Error(
    const DecodeErrorCode code, std::string message,
    const std::optional<std::uint64_t> item_index = std::nullopt)
{
    return ModelIrError{
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

[[nodiscard]] bool AddArrayBytes(
    std::uint64_t& total, const std::uint64_t count, const std::uint64_t item_size) noexcept
{
    std::uint64_t bytes = 0;
    return Multiply(count, item_size, bytes) && Add(total, bytes, total);
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

[[nodiscard]] ModelIrResult<void> CheckUsage(
    const Usage& usage, const DecodeLimits& limits, const std::string& subject)
{
    if (usage.items > limits.maximum_items)
    {
        return std::unexpected(Error(
            DecodeErrorCode::LimitExceeded, subject + " exceeds the caller item budget"));
    }
    if (usage.output_bytes > limits.maximum_output_bytes)
    {
        return std::unexpected(Error(
            DecodeErrorCode::LimitExceeded, subject + " exceeds the caller output-byte budget"));
    }
    return {};
}

[[nodiscard]] ModelIrResult<Usage> MeasureSkeleton(
    const SkeletonIR& skeleton, const DecodeLimits& limits)
{
    if (skeleton.joints.size() > kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed project ceiling"));
    }

    Usage usage{
        .items = 1,
        .output_bytes = sizeof(SkeletonIR),
    };
    if (!Add(usage.items, skeleton.joints.size(), usage.items) ||
        !AddArrayBytes(usage.output_bytes, skeleton.joints.size(), sizeof(JointIR)))
    {
        return std::unexpected(Error(DecodeErrorCode::Overflow, "skeleton usage overflows"));
    }

    for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
    {
        const auto& name = skeleton.joints[index].name;
        if (name.size() > limits.maximum_string_bytes)
        {
            return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
                "joint name exceeds the caller string-byte budget", index));
        }
        if (!Add(usage.output_bytes, name.size(), usage.output_bytes))
        {
            return std::unexpected(
                Error(DecodeErrorCode::Overflow, "skeleton output size overflows", index));
        }
    }
    return usage;
}
} // namespace

ModelIrResult<void> ValidateSkeletonIR(const SkeletonIR& skeleton, const DecodeLimits& limits)
{
    const auto usage = MeasureSkeleton(skeleton, limits);
    if (!usage)
        return std::unexpected(usage.error());
    if (auto budget = CheckUsage(*usage, limits, "skeleton"); !budget)
        return budget;

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

ModelIrResult<void> ValidateModelIR(const ModelIR& model, const DecodeLimits& limits)
{
    if (model.mesh.triangle_indices.size() % 3 != 0)
    {
        return std::unexpected(Error(DecodeErrorCode::Malformed,
            "model mesh triangle_indices is not a whole number of triangles"));
    }
    if (model.skin_influences.has_value() && !model.skeleton.has_value())
    {
        return std::unexpected(Error(DecodeErrorCode::Malformed,
            "model skin_influences is present without an owned skeleton"));
    }
    if (model.skin_influences.has_value() &&
        model.skin_influences->size() != model.mesh.positions.size())
    {
        return std::unexpected(Error(DecodeErrorCode::Malformed,
            "model skin_influences size does not match mesh position count"));
    }

    Usage usage{
        .items = 1,
        .output_bytes = sizeof(ModelIR),
    };
    if (!Add(usage.items, model.mesh.positions.size(), usage.items) ||
        !Add(usage.items, model.mesh.triangle_indices.size(), usage.items) ||
        !AddArrayBytes(
            usage.output_bytes, model.mesh.positions.size(), sizeof(Float3IR)) ||
        !AddArrayBytes(usage.output_bytes, model.mesh.triangle_indices.size(),
            sizeof(std::uint32_t)))
    {
        return std::unexpected(Error(DecodeErrorCode::Overflow, "model usage overflows"));
    }

    if (model.skeleton.has_value())
    {
        const auto skeleton_usage = MeasureSkeleton(*model.skeleton, limits);
        if (!skeleton_usage)
            return std::unexpected(skeleton_usage.error());
        if (!Add(usage.items, skeleton_usage->items, usage.items) ||
            !AddArrayBytes(usage.output_bytes, model.skeleton->joints.size(), sizeof(JointIR)))
        {
            return std::unexpected(Error(DecodeErrorCode::Overflow, "model usage overflows"));
        }
        for (std::size_t index = 0; index < model.skeleton->joints.size(); ++index)
        {
            if (!Add(usage.output_bytes, model.skeleton->joints[index].name.size(),
                    usage.output_bytes))
            {
                return std::unexpected(
                    Error(DecodeErrorCode::Overflow, "model output size overflows", index));
            }
        }
    }

    if (model.skin_influences.has_value())
    {
        if (!Add(usage.items, model.skin_influences->size(), usage.items) ||
            !AddArrayBytes(usage.output_bytes, model.skin_influences->size(),
                sizeof(SkinInfluenceIR)))
        {
            return std::unexpected(Error(DecodeErrorCode::Overflow, "model usage overflows"));
        }
    }

    if (auto budget = CheckUsage(usage, limits, "model"); !budget)
        return budget;

    if (model.skeleton.has_value())
    {
        const auto skeleton_result = ValidateSkeletonIR(*model.skeleton, limits);
        if (!skeleton_result)
            return std::unexpected(skeleton_result.error());
    }

    for (std::size_t index = 0; index < model.mesh.positions.size(); ++index)
    {
        const Float3IR& position = model.mesh.positions[index];
        if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
            !std::isfinite(position.z))
        {
            return std::unexpected(Error(
                DecodeErrorCode::Malformed, "model mesh position is not finite", index));
        }
    }
    for (std::size_t index = 0; index < model.mesh.triangle_indices.size(); ++index)
    {
        if (model.mesh.triangle_indices[index] >= model.mesh.positions.size())
        {
            return std::unexpected(Error(DecodeErrorCode::InvalidReference,
                "model mesh triangle index is out of bounds", index));
        }
    }

    if (model.skin_influences.has_value())
    {
        const auto& influences = *model.skin_influences;
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
                        "skin influence weight is not a finite non-negative value", vertex_index));
                }
            }
            for (std::size_t slot = influence.used_influences;
                slot < kMaximumSkinInfluencesPerVertex; ++slot)
            {
                if (influence.joint_indices[slot] != 0 ||
                    !std::isfinite(influence.weights[slot]) ||
                    influence.weights[slot] != 0.0F || std::signbit(influence.weights[slot]))
                {
                    return std::unexpected(Error(DecodeErrorCode::Malformed,
                        "unused skin influence slot is not canonical zero", vertex_index));
                }
            }
        }
    }
    return {};
}

ModelIrResult<void> ValidatePoseAgainstSkeleton(
    const SkeletonIR& skeleton, const PoseIR& pose, const DecodeLimits& limits)
{
    if (skeleton.joints.size() > kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed project ceiling"));
    }
    if (pose.joint_local_transforms.size() != skeleton.joints.size())
    {
        return std::unexpected(Error(DecodeErrorCode::Malformed,
            "pose joint_local_transforms count does not match the skeleton joint count"));
    }

    Usage usage{
        .items = 1,
        .output_bytes = sizeof(PoseIR),
    };
    if (!Add(usage.items, pose.joint_local_transforms.size(), usage.items) ||
        !AddArrayBytes(usage.output_bytes, pose.joint_local_transforms.size(),
            sizeof(Matrix4x4IR)))
    {
        return std::unexpected(Error(DecodeErrorCode::Overflow, "pose usage overflows"));
    }
    if (auto budget = CheckUsage(usage, limits, "pose"); !budget)
        return budget;

    for (std::size_t index = 0; index < pose.joint_local_transforms.size(); ++index)
    {
        if (!IsFiniteMatrix(pose.joint_local_transforms[index]))
        {
            return std::unexpected(Error(
                DecodeErrorCode::Malformed, "pose joint_local_transform is not finite", index));
        }
    }
    return {};
}

ModelIrResult<void> ValidateClipAgainstSkeleton(
    const SkeletonIR& skeleton, const ClipIR& clip, const DecodeLimits& limits)
{
    if (skeleton.joints.size() > kMaximumSkeletonJoints)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "skeleton joint count exceeds the fixed project ceiling"));
    }
    if (clip.keyframes.size() > kMaximumClipKeyframes)
    {
        return std::unexpected(Error(DecodeErrorCode::LimitExceeded,
            "clip keyframe count exceeds the fixed project ceiling"));
    }
    if (clip.name.size() > limits.maximum_string_bytes)
    {
        return std::unexpected(
            Error(DecodeErrorCode::LimitExceeded, "clip name exceeds the caller string-byte budget"));
    }

    Usage usage{
        .items = 1,
        .output_bytes = sizeof(ClipIR),
    };
    if (!Add(usage.items, clip.keyframes.size(), usage.items) ||
        !Add(usage.output_bytes, clip.name.size(), usage.output_bytes) ||
        !AddArrayBytes(
            usage.output_bytes, clip.keyframes.size(), sizeof(ClipKeyframeIR)))
    {
        return std::unexpected(Error(DecodeErrorCode::Overflow, "clip usage overflows"));
    }

    for (std::size_t keyframe_index = 0; keyframe_index < clip.keyframes.size(); ++keyframe_index)
    {
        const auto transform_count = clip.keyframes[keyframe_index].pose.joint_local_transforms.size();
        if (transform_count != skeleton.joints.size())
        {
            return std::unexpected(Error(DecodeErrorCode::Malformed,
                "clip keyframe pose count does not match the skeleton joint count",
                keyframe_index));
        }
        if (!Add(usage.items, transform_count, usage.items) ||
            !AddArrayBytes(usage.output_bytes, transform_count, sizeof(Matrix4x4IR)))
        {
            return std::unexpected(
                Error(DecodeErrorCode::Overflow, "clip usage overflows", keyframe_index));
        }
    }
    if (auto budget = CheckUsage(usage, limits, "clip"); !budget)
        return budget;

    for (std::size_t keyframe_index = 0; keyframe_index < clip.keyframes.size(); ++keyframe_index)
    {
        const ClipKeyframeIR& keyframe = clip.keyframes[keyframe_index];
        if (!std::isfinite(keyframe.sample_time))
        {
            return std::unexpected(Error(
                DecodeErrorCode::Malformed, "clip sample_time is not finite", keyframe_index));
        }
        for (const Matrix4x4IR& transform : keyframe.pose.joint_local_transforms)
        {
            if (!IsFiniteMatrix(transform))
            {
                return std::unexpected(Error(DecodeErrorCode::Malformed,
                    "clip keyframe pose contains a non-finite transform", keyframe_index));
            }
        }
    }
    return {};
}
} // namespace omega::asset
