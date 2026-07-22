#pragma once

#include "omega/asset/level_content_ir.h"
#include "omega/asset/material_catalog_ir.h"
#include "omega/asset/scene_ir.h"
#include "omega/runtime/canonical_level_scene.h"

#include <expected>
#include <string>
#include <vector>

namespace omega::runtime
{
// A canonical level scene paired with the material/name catalog decoded from the same source cell
// as each mesh instance. `material_catalogs[i]` corresponds to `scene.mesh_instances[i]` (and to
// `scene.render_meshes[i]`, since `BuildCanonicalLevelScene` keeps that 1:1): both were decoded from
// the same manifest terrain cell in the same GameDataService pass. This proves only coherent
// per-cell ownership — the same property `LevelContentIR` itself documents. It asserts no
// mesh-to-material, triangle, vertex, texture, or visibility binding; name roles remain unassigned
// until the render/material binding grammar is independently proven.
struct CanonicalLevelSceneWithMaterials
{
    asset::SceneIR scene;
    std::vector<asset::MaterialCatalogIR> material_catalogs;
};

// [any worker thread; reentrant] Builds a canonical level scene from `content.spatial` and pairs it,
// index for index, with `content.material_catalogs`. Fails closed if the two collections do not
// share the cardinality `LevelContentIR` documents, and otherwise fails exactly as
// `BuildCanonicalLevelScene` would for the spatial half.
[[nodiscard]] std::expected<CanonicalLevelSceneWithMaterials, std::string>
BuildCanonicalLevelSceneWithMaterials(const asset::LevelContentIR& content,
    const CanonicalLevelSceneLimits& limits = CanonicalLevelSceneLimits{});
} // namespace omega::runtime
