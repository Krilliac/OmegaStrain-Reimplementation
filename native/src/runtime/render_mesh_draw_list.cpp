#include "omega/runtime/render_mesh_draw_list.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace omega::runtime
{
namespace
{
[[nodiscard]] constexpr RenderMeshDrawListError Error(
    const RenderMeshDrawListErrorCode code) noexcept
{
    return RenderMeshDrawListError{
        .code = code,
        .message = RenderMeshDrawListErrorMessage(code),
    };
}

[[nodiscard]] bool IsFinite(const asset::Matrix4x4IR& matrix) noexcept
{
    for (const float value : matrix.row_major)
    {
        if (!std::isfinite(value))
            return false;
    }
    return true;
}

[[nodiscard]] constexpr bool IsValidRasterMode(const RenderMeshRasterMode mode) noexcept
{
    switch (mode)
    {
    case RenderMeshRasterMode::Fill:
    case RenderMeshRasterMode::Wireframe:
        return true;
    }
    return false;
}
} // namespace

RenderMeshDrawList::RenderMeshDrawList() noexcept
{
    static_assert(std::is_trivially_copyable_v<RenderMeshDrawCommand>);
    std::memset(static_cast<void*>(commands_.data()), 0, sizeof(commands_));
}

std::expected<RenderMeshDrawList, RenderMeshDrawListError> RenderMeshDrawList::Create(
    const std::span<const RenderMeshDrawCommand> commands) noexcept
{
    if (commands.size() > kMaximumRenderMeshDrawsPerFrame)
        return std::unexpected(Error(RenderMeshDrawListErrorCode::CapacityExceeded));

    RenderMeshDrawList result;
    for (std::size_t index = 0U; index < commands.size(); ++index)
    {
        const RenderMeshDrawCommand& command = commands[index];
        if (!command.mesh.valid())
            return std::unexpected(Error(RenderMeshDrawListErrorCode::InvalidMeshHandle));
        if (!IsFinite(command.object_to_clip))
            return std::unexpected(Error(RenderMeshDrawListErrorCode::NonFiniteTransform));
        if (!IsValidRasterMode(command.raster_mode))
            return std::unexpected(Error(RenderMeshDrawListErrorCode::InvalidRasterMode));
        result.commands_[index] = command;
    }
    result.count_ = static_cast<std::uint32_t>(commands.size());
    return result;
}
} // namespace omega::runtime
