#include "omega/runtime/scene_transform.h"

#include <cmath>
#include <cstddef>
#include <limits>

namespace omega::runtime
{
namespace
{
[[nodiscard]] bool IsFinite(const asset::Matrix4x4IR& matrix) noexcept
{
    for (const float value : matrix.row_major)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

[[nodiscard]] std::expected<asset::Matrix4x4IR, SceneTransformError> MultiplyMatrices(
    const asset::Matrix4x4IR& left, const asset::Matrix4x4IR& right) noexcept
{
    asset::Matrix4x4IR result;
    constexpr double float_max =
        static_cast<double>(std::numeric_limits<float>::max());
    for (std::size_t row = 0U; row < 4U; ++row)
    {
        for (std::size_t column = 0U; column < 4U; ++column)
        {
            double value = 0.0;
            for (std::size_t inner = 0U; inner < 4U; ++inner)
            {
                value += static_cast<double>(left.row_major[row * 4U + inner]) *
                         static_cast<double>(right.row_major[inner * 4U + column]);
            }
            if (!std::isfinite(value) || value > float_max || value < -float_max)
                return std::unexpected(SceneTransformError::NonFiniteResult);
            result.row_major[row * 4U + column] = static_cast<float>(value);
        }
    }
    return result;
}
} // namespace

std::expected<asset::Matrix4x4IR, SceneTransformError> ComposeObjectToClip(
    const asset::SceneCameraIR& camera,
    const asset::Matrix4x4IR& local_to_world) noexcept
{
    if (!IsFinite(camera.world_to_view) || !IsFinite(camera.view_to_clip) ||
        !IsFinite(local_to_world))
    {
        return std::unexpected(SceneTransformError::NonFiniteInput);
    }

    const auto object_to_view =
        MultiplyMatrices(camera.world_to_view, local_to_world);
    if (!object_to_view)
        return std::unexpected(object_to_view.error());

    const auto object_to_clip =
        MultiplyMatrices(camera.view_to_clip, *object_to_view);
    if (!object_to_clip)
        return std::unexpected(object_to_clip.error());

    return *object_to_clip;
}
} // namespace omega::runtime
