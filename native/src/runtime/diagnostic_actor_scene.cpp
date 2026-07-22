#include "omega/runtime/diagnostic_actor_scene.h"

#include <array>
#include <cstdint>
#include <new>
#include <vector>

namespace omega::runtime
{
std::expected<asset::RenderMeshIR, std::string_view>
BuildProjectDiagnosticActorMesh() noexcept
{
    // Synthetic project clip-like coordinates. The triangle's X/Y centroid is the origin, its
    // extent stays inside the clip volume at every app-owned bounded translation, and its fixed
    // depth assigns no world, camera, material, or retail meaning.
    constexpr float extent = 1.0F / 32.0F;
    constexpr float half_extent = extent / 2.0F;
    constexpr float depth = 0.5F;
    constexpr std::array positions{
        asset::Float3IR{.x = -extent, .y = -half_extent, .z = depth},
        asset::Float3IR{.x = extent, .y = -half_extent, .z = depth},
        asset::Float3IR{.x = 0.0F, .y = extent, .z = depth},
    };
    constexpr std::array<std::uint32_t, kProjectDiagnosticActorTriangleIndexCount>
        triangle_indices{0U, 1U, 2U};

    static_assert(positions.size() == kProjectDiagnosticActorPositionCount);
    static_assert(triangle_indices.size() ==
                  kProjectDiagnosticActorTriangleIndexCount);
    static_assert(kProjectDiagnosticActorMeshLogicalBytes == 48U);

    try
    {
        return asset::RenderMeshIR{
            .positions = std::vector<asset::Float3IR>(
                positions.begin(), positions.end()),
            .triangle_indices = std::vector<std::uint32_t>(
                triangle_indices.begin(), triangle_indices.end()),
        };
    }
    catch (const std::bad_alloc&)
    {
        return std::unexpected(kProjectDiagnosticActorMeshAllocationError);
    }
}
} // namespace omega::runtime
