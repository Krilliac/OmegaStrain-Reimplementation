#include "omega/runtime/canonical_level_scene_materials.h"

#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
int failures = 0;

void Check(const bool condition, const std::string_view message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
        ++failures;
    }
}

[[nodiscard]] omega::asset::SpatialMeshIR MakeTriangleMesh(const float scale = 1.0F)
{
    return omega::asset::SpatialMeshIR{
        .vertices = {
            {.x = 0.0F, .y = 0.0F, .z = 0.0F},
            {.x = scale, .y = 0.0F, .z = 0.0F},
            {.x = 0.0F, .y = scale, .z = 0.0F},
        },
        .triangles = {
            {.vertex_indices = {0U, 1U, 2U}},
        },
    };
}

[[nodiscard]] omega::asset::MaterialCatalogIR MakeCatalog(const std::string& name)
{
    return omega::asset::MaterialCatalogIR{
        .names = {name},
        .materials = {omega::asset::MaterialCatalogEntryIR{
            .name_indices = {0U, 0U, 0U},
            .name_count = 1U,
        }},
    };
}
} // namespace

int main()
{
    // Empty content.
    auto empty = omega::runtime::BuildCanonicalLevelSceneWithMaterials({});
    Check(empty && empty->scene.render_meshes.empty() && empty->material_catalogs.empty(),
        "empty level content produces an empty paired scene and material list");

    // Matching two-cell content: scene and materials line up index for index.
    omega::asset::LevelContentIR content{
        .spatial = {.terrain_cells = {MakeTriangleMesh(), MakeTriangleMesh(2.0F)}},
        .material_catalogs = {
            .terrain_cells = {MakeCatalog("cell-zero-name"), MakeCatalog("cell-one-name")}},
    };
    auto paired = omega::runtime::BuildCanonicalLevelSceneWithMaterials(content);
    Check(paired.has_value(), "matching cell counts build a paired canonical scene");
    if (paired)
    {
        Check(paired->scene.render_meshes.size() == 2U &&
                  paired->material_catalogs.size() == 2U,
            "scene mesh count and material catalog count both equal the manifest cell count");
        Check(paired->material_catalogs[0].names == std::vector<std::string>{"cell-zero-name"} &&
                  paired->material_catalogs[1].names ==
                      std::vector<std::string>{"cell-one-name"},
            "material catalogs retain source cell order, matching scene.mesh_instances[i]");
        Check(paired->scene.mesh_instances[0].render_mesh_index == 0U &&
                  paired->scene.mesh_instances[1].render_mesh_index == 1U,
            "the scene half keeps its own deterministic per-cell mesh index");
    }

    // Mismatched cardinality is rejected before any composition.
    omega::asset::LevelContentIR mismatched{
        .spatial = {.terrain_cells = {MakeTriangleMesh()}},
        .material_catalogs = {.terrain_cells = {MakeCatalog("a"), MakeCatalog("b")}},
    };
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(mismatched),
        "mismatched spatial and material cell counts are rejected");

    // Spatial-side validation failures propagate.
    omega::asset::LevelContentIR invalid_spatial{
        .spatial = {.terrain_cells = {MakeTriangleMesh()}},
        .material_catalogs = {.terrain_cells = {MakeCatalog("a")}},
    };
    invalid_spatial.spatial.terrain_cells[0].triangles[0].vertex_indices[0] = 99U;
    Check(!omega::runtime::BuildCanonicalLevelSceneWithMaterials(invalid_spatial),
        "an invalid spatial triangle index rejects the whole paired build");

    return failures;
}
