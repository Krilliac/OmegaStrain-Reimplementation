#pragma once

#include "omega/asset/scene_ir.h"

#include <cstdint>
#include <expected>

namespace omega::runtime
{
enum class SceneTransformError : std::uint8_t
{
    NonFiniteInput = 0U,
    NonFiniteResult,
};

// [any thread; reentrant] Composes the project-owned object-to-clip transform as
// view_to_clip * world_to_view * local_to_world. The calculation retains no references or state,
// performs no allocation, and rejects non-finite inputs and computed matrices explicitly.
[[nodiscard]] std::expected<asset::Matrix4x4IR, SceneTransformError> ComposeObjectToClip(
    const asset::SceneCameraIR& camera,
    const asset::Matrix4x4IR& local_to_world) noexcept;
} // namespace omega::runtime
