#include "omega/runtime/canonical_level_scene_materials.h"

#include <utility>

namespace omega::runtime
{
std::expected<CanonicalLevelSceneWithMaterials, std::string>
BuildCanonicalLevelSceneWithMaterials(
    const asset::LevelContentIR& content, const CanonicalLevelSceneLimits& limits)
{
    if (content.spatial.terrain_cells.size() != content.material_catalogs.terrain_cells.size())
        return std::unexpected(
            "canonical level scene materials require matching spatial and material cell counts");

    auto scene = BuildCanonicalLevelScene(content.spatial, limits);
    if (!scene)
        return std::unexpected(scene.error());

    return CanonicalLevelSceneWithMaterials{
        .scene = std::move(*scene),
        .material_catalogs = content.material_catalogs.terrain_cells,
    };
}
} // namespace omega::runtime
